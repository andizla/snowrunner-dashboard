// truck.h: reads the truck being driven out of the game's memory, and writes one thing into it: the damage of the
// truck's engine (TruckDamageAdd). Nothing in the game is called. Every read goes through one function
// (TruckMemory::peek) and every write through another (TruckMemory::poke), so the offline test can put a made up
// game in their place (test\truck_test.cpp). gauges.cpp finds the truck's objects twice a second and when a read
// fails (TruckFind), reads their values on every pass (TruckRead) and asks what the game is showing (WorldRead). The
// engine speed, the range, the tilt, the coolant temperature and what it costs the engine are worked out from these
// values in models.h.
//
// Where the values are, in the Steam build of 22 July 2026 (kGaugesExeStamp); in another build the statics of the exe
// (the places "exe +" below) are found by the code that uses them (game_globals.h), and the offsets are taken as they are:
//   control = [exe + 0x2a8eb78]      the game's object for the truck being driven (it exists while a level is loaded);
//                                    vehicle = [control + 8]
//   vehicle + 0x768                  bit 0: the engine is on, bit 1: it has finished starting, bit 2: headlights,
//                                    bit 3: high beam, bit 4: reversing lights, bit 5: brake lights
//   vehicle + 0x114                  seconds the game has simulated the truck for (a float, in steps of a thirtieth;
//                                    it stands still while a level loads and whenever the game does not simulate)
//   vehicle + 0x11C                  seconds the engine has been stalling (it dies at 1.4)
//   vehicle + 0x730                  seconds until a starting engine runs (1.8 at the start, then down to 0)
//   vehicle + 0xE38 .. + 0xE6C       what the game burns fuel by (floats): + 0xE38 and + 0xE3C the engine's two shares,
//                                    + 0xE40 the factor for all wheel drive, + 0xE44 for a damaged engine, + 0xE48 for
//                                    a damaged gearbox, + 0xE50 and + 0xE58 the begin and the end of the list of each
//                                    gear's factor (reverse, neutral, 1 .. N, high), + 0xE68 the idle share, + 0xE6C
//                                    litres a minute out of a holed tank
//   action = [vehicle + 0x68]        the drive train: + 0x38 power factor (float), + 0x3C automatic (byte),
//                                    + 0x44 throttle (float, below 0 = forwards), + 0x48 handbrake, + 0x49 all wheel
//                                    drive, + 0x4A differential lock (bytes), + 0x70 the gear engaged, + 0x74 the gear
//                                    picked (ints), + 0xB4 how hard the engine pulls (0 to 1) and + 0xCC the share
//                                    of its torque a damaged engine keeps (floats), + 0x58 .. + 0x60 the wheel speed
//                                    of each gear (floats: reverse, 1 .. N, high)
//   model = [vehicle + 0x60]         + 0x5E8 fuel in litres, + 0x5F0 the tank's size
//   chassis = [model + 0x230]        the rigid body: + 0x170 its forward axis, + 0x180 up, + 0x190 left (four floats
//                                    each, y is the world's up), + 0x1A0 position, + 0x230 velocity
//   [vehicle + 0x200] .. [+ 0x208]   the wheel models; wheel = [wheel model + 0x2C8] (its class at exe + 0x2258918):
//                                    + 0xE8 how it is driven (int), + 0x13C where its centre is in the world
//                                    (three floats), + 0x16C its turning speed in rad/s, + 0x17C metres of water it
//                                    stands in (0 on dry ground)
//   vehicle + 0x150                  the handle of the engine's damage: { int damage, int capacity, float effect };
//                                    + 0x148 the gearbox's and + 0x158 the fuel tank's, the same way
//   vehicle + 0x108, + 0x10C         the damage of all the truck's parts together and what they take (ints), as the
//                                    game's damage update last summed them
//   vehicle + 0x930 .. + 0x938       the names of the engine sets the truck picks its engine from (0x20 bytes each);
//                                    vehicle + 0x968 the name of the engine fitted
//   [exe + 0x2a9e8e0 + 8]            the game's engine sets, a list: an entry holds the next one at + 0, the set's
//                                    name at + 0x10 and its engines at + 0x30, a tree by name. An engine's record is
//                                    0x40 into its node: + 0x74 the share of its damage from which the torque falls,
//                                    + 0x78 the torque factor there and + 0x7C with the engine worn out (floats),
//                                    + 0x84 the damage it takes (int)
//   game = [exe + 0x2a8eb90]         the game's main object: + 0x30 (int) above 0 while a menu holds the game,
//                                    + 0x4A (byte) set while a level loads
//   [exe + 0x2a8eb30]                the map, when there is one: + 0x148 (byte) set while it is open
//   [exe + 0x2a8eb38], [+ 0x2a90728] the truck viewer and the garage, when there is one
//   input = [exe + 0x2a8ebe0]        + 0x1D8 (byte): the game's menus have the input; + 0x1DA and + 0x1DC (bytes): a
//                                    wheel's shifter picks the gears
//   control + 0xD8                   (byte) the function menu is open
#pragma once
#include "gauges.h"

static const uint64_t kRvaControl = 0x2a8eb78, kRvaWheelClass = 0x2258918, kRvaGame = 0x2a8eb90, kRvaMap = 0x2a8eb30, kRvaViewer = 0x2a8eb38,
                      kRvaGarage = 0x2a90728, kRvaInput = 0x2a8ebe0, kRvaEngineSets = 0x2a9e8e0;

// Those places in the build that runs, from the exe's base: the Steam build's until gauges.cpp finds them in another
// build by the code that uses them (game_globals.h). 0 for a place not found: what is read there is not read.
struct GamePlaces
{
    uint64_t control, wheelClass, game, map, viewer, garage, input, engineSets;
};
inline GamePlaces g_places = { kRvaControl, kRvaWheelClass, kRvaGame, kRvaMap, kRvaViewer, kRvaGarage, kRvaInput, kRvaEngineSets };
enum { kMaxWheels = 32, kMaxGears = 16 };

// Game memory: peek copies size bytes from addr and says whether it could, and poke copies size bytes to addr the
// same way (it is left out where nothing is to be written). In the game they are ReadProcessMemory and
// WriteProcessMemory on the game's own process, so an object the game freed under the mod is a failed call and not
// a fault.
struct TruckMemory
{
    bool (*peek)(uint64_t addr, void *out, size_t size);
    uint64_t base; // where the exe is loaded
    bool (*poke)(uint64_t addr, const void *in, size_t size);
    template <typename T> bool get(uint64_t addr, T &out) const { return addr >= 0x10000 && peek(addr, &out, sizeof out); }
    template <typename T> bool put(uint64_t addr, const T &in) const { return poke && addr >= 0x10000 && poke(addr, &in, sizeof in); }
};

// Where the driven truck's objects are. TruckFind fills it; TruckRead uses it until a read fails or the game drives
// another truck.
struct TruckRefs
{
    uint64_t control, vehicle, action, model, chassis;
    uint64_t wheel[kMaxWheels];
    int wheels;
};

// The truck's values as the game holds them. The probe log prints them as they are.
struct TruckRaw
{
    uint8_t flags;                 // vehicle + 0x768
    float stall, startTimer;       // vehicle + 0x11C, + 0x730
    float powerFactor;             // action + 0x38
    uint8_t automatic;             // action + 0x3C
    float throttle;                // action + 0x44
    uint8_t handbrake, awd, diffLock;
    int gear;                      // action + 0x74: below 0 reverse, 0 neutral, 1 .. N the gears, N + 1 the high gear
    int gearEngaged;               // action + 0x70: the gear the drive train is in; it follows gear after a shift
    float actionB4, torqueFactor;  // action + 0xB4, + 0xCC
    int gearSpeeds;                // how many entries the gearbox has: reverse, the gears, high
    float gearSpeed[kMaxGears];
    int wheels;
    int drive[kMaxWheels];         // 0 not driven, 1 driven, 2 driven with all wheel drive
    float spin[kMaxWheels];
    bool waterKnown;               // each wheel's place and the water at it were read
    float wheelAt[kMaxWheels][3];  // wheel + 0x13C: where the wheel's centre is in the world
    float wheelWater[kMaxWheels];  // wheel + 0x17C: metres of water the wheel stands in, 0 on dry ground
    bool clockKnown;
    float clock;                   // vehicle + 0x114: the seconds the game has simulated this truck for
    float fuel, tank;
    bool damageKnown;
    int damage, damageCapacity;
    float damageEffect;
    int damageSum, damageSumCapacity; // vehicle + 0x108, + 0x10C: all the parts' damage and what they take, as the game last summed them
    float gearboxEffect, tankEffect; // what the gearbox's and the fuel tank's damage does, 0 when sound
    bool burnKnown;                // the numbers of the game's fuel burn were read
    float burnShare[2];            // vehicle + 0xE38, + 0xE3C
    float burnAwd, burnEngine, burnGearbox, burnIdle, burnLeak; // + 0xE40, + 0xE44, + 0xE48, + 0xE68, + 0xE6C
    int gearBurns;                 // entries of gearBurn: reverse, neutral, the gears, high
    float gearBurn[kMaxGears + 1];
    float forward[3], up[3], left[3], position[3], velocity[3]; // the chassis: its three axes in the world, where it is, how it moves
    float speed;                   // metres a second along the forward axis, below 0 backwards
};

// What the game is showing, as far as the cluster cares.
struct World
{
    bool known;          // the game's main object was read
    int menu;            // game + 0x30
    uint8_t loading;     // game + 0x4A
    bool mapThere;       // the map object exists
    uint8_t mapOpen;     // map + 0x148
    bool viewer, garage; // the truck viewer and the garage exist
    uint8_t menuInput;   // input + 0x1D8
    uint8_t shifter;     // input + 0x1DA and + 0x1DC are both set
    uint8_t functionMenu; // control + 0xD8
    bool menusKnown;     // menuInput and functionMenu were both read: a 0 in them then means that no menu is up
};

inline bool TruckEngineOn(const TruckRaw &t) { return (t.flags & 1) != 0; }
inline bool TruckHeadlights(const TruckRaw &t) { return (t.flags & 4) != 0; }
// The highest numbered gear: the gearbox's entries less reverse and high.
inline int TruckTopGear(const TruckRaw &t) { return t.gearSpeeds >= 3 ? t.gearSpeeds - 2 : 0; }

// A part's damage sits in a shared block behind a handle: the block is the handle without bits 48 to 55, the word at
// + 0x10 says whether it holds data (bit 0 set, bit 2 clear), and the data starts 0x38 in.
inline uint64_t TruckBlock(const TruckMemory &m, uint64_t handleAt)
{
    uint64_t block = 0;
    uint16_t flags = 0;
    if (!m.get(handleAt, block)) return 0;
    block &= 0xFF00FFFFFFFFFFFFull;
    if (!block || !m.get(block + 0x10, flags) || (flags & 4) || !(flags & 1)) return 0;
    return block + 0x38;
}

// The driven truck's objects. False when the game drives no truck, or one without a wheel (each wheel is checked by
// its class, so a wrong guess about the layout finds none).
inline bool TruckFind(const TruckMemory &m, TruckRefs &r)
{
    r = TruckRefs{};
    uint64_t begin = 0, end = 0;
    if (!g_places.control || !m.get(m.base + g_places.control, r.control) || !m.get(r.control + 8, r.vehicle) || !m.get(r.vehicle + 0x68, r.action) ||
        !m.get(r.vehicle + 0x60, r.model) || !m.get(r.model + 0x230, r.chassis) || !m.get(r.vehicle + 0x200, begin) || !m.get(r.vehicle + 0x208, end))
        return false;
    if (!r.action || !r.chassis || end <= begin || (end - begin) % 8 || end - begin > kMaxWheels * 8) return false;
    const int count = (int)((end - begin) / 8);
    for (int i = 0; i < count; i++)
    {
        uint64_t model = 0, wheel = 0, cls = 0;
        if (m.get(begin + (uint64_t)i * 8, model) && m.get(model + 0x2C8, wheel) && m.get(wheel, cls) && cls == m.base + g_places.wheelClass) r.wheel[r.wheels++] = wheel;
    }
    return r.wheels > 0;
}

// The values of the truck found. False when a read fails or the game now drives another truck: find it again.
inline bool TruckRead(const TruckMemory &m, const TruckRefs &r, TruckRaw &t)
{
    t = TruckRaw{};
    uint64_t vehicle = 0, action = 0;
    if (!m.get(r.control + 8, vehicle) || vehicle != r.vehicle) return false;
    // the drive train the truck was found with: the game rebuilds it under a truck that stays (a gearbox fitted), and
    // the mod's writes go through it on this pass, so an old one is no read (and the truck is looked up anew)
    if (!m.get(r.vehicle + 0x68, action) || action != r.action) return false;
    // the action from + 0x38 to + 0xD0 in one read
    uint8_t a[0x98];
    if (!m.get(r.vehicle + 0x768, t.flags) || !m.get(r.vehicle + 0x11C, t.stall) || !m.get(r.vehicle + 0x730, t.startTimer) || !m.get(r.action + 0x38, a)) return false;
    uint64_t speedsBegin = 0, speedsEnd = 0;
    memcpy(&t.powerFactor, a + 0x00, 4);
    t.automatic = a[0x04];
    memcpy(&t.throttle, a + 0x0C, 4);
    t.handbrake = a[0x10];
    t.awd = a[0x11];
    t.diffLock = a[0x12];
    memcpy(&speedsBegin, a + 0x20, 8);
    memcpy(&speedsEnd, a + 0x28, 8);
    memcpy(&t.gearEngaged, a + 0x38, 4);
    memcpy(&t.gear, a + 0x3C, 4);
    memcpy(&t.actionB4, a + 0x7C, 4);
    memcpy(&t.torqueFactor, a + 0x94, 4);
    if (speedsEnd > speedsBegin && (speedsEnd - speedsBegin) % 4 == 0 && speedsEnd - speedsBegin <= kMaxGears * 4)
    {
        const int count = (int)((speedsEnd - speedsBegin) / 4);
        uint8_t speeds[kMaxGears * 4];
        if (m.peek(speedsBegin, speeds, (size_t)count * 4))
        {
            t.gearSpeeds = count;
            memcpy(t.gearSpeed, speeds, (size_t)count * 4);
        }
    }
    t.waterKnown = r.wheels > 0;
    for (int i = 0; i < r.wheels; i++)
    {
        // the wheel from + 0x13C to + 0x180 in one read: its place, its turning speed at + 0x16C, the water at + 0x17C.
        // A wheel that gives only its turning speed is still a wheel: the water is then not known.
        float w[17];
        if (!m.get(r.wheel[i] + 0xE8, t.drive[i])) return false;
        if (m.get(r.wheel[i] + 0x13C, w))
        {
            memcpy(t.wheelAt[i], w, sizeof t.wheelAt[i]);
            t.spin[i] = w[12];
            t.wheelWater[i] = w[16] >= 0.0f && w[16] < 100.0f ? w[16] : 0.0f; // 0 too for a depth that is no number
        }
        else
        {
            if (!m.get(r.wheel[i] + 0x16C, t.spin[i])) return false;
            t.waterKnown = false;
        }
        t.wheels++;
    }
    t.clockKnown = m.get(r.vehicle + 0x114, t.clock);
    float fuel[3]; // + 0x5E8 litres, + 0x5F0 the tank
    if (!m.get(r.model + 0x5E8, fuel)) return false;
    t.fuel = fuel[0];
    t.tank = fuel[2];
    int block[3];
    const uint64_t data = TruckBlock(m, r.vehicle + 0x150);
    if (data && m.get(data, block) && block[1] > 0 && block[1] < 1000000 && block[0] >= 0 && block[0] <= block[1])
    {
        t.damageKnown = true;
        t.damage = block[0];
        t.damageCapacity = block[1];
        memcpy(&t.damageEffect, &block[2], 4);
    }
    int sums[2];
    if (m.get(r.vehicle + 0x108, sums))
    {
        t.damageSum = sums[0];
        t.damageSumCapacity = sums[1];
    }
    // the fuel burn's numbers; a truck without them is still a truck
    const uint64_t gearbox = TruckBlock(m, r.vehicle + 0x148), tank = TruckBlock(m, r.vehicle + 0x158);
    if (gearbox) m.get(gearbox + 8, t.gearboxEffect);
    if (tank) m.get(tank + 8, t.tankEffect);
    uint8_t b[0x38]; // vehicle + 0xE38 .. + 0xE70
    if (m.get(r.vehicle + 0xE38, b))
    {
        uint64_t burnsBegin = 0, burnsEnd = 0;
        memcpy(t.burnShare, b, 8);
        memcpy(&t.burnAwd, b + 0x08, 4);
        memcpy(&t.burnEngine, b + 0x0C, 4);
        memcpy(&t.burnGearbox, b + 0x10, 4);
        memcpy(&burnsBegin, b + 0x18, 8);
        memcpy(&burnsEnd, b + 0x20, 8);
        memcpy(&t.burnIdle, b + 0x30, 4);
        memcpy(&t.burnLeak, b + 0x34, 4);
        if (burnsEnd > burnsBegin && (burnsEnd - burnsBegin) % 4 == 0 && burnsEnd - burnsBegin <= (kMaxGears + 1) * 4)
        {
            const int count = (int)((burnsEnd - burnsBegin) / 4);
            if (m.peek(burnsBegin, t.gearBurn, (size_t)count * 4))
            {
                t.gearBurns = count;
                t.burnKnown = true;
            }
        }
    }
    float frame[16]; // + 0x170 forward, + 0x180 up, + 0x190 left, + 0x1A0 position, four floats each
    if (!m.get(r.chassis + 0x170, frame) || !m.get(r.chassis + 0x230, t.velocity)) return false;
    memcpy(t.forward, frame, sizeof t.forward);
    memcpy(t.up, frame + 4, sizeof t.up);
    memcpy(t.left, frame + 8, sizeof t.left);
    memcpy(t.position, frame + 12, sizeof t.position);
    t.speed = t.velocity[0] * t.forward[0] + t.velocity[1] * t.forward[1] + t.velocity[2] * t.forward[2];
    return true;
}

// Whether the values can be a truck's: finite numbers in the ranges a truck has. What fails here is not drawn.
inline bool TruckSane(const TruckRaw &t)
{
    return t.wheels > 0 && isfinite(t.speed) && fabsf(t.speed) < 200.0f && isfinite(t.fuel) && isfinite(t.tank) && t.tank > 0.0f && t.tank < 100000.0f &&
           t.fuel >= 0.0f && t.fuel <= t.tank * 1.01f && t.gear >= -1 && t.gear <= kMaxGears && t.gearEngaged >= -1 &&
           t.gearEngaged <= kMaxGears && isfinite(t.throttle) && fabsf(t.throttle) <= 2.0f && isfinite(t.stall) && isfinite(t.startTimer);
}

// ---- the engine's record, and the write of its damage ----

enum { kMaxText = 96, kMaxEngineSets = 16, kMaxSetEntries = 256, kMaxTreeSteps = 64 };
// The numbers the probe log reckons with for an engine whose record is not found: the share of its damage from
// which the torque falls, the torque factor there and the torque factor with the engine worn out. Nothing is
// written by them.
static const float kEngineThreshold = 0.7f, kEngineTorqueMin = 1.0f, kEngineTorqueMax = 0.6f;
// How far the exe reaches from where it is loaded (its image size; gauges.cpp sets the running one's at the start). The
// game's objects are never inside it, and nothing is written there.
inline uint64_t g_exeSize = 0x2e06000;

// A text as the game keeps it, 0x20 bytes: the characters themselves while it has room for fewer than 16, else a
// pointer to them; the length at + 0x10 and the room at + 0x18. False for one that cannot be read or has kMaxText
// characters or more.
inline bool TruckText(const TruckMemory &m, uint64_t at, char (&out)[kMaxText], size_t &length)
{
    uint8_t text[0x20];
    uint64_t chars = 0, size = 0, room = 0;
    out[0] = 0;
    length = 0;
    if (!m.get(at, text)) return false;
    memcpy(&chars, text, 8);
    memcpy(&size, text + 0x10, 8);
    memcpy(&room, text + 0x18, 8);
    if (size >= kMaxText || room < size) return false;
    if (room < 16) memcpy(out, text, (size_t)size);
    else if (size && (chars < 0x10000 || !m.peek(chars, out, (size_t)size))) return false;
    out[size] = 0;
    length = (size_t)size;
    return true;
}

// The order of two texts as the game's own compare has it (RVA 0x4d8230): by their bytes, and the shorter one first
// when the longer begins with it. Below 0: a comes first; 0: the same text.
inline int TruckTextOrder(const char *a, size_t aLength, const char *b, size_t bLength)
{
    const int order = memcmp(a, b, aLength < bLength ? aLength : bLength);
    return order ? order : aLength < bLength ? -1 : aLength > bLength ? 1 : 0;
}

// Which truck is driven, for what the mod keeps per truck: the scales in Gauges.ini and the models' memory.
// key: the truck's kind as the game's files name it, a text the game holds at model + 0x588 ("chevrolet_ck1500";
// for a truck from a mod with the mod's number behind a stroke, "mod_suv/1000001"). It is what the game's save
// calls the truck's type, so every truck of the kind has it, in every game. Where the text cannot be read the key
// is made from the truck's build (named is false then), which trucks of one kind share as well.
// id: the text in braces the game holds for the one truck while it is in the world, behind the pointer at
// vehicle + 0x48 ("{8E42BF0A-F7C6-40f9-B0F2-225DE74CE031}"); "" when it is not read. It is here for the log
// alone: the save holds none for a truck in storage, so it is nothing to keep settings by.
struct TruckIdentity
{
    char key[kMaxText];
    char id[40];
    bool named;      // key is the kind's name, not the build
};

// The shape of the game's id of a truck: 38 characters, braces around hex digits in groups of 8, 4, 4, 4 and 12.
inline bool TruckIdText(const char *text, size_t length)
{
    if (length != 38 || text[0] != '{' || text[37] != '}') return false;
    for (size_t i = 1; i < 37; i++)
    {
        const char c = text[i];
        const bool dash = i == 9 || i == 14 || i == 19 || i == 24;
        if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// The key of a truck whose kind's name is not read: its build, which is the tank's size, the number of wheels and
// of the gearbox's entries, and the damage the engine takes.
inline void TruckBuildKey(const TruckRaw &t, char (&key)[kMaxText])
{
    sprintf_s(key, "tank%g_wheels%d_gearbox%d_engine%d", t.tank, t.wheels, t.gearSpeeds, t.damageKnown ? t.damageCapacity : 0);
}

// The identity of the truck in r, whose values are t.
inline void TruckKey(const TruckMemory &m, const TruckRefs &r, const TruckRaw &t, TruckIdentity &id)
{
    id = TruckIdentity{};
    size_t length = 0;
    if (TruckText(m, r.model + 0x588, id.key, length) && length && id.key[0])
    {
        // the key names a section of the ini: what would end the name there, or does not print, is replaced
        for (char *c = id.key; *c; c++)
            if ((unsigned char)*c <= 0x20 || (unsigned char)*c > 0x7E || *c == '[' || *c == ']') *c = '_';
        id.named = true;
    }
    else TruckBuildKey(t, id.key);
    uint64_t chars = 0;
    char text[39];
    if (m.get(r.vehicle + 0x48, chars) && chars >= 0x10000 && m.peek(chars, text, sizeof text) && !text[38] && TruckIdText(text, 38)) memcpy(id.id, text, sizeof text);
}

// The driven truck from pass to pass. taken is the truck the mod goes by, its key "" while none is read. Another
// key than taken's counts once it was read on kTruckPasses passes in a row, and on kTruckPassesUnnamed for a key
// made from the build: a text caught while the game writes it, or a build of which a part was not read for a
// moment, is no other truck. last and passes: the key the last pass read, and for how many passes running.
enum { kTruckPasses = 2, kTruckPassesUnnamed = 20 };
struct TruckWho { TruckIdentity taken, last; int passes; };

// One pass that read id. True when the truck read is the one taken. False while another key waits to be taken and
// on the pass that takes it; taking is true on that pass alone, when what goes with one truck is to start over.
inline bool TruckWhoStep(TruckWho &w, const TruckIdentity &id, bool &taking)
{
    taking = false;
    const bool again = !strcmp(id.key, w.last.key);
    w.last = id;
    if (!strcmp(id.key, w.taken.key))
    {
        if (id.id[0]) memcpy(w.taken.id, id.id, sizeof w.taken.id); // an id that is not read leaves the last one
        w.passes = 0;
        return true;
    }
    w.passes = again ? w.passes + 1 : 1;
    if (w.passes < (id.named ? kTruckPasses : kTruckPassesUnnamed)) return false;
    w.taken = id;
    w.passes = 0;
    taking = true;
    return false;
}

// After a loading screen: whether the truck that drives on is the one driven before it, by the game's id for it
// (before: the id as the screen came up, "" for none; after: the id read now, "" or nullptr while none is read;
// waited: milliseconds since the load ended). kLoadSame keeps its heat; kLoadOther (another truck, or none before)
// and kLoadNone (no truck for kLoadWaitMs) make every engine cold; kLoadWait is not decided yet.
enum { kLoadWait, kLoadSame, kLoadOther, kLoadNone };
static const uint64_t kLoadWaitMs = 60000;
inline int LoadTruckAfter(const char *before, const char *after, uint64_t waited)
{
    if (after && after[0]) return before[0] && !strcmp(before, after) ? kLoadSame : kLoadOther;
    return waited >= kLoadWaitMs ? kLoadNone : kLoadWait;
}

// A number for a key, never 0, for the models to tell trucks apart by.
inline uint64_t TruckKeyNumber(const char *key)
{
    uint64_t n = 1469598103934665603ull;
    for (; *key; key++) n = (n ^ (uint8_t)*key) * 1099511628211ull;
    return n ? n : 1;
}

// An engine's numbers for its damage, from the game's record of the engine. found is false when the record was not
// reached.
struct EngineRecord
{
    bool found;
    uint64_t at;                            // where the record is
    char name[kMaxText];                    // the engine fitted, by its name in the game's files
    float threshold, torqueMin, torqueMax;  // record + 0x74, + 0x78, + 0x7C
    int capacity;                           // record + 0x84: the damage the engine takes
    // the engine's figures from its file, 0 where one is no likely number: Torque (record + 0x60), MaxDeltaAngVel
    // (+ 0x68), FuelConsumption (+ 0x6C) and EngineResponsiveness (+ 0x80); found by the probe's dump of a scout's
    // record against its file (Torque 195000, 0.04, 2, 0.35)
    float torque, deltaAngVel, consumption, responsiveness;
};

// The engines of a set, by the set's name: the tree at + 0x30 of the set's entry in the game's list of sets. 0 when
// no set has the name. The game finds the entry through a hash; here the list is read from end to end.
inline uint64_t TruckEngineSet(const TruckMemory &m, const char *name, size_t length)
{
    uint64_t head = 0, entry = 0, tree = 0;
    if (!g_places.engineSets || !m.get(m.base + g_places.engineSets + 8, head) || !m.get(head, entry)) return 0;
    for (int i = 0; i < kMaxSetEntries && entry && entry != head; i++)
    {
        char key[kMaxText];
        size_t keyLength = 0;
        if (TruckText(m, entry + 0x10, key, keyLength) && keyLength == length && !memcmp(key, name, length)) return m.get(entry + 0x30, tree) ? tree : 0;
        if (!m.get(entry, entry)) return 0;
    }
    return 0;
}

// An engine's record in a set's tree, by the engine's name. The tree's head holds the root at + 8. A node holds its
// left child at + 0, its right child at + 0x10, a byte at + 0x19 that is set on the head alone, the engine's name
// at + 0x20 and the record at + 0x40. The way down is the game's own (RVA 0xc3c010): right while the node's name
// comes before the engine's, else left, keeping the node. 0 when the engine is not in the tree.
inline uint64_t TruckEngineInSet(const TruckMemory &m, uint64_t tree, const char *name, size_t length)
{
    uint64_t at = 0, kept = tree;
    char key[kMaxText];
    size_t keyLength = 0;
    if (!m.get(tree + 8, at)) return 0;
    for (int i = 0; i < kMaxTreeSteps; i++)
    {
        uint8_t head = 1;
        if (!m.get(at + 0x19, head)) return 0;
        if (head)
        {
            // the node kept last is the first whose name does not come before the engine's: it is the engine's, or the engine is not here
            if (kept == tree || !TruckText(m, kept + 0x20, key, keyLength) || TruckTextOrder(key, keyLength, name, length)) return 0;
            return kept + 0x40;
        }
        if (!TruckText(m, at + 0x20, key, keyLength)) return 0;
        const bool before = TruckTextOrder(key, keyLength, name, length) < 0;
        if (!before) kept = at;
        if (!m.get(at + (before ? 0x10 : 0), at)) return 0;
    }
    return 0;
}

// The record of the engine fitted to a vehicle, found as the game's damage update finds it (RVA 0xd7bb90) and by
// reading only: the engine's name is looked up in each of the truck's engine sets in turn. Every walk ends after a
// set number of steps, so memory that is not what it is taken for gives "not found". e.name is the engine's name
// whenever that could be read.
inline bool TruckEngineRecord(const TruckMemory &m, uint64_t vehicle, EngineRecord &e)
{
    e = EngineRecord{};
    size_t length = 0;
    uint64_t begin = 0, end = 0;
    if (!TruckText(m, vehicle + 0x968, e.name, length) || !length) return false;
    if (!m.get(vehicle + 0x930, begin) || !m.get(vehicle + 0x938, end) || end <= begin || (end - begin) % 0x20 || end - begin > kMaxEngineSets * 0x20) return false;
    for (uint64_t set = begin; set < end; set += 0x20)
    {
        char setName[kMaxText];
        size_t setLength = 0;
        if (!TruckText(m, set, setName, setLength)) continue;
        const uint64_t tree = TruckEngineSet(m, setName, setLength);
        const uint64_t record = tree ? TruckEngineInSet(m, tree, e.name, length) : 0;
        float numbers[3]; // + 0x74 the threshold, + 0x78 and + 0x7C the two torque factors
        int capacity = 0;
        if (!record || !m.get(record + 0x74, numbers) || !m.get(record + 0x84, capacity)) continue;
        // numbers that no engine has: the record is not what it was taken for
        if (!(numbers[0] >= 0.0f && numbers[0] <= 1.0f) || !(numbers[1] >= 0.0f && numbers[1] <= 2.0f) || !(numbers[2] >= 0.0f && numbers[2] <= 2.0f) || capacity <= 0)
            return false;
        e.threshold = numbers[0];
        e.torqueMin = numbers[1];
        e.torqueMax = numbers[2];
        e.capacity = capacity;
        e.at = record;
        float figure = 0.0f;
        e.torque = m.get(record + 0x60, figure) && figure >= 1000.0f && figure <= 1e8f ? figure : 0.0f;
        e.deltaAngVel = m.get(record + 0x68, figure) && figure > 0.0f && figure <= 10.0f ? figure : 0.0f;
        e.consumption = m.get(record + 0x6C, figure) && figure > 0.0f && figure <= 100.0f ? figure : 0.0f;
        e.responsiveness = m.get(record + 0x80, figure) && figure > 0.0f && figure <= 10.0f ? figure : 0.0f;
        e.found = true;
        return true;
    }
    return false;
}

// The name of the gearbox fitted, as the game's files name it (the game's own begin with "g_", as "g_scout_default";
// a mod's may not): a text the game holds at vehicle + 0x9a0, as it holds the engine's at + 0x968 (found by 0.13.9's
// search on four trucks, two of the game's and two modded: "g_truck_default", "g_scout_default" and the mods' own). A
// name there is taken whatever it begins with; where none is, the texts from + 0x900 to + 0xC00 are looked through
// for the first that begins with "g_". A name: letters, digits, "_" and "-". at: where it stood, from the vehicle,
// for the log. False where none is.
static const uint32_t kGearboxNameAt = 0x9a0, kGearboxNameFrom = 0x900, kGearboxNameTo = 0xC00;
inline bool TruckNameText(const TruckMemory &m, uint64_t at, char (&out)[kMaxText], size_t &length)
{
    if (!TruckText(m, at, out, length) || length < 2) return false;
    for (size_t i = 0; i < length; i++)
    {
        const char c = out[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}
inline bool TruckGearboxName(const TruckMemory &m, uint64_t vehicle, char (&out)[kMaxText], uint32_t &at)
{
    out[0] = 0;
    at = 0;
    if (!vehicle) return false;
    char text[kMaxText];
    size_t length = 0;
    if (TruckNameText(m, vehicle + kGearboxNameAt, text, length))
    {
        memcpy(out, text, length + 1);
        at = kGearboxNameAt;
        return true;
    }
    for (uint32_t o = kGearboxNameFrom; o + 0x20 <= kGearboxNameTo; o += 8)
    {
        if (!TruckNameText(m, vehicle + o, text, length) || length < 3 || text[0] != 'g' || text[1] != '_') continue;
        memcpy(out, text, length + 1);
        at = o;
        return true;
    }
    return false;
}

// The gearbox's key for what the mod keeps for it ([Gearbox:<key>] in Gauges.ini): its name where it was read, else
// its gears' speeds as the game holds them, the gears and the high gear ("gears 1.5 4 7 11 19 30 high 20").
inline void GearboxKey(const TruckRaw &t, const char *name, char *out, size_t size)
{
    if (name && name[0])
    {
        strncpy_s(out, size, name, _TRUNCATE);
        return;
    }
    size_t n = (size_t)snprintf(out, size, "gears");
    const int top = TruckTopGear(t);
    for (int g = 1; g <= top && n + 12 < size; g++) n += (size_t)snprintf(out + n, size - n, " %g", t.gearSpeed[g]);
    if (top >= 1 && n + 16 < size) snprintf(out + n, size - n, " high %g", t.gearSpeed[top + 1]);
}

// The truck's mass in kg as its files give it (the bodies of its chassis, without addons, cargo or a trailer): a float
// at model + 0x784, found by the probe's scan of five trucks against their files (a scout's 9007 where its bodies weigh
// 9001, a pickup's 2212, others of 5507, 6314 and 8100). 0 where it is no likely mass.
inline float TruckMass(const TruckMemory &m, uint64_t model)
{
    float mass = 0.0f;
    return model && m.get(model + 0x784, mass) && mass >= 100.0f && mass <= 500000.0f ? mass : 0.0f;
}

// A stretch of the game's memory as text for the probe log, to find where the game keeps a figure its files give (an
// engine's Torque, a truck's mass): words 4-byte words from the address from, each as a number where it reads as a
// float of a likely size (0.001 to 10 million, either sign), else in hex, "?" where it cannot be read.
inline void TruckWordsText(const TruckMemory &m, uint64_t from, int words, char *out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < words && n + 24 < size; i++)
    {
        uint32_t word = 0;
        float f = 0.0f;
        if (!m.get(from + 4 * (uint64_t)i, word)) n += (size_t)snprintf(out + n, size - n, "%s?", i ? " " : "");
        else
        {
            memcpy(&f, &word, 4);
            const float a = fabsf(f);
            n += (size_t)(a >= 0.001f && a <= 1e7f ? snprintf(out + n, size - n, "%s%g", i ? " " : "", f) : snprintf(out + n, size - n, "%s%x", i ? " " : "", word));
        }
    }
}

// The floats from lo to hi in the bytes from the address from, as "+offset value" for the probe log, at most most of
// them: where a truck's mass may stand. Their count.
inline int TruckFloatsText(const TruckMemory &m, uint64_t from, int bytes, float lo, float hi, int most, char *out, size_t size)
{
    size_t n = 0;
    int count = 0;
    out[0] = 0;
    for (int at = 0; at + 4 <= bytes && count < most && n + 24 < size; at += 4)
    {
        float f = 0.0f;
        if (!m.get(from + (uint64_t)at, f) || !(f >= lo && f <= hi)) continue;
        n += (size_t)snprintf(out + n, size - n, "%s+%x %g", count ? ", " : "", at, f);
        count++;
    }
    return count;
}

// What the game's damage update makes of an engine's damage (RVA 0xd6f690). While the damage's share of what the
// engine takes is no more than the record's threshold there is no effect, and the engine keeps all its torque. Past
// the threshold the effect is how far the share has come on the way from there to 1, and the torque factor goes
// from the record's first to its second with it. Without a record the result is a guess from kEngineThreshold,
// kEngineTorqueMin and kEngineTorqueMax, for the probe log to show.
inline void TruckEngineEffect(int damage, int capacity, const EngineRecord &e, float &effect, float &torque)
{
    const float threshold = e.found ? e.threshold : kEngineThreshold, first = e.found ? e.torqueMin : kEngineTorqueMin,
                second = e.found ? e.torqueMax : kEngineTorqueMax;
    float share = capacity > 0 ? (float)damage / (float)capacity : 0.0f;
    effect = 0.0f;
    torque = 1.0f;
    if (share > threshold)
    {
        share = (share - threshold) / (1.0f - threshold);
        effect = share;
        torque = (1.0f - share) * first + share * second;
    }
}

// What a write of engine damage did. written: the damage went into the game; why: what stood in the way when it
// did not. before, wrote and capacity are the engine's damage as read, as written and what the engine takes; after
// is what the game held right after the write. The effect and the torque factor are as read and as left.
struct DamageWrite
{
    bool written;
    const char *why; // a string literal
    int before, wrote, after, capacity;
    float effectBefore, effectAfter, torqueBefore, torqueAfter;
};

// Adds points to the damage of the driven truck's engine, in the game's memory. The damage only goes up from what
// was read, and it stops one short of what the engine takes: the last point stops the engine, and that stays the
// game's to do. Once the damage is past the record's threshold, the effect and the torque factor are written as the
// game's own damage update would leave them (TruckEngineEffect), and never to less of an effect than the game
// holds. Without a record only the damage is written, and the effect follows at the game's next damage event.
// Nothing is written unless the game still drives the truck in r through the same objects, the engine's damage and
// the drive train lie outside the exe, the damage reads as an engine's does, and the engine takes what its record
// says it takes. The game changes the damage too, on its own thread: the damage is read last, right before the
// write, and a change of the game's that falls into the instant between the two is lost.
inline bool TruckDamageAdd(const TruckMemory &m, const TruckRefs &r, int points, const EngineRecord &e, DamageWrite &w)
{
    w = DamageWrite{};
    w.why = "";
    uint64_t control = 0, vehicle = 0, action = 0;
    int block[3] = {}, after = 0;
    float torque = 0.0f, effect = 0.0f;
    if (!m.poke || points <= 0) { w.why = "there is nothing to write"; return false; }
    if (!g_places.control || !m.get(m.base + g_places.control, control) || control != r.control || !m.get(r.control + 8, vehicle) || vehicle != r.vehicle ||
        !m.get(r.vehicle + 0x68, action) || action != r.action)
    {
        w.why = "the truck is no longer the one being driven";
        return false;
    }
    const uint64_t data = TruckBlock(m, r.vehicle + 0x150);
    if ((data >= m.base && data < m.base + g_exeSize) || (r.action >= m.base && r.action < m.base + g_exeSize))
    {
        w.why = "the engine's damage is not where the game keeps its objects";
        return false;
    }
    if (!data || !m.get(r.action + 0xCC, torque) || !m.get(data, block)) { w.why = "the engine's damage could not be read"; return false; }
    w.before = w.wrote = w.after = block[0];
    w.capacity = block[1];
    memcpy(&w.effectBefore, &block[2], 4);
    w.effectAfter = w.effectBefore;
    w.torqueBefore = w.torqueAfter = torque;
    if (block[1] < 2 || block[1] >= 1000000 || block[0] < 0 || block[0] > block[1]) { w.why = "the engine's damage does not read as an engine's"; return false; }
    if (e.found && e.capacity != block[1]) { w.why = "the engine does not take what its record says"; return false; }
    const int room = block[1] - 1 - block[0];
    if (room <= 0) { w.why = "the engine is at its last point"; return false; }
    const int to = block[0] + (points < room ? points : room);
    if (!m.put(data, to)) { w.why = "the game's memory did not take the write"; return false; }
    w.written = true;
    w.wrote = w.after = to;
    if (m.get(data, after)) w.after = after;
    if (!e.found) return true;
    TruckEngineEffect(to, block[1], e, effect, torque);
    if (effect > w.effectBefore)
    {
        if (m.put(data + 8, effect)) w.effectAfter = effect;
        if (m.put(r.action + 0xCC, torque)) w.torqueAfter = torque;
    }
    return true;
}

// What the game is showing. control: the driven truck's control object, 0 when there is none.
inline void WorldRead(const TruckMemory &m, uint64_t control, World &w)
{
    w = World{};
    uint64_t game = 0, map = 0, viewer = 0, garage = 0, input = 0;
    uint8_t wheel[5] = {}; // input + 0x1D8 .. + 0x1DC
    w.known = g_places.game && m.get(m.base + g_places.game, game) && m.get(game + 0x30, w.menu) && m.get(game + 0x4A, w.loading);
    if (g_places.map && m.get(m.base + g_places.map, map) && map) w.mapThere = m.get(map + 0x148, w.mapOpen);
    w.viewer = g_places.viewer && m.get(m.base + g_places.viewer, viewer) && viewer;
    w.garage = g_places.garage && m.get(m.base + g_places.garage, garage) && garage;
    const bool inputRead = g_places.input && m.get(m.base + g_places.input, input) && m.get(input + 0x1D8, wheel);
    if (inputRead)
    {
        w.menuInput = wheel[0];
        w.shifter = wheel[2] && wheel[4];
    }
    w.menusKnown = control && m.get(control + 0xD8, w.functionMenu) && inputRead;
}

// A truck is being driven in the world: no level is loading, no menu holds the game, the map is shut, and the truck
// is neither in the garage nor being put back into the world.
inline bool WorldDriving(const World &w) { return w.known && w.menu <= 0 && !w.loading && !w.mapOpen && !w.viewer && !w.garage; }

// The gearbox position by the letters the cluster shows: "A" automatic (the gear's number goes with it), "H" the high
// gear, "L", "L+", "L-" the low range, "" a numbered gear picked on a wheel's shifter, reverse and neutral. The low
// range is the game's first gear with a power factor: 0.45 in L, 0.2 in L- and 1 in L+.
inline const char *TruckGearMode(const TruckRaw &t, bool shifter)
{
    if (t.gear <= 0) return "";
    if (t.automatic) return "A";
    if (t.gear == TruckTopGear(t) + 1) return "H";
    if (t.gear == 1 && !shifter) return t.powerFactor < 0.325f ? "L-" : t.powerFactor < 0.725f ? "L" : "L+";
    return "";
}

// The state the cluster draws, from the truck's values. The engine speed, the range, the tilt and the coolant
// temperature are not among them: gauges.cpp puts in what models.h works out, and here the tachometer rests at idle,
// the range is not known, the tilt lamp and the cold lamp are dark and the temperature needle rests at the low end
// of its dial, as does the boost's.
inline void TruckToState(const TruckRaw &t, const World &w, const GaugeSettings &s, GaugeState &st)
{
    st = GaugeState{};
    GaugeStateSettings(st, s);
    st.engineOn = TruckEngineOn(t);
    st.speedKmh = fabsf(t.speed) * 3.6f;
    st.rpm = st.engineOn ? GaugeScaleOf(s).idle : 0.0f;
    st.gear = t.gear;
    st.gearMode = TruckGearMode(t, w.shifter != 0);
    st.lowGear = st.gearMode[0] == 'L';
    st.fuelFrac = t.tank > 0.0f ? SettingsRange(t.fuel / t.tank, 0.0f, 1.0f) : 0.0f;
    st.rangeKm = -1.0f;
    st.handbrake = t.handbrake != 0;
    st.awd = t.awd != 0;
    st.diffLock = t.diffLock != 0;
    st.headlights = TruckHeadlights(t);
    st.damage = t.damageKnown && t.damage * 2 > t.damageCapacity;
    st.tilt = -1;
}

// ---- the probe log (ini Probe=1): the values as read, for checking them against the game ----

// The truck's values in two lines of text. byPosition: metres a second from the way the chassis went, to hold the
// speed against.
inline void TruckProbeText(const TruckRaw &t, const World &w, float byPosition, char *first, size_t firstSize, char *second, size_t secondSize)
{
    sprintf_s(first, firstSize,
              "flags %02X (engine %d, running %d, lights %d) stall %.3f start %.2f | handbrake %d awd %d diff %d | gear %d (engaged %d) automatic %d power %.3f = %s%s | "
              "throttle %.3f | speed %.2f m/s = %.1f km/h (%.2f m/s by the way gone) | fuel %.2f of %.2f l | engine damage %d of %d, effect %.3f, torque x%.3f%s | B4 %.3f",
              t.flags, t.flags & 1, (t.flags >> 1) & 1, (t.flags >> 2) & 1, t.stall, t.startTimer, t.handbrake, t.awd, t.diffLock, t.gear, t.gearEngaged, t.automatic, t.powerFactor,
              t.gear < 0 ? "R" : t.gear == 0 ? "N" : "", t.gear > 0 ? (TruckGearMode(t, w.shifter != 0)[0] ? TruckGearMode(t, w.shifter != 0) : "a numbered gear") : "",
              t.throttle, t.speed, t.speed * 3.6f, byPosition, t.fuel, t.tank, t.damage, t.damageCapacity, t.damageEffect, t.torqueFactor,
              t.damageKnown ? "" : " (no damage data)", t.actionB4);
    char wheels[kMaxWheels * 16] = "", speeds[kMaxGears * 12] = "";
    for (int i = 0; i < t.wheels; i++) sprintf_s(wheels + strlen(wheels), sizeof wheels - strlen(wheels), " %d:%.2f", t.drive[i], t.spin[i]);
    for (int i = 0; i < t.gearSpeeds; i++) sprintf_s(speeds + strlen(speeds), sizeof speeds - strlen(speeds), " %.3f", t.gearSpeed[i]);
    sprintf_s(second, secondSize,
              "wheels (drive:rad/s)%s | gear speeds%s | game: menu %d loading %d map %d open %d viewer %d garage %d menu input %d shifter %d function menu %d", wheels, speeds,
              w.menu, w.loading, w.mapThere, w.mapOpen, w.viewer, w.garage, w.menuInput, w.shifter, w.functionMenu);
}

// Whether two numbers hold the same bits. A value that is no number is then the same as itself, where != would
// call it changed on every pass.
inline bool TruckSameBits(float a, float b) { return !memcmp(&a, &b, sizeof a); }

// What makes the probe write between its two second lines: a switch, the gear, the engine, the damage, the pedal
// going down or up, the game's state.
inline bool TruckProbeEvent(const TruckRaw &a, const World &wa, const TruckRaw &b, const World &wb)
{
    return a.flags != b.flags || a.handbrake != b.handbrake || a.awd != b.awd || a.diffLock != b.diffLock || a.gear != b.gear || a.gearEngaged != b.gearEngaged ||
           a.automatic != b.automatic ||
           !TruckSameBits(a.powerFactor, b.powerFactor) || a.damage != b.damage || a.damageCapacity != b.damageCapacity || !TruckSameBits(a.torqueFactor, b.torqueFactor) ||
           !TruckSameBits(a.damageEffect, b.damageEffect) || a.damageSum != b.damageSum ||
           a.gearSpeeds != b.gearSpeeds || a.wheels != b.wheels || (a.throttle != 0.0f) != (b.throttle != 0.0f) || (a.stall > 0.0f) != (b.stall > 0.0f) ||
           (a.startTimer > 0.0f) != (b.startTimer > 0.0f) || wa.menu != wb.menu || wa.loading != wb.loading || wa.mapThere != wb.mapThere ||
           wa.mapOpen != wb.mapOpen || wa.viewer != wb.viewer || wa.garage != wb.garage || wa.menuInput != wb.menuInput || wa.shifter != wb.shifter ||
           wa.functionMenu != wb.functionMenu;
}
