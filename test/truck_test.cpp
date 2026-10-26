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
#include "maps.h"
#include "game_code.h"
#include "game_globals.h"
#include "clock_scan.h"
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
        char gearboxName[kMaxText], gearboxKey[96], gearsKey[96];
        uint32_t gearboxAt = 0;
        const bool noName = !TruckGearboxName(kMemory, kVehicle, gearboxName, gearboxAt);
        PutText(kVehicle + 0xA00, "g_ab cd", 0);                   // a text with a space: no name
        PutText(kVehicle + 0xA28, "g_scout_highway_long", kTexts + 0x380);
        const bool named = TruckGearboxName(kMemory, kVehicle, gearboxName, gearboxAt) && !strcmp(gearboxName, "g_scout_highway_long") && gearboxAt == 0xA28;
        TruckRaw gearsOnly = {};
        TruckRead(kMemory, r, gearsOnly);
        GearboxKey(gearsOnly, gearboxName, gearboxKey, sizeof gearboxKey);
        GearboxKey(gearsOnly, "", gearsKey, sizeof gearsKey);
        // at + 0x9a0, where the game keeps it, a mod's name that does not begin with "g_" is taken too
        PutText(kVehicle + 0x9a0, "box-of_a_mod", 0);
        char atName[kMaxText];
        uint32_t atAt = 0;
        const bool atPlace = TruckGearboxName(kMemory, kVehicle, atName, atAt) && !strcmp(atName, "box-of_a_mod") && atAt == 0x9a0;
        Check(noName && named && atPlace && !strcmp(gearboxKey, "g_scout_highway_long") && !strcmp(gearsKey, "gears 3 6 12 16 high 8"),
              "gearbox: its name read from the vehicle at + 0x9a0, whatever it begins with, else the first name with g_ near it, a text that is no "
              "name passed over; without a name it goes by its gears");
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

    RealImageTest();

    printf(g_failed ? "TRUCK TEST FAILED (%d)\n" : "TRUCK TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
