// truck.h: reads the truck being driven out of the game's memory. Nothing is written, and nothing in the game is
// called. Every read goes through one function (TruckMemory::peek), so the offline test can put a made up game in its
// place (test\truck_test.cpp). gauges.cpp finds the truck's objects twice a second and when a read fails (TruckFind),
// reads their values on every pass (TruckRead) and asks what the game is showing (WorldRead). The engine speed,
// the range and the tilt are worked out from these values in models.h.
//
// Where the values are, in the Steam build of 22 July 2026 (kGaugesExeStamp):
//   control = [exe + 0x2a8eb78]      the game's object for the truck being driven (it exists while a level is loaded);
//                                    vehicle = [control + 8]
//   vehicle + 0x768                  bit 0: the engine is on, bit 1: it has finished starting, bit 2: headlights,
//                                    bit 3: high beam, bit 4: reversing lights, bit 5: brake lights
//   vehicle + 0x11C                  seconds the engine has been stalling (it dies at 1.4)
//   vehicle + 0x730                  seconds until a starting engine runs (1.8 at the start, then down to 0)
//   vehicle + 0xE38 .. + 0xE6C       what the game burns fuel by (floats): + 0xE38 and + 0xE3C the engine's two shares,
//                                    + 0xE40 the factor for all wheel drive, + 0xE44 for a damaged engine, + 0xE48 for
//                                    a damaged gearbox, + 0xE50 .. + 0xE58 each gear's factor (reverse, neutral, 1 .. N,
//                                    high), + 0xE68 the idle share, + 0xE6C litres a minute out of a holed tank
//   action = [vehicle + 0x68]        the drive train: + 0x38 power factor (float), + 0x3C automatic (byte),
//                                    + 0x44 throttle (float, below 0 = forwards), + 0x48 handbrake, + 0x49 all wheel
//                                    drive, + 0x4A differential lock (bytes), + 0x70 the gear engaged, + 0x74 the gear
//                                    picked (ints), + 0xB4 how hard the engine pulls (0 to 1) and + 0xCC (floats),
//                                    + 0x58 .. + 0x60 the wheel speed of each gear (floats: reverse, 1 .. N, high)
//   model = [vehicle + 0x60]         + 0x5E8 fuel in litres, + 0x5F0 the tank's size
//   chassis = [model + 0x230]        the rigid body: + 0x170 its forward axis, + 0x180 up, + 0x190 left (four floats
//                                    each, y is the world's up), + 0x1A0 position, + 0x230 velocity
//   [vehicle + 0x200] .. [+ 0x208]   the wheel models; wheel = [wheel model + 0x2C8] (its class at exe + 0x2258918):
//                                    + 0xE8 how it is driven (int), + 0x16C its turning speed in rad/s
//   vehicle + 0x150                  the handle of the engine's damage: { int damage, int capacity, float effect };
//                                    + 0x148 the gearbox's and + 0x158 the fuel tank's, the same way
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
                      kRvaGarage = 0x2a90728, kRvaInput = 0x2a8ebe0;
enum { kMaxWheels = 32, kMaxGears = 16 };

// Game memory: peek copies size bytes from addr and says whether it could. In the game that is ReadProcessMemory on
// the game's own process, so an object the game freed under the mod is a failed read and not a fault.
struct TruckMemory
{
    bool (*peek)(uint64_t addr, void *out, size_t size);
    uint64_t base; // where the exe is loaded
    template <typename T> bool get(uint64_t addr, T &out) const { return addr >= 0x10000 && peek(addr, &out, sizeof out); }
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
    float fuel, tank;
    bool damageKnown;
    int damage, damageCapacity;
    float damageEffect;
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
    if (!m.get(m.base + kRvaControl, r.control) || !m.get(r.control + 8, r.vehicle) || !m.get(r.vehicle + 0x68, r.action) ||
        !m.get(r.vehicle + 0x60, r.model) || !m.get(r.model + 0x230, r.chassis) || !m.get(r.vehicle + 0x200, begin) || !m.get(r.vehicle + 0x208, end))
        return false;
    if (!r.action || !r.chassis || end <= begin || (end - begin) % 8 || end - begin > kMaxWheels * 8) return false;
    const int count = (int)((end - begin) / 8);
    for (int i = 0; i < count; i++)
    {
        uint64_t model = 0, wheel = 0, cls = 0;
        if (m.get(begin + (uint64_t)i * 8, model) && m.get(model + 0x2C8, wheel) && m.get(wheel, cls) && cls == m.base + kRvaWheelClass) r.wheel[r.wheels++] = wheel;
    }
    return r.wheels > 0;
}

// The values of the truck found. False when a read fails or the game now drives another truck: find it again.
inline bool TruckRead(const TruckMemory &m, const TruckRefs &r, TruckRaw &t)
{
    t = TruckRaw{};
    uint64_t vehicle = 0;
    if (!m.get(r.control + 8, vehicle) || vehicle != r.vehicle) return false;
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
    for (int i = 0; i < r.wheels; i++)
    {
        if (!m.get(r.wheel[i] + 0xE8, t.drive[i]) || !m.get(r.wheel[i] + 0x16C, t.spin[i])) return false;
        t.wheels++;
    }
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

// What the game is showing. control: the driven truck's control object, 0 when there is none.
inline void WorldRead(const TruckMemory &m, uint64_t control, World &w)
{
    w = World{};
    uint64_t game = 0, map = 0, viewer = 0, garage = 0, input = 0;
    uint8_t wheel[5] = {}; // input + 0x1D8 .. + 0x1DC
    w.known = m.get(m.base + kRvaGame, game) && m.get(game + 0x30, w.menu) && m.get(game + 0x4A, w.loading);
    if (m.get(m.base + kRvaMap, map) && map) w.mapThere = m.get(map + 0x148, w.mapOpen);
    w.viewer = m.get(m.base + kRvaViewer, viewer) && viewer;
    w.garage = m.get(m.base + kRvaGarage, garage) && garage;
    if (m.get(m.base + kRvaInput, input) && m.get(input + 0x1D8, wheel))
    {
        w.menuInput = wheel[0];
        w.shifter = wheel[2] && wheel[4];
    }
    if (control) m.get(control + 0xD8, w.functionMenu);
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

// The state the cluster draws, from the truck's values. The engine speed, the range and the tilt are not among them:
// gauges.cpp puts in what models.h works out, and here the tachometer rests at idle, the range is not known and the
// tilt lamp is dark. The temperature and the boost have no source in this version: their needles rest at the low end
// of their dials, and the cold lamp stays dark.
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

// What makes the probe write between its two second lines: a switch, the gear, the engine, the damage, the pedal
// going down or up, the game's state.
inline bool TruckProbeEvent(const TruckRaw &a, const World &wa, const TruckRaw &b, const World &wb)
{
    return a.flags != b.flags || a.handbrake != b.handbrake || a.awd != b.awd || a.diffLock != b.diffLock || a.gear != b.gear || a.gearEngaged != b.gearEngaged ||
           a.automatic != b.automatic ||
           a.powerFactor != b.powerFactor || a.damage != b.damage || a.damageCapacity != b.damageCapacity || a.torqueFactor != b.torqueFactor ||
           a.gearSpeeds != b.gearSpeeds || a.wheels != b.wheels || (a.throttle != 0.0f) != (b.throttle != 0.0f) || (a.stall > 0.0f) != (b.stall > 0.0f) ||
           (a.startTimer > 0.0f) != (b.startTimer > 0.0f) || wa.menu != wb.menu || wa.loading != wb.loading || wa.mapThere != wb.mapThere ||
           wa.mapOpen != wb.mapOpen || wa.viewer != wb.viewer || wa.garage != wb.garage || wa.menuInput != wb.menuInput || wa.shifter != wb.shifter ||
           wa.functionMenu != wb.functionMenu;
}
