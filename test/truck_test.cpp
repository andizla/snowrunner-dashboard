// truck_test.cpp: the truck reader (src\truck.h) against a made up game: blocks of memory laid out as the game lays
// out its objects, read through the same TruckMemory the mod reads the game with. It checks that the truck is found
// and read, that each gearbox position gets its letters, when the cluster is on screen, and that a truck that is
// changed, freed or makes no sense is not drawn. It also checks that the engine's record is found by its name as the
// game finds it, and that engine damage is written only where it belongs: up, one short of what the engine takes at
// most, with the effect and the torque factor the game's own rule gives. And it checks which truck the mod takes a
// truck for: by its kind's name where the game has one, by its build where it has none.
// And it checks the maps by the game's names with the map its save names (maps.h), the search for the game's gearbox
// functions in a made up exe's code (game_code.h), and the hooks on them (gearbox_hooks.h).
// build.bat makes out\test\truck_test.exe; test.bat runs it.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "gauges.h"
#include "truck.h"
#include "models.h"
#include "maps.h"
#include "game_code.h"
#include "game_globals.h"
#include "clock_scan.h"
#include "suspension_scan.h"
#include "suspension_step.h"
#include "gearbox_hooks.h"

// The hooks' attach and detach (gearbox_hooks.h) on a function of the test's own: the hook counts and calls through.
static volatile int g_hookSaw = 0;
static int (*g_testWay)(int, int) = nullptr;
__declspec(noinline) static int TestTarget(int a, int b)
{
    volatile int x = a;
    return x * 3 + b;
}
static int TestHook(int a, int b)
{
    g_hookSaw = g_hookSaw + 1;
    return g_testWay(a, b);
}
// a second function for GearboxHook's choice (never called while hooked: the game's hooks take other arguments)
__declspec(noinline) static int TestTarget2(int a, int b)
{
    volatile int x = b;
    return x * 7 - a;
}

// A stand-in for the game's truck update (review 10, D7): two pointers and two floats as the game's homes them, and
// two things the hook does not declare: a fifth argument on the stack and an answer in xmm0. Both reach the caller
// only while the hook's call through stays a tail jump (D2).
static volatile uint64_t g_fakeVehicle = 0;
__declspec(noinline) static float FakeTruckUpdate(void *first, void *second, float third, float fourth, int fifth)
{
    g_fakeVehicle = *(volatile uint64_t *)((uint8_t *)first + 0x20);
    return third + fourth + (float)fifth + (second ? 0.5f : 0.0f);
}
static volatile LONG64 g_frameFirst = 0;
static void FakeOnFrame(void *first) { g_frameFirst = (LONG64)first; }

// A stand-in for the game's wheels' drive (the creep's hook): the action, the throttle and the step's seconds, as
// the game passes them; what it got is kept.
static volatile float g_driveThrottle = 0.0f, g_driveDt = 0.0f;
static void *volatile g_driveAction = nullptr;
__declspec(noinline) static void FakeDrive(void *action, float throttle, float dt)
{
    g_driveAction = action;
    g_driveThrottle = throttle;
    g_driveDt = dt;
}

// A stand-in for the step probe's look (suspension_step.h, SuspStepLook): what the hook handed it is kept.
static volatile uint64_t g_lookAction = 0;
static volatile float g_lookDt = 0.0f;
static void FakeStepLook(uint64_t action, float dt)
{
    g_lookAction = action;
    g_lookDt = dt;
}

// This process's own memory as the game's, for the physics thread's code run on objects of the test's own.
static bool NativePeek(uint64_t addr, void *out, size_t size) { memcpy(out, (const void *)(uintptr_t)addr, size); return true; }
static bool NativePoke(uint64_t addr, const void *in, size_t size) { memcpy((void *)(uintptr_t)addr, in, size); return true; }
static const TruckMemory kNativeMemory = { NativePeek, 0, NativePoke };

struct Region { uint64_t at; std::vector<uint8_t> bytes; };
static std::vector<Region> g_game;

static bool Peek(uint64_t addr, void *out, size_t size)
{
    for (const Region &r : g_game)
        if (addr >= r.at && addr + size <= r.at + r.bytes.size()) { memcpy(out, r.bytes.data() + (addr - r.at), size); return true; }
    return false;
}

template <typename T> static void Put(uint64_t addr, const T &v)
{
    for (Region &r : g_game)
        if (addr >= r.at && addr + sizeof v <= r.at + r.bytes.size()) { memcpy(r.bytes.data() + (addr - r.at), &v, sizeof v); return; }
    printf("FAIL the test writes outside its made up game at %llx\n", addr);
    exit(1);
}

static bool Poke(uint64_t addr, const void *in, size_t size)
{
    for (Region &r : g_game)
        if (addr >= r.at && addr + size <= r.at + r.bytes.size()) { memcpy(r.bytes.data() + (addr - r.at), in, size); return true; }
    return false;
}

// A text as the game keeps it, 0x20 bytes at `at`: the characters in place when there are fewer than 16, else at chars.
static void PutText(uint64_t at, const char *text, uint64_t chars)
{
    const uint64_t size = strlen(text), room = size < 16 ? 15 : size;
    uint8_t s[0x20] = {};
    if (size < 16) memcpy(s, text, size);
    else
    {
        for (uint64_t i = 0; i < size; i++) Put(chars + i, text[i]);
        memcpy(s, &chars, 8);
    }
    memcpy(s + 0x10, &size, 8);
    memcpy(s + 0x18, &room, 8);
    Put(at, s);
}

static void Block(uint64_t at, size_t size) { g_game.push_back({ at, std::vector<uint8_t>(size) }); }
static void Drop(uint64_t at)
{
    for (size_t i = 0; i < g_game.size(); i++)
        if (g_game[i].at == at) { g_game.erase(g_game.begin() + i); return; }
}

static const uint64_t kExe = 0x140000000ull, kControl = 0x10000000, kVehicle = 0x10010000, kAction = 0x10020000, kModel = 0x10030000, kChassis = 0x10040000,
                      kWheelList = 0x10050000, kWheelModel = 0x10060000, kWheel = 0x10070000, kSpeeds = 0x10080000, kDamage = 0x10090000, kGameObject = 0x100A0000,
                      kInput = 0x100B0000, kMap = 0x100C0000, kOtherVehicle = 0x100D0000, kTexts = 0x100E0000, kSets = 0x100F0000, kTree = 0x10100000;
static const TruckMemory kMemory = { Peek, kExe, Poke };
// The made up truck's kind and the game's id for it, as the game holds them: the kind a text at model + 0x588, the
// id behind a pointer at vehicle + 0x48 with its length before the pointer and its room after it.
static const char *const kKind = "mod_suv/1000001", *const kId = "{8E42BF0A-F7C6-40f9-B0F2-225DE74CE031}";
static const uint64_t kIdChars = kTexts + 0x300;
static void PutId(const char *id)
{
    for (uint64_t i = 0; i <= strlen(id); i++) Put(kIdChars + i, id[i]);
    Put(kVehicle + 0x40, (uint64_t)strlen(id));
    Put(kVehicle + 0x48, kIdChars);
    Put(kVehicle + 0x50, (uint64_t)strlen(id) + 1);
}
// The made up game's engine sets: the list's head and its three entries, and the nodes of the two trees. The truck's
// engine is the last node of the second tree.
static const uint64_t kSetEntry[3] = { kSets + 0x40, kSets + 0x80, kSets + 0xC0 }, kOldHead = kTree, kOldNode = kTree + 0x100, kNewHead = kTree + 0x200,
                      kNodeA = kTree + 0x300, kNodeB = kTree + 0x400, kNodeC = kTree + 0x500;

// A truck in third gear of four in automatic at 18 km/h: four wheels, the engine running, headlights and all wheel
// drive on, 60 of 75 litres, 30 of 150 engine damage. Its engine is "us_scout_modern_engine", which takes 150 and
// loses torque past 0.7 of that, down to x0.6. The truck takes engines from two sets: the first holds another engine
// only, the second holds three, the truck's among them. It is a "mod_suv/1000001" with an id of the game's.
static void MakeGame()
{
    g_game.clear();
    Block(kExe + 0x2a8e000, 0x3000);
    Block(kControl, 0x200);
    Block(kVehicle, 0x1000);
    Block(kAction, 0x200);
    Block(kModel, 0x800);
    Block(kChassis, 0x400);
    Block(kWheelList, 0x100);
    Block(kSpeeds, 0x40);
    Block(kDamage, 0x80);
    Block(kGameObject, 0x100);
    Block(kInput, 0x200);
    Put(kExe + kRvaControl, kControl);
    Put(kExe + kRvaGame, kGameObject);
    Put(kExe + kRvaInput, kInput);
    Put(kControl + 8, kVehicle);
    Put(kVehicle + 0x68, kAction);
    Put(kVehicle + 0x60, kModel);
    Put(kModel + 0x230, kChassis);
    Put(kVehicle + 0x200, kWheelList);
    Put(kVehicle + 0x208, kWheelList + 4 * 8);
    static const int drive[4] = { 2, 2, 1, 1 };
    for (int i = 0; i < 4; i++)
    {
        const uint64_t model = kWheelModel + (uint64_t)i * 0x1000, wheel = kWheel + (uint64_t)i * 0x1000;
        Block(model, 0x400);
        Block(wheel, 0x200);
        Put(kWheelList + (uint64_t)i * 8, model);
        Put(model + 0x2C8, wheel);
        Put(wheel, kExe + kRvaWheelClass);
        Put(wheel + 0xE8, drive[i]);
        Put(wheel + 0x16C, 11.5f + (float)i);
        // the front pair 1.5 m ahead of the chassis and the rear pair 1.5 m behind it, 0.8 m to either side; the
        // front wheels stand in a metre of water, the rear ones in a third of that
        const float side = (i % 2) ? -0.8f : 0.8f, ahead = i < 2 ? 1.5f : -1.5f;
        const float at[3] = { 100.0f + ahead * 0.6f - side * 0.8f, 20.0f, 300.0f + ahead * 0.8f + side * 0.6f };
        Put(wheel + 0x13C, at);
        Put(wheel + 0x17C, i < 2 ? 1.0f - 0.1f * (float)i : 0.3f - 0.1f * (float)(i - 2));
    }
    Put(kVehicle + 0x114, 12.5f);
    Put(kVehicle + 0x768, (uint8_t)0x07);
    Put(kVehicle + 0x11C, 0.0f);
    Put(kVehicle + 0x730, 0.0f);
    Put(kAction + 0x38, 1.0f);
    Put(kAction + 0x3C, (uint8_t)1);
    Put(kAction + 0x44, -0.6f);
    Put(kAction + 0x48, (uint8_t)0);
    Put(kAction + 0x49, (uint8_t)1);
    Put(kAction + 0x4A, (uint8_t)0);
    Put(kAction + 0x70, 3);
    Put(kAction + 0x74, 3);
    Put(kAction + 0xB4, 0.25f);
    Put(kAction + 0xCC, 1.0f);
    static const float speeds[6] = { 1.5f, 3.0f, 6.0f, 12.0f, 16.0f, 8.0f };
    Put(kSpeeds, speeds);
    Put(kAction + 0x58, kSpeeds);
    Put(kAction + 0x60, kSpeeds + sizeof speeds);
    Put(kModel + 0x5E8, 60.0f);
    Put(kModel + 0x5F0, 75.0f);
    Put(kVehicle + 0x150, kDamage | 0x00AB000000000000ull); // the handle carries a tag in bits 48 to 55
    Put(kDamage + 0x10, (uint16_t)1);
    const int damage[2] = { 30, 150 };
    Put(kDamage + 0x38, damage);
    // the fuel burn's numbers, with each gear's factor behind the gear speeds
    const float burn[5] = { 2.0f, 2.2f, 1.8f, 1.5f, 1.5f }, idleAndLeak[2] = { 0.2f, 40.0f }, gearBurn[7] = { 1.0f, 1.0f, 1.2f, 1.1f, 1.0f, 0.9f, 1.3f };
    Put(kVehicle + 0xE38, burn);
    Put(kVehicle + 0xE68, idleAndLeak);
    Put(kSpeeds + 0x20, gearBurn);
    Put(kVehicle + 0xE50, kSpeeds + 0x20);
    Put(kVehicle + 0xE58, kSpeeds + 0x20 + sizeof gearBurn);
    const float forward[3] = { 0.6f, 0.0f, 0.8f }, up[3] = { 0.0f, 1.0f, 0.0f }, left[3] = { -0.8f, 0.0f, 0.6f }, velocity[3] = { 3.0f, 0.0f, 4.0f }, position[3] = { 100.0f, 20.0f, 300.0f };
    Put(kChassis + 0x170, forward);
    Put(kChassis + 0x180, up);
    Put(kChassis + 0x190, left);
    Put(kChassis + 0x1A0, position);
    Put(kChassis + 0x230, velocity);
    // the damage of all the truck's parts, as the game last summed it
    const int sums[2] = { 48, 840 };
    Put(kVehicle + 0x108, sums);
    // the engine's name, and the names of the two sets the truck takes engines from
    Block(kExe + 0x2a9e000, 0x1000);
    Block(kTexts, 0x400);
    Block(kSets, 0x400);
    Block(kTree, 0x800);
    PutText(kVehicle + 0x968, "us_scout_modern_engine", kTexts);
    PutText(kModel + 0x588, kKind, 0);
    PutId(kId);
    PutText(kTexts + 0x100, "e_us_scout_old", 0);
    PutText(kTexts + 0x120, "e_us_scout_modern", kTexts + 0x40);
    Put(kVehicle + 0x930, kTexts + 0x100);
    Put(kVehicle + 0x938, kTexts + 0x140);
    // the game's sets: a list of three entries behind its head, each with the next entry at + 0, the set's name at
    // + 0x10 and the tree of its engines at + 0x30
    Put(kExe + kRvaEngineSets + 8, kSets);
    Put(kSets, kSetEntry[0]);
    Put(kSetEntry[0], kSetEntry[1]);
    Put(kSetEntry[1], kSetEntry[2]);
    Put(kSetEntry[2], kSets);
    PutText(kSetEntry[0] + 0x10, "e_ru_truck_old", 0);
    PutText(kSetEntry[1] + 0x10, "e_us_scout_modern", kTexts + 0x80);
    PutText(kSetEntry[2] + 0x10, "e_us_scout_old", 0);
    Put(kSetEntry[1] + 0x30, kNewHead);
    Put(kSetEntry[2] + 0x30, kOldHead);
    // the trees: a head with the root at + 8 and the byte at + 0x19 set, and nodes with the left child at + 0, the
    // right child at + 0x10, the engine's name at + 0x20 and its record at + 0x40. A node without a child has the
    // head in its place.
    Put(kOldHead + 8, kOldNode);
    Put(kOldHead + 0x19, (uint8_t)1);
    Put(kOldNode, kOldHead);
    Put(kOldNode + 0x10, kOldHead);
    PutText(kOldNode + 0x20, "us_scout_old_engine", kTexts + 0x180);
    Put(kNewHead + 8, kNodeB);
    Put(kNewHead + 0x19, (uint8_t)1);
    Put(kNodeB, kNodeA);
    Put(kNodeB + 0x10, kNodeC);
    Put(kNodeA, kNewHead);
    Put(kNodeA + 0x10, kNewHead);
    Put(kNodeC, kNewHead);
    Put(kNodeC + 0x10, kNewHead);
    PutText(kNodeA + 0x20, "us_scout_modern_a", kTexts + 0x1C0);
    PutText(kNodeB + 0x20, "us_scout_modern_b", kTexts + 0x200);
    PutText(kNodeC + 0x20, "us_scout_modern_engine", kTexts + 0x240);
    // the records: the threshold and the two torque factors at + 0x74, what the engine takes at + 0x84
    const float own[3] = { 0.7f, 1.0f, 0.6f }, others[3] = { 0.5f, 0.9f, 0.3f };
    Put(kNodeC + 0x40 + 0x74, own);
    Put(kNodeC + 0x40 + 0x84, 150);
    for (uint64_t node : { kOldNode, kNodeA, kNodeB })
    {
        Put(node + 0x40 + 0x74, others);
        Put(node + 0x40 + 0x84, 200);
    }
}

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

static bool Near(float a, float b) { return fabsf(a - b) < 0.001f; }

// The letters of a gearbox position: the truck is set to it, read and turned into the cluster's state.
static bool Letters(int gear, int automatic, float power, bool shifter, const char *mode, bool lowLamp)
{
    Put(kAction + 0x74, gear);
    Put(kAction + 0x3C, (uint8_t)automatic);
    Put(kAction + 0x38, power);
    const uint8_t wheel[5] = { 0, 0, (uint8_t)(shifter ? 1 : 0), 0, (uint8_t)(shifter ? 1 : 0) };
    Put(kInput + 0x1D8, wheel);
    TruckRefs r;
    TruckRaw t;
    World w;
    GaugeSettings s;
    GaugeState st;
    SettingsDefaults(s);
    if (!TruckFind(kMemory, r) || !TruckRead(kMemory, r, t)) return false;
    WorldRead(kMemory, r.control, w);
    TruckToState(t, w, s, st);
    const bool ok = st.gear == gear && !strcmp(st.gearMode, mode) && st.lowGear == lowLamp;
    if (!ok) printf("     gear %d automatic %d power %g shifter %d gives \"%s\", low lamp %d\n", gear, automatic, power, shifter, st.gearMode, st.lowGear);
    return ok;
}

// The place search on a real image of the game's exe (SR_IMAGE: a copy of its image from a running game, laid out as
// in memory, as the tire mod's test takes one): every place and ShiftGear where the code review of 2026-10-23 found
// them in the builds it knows, every place in the exe's data.
static void RealImageTest()
{
    char file[MAX_PATH] = "";
    GetEnvironmentVariableA("SR_IMAGE", file, MAX_PATH);
    if (!*file)
    {
        printf("     places in a real image of the game's exe: no SR_IMAGE names one, not tested\n");
        return;
    }
    std::vector<uint8_t> image;
    FILE *f = nullptr;
    if (fopen_s(&f, file, "rb") == 0 && f)
    {
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        image.resize(n > 0 ? (size_t)n : 0);
        if (!image.empty() && fread(image.data(), 1, image.size(), f) != image.size()) image.clear();
        fclose(f);
    }
    uint32_t stamp = 0, size = 0, lfanew = 0;
    if (image.size() > 0x1000)
    {
        memcpy(&lfanew, image.data() + 0x3C, 4);
        if (lfanew + 0x58 < 0x1000)
        {
            memcpy(&stamp, image.data() + lfanew + 8, 4);
            memcpy(&size, image.data() + lfanew + 0x50, 4);
        }
    }
    size_t from = 0, to = 0;
    if (!size || size != image.size() || !CodeRangeOf(image.data(), 0x1000, from, to))
    {
        Check(false, "places in a real image of the game's exe: SR_IMAGE is not an image of an exe as it lies in memory");
        return;
    }
    // the builds the review checked; the order of kGameGlobals: control, wheel class, game, map, viewer, garage, input,
    // engine sets, clock
    static const uint64_t kUpdate[kGlobCount] = { 0x2a92e38, 0x225ab38, 0x2a92e50, 0x2a92df0, 0x2a92df8, 0x2a949e8, 0x2a92ea0, 0x2aa2ba0, 0x2a92e08 };
    const bool steam = stamp == kGaugesExeStamp, update = stamp == 0x6ab52f5d;
    int right = 0, inData = 0;
    char wrong[512] = "";
    for (int g = 0; g < kGlobCount; g++)
    {
        int signatures = 0;
        bool conflict = false;
        const uint64_t place = GlobalResolve(image.data(), from, to, kGlobalSigs, g, signatures, conflict);
        const uint64_t want = steam ? kGameGlobals[g].steam : update ? kUpdate[g] : place;
        if (place && place == want) right++;
        else
        {
            char one[80];
            snprintf(one, sizeof one, " %s at %llx, not %llx;", kGameGlobals[g].name, (unsigned long long)place, (unsigned long long)want);
            strcat_s(wrong, one);
        }
        if (place && RvaInDataOf(image.data(), 0x1000, place)) inData++;
    }
    FunctionFind finds[kFnCount];
    GameFunctionsFind(image.data(), from, to, finds);
    const FunctionFind &shift = finds[kFnShiftGear];
    const size_t shiftWant = steam ? 0xd72360 : update ? 0xd73d80 : shift.at;
    const bool shiftRight = shift.state == kFnFound && shift.count == 1 && shift.at == shiftWant;
    char line[900];
    snprintf(line, sizeof line, "places in a real image of the game's exe (stamp %08x%s, code %zx to %zx): %d of %d places, %d in its data, ShiftGear at %zx%s%s", stamp,
             steam ? ", the Steam build of 22 July" : update ? ", the update of 20 October" : ", a build the test has no numbers for", from, to, right, kGlobCount, inData,
             shift.at, *wrong ? "; wrong:" : "", wrong);
    Check(right == kGlobCount && inData == kGlobCount && shiftRight, line);
    // the damage path the tire mod uses: the truck update and the damage update (in the Steam build of 22 July where the
    // tire mod has them, in another build found once)
    DamagePath path;
    const bool pathFound = DamagePathFind(image.data(), from, to, path);
    const bool pathRight = pathFound && (!steam || (path.truckUpdate == 0xa56970 && path.damageUpdate == 0xd6f690));
    snprintf(line, sizeof line, "places in a real image of the game's exe: the truck update at %zx, the damage update at %zx%s%s", path.truckUpdate, path.damageUpdate,
             pathFound ? "" : "; ", path.why);
    Check(pathRight, line);
    // the wheels' drive for the creep: its call and the drive where the two builds have them (review 11's addendum), in
    // another build found once
    DrivePlace drive;
    const bool driveFound = DrivePlaceFind(image.data(), from, to, drive);
    const bool driveRight = driveFound && (!steam || (drive.call == 0xc40270 && drive.drive == 0xc402d0)) && (!update || (drive.call == 0xc41d40 && drive.drive == 0xc41da0));
    snprintf(line, sizeof line, "places in a real image of the game's exe: the wheels' drive at %zx, called at %zx%s%s", drive.drive, drive.call, driveFound ? "" : "; ", drive.why);
    Check(driveRight, line);

    // the places the mod cannot do without, by each of their ways alone (game_globals.h): on a build the test has
    // numbers for, every way a place has must give it there; on another the line tells which ways found what, so a
    // player's Gauges.log of that build can be read against it
    uint64_t imageBase = 0;
    memcpy(&imageBase, image.data() + lfanew + 0x18 + 24, 8);
    uint64_t control = 0;
    for (int g : { kGlobControl, kGlobGame, kGlobWheelClass })
    {
        const int ways = g == kGlobControl ? kWaysMost : 2;
        const uint64_t want = steam ? kGameGlobals[g].steam : update ? kUpdate[g] : 0;
        char said[400] = "";
        bool agree = true;
        uint64_t any = 0;
        for (int w = 1; w <= ways; w++)
        {
            int signatures = 0;
            bool conflict = false;
            const uint64_t place = GlobalByWay(image.data(), image.size(), imageBase, from, to, g, w, control, signatures, conflict);
            char one[64];
            if (place) snprintf(one, sizeof one, "%sway %d at %llx", w > 1 ? ", " : "", w, (unsigned long long)place);
            else snprintf(one, sizeof one, "%sway %d not found", w > 1 ? ", " : "", w);
            strcat_s(said, one);
            agree = agree && (want ? place == want : true);
            if (!any) any = place;
        }
        if (g == kGlobControl) control = want ? want : any;
        snprintf(line, sizeof line, "places by their ways alone (stamp %08x): %s: %s", stamp, kGameGlobals[g].name, said);
        Check(want ? agree : any != 0, line);
    }

    // a build whose accessor of control's object reads otherwise (its lea broken here): the signatures of control and
    // the game miss, the truck update's calls still give control and the game stands beside it
    size_t window = 0;
    int windows = 0;
    if ((steam || update) && GlobalPatternFind(image.data(), from, to, kSigControlObject, window, windows) && windows == 1)
    {
        image[window + 13] = 0x90;
        int signatures = 0;
        bool conflict = false;
        const uint64_t controlOne = GlobalByWay(image.data(), image.size(), imageBase, from, to, kGlobControl, 1, 0, signatures, conflict);
        const uint64_t controlTwo = GlobalByWay(image.data(), image.size(), imageBase, from, to, kGlobControl, 2, 0, signatures, conflict);
        const uint64_t gameOne = GlobalByWay(image.data(), image.size(), imageBase, from, to, kGlobGame, 1, controlTwo, signatures, conflict);
        const uint64_t gameTwo = GlobalByWay(image.data(), image.size(), imageBase, from, to, kGlobGame, 2, controlTwo, signatures, conflict);
        Check(!controlOne && !gameOne && controlTwo == control && gameTwo == control + 0x18,
              "places: with the accessor's code of another kind the signatures of control and the game miss, and way 2 finds both");
    }
    // the suspension probe's vtable (suspension_scan.h): by its RTTI name, at RVA 0x23d9538 in 6ab52f5d (the finding
    // pass of review 14)
    const uint64_t constraint = VtableByName(image.data(), image.size(), imageBase, kSuspConstraintName);
    Check(update ? constraint == 0x23d9538 : constraint != 0, "places: hkpWheelConstraintData's vtable by its RTTI name, for the suspension probe");
}

int main()
{
    TruckRefs r;
    TruckRaw t;
    World w;
    GaugeSettings s;
    GaugeState st;
    SettingsDefaults(s);

    MakeGame();
    Check(TruckFind(kMemory, r) && r.vehicle == kVehicle && r.action == kAction && r.wheels == 4, "find: the driven truck's objects and its four wheels");
    Check(TruckRead(kMemory, r, t) && t.flags == 7 && t.gear == 3 && t.automatic == 1 && Near(t.powerFactor, 1.0f) && Near(t.throttle, -0.6f) && !t.handbrake && t.awd &&
          !t.diffLock && Near(t.actionB4, 0.25f) && Near(t.torqueFactor, 1.0f), "read: the engine's flags and the drive train");
    Check(t.gearSpeeds == 6 && Near(t.gearSpeed[0], 1.5f) && Near(t.gearSpeed[5], 8.0f) && TruckTopGear(t) == 4, "read: six gear speeds, so four gears and a high gear");
    Check(t.wheels == 4 && t.drive[0] == 2 && t.drive[3] == 1 && Near(t.spin[2], 13.5f), "read: each wheel's drive and turning speed");
    Check(Near(t.fuel, 60.0f) && Near(t.tank, 75.0f) && t.damageKnown && t.damage == 30 && t.damageCapacity == 150, "read: fuel, and the engine damage behind its handle");
    Check(t.damageSum == 48 && t.damageSumCapacity == 840, "read: the damage of all the truck's parts, as the game last summed it");
    Check(t.waterKnown && Near(t.wheelWater[0], 1.0f) && Near(t.wheelWater[1], 0.9f) && Near(t.wheelWater[3], 0.2f) && Near(t.wheelAt[0][0], 100.26f) &&
          Near(t.wheelAt[0][2], 301.68f) && t.clockKnown && Near(t.clock, 12.5f),
          "read: each wheel's place and the water it stands in, and the game's count of the seconds it has simulated the truck for");
    Check(Near(t.speed, 5.0f) && Near(t.position[2], 300.0f) && TruckSane(t), "read: 5 m/s along the forward axis, and the values make sense");
    Check(t.gearEngaged == 3 && t.burnKnown && Near(t.burnShare[0], 2.0f) && Near(t.burnShare[1], 2.2f) && Near(t.burnAwd, 1.8f) && Near(t.burnEngine, 1.5f) &&
          Near(t.burnGearbox, 1.5f) && Near(t.burnIdle, 0.2f) && Near(t.burnLeak, 40.0f) && t.gearBurns == 7 && Near(t.gearBurn[2], 1.2f) && Near(t.gearBurn[6], 1.3f) &&
          t.gearboxEffect == 0.0f && t.tankEffect == 0.0f, "read: the gear engaged, and the numbers the game burns fuel by");
    Put(kVehicle + 0xE58, (uint64_t)0);
    TruckRaw bare;
    Check(TruckRead(kMemory, r, bare) && !bare.burnKnown && TruckSane(bare), "read: a truck without burn numbers is still read");
    Put(kVehicle + 0xE58, kSpeeds + 0x20 + 7 * sizeof(float));
    // the game rebuilt the drive train under the truck: what was found is no longer read, so nothing is written through it
    Put(kVehicle + 0x68, kAction + 0x1000);
    TruckRaw staleRead;
    Check(!TruckRead(kMemory, r, staleRead), "read: none once the vehicle holds another drive train than the one found (the mod's writes go through it)");
    Put(kVehicle + 0x68, kAction);

    WorldRead(kMemory, r.control, w);
    TruckToState(t, w, s, st);
    Check(st.engineOn && Near(st.speedKmh, 18.0f) && st.gear == 3 && !strcmp(st.gearMode, "A") && Near(st.fuelFrac, 0.8f) && st.headlights && st.awd && !st.handbrake &&
          !st.diffLock && !st.lowGear && !st.damage, "state: 18 km/h, third gear in automatic, the tank four fifths full, the lamps as the truck has them");
    Check(Near(st.rpm, 650.0f) && st.rangeKm < 0.0f && st.tilt < 0 && !st.cold && st.boostBar == 0.0f,
          "state: the tachometer at idle, no range and a dark tilt lamp before the models, the dials without a source at rest");

    // each gearbox position
    Check(Letters(-1, 1, 1.0f, false, "", false) && Letters(0, 1, 1.0f, false, "", false) && Letters(-1, 0, 1.0f, false, "", false), "gear: reverse and neutral carry no letters");
    Check(Letters(1, 1, 1.0f, false, "A", false) && Letters(4, 1, 1.0f, false, "A", false), "gear: automatic");
    Check(Letters(5, 0, 1.0f, false, "H", false), "gear: the high gear is the one above the last numbered gear");
    Check(Letters(1, 0, 0.45f, false, "L", true) && Letters(1, 0, 0.2f, false, "L-", true) && Letters(1, 0, 1.0f, false, "L+", true), "gear: the low range by its power factor, with the low gear lamp");
    Check(Letters(1, 0, 1.0f, true, "", false) && Letters(3, 0, 1.0f, true, "", false) && Letters(2, 0, 1.0f, false, "", false), "gear: a gear picked on a wheel's shifter is a number");

    // the engine and the damage lamp
    MakeGame();
    Put(kVehicle + 0x768, (uint8_t)0);
    TruckFind(kMemory, r);
    TruckRead(kMemory, r, t);
    WorldRead(kMemory, r.control, w);
    TruckToState(t, w, s, st);
    Check(!st.engineOn && st.rpm == 0.0f && !st.headlights, "state: the engine off");
    const int half[2] = { 75, 150 }, over[2] = { 76, 150 };
    Put(kDamage + 0x38, half);
    TruckRead(kMemory, r, t);
    TruckToState(t, w, s, st);
    const bool atHalf = st.damage;
    Put(kDamage + 0x38, over);
    TruckRead(kMemory, r, t);
    TruckToState(t, w, s, st);
    Check(!atHalf && st.damage, "state: the damage lamp lights above half the engine's capacity");
    Put(kDamage + 0x10, (uint16_t)4);
    Check(TruckRead(kMemory, r, t) && !t.damageKnown && (TruckToState(t, w, s, st), !st.damage), "read: a handle without data gives no damage and a dark lamp");

    // a wheel whose memory ends before the water's depth
    MakeGame();
    TruckFind(kMemory, r);
    Drop(kWheel + 0x3000);
    Block(kWheel + 0x3000, 0x170);
    Put(kWheel + 0x3000, kExe + kRvaWheelClass);
    Put(kWheel + 0x3000 + 0xE8, 1);
    Put(kWheel + 0x3000 + 0x16C, 14.5f);
    TruckRaw dry;
    Check(TruckRead(kMemory, r, dry) && dry.wheels == 4 && !dry.waterKnown && Near(dry.spin[3], 14.5f) && Near(dry.spin[0], 11.5f),
          "read: a wheel that gives its turning speed alone is still a wheel, and the water is then not known");

    // backwards
    MakeGame();
    const float back[3] = { -3.0f, 0.0f, -4.0f };
    Put(kChassis + 0x230, back);
    TruckFind(kMemory, r);
    TruckRead(kMemory, r, t);
    WorldRead(kMemory, r.control, w);
    TruckToState(t, w, s, st);
    Check(Near(t.speed, -5.0f) && Near(st.speedKmh, 18.0f), "state: the speedometer shows the speed backwards too");

    // a tilted chassis: the nose 30 degrees up a slope, the truck leaning 20 degrees to its left
    MakeGame();
    TruckFind(kMemory, r);
    TruckRead(kMemory, r, t);
    const bool level = Near(t.forward[1], 0.0f) && Near(t.up[1], 1.0f) && Near(t.left[1], 0.0f) && Near(t.left[2], 0.6f);
    const float tiltedForward[3] = { 0.866f, 0.5f, 0.0f }, tiltedUp[3] = { -0.4698f, 0.8138f, 0.342f }, tiltedLeft[3] = { 0.171f, -0.2962f, 0.9397f };
    Put(kChassis + 0x170, tiltedForward);
    Put(kChassis + 0x180, tiltedUp);
    Put(kChassis + 0x190, tiltedLeft);
    const float uphill[3] = { 4.33f, 2.5f, 0.0f };
    Put(kChassis + 0x230, uphill);
    Check(level && TruckRead(kMemory, r, t) && Near(t.forward[1], 0.5f) && Near(t.up[1], 0.8138f) && Near(t.left[1], -0.2962f) && Near(t.up[2], 0.342f) && Near(t.speed, 5.0f) &&
          TruckSane(t), "read: the chassis axes of a truck on level ground and of one tilted on a slope, which drives along its nose");

    // what the game is showing
    MakeGame();
    TruckFind(kMemory, r);
    WorldRead(kMemory, r.control, w);
    Check(w.known && WorldDriving(w), "world: a truck driven in the world");
    Put(kGameObject + 0x30, 1);
    WorldRead(kMemory, r.control, w);
    const bool menu = !WorldDriving(w);
    Put(kGameObject + 0x30, 0);
    Put(kGameObject + 0x4A, (uint8_t)1);
    WorldRead(kMemory, r.control, w);
    const bool loading = !WorldDriving(w);
    Put(kGameObject + 0x4A, (uint8_t)0);
    Check(menu && loading, "world: not while a menu holds the game, not while a level loads");
    Block(kMap, 0x200);
    Put(kExe + kRvaMap, kMap);
    WorldRead(kMemory, r.control, w);
    const bool mapShut = w.mapThere && WorldDriving(w);
    Put(kMap + 0x148, (uint8_t)1);
    WorldRead(kMemory, r.control, w);
    Check(mapShut && !WorldDriving(w), "world: not while the map is open; a shut map does not matter");
    Put(kMap + 0x148, (uint8_t)0);
    Put(kExe + kRvaViewer, kMap);
    WorldRead(kMemory, r.control, w);
    const bool viewer = w.viewer && !WorldDriving(w);
    Put(kExe + kRvaViewer, (uint64_t)0);
    Put(kExe + kRvaGarage, kMap);
    WorldRead(kMemory, r.control, w);
    Check(viewer && w.garage && !WorldDriving(w), "world: not with the truck viewer or the garage");
    Put(kExe + kRvaGarage, (uint64_t)0);
    const bool menusRead = w.menusKnown;
    Put(kControl + 0xD8, (uint8_t)1);
    WorldRead(kMemory, r.control, w);
    Check(w.functionMenu && WorldDriving(w), "world: the function menu leaves the cluster on screen");
    Put(kExe + kRvaInput, (uint64_t)0);
    WorldRead(kMemory, r.control, w);
    const bool inputGone = !w.menusKnown;
    Put(kExe + kRvaInput, kInput);
    WorldRead(kMemory, 0, w);
    Check(menusRead && inputGone && !w.menusKnown, "world: the two menu flags count as read only when both were, so a flag that could not be read is not taken for no menu");
    Put(kExe + kRvaGame, (uint64_t)0);
    WorldRead(kMemory, r.control, w);
    Check(!w.known && !WorldDriving(w), "world: not when the game's state cannot be read");

    // a truck that changes or goes
    MakeGame();
    TruckFind(kMemory, r);
    Put(kControl + 8, kOtherVehicle);
    TruckRefs r2;
    Check(!TruckRead(kMemory, r, t) && !TruckFind(kMemory, r2), "loss: another vehicle in the driver's seat ends the read, and one that cannot be read is not found");
    Put(kControl + 8, (uint64_t)0);
    Check(!TruckFind(kMemory, r2), "loss: no driven vehicle");
    MakeGame();
    TruckFind(kMemory, r);
    Drop(kAction);
    Check(!TruckRead(kMemory, r, t), "loss: an object freed under the mod is a failed read");
    MakeGame();
    Put(kVehicle + 0x208, kWheelList);
    const bool none = !TruckFind(kMemory, r);
    MakeGame();
    Put(kWheel + 0x1000, kExe + 0x1234);
    Check(none && TruckFind(kMemory, r) && r.wheels == 3, "find: a vehicle without wheels is no truck, and what is not a wheel is left out");

    // values that cannot be a truck's
    MakeGame();
    TruckFind(kMemory, r);
    const float nan[3] = { nanf(""), 0.0f, 0.0f };
    Put(kChassis + 0x230, nan);
    TruckRead(kMemory, r, t);
    const bool nanSpeed = !TruckSane(t);
    MakeGame();
    Put(kModel + 0x5E8, 90.0f);
    TruckRead(kMemory, r, t);
    const bool overfull = !TruckSane(t);
    Put(kModel + 0x5E8, 10.0f);
    Put(kModel + 0x5F0, 0.0f);
    TruckRead(kMemory, r, t);
    Check(nanSpeed && overfull && !TruckSane(t), "sane: no number for a speed, more fuel than the tank holds, no tank");

    // the engine's record
    MakeGame();
    TruckFind(kMemory, r);
    EngineRecord e;
    Check(TruckEngineRecord(kMemory, kVehicle, e) && e.found && !strcmp(e.name, "us_scout_modern_engine") && Near(e.threshold, 0.7f) && Near(e.torqueMin, 1.0f) &&
          Near(e.torqueMax, 0.6f) && e.capacity == 150, "engine: the record is found by the engine's name, in the second of the truck's two sets");
    PutText(kVehicle + 0x968, "us_scout_modern_c", kTexts);
    const bool unknown = !TruckEngineRecord(kMemory, kVehicle, e) && !e.found && !strcmp(e.name, "us_scout_modern_c");
    PutText(kVehicle + 0x968, "us_scout_modern_a", kTexts);
    Check(unknown && TruckEngineRecord(kMemory, kVehicle, e) && e.capacity == 200 && Near(e.threshold, 0.5f),
          "engine: an engine that is in none of the sets has no record, and its neighbour in the tree is found by its own name");
    // the probe's dumps: the record's words as numbers or in hex, and the floats of a mass's size in a stretch
    {
        MakeGame();
        TruckFind(kMemory, r);
        EngineRecord dumped;
        TruckEngineRecord(kMemory, kVehicle, dumped);
        Put(dumped.at + 0x60, 195000.0f);
        Put(dumped.at + 0x68, 0.04f);
        Put(dumped.at + 0x6C, 2.0f);
        Put(dumped.at + 0x80, 0.35f);
        EngineRecord figures;
        TruckEngineRecord(kMemory, kVehicle, figures);
        Put(dumped.at + 0x60, 5.0f); // no engine's torque
        EngineRecord odd;
        TruckEngineRecord(kMemory, kVehicle, odd);
        Check(figures.found && figures.torque == 195000.0f && Near(figures.deltaAngVel, 0.04f) && figures.consumption == 2.0f && Near(figures.responsiveness, 0.35f) &&
                  odd.found && odd.torque == 0.0f && odd.capacity == figures.capacity,
              "engine: the figures of its file read from its record, a torque no engine has read as none and the record kept");
        // the gearbox: its name, the first text from + 0x900 that begins with "g_" and is a name, else its gears
        char gearboxName[kMaxText], gearboxStock[kMaxText], gearboxKey[96], gearsKey[96];
        uint32_t gearboxAt = 0;
        const bool noName = !TruckGearboxName(kMemory, kVehicle, gearboxName, gearboxAt, gearboxStock);
        PutText(kVehicle + 0xA00, "g_ab cd", 0);                   // a text with a space: no name
        PutText(kVehicle + 0xA28, "g_scout_highway_long", kTexts + 0x380);
        const bool named = TruckGearboxName(kMemory, kVehicle, gearboxName, gearboxAt, gearboxStock) && !strcmp(gearboxName, "g_scout_highway_long") &&
                           gearboxAt == 0xA28 && !gearboxStock[0];
        TruckRaw gearsOnly = {};
        TruckRead(kMemory, r, gearsOnly);
        GearboxKey(gearsOnly, gearboxName, gearboxKey, sizeof gearboxKey);
        GearboxKey(gearsOnly, "", gearsKey, sizeof gearsKey);
        // the socket block: at + 0x9a0 the truck's default, taken while nothing is at + 0x9c0 (a mod's name that does
        // not begin with "g_" too); at + 0x9c0 the gearbox fitted, which wins (a d110 on its highway gearbox, 2026-10-26)
        PutText(kVehicle + 0x9a0, "box-of_a_mod", 0);
        char atName[kMaxText], atStock[kMaxText];
        uint32_t atAt = 0;
        const bool atDefault = TruckGearboxName(kMemory, kVehicle, atName, atAt, atStock) && !strcmp(atName, "box-of_a_mod") && atAt == 0x9a0 &&
                               !strcmp(atStock, "box-of_a_mod");
        PutText(kVehicle + 0x9c0, "g_lrd_110_highway", kTexts + 0x3c0);
        const bool fitted = TruckGearboxName(kMemory, kVehicle, atName, atAt, atStock) && !strcmp(atName, "g_lrd_110_highway") && atAt == 0x9c0 &&
                            !strcmp(atStock, "box-of_a_mod");
        Check(noName && named && atDefault && fitted && !strcmp(gearboxKey, "g_scout_highway_long") && !strcmp(gearsKey, "gears 3 6 12 16 high 8"),
              "gearbox: the name of the gearbox fitted at + 0x9c0, the truck's default at + 0x9a0 only while none is fitted, whatever they begin "
              "with, else the first name with g_ near them, a text that is no name passed over; without a name it goes by its gears");
        Put(kModel + 0x784, 9007.0f);
        const float massRead = TruckMass(kMemory, kModel);
        Put(kModel + 0x784, 5.0f);
        Check(massRead == 9007.0f && TruckMass(kMemory, kModel) == 0.0f && TruckMass(kMemory, 0) == 0.0f,
              "truck: its mass read from its model, a number no truck weighs read as none");
        Put(dumped.at + 0x10, 195000.0f);
        Put(dumped.at + 0x14, 0.35f);
        Put(dumped.at + 0x18, (uint32_t)0x12345678u);
        char words[512], masses[512];
        TruckWordsText(kMemory, dumped.at + 0x10, 3, words, sizeof words);
        Put(kVehicle + 0x200, 9000.0f);
        Put(kVehicle + 0x204, 120000.0f);
        Put(kVehicle + 0x208, 4000.0f);
        const int found = TruckFloatsText(kMemory, kVehicle + 0x200, 0x10, 300.0f, 100000.0f, 60, masses, sizeof masses);
        char unread[64];
        TruckWordsText(kMemory, 0x100, 1, unread, sizeof unread);
        Check(dumped.at && !strcmp(words, "195000 0.35 12345678") && found == 2 && !strcmp(masses, "+0 9000, +8 4000") && !strcmp(unread, "?"),
              "probe: the engine record's words as numbers or in hex, and the floats of a mass's size with their offsets");
    }
    // memory that is not what it is taken for
    MakeGame();
    PutText(kTexts + 0x100, "e_no_such_set", 0);
    PutText(kTexts + 0x120, "e_nor_this_one_here", kTexts + 0x40);
    Put(kSetEntry[2], kSetEntry[2]); // the last entry is its own next: the list never ends
    const bool endless = !TruckEngineRecord(kMemory, kVehicle, e);
    MakeGame();
    Put(kNodeC, kNodeC); // the engine's node is its own left child: the tree never ends
    const bool looped = !TruckEngineRecord(kMemory, kVehicle, e);
    MakeGame();
    const float wrong[3] = { 7.0f, 1.0f, 0.6f }, strong[3] = { 0.7f, 1.0f, 3.0f };
    Put(kNodeC + 0x40 + 0x74, wrong);
    const bool noThreshold = !TruckEngineRecord(kMemory, kVehicle, e) && !e.found;
    Put(kNodeC + 0x40 + 0x74, strong);
    const bool nonsense = noThreshold && !TruckEngineRecord(kMemory, kVehicle, e) && !e.found;
    MakeGame();
    Put(kVehicle + 0x968 + 0x10, (uint64_t)5000); // a name of 5000 characters
    const bool tooLong = !TruckEngineRecord(kMemory, kVehicle, e) && !e.name[0];
    MakeGame();
    Put(kVehicle + 0x938, kTexts + 0x130); // the truck's sets end in the middle of a name
    Check(endless && looped && nonsense && tooLong && !TruckEngineRecord(kMemory, kVehicle, e),
          "engine: a list or a tree that never ends, numbers no engine has, a name too long and a broken list of sets give no record, and every walk ends");

    // the game's rule for a damaged engine
    MakeGame();
    TruckEngineRecord(kMemory, kVehicle, e);
    float effect = -1.0f, torque = -1.0f;
    TruckEngineEffect(105, 150, e, effect, torque);
    const bool atThreshold = effect == 0.0f && torque == 1.0f;
    TruckEngineEffect(120, 150, e, effect, torque);
    const bool pastIt = Near(effect, 1.0f / 3.0f) && Near(torque, 1.0f - 0.4f / 3.0f);
    TruckEngineEffect(150, 150, e, effect, torque);
    const bool wornOut = Near(effect, 1.0f) && Near(torque, 0.6f);
    EngineRecord other = e;
    other.threshold = 0.5f;
    other.torqueMin = 0.9f;
    other.torqueMax = 0.3f;
    TruckEngineEffect(120, 150, other, effect, torque);
    const bool byRecord = Near(effect, 0.6f) && Near(torque, 0.54f);
    TruckEngineEffect(120, 150, EngineRecord{}, effect, torque);
    Check(atThreshold && pastIt && wornOut && byRecord && Near(effect, 1.0f / 3.0f) && Near(torque, 1.0f - 0.4f / 3.0f),
          "engine: no effect up to the record's threshold, then the effect and the torque factor by how far past it the damage is; 0.7, 1 and 0.6 stand in for a record");

    // the write of engine damage: 30 of 150 in the made up game
    MakeGame();
    TruckFind(kMemory, r);
    TruckEngineRecord(kMemory, kVehicle, e);
    DamageWrite d;
    int engine[3] = {};
    float engineEffect = 0.0f, engineTorque = 0.0f;
    auto readEngine = [&]() {
        Peek(kDamage + 0x38, engine, sizeof engine);
        memcpy(&engineEffect, &engine[2], 4);
        Peek(kAction + 0xCC, &engineTorque, 4);
    };
    const bool one = TruckDamageAdd(kMemory, r, 1, e, d) && d.written && d.before == 30 && d.wrote == 31 && d.after == 31 && d.capacity == 150;
    readEngine();
    Check(one && engine[0] == 31 && engine[1] == 150 && engineEffect == 0.0f && engineTorque == 1.0f && d.effectAfter == 0.0f && d.torqueAfter == 1.0f,
          "write: one more point of engine damage, and below the threshold nothing else changes");
    const bool eighty = TruckDamageAdd(kMemory, r, 80, e, d) && d.wrote == 111 && d.effectBefore == 0.0f && d.torqueBefore == 1.0f;
    readEngine();
    const float share = (0.74f - 0.7f) / 0.3f;
    Check(eighty && engine[0] == 111 && Near(engineEffect, share) && Near(engineTorque, 1.0f - 0.4f * share) && Near(d.effectAfter, share) && Near(d.torqueAfter, 1.0f - 0.4f * share),
          "write: past the threshold the effect and the torque factor are written as the game's rule gives them");
    const bool toTheLast = TruckDamageAdd(kMemory, r, 1000, e, d) && d.wrote == 149;
    readEngine();
    const bool stopped = engine[0] == 149;
    const bool full = !TruckDamageAdd(kMemory, r, 1, e, d) && !d.written && d.before == 149 && d.why[0];
    readEngine();
    Check(toTheLast && stopped && full && engine[0] == 149, "write: the damage stops one short of what the engine takes, and nothing is written from there");
    const int ended[2] = { 150, 150 };
    Put(kDamage + 0x38, ended);
    const bool leftAlone = !TruckDamageAdd(kMemory, r, 1, e, d);
    readEngine();
    Check(leftAlone && engine[0] == 150, "write: an engine at its end is left alone");

    // a stall the mod's gearbox ordered costs the gearbox: 5 percent of its 200 is 10 points into the block behind
    // vehicle + 0x148, again on a second stall, capped at what it takes; nothing with Damage off or StallDamagePercent 0
    {
        MakeGame();
        TruckFind(kMemory, r);
        const uint64_t kGearboxBlock = 0x10200000;
        Block(kGearboxBlock, 0x80);
        Put(kVehicle + 0x148, kGearboxBlock | 0x00CD000000000000ull);
        Put(kGearboxBlock + 0x10, (uint16_t)1);
        const int sound[2] = { 0, 200 };
        Put(kGearboxBlock + 0x38, sound);
        GaugeSettings gs;
        SettingsDefaults(gs);
        GearboxDamageWrite gw;
        int gearbox[2] = {};
        const bool first = TruckGearboxDamageAdd(kMemory, r, StallDamageShare(gs), gw) && gw.points == 10 && gw.capacity == 200;
        Peek(kGearboxBlock + 0x38, gearbox, sizeof gearbox);
        const bool ten = gearbox[0] == 10 && gearbox[1] == 200;
        const bool second = TruckGearboxDamageAdd(kMemory, r, StallDamageShare(gs), gw) && gw.before == 10 && gw.wrote == 20;
        Peek(kGearboxBlock + 0x38, gearbox, sizeof gearbox);
        const bool twenty = gearbox[0] == 20;
        gs.damage = false;
        const bool off = StallDamageShare(gs) == 0.0f && !TruckGearboxDamageAdd(kMemory, r, StallDamageShare(gs), gw);
        gs.damage = true;
        gs.stallDamage = 0.0f;
        const bool none = !TruckGearboxDamageAdd(kMemory, r, StallDamageShare(gs), gw);
        Peek(kGearboxBlock + 0x38, gearbox, sizeof gearbox);
        const bool untouched = gearbox[0] == 20;
        const int nearly[2] = { 195, 200 };
        Put(kGearboxBlock + 0x38, nearly);
        gs.stallDamage = 5.0f;
        const bool capped = TruckGearboxDamageAdd(kMemory, r, StallDamageShare(gs), gw) && gw.wrote == 200 && gw.points == 5;
        const bool worst = !TruckGearboxDamageAdd(kMemory, r, StallDamageShare(gs), gw) && gw.before == 200;
        Peek(kGearboxBlock + 0x38, gearbox, sizeof gearbox);
        TruckRaw t;
        const bool read = TruckRead(kMemory, r, t) && t.gearboxKnown && t.gearboxDamage == 200 && t.gearboxCapacity == 200;
        Check(first && ten && second && twenty && off && none && untouched && capped && worst && gearbox[0] == 200 && read,
              "stall: 5 percent of the gearbox's 200 is 10 points behind vehicle + 0x148, again on a second stall, capped at what it takes; nothing with "
              "Damage off or StallDamagePercent 0; the reader names the gearbox's damage");

        // the job: posted by the mod's thread, dealt by a frame of the truck update for that truck only, the game's damage
        // update run after the write (here it sums the parts into vehicle + 0x108), the result read once; one at a time;
        // a job no frame took is taken back after its wait
        Put(kGearboxBlock + 0x38, sound);
        static uint64_t updated = 0;
        updated = 0;
        auto update = [](uint64_t vehicle) {
            updated = vehicle;
            int block[2] = {}, sums[2] = { 0, 0 };
            Peek(0x10200000 + 0x38, block, sizeof block);
            sums[0] = 30 + block[0];
            sums[1] = 150 + block[1];
            Put(vehicle + 0x108, sums);
        };
        StallJob job = {};
        const bool posted = StallJobPost(job, r, 5.0f, 1000) && !StallJobPost(job, r, 5.0f, 1001);
        const bool otherTruck = !StallJobDeal(job, kMemory, kOtherVehicle, update) && job.state == kStallJobPosted;
        GearboxDamageWrite jw;
        bool ran = false;
        int sum = 0, sumCapacity = 0;
        const bool notYet = !StallJobResult(job, jw, ran, sum, sumCapacity);
        const bool dealt = StallJobDeal(job, kMemory, kVehicle, update) && !StallJobDeal(job, kMemory, kVehicle, update);
        Peek(kGearboxBlock + 0x38, gearbox, sizeof gearbox);
        const bool result = StallJobResult(job, jw, ran, sum, sumCapacity) && jw.written && jw.points == 10 && ran && updated == kVehicle && sum == 40 &&
                            sumCapacity == 350 && gearbox[0] == 10 && job.state == kStallJobIdle && !StallJobResult(job, jw, ran, sum, sumCapacity);
        // kept however long while the same truck is driven (a pause, a menu), dropped once another one is (review 10, D4)
        const bool again = StallJobPost(job, r, 5.0f, 2000) && !StallJobDrop(job, kVehicle) && job.state == kStallJobPosted &&
                           StallJobDrop(job, kOtherVehicle) && job.state == kStallJobIdle;
        Check(posted && otherTruck && notYet && dealt && result && again,
              "stall job: posted once at a time, dealt only by a frame for its truck with the game's damage update after, its result and the game's sum "
              "read once; kept while its truck is driven, dropped once another is");

        // a frame of the truck update: its first argument an object with the vehicle at + 0x20 (StallJobFrame, what the
        // hook's callback runs); and the clutch's burn end to end, its whole points posted to the job and dealt by a frame
        // (review 10, D7)
        const uint64_t kFirst = 0x10210000, kOtherFirst = 0x10220000;
        Block(kFirst, 0x40);
        Block(kOtherFirst, 0x40);
        Put(kFirst + 0x20, kVehicle);
        Put(kOtherFirst + 0x20, kOtherVehicle);
        Put(kGearboxBlock + 0x38, sound);
        ClutchHeat heat = {};
        heat.owed = 0.004f; // under one point of 200
        const int underOne = ClutchBurnPost(heat, job, r, 200, 3000);
        heat.owed = 0.057f; // 11.4 points of 200
        const int points = ClutchBurnPost(heat, job, r, 200, 3000);
        const bool busy = ClutchBurnPost(heat, job, r, 200, 3000) == 0;
        const bool idleFrame = !StallJobFrame(job, kMemory, kOtherFirst, update) && job.state == kStallJobPosted;
        const bool frame = StallJobFrame(job, kMemory, kFirst, update);
        bool burn = false;
        const bool burnt = StallJobResult(job, jw, ran, sum, sumCapacity, &burn) && burn && jw.written && jw.points == 11;
        Peek(kGearboxBlock + 0x38, gearbox, sizeof gearbox);
        StallJob quiet = {};
        const bool unread = !StallJobFrame(quiet, kMemory, 0x5000, update); // no job: first is not read, nor is a bad one
        Check(underOne == 0 && points == 11 && Near(heat.owed, 0.002f) && busy && idleFrame && frame && burnt && gearbox[0] == 11 && unread,
              "clutch burn end to end: whole points owed are posted to the job, the rest kept; a frame reads the vehicle at first + 0x20 and deals "
              "only its truck's job, the gearbox's damage written and marked as the burn's");
    }

    // the locks on the mod's keys: a press flips its byte in the action (the lock at + 0x4A, all-wheel drive at + 0x49,
    // the made up truck with all-wheel drive on and the lock off), a key held is no press, and a byte the game puts back
    // to 0 after a write of 1 is said once a truck
    {
        MakeGame();
        LocksWatch l = {};
        int out[kLocks];
        uint8_t diff = 0, awd = 0;
        auto pass = [&](bool pressDiff, bool pressAwd) {
            const bool pressed[kLocks] = { pressDiff, pressAwd };
            LocksStep(l, kMemory, kAction, pressed, out);
            Peek(kAction + 0x4A, &diff, 1);
            Peek(kAction + 0x49, &awd, 1);
        };
        pass(true, false);
        const bool lockOn = out[kLockDiff] == kLockOn && out[kLockAwd] == kLockNone && diff == 1 && awd == 1;
        pass(false, false); // the key held: the reader gives no second press
        pass(false, false);
        const bool held = out[kLockDiff] == kLockNone && diff == 1;
        pass(false, true);
        const bool awdOff = out[kLockAwd] == kLockOff && awd == 0 && diff == 1;
        pass(true, false);
        const bool lockOff = out[kLockDiff] == kLockOff && diff == 0;
        pass(true, false);
        Put(kAction + 0x4A, (uint8_t)0); // a truck without the upgrade: the game puts it back
        pass(false, false);
        const bool cleared = out[kLockDiff] == kLockCleared && diff == 0;
        pass(true, false);
        Put(kAction + 0x4A, (uint8_t)0);
        pass(false, false);
        const bool once = out[kLockDiff] == kLockNone;
        Check(lockOn && held && awdOff && lockOff && cleared && once,
              "locks: a press flips the differential lock or all-wheel drive in the truck's action, held is no repeat, a clear by the game said once");
        const TruckMemory readOnly = { Peek, kExe };
        const bool pressed[kLocks] = { true, true };
        LocksStep(l, readOnly, kAction, pressed, out);
        Peek(kAction + 0x4A, &diff, 1);
        Check(out[kLockDiff] == kLockNone && out[kLockAwd] == kLockNone && diff == 0, "locks: a write the memory does not take is no flip");
    }

    // the effect the game holds is never lowered: 100 of 150 with an effect of 0.25 from a rule the record does not give
    MakeGame();
    TruckFind(kMemory, r);
    TruckEngineRecord(kMemory, kVehicle, e);
    const int worn[2] = { 100, 150 };
    Put(kDamage + 0x38, worn);
    Put(kDamage + 0x40, 0.25f);
    Put(kAction + 0xCC, 0.9f);
    const bool lower = TruckDamageAdd(kMemory, r, 10, e, d) && d.wrote == 110;
    readEngine();
    const bool held = engine[0] == 110 && engineEffect == 0.25f && engineTorque == 0.9f && d.effectAfter == 0.25f && d.torqueAfter == 0.9f;
    const bool higher = TruckDamageAdd(kMemory, r, 10, e, d) && d.wrote == 120;
    readEngine();
    Check(lower && held && higher && Near(engineEffect, 1.0f / 3.0f) && Near(engineTorque, 1.0f - 0.4f / 3.0f),
          "write: the effect the game holds is never written lower, and it is written once the rule gives more");
    // without a record only the damage is written, however far past any threshold it is
    Put(kDamage + 0x38, worn);
    Put(kDamage + 0x40, 0.25f);
    Put(kAction + 0xCC, 0.9f);
    const bool alone = TruckDamageAdd(kMemory, r, 45, EngineRecord{}, d) && d.wrote == 145;
    readEngine();
    Check(alone && engine[0] == 145 && engineEffect == 0.25f && engineTorque == 0.9f,
          "write: without a record the damage is written alone, and the effect and the torque factor stay as the game left them");

    // what stands in the way of a write
    MakeGame();
    TruckFind(kMemory, r);
    TruckEngineRecord(kMemory, kVehicle, e);
    EngineRecord otherEngine = e;
    otherEngine.capacity = 200;
    const bool mismatch = !TruckDamageAdd(kMemory, r, 1, otherEngine, d) && !d.written && d.why[0];
    const bool nothing = !TruckDamageAdd(kMemory, r, 0, e, d) && !TruckDamageAdd(kMemory, r, -5, e, d);
    const TruckMemory readOnly = { Peek, kExe };
    const bool noWay = !TruckDamageAdd(readOnly, r, 1, e, d);
    Put(kDamage + 0x10, (uint16_t)4);
    const bool noData = !TruckDamageAdd(kMemory, r, 1, e, d);
    Put(kDamage + 0x10, (uint16_t)1);
    Put(kVehicle + 0x68, kAction + 0x10);
    const bool otherDriveTrain = !TruckDamageAdd(kMemory, r, 1, e, d);
    Put(kVehicle + 0x68, kAction);
    Put(kControl + 8, kOtherVehicle);
    const bool otherTruck = !TruckDamageAdd(kMemory, r, 1, e, d);
    Put(kControl + 8, kVehicle);
    // the game's own pointer to the control object has moved on: the objects in r are no longer the game's
    Put(kExe + kRvaControl, kOtherVehicle);
    const bool otherControl = !TruckDamageAdd(kMemory, r, 1, e, d);
    Put(kExe + kRvaControl, kControl);
    // a handle that leads into the exe itself, with what reads as an engine's damage there
    Put(kVehicle + 0x150, kExe + 0x2a8e400);
    Put(kExe + 0x2a8e400 + 0x10, (uint16_t)1);
    const int inExe[2] = { 30, 150 };
    Put(kExe + 0x2a8e400 + 0x38, inExe);
    const bool exeLeftAlone = !TruckDamageAdd(kMemory, r, 1, e, d) && !d.written;
    int exeDamage = 0;
    Peek(kExe + 0x2a8e400 + 0x38, &exeDamage, sizeof exeDamage);
    Put(kVehicle + 0x150, kDamage | 0x00AB000000000000ull);
    readEngine();
    const bool untouched = engine[0] == 30 && engineEffect == 0.0f && engineTorque == 1.0f && exeDamage == 30;
    const int odd[2] = { 200, 150 };
    Put(kDamage + 0x38, odd);
    const bool oddDamage = !TruckDamageAdd(kMemory, r, 1, e, d);
    readEngine();
    Check(mismatch && nothing && noWay && noData && otherDriveTrain && otherTruck && otherControl && exeLeftAlone && untouched && oddDamage && engine[0] == 200,
          "write: nothing goes to an engine that does not take what its record says, to a handle without data, to another truck, into the exe, or to damage that reads as no engine's");

    // which truck this is
    {
        MakeGame();
        TruckIdentity id, now;
        TruckFind(kMemory, r);
        TruckRead(kMemory, r, t);
        TruckKey(kMemory, r, t, id);
        Check(id.named && !strcmp(id.key, kKind) && !strcmp(id.id, kId), "identity: a truck goes by the name of its kind, and the game's id for it is read");
        // a longer name, which the game keeps outside the text itself; and one with characters an ini section's name cannot hold
        PutText(kModel + 0x588, "scania_nextgen_8x8v2/6169632", kTexts + 0x280);
        TruckKey(kMemory, r, t, now);
        const bool longName = now.named && !strcmp(now.key, "scania_nextgen_8x8v2/6169632");
        PutText(kModel + 0x588, "my [new] truck!", 0);
        Put(kModel + 0x588 + 13, (uint8_t)0x01);
        Put(kModel + 0x588 + 14, (uint8_t)0xE9);
        TruckKey(kMemory, r, t, now);
        Check(longName && now.named && !strcmp(now.key, "my__new__truc__"), "identity: a long name is read whole, and what a section's name cannot hold is replaced in the key");
        // the same kind under another id, an id that is none, and no id at all
        PutText(kModel + 0x588, kKind, 0);
        PutId("{00000000-1111-2222-3333-444444444444}");
        TruckKey(kMemory, r, t, now);
        const bool sameKind = !strcmp(now.key, id.key) && !strcmp(now.id, "{00000000-1111-2222-3333-444444444444}");
        PutId("{00000000-1111-2222-3333-44444444444}}");
        TruckKey(kMemory, r, t, now);
        const bool noShape = !now.id[0] && !strcmp(now.key, id.key);
        Put(kVehicle + 0x48, (uint64_t)0);
        TruckKey(kMemory, r, t, now);
        Check(sameKind && noShape && !now.id[0] && now.named && TruckIdText(kId, 38) && !TruckIdText(kId, 37) && !TruckIdText("{8E42BF0A_F7C6-40f9-B0F2-225DE74CE031}", 38),
              "identity: two trucks of one kind have one key whatever the game's ids for them, and an id that has not the shape of one is not taken");
        // no name: the truck goes by its build, which two trucks of one build share and a truck of another build does not
        PutText(kModel + 0x588, "", 0);
        TruckKey(kMemory, r, t, id);
        const bool byBuild = !id.named && !strcmp(id.key, "tank75_wheels4_gearbox6_engine150");
        TruckRaw twin = t, bigger = t, noEngine = t;
        twin.fuel = 10.0f;
        twin.speed = 0.0f;
        bigger.tank = 200.5f;
        noEngine.damageKnown = false;
        TruckIdentity twinId, biggerId, bareId;
        TruckKey(kMemory, r, twin, twinId);
        TruckKey(kMemory, r, bigger, biggerId);
        TruckKey(kMemory, r, noEngine, bareId);
        Drop(kModel);
        TruckKey(kMemory, r, t, now);
        Check(byBuild && !strcmp(twinId.key, id.key) && !strcmp(biggerId.key, "tank200.5_wheels4_gearbox6_engine150") && !strcmp(bareId.key, "tank75_wheels4_gearbox6_engine0") &&
              !now.named && !strcmp(now.key, id.key),
              "identity: a truck whose kind has no name, or whose name cannot be read, goes by its build, which two trucks of one build share");
        Check(TruckKeyNumber(kKind) != 0 && TruckKeyNumber(kKind) == TruckKeyNumber("mod_suv/1000001") && TruckKeyNumber(kKind) != TruckKeyNumber("mod_suv/1000002") &&
              TruckKeyNumber("") != 0, "identity: a key's number is the same for the same key, another for another, and never 0");
        // a name whose first character is the end of a text is no name
        MakeGame();
        TruckFind(kMemory, r);
        TruckRead(kMemory, r, t);
        Put(kModel + 0x588, (uint8_t)0);
        TruckKey(kMemory, r, t, now);
        Check(!now.named && !strcmp(now.key, "tank75_wheels4_gearbox6_engine150"), "identity: a name that begins with the end of a text is none, and the truck goes by its build");

        // from pass to pass: another truck counts from the second pass that reads it, one that goes by its build from the twentieth
        TruckIdentity modded = {}, ck = {}, built = {};
        strcpy_s(modded.key, "mod_suv/1000001");
        strcpy_s(modded.id, kId);
        strcpy_s(ck.key, "chevrolet_ck1500");
        strcpy_s(built.key, "tank75_wheels4_gearbox6_engine150");
        modded.named = ck.named = true;
        TruckWho who = {};
        bool taking = false;
        const bool onFirst = !TruckWhoStep(who, modded, taking) && !taking && !who.taken.key[0];
        const bool onSecond = !TruckWhoStep(who, modded, taking) && taking && !strcmp(who.taken.key, modded.key);
        const bool onThird = TruckWhoStep(who, modded, taking) && !taking;
        Check(onFirst && onSecond && onThird, "identity: a truck is taken on the second pass that reads it, and the models wait until the pass after");
        // one pass reads another name: nothing changes; then the other truck for good
        const bool once = !TruckWhoStep(who, ck, taking) && !taking;
        const bool backAgain = TruckWhoStep(who, modded, taking) && !strcmp(who.taken.key, modded.key);
        const bool onceMore = !TruckWhoStep(who, ck, taking) && !taking;
        const bool swapped = !TruckWhoStep(who, ck, taking) && taking && !strcmp(who.taken.key, ck.key) && !who.taken.id[0];
        Check(once && backAgain && onceMore && swapped, "identity: a name read on one pass alone changes nothing, and each run of passes counts from its own start");
        // the game's id: one that is not read leaves the last, another one replaces it
        TruckWho ids = {};
        TruckWhoStep(ids, modded, taking);
        TruckWhoStep(ids, modded, taking);
        TruckIdentity noId = modded, otherId = modded;
        noId.id[0] = 0;
        strcpy_s(otherId.id, "{00000000-1111-2222-3333-444444444444}");
        const bool idKept = TruckWhoStep(ids, noId, taking) && !strcmp(ids.taken.id, kId);
        Check(idKept && TruckWhoStep(ids, otherId, taking) && !taking && !strcmp(ids.taken.id, otherId.id),
              "identity: under one key the game's id follows the truck, and an id that is not read leaves the last one");
        // a truck that goes by its build: nineteen passes are not enough, and a pass that reads the old truck starts the count over
        bool early = false;
        for (int i = 0; i < kTruckPassesUnnamed - 1; i++) early = early || TruckWhoStep(who, built, taking) || taking;
        const bool notYet = !early && !strcmp(who.taken.key, ck.key);
        const bool interrupted = TruckWhoStep(who, ck, taking);
        for (int i = 0; i < kTruckPassesUnnamed - 1; i++) early = early || TruckWhoStep(who, built, taking) || taking;
        const bool stillNot = !early && !strcmp(who.taken.key, ck.key);
        const bool atLast = !TruckWhoStep(who, built, taking) && taking && !strcmp(who.taken.key, built.key);
        Check(notYet && interrupted && stillNot && atLast, "identity: a truck that goes by its build is taken only after twenty passes in a row read that build");
        // after a load the truck that drives on under its id keeps its heat; another, none before, or none for a minute is cold
        Check(LoadTruckAfter(kId, kId, 500) == kLoadSame && LoadTruckAfter(kId, otherId.id, 500) == kLoadOther && LoadTruckAfter("", kId, 500) == kLoadOther &&
                  LoadTruckAfter(kId, nullptr, 59000) == kLoadWait && LoadTruckAfter(kId, "", kLoadWaitMs) == kLoadNone,
              "load: the same truck by its id drives on warm; another truck, none before the load, or none for a minute after it goes cold");
    }

    // the gearbox hooks' attach and detach: a function of the test's own detoured, the hook called and calling through
    // with the arguments and the answer unchanged, and the function its own again after the detach
    {
        g_testWay = &TestTarget;
        void **targets[1] = { (void **)&g_testWay };
        void *const hooks[1] = { (void *)&TestHook };
        volatile int a = 4, b = 5;
        const int before = TestTarget(a, b);
        const bool attached = GearboxAttach(targets, hooks, 1) == NO_ERROR;
        const bool wayMoved = g_testWay != &TestTarget;
        const int through = TestTarget(a, b);
        const bool seen = g_hookSaw == 1;
        const bool detached = GearboxDetach(targets, hooks, 1) == NO_ERROR;
        const int after = TestTarget(a, b);
        Check(before == 17 && attached && wayMoved && seen && through == before && detached && after == before && g_hookSaw == 1 && g_testWay == &TestTarget,
              "gearbox hooks: a function detoured calls the hook, which calls through with its arguments and answer unchanged; the detach makes it its own again");
    }

    // the truck update's hook through Detours on a function with the game's signature (review 10, D7): the callback gets
    // the first argument before the update runs; the update gets all five arguments, the fifth on the stack, and its
    // answer in xmm0 comes back; after the unhook the function is its own again
    {
        uint8_t object[0x40] = {};
        const uint64_t truck = 0x1234567890ull;
        memcpy(object + 0x20, &truck, 8);
        int second = 0;
        float (*volatile update)(void *, void *, float, float, int) = &FakeTruckUpdate;
        const float plain = update(object, &second, 1.5f, 2.0f, 7);
        char why[160] = "";
        const bool hooked = TruckUpdateHook((uintptr_t)&FakeTruckUpdate, &FakeOnFrame, why, sizeof why);
        const long framesBefore = TruckUpdateFrames();
        g_fakeVehicle = 0;
        const float through = update(object, &second, 1.5f, 2.0f, 7);
        const bool seen = g_frameFirst == (LONG64)(uintptr_t)object && TruckUpdateFrames() == framesBefore + 1 && g_fakeVehicle == truck;
        const bool twice = TruckUpdateHook((uintptr_t)&FakeTruckUpdate, &FakeOnFrame, why, sizeof why);
        TruckUpdateUnhook();
        g_frameFirst = 0;
        const float after = update(object, &second, 1.5f, 2.0f, 7);
        Check(plain == 11.0f && hooked && through == 11.0f && seen && twice && after == 11.0f && g_frameFirst == 0 && TruckUpdateFrames() == framesBefore + 1,
              "truck update hook: through Detours on a function with the game's signature, the callback gets the first argument, the function its five "
              "arguments (the fifth on the stack) and its answer in xmm0 comes back; after the unhook it is its own again");
        if (!hooked) printf("     %s\n", why);
    }

    // the creep's stub on a function with the wheels' drive's signature: for the action given the throttle becomes the
    // larger of its own and the share, the step's seconds and every other action's throttle pass as they came, and
    // with no share every call passes; the counts follow
    {
        uint8_t truck[0x100] = {}, other[0x100] = {};
        void (*volatile drive)(void *, float, float) = &FakeDrive;
        char why[160] = "";
        const bool hooked = CreepHook((uintptr_t)&FakeDrive, why, sizeof why);
        long hits0 = 0, calls0 = 0;
        CreepCounts(hits0, calls0);
        CreepGive((uint64_t)(uintptr_t)truck, 0.2f);
        drive(truck, 0.05f, 0.033f);
        const bool raised = g_driveThrottle == 0.2f && g_driveDt == 0.033f && g_driveAction == truck;
        drive(truck, 0.5f, 0.033f);
        const bool pedalWins = g_driveThrottle == 0.5f;
        drive(other, 0.05f, 0.033f);
        const bool otherPasses = g_driveThrottle == 0.05f && g_driveAction == other;
        CreepGive(0, 0.0f);
        drive(truck, 0.05f, 0.033f);
        const bool nonePasses = g_driveThrottle == 0.05f;
        CreepGive((uint64_t)(uintptr_t)truck, 0.0f);
        drive(truck, 0.0f, 0.033f);
        const bool zeroPasses = g_driveThrottle == 0.0f;
        long hits = 0, calls = 0;
        CreepCounts(hits, calls);
        const bool twice = CreepHook((uintptr_t)&FakeDrive, why, sizeof why);
        Check(hooked && raised && pedalWins && otherPasses && nonePasses && zeroPasses && hits - hits0 == 2 && calls - calls0 == 5 && twice && CreepHooked(),
              "creep hook: through Detours on a function with the wheels' drive's signature, the driven truck's throttle is the larger of its own and the "
              "share, the seconds pass as they came, another truck's throttle and every call without a share pass; the counts follow");
        if (!hooked) printf("     %s\n", why);
    }

    // the wheels' drive found by its call in made-up code: int3 padding, the drive's first bytes, the call's bytes once;
    // not taken where the call is there twice, does not go to a function's start, or another mod's jump stands there
    {
        uint8_t driveHead[kPatternMax], driveMask[kPatternMax], callBytes[kPatternMax], callMask[kPatternMax];
        const int headLength = PatternParse(kDriveHeadPattern, driveHead, driveMask), callLength = PatternParse(kDriveCallPattern, callBytes, callMask);
        std::vector<uint8_t> code(0x400, 0x90);
        const size_t driveAt = 0x200, callAt = 0x80;
        memset(code.data() + driveAt - 16, 0xCC, 16);
        for (int i = 0; i < headLength; i++) code[driveAt + i] = driveMask[i] ? driveHead[i] : 0x11;
        for (int i = 0; i < callLength; i++) code[callAt + i] = callMask[i] ? callBytes[i] : 0;
        const size_t call = callAt + (size_t)callLength - 5;
        const int32_t distance = (int32_t)driveAt - (int32_t)(call + 5);
        memcpy(code.data() + call + 1, &distance, 4);
        DrivePlace place;
        const bool found = DrivePlaceFind(code.data(), 0x10, code.size(), place) && place.call == call && place.drive == driveAt && !*place.why;
        std::vector<uint8_t> held = code;
        held[driveAt] = 0xE9;
        DrivePlace heldPlace;
        const bool heldNot = !DrivePlaceFind(held.data(), 0x10, held.size(), heldPlace) && strstr(heldPlace.why, "another mod");
        std::vector<uint8_t> noStart = code;
        noStart[driveAt - 1] = 0x90;
        DrivePlace noStartPlace;
        const bool noStartNot = !DrivePlaceFind(noStart.data(), 0x10, noStart.size(), noStartPlace) && strstr(noStartPlace.why, "start");
        std::vector<uint8_t> twiceCode = code;
        memcpy(twiceCode.data() + 0x300, code.data() + callAt, (size_t)callLength);
        DrivePlace twicePlace;
        const bool twiceNot = !DrivePlaceFind(twiceCode.data(), 0x10, twiceCode.size(), twicePlace) && strstr(twicePlace.why, "more than once");
        Check(found && heldNot && noStartNot && twiceNot,
              "creep place: the wheels' drive found by its call's bytes, the call's target a function's start with the drive's first bytes; not where the "
              "call stands twice, does not go to a start, or another mod holds the drive");
    }

    // GearboxHook's choice (review 6, G6): made-up finds at base 0, two found once at the test's own functions, one held,
    // one there twice, one unplaced, the rest not found; the guard against a second attach; the unhook; and a failed
    // transaction (a found function at address 0) that leaves nothing hooked and no way through
    {
        FunctionFind finds[kFnCount] = {};
        finds[kFnShiftGear] = FunctionFind{ kFnFound, (size_t)(uintptr_t)&TestTarget, 1, false };
        finds[kFnShiftToAuto] = FunctionFind{ kFnHeld, 0x100, 1, false };
        finds[kFnShiftToHigh] = FunctionFind{ kFnFound, 0x200, 2, false };
        finds[kFnShiftToNeutral] = FunctionFind{ kFnUnplaced, 0, 0, false };
        finds[kFnAutoOffAndShift] = FunctionFind{ kFnFound, (size_t)(uintptr_t)&TestTarget2, 1, false };
        char why[1024];
        const int hooked = GearboxHook(0, finds, why, sizeof why);
        const bool reasons = strstr(why, "ShiftToAutoGear: another mod holds it") && strstr(why, "ShiftToHigh: its bytes stand more than once") &&
                             strstr(why, "ShiftToReverse: not found") && strstr(why, "ShiftToNeutral: not looked for") && strstr(why, "SetPowerFactor: not found") &&
                             strstr(why, "SetCurrentVehicle: not found") && !strstr(why, "GetMaxGear");
        void *const way = GearboxOriginal(kFnShiftGear);
        const bool ways = way && way != (void *)&TestTarget && GearboxOriginal(kFnAutoOffAndShift) && !GearboxOriginal(kFnShiftToHigh) && !GearboxOriginal(kFnMaxGear);
        char again[256];
        const bool guarded = GearboxHook(0, finds, again, sizeof again) == 2 && GearboxHooked();
        GearboxUnhook();
        const bool unhooked = !GearboxHooked() && !GearboxOriginal(kFnShiftGear) && TestTarget(4, 5) == 17 && TestTarget2(4, 5) == 31;
        FunctionFind nowhere[kFnCount] = {};
        nowhere[kFnShiftGear] = FunctionFind{ kFnFound, 0, 1, false };
        char failed[512];
        const bool refused = GearboxHook(0, nowhere, failed, sizeof failed) == 0 && strstr(failed, "Detours could not attach") && !GearboxHooked() &&
                             !GearboxOriginal(kFnShiftGear);
        Check(hooked == 2 && reasons && ways && guarded && unhooked && refused,
              "gearbox hooks: only functions found once and not held are hooked, each other one with its reason; a second attach changes nothing; the unhook "
              "gives the functions back; a failed transaction hooks none");
    }

    // the probe's text and what makes it write
    MakeGame();
    TruckFind(kMemory, r);
    TruckRead(kMemory, r, t);
    WorldRead(kMemory, r.control, w);
    char first[640], second[1024];
    TruckProbeText(t, w, 4.9f, first, sizeof first, second, sizeof second);
    Check(strstr(first, "gear 3 (engaged 3) automatic 1 power 1.000 = A") && strstr(first, "speed 5.00 m/s = 18.0 km/h (4.90 m/s by the way gone)") && strstr(first, "fuel 60.00 of 75.00 l") &&
          strstr(first, "engine damage 30 of 150") && strstr(second, " 2:11.50 2:12.50 1:13.50 1:14.50") && strstr(second, "gear speeds 1.500 3.000 6.000 12.000 16.000 8.000") &&
          strstr(second, "menu 0 loading 0"), "probe: the two lines carry the values as read");
    {
        // the mod's gearbox has the truck: the model's letter, as the cluster's; a numbered gear reads as its number
        // where the game's own rule would read the first gear's factor as L+ (the first drives, 2026-10-26)
        TruckRaw g1 = t;
        g1.gear = 1;
        g1.automatic = 0;
        g1.powerFactor = 1.0f;
        char own[640], game[640], low[640];
        TruckProbeText(g1, w, 4.9f, game, sizeof game, second, sizeof second);
        TruckProbeText(g1, w, 4.9f, own, sizeof own, second, sizeof second, "");
        TruckProbeText(g1, w, 4.9f, low, sizeof low, second, sizeof second, "L");
        Check(strstr(game, "power 1.000 = L+") && strstr(own, "power 1.000 = 1 |") && strstr(low, "power 1.000 = L |"),
              "probe: while the mod's gearbox has the truck the letter is the model's, a numbered gear its number");
    }
    TruckRaw t2 = t;
    World w2 = w;
    t2.speed += 0.5f;
    t2.fuel -= 0.01f;
    const bool quiet = !TruckProbeEvent(t2, w2, t, w);
    t2.handbrake = 1;
    const bool lever = TruckProbeEvent(t2, w2, t, w);
    t2 = t;
    w2.mapOpen = 1;
    Check(quiet && lever && TruckProbeEvent(t2, w2, t, w), "probe: a switch or the game's state writes at once, a changing speed does not");

    // the maps by the game's names, and the map its save names as loaded last
    {
        Check(MapIdValid("level_ru_17_01") && MapIdValid("level_my_map") && !MapIdValid("level_") && !MapIdValid("Level_us_01_01") && !MapIdValid("level_us_01 01"),
              "maps: a map's id is level_ and lower case letters, digits and _");
        Check(MapIdCore("level_us_01_04_new") == 14 && MapIdCore("level_ru_02_01_crop") == 14 && MapIdCore("level_us_01_01") == 14 && MapIdCore("level_my_map") == 0 &&
                  MapIdCore("level_us_01_0") == 0 && MapIdCore("level_us_01_012") == 0,
              "maps: the id up to the map's number, with or without more after it");
        const MapRegion *drummond = MapRegionOf("level_us_01_04_new"), *zurdania = MapRegionOf("level_ru_17_01"), *wisconsin = MapRegionOf("level_us_03_02");
        Check(drummond && !strcmp(drummond->name, "Michigan") && zurdania && !strcmp(zurdania->name, "Zurdania") && wisconsin && !strcmp(wisconsin->name, "Wisconsin") &&
                  !MapRegionOf("level_us_99_01") && !MapRegionOf("level_my_map"),
              "maps: the region of a map by its id, none for a region the game's texts do not know or a mod's map");
        const char *drummondName = MapNameOf("level_us_01_04_new"), *crop = MapNameOf("level_ru_02_01_crop");
        Check(drummondName && !strcmp(drummondName, "Drummond Island") && crop && !strcmp(crop, "Quarry") && MapNameOf("level_us_03_02") && !strcmp(MapNameOf("level_us_03_02"), "Grainwoods River") && !MapNameOf("level_us_01_0"),
              "maps: a map's English name, also with more after its number (Grainwoods River, under its id in the texts); none for an id that is no map's");
        char title[96], title2[96], title3[96];
        MapTitle("level_ru_17_01", title, sizeof title);
        MapTitle("level_us_03_09", title2, sizeof title2);
        MapTitle("level_my_map", title3, sizeof title3);
        Check(!strcmp(title, "Zurdanian Valley, Zurdania") && !strcmp(title2, "level_us_03_09, Wisconsin") && !strcmp(title3, "level_my_map"), "maps: a map in words");
        bool regionsKnown = true, idsOnce = true;
        for (const auto &m : kMapLevels)
        {
            regionsKnown &= MapRegionOf(m.id) != nullptr && MapIdCore(m.id) == strlen(m.id);
            int seen = 0;
            for (const auto &n : kMapLevels) seen += !strcmp(n.id, m.id);
            idsOnce &= seen == 1;
        }
        Check(regionsKnown && idsOnce && sizeof kMapLevels / sizeof kMapLevels[0] == 56 && sizeof kMapRegions / sizeof kMapRegions[0] == 22,
              "maps: 56 named maps, each once and in one of the 22 regions (Switzerland, Season 19, the newest)");

        // a save as the game writes it, its end a 0
        static const char save[] = "{\"CompleteSave\":{\"SslType\":\"CompleteSave\",\"SslValue\":{\"persistentProfileData\":{\"trucksInWarehouse\":"
                                   "[{\"retainedMapId\":\"level_us_15_01\"}]},\"lastLoadedLevel\":\"level_ru_17_01\",\"visitedLevels\":[\"level_us_01_01\"]}},"
                                   "\"cfg_version\":1}";
        char loaded[64] = "";
        const bool read = MapFromSave(save, sizeof save, loaded, sizeof loaded) && !strcmp(loaded, "level_ru_17_01");
        const char *value = strstr(save, "level_ru_17_01");
        char cut[64] = "x";
        const bool partial = !MapFromSave(save, (size_t)(value - save) + 5, cut, sizeof cut) && !strcmp(cut, "x");
        static const char spaced[] = "{ \"lastLoadedLevel\" :\r\n \"level_us_01_04_new\" }";
        char spacedOut[64] = "";
        const bool spaces = MapFromSave(spaced, sizeof spaced - 1, spacedOut, sizeof spacedOut) && !strcmp(spacedOut, "level_us_01_04_new");
        static const char notId[] = "{\"lastLoadedLevel\":\"<none>\"}", noKey[] = "{\"retainedMapId\":\"level_us_15_01\"}", noValue[] = "{\"lastLoadedLevel\":\"\"}";
        char wrongOut[64] = "x", tooShort[8] = "x";
        const bool refused = !MapFromSave(notId, sizeof notId - 1, wrongOut, sizeof wrongOut) && !MapFromSave(noKey, sizeof noKey - 1, wrongOut, sizeof wrongOut) &&
                             !MapFromSave(noValue, sizeof noValue - 1, wrongOut, sizeof wrongOut) && !MapFromSave(save, sizeof save, tooShort, sizeof tooShort) && !strcmp(wrongOut, "x");
        Check(read && partial && spaces && refused,
              "maps: the map loaded last from a save's text; nothing from a save cut short, a value that is no map's id, a save without it, or no room for it");
        // an empty slot, as the game writes CompleteSave1.cfg a few seconds after the slot played (25 October)
        static const char emptySlot[] = "{\"CompleteSave1\":{\"SslValue\":{\"lastLoadedLevel\" : \"\",\"discoveredObjects\":[]}}}";
        const bool empty = SaveSlotEmpty(emptySlot, sizeof emptySlot - 1) && !SaveSlotEmpty(save, sizeof save) && !SaveSlotEmpty(noKey, sizeof noKey - 1) &&
                           !SaveSlotEmpty(save, (size_t)(value - save) - 1) && !SaveSlotEmpty(notId, sizeof notId - 1);
        Check(empty, "maps: an empty slot (its lastLoadedLevel \"\") told from a save that names a map, one without it and one cut short");
    }

    // the game's gearbox functions in a made up exe's code: each one's bytes put in place, a hook's jump over one
    {
        uint8_t parsed[kPatternMax], parsedMask[kPatternMax];
        const int parsedLength = PatternParse("48 ? FF", parsed, parsedMask);
        Check(parsedLength == 3 && parsed[0] == 0x48 && !parsedMask[1] && parsedMask[2] && parsed[2] == 0xFF && !PatternParse("4G", parsed, parsedMask) &&
                  !PatternParse("4", parsed, parsedMask),
              "gearbox code: a pattern read as bytes and a mask");
        std::vector<uint8_t> code(0x10000, 0x90);
        size_t placed[kFnCount] = { 0x1000, 0x2000, 0x3000, 0x3400, 0x3200, 0x4000, 0x5000, 0x6000, 0x7000 };
        auto put = [&](int f, size_t at) {
            uint8_t b[kPatternMax], k[kPatternMax];
            const int length = PatternParse(kGameFunctions[f].pattern, b, k);
            for (int i = 0; i < length; i++) code[at + i] = k[i] ? b[i] : (uint8_t)(0x11 + i);
        };
        for (int f = 0; f < kFnCount; f++) put(f, placed[f]);
        put(kFnShiftToNeutral, 0x0800); // its few bytes elsewhere too, before ShiftToHigh
        FunctionFind clean[kFnCount];
        GameFunctionsFind(code.data(), 0x400, code.size(), clean);
        bool allFound = true;
        for (int f = 0; f < kFnCount; f++) allFound &= clean[f].state == kFnFound && clean[f].at == placed[f] && clean[f].count == 1;
        // a hook's jump over ShiftGear's, ShiftToAutoGear's and GetMaxGear's starts, int3 over the rest of the
        // instructions it moved (ShiftToAutoGear's first two are 9 bytes, GetMaxGear's 8), and SetPowerFactor's bytes a
        // second time
        code[0x1000] = 0xE9;
        code[0x1001] = code[0x1002] = code[0x1003] = code[0x1004] = 0x7F;
        code[0x2000] = 0xFF;
        code[0x2001] = 0x25;
        for (int i = 6; i < 9; i++) code[0x2000 + i] = 0xCC;
        code[0x4000] = 0xE9;
        for (int i = 5; i < 8; i++) code[0x4000 + i] = 0xCC;
        put(kFnSetPower, 0x9000);
        FunctionFind hooked[kFnCount];
        GameFunctionsFind(code.data(), 0x400, code.size(), hooked);
        const bool heldSeen = hooked[kFnShiftGear].state == kFnHeld && hooked[kFnShiftGear].at == 0x1000 && hooked[kFnShiftToAuto].state == kFnHeld &&
                              hooked[kFnShiftToAuto].at == 0x2000 && hooked[kFnMaxGear].state == kFnHeld && hooked[kFnMaxGear].at == 0x4000 &&
                              hooked[kFnSetPower].state == kFnFound && hooked[kFnSetPower].count == 2;
        // a short function's bytes changed are not taken for a hook: too little is left to tell it by
        code[0x3400] = 0xE9;
        FunctionFind shortGone[kFnCount];
        GameFunctionsFind(code.data(), 0x400, code.size(), shortGone);
        char line[1024];
        GameFunctionsText(hooked, line, sizeof line);
        char line2[1024];
        GameFunctionsText(shortGone, line2, sizeof line2);
        Check(allFound && heldSeen && shortGone[kFnShiftToReverse].state == kFnNotFound && shortGone[kFnShiftToReverse].tooShort &&
                  shortGone[kFnShiftToNeutral].state == kFnUnplaced && strstr(line, "ShiftGear at +1000 held by another mod") &&
                  strstr(line, "SetPowerFactor at +6000 (more than once)") && strstr(line, "ShiftToNeutral at +3200,") &&
                  strstr(line2, "ShiftToReverse not found (too short to tell it behind a hook)") && strstr(line2, "ShiftToNeutral not looked for"),
              "gearbox code: each function found by its bytes, ShiftToNeutral between ShiftToHigh and ShiftToReverse, one behind a hook's jump held by another mod");
    }

    // the game's statics by the code that uses them (game_globals.h): a made up exe whose code reaches a static by a
    // load, a compare with a byte, a store of a dword and a load of a field 8 past it, a call near the load; the
    // signatures harvested from it find the static in a second build where the code and the static have moved, and
    // a signature found twice or one that points elsewhere is not taken
    {
        auto build = [](size_t shift, uint64_t place, std::vector<uint8_t> &image) {
            image.assign(0x20000, 0);
            uint32_t seed = 12345;
            for (size_t i = 0x1000; i < 0xF000; i++)
            {
                seed = seed * 1103515245u + 12345u;
                image[i + shift] = (uint8_t)(seed >> 16);
            }
            auto ref = [&](size_t at, const std::vector<uint8_t> &head, int after, int into) {
                const size_t p = at + shift + head.size();
                memcpy(&image[at + shift], head.data(), head.size());
                const int32_t disp = (int32_t)((int64_t)place + into - (int64_t)(p + 4 + after));
                memcpy(&image[p], &disp, 4);
            };
            ref(0x2000, { 0x48, 0x8B, 0x05 }, 0, 0);       // mov rax, [rip + d]
            const size_t call = 0x2000 + shift - 5;        // a call just before it, to a place that moves with the build
            image[call] = 0xE8;
            const int32_t to = (int32_t)(0x1800 + shift - (call + 5));
            memcpy(&image[call + 1], &to, 4);
            ref(0x3000, { 0x80, 0x3D }, 1, 0);             // cmp byte [rip + d], 0
            image[0x3000 + shift + 6] = 0x00;
            ref(0x4000, { 0xC7, 0x05 }, 4, 0);             // mov dword [rip + d], 7
            const int32_t seven = 7;
            memcpy(&image[0x4000 + shift + 6], &seven, 4);
            ref(0x5000, { 0x48, 0x8B, 0x0D }, 0, 8);       // mov rcx, [rip + d]: the field 8 past it
        };
        std::vector<uint8_t> oldBuild, newBuild;
        build(0, 0x18000, oldBuild);
        build(0x40, 0x18800, newBuild);
        GlobalHarvested sigs[6];
        const int harvested = GlobalHarvest(oldBuild.data(), 0x1000, 0xF000, oldBuild.size(), 0x18000, 16, sigs, 6);
        std::vector<GlobalSig> table;
        for (int i = 0; i < harvested; i++) table.push_back(GlobalSig{ kGlobControl, sigs[i].pattern, sigs[i].at, sigs[i].after, sigs[i].into });
        table.push_back(GlobalSig{ -1, "", 0, 0, 0 });
        int signatures = 0;
        bool conflict = false;
        const uint64_t again = GlobalResolve(oldBuild.data(), 0x1000, 0xF000, table.data(), kGlobControl, signatures, conflict);
        const bool sameBuild = again == 0x18000 && signatures == harvested && !conflict;
        const uint64_t moved = GlobalResolve(newBuild.data(), 0x1000, 0xF040, table.data(), kGlobControl, signatures, conflict);
        const bool otherBuild = moved == 0x18800 && signatures == harvested && !conflict;
        bool callLeftOut = false, kinds = harvested == 4;
        for (int i = 0; i < harvested; i++)
        {
            callLeftOut = callLeftOut || strstr(sigs[i].pattern, "E8 ? ? ? ? 48 8B 05 ? ? ? ?") != nullptr;
            kinds = kinds && (sigs[i].after == 0 || sigs[i].after == 1 || sigs[i].after == 4);
        }
        const bool fieldSeen = harvested == 4 && sigs[3].into == 8;
        // a signature that points elsewhere in the second build makes a conflict; one found twice is not counted
        std::vector<GlobalSig> askew = table;
        askew.insert(askew.begin(), GlobalSig{ kGlobControl, sigs[0].pattern, sigs[0].at, sigs[0].after, sigs[0].into + 0x10 });
        const uint64_t split = GlobalResolve(newBuild.data(), 0x1000, 0xF040, askew.data(), kGlobControl, signatures, conflict);
        const bool conflictSeen = split == 0 && conflict;
        std::vector<uint8_t> twice = newBuild;
        memcpy(&twice[0x9000], &twice[0x2040 - 13], 40); // the load's bytes again elsewhere
        const uint64_t onceLess = GlobalResolve(twice.data(), 0x1000, 0xF040, table.data(), kGlobControl, signatures, conflict);
        Check(sameBuild && otherBuild && callLeftOut && kinds && fieldSeen && conflictSeen && onceLess == 0x18800 && signatures == harvested - 1,
              "places: the signatures harvested from one build find a static moved in another, a call's distance and the displacement left out; a load, a "
              "compare with a byte, a store of a dword and a field past it; a signature found twice is not taken, two that disagree take neither");
    }
    // the signatures the mod carries: each one a pattern with its displacement in it, of a global the mod knows
    {
        bool carried = kGlobalSigs[0].global >= 0;
        for (const GlobalSig *sig = kGlobalSigs; sig->global >= 0; sig++)
        {
            uint8_t b[kPatternMax], k[kPatternMax];
            const int length = PatternParse(sig->pattern, b, k);
            carried = carried && sig->global < kGlobCount && length > 0 && sig->at >= 0 && sig->at + 4 <= length && !k[sig->at] && !k[sig->at + 3];
        }
        Check(carried, "places: each signature the mod carries reads as a pattern, its displacement left out");
    }
    // a static the code never names, a field 0x48 into an object the code reaches by its start (lea rcx, [rip + d]),
    // with another instruction's displacement cut by the window's end and a load of a nearer field: the signature is
    // of the object's start, 0x48 before the static, and finds it in a second build where the code and the object
    // have moved
    {
        auto build = [](size_t shift, uint64_t object, std::vector<uint8_t> &image) {
            image.assign(0x20000, 0);
            uint32_t seed = 777;
            for (size_t i = 0x1000; i < 0xF000; i++)
            {
                seed = seed * 1103515245u + 12345u;
                image[i + shift] = (uint8_t)(seed >> 16);
            }
            auto lea = [&](size_t at, uint8_t modrm, uint64_t target) {
                const size_t p = at + shift + 3;
                image[at + shift] = 0x48;
                image[at + shift + 1] = 0x8D;
                image[at + shift + 2] = modrm;
                const int32_t disp = (int32_t)((int64_t)target - (int64_t)(p + 4));
                memcpy(&image[p], &disp, 4);
            };
            lea(0x6000, 0x0D, object);          // lea rcx, [rip + d]: the object's start
            lea(0x6000 + 3 + 4 + 10 - 2 - 3, 0x15, 0x1400); // lea rdx, [rip + d] to code that stays: its displacement's first two bytes in the window
            lea(0x7000, 0x0D, object);
            // a load of another field, nearer the static but no object's start: not taken
            image[0x8000 + shift] = 0x48;
            image[0x8001 + shift] = 0x8B;
            image[0x8002 + shift] = 0x05;
            const int32_t field = (int32_t)((int64_t)object + 0x40 - (int64_t)(0x8003 + shift + 4));
            memcpy(&image[0x8003 + shift], &field, 4);
        };
        std::vector<uint8_t> oldBuild, newBuild;
        build(0, 0x19000, oldBuild);
        build(0x40, 0x19800, newBuild);
        GlobalHarvested sigs[4];
        GlobalHarvestNote note;
        const int harvested = GlobalHarvest(oldBuild.data(), 0x1000, 0xF000, oldBuild.size(), 0x19048, 1, sigs, 4, &note);
        std::vector<GlobalSig> table;
        for (int i = 0; i < harvested; i++) table.push_back(GlobalSig{ kGlobGame, sigs[i].pattern, sigs[i].at, sigs[i].after, sigs[i].into });
        table.push_back(GlobalSig{ -1, "", 0, 0, 0 });
        int signatures = 0;
        bool conflict = false;
        const uint64_t again = GlobalResolve(oldBuild.data(), 0x1000, 0xF000, table.data(), kGlobGame, signatures, conflict);
        const uint64_t moved = GlobalResolve(newBuild.data(), 0x1000, 0xF040, table.data(), kGlobGame, signatures, conflict);
        const bool cut = harvested >= 1 && strlen(sigs[0].pattern) > 12 && !strcmp(sigs[0].pattern + strlen(sigs[0].pattern) - 12, "48 8D 15 ? ?");
        Check(harvested == 2 && note.direct == 0 && note.base == -0x48 && note.baseRefs == 2 && sigs[0].into == -0x48 && again == 0x19048 &&
                  moved == 0x19848 && signatures == 2 && !conflict && cut,
              "places: a static the code never names is found by the object it is a field of, the signature of its start with how far before it "
              "stands; a displacement the window cuts is left out");
    }

    // the game's clock: a made up game with the hour two pointers away from a static ([[exe+static]+0x30]+0x44), a
    // day of 24 minutes, a decoy that rises at a pace of its own, and a value that stands. Both rising places become
    // steady, neither is taken while there are two, and the night skipped on the map (a gap, the hour 9 on) makes the
    // hour the clock
    {
        Block(kExe + kClockStaticFrom, kClockStaticTo - kClockStaticFrom);
        const uint64_t sky = 0x10200000, decoy = 0x10210000, deep = 0x10220000;
        Block(sky, 0x1000);
        Block(decoy, 0x1000);
        Block(deep, 0x1000);
        Put(kExe + kClockStaticFrom + 0x100, sky);
        Put(sky + 0x30, deep);
        Put(kExe + kClockStaticFrom + 0x108, decoy);
        float hour = 21.0f, timer = 3.0f, clock = 100.0f;
        auto set = [&]() {
            Put(deep + 0x44, hour);
            Put(decoy + 0x10, timer);
            Put(sky + 0x8, 5.0f);
        };
        set();
        ClockScan scan;
        ClockScanBegin(scan, kMemory, 0x100000);
        const bool begun = scan.state == kClockSearching && scan.objects.size() == 4 && scan.slots.size() >= 3;
        int steadyCount = 0;
        bool foundEarly = false;
        auto drive = [&](float seconds, bool driving) {
            for (float at = 0.0f; at < seconds; at += 1.0f)
            {
                if (driving) clock += 1.0f;
                hour += driving ? 24.0f / (24.0f * 60.0f) : 0.0f;
                if (hour >= 24.0f) hour -= 24.0f;
                timer += driving ? 0.005f : 0.0f;
                set();
                const ClockNews news = ClockScanStep(scan, kMemory, kVehicle, clock, driving);
                steadyCount += news.steadyNow;
                foundEarly = foundEarly || news.found;
            }
        };
        drive(120.0f, true);
        const bool twoSteady = scan.state == kClockSearching && steadyCount == 2 && !foundEarly && scan.slots.size() == 2;
        // the night skipped on the map: the truck not driven, the hour 9 on
        drive(3.0f, false);
        hour += 9.0f;
        if (hour >= 24.0f) hour -= 24.0f;
        set();
        drive(10.0f, true);
        float read = -1.0f;
        const bool found = scan.state == kClockFound && scan.found.depth == 2 && scan.found.rva == kClockStaticFrom + 0x100 && scan.found.off[0] == 0x30 &&
                           scan.found.off[1] == 0x44 && !scan.found.isDouble && scan.found.scale == 1.0f && ClockRead(kMemory, scan.found, read) && fabsf(read - hour) < 0.001f;
        char text[64];
        ClockPath parsed = {};
        ClockPathText(scan.found, text, sizeof text);
        const bool roundTrip = ClockPathParse(text, parsed) && parsed.rva == scan.found.rva && parsed.depth == 2 && parsed.off[1] == 0x44 && parsed.scale == 1.0f &&
                               !ClockPathParse("2a8eb90,3,0,0,f,1", parsed) && !ClockPathParse("nonsense", parsed);
        Check(begun && twoSteady && found && roundTrip, "clock: the game's hour found two pointers from a static: steady among a decoy, taken when the night is skipped, kept as text");
        // the only steady place is taken after kClockAlone without a jump
        Put(decoy + 0x10, 0.0f);
        ClockScan single;
        ClockScanBegin(single, kMemory, 0x100000);
        for (float at = 0.0f; at < kClockAlone + 30.0f; at += 1.0f)
        {
            clock += 1.0f;
            hour += 24.0f / (24.0f * 60.0f);
            if (hour >= 24.0f) hour -= 24.0f;
            Put(deep + 0x44, hour);
            ClockScanStep(single, kMemory, kVehicle, clock, true);
        }
        Check(single.state == kClockFound && single.found.off[1] == 0x44, "clock: the only place that rises at a day's pace is taken after three minutes");
    }

    // the suspension probe (suspension_scan.h): a made up wheel whose body holds a constraint at + 0x40 (two pointers
    // from the wheel), the same constraint again at + 0x48, a wheel model holding a second one itself at + 0x60, and
    // a decoy of another class. Each constraint once for each root, with its path, and its floats as text
    {
        const uint64_t vtable = kExe + 0x23d9538, wheel = 0x10300000, model = 0x10310000, body = 0x10320000, inBody = 0x10330000, inModel = 0x10340000, decoy = 0x10350000;
        for (uint64_t at : { wheel, model, body, inBody, inModel, decoy }) Block(at, 0x800);
        Put(wheel, kExe + 0x100);
        Put(wheel + 0x18, body);
        Put(wheel + 0x20, decoy);
        Put(body + 0x40, inBody);
        Put(body + 0x48, inBody);
        Put(model + 0x60, inModel);
        Put(inBody, vtable);
        Put(inModel, vtable);
        Put(decoy, kExe + 0x23d9540);
        Put(inBody + 0x30, 1.5f);
        Put(inBody + 0x34, 0.25f);
        SuspHit hits[8];
        const int n = SuspScan(kMemory, 0x100000, &wheel, &model, 1, vtable, hits, 8);
        char path[96], floats[1400];
        SuspHitWords(hits[0], path, sizeof path);
        const bool found = n == 2 && hits[0].at == inBody && hits[0].depth == 2 && hits[0].off[0] == 0x18 && hits[0].off[1] == 0x40 && !hits[0].fromModel &&
                           hits[1].at == inModel && hits[1].depth == 1 && hits[1].off[0] == 0x60 && hits[1].fromModel && !strcmp(path, "wheel 0's object [[+0x18]+0x40]");
        const bool text = SuspFloatsText(kMemory, inBody, vtable, floats, sizeof floats) && strstr(floats, "| +30 1.5 0.25 ") && !SuspFloatsText(kMemory, decoy, vtable, floats, sizeof floats);
        Check(found && text, "suspension probe: a constraint two pointers from the wheel and one in its model, each once, with its path and floats; a decoy left out");
    }

    // the suspension tune (suspension_scan.h): a made up wheel whose constraint at + 0x28 holds the Arocs' front
    // figures. The stock times the slider, never times what is there; held to 1; the game's own new value taken as
    // the stock; 1 puts the stock back; a new constraint read anew; another class never written
    {
        const uint64_t vtable = kExe + 0x23d9538, wheel = 0x10400000, first = 0x10410000, second = 0x10420000;
        for (uint64_t at : { wheel, first, second }) Block(at, 0x200);
        Put(wheel + 0x28, first);
        Put(first, vtable);
        Put(first + 0xC4, 0.026f);
        Put(first + 0xC8, 0.4f);
        Put(first + 0xB4, -0.6f);
        Put(first + 0xB8, 0.4f);
        TruckRefs refs = {};
        refs.vehicle = kVehicle;
        refs.wheel[0] = wheel;
        refs.wheels = 1;
        SuspTune tune = {};
        auto at = [&](uint64_t c, uint32_t off) { float v = 0.0f; Peek(c + off, &v, sizeof v); return v; };
        const float same[kSuspValues] = { 1.0f, 1.0f, 1.0f, 1.0f }, soft[kSuspValues] = { 1.5f, 0.5f, 1.0f, 1.0f }, hard[kSuspValues] = { 2.0f, 2.0f, 1.0f, 1.0f };
        const bool none = SuspTuneStep(tune, kMemory, refs, vtable, same) == 0 && tune.wheel[0].fresh && tune.wheel[0].stock[0] == 0.026f && at(first, 0xC4) == 0.026f;
        const bool once = SuspTuneStep(tune, kMemory, refs, vtable, soft) == 1 && Near(at(first, 0xC4), 0.039f) && Near(at(first, 0xC8), 0.2f) && Near(tune.wheel[0].back[0], 0.039f);
        const bool steady = SuspTuneStep(tune, kMemory, refs, vtable, soft) == 0 && !tune.moved && Near(at(first, 0xC4), 0.039f) && tune.wheel[0].stock[0] == 0.026f &&
                            at(first, 0xB4) == -0.6f && at(first, 0xB8) == 0.4f;
        const bool held = SuspTuneStep(tune, kMemory, refs, vtable, hard) == 1 && tune.moved && tune.wheel[0].settle && tune.wheel[0].writes == 0 && Near(at(first, 0xC4), 0.052f) &&
                          at(first, 0xC8) == 0.8f;
        Put(first + 0xC4, 0.03f); // the game rebuilt the spring in place (another suspension fitted)
        const bool anew = SuspTuneStep(tune, kMemory, refs, vtable, hard) == 1 && tune.wheel[0].fresh && tune.wheel[0].stock[0] == 0.03f && Near(at(first, 0xC4), 0.06f) &&
                          tune.wheel[0].stock[1] == 0.4f && at(first, 0xC8) == 0.8f;
        const bool back = SuspTuneStep(tune, kMemory, refs, vtable, same) == 1 && at(first, 0xC4) == 0.03f && at(first, 0xC8) == 0.4f &&
                          SuspTuneStep(tune, kMemory, refs, vtable, same) == 0;
        Put(first + 0xC8, 0.9f);
        const bool top = SuspTuneStep(tune, kMemory, refs, vtable, hard) == 1 && at(first, 0xC8) == 1.0f;
        Put(second, kExe + 0x23d9540);
        Put(second + 0xC4, 0.02f);
        Put(second + 0xC8, 0.4f);
        Put(wheel + 0x28, second);
        const bool other = SuspTuneStep(tune, kMemory, refs, vtable, hard) == 0 && at(second, 0xC4) == 0.02f;
        Put(second, vtable);
        const bool rebuilt = SuspTuneStep(tune, kMemory, refs, vtable, soft) == 1 && tune.wheel[0].constraint == second && Near(at(second, 0xC4), 0.03f);
        // the travel limits: each keeps its side of 0, a limit of a kind not known (0 here) is left as it is, then
        // both known and scaled; back at 1 they are the stock again
        const float travel[kSuspValues] = { 1.5f, 0.5f, 1.5f, 0.5f };
        const bool unknown = SuspTuneStep(tune, kMemory, refs, vtable, travel) == 0 && at(second, 0xB4) == 0.0f && at(second, 0xB8) == 0.0f;
        Put(second + 0xB4, -0.44f);
        Put(second + 0xB8, 0.4f);
        const bool limits = SuspTuneStep(tune, kMemory, refs, vtable, travel) == 1 && Near(at(second, 0xB4), -0.66f) && Near(at(second, 0xB8), 0.2f) &&
                            Near(at(second, 0xC4), 0.03f) && tune.wheel[0].stock[kSuspMin] == -0.44f && SuspTuneStep(tune, kMemory, refs, vtable, travel) == 0;
        const bool limitsBack = SuspTuneStep(tune, kMemory, refs, vtable, same) == 1 && at(second, 0xB4) == -0.44f && at(second, 0xB8) == 0.4f && at(second, 0xC4) == 0.02f;
        Check(none && once && steady && held && anew && back && top && other && rebuilt && unknown && limits && limitsBack,
              "suspension tune: the stock times the slider and never times what is there, held to 1, the game's new value taken as the stock, 1 puts the stock back, "
              "a rebuilt constraint read anew, another class never written; the travel limits scaled on their side of 0, an unknown one left alone; a moving slider's writes not counted");
    }

    // a truck left tuned (review 15): truck A tuned, truck B driven, A again. A gets its stock back when B is driven,
    // so A's stock is read as the truck's own again and the slider scales it once, never twice
    {
        const uint64_t vtable = kExe + 0x23d9538, wheelA = 0x10430000, wheelB = 0x10440000, a = 0x10450000, b = 0x10460000;
        for (uint64_t at : { wheelA, wheelB, a, b }) Block(at, 0x200);
        Put(wheelA + 0x28, a);
        Put(wheelB + 0x28, b);
        for (uint64_t c : { a, b })
        {
            Put(c, vtable);
            Put(c + 0xC4, 0.026f);
            Put(c + 0xC8, 0.4f);
            Put(c + 0xB4, -0.6f);
            Put(c + 0xB8, 0.4f);
        }
        TruckRefs truckA = {}, truckB = {};
        truckA.vehicle = kVehicle;
        truckA.wheel[0] = wheelA;
        truckA.wheels = 1;
        truckB.vehicle = kOtherVehicle;
        truckB.wheel[0] = wheelB;
        truckB.wheels = 1;
        auto at = [&](uint64_t c, uint32_t off) { float v = 0.0f; Peek(c + off, &v, sizeof v); return v; };
        const float twice[kSuspValues] = { 2.0f, 1.0f, 2.0f, 1.0f };
        SuspTune tune = {};
        int restored = -1;
        const bool tuned = SuspTuneStep(tune, kMemory, truckA, vtable, twice, &restored) == 1 && restored == 0 && Near(at(a, 0xC4), 0.052f) && Near(at(a, 0xB4), -1.2f);
        const bool left = SuspTuneStep(tune, kMemory, truckB, vtable, twice, &restored) == 1 && restored == 2 && at(a, 0xC4) == 0.026f && at(a, 0xB4) == -0.6f &&
                          Near(at(b, 0xC4), 0.052f);
        const bool again = SuspTuneStep(tune, kMemory, truckA, vtable, twice, &restored) == 1 && restored == 2 && tune.wheel[0].stock[kSuspStrength] == 0.026f &&
                           Near(at(a, 0xC4), 0.052f) && Near(at(a, 0xB4), -1.2f) && at(b, 0xC4) == 0.026f;
        // a value the game wrote in the meantime is the game's, never overwritten by the old stock
        Put(a + 0xC8, 0.5f);
        Put(a + 0xC4, 0.03f);
        const bool theirs = SuspTuneStep(tune, kMemory, truckB, vtable, twice, &restored) == 1 && restored == 1 && at(a, 0xC4) == 0.03f && at(a, 0xB4) == -0.6f;
        Check(tuned && left && again && theirs, "suspension tune: a truck left tuned gets its stock back when another is driven, so driven again it is scaled once and never twice; "
                                                "a value the game wrote in the meantime is left as the game's");
    }

    // the travel probe (suspension_scan.h): a chassis turned a quarter round, its forward along the world's z and its
    // left along -x. A wheel 2 m ahead, 0.5 m down and 1 m to the left is that in the chassis's frame; rising 1 cm in
    // 0.05 s of the game's clock is 0.2 m/s, and a pass with the clock standing adds nothing
    {
        TruckRaw t = {};
        t.waterKnown = t.clockKnown = true;
        t.wheels = 1;
        const float forward[3] = { 0.0f, 0.0f, 1.0f }, up[3] = { 0.0f, 1.0f, 0.0f }, left[3] = { -1.0f, 0.0f, 0.0f }, at[3] = { 10.0f, 5.0f, 20.0f };
        memcpy(t.forward, forward, sizeof forward);
        memcpy(t.up, up, sizeof up);
        memcpy(t.left, left, sizeof left);
        memcpy(t.position, at, sizeof at);
        t.wheelAt[0][0] = 9.0f;
        t.wheelAt[0][1] = 4.5f;
        t.wheelAt[0][2] = 22.0f;
        t.clock = 100.0f;
        float local[3];
        SuspLocal(t, 0, local);
        SuspTravel s = {};
        const bool frame = Near(local[0], 2.0f) && Near(local[1], -0.5f) && Near(local[2], 1.0f) && SuspTravelStep(s, t, kVehicle) && s.looks == 1;
        t.wheelAt[0][1] = 4.51f;
        t.clock = 100.05f;
        const bool rise = SuspTravelStep(s, t, kVehicle) && Near(s.most[0], -0.49f) && Near(s.least[0], -0.5f) && fabsf(s.fastest[0] - 0.2f) < 0.002f;
        t.wheelAt[0][1] = 4.0f;
        const bool still = SuspTravelStep(s, t, kVehicle) && s.looks == 2 && Near(s.least[0], -0.5f);
        SuspTravelWindow(s, 1);
        t.waterKnown = false;
        const bool window = s.looks == 1 && s.fastest[0] == 0.0f && Near(s.least[0], -0.49f) && Near(s.most[0], -0.49f) && !SuspTravelStep(s, t, kVehicle);
        Check(frame && rise && still && window, "travel probe: a wheel's centre in a turned chassis's frame, its range and its speed by the game's clock; a standing clock adds nothing");
    }

    // the hook on the wheels' drive as a C++ function (BRIEF_suspension_step_probe.md, part 1): with no share and no
    // step look the drive gets its three arguments as they came; the look gets the call's action and seconds before
    // the drive; a throttle that is not a number takes the share, as the stub's maxss did
    {
        uint8_t truck[0x100] = {};
        void (*volatile drive)(void *, float, float) = &FakeDrive;
        char why[160] = "";
        const bool hooked = CreepHook((uintptr_t)&FakeDrive, why, sizeof why);
        CreepGive(0, 0.0f);
        DriveStepSet(nullptr);
        drive(truck, 0.37f, 0.0125f);
        const bool plain = g_driveThrottle == 0.37f && g_driveDt == 0.0125f && g_driveAction == truck;
        DriveStepSet(&FakeStepLook);
        g_lookAction = 0;
        drive(truck, 0.37f, 0.0125f);
        const bool looked = g_lookAction == (uint64_t)(uintptr_t)truck && g_lookDt == 0.0125f && g_driveThrottle == 0.37f;
        DriveStepSet(nullptr);
        CreepGive((uint64_t)(uintptr_t)truck, 0.2f);
        drive(truck, nanf(""), 0.0125f);
        const bool notNumber = g_driveThrottle == 0.2f;
        CreepGive(0, 0.0f);
        Check(hooked && plain && looked && notNumber, "drive hook (C++): with no share and no look the drive gets its action, throttle and seconds as they came; the step look gets the "
                                                      "action and the seconds before the drive; a throttle that is not a number takes the share, as the stub's maxss did");
    }

    // the block handed to the physics thread (suspension_step.h): a reader takes it whole or not at all; the two slots
    // alternate; a block whose seal is not its serial (being written) is refused
    {
        static SuspStepBlock slots[2];
        static SuspStepBlock *volatile slot = nullptr;
        int next = 0;
        uint32_t serial = 0;
        SuspStepBlock b = {}, got = {};
        b.action = 0x1234;
        b.generation = 7;
        b.wheels = 2;
        SuspStepPublish(slots, next, &slot, b, serial);
        const bool first = SuspStepTake(slot, got) && got.action == 0x1234 && got.generation == 7 && slot == &slots[0];
        b.generation = 8;
        SuspStepPublish(slots, next, &slot, b, serial);
        const bool second = SuspStepTake(slot, got) && got.generation == 8 && slot == &slots[1] && slots[0].generation == 7;
        slots[1].seal = 0;
        const bool half = !SuspStepTake(slot, got) && !SuspStepTake(nullptr, got);
        Check(first && second && half, "step block: taken whole, the two slots alternate, one being written (its seal not its serial) is refused");
    }

    // the step's figures from heights: the speed over the step's seconds, a bad step skipped, the histogram's bins, the
    // repeats while moving, the window published at its end and a new generation dropping each wheel's state
    {
        static SuspStepState s;
        memset(&s, 0, sizeof s);
        SuspStepBlock b = {};
        b.generation = 1;
        b.wheels = 1;
        const float step = 1.0f / 128.0f;          // heights and seconds a float holds exactly
        float h = 0.125f;
        SuspStepAdd(s, b, &h, 2.0f, 0.0f, step);
        const bool started = s.have && s.win.steps == 0 && s.h[0] == 0.125f;
        h = 0.125f + step;
        SuspStepAdd(s, b, &h, 2.0f, 0.0f, step);
        const bool speed = s.v[0] == 1.0f && s.win.steps == 1 && s.win.bins[0][0][6] == 1 && SuspStepBin(1.0f) == 6;
        h = 0.125f;
        SuspStepAdd(s, b, &h, 2.0f, 0.0f, 0.0f);   // no seconds: no speed
        SuspStepAdd(s, b, &h, 2.0f, 0.0f, 0.2f);   // a pause: no speed
        const bool skipped = s.win.steps == 1;
        SuspStepAdd(s, b, &h, 2.0f, 0.0f, step);   // the same height while moving: a repeat
        SuspStepAdd(s, b, &h, 0.5f, 0.0f, step);   // the same, standing: no repeat counted
        const bool repeats = s.win.moving[0] == 2 && s.win.repeats[0] == 1 && s.win.steps == 3;
        const bool bins = SuspStepBin(0.01f) == 0 && SuspStepBin(0.03f) == 1 && SuspStepBin(-0.15f) == 3 && SuspStepBin(2.5f) == 7;
        const bool bounce = s.win.bounceSteps[0] >= 1 && s.win.bounce[0][0] == 0.125f + step;
        LONG seen = 0;
        SuspStepWin win;
        for (int i = 0; i < 1000; i++) SuspStepAdd(s, b, &h, 0.0f, 0.0f, 0.01f);   // 10 s: the window ends
        const bool window = SuspStepWindowTake(s, seen, win) && win.generation == 1 && win.steps >= 999 && win.seconds >= kStepWindow && !SuspStepWindowTake(s, seen, win);
        b.generation = 2;
        h = 0.5f;
        for (int i = 0; i < 150; i++) SuspStepAdd(s, b, &h, 0.0f, 0.0f, 0.01f);   // 1.5 s of the new generation
        const bool dropped = s.generation == 2 && s.v[0] == 0.0f && s.win.least[0] == 0.5f;
        b.generation = 3;
        SuspStepAdd(s, b, &h, 0.0f, 0.0f, 0.01f);
        const bool cut = SuspStepWindowTake(s, seen, win) && win.generation == 2 && win.seconds >= 1.0f && win.seconds < 2.0f;
        Check(started && speed && skipped && repeats && bins && bounce && window && dropped && cut,
              "step figures: the speed over the step's seconds, a step with no seconds or a pause skipped, the bins, repeats counted only while moving, a bounce "
              "taken, the window published after 10 s, and a new generation dropping each wheel's state and publishing the window it cut");
    }

    // the step's reads in this process's own memory: a wheel's centre against its body along the body's up axis; a
    // freed object (a bad address) is a fault, counted, and the block is dropped until a new one
    {
        static SuspStepState s;
        memset(&s, 0, sizeof s);
        static float wheel[0x200 / 4], body[0x300 / 4];
        memset(wheel, 0, sizeof wheel);
        memset(body, 0, sizeof body);
        const float at[3] = { 1.0f, 2.3f, 3.0f }, up[3] = { 0.0f, 1.0f, 0.0f }, pos[3] = { 0.0f, 2.0f, 0.0f }, vel[3] = { 3.0f, 0.0f, 4.0f };
        memcpy((uint8_t *)wheel + 0x13C, at, sizeof at);
        memcpy((uint8_t *)body + 0x180, up, sizeof up);
        memcpy((uint8_t *)body + 0x1A0, pos, sizeof pos);
        memcpy((uint8_t *)body + 0x230, vel, sizeof vel);
        static SuspStepBlock slots[2];
        static SuspStepBlock *volatile slot = nullptr;
        int next = 0;
        uint32_t serial = 0;
        SuspStepBlock b = {};
        b.action = 0x5678;
        b.chassis = b.body[0] = (uint64_t)(uintptr_t)body;
        b.wheel[0] = (uint64_t)(uintptr_t)wheel;
        b.wheels = 1;
        b.generation = 1;
        SuspStepPublish(slots, next, &slot, b, serial);
        float h[kMaxWheels] = {}, speed = 0.0f, field = 0.0f;
        const bool read = SuspStepRead(*slot, h, speed, field) && Near(h[0], 0.3f) && Near(speed, 5.0f);
        SuspStepLook(s, slot, 0x9999, 0.01f);
        const bool other = !s.have;
        SuspStepLook(s, slot, 0x5678, 0.01f);
        const bool taken = s.have && Near(s.h[0], 0.3f);
        b.wheel[0] = 0x10;
        SuspStepPublish(slots, next, &slot, b, serial);
        SuspStepLook(s, slot, 0x5678, 0.01f);
        const bool fault = s.faults == 1 && !s.have;
        SuspStepLook(s, slot, 0x5678, 0.01f);
        const bool once = s.faults == 1;
        Check(read && other && taken && fault && once, "step reads: the wheel's centre against its body along its up axis and the chassis's speed; another truck's call is passed by; "
                                                       "a bad address faults once, counted, and its block is dropped until a new one");
    }

    // which body a wheel rides on: the F350's front wheel fits the chassis by its constraint's attach point (0.02 m
    // along, 0.27 m inboard); the Arocs' front wheel 3.2 m behind its attach point does not (review 14)
    {
        float chassis[16] = {};
        chassis[0] = 1.0f, chassis[5] = 1.0f, chassis[10] = 1.0f;     // forward x, up y, left z, at the origin
        const float f350Attach[3] = { 2.2820f, -0.1350f, 0.6950f }, f350Wheel[3] = { 2.2622f, 0.0057f, 0.9650f };
        const float arocsAttach[3] = { -0.4021f, -1.2123f, 0.864f }, arocsWheel[3] = { 2.7850f, 0.6288f, 1.1157f };
        float cab[16];
        memcpy(cab, chassis, sizeof cab);
        cab[12] = 3.19f, cab[13] = 1.84f;                              // a body 3.19 m ahead and 1.84 m up
        Check(SuspBodyFits(chassis, f350Attach, f350Wheel) && !SuspBodyFits(chassis, arocsAttach, arocsWheel) && SuspBodyFits(cab, arocsAttach, arocsWheel),
              "step bodies: the F350's front wheel fits the chassis by its attach point, the Arocs' front wheel does not, and fits a body where its attach point is");
    }

    // SuspStepTest's report on a made-up trace at 128 steps a second: the first step that reads the new value, the steps
    // until the height leaves its trend by 2 mm, and the speed's swing before and after
    {
        static SuspStepTraceEntry e[200];
        for (int i = 0; i < 200; i++)
        {
            const float t = i / 128.0f;
            e[i] = SuspStepTraceEntry{ (uint32_t)(1000 + i), 1.0f / 128.0f, i < 100 ? 0.026f : 0.013f, 0.100f + 0.001f * t, 0.001f };
            if (i >= 103) e[i].h -= 0.0015f * (i - 102);               // sinking from three steps after the write
            if (i >= 100) e[i].v = i % 2 ? 0.05f : -0.05f;
        }
        const int write = SuspStepFirst(e, 200, 0, 0.013f), none = SuspStepFirst(e, 200, 0, 0.5f);
        const int leave = SuspStepLeave(e, 200, write, 0.2f, 0.002f);
        float rmsAfter, mostAfter, rmsBefore, mostBefore;
        int stepsAfter, stepsBefore;
        SuspStepSwing(e, 200, write, 0.5f, rmsAfter, mostAfter, stepsAfter);
        SuspStepSwing(e, 200, SuspStepBack(e, write, 0.5f), 0.5f, rmsBefore, mostBefore, stepsBefore);
        Check(write == 100 && none == -1 && leave == 4 && Near(rmsAfter, 0.05f) && Near(rmsBefore, 0.001f) && stepsAfter == 64 && stepsBefore == 64 && SuspStepBack(e, write, 0.2f) == 74,
              "step test report: the first step reading the new value, the steps until the height leaves its trend by more than 2 mm, the speed's swing after against before");
    }

    // SuspStepTest's write is the tune's own (review 15): wheel 0's spring at half its tuned value and back, the stock
    // kept through it with the sliders moved, and its writes not taken for the game putting values back
    {
        const uint64_t vtable = kExe + 0x23d9538, wheel = 0x10470000, c = 0x10480000;
        Block(wheel, 0x200);
        Block(c, 0x200);
        Put(wheel + 0x28, c);
        Put(c, vtable);
        Put(c + 0xC4, 0.026f);
        Put(c + 0xC8, 0.4f);
        Put(c + 0xB4, -0.6f);
        Put(c + 0xB8, 0.4f);
        TruckRefs r = {};
        r.vehicle = kVehicle;
        r.wheel[0] = wheel;
        r.wheels = 1;
        auto at = [&](uint32_t off) { float v = 0.0f; Peek(c + off, &v, sizeof v); return v; };
        const float plain[kSuspValues] = { 1.0f, 1.0f, 1.0f, 1.0f }, moved[kSuspValues] = { 1.5f, 1.0f, 1.0f, 1.0f };
        SuspTune tune = {};
        const SuspTuneTest half = { 0, kSuspStrength, 0.5f }, between = { 0, kSuspStrength, 1.0f };
        const bool on = SuspTuneStep(tune, kMemory, r, vtable, plain, nullptr, &half) == 1 && Near(at(0xC4), 0.013f) && tune.wheel[0].ours[kSuspStrength];
        const bool off = SuspTuneStep(tune, kMemory, r, vtable, plain, nullptr, &between) == 1 && at(0xC4) == 0.026f && !tune.wheel[0].ours[kSuspStrength];
        const bool movedOn = SuspTuneStep(tune, kMemory, r, vtable, moved, nullptr, &half) == 1 && Near(at(0xC4), 0.0195f) && tune.wheel[0].stock[kSuspStrength] == 0.026f;
        const bool movedOff = SuspTuneStep(tune, kMemory, r, vtable, moved, nullptr, &between) == 1 && Near(at(0xC4), 0.039f) && tune.wheel[0].stock[kSuspStrength] == 0.026f;
        for (int i = 0; i < 30; i++)
        {
            SuspTuneStep(tune, kMemory, r, vtable, moved, nullptr, &half);
            SuspTuneStep(tune, kMemory, r, vtable, moved, nullptr, &between);
        }
        const bool counted = tune.wheel[0].writes == 0 && tune.wheel[0].stock[kSuspStrength] == 0.026f && at(0xC8) == 0.4f;
        const bool back = SuspTuneStep(tune, kMemory, r, vtable, plain, nullptr, nullptr) == 1 && at(0xC4) == 0.026f;
        Check(on && off && movedOn && movedOff && counted && back,
              "step test write: the spring at half the tuned value and back, recorded as the tune's own, the stock kept through it with a slider moved, its writes not "
              "counted as the game putting values back; the test over, the stock again");
    }

    // the damper's curve G(v) (suspension_step.h): the defaults give 1 at every speed and do not run; it is continuous at
    // the knee; above it digressive is under linear and progressive over it; the sign picks compression or rebound;
    // the factor written is held to 0.01 to 1
    {
        const DamperSet plain = { 0, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f };
        bool ones = !DamperOn(plain);
        for (float v = -3.0f; v <= 3.0f; v += 0.01f) ones &= Near(DamperG(plain, v), 1.0f);
        DamperSet kneeOnly = plain;
        kneeOnly.knee = 0.3f;
        ones &= !DamperOn(kneeOnly);
        bool continuous = true, order = true;
        for (int curve = 0; curve < 3; curve++)
            for (float knee : { 0.02f, 0.1f, 0.5f })
            {
                const DamperSet d = { curve, { 0.6f, 1.4f }, { 0.8f, 1.0f }, knee };
                continuous &= fabsf(DamperG(d, knee * 1.0001f) - DamperG(d, knee * 0.9999f)) < 0.002f && fabsf(DamperG(d, -knee * 1.0001f) - DamperG(d, -knee * 0.9999f)) < 0.002f;
            }
        const DamperSet linear = { 0, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f }, digressive = { 1, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f },
                        progressive = { 2, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f };
        for (float v : { 0.2f, 0.5f, 1.0f, 2.0f, -0.5f })
            order &= DamperG(digressive, v) < DamperG(linear, v) && DamperG(progressive, v) > DamperG(linear, v);
        order &= DamperG(digressive, 0.05f) == 1.0f && DamperG(progressive, 0.05f) == 1.0f && DamperOn(digressive);
        const DamperSet sides = { 0, { 2.0f, 0.5f }, { 3.0f, 0.25f }, 0.1f };
        const bool sign = DamperG(sides, 0.05f) == 2.0f && DamperG(sides, -0.05f) == 0.5f && Near(DamperG(sides, 0.2f), 2.5f) && Near(DamperG(sides, -0.2f), 0.375f) &&
                          DamperG(sides, 0.001f) == 2.0f && DamperG(sides, -0.001f) == 0.5f;
        bool high = false, low = false;
        const bool clamp = DamperFactor(sides, 0.9f, 0.05f, high) == 1.0f && high && DamperFactor(sides, 0.01f, -0.05f, low) == 0.01f && low &&
                           Near(DamperFactor(sides, 0.2f, 0.05f, high), 0.4f) && !high;
        Check(ones && continuous && order && sign && clamp, "damper curve: the defaults give 1 at every speed and do not run (the knee alone changes nothing); continuous at the knee; "
                                                            "digressive under linear and progressive over it above the knee; the sign picks compression or rebound; the clamp holds");
    }

    // a quarter car at the game's figures (the addendum's item 8): 60 steps a second, the stock damping a ratio of 0.55
    // at 1.6 Hz, Havok's damper implicit and its factor written one step late from the step before's speed. From a step
    // of the ground each preset, and each factor alone and all four together at 0.25 and at 4 on each curve, settles
    // without growing
    {
        struct Case { DamperSet d; const char *name; };
        std::vector<Case> cases = { { { 0, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f }, "Stock" }, { { 1, { 0.8f, 1.4f }, { 0.6f, 1.0f }, 0.1f }, "Trail" },
                                    { { 2, { 0.6f, 0.9f }, { 0.8f, 0.7f }, 0.08f }, "Rock crawl" }, { { 1, { 1.4f, 1.8f }, { 0.8f, 1.0f }, 0.1f }, "Road" } };
        for (int curve = 0; curve < 3; curve++)
            for (float f : { 0.25f, 4.0f })
            {
                for (int which = 0; which < 5; which++)
                {
                    DamperSet d = { curve, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f };
                    float *factor[4] = { &d.low[0], &d.high[0], &d.low[1], &d.high[1] };
                    for (int k = 0; k < 4; k++)
                        if (which == 4 || which == k) *factor[k] = f;
                    cases.push_back({ d, "a factor" });
                }
            }
        const double dt = 1.0 / 60.0, omega = 2.0 * 3.14159265 * 1.6, k = omega * omega, cStock = 2.0 * 0.55 * omega, base = 0.3, ground = 0.05;
        bool all = true;
        for (const Case &c : cases)
        {
            double y = 0.0, vy = 0.0, field = base, last = 0.0, peak = 1e9;
            bool settled = true, grows = false, rising = false;
            float lastV = 0.0f;
            for (int n = 0; n < 600; n++)
            {
                const double ce = cStock * field / base;
                vy = (vy + dt * k * (ground - y)) / (1.0 + dt * ce);
                y += dt * vy;
                // the damper writes for the next step from this step's shaft speed (compression positive: the body falling
                // towards the ground)
                const float v = (float)-vy;
                bool clamped = false;
                field = DamperFactor(c.d, (float)base, lastV, clamped);
                lastV = v;
                const double e = fabs(y - ground);
                if (e > last) rising = true;
                else if (rising)
                {
                    // a peak of the error: never above the one before
                    if (last > peak * 1.001 + 1e-6) grows = true;
                    peak = last;
                    rising = false;
                }
                last = e;
                if (n >= 480 && e > 0.001) settled = false;
            }
            if (!settled || grows)
            {
                all = false;
                printf("     %s (curve %d, compression x%g x%g, rebound x%g x%g): %s\n", c.name, c.d.curve, c.d.low[0], c.d.high[0], c.d.low[1], c.d.high[1], grows ? "grows" : "not settled");
            }
        }
        Check(all, "damper quarter car: at 60 steps a second, a damping ratio of 0.55 at 1.6 Hz and the factor written a step late, each preset and each factor at 0.25 and 4 "
                   "(alone and all four, every curve) settles from a step of the ground without growing");
    }

    // the damper on the physics thread (in this process's memory): each step it writes the wheel's damping as the tune
    // gives it times G of the speed just read; right before, [wheel + 0x28] must still be the block's constraint and of
    // its class (review 16, finding 1): on a mismatch nothing is written and the block is dropped
    {
        static SuspStepState s;
        memset(&s, 0, sizeof s);
        static uint64_t wheel[0x200 / 8], constraint[0x100 / 8], other[0x100 / 8];
        static float body[0x300 / 4];
        memset(wheel, 0, sizeof wheel);
        memset(constraint, 0, sizeof constraint);
        memset(body, 0, sizeof body);
        const uint64_t vtable = 0x140000000ull + 0x23d9538;
        constraint[0] = other[0] = vtable;
        const float up[3] = { 0.0f, 1.0f, 0.0f }, base = 0.28f;
        memcpy((uint8_t *)body + 0x180, up, sizeof up);
        wheel[0x28 / 8] = (uint64_t)(uintptr_t)constraint;
        float *field = (float *)((uint8_t *)constraint + 0xC8), *height = (float *)((uint8_t *)wheel + 0x13C + 4);
        *field = base;
        SuspStepBlock b = {};
        b.action = 0x7777;
        b.chassis = b.body[0] = (uint64_t)(uintptr_t)body;
        b.wheel[0] = (uint64_t)(uintptr_t)wheel;
        b.wheels = 1;
        b.generation = 1;
        b.damping = true;
        b.damper = DamperSet{ 0, { 2.0f, 0.5f }, { 2.0f, 0.5f }, 0.1f };
        b.vtable = vtable;
        b.constraint[0] = (uint64_t)(uintptr_t)constraint;
        b.base[0] = base;
        static SuspStepBlock slots[2];
        static SuspStepBlock *volatile slot = nullptr;
        int next = 0;
        uint32_t serial = 0;
        SuspStepPublish(slots, next, &slot, b, serial);
        const float step = 1.0f / 64.0f;
        *height = 0.0f;
        SuspStepLook(s, slot, 0x7777, step);                // the first step: no speed yet, nothing written
        const bool first = *field == base;
        *height = 0.03125f;                                  // rising 2 m/s: compression, over the knee
        SuspStepLook(s, slot, 0x7777, step);
        const bool rising = Near(*field, fminf(1.0f, base * DamperG(b.damper, 2.0f))) && s.win.written[0] == 1;
        *height = 0.03125f - 0.0009765625f;                  // falling 0.0625 m/s: rebound, under the knee
        SuspStepLook(s, slot, 0x7777, step);
        const bool falling = Near(*field, base * 0.5f) && s.win.written[0] == 2;
        wheel[0x28 / 8] = (uint64_t)(uintptr_t)other;        // the game rebuilt the wheel's constraint
        *field = 0.123f;
        *height = 0.0f;
        SuspStepLook(s, slot, 0x7777, step);
        const bool refused = *field == 0.123f && other[0xC8 / 8] == 0 && s.refused == 1 && !s.have;
        SuspStepLook(s, slot, 0x7777, step);
        const bool dropped = s.refused == 1 && s.win.written[0] == 2;
        Check(first && rising && falling && refused && dropped, "damper step: nothing on the first step, then the tune's damping times G of the speed just read, compression and "
                                                                "rebound; a wheel whose constraint changed is not written and the block is dropped");
    }

    // who owns the damping (BRIEF_suspension_damper.md, part 2): with the damper on a wheel the tune keeps its damping stock
    // however the field changes; the damper let go writes stock times DampingScale back, which the tune takes as its own;
    // on a change of truck, let go first and the tune then puts the stock back; let go skips a rebuilt constraint
    {
        const uint64_t vtable = kExe + 0x23d9538, wheelA = 0x10490000, wheelB = 0x104A0000, a = 0x104B0000, bc = 0x104C0000, rebuilt = 0x104D0000;
        for (uint64_t at : { wheelA, wheelB, a, bc, rebuilt }) Block(at, 0x200);
        Put(wheelA + 0x28, a);
        Put(wheelB + 0x28, bc);
        for (uint64_t c : { a, bc, rebuilt })
        {
            Put(c, vtable);
            Put(c + 0xC4, 0.026f);
            Put(c + 0xC8, 0.4f);
            Put(c + 0xB4, -0.6f);
            Put(c + 0xB8, 0.4f);
        }
        TruckRefs truckA = {}, truckB = {};
        truckA.vehicle = kVehicle;
        truckA.wheel[0] = wheelA;
        truckA.wheels = 1;
        truckB.vehicle = kOtherVehicle;
        truckB.wheel[0] = wheelB;
        truckB.wheels = 1;
        auto at = [&](uint64_t c) { float v = 0.0f; Peek(c + 0xC8, &v, sizeof v); return v; };
        const float scale[kSuspValues] = { 1.0f, 1.5f, 1.0f, 1.0f };
        const bool owns[kMaxWheels] = { true }, none[kMaxWheels] = {};
        SuspTune tune = {};
        SuspTuneStep(tune, kMemory, truckA, vtable, scale, nullptr, nullptr, none);
        const bool tuned = Near(at(a), 0.6f);
        Put(a + 0xC8, 0.93f);                                // the damper's writes
        SuspTuneStep(tune, kMemory, truckA, vtable, scale, nullptr, nullptr, owns);
        Put(a + 0xC8, 0.17f);
        SuspTuneStep(tune, kMemory, truckA, vtable, scale, nullptr, nullptr, owns);
        const bool kept = at(a) == 0.17f && tune.wheel[0].stock[kSuspDamping] == 0.4f;
        SuspStepBlock block = {};
        block.wheels = 1;
        block.wheel[0] = wheelA;
        block.constraint[0] = a;
        block.vtable = vtable;
        block.damping = true;
        block.base[0] = fminf(1.0f, tune.wheel[0].stock[kSuspDamping] * scale[kSuspDamping]);
        const bool back = SuspDamperLetGo(block, kMemory) == 1 && Near(at(a), 0.6f);
        SuspTuneStep(tune, kMemory, truckA, vtable, scale, nullptr, nullptr, none);
        const bool ownAgain = Near(at(a), 0.6f) && tune.wheel[0].stock[kSuspDamping] == 0.4f;
        // a change of truck: the damper's value in the field, let go first, then the tune's restore
        Put(a + 0xC8, 0.77f);
        SuspDamperLetGo(block, kMemory);
        int restored = 0;
        SuspTuneStep(tune, kMemory, truckB, vtable, scale, &restored, nullptr, none);
        const bool switched = at(a) == 0.4f && restored >= 1;
        Put(wheelA + 0x28, rebuilt);
        const bool skips = SuspDamperLetGo(block, kMemory) == 0 && at(rebuilt) == 0.4f;
        Check(tuned && kept && back && ownAgain && switched && skips,
              "damper ownership: with the damper on a wheel the tune keeps its damping stock however the field changes; let go writes stock x DampingScale back and the tune "
              "takes it as its own; on a change of truck let go first, then the tune puts the stock back; a rebuilt constraint is not written");
    }

    // the damper's presets by name (the addendum of 01:18, item 1): each preset's six settings give its name, the
    // defaults are Stock, one setting off any preset is custom
    {
        GaugeSettings g;
        SettingsDefaults(g);
        bool named = DamperPresetOf(g) == 0;
        for (int i = 0; i < kDamperPresetCount; i++)
        {
            const DamperPreset &d = kDamperPresets[i];
            g.lowCompression = d.lowC, g.highCompression = d.highC, g.lowRebound = d.lowR, g.highRebound = d.highR, g.damperKnee = d.knee, g.damperCurve = d.curve;
            named &= DamperPresetOf(g) == i;
        }
        g.damperKnee = 0.12f;
        const bool custom = DamperPresetOf(g) == -1 && !strcmp(kDamperPresets[2].name, "Rock crawl");
        Check(named && custom, "damper presets: each preset's six settings give its name, the defaults are Stock, a setting off every preset is custom");
    }

    // a bounce's decay (the addendum of 01:18, item 4): a damped bounce's second swing over its first; ripples under 2 mm
    // are no swing; a bounce with one swing has no ratio
    {
        // a damped sine from rest: its swings half a period apart shrink by e^(-2.5 x 0.3125) = 0.46
        static float h[90];
        for (int i = 0; i < 90; i++)
        {
            const float t = i / 60.0f;
            h[i] = 0.1f + 0.06f * expf(-2.5f * t) * sinf(2.0f * 3.14159265f * 1.6f * t);
        }
        float ratio = 0.0f;
        const bool decay = SuspBounceRatio(h, 90, ratio) && fabsf(ratio - expf(-2.5f * 0.3125f)) < 0.06f;
        static float flat[90];
        for (int i = 0; i < 90; i++) flat[i] = 0.1f + (i % 2 ? 0.001f : -0.001f);
        float none = 1.0f;
        const bool ripples = !SuspBounceRatio(flat, 90, none) && none == 0.0f;
        static float once[90];
        for (int i = 0; i < 90; i++) once[i] = 0.1f + (i < 10 ? 0.003f * i : 0.0f);
        const bool single = !SuspBounceRatio(once, 90, none);
        Check(decay && ripples && single, "bounce decay: the second swing over the first of a damped bounce; ripples under 2 mm are no swing; a single swing has no ratio");
    }

    // the spring's features (BRIEF_suspension_extras.md, build A): given as a rate and written as its secant f(u), so the
    // force f(u) u is continuous at u = 0, at the bump zone's start and at dual rate's crossover, and its slope is the
    // rate; each default gives f = 1 and nothing runs
    {
        SpringSet off = { 0, 2.0f, 0.5f, false, 0.08f, 4.0f, 2.0f, false, 0.7f, 1.5f, 0.1f, 0.6f };
        bool ones = !SpringOn(off) && !ZonesOn(off);
        for (float u = 0.0f; u <= 1.0f; u += 0.01f) ones &= SpringF(off, u, 0.3f) == 1.0f && ZoneFactor(off, u, 0.5f, 0.3f) == 1.0f;
        SpringSet stopsSoft = off;
        stopsSoft.bump = true, stopsSoft.bumpForce = 1.0f, stopsSoft.bumpDamping = 1.0f;
        ones &= !SpringOn(stopsSoft) && !ZonesOn(stopsSoft);
        bool smooth = true, slope = true;
        const float travel = 0.36f;
        for (int curve = 0; curve < 3; curve++)
            for (bool bump : { false, true })
            {
                SpringSet k = off;
                k.curve = curve, k.rise = 3.0f, k.bump = bump, k.bumpForce = 6.0f;
                float last = 0.0f, most = 0.0f;
                for (int n = 0; n <= 1000; n++) most = fmaxf(most, SpringRate(k, n / 1000.0f, travel));
                for (int n = 1; n <= 1000; n++)
                {
                    const float u = n / 1000.0f, force = SpringF(k, u, travel) * u;
                    // no step in the force anywhere: from one thousandth of the travel to the next it moves by at most the
                    // largest rate times a thousandth
                    smooth &= force - last >= 0.0f && force - last <= most / 1000.0f + 1e-4f;
                    // and its slope is the rate
                    if (n > 1 && n < 1000)
                    {
                        const float d = (SpringF(k, u + 0.0005f, travel) * (u + 0.0005f) - SpringF(k, u - 0.0005f, travel) * (u - 0.0005f)) / 0.001f;
                        slope &= fabsf(d - SpringRate(k, u, travel)) < 0.05f * fmaxf(1.0f, SpringRate(k, u, travel));
                    }
                    last = force;
                }
                smooth &= fabsf(SpringF(k, 1e-5f, travel) - 1.0f) < 1e-3f;
            }
        SpringSet zones = off;
        zones.zones = true;
        const bool middle = Near(ZoneFactor(zones, 0.35f, 0.5f, travel), 0.7f) && Near(ZoneFactor(zones, 0.0f, -0.5f, travel), 1.5f) &&
                            Near(ZoneFactor(zones, 0.9f, 0.5f, travel), 1.5f);
        bool zoneSmooth = true;
        for (int n = 1; n <= 1000; n++) zoneSmooth &= fabsf(ZoneFactor(zones, n / 1000.0f, 0.5f, travel) - ZoneFactor(zones, (n - 1) / 1000.0f, 0.5f, travel)) < 0.02f;
        SpringSet stops = off;
        stops.bump = true;
        // the stop's damping on compression only, at its end x2; the zone starts 0.08 m before the end
        const bool stopDamping = Near(ZoneFactor(stops, 1.0f, 0.5f, travel), 2.0f) && ZoneFactor(stops, 1.0f, -0.5f, travel) == 1.0f &&
                                 ZoneFactor(stops, 1.0f - 0.08f / travel - 0.01f, 0.5f, travel) == 1.0f && Near(BumpStart(stops, travel), 1.0f - 0.08f / travel);
        Check(ones && smooth && slope && middle && zoneSmooth && stopDamping,
              "spring features: written as the secant of the rate, so the force is continuous at 0, at the bump zone and at the crossover and its slope is the rate; "
              "the defaults give 1 and do not run; the zones soft in the middle and firm at the ends with no step; the bump stop's damping on compression only");
    }

    // the strength on the physics thread (in this process's memory): with a spring feature on, the tune's strength
    // times f(u) at the place just read; with everything off nothing is written; let go writes the strength back
    {
        static SuspStepState s;
        memset(&s, 0, sizeof s);
        static uint64_t wheel[0x200 / 8], constraint[0x100 / 8];
        static float body[0x300 / 4];
        memset(wheel, 0, sizeof wheel);
        memset(constraint, 0, sizeof constraint);
        memset(body, 0, sizeof body);
        const uint64_t vtable = 0x140000000ull + 0x23d9538;
        constraint[0] = vtable;
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        memcpy((uint8_t *)body + 0x180, up, sizeof up);
        wheel[0x28 / 8] = (uint64_t)(uintptr_t)constraint;
        float *strength = (float *)((uint8_t *)constraint + 0xC4), *damping = (float *)((uint8_t *)constraint + 0xC8),
              *height = (float *)((uint8_t *)wheel + 0x13C + 4);
        *strength = 0.025f;
        *damping = 0.28f;
        SuspStepBlock b = {};
        b.action = 0x8888;
        b.chassis = b.body[0] = (uint64_t)(uintptr_t)body;
        b.wheel[0] = (uint64_t)(uintptr_t)wheel;
        b.wheels = 1;
        b.generation = 1;
        b.vtable = vtable;
        b.constraint[0] = (uint64_t)(uintptr_t)constraint;
        b.attachUp[0] = -0.135f, b.axisUp[0] = -1.0f, b.travel[0] = 0.36f;   // the F350's front, x = -0.135 - h
        b.spring = SpringSet{ 1, 3.0f, 0.5f, false, 0.08f, 4.0f, 2.0f, false, 0.7f, 1.5f, 0.1f, 0.6f };
        static SuspStepBlock slots[2];
        static SuspStepBlock *volatile slot = nullptr;
        int next = 0;
        uint32_t serial = 0;
        SuspStepPublish(slots, next, &slot, b, serial);
        const float step = 1.0f / 64.0f;
        *height = 0.0f;
        SuspStepLook(s, slot, 0x8888, step);
        *height = 0.045f;                                    // x = -0.18: half the compression travel
        SuspStepLook(s, slot, 0x8888, step);
        const bool off = *strength == 0.025f && *damping == 0.28f && s.win.springWritten[0] == 0;
        b.springing = true;
        b.baseK[0] = 0.025f;
        b.generation = 2;
        SuspStepPublish(slots, next, &slot, b, serial);
        SuspStepLook(s, slot, 0x8888, step);
        *height = 0.046f;
        SuspStepLook(s, slot, 0x8888, step);
        const float u = (0.135f + 0.046f) / 0.36f;
        const bool written = Near(*strength, 0.025f * SpringF(b.spring, u, 0.36f)) && *strength > 0.025f && *damping == 0.28f && s.win.springWritten[0] == 1;
        const bool back = SuspDamperLetGo(b, kNativeMemory) == 1 && *strength == 0.025f && *damping == 0.28f;
        Check(off && written && back, "spring step: nothing written with every feature off; with a curve on, the tune's strength times f(u) at the place just read, the damping "
                                      "left alone; let go writes the strength back");
    }

    // who owns the strength (build A): with a spring feature on a wheel the tune keeps its strength stock however the field
    // changes, the damping still its own; let go writes stock x SpringScale back, which the tune takes as its own
    {
        const uint64_t vtable = kExe + 0x23d9538, wheel = 0x104E0000, c = 0x104F0000;
        Block(wheel, 0x200);
        Block(c, 0x200);
        Put(wheel + 0x28, c);
        Put(c, vtable);
        Put(c + 0xC4, 0.026f);
        Put(c + 0xC8, 0.4f);
        Put(c + 0xB4, -0.6f);
        Put(c + 0xB8, 0.4f);
        TruckRefs r = {};
        r.vehicle = kVehicle;
        r.wheel[0] = wheel;
        r.wheels = 1;
        auto at = [&](uint32_t off) { float v = 0.0f; Peek(c + off, &v, sizeof v); return v; };
        const float scale[kSuspValues] = { 1.5f, 2.0f, 1.0f, 1.0f };
        const bool owns[kMaxWheels] = { true }, none[kMaxWheels] = {};
        SuspTune tune = {};
        SuspTuneStep(tune, kMemory, r, vtable, scale, nullptr, nullptr, none, none);
        const bool tuned = Near(at(0xC4), 0.039f) && Near(at(0xC8), 0.8f);
        Put(c + 0xC4, 0.07f);                                // the spring's writes
        SuspTuneStep(tune, kMemory, r, vtable, scale, nullptr, nullptr, none, owns);
        Put(c + 0xC4, 0.05f);
        Put(c + 0xC8, 0.3f);                                 // the damping is still the tune's: the game's write is taken as a new stock
        SuspTuneStep(tune, kMemory, r, vtable, scale, nullptr, nullptr, none, owns);
        const bool kept = at(0xC4) == 0.05f && tune.wheel[0].stock[kSuspStrength] == 0.026f && tune.wheel[0].stock[kSuspDamping] == 0.3f && Near(at(0xC8), 0.6f);
        SuspStepBlock block = {};
        block.wheels = 1;
        block.wheel[0] = wheel;
        block.constraint[0] = c;
        block.vtable = vtable;
        block.springing = true;
        block.baseK[0] = fminf(1.0f, tune.wheel[0].stock[kSuspStrength] * scale[kSuspStrength]);
        const bool back = SuspDamperLetGo(block, kMemory) == 1 && Near(at(0xC4), 0.039f) && Near(at(0xC8), 0.6f);
        SuspTuneStep(tune, kMemory, r, vtable, scale, nullptr, nullptr, none, none);
        const bool ownAgain = Near(at(0xC4), 0.039f) && tune.wheel[0].stock[kSuspStrength] == 0.026f;
        Check(tuned && kept && back && ownAgain, "spring ownership: with a spring feature on a wheel the tune keeps its strength stock however the field changes, the damping "
                                                 "still its own; let go writes stock x SpringScale back and the tune takes it as its own");
    }

    // the quarter car with the spring's features (build A's tests): 60 steps a second, the stock damping a ratio of 0.55
    // at 1.6 Hz, gravity's sag a third of the compression travel, and the strength and the damping written a step late
    // from the step before's place and speed. With BumpForce x6 and SpringRise x3 (progressive and dual rate), and with
    // the zones, each settles from a step of the ground without growing
    {
        const SpringSet base = { 0, 3.0f, 0.5f, false, 0.12f, 6.0f, 2.0f, false, 0.7f, 1.5f, 0.1f, 0.6f };
        std::vector<SpringSet> cases;
        for (int curve = 0; curve < 3; curve++)
            for (int bump = 0; bump < 2; bump++)
                for (int zones = 0; zones < 2; zones++)
                {
                    SpringSet k = base;
                    k.curve = curve, k.bump = bump != 0, k.zones = zones != 0;
                    cases.push_back(k);
                }
        const double dt = 1.0 / 60.0, omega = 2.0 * 3.14159265 * 1.6, k0 = omega * omega, c0 = 2.0 * 0.55 * omega, travel = 0.3, g = k0 * travel / 3.0;
        const DamperSet plain = { 0, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f };
        bool all = true;
        for (const SpringSet &k : cases)
        {
            // x is the wheel's place along its axis: compression negative; the ground's step pushes it 0.15 m deeper
            double x = -g / k0, vx = 0.0, kf = 1.0, cf = 1.0, last = 0.0, peak = 1e9;
            bool rising = false, grows = false, settled = true;
            float lastU = SuspDepth((float)x, (float)travel), lastV = 0.0f;
            for (int n = 0; n < 600; n++)
            {
                // the body's motion: x' = vx; the spring and the damper push along the axis toward 0, the load toward
                // compression, and the ground's step is a constant push of 0.15 m worth of the stock spring
                const double push = -g - k0 * 0.15 * (n > 0 ? 1.0 : 0.0);
                vx = (vx + dt * (push - k0 * kf * x)) / (1.0 + dt * c0 * cf);
                x += dt * vx;
                const float u = SuspDepth((float)x, (float)travel), v = (float)-vx;
                kf = SpringF(k, lastU, (float)travel);
                // the damping as a game factor of 0.3 times the zone, held as the physics thread holds it, over the stock
                bool clamped = false;
                cf = DamperFactor(plain, 0.3f * ZoneFactor(k, lastU, lastV, (float)travel), lastV, clamped) / 0.3;
                lastU = u, lastV = v;
                const double err = fabs(vx);
                if (err > last) rising = true;
                else if (rising)
                {
                    if (last > peak * 1.001 + 1e-6) grows = true;
                    peak = last;
                    rising = false;
                }
                last = err;
                if (n >= 480 && fabs(vx) > 0.002) settled = false;
            }
            if (grows || !settled)
            {
                all = false;
                printf("     spring curve %d, bump stops %d, zones %d: %s\n", k.curve, k.bump, k.zones, grows ? "grows" : "not settled");
            }
        }
        Check(all, "spring quarter car: at 60 steps a second, a damping ratio of 0.55 at 1.6 Hz and the strength and damping written a step late, linear, progressive "
                   "and dual rate x3, with and without bump stops x6 and the zones, each settles from a step of the ground without growing");
    }

    // two spring sets alike field by field compare the same, whatever their padding holds (the drive of 1 November: a
    // memcmp made a new generation on every pass)
    {
        SpringSet a, b;
        memset(&a, 0x00, sizeof a);
        memset(&b, 0xA5, sizeof b);
        for (SpringSet *k : { &a, &b })
        {
            k->curve = 2, k->rise = 3.0f, k->cross = 0.5f, k->bump = true, k->zone = 0.08f, k->bumpForce = 4.0f, k->bumpDamping = 2.0f;
            k->zones = true, k->soft = 0.7f, k->firm = 1.5f, k->from = 0.1f, k->to = 0.6f;
        }
        const bool same = SpringSame(a, b) && memcmp(&a, &b, sizeof a) != 0;
        b.to = 0.61f;
        Check(same && !SpringSame(a, b), "spring sets: alike field by field compare the same whatever their padding holds; one field off does not");
    }

    // the bump stop's law of 11:39: the force at the end is BumpForce times the spring's own there, curve included; it
    // starts with no step at the stop's start; the stop is held to a third of the travel
    {
        bool end = true, start = true;
        const float travel = 0.36f;
        for (int curve = 0; curve < 3; curve++)
            for (float force : { 1.0f, 3.0f, 6.0f })
            {
                SpringSet k = { curve, 3.0f, 0.5f, true, 0.12f, force, 2.0f, false, 0.7f, 1.5f, 0.1f, 0.6f };
                end &= fabsf(SpringF(k, 1.0f, travel) - force * CurveArea(k, 1.0f)) < 1e-4f;
                const float u0 = BumpStart(k, travel);
                start &= fabsf(SpringF(k, u0 + 1e-4f, travel) * (u0 + 1e-4f) - CurveArea(k, u0 + 1e-4f)) < 1e-4f && BumpArea(k, u0, travel) == 0.0f;
            }
        SpringSet wide = { 0, 2.0f, 0.5f, true, 0.2f, 3.0f, 2.0f, false, 0.7f, 1.5f, 0.1f, 0.6f };
        const bool third = Near(BumpZoneOf(wide, 0.3f), 0.1f) && Near(BumpStart(wide, 0.3f), 2.0f / 3.0f) && Near(BumpZoneOf(wide, 0.9f), 0.2f);
        Check(end && start && third, "bump stop law: the force at the end is BumpForce times the spring's own there, curve included; no step at the stop's start; "
                                     "the stop held to a third of the travel");
    }

    // the stop where the wheel will be (the addendum of 11:39, item 3): a quarter car at 60 steps a second, gravity's
    // sag half the compression travel, the stop at its defaults (0.12 m, x3, damping x2) written a step late, entering at
    // 1, 2.5 and 4 m/s. With the look a step ahead the wheel goes no deeper, and at 2.5 and 4 m/s less deep; both settle
    // without growing
    {
        const double dt = 1.0 / 60.0, omega = 2.0 * 3.14159265 * 1.6, k0 = omega * omega, c0 = 2.0 * 0.55 * omega, travel = 0.36, g = k0 * travel / 2.0;
        const SpringSet k = { 0, 2.0f, 0.5f, true, 0.12f, 3.0f, 2.0f, false, 0.7f, 1.5f, 0.1f, 0.6f };
        const DamperSet plain = { 0, { 1.0f, 1.0f }, { 1.0f, 1.0f }, 0.1f };
        bool less = true, all = true;
        for (double speed : { 1.0, 2.5, 4.0 })
        {
            double deepest[2] = {};
            for (int ahead = 0; ahead < 2; ahead++)
            {
                double x = -g / k0, vx = -speed, kf = 1.0, cf = 1.0, last = 0.0, peak = 1e9, deep = 0.0;
                bool rising = false, grows = false, settled = true;
                float lastU = SuspDepth((float)x, (float)travel), lastAhead = lastU, lastV = (float)speed;
                for (int n = 0; n < 600; n++)
                {
                    vx = (vx + dt * (-g - k0 * kf * x)) / (1.0 + dt * c0 * cf);
                    x += dt * vx;
                    deep = fmin(deep, x);
                    const float u = SuspDepth((float)x, (float)travel), v = (float)-vx, next = SuspDepth((float)(x + vx * dt), (float)travel);
                    kf = SpringF(k, lastU, (float)travel, ahead ? lastAhead : -1.0f);
                    bool clamped = false;
                    cf = DamperFactor(plain, 0.3f * ZoneFactor(k, lastU, lastV, (float)travel, ahead ? lastAhead : -1.0f), lastV, clamped) / 0.3;
                    lastU = u, lastV = v, lastAhead = next;
                    const double err = fabs(vx);
                    if (err > last) rising = true;
                    else if (rising)
                    {
                        if (last > peak * 1.001 + 1e-6) grows = true;
                        peak = last;
                        rising = false;
                    }
                    last = err;
                    if (n >= 480 && fabs(vx) > 0.002) settled = false;
                }
                deepest[ahead] = deep;
                if (grows || !settled)
                {
                    all = false;
                    printf("     entering at %g m/s, %s: %s\n", speed, ahead ? "a step ahead" : "where it is", grows ? "grows" : "not settled");
                }
            }
            less &= deepest[1] >= deepest[0] - 1e-6 && (speed < 2.0 || deepest[1] > deepest[0] + 0.001);
            printf("     entering at %g m/s: deepest %.3f m where it is, %.3f m a step ahead (the end at -%.2f)\n", speed, deepest[0], deepest[1], travel);
        }
        Check(less && all, "bump stop a step ahead: entering at 1, 2.5 and 4 m/s, the wheel goes no deeper with the look ahead and less deep at 2.5 and 4 m/s; "
                           "both settle without growing");
    }

    // one gearbox's keys of gears match within 2 % (BRIEF_manual_creep_and_hold.md): the four of the logs match each
    // other; a gear more, or one off by 5 %, does not; a name matches nothing, and the key of a named gearbox is its name
    {
        const char *keys[4] = { "gears 3.03638 6.05623 8.06946 14.1091 20.1488 high 10.0794", "gears 3.0269 6.04157 8.05135 14.0807 20.11 high 10.0587",
                                "gears 3.03574 6.05523 8.06823 14.1072 20.1462 high 10.078", "gears 3.03703 6.05722 8.07069 14.1111 20.1515 high 10.0808" };
        bool four = true;
        for (const char *a : keys)
            for (const char *b : keys) four &= GearboxKeysMatch(a, b);
        float speeds[kMaxGears + 2];
        const bool parsed = GearboxKeySpeeds(keys[0], speeds, kMaxGears + 2) == 6 && speeds[0] == 3.03638f && speeds[5] == 10.0794f;
        const bool more = !GearboxKeysMatch(keys[0], "gears 3.03638 6.05623 8.06946 14.1091 20.1488 25 high 10.0794");
        const bool off = !GearboxKeysMatch(keys[0], "gears 3.03638 6.05623 8.47293 14.1091 20.1488 high 10.0794");
        const bool names = !GearboxKeysMatch("g_arocs_highrange", "g_arocs_highrange") && !GearboxKeysMatch("g_arocs_highrange", keys[0]) &&
                           GearboxKeySpeeds("g_arocs_highrange", speeds, kMaxGears + 2) == 0;
        TruckRaw t = {};
        t.gearSpeeds = 4;
        t.gearSpeed[1] = 3.0f, t.gearSpeed[2] = 6.0f, t.gearSpeed[3] = 10.0f;
        char key[96];
        GearboxKey(t, "g_arocs_highrange", key, sizeof key);
        const bool named = !strcmp(key, "g_arocs_highrange");
        Check(four && parsed && more && off && names && named, "gearbox keys: one gearbox's four keys of the logs match within 2 %; a gear more, or one off by 5 %, does not; "
                                                               "a name matches nothing, and a named gearbox keeps its name");
    }

    // the four sliders reworked (the addendum of 2 November, 13:18): at the defaults the tune reads each wheel's stock
    // for the tab, front and rear apart, and writes nothing; ExtraTravel puts the compression end past the file's, from a
    // 0.0 too, and never shortens; a spring past the game's 1 is held there
    {
        const uint64_t vtable = kExe + 0x23d9538, wf = 0x10500000, wr = 0x10510000, cf = 0x10520000, cr = 0x10530000;
        for (uint64_t at : { wf, wr, cf, cr }) Block(at, 0x200);
        Put(wf + 0x28, cf);
        Put(wr + 0x28, cr);
        Put(cf, vtable), Put(cr, vtable);
        Put(cf + 0xC4, 0.026f), Put(cf + 0xC8, 0.4f), Put(cf + 0xB4, -0.6f), Put(cf + 0xB8, 0.4f);
        Put(cr + 0xC4, 0.3f), Put(cr + 0xC8, 0.2f), Put(cr + 0xB4, 0.0f), Put(cr + 0xB8, 0.4f);   // a rear SuspensionMin of 0.0
        TruckRefs r = {};
        r.vehicle = kVehicle;
        r.wheel[0] = wf, r.wheel[1] = wr;
        r.wheels = 2;
        auto at = [&](uint64_t c, uint32_t off) { float v = 0.0f; Peek(c + off, &v, sizeof v); return v; };
        const float plain[kSuspValues] = { 1.0f, 1.0f, 1.0f, 1.0f };
        SuspTune tune = {};
        const bool none = SuspTuneStep(tune, kMemory, r, vtable, plain) == 0 && tune.wheel[0].stock[kSuspStrength] == 0.026f && tune.wheel[1].stock[kSuspStrength] == 0.3f &&
                          tune.wheel[1].stock[kSuspMin] == 0.0f && at(cr + 0, 0xB4) == 0.0f && at(cf, 0xC4) == 0.026f;
        const bool extra = SuspTuneStep(tune, kMemory, r, vtable, plain, nullptr, nullptr, nullptr, nullptr, 0.12f) == 2 && Near(at(cf, 0xB4), -0.72f) &&
                           Near(at(cr, 0xB4), -0.12f) && at(cf, 0xB8) == 0.4f && tune.wheel[1].stock[kSuspMin] == 0.0f;
        const bool back = SuspTuneStep(tune, kMemory, r, vtable, plain) == 2 && at(cf, 0xB4) == -0.6f && at(cr, 0xB4) == 0.0f;
        const float four[kSuspValues] = { 4.0f, 1.0f, 1.0f, 1.0f };
        const bool held = SuspTuneStep(tune, kMemory, r, vtable, four) == 2 && Near(at(cf, 0xC4), 0.104f) && at(cr, 0xC4) == 1.0f && tune.wheel[1].stock[kSuspStrength] == 0.3f;
        Check(none && extra && back && held, "sliders: at the defaults the stock read front and rear and nothing written; ExtraTravel past the file's compression end, a 0.0 "
                                             "included, and back to the file's at 0; a spring past the game's 1 held at 1, its stock kept");
    }

    // the attach point's features (build B): pairs on a 4 and an 8 wheel layout; the bar's sign; the shift held to 12 cm
    {
        const float fwd4[4] = { 2.28f, 2.28f, -2.05f, -2.05f }, left4[4] = { 0.96f, -0.96f, 0.96f, -0.96f };
        int partner[8];
        bool isLeft[8], front[8], rearmost[8];
        SuspPairs(4, fwd4, left4, partner, isLeft, front, rearmost);
        const bool four = partner[0] == 1 && partner[1] == 0 && partner[2] == 3 && partner[3] == 2 && isLeft[0] && !isLeft[1] && front[0] && front[1] && !front[2] &&
                          rearmost[2] && rearmost[3] && !rearmost[0];
        const float fwd8[8] = { 2.78f, 2.78f, 1.0f, 1.0f, -1.98f, -1.98f, -3.33f, -3.33f }, left8[8] = { 1.1f, -1.1f, 1.1f, -1.1f, 1.0f, -1.0f, 1.0f, -1.0f };
        SuspPairs(8, fwd8, left8, partner, isLeft, front, rearmost);
        const bool eight = partner[0] == 1 && partner[2] == 3 && partner[4] == 5 && partner[7] == 6 && front[0] && !front[2] && rearmost[6] && !rearmost[4];
        float sL = 0.0f, sR = 0.0f;
        SuspSwayShift(1.0f, 0.3f, 0.1f, 0.03f, 0.03f, sL, sR);   // the left more compressed
        const bool sign = sL > 0.0f && sR < 0.0f && Near(sL, 0.1f) && Near(sR, -0.1f);
        SuspSwayShift(1.0f, 0.1f, 0.3f, 0.03f, 0.03f, sL, sR);
        const bool other = sL < 0.0f && sR > 0.0f;
        const bool held = SuspShiftHold(0.2f) == kShiftMost && SuspShiftHold(-0.5f) == -kShiftMost && SuspShiftHold(0.05f) == 0.05f;
        Check(four && eight && sign && other && held, "attach point: axles paired on a 4 and an 8 wheel layout, the front and the rearmost known; the bar pushes the more compressed "
                                                      "side toward the droop and the other toward the bump; the shift held to 12 cm");
    }

    // the attach point's write and its guard on the physics thread (in this process's memory): with everything off nothing
    // is written; with a ride height the stock plus the shift along the axis; a field the game wrote meanwhile (neither the
    // mod's nor the stock) stops the writes for the block and is counted once; let go puts the stock back
    {
        static SuspStepState s;
        memset(&s, 0, sizeof s);
        static uint64_t wheel[0x200 / 8], constraint[0x100 / 8];
        static float body[0x300 / 4];
        memset(wheel, 0, sizeof wheel);
        memset(constraint, 0, sizeof constraint);
        memset(body, 0, sizeof body);
        const uint64_t vtable = 0x140000000ull + 0x23d9538;
        constraint[0] = vtable;
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        memcpy((uint8_t *)body + 0x180, up, sizeof up);
        wheel[0x28 / 8] = (uint64_t)(uintptr_t)constraint;
        float *attach = (float *)((uint8_t *)constraint + 0xA4), *height = (float *)((uint8_t *)wheel + 0x13C + 4);
        *attach = -0.135f;
        *height = 0.0f;
        SuspStepBlock b = {};
        b.action = 0x9999;
        b.chassis = b.body[0] = (uint64_t)(uintptr_t)body;
        b.wheel[0] = (uint64_t)(uintptr_t)wheel;
        b.wheels = 1;
        b.generation = 1;
        b.vtable = vtable;
        b.attachUp[0] = -0.135f, b.axisUp[0] = -1.0f, b.travel[0] = 0.36f;
        b.attachAt[0] = (uint64_t)(uintptr_t)attach;
        b.partner[0] = -1;
        static SuspStepBlock slots[2];
        static SuspStepBlock *volatile slot = nullptr;
        int next = 0;
        uint32_t serial = 0;
        SuspStepPublish(slots, next, &slot, b, serial);
        const float step = 1.0f / 64.0f;
        for (int i = 0; i < 3; i++) SuspStepLook(s, slot, 0x9999, step);
        const bool off = *attach == -0.135f;
        b.shifting = true;
        b.attachC[0] = (uint64_t)(uintptr_t)constraint;
        b.attachStock[0] = -0.135f;
        b.ride[0] = 0.05f;                                   // 5 cm up: the point 5 cm along the axis, down the body
        b.generation = 2;
        SuspStepPublish(slots, next, &slot, b, serial);
        SuspStepLook(s, slot, 0x9999, step);
        SuspStepLook(s, slot, 0x9999, step);
        const bool ride = Near(*attach, -0.185f) && s.attachMoved == 0;
        SuspStepLook(s, slot, 0x9999, step);
        const bool steady = Near(*attach, -0.185f) && s.attachMoved == 0;
        *attach = -0.15f;                                    // the game's own ride height control moves it
        SuspStepLook(s, slot, 0x9999, step);
        const bool moved = s.attachMoved == 1 && *attach == -0.15f;
        SuspStepLook(s, slot, 0x9999, step);
        const bool once = s.attachMoved == 1 && *attach == -0.15f;
        b.ride[0] = 0.5f;                                    // held to 12 cm
        b.generation = 3;
        *attach = -0.135f;
        SuspStepPublish(slots, next, &slot, b, serial);
        SuspStepLook(s, slot, 0x9999, step);
        SuspStepLook(s, slot, 0x9999, step);
        const bool most = Near(*attach, -0.255f);
        const bool back = SuspDamperLetGo(*slot, kNativeMemory) == 1 && *attach == -0.135f;
        Check(off && ride && steady && moved && once && most && back, "attach point write: nothing with everything off; the stock plus the ride height along the axis; a field the game "
                                                                      "moved stops the block's writes, counted once; the shift held to 12 cm; let go puts the stock back");
    }

    // a half axle in roll with the bar (build B's tests): the body's roll at 60 steps a second, the springs and dampers at
    // the game's figures (1.6 Hz, 0.55), the bar's shift worked out from the last step's places and written a step late;
    // at 0.3, the top of its range, it settles without growing, and it holds the roll less than with no bar
    {
        const double dt = 1.0 / 60.0, omega = 2.0 * 3.14159265 * 1.6, k = omega * omega, c = 2.0 * 0.55 * omega, a = 1.0;
        double settledRoll[2] = {};
        bool all = true;
        for (int bar = 0; bar < 2; bar++)
        {
            const float setting = bar ? 0.3f : 0.0f;
            double th = 0.0, w = 0.0, last = 0.0, peak = 1e9;
            float sL = 0.0f, sR = 0.0f;
            bool rising = false, grows = false;
            for (int n = 0; n < 600; n++)
            {
                // a steady roll moment (a side slope); each side's spring force k (c + s), its compression c = +/- a th
                const double cL = a * th, cR = -a * th;
                const double moment = 0.3 * k - a * (k * (cL + sL) - k * (cR + sR));
                w = (w + dt * moment) / (1.0 + dt * c * 2.0 * a * a);
                th += dt * w;
                SuspSwayShift(setting, (float)(a * th), (float)(-a * th), 1.0f, 1.0f, sL, sR);
                sL = SuspShiftHold(sL), sR = SuspShiftHold(sR);
                const double err = fabs(w);
                if (err > last) rising = true;
                else if (rising)
                {
                    if (last > peak * 1.001 + 1e-6) grows = true;
                    peak = last;
                    rising = false;
                }
                last = err;
            }
            settledRoll[bar] = th;
            all &= !grows && fabs(w) < 0.002;
        }
        printf("     roll on a side slope: %.3f with no bar, %.3f with the bar at 0.3\n", settledRoll[0], settledRoll[1]);
        Check(all && settledRoll[1] < settledRoll[0] * 0.85, "sway bar: a half axle in roll at 60 steps a second with the shift a step late settles at 0.3 without growing, and "
                                                           "holds the roll to less than 0.85 of the roll with no bar");
    }

    // the display's figures from fed places: the place along the axis, a hit of the compression end kept for a second,
    // a hanging wheel at or past its spring's zero, the rest place taken standing
    {
        static SuspStepState s;
        memset(&s, 0, sizeof s);
        SuspStepBlock b = {};
        b.wheels = 2;
        b.display = true;
        b.axisUp[0] = b.axisUp[1] = -1.0f, b.attachUp[0] = b.attachUp[1] = -0.135f, b.travel[0] = b.travel[1] = 0.36f, b.travelMax[0] = b.travelMax[1] = 1.0f;
        const float attach[2] = { -0.135f, -0.135f };
        float h[2] = { 0.3f, -0.2f };                        // wheel 0 at -0.435 (past the end), wheel 1 at +0.065 (hanging)
        s.clock = 5.0f;
        SuspDisplayStep(s, b, h, attach, 0.0f);
        const SuspDisplaySlot &d = s.display[(s.displays - 1) & 1];
        const bool first = s.displays == 1 && Near(d.x[0], -0.435f) && d.hit[0] && !d.hanging[0] && d.hanging[1] && !d.hit[1] && Near(d.rest[0], -0.435f);
        h[0] = 0.0f;                                         // back up, moving: the rest place stays, the hit for a second
        s.clock = 5.5f;
        SuspDisplayStep(s, b, h, attach, 3.0f);
        const SuspDisplaySlot &e = s.display[(s.displays - 1) & 1];
        const bool kept = e.hit[0] && Near(e.rest[0], -0.435f) && Near(e.x[0], -0.135f);
        s.clock = 6.2f;
        SuspDisplayStep(s, b, h, attach, 3.0f);
        const bool gone = !s.display[(s.displays - 1) & 1].hit[0] && s.displays == 3;
        Check(first && kept && gone, "display figures: the place along the axis, a hit of the compression end kept for a second, a hanging wheel, the rest place taken standing");
    }

    // the wheels' places in the chassis for the air suspension's plane: one axle of two, and two axles of two
    static const float kAlong2[2] = { 0.0f, 0.0f }, kAcross2[2] = { 0.8f, -0.8f };
    static const float kAlong4[4] = { 1.5f, 1.5f, -1.5f, -1.5f }, kAcross4[4] = { 0.8f, -0.8f, 0.8f, -0.8f };

    // the air suspension (build C): an axle of two corners at 60 steps a second, the springs and dampers at the game's
    // figures (1.6 Hz, 0.55), the shift written a step late. The rest place is taken after 1 s standing; half as much
    // load again sags it 9.5 cm without the levelling, and with it the axle comes back within 5 mm in 15 s without
    // swinging past; a ride height of +2 cm on top stays (the two within 12 cm); three times the load holds the shift to 12 cm
    {
        const double dt = 1.0 / 60.0, omega = 2.0 * 3.14159265 * 1.6, k = omega * omega, c = 2.0 * 0.55 * omega, w1 = k * 0.19;
        double sagged = 0.0, back = 0.0, raised = 0.0, past = 0.0;
        int takenAt = -1;
        float most = 0.0f;
        for (int on = 0; on < 2; on++)
        {
            SuspLevel l = {};
            const int partner[2] = { 1, 0 };
            const bool placed[2] = { true, true }, held[2] = { false, false };
            double z = -0.19, v = 0.0, shiftNow = 0.0;
            for (int n = 0; n < 60 * 50; n++)
            {
                const double time = n * dt;
                const double load = time < 5.0 ? w1 : time < 38.0 ? 1.5 * w1 : 3.0 * w1;
                const float ride = time >= 20.0 ? 0.02f : 0.0f;
                v = (v + dt * (k * (shiftNow - z) - load)) / (1.0 + dt * c);
                z += dt * v;
                const float g[2] = { (float)z, (float)z }, r[2] = { ride, ride };
                if (on && SuspLevelStep(l, 2, partner, kAlong2, kAcross2, g, r, placed, held, 0.0f, 0.0f, (float)dt)) takenAt = n;
                shiftNow = ride + (on ? l.level[0] : 0.0f);
                if (on) most = fmaxf(most, l.level[0] + ride);
                if (on && time > 5.0 && time < 20.0) past = fmax(past, z - (-0.19));
                if (n == 60 * 20 - 1) (on ? back : sagged) = z;
                if (on && n == 60 * 37 - 1) raised = z;
            }
        }
        printf("     air suspension: the load sags the axle to %.3f m with no levelling, %.3f m with it (the rest -0.190); +2 cm gives %.3f m; the shift at most %.3f m\n",
               sagged, back, raised, most);
        Check(takenAt >= 58 && takenAt <= 61 && sagged < -0.28 && fabs(back + 0.19) < 0.005 && past < 0.01 && fabs(raised - (-0.17)) < 0.005 &&
                  most <= kShiftMost + 1e-6f,
              "air suspension: the rest place taken after 1 s standing; half as much load again comes back within 5 mm in 15 s and never swings 1 cm past; "
              "a ride height of +2 cm on top stays (9.5 and 2 within the 12 cm); three times the load holds the shift to 12 cm");
    }

    // the air suspension's take waits for a standstill: moving, nothing is taken, and a stop starts the second over
    {
        SuspLevel l = {};
        const int partner[2] = { 1, 0 };
        const bool placed[2] = { true, true }, held[2] = { false, false };
        const float g[2] = { -0.2f, -0.2f }, r[2] = { 0.0f, 0.0f };
        bool any = false;
        for (int n = 0; n < 120; n++) any |= SuspLevelStep(l, 2, partner, kAlong2, kAcross2, g, r, placed, held, 0.0f, 2.0f, 1.0f / 60.0f);
        for (int n = 0; n < 30; n++) any |= SuspLevelStep(l, 2, partner, kAlong2, kAcross2, g, r, placed, held, 0.0f, 0.0f, 1.0f / 60.0f);
        any |= SuspLevelStep(l, 2, partner, kAlong2, kAcross2, g, r, placed, held, 0.0f, 0.5f, 1.0f / 60.0f);
        int at = -1;
        for (int n = 0; n < 90 && at < 0; n++)
            if (SuspLevelStep(l, 2, partner, kAlong2, kAcross2, g, r, placed, held, 0.0f, 0.0f, 1.0f / 60.0f)) at = n;
        Check(!any && at >= 58 && at <= 61 && Near(l.ref[0], -0.2f) && Near(l.ref[1], -0.2f),
              "air suspension: nothing is taken while moving, a move resets the second standing, and a full second standing takes each wheel's place");
    }

    // the keys: 2 cm a press within 10 cm, only with the air suspension on and under 20 km/h
    {
        int cm = 0;
        for (int i = 0; i < 7; i++) cm = AirHeightStep(cm, 1, 5.0f, true);
        const bool top = cm == kRideKeyMost;
        cm = AirHeightStep(cm, -1, 5.0f, true);
        const bool down = cm == 8;
        const bool fast = AirHeightStep(4, 1, 25.0f, true) == 4, off = AirHeightStep(4, 1, 5.0f, false) == 4;
        int low = 0;
        for (int i = 0; i < 9; i++) low = AirHeightStep(low, -1, 0.0f, true);
        Check(top && down && fast && off && low == -kRideKeyMost, "air suspension keys: 2 cm a press, held to +/-10 cm; nothing at 20 km/h and over or with it off");
    }

    // the review of build C: R1, the levelling acts only standing and an axle hanging or at its end keeps its shift; R2,
    // turned off, the shift and the keys' height fade with the time constant, never a step; the keys' height follows
    {
        const float dt = 1.0f / 60.0f;
        const int partner[2] = { 1, 0 };
        const bool placed[2] = { true, true }, free2[2] = { false, false }, hanging[2] = { true, false };
        const float r[2] = { 0.0f, 0.0f };
        SuspLevel l = {};
        const float rest[2] = { -0.2f, -0.2f };
        for (int n = 0; n < 61; n++) SuspLevelStep(l, 2, partner, kAlong2, kAcross2, rest, r, placed, free2, 0.0f, 0.0f, dt);
        // a load put on while moving at 11 km/h: nothing for 10 s, then levelling once a second under 10 km/h
        const float sag[2] = { -0.25f, -0.25f };
        for (int n = 0; n < 600; n++) SuspLevelStep(l, 2, partner, kAlong2, kAcross2, sag, r, placed, free2, 0.0f, 3.0f, dt);
        const bool movingHeld = l.level[0] == 0.0f;
        for (int n = 0; n < 59; n++) SuspLevelStep(l, 2, partner, kAlong2, kAcross2, sag, r, placed, free2, 0.0f, 0.0f, dt);
        const bool waits = l.level[0] == 0.0f;
        for (int n = 0; n < 120; n++) SuspLevelStep(l, 2, partner, kAlong2, kAcross2, sag, r, placed, free2, 0.0f, 0.0f, dt);
        const bool levels = l.level[0] > 0.01f;
        // an axle with one wheel hanging for 5 s, standing, its droop in the error: the shift kept, and after it lands
        const float kept = l.level[0];
        const float droop[2] = { 0.0f, -0.05f };
        for (int n = 0; n < 300; n++) SuspLevelStep(l, 2, partner, kAlong2, kAcross2, droop, r, placed, hanging, 0.0f, 0.0f, dt);
        const bool hangKept = l.level[0] == kept && l.level[1] == kept;
        SuspLevelStep(l, 2, partner, kAlong2, kAcross2, sag, r, placed, free2, 0.0f, 0.0f, dt);
        const bool landed = fabsf(l.level[0] - kept) < 0.001f;
        // the keys' height follows in steps of dt / 3 s of the way, never at once
        SuspLevel k = l;
        SuspLevelStep(k, 2, partner, kAlong2, kAcross2, sag, r, placed, free2, 0.04f, 0.0f, dt);
        const bool follows = k.keys > 0.0f && k.keys < 0.04f * dt / kLevelTau * 1.01f;
        // turned off with 0.08 levelled and 0.04 on the keys: each step moves each by at most dt / 3 s of it, and it ends
        SuspLevel f = {};
        f.action = 1, f.taken = true, f.height = f.level[0] = f.level[1] = 0.08f, f.keys = 0.04f;
        bool gentle = true, done = false;
        int steps = 0;
        while (!done && steps < 60 * 30)
        {
            const float was0 = f.level[0], wasK = f.keys;
            done = SuspLevelFade(f, 2, dt);
            gentle &= fabsf(was0 - f.level[0]) <= was0 * dt / kLevelTau + 1e-7f && fabsf(wasK - f.keys) <= wasK * dt / kLevelTau + 1e-7f;
            steps++;
        }
        printf("     air suspension off: 8 cm levelled and 4 cm on the keys fade out in %.1f s\n", steps * dt);
        Check(movingHeld && waits && levels && hangKept && landed && follows && gentle && done && steps > 60 * 10,
              "air suspension (review R1, R2): a load put on while moving waits for a second standing; a hanging axle keeps its shift for 5 s and "
              "after landing; the keys' height follows in steps; turned off, the shift and the keys fade by dt / 3 s a step and then end");
    }

    // the review of build C, R4: a take with one axle hanging waits until it lands, and then takes the landed place
    {
        const float dt = 1.0f / 60.0f;
        const int partner[4] = { 1, 0, 3, 2 };
        const bool placed[4] = { true, true, true, true }, rearHanging[4] = { false, false, true, false }, free4[4] = { false, false, false, false };
        const float r[4] = {};
        const float overDitch[4] = { -0.2f, -0.2f, 0.0f, -0.1f }, landed[4] = { -0.2f, -0.2f, -0.21f, -0.21f };
        SuspLevel l = {};
        bool early = false;
        for (int n = 0; n < 300; n++) early |= SuspLevelStep(l, 4, partner, kAlong4, kAcross4, overDitch, r, placed, rearHanging, 0.0f, 0.0f, dt);
        int at = -1;
        for (int n = 0; n < 90 && at < 0; n++)
            if (SuspLevelStep(l, 4, partner, kAlong4, kAcross4, landed, r, placed, free4, 0.0f, 0.0f, dt)) at = n;
        Check(!early && at >= 58 && at <= 61 && Near(l.ref[2], -0.21f) && Near(l.ref[0], -0.2f),
              "air suspension (review R4): standing 5 s with a rear wheel hanging takes nothing; after it lands a second standing takes the landed place");
    }

    // side to side, and under 10 km/h (the author, 2 November): a load on the left sags the left wheel 4 cm and the right
    // 1 cm; driving at 7 km/h each comes back to its own rest place within 5 mm in 15 s, so the truck is level again; at
    // 11 km/h nothing moves
    {
        const float dt = 1.0f / 60.0f;
        const int partner[2] = { 1, 0 };
        const bool placed[2] = { true, true }, free2[2] = { false, false };
        const float r[2] = {}, rest[2] = { -0.2f, -0.2f }, sag[2] = { 0.04f, 0.01f };
        SuspLevel l = {}, fast = {};
        for (int n = 0; n < 61; n++) SuspLevelStep(l, 2, partner, kAlong2, kAcross2, rest, r, placed, free2, 0.0f, 0.0f, dt);
        fast = l;
        float g[2] = {};
        for (int n = 0; n < 60 * 15; n++)
        {
            for (int i = 0; i < 2; i++) g[i] = rest[i] - sag[i] + l.level[i];
            SuspLevelStep(l, 2, partner, kAlong2, kAcross2, g, r, placed, free2, 0.0f, 2.0f, dt);
        }
        const float leftOff = g[0] - rest[0], rightOff = g[1] - rest[1];
        for (int n = 0; n < 60 * 15; n++)
        {
            for (int i = 0; i < 2; i++) g[i] = rest[i] - sag[i] + fast.level[i];
            SuspLevelStep(fast, 2, partner, kAlong2, kAcross2, g, r, placed, free2, 0.0f, 3.0f, dt);
        }
        printf("     air suspension side to side: 4 and 1 cm of sag end %.4f and %.4f m off at 7 km/h\n", leftOff, rightOff);
        Check(fabsf(leftOff) < 0.005f && fabsf(rightOff) < 0.005f && l.level[0] > l.level[1] + 0.02f && fast.level[0] == 0.0f && fast.level[1] == 0.0f,
              "air suspension: a load on one side is levelled side to side at 7 km/h, each wheel back within 5 mm in 15 s; nothing at 11 km/h");
    }

    // review R5: a rigid chassis on springs, each corner at the plane through the ground and the shifts (least squares)
    // less a load's sag; a wheel's place from its stock attach point is that less its ground. A 6 cm diagonal twist
    // standing and a 6x6 with 5 cm under its middle axle keep every shift within 5 mm of 0 for 30 s; a load at the rear
    // of a 4x4 still levels within 5 mm in 15 s
    {
        const float dt = 1.0f / 60.0f;
        auto plane = [](int n, const float *x, const float *y, const float *v, float *out) {
            float c = 0, vm = 0, xm = 0, ym = 0;
            for (int i = 0; i < n; i++) c += 1, vm += v[i], xm += x[i], ym += y[i];
            vm /= c, xm /= c, ym /= c;
            float sxx = 0, syy = 0, sxy = 0, sxv = 0, syv = 0;
            for (int i = 0; i < n; i++)
            {
                const float a = x[i] - xm, b = y[i] - ym, e = v[i] - vm;
                sxx += a * a, syy += b * b, sxy += a * b, sxv += a * e, syv += b * e;
            }
            const float det = sxx * syy - sxy * sxy, px = (sxv * syy - syv * sxy) / det, py = (syv * sxx - sxv * sxy) / det;
            for (int i = 0; i < n; i++) out[i] = vm + px * (x[i] - xm) + py * (y[i] - ym);
        };
        auto run = [&](int n, const int *partner, const float *x, const float *y, const float *ground, const float *sag, float seconds, float *off) {
            const bool placed[6] = { true, true, true, true, true, true }, free6[6] = {};
            const float r[6] = {}, flat[6] = {};
            SuspLevel l = {};
            float g[6] = {}, v[6] = {}, b[6] = {};
            for (int k = 0; k < 61; k++)
            {
                for (int i = 0; i < n; i++) v[i] = flat[i];
                plane(n, x, y, v, b);
                for (int i = 0; i < n; i++) g[i] = b[i] - 0.2f;
                SuspLevelStep(l, n, partner, x, y, g, r, placed, free6, 0.0f, 0.0f, dt);
            }
            float most = 0.0f;
            for (int k = 0; k < (int)(seconds * 60.0f); k++)
            {
                for (int i = 0; i < n; i++) v[i] = ground[i] + l.level[i];
                plane(n, x, y, v, b);
                for (int i = 0; i < n; i++) g[i] = b[i] - ground[i] - sag[i] - 0.2f;
                SuspLevelStep(l, n, partner, x, y, g, r, placed, free6, 0.0f, 0.0f, dt);
                for (int i = 0; i < n; i++) most = fmaxf(most, fabsf(l.level[i]));
            }
            for (int i = 0; i < n; i++) off[i] = g[i] + 0.2f;
            return most;
        };
        const int partner4[4] = { 1, 0, 3, 2 }, partner6[6] = { 1, 0, 3, 2, 5, 4 };
        const float none[6] = {}, twist[4] = { 0.03f, -0.03f, -0.03f, 0.03f };
        const float x6[6] = { 2.0f, 2.0f, 0.0f, 0.0f, -2.0f, -2.0f }, y6[6] = { 0.9f, -0.9f, 0.9f, -0.9f, 0.9f, -0.9f };
        const float hump[6] = { 0.0f, 0.0f, 0.05f, 0.05f, 0.0f, 0.0f }, rearLoad[4] = { 0.01f, 0.01f, 0.05f, 0.05f };
        float off[6];
        const float twisted = run(4, partner4, kAlong4, kAcross4, twist, none, 30.0f, off);
        const float humped = run(6, partner6, x6, y6, hump, none, 30.0f, off);
        run(4, partner4, kAlong4, kAcross4, none, rearLoad, 15.0f, off);
        float rearOff = 0.0f;
        for (int i = 0; i < 4; i++) rearOff = fmaxf(rearOff, fabsf(off[i]));
        printf("     air suspension (R5): the shift at most %.4f m on a 6 cm twist and %.4f m on a 6x6 hump; a rear load ends %.4f m off\n", twisted, humped, rearOff);
        Check(twisted < 0.005f && humped < 0.005f && rearOff < 0.005f,
              "air suspension (review R5): a 6 cm diagonal twist and a 6x6 with 5 cm under its middle axle keep every shift within 5 mm for 30 s; a rear load still levels");
    }

    // review R6: the front axle held for 5 s while the rear levels a load to its left, then let go: every wheel's shift
    // stays on one plane (within 1 mm) all the way, and the rear comes up
    {
        const float dt = 1.0f / 60.0f;
        const int partner[4] = { 1, 0, 3, 2 };
        const bool placed[4] = { true, true, true, true }, frontHeld[4] = { true, true, false, false }, free4[4] = {};
        const float r[4] = {}, sag[4] = { 0.0f, 0.0f, 0.03f, 0.01f };
        SuspLevel l = {};
        float g[4];
        for (int k = 0; k < 61; k++)
        {
            for (int i = 0; i < 4; i++) g[i] = -0.2f;
            SuspLevelStep(l, 4, partner, kAlong4, kAcross4, g, r, placed, free4, 0.0f, 0.0f, dt);
        }
        float twist = 0.0f;
        for (int k = 0; k < 60 * 15; k++)
        {
            for (int i = 0; i < 4; i++) g[i] = -0.2f - sag[i] + l.level[i];
            SuspLevelStep(l, 4, partner, kAlong4, kAcross4, g, r, placed, k < 60 * 5 ? frontHeld : free4, 0.0f, 0.0f, dt);
            // on a box of four corners a plane has level[0] - level[1] == level[2] - level[3]
            twist = fmaxf(twist, fabsf((l.level[0] - l.level[1]) - (l.level[2] - l.level[3])));
        }
        printf("     air suspension (R6): the shifts at most %.5f m off one plane; the rear up %.4f m\n", twist, 0.5f * (l.level[2] + l.level[3]));
        Check(twist < 0.001f && l.level[2] + l.level[3] > 0.02f,
              "air suspension (review R6): a front axle held for 5 s while the rear levels a side load, then let go, keeps every shift within 1 mm of one plane");
    }

    // AirHorizon (the author, 2 November): a rigid 4x4 on springs standing on ground 8 % up to its left and 4 % down to
    // its nose comes level to the horizon within 0.3 degrees in 20 s; on 20 % to the left the shifts stop at 12 cm and
    // the lean is what is left; with it off the body stays with the ground
    {
        const float dt = 1.0f / 60.0f;
        const int partner[4] = { 1, 0, 3, 2 };
        const bool placed[4] = { true, true, true, true }, free4[4] = {};
        const float r[4] = {};
        // the corners' heights: the least-squares plane through the ground plus the shifts; the tilt is its slope
        auto stand = [&](float side, float nose, bool horizon, float &tiltF, float &tiltL, float &most, bool driveOff = false) {
            SuspLevel l = {};
            float g[4], ground[4], B[4];
            for (int i = 0; i < 4; i++) ground[i] = side * kAcross4[i] + nose * kAlong4[i];
            most = 0.0f;
            for (int k = 0; k < 60 * (driveOff ? 31 : 21); k++)
            {
                // the drive off (review R7): from 21 s on flat ground at 20 km/h
                const bool off = driveOff && k >= 60 * 21;
                if (off)
                    for (int i = 0; i < 4; i++) ground[i] = 0.0f;
                float mean = 0.0f, px = 0.0f, py = 0.0f;
                for (int i = 0; i < 4; i++) mean += (ground[i] + l.level[i]) / 4.0f;
                for (int i = 0; i < 4; i++)
                    px += (ground[i] + l.level[i]) * kAlong4[i] / (4.0f * 1.5f * 1.5f), py += (ground[i] + l.level[i]) * kAcross4[i] / (4.0f * 0.8f * 0.8f);
                for (int i = 0; i < 4; i++) B[i] = mean + px * kAlong4[i] + py * kAcross4[i], g[i] = B[i] - ground[i] - 0.2f;
                tiltF = px, tiltL = py;
                const float tilt[2] = { tiltF, tiltL };
                SuspLevelStep(l, 4, partner, kAlong4, kAcross4, g, r, placed, free4, 0.0f, off ? 20.0f / 3.6f : 0.0f, dt, horizon ? tilt : nullptr);
                for (int i = 0; i < 4; i++) most = fmaxf(most, fabsf(l.level[i]));
            }
        };
        float f1, l1, m1, f2, l2, m2, f3, l3, m3;
        stand(0.08f, -0.04f, true, f1, l1, m1);
        stand(0.20f, 0.0f, true, f2, l2, m2);
        stand(0.08f, -0.04f, false, f3, l3, m3);
        float f4, l4, m4;
        stand(0.08f, 0.0f, true, f4, l4, m4, true);
        printf("     air suspension to the horizon: 8 %% and 4 %% end at %.4f and %.4f; 20 %% ends at %.3f with the shift at %.3f m; off it stays %.3f and %.3f; "
               "driven off onto flat ground at 20 km/h it leans %.4f after 10 s\n",
               l1, f1, l2, m2, l3, f3, l4);
        Check(fabsf(l1) < 0.005f && fabsf(f1) < 0.005f && m2 <= kShiftMost + 1e-5f && fabsf(l2 - (0.20f - kShiftMost / 0.8f)) < 0.01f && fabsf(l3 - 0.08f) < 1e-4f &&
                  fabsf(f3 + 0.04f) < 1e-4f && fabsf(l4) < 0.0087f,
              "air suspension AirHorizon: a 4x4 on 8 % to a side and 4 % to its nose comes level within 0.3 degrees; on 20 % it stops at 12 cm; off it stays with the ground; levelled on 8 % and driven off onto flat ground at 20 km/h, within 0.5 degrees of flat in 10 s (R7)");
    }

    RealImageTest();

    printf(g_failed ? "TRUCK TEST FAILED (%d)\n" : "TRUCK TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
