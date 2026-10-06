// models.h: what the cluster shows that the game does not hold: the engine speed, the fuel range, the tilt and the
// coolant temperature, worked out from the truck's values (truck.h) on every pass of the mod's thread. No game memory
// is touched here, so the offline test (test\models_test.cpp) runs the models on drives it makes up.
//
// The engine speed. The game has no engine speed, only wheels, gears and a pedal. Each gear has a wheel speed it is
// governed to, which the game works out from the gearbox's number for the gear (TruckGearTop). The share of it the
// driven wheels turn at is the share of the way from idle to the red zone the needle stands at, so a gear run out
// reaches the red zone and the next gear starts lower. The pedal lifts the needle above that share while the wheels
// are slow, as an engine pulls up against a load before the truck moves. In neutral the pedal revs the engine freely.
// The needle follows with a short lag, drops for a moment when a gear goes in, rises from 0 while the engine starts
// and falls with the game's stall counter.
//
// The range. Litres in the tank over the litres a kilometre the truck burned on the last three kilometres it drove.
// Fuel burned while standing counts against no distance and is left out, as is fuel that leaves the tank in one go.
// The readout shows no number until the truck has driven half a kilometre: the figure of a shorter way is mostly
// the pulling away.
//
// The tilt. The chassis has three axes in the world: forward, up and left. How far the forward axis points up is the
// slope the truck stands on, and how far the left axis points up or down is its lean to the side. Both are smoothed
// over a third of a second and held against the settings' angles for the lamp.
//
// The coolant temperature. The game has no engine heat. Here the engine makes heat by how far the pedal is down and
// how fast it turns, and a small share of that at idle. The radiator takes heat away by how much warmer the coolant
// is than the air outside, by how far the thermostat is open (shut below 82 C, open from 92 C) and by the air that
// goes through it: more the faster the truck drives, and a fan helps from about 100 C. So a warm engine holds about
// 86 C at idle, stands near 100 C when it works hard at speed, and climbs into the red zone from 118 C only when the
// pedal is floored at a crawl. A stopped engine cools slowly towards the air outside. Three of the settings are the
// times all of this takes, and a fourth is the outside temperature. Each truck keeps its own temperature.
#pragma once
#include "truck.h"

// ---- the engine speed ----

static const float kTachUp = 0.18f, kTachDown = 0.25f, kTachOff = 0.4f; // seconds the needle lags by: rising, falling, engine off
static const float kTachLoad = 0.65f;      // how far a floored pedal lifts the needle with the wheels standing, as a share of idle to red zone
static const float kTachFree = 0.9f;       // the same in neutral
static const float kTachOverrun = 1.1f;    // wheels faster than the gear's top take the needle this far, into the red zone
static const float kTachDip = 0.85f, kTachDipTime = 0.3f; // a gear going in: the engine speed times this, for this long
static const float kStartTime = 1.8f;      // seconds the game's engine takes to start
static const float kStallRate = 0.714f;    // the game's own: the engine is dead when the stall counter times this is 1

// The mean turning speed of the driven wheels in rad/s, whichever way they turn. A wheel is driven always (1) or
// with all wheel drive on (2).
inline float TruckDrivenSpin(const TruckRaw &t)
{
    float sum = 0.0f, all = 0.0f;
    int n = 0;
    for (int i = 0; i < t.wheels; i++)
    {
        all += fabsf(t.spin[i]);
        if (t.drive[i] == 1 || (t.drive[i] == 2 && t.awd)) { sum += fabsf(t.spin[i]); n++; }
    }
    return n ? sum / n : t.wheels ? all / t.wheels : 0.0f;
}

// The wheel speed in rad/s the game governs a gear to, 0 for neutral or a gear the gearbox does not have. From the
// gearbox's number a for the gear: 2 a + 5, and a + 2 in the high gear. The low range is the first gear held to a
// share of that by the power factor.
inline float TruckGearTop(const TruckRaw &t, int gear)
{
    if (gear == 0 || gear >= t.gearSpeeds) return 0.0f;
    const float a = t.gearSpeed[gear < 0 ? 0 : gear];
    if (!(a > 0.0f)) return 0.0f;
    float top = gear == TruckTopGear(t) + 1 ? a + 2.0f : 2.0f * a + 5.0f;
    if (gear == 1 && !t.automatic) top = 1.0f + (top - 1.0f) * SettingsRange(t.powerFactor, 0.0f, 1.0f);
    return top;
}

// How far the pedal is down, 0 to 1. The game holds forwards as the negative side; an automatic in reverse drives on
// the positive side, and the other side is the brake.
inline float TruckPedal(const TruckRaw &t)
{
    const float p = t.automatic && t.gearEngaged < 0 ? t.throttle : -t.throttle;
    return SettingsRange(p, 0.0f, 1.0f);
}

// The share of its top speed the engaged gear's wheels turn at, 0 in neutral.
inline float TachWheelShare(const TruckRaw &t)
{
    const float top = TruckGearTop(t, t.gearEngaged);
    return top > 0.0f ? SettingsRange(TruckDrivenSpin(t) / top, 0.0f, kTachOverrun) : 0.0f;
}

// The engine speed the needle heads for, before the lag.
inline float TachTarget(const TruckRaw &t, const GaugeScale &k)
{
    if (!TruckEngineOn(t)) return 0.0f;
    if (t.startTimer > 0.0f && !(t.flags & 2)) return k.idle * SettingsRange(1.0f - t.startTimer / kStartTime, 0.0f, 1.0f);
    const float pedal = sqrtf(TruckPedal(t));
    float n;
    if (t.gearEngaged == 0) n = kTachFree * pedal;
    else
    {
        const float wheel = TachWheelShare(t), slack = 1.0f - fminf(wheel, 1.0f);
        n = wheel + kTachLoad * pedal * slack * sqrtf(slack);
    }
    const float alive = SettingsRange(1.0f - kStallRate * t.stall, 0.0f, 1.0f);
    return (k.idle + (k.redline - k.idle) * n) * alive * alive;
}

struct TachModel
{
    bool running;  // the model has a truck: the next step lags from rpm
    float rpm;     // what the needle shows
    int gear;      // the gear engaged on the last step
    float dip;     // seconds left of a gear change's dip
};

// One step of dt seconds. The first step on a truck puts the needle where it belongs without a lag.
inline void TachStep(TachModel &m, const TruckRaw &t, const GaugeScale &k, float dt)
{
    if (m.running && (t.gearEngaged != m.gear || t.gear != t.gearEngaged) && TruckEngineOn(t)) m.dip = kTachDipTime;
    m.gear = t.gearEngaged;
    float target = TachTarget(t, k);
    if (m.dip > 0.0f)
    {
        target *= kTachDip;
        m.dip -= dt;
    }
    if (!m.running)
    {
        m.rpm = target >= 0.0f ? target : 0.0f; // 0 too for a target that is no number
        m.running = true;
        return;
    }
    const float tau = !TruckEngineOn(t) ? kTachOff : target > m.rpm ? kTachUp : kTachDown;
    m.rpm += (target - m.rpm) * (1.0f - expf(-dt / tau));
    if (!(m.rpm >= 0.0f)) m.rpm = 0.0f;
}

// How far the needle stands on the way from idle to the red zone, 0 to 1.
inline float TachShare(const TachModel &m, const GaugeScale &k)
{
    const float share = k.redline > k.idle ? (m.rpm - k.idle) / (k.redline - k.idle) : 0.0f;
    return share > 0.0f ? fminf(share, 1.0f) : 0.0f; // 0 too for a needle that is no number
}

// ---- the fuel the game burns ----

// The game's own tachometer value, 0 to 1 (0.19 at idle, 0.69 floored): the game burns fuel by it.
inline float TruckGameRevs(const TruckRaw &t)
{
    const float pedal = TruckPedal(t);
    const float h = pedal < 0.01f ? 0.0f : fminf(powf(pedal, 0.25f) + 0.25f, 1.0f) * t.actionB4;
    const float d = t.damageKnown && t.damageCapacity > 0 ? (float)t.damage / (float)t.damageCapacity : 0.0f;
    const float alive = SettingsRange(1.0f - kStallRate * t.stall, 0.0f, 1.0f);
    return ((0.1f * d + 0.19f * (1.0f - d)) * (1.0f - h) + (0.69f * (1.0f - d) + d) * h) * alive * alive;
}

// Litres a minute the game burns now, by its own rule; below 0 when the rule's numbers were not read. The engine's
// load runs from the idle share to the gear's factor, times the engine's two shares, more with all wheel drive and
// with a damaged engine or gearbox, plus what a holed tank loses.
inline float TruckFuelRate(const TruckRaw &t)
{
    if (!t.burnKnown) return -1.0f;
    if (!TruckEngineOn(t) || (t.startTimer > 0.0f && !(t.flags & 2))) return 0.0f;
    const int at = t.gear + 1;
    const float gear = at >= 0 && at < t.gearBurns ? t.gearBurn[at] : 1.0f;
    const float load = SettingsRange(2.0f * (TruckGameRevs(t) - 0.19f), 0.0f, 1.0f);
    float rate = ((1.0f - load) * t.burnIdle + load * gear) * (t.burnShare[0] + t.burnShare[1]);
    if (t.awd) rate *= t.burnAwd;
    rate *= t.damageEffect * t.burnEngine + 1.0f - t.damageEffect;
    rate *= t.gearboxEffect * t.burnGearbox + 1.0f - t.gearboxEffect;
    return rate + t.tankEffect * t.burnLeak;
}

// ---- the range ----

static const float kRangeWindow = 3000.0f;  // metres the litres a kilometre are measured over
static const float kRangeFirst = 500.0f;    // metres to drive before the readout shows a number
static const float kRangeMark = 50.0f;      // metres between two marks of the window
static const float kRangeMoving = 0.5f;     // metres a second: slower than this is standing
static const float kRangeGulp = 0.1f;       // litres: more than this gone in one step left the tank some other way than through the engine
static const float kRangeSmooth = 10.0f;    // seconds the litres a kilometre are smoothed over
static const float kRangeMax = 999.0f;      // kilometres: the readout's most
enum { kRangeMarks = 64 };                  // the window's marks: kRangeWindow / kRangeMark and a few to spare

struct RangeModel
{
    bool running;        // the model has a truck
    float fuel, tank;    // litres at the last step, and the tank the model belongs to
    double way, used;    // metres driven and litres burned while driving, since the model began
    struct { double way, used; } mark[kRangeMarks]; // the way and the litres every kRangeMark metres, oldest first
    int marks;
    float perKm;         // litres a kilometre over the window, smoothed; 0 = none yet
    float rangeKm;       // below 0 = not known yet
    float standing;      // litres a minute burned while standing, smoothed, for the probe
};

inline void RangeReset(RangeModel &m, const TruckRaw &t)
{
    m = RangeModel{};
    m.running = true;
    m.fuel = t.fuel;
    m.tank = t.tank;
    m.marks = 1; // the start: no way, no litres
    m.rangeKm = -1.0f;
}

// One step of dt seconds. A truck with another tank is another truck: the model starts over.
inline void RangeStep(RangeModel &m, const TruckRaw &t, float dt)
{
    if (!m.running || fabsf(t.tank - m.tank) > 0.01f) RangeReset(m, t);
    const float burned = m.fuel - t.fuel; // below 0: the tank was filled
    m.fuel = t.fuel;
    const float speed = fabsf(t.speed);
    if (speed >= kRangeMoving)
    {
        m.way += speed * dt;
        if (burned > 0.0f && burned <= kRangeGulp) m.used += burned;
        if (m.way - m.mark[m.marks - 1].way >= kRangeMark)
        {
            // a new mark; the oldest go once the one after them still spans the window
            if (m.marks == kRangeMarks) { memmove(m.mark, m.mark + 1, sizeof m.mark[0] * (kRangeMarks - 1)); m.marks--; }
            m.mark[m.marks].way = m.way;
            m.mark[m.marks].used = m.used;
            m.marks++;
            int drop = 0;
            while (drop + 2 < m.marks && m.way - m.mark[drop + 1].way >= kRangeWindow) drop++;
            if (drop) { memmove(m.mark, m.mark + drop, sizeof m.mark[0] * (size_t)(m.marks - drop)); m.marks -= drop; }
        }
    }
    else if (dt > 0.0f && burned >= 0.0f && burned <= kRangeGulp)
        m.standing += (burned / dt * 60.0f - m.standing) * (1.0f - expf(-dt / kRangeSmooth));
    const double span = m.way - m.mark[0].way;
    if (span >= kRangeFirst)
    {
        const float perKm = (float)((m.used - m.mark[0].used) / span * 1000.0);
        m.perKm = m.perKm > 0.0f ? m.perKm + (perKm - m.perKm) * (1.0f - expf(-dt / kRangeSmooth)) : perKm;
    }
    if (m.perKm > 0.0f) m.rangeKm = fminf(t.fuel / m.perKm, kRangeMax);
    else if (span >= kRangeFirst) m.rangeKm = kRangeMax; // driven that far without burning anything
}

// ---- the tilt ----

static const float kTiltSmooth = 0.3f; // seconds the chassis axes are smoothed over, so a bump does not light the lamp
static const float kTiltBack = 2.0f;   // degrees below an angle of the settings at which the lamp goes back, so it does not flicker at the angle
static const float kDegrees = 57.29578f;

// The angle in degrees whose sine is the share y of an axis that points up, and beyond 90 for a truck lying on its
// roof (over: its up axis points down).
inline float TiltAngle(float y, bool over)
{
    const float a = asinf(SettingsRange(y, -1.0f, 1.0f)) * kDegrees;
    return !over ? a : a < 0.0f ? -180.0f - a : 180.0f - a;
}

struct TiltModel
{
    bool running;         // the model has a truck: the next step smooths from the axes it holds
    float forward, up, left;  // how far each chassis axis points up (its y), smoothed
    float pitchDeg;       // above 0: the nose points up the slope
    float rollDeg;        // above 0: the truck leans to its right; beyond 90 either way it lies on its roof
    int lamp;             // 0 level, 1 warning, 2 danger, below 0 not known
};

// The lamp for a lean to the side of roll degrees and a slope of pitch degrees, with back degrees taken off the
// settings' angles: amber from RollWarnDeg, red from RollDangerDeg, and red from PitchDangerDeg up or down a slope.
inline int TiltLamp(float roll, float pitch, const GaugeSettings &s, float back)
{
    return roll >= s.rollDangerDeg - back || pitch >= s.pitchDangerDeg - back ? 2 : roll >= s.rollWarnDeg - back ? 1 : 0;
}

// One step of dt seconds. The first step on a truck takes the axes as they are. The lamp goes up at the settings'
// angles and back down kTiltBack degrees below them.
inline void TiltStep(TiltModel &m, const TruckRaw &t, const GaugeSettings &s, float dt)
{
    if (!isfinite(t.forward[1]) || !isfinite(t.up[1]) || !isfinite(t.left[1]))
    {
        m = TiltModel{};
        m.lamp = -1;
        return;
    }
    const float follow = m.running ? 1.0f - expf(-dt / kTiltSmooth) : 1.0f;
    m.forward += (t.forward[1] - m.forward) * follow;
    m.up += (t.up[1] - m.up) * follow;
    m.left += (t.left[1] - m.left) * follow;
    m.running = true;
    m.pitchDeg = TiltAngle(m.forward, false);
    m.rollDeg = TiltAngle(m.left, m.up < 0.0f); // the left side higher than the right: leaning right
    const float roll = fabsf(m.rollDeg), pitch = fabsf(m.pitchDeg);
    const int now = TiltLamp(roll, pitch, s, 0.0f), held = TiltLamp(roll, pitch, s, kTiltBack);
    if (m.lamp == 2 && held == 2) m.lamp = 2;                               // red stays until the truck is well out of it
    else if (m.lamp >= 1 && now == 0 && roll >= s.rollWarnDeg - kTiltBack) m.lamp = 1; // and amber likewise
    else m.lamp = now;
}

// The tilt in a line of text for the probe log, with the axes as read.
inline void TiltProbeText(const TruckRaw &t, const TiltModel &m, char *out, size_t size)
{
    static const char *lamps[4] = { "dark", "green", "amber", "red" };
    sprintf_s(out, size, "tilt: nose %s %.1f deg, leaning %s %.1f deg, lamp %s | y of the chassis axes as read: forward %.3f, up %.3f, left %.3f",
              m.pitchDeg < 0.0f ? "down" : "up", fabsf(m.pitchDeg), m.rollDeg < 0.0f ? "left" : "right", fabsf(m.rollDeg), lamps[m.lamp < 0 || m.lamp > 2 ? 0 : m.lamp + 1],
              t.forward[1], t.up[1], t.left[1]);
}

// ---- the coolant temperature ----

// The temperatures the settings' three times are measured between (kTempOutside, kTempWarm, kTempHot, kTempRed,
// kTempCooled) and the cold lamp's (kTempCold) are in gauges.h.
static const float kTempMost = 140.0f;       // the model stops here; the dial ends at 130
static const float kTempCruise = 97.0f;      // where an engine at full load settles with all the air the radiator can get, at kTempOutside outside
static const float kThermostatShut = 82.0f, kThermostatOpen = 92.0f; // the thermostat opens between these
static const float kThermostatLeak = 0.15f;  // the share of the radiator a shut thermostat still lets the coolant through
static const float kAirStill = 0.35f;        // the air through the radiator of a standing truck, as a share of all it can get from driving
static const float kAirFullKmh = 50.0f;      // the speed from which it gets all of it
static const float kFan = 0.3f, kFanFrom = 98.0f, kFanFull = 102.0f; // the fan adds this share on top; it comes in between these temperatures
static const float kHeatIdle = 0.15f;        // the heat of an idling engine as a share of its heat at full load
static const float kLoadRevs = 0.6f;         // how much of the load goes with the engine speed: load = pedal x (1 - kLoadRevs + kLoadRevs x revs)
static const float kLoadNeutral = 0.25f;     // in neutral nothing pulls against the engine: the load is this share of that
static const float kUsualLoad = 0.7f, kUsualKmh = 15.0f; // "driven as usual", for WarmUpMinutes: about the mean load and speed of two recorded drives
static const float kTempFall = 1.0f;         // degrees a second a running engine's coolant falls for each share of a full load's heat taken away beyond what the engine makes
enum { kTempTrucks = 16 };                   // the trucks the model remembers; the one left longest ago makes room for a new one

// How far the thermostat is open: kThermostatLeak while it is shut, 1 when it is open.
inline float TempThermostat(float temp)
{
    return kThermostatLeak + (1.0f - kThermostatLeak) * SettingsRange((temp - kThermostatShut) / (kThermostatOpen - kThermostatShut), 0.0f, 1.0f);
}

// How hard the fan runs, 0 to 1.
inline float TempFan(float temp) { return SettingsRange((temp - kFanFrom) / (kFanFull - kFanFrom), 0.0f, 1.0f); }

// The air through the radiator at a speed in km/h, as a share of all it can get from driving, with the fan's share
// on top.
inline float TempAirflow(float temp, float kmh)
{
    return kAirStill + (1.0f - kAirStill) * SettingsRange(kmh / kAirFullKmh, 0.0f, 1.0f) + kFan * TempFan(temp);
}

// How hard the engine works, 0 to 1: the pedal, and more of it the higher the tachometer's needle stands between
// idle and the red zone (revs, 0 to 1).
inline float TruckLoad(const TruckRaw &t, float revs)
{
    const float needle = revs > 0.0f ? fminf(revs, 1.0f) : 0.0f; // 0 too for revs that are no number
    const float load = TruckPedal(t) * (1.0f - kLoadRevs + kLoadRevs * needle);
    return t.gearEngaged == 0 ? load * kLoadNeutral : load;
}

// The heat the engine makes at a load, as a share of its heat at full load.
inline float TempHeat(float load) { return kHeatIdle + (1.0f - kHeatIdle) * SettingsRange(load, 0.0f, 1.0f); }

// The heat the radiator takes away, in the same measure: 1 is all of a full load's heat, which it takes from coolant
// at kTempCruise with the thermostat open, all the air and kTempOutside outside.
inline float TempCooling(float temp, float outside, float kmh)
{
    return (temp - outside) / (kTempCruise - kTempOutside) * TempThermostat(temp) * TempAirflow(temp, kmh);
}

// How fast the temperature rises, and how fast a stopped engine cools. warm and hot: degrees a second for each share
// of a full load's heat that the radiator does not take away, up to kTempWarm and from kTempHot (a blend of the two
// between them). off: the share of its warmth over the outside air that a stopped engine loses a second. The way
// down with the engine running has one rate whatever the settings (kTempFall), so a longer time to overheat does not
// make a hot engine slower to come back.
struct TempRates { float warm, hot, off; };

// The seconds the coolant takes from one temperature up to another at a rate of 1, at a load and a speed with
// kTempOutside outside: each quarter of a degree over the heat that is left over at it.
inline float TempPath(float from, float to, float load, float kmh)
{
    const float step = 0.25f;
    float seconds = 0.0f;
    for (float temp = from + step * 0.5f; temp < to; temp += step)
        seconds += step / fmaxf(TempHeat(load) - TempCooling(temp, kTempOutside, kmh), 0.001f);
    return seconds;
}

// The rates that make the settings' three times come true. The warm-up and the overheating each have a rate of
// their own, so each setting moves its own time and leaves the other two alone. None of them moves the
// temperatures the engine settles at: those come from the constants above and the outside temperature.
inline TempRates TempRatesOf(const GaugeSettings &s)
{
    static const float warmPath = TempPath(kTempOutside, kTempWarm, kUsualLoad, kUsualKmh), hotPath = TempPath(kTempHot, kTempRed, 1.0f, 0.0f);
    TempRates r;
    r.warm = warmPath / (fmaxf(s.warmUpMinutes, kWarmUpRange[0]) * 60.0f);
    r.hot = hotPath / (fmaxf(s.overheatMinutes, kOverheatRange[0]) * 60.0f);
    r.off = logf((kTempHot - kTempOutside) / (kTempCooled - kTempOutside)) / (fmaxf(s.coolDownMinutes, kCoolDownRange[0]) * 60.0f);
    return r;
}

struct TempModel
{
    // A truck the model has driven: the game's object for it, its build (the tank's size, the number of wheels and
    // of the gearbox's entries), its coolant temperature, and the model's clock when it was last driven. id 0 is a
    // free place.
    struct Truck { uint64_t id; float tank; int wheels, gears; float temp; double left; } truck[kTempTrucks];
    bool known;      // truck[at] is the truck being driven
    int at;
    double clock;    // seconds the model has been stepped for, whichever truck was driven
    float temp;      // the driven truck's coolant temperature in C
    float load, heat, cooling, perSecond; // the last step, for the probe: the load, the heat made and taken away, degrees a second
};

// Whether a remembered truck has the build of the truck being read: the same tank and wheels, and the same gearbox
// where both were read.
inline bool TempSameBuild(const TempModel::Truck &k, const TruckRaw &t)
{
    return k.id && fabsf(k.tank - t.tank) <= 0.01f && k.wheels == t.wheels && (k.gears == t.gearSpeeds || !k.gears || !t.gearSpeeds);
}

// One step of dt seconds for the truck the game holds in the object id; revs is TachShare. A truck driven for the
// first time is as cold as the air outside. One driven before has cooled as a stopped engine does for as long as
// other trucks were driven; the time the game spends in its menus does not count. The game makes a new object for
// a truck it puts back into the world or takes out of the garage: a new object with the build of a truck driven
// before is that truck (the one left last, when there are several), and is as warm as it was left, less the
// cooling. The same object with another tank is another truck that the game made in the old one's place.
inline void TempStep(TempModel &m, uint64_t id, const TruckRaw &t, float revs, const GaugeSettings &s, float dt)
{
    const TempRates r = TempRatesOf(s);
    if (!m.known || m.truck[m.at].id != id || fabsf(m.truck[m.at].tank - t.tank) > 0.01f)
    {
        int to = -1, like = -1, room = -1;
        for (int i = 0; i < kTempTrucks; i++)
        {
            const TempModel::Truck &k = m.truck[i];
            if (k.id == id && fabsf(k.tank - t.tank) <= 0.01f) to = i;
            if (TempSameBuild(k, t) && (like < 0 || k.left > m.truck[like].left)) like = i;
            if (m.known && i == m.at) continue;
            if (room < 0 || (m.truck[room].id && (!k.id || k.left < m.truck[room].left))) room = i;
        }
        if (to < 0 && like >= 0)
        {
            to = like;
            m.truck[to].id = id;
        }
        if (to >= 0) m.truck[to].temp = s.ambientC + (m.truck[to].temp - s.ambientC) * expf(-r.off * (float)(m.clock - m.truck[to].left));
        else
        {
            to = room;
            m.truck[to] = { id, t.tank, t.wheels, t.gearSpeeds, s.ambientC, m.clock };
        }
        m.at = to;
        m.known = true;
    }
    TempModel::Truck &k = m.truck[m.at];
    m.clock += dt;
    k.left = m.clock;
    k.wheels = t.wheels;
    if (t.gearSpeeds) k.gears = t.gearSpeeds;
    const float was = k.temp;
    if (TruckEngineOn(t))
    {
        m.load = TruckLoad(t, revs);
        m.heat = TempHeat(m.load);
        m.cooling = TempCooling(k.temp, s.ambientC, fabsf(t.speed) * 3.6f);
        const float rate = m.heat < m.cooling ? kTempFall : r.warm + (r.hot - r.warm) * SettingsRange((k.temp - kTempWarm) / (kTempHot - kTempWarm), 0.0f, 1.0f);
        k.temp += rate * (m.heat - m.cooling) * dt;
    }
    else
    {
        m.load = m.heat = m.cooling = 0.0f;
        k.temp = s.ambientC + (k.temp - s.ambientC) * expf(-r.off * dt);
    }
    if (!isfinite(k.temp)) k.temp = isfinite(was) ? was : s.ambientC; // a step that gave no number leaves the temperature as it was
    if (k.temp > kTempMost) k.temp = kTempMost;
    m.perSecond = dt > 0.0f ? (k.temp - was) / dt : 0.0f;
    m.temp = k.temp;
}

// The cold lamp: on while the driven truck's coolant is below kTempCold. The temperature moves too slowly for the
// lamp to flicker at it.
inline bool TempCold(const TempModel &m) { return m.known && m.temp < kTempCold; }

// Where the temperature stands: 0 the cold lamp is on, 1 between, 2 in the red zone; below 0 not known.
inline int TempZone(const TempModel &m) { return !m.known ? -1 : m.temp < kTempCold ? 0 : m.temp < kTempRed ? 1 : 2; }

// The coolant in a line of text for the probe log, with what the last step made of the truck.
inline void TempProbeText(const TruckRaw &t, const TempModel &m, const GaugeSettings &s, char *out, size_t size)
{
    if (!m.known)
    {
        sprintf_s(out, size, "coolant not known yet: no truck was driven since the game started | %.0f C outside", s.ambientC);
        return;
    }
    const float kmh = fabsf(t.speed) * 3.6f;
    sprintf_s(out, size, "coolant %.1f C, %+.2f C/s, cold lamp %s%s | heat %.2f of a full load's (load %.2f), %.2f taken away (thermostat %.2f, air %.2f at %.1f km/h, fan %.2f) | %.0f C outside",
              m.temp, m.perSecond, TempCold(m) ? "on" : "off", TempZone(m) == 2 ? ", in the red zone" : "", m.heat, m.load, m.cooling, TempThermostat(m.temp),
              TempAirflow(m.temp, kmh) - kFan * TempFan(m.temp), kmh, TempFan(m.temp), s.ambientC);
}

// The models' numbers in a line of text for the probe log.
inline void ModelProbeText(const TruckRaw &t, const TachModel &tach, const RangeModel &range, const GaugeScale &k, char *out, size_t size)
{
    char km[32] = "none yet";
    if (range.rangeKm >= 0.0f) sprintf_s(km, "%.1f km", range.rangeKm);
    sprintf_s(out, size,
              "engine speed %.0f rpm, heading for %.0f (pedal %.2f, wheels %.2f rad/s of the gear's %.2f) | range %s: %.3f l/km over %.0f m, %.2f l burned on %.0f m driven | "
              "burn %.2f l/min by the game's rule, %.2f l/min measured standing",
              tach.rpm, TachTarget(t, k), TruckPedal(t), TruckDrivenSpin(t), TruckGearTop(t, t.gearEngaged), km, range.perKm, range.way - range.mark[0].way, range.used,
              range.way, TruckFuelRate(t), range.standing);
}
