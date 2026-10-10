// models.h: what the cluster shows that the game does not hold: the engine speed, the fuel range, the tilt, the
// coolant temperature and the boost, worked out from the truck's values (truck.h) on every pass of the mod's thread, and what the
// temperature costs the engine. No game memory is touched here, so the offline test (test\models_test.cpp) runs the
// models on drives it makes up.
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
// The coolant temperature. The game has no engine heat. Here the engine makes heat by the fuel it burns: the pedal
// and the engine speed together, as a real engine's fuel goes with its torque and its speed. Lugging at low revs
// makes little heat, and so does coasting at high revs with the foot off, where a diesel burns next to nothing; a
// small share of the heat comes at idle, and less in neutral, where nothing pulls against the engine. The radiator
// takes heat away by how much warmer the coolant is than the air outside, by how far the thermostat is open (shut
// below 82 C, open from 92 C) and by the air that goes through it: more the faster the truck drives, and a fan helps
// from about 100 C. Water around the truck's front cools as well, from about axle height and most with the engine
// standing in it. So a warm engine holds about 86 C at idle, stands near 100 C when it works hard at speed, and climbs
// into the red zone from 118 C only when it works hard at a crawl. A stopped engine cools slowly towards the air
// outside. Three of the settings are the times all of this takes, a fourth is the outside temperature, and a fifth
// the speed from which the radiator gets all its air. Each truck keeps its own temperature.
//
// The oil temperature. The coolant takes most of its heat from the cylinders; the oil takes the heat of the friction
// in the bearings and the pistons, which grows with the engine speed, and of the jets that cool the pistons from
// below, which grows with the load, and gives it to the coolant through the oil cooler. So the oil heads for the
// coolant's temperature and a few degrees over it (about 7 at cruise, 14 at full load at the red zone), and follows
// slowly: it is warm after the coolant and keeps its heat longer. Its warm-up time is a setting, at least a minute
// longer than the coolant's. From the settings' warning temperature the oil lamp blinks and a warning sounds every
// few seconds.
//
// The engine's wear. The game has engine damage and nothing that makes heat cost any. Here an engine in the red zone
// loses a share of what it can take every minute, more the hotter it runs, and so does a cold engine worked hard (more
// than half the fuel it burns floored at the red zone), more the colder it is. The model counts that in points of the game's own engine damage, and gauges.cpp
// writes the whole points into the game. So a garage repairs them, and the game's rules for a damaged engine apply.
//
// The boost. The game has no boost pressure. Here the turbo blows by how far the pedal is down and how fast the
// engine turns: nothing near idle, all it can from about two thirds of the way to the red zone. The pressure lags
// behind, longer at low engine speed, and falls away quickly when the foot comes off. How much a truck's turbo
// reaches is a setting, by truck class or the truck's own, and so is how it gets there: four profiles move where the
// boost comes in and how long it lags (a small quick turbo, a big slow one, two turbos one after the other). A quick lift of the pedal under boost is told to the
// caller as a flutter or a blow-off, for the sounds (LiftSoundOf: how loud and how fast each is played).
//
// The powertrain. What the engine would deliver if the mod drove it, as a real engine does: the pedal is shaped (a
// progressive curve, and a ramp so that a keyboard's pedal, which is either up or floored, comes in over a moment),
// and the torque comes from the engine speed by a torque curve (a diesel's broad plateau for trucks, a later peak
// for scouts, nothing past the governor), with less of it while the oil is cold and more with the turbo's boost. The
// power is that torque times the shaped pedal, 1 being the engine floored at its peak, warm and at full boost. The
// mod does not hand any of it to the game yet: that waits for the hooks (module 12), and until then the probe log
// says what it would have done.
//
// The automatic gearbox. What a truck's automatic with a torque converter would do with the driven wheels' speed and
// the shaped pedal: the converter lets the engine run up to its stall speed against a standing truck and multiplies
// the torque (twice at the stall, none from the coupling point), lets an idling truck creep, and locks up from the
// second gear once the engine and the wheels turn together. It shifts up later the further the pedal is down, down
// well below that so a gear holds, kicks down on a floored pedal, holds its gear rolling downhill with the foot off,
// and lets a second pass between two shifts. A lifted foot is no call to shift up at once, and wheelspin shifts
// nothing. On the user's 20 minute drive it shifts as often as the game's own automatic, which hunts the same. Like the
// powertrain it is worked out and logged, not handed to the game.
//
// The clutch. With a manual gearbox and a clutch pedal (a wheel's, through the manual transmission mod) the engine
// runs behind the clutch: pressed, the engine is free and its governor holds the speed the pedal asks for; let up, it
// turns with the gearbox; in the bite it slips, the engine pulled towards the gearbox's speed by what the clutch
// holds. Let up too fast with the truck standing, or in too high a gear at a crawl, it drags the engine under half its
// idle speed and the engine stalls, and it stays dead until the clutch is pressed or the gearbox is in neutral. Without
// a pedal (a clutch button, or none) the clutch works by itself: let go at idle, taking hold as the engine speeds up,
// and it never stalls the engine. Worked out and logged the same way, while the manual transmission mod drives.
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

// How far the driven wheels' speeds lie apart, the fastest less the slowest as a share of their mean; 0 for one wheel
// or none turning.
inline float TruckDrivenSpread(const TruckRaw &t)
{
    float lo = 0.0f, hi = 0.0f, sum = 0.0f;
    int n = 0;
    for (int i = 0; i < t.wheels; i++)
    {
        if (!(t.drive[i] == 1 || (t.drive[i] == 2 && t.awd))) continue;
        const float s = fabsf(t.spin[i]);
        lo = n ? fminf(lo, s) : s;
        hi = n ? fmaxf(hi, s) : s;
        sum += s;
        n++;
    }
    return n > 1 && sum > 0.0f ? (hi - lo) / (sum / n) : 0.0f;
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

// One step of the needle towards an engine speed worked out elsewhere (the converter of the mod's automatic, while it
// drives the game's gears), with the needle's own lag; starting, stalling and stopped as TachTarget has them.
inline void TachFollow(TachModel &m, const TruckRaw &t, const GaugeScale &k, float rpm, float dt)
{
    float target = !TruckEngineOn(t) ? 0.0f : t.startTimer > 0.0f && !(t.flags & 2) ? TachTarget(t, k) : rpm;
    const float alive = SettingsRange(1.0f - kStallRate * t.stall, 0.0f, 1.0f);
    target *= alive * alive;
    m.gear = t.gearEngaged;
    m.dip = 0.0f;
    if (!m.running || !(target >= 0.0f))
    {
        m.rpm = target >= 0.0f ? target : 0.0f;
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

// The engine speed as a share of where the red zone starts, 0 to 1: what the fuel it burns goes with.
inline float TachSpeed(const TachModel &m, const GaugeScale &k)
{
    const float speed = k.redline > 0.0f ? m.rpm / k.redline : 0.0f;
    return speed > 0.0f ? fminf(speed, 1.0f) : 0.0f; // 0 too for a needle that is no number
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
    uint64_t id;         // the kind of truck the model belongs to (TruckKeyNumber)
    float fuel, tank;    // litres at the last step, and the tank the model belongs to
    double way, used;    // metres driven and litres burned while driving, since the model began
    struct { double way, used; } mark[kRangeMarks]; // the way and the litres every kRangeMark metres, oldest first
    int marks;
    float perKm;         // litres a kilometre over the window, smoothed; 0 = none yet
    float rangeKm;       // below 0 = not known yet
    float standing;      // litres a minute burned while standing, smoothed, for the probe
};

inline void RangeReset(RangeModel &m, uint64_t id, const TruckRaw &t)
{
    m = RangeModel{};
    m.running = true;
    m.id = id;
    m.fuel = t.fuel;
    m.tank = t.tank;
    m.marks = 1; // the start: no way, no litres
    m.rangeKm = -1.0f;
}

// One step of dt seconds for a truck of the kind id (TruckKeyNumber). A truck of another kind, or with another
// tank, is another truck: the model starts over. A truck the game puts back into the world, or another truck of
// the kind, goes on with the litres a kilometre the model has.
inline void RangeStep(RangeModel &m, uint64_t id, const TruckRaw &t, float dt)
{
    if (!m.running || m.id != id || fabsf(t.tank - m.tank) > 0.01f) RangeReset(m, id, t);
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
static const float kFan = 0.3f, kFanFrom = 98.0f, kFanFull = 102.0f; // the fan adds this share on top; it comes in between these temperatures
static const float kHeatIdle = 0.15f;        // the heat of an idling engine as a share of its heat at full load
static const float kLoadNeutral = 0.25f;     // in neutral nothing pulls against the engine: the load is this share of what it would be in gear
static const float kUsualLoad = 0.5f, kUsualKmh = 13.0f; // "driven as usual", for WarmUpMinutes: the mean load and speed while driving on the user's drive of 2026-10-08 (0.49 at 13.1 km/h)
static const float kTempFall = 1.0f;         // degrees a second a running engine's coolant falls for each share of a full load's heat taken away beyond what the engine makes
static const float kWaterFrom = 0.5f, kWaterFull = 1.2f; // metres of water at the truck's front: from about axle height it cools the engine, and with the engine in it it does all it can
static const float kWater = 1.0f;            // what deep water adds to the air through the radiator, as a share of all the air that driving gives
static const float kWaterStopped = 4.0f;     // a stopped engine in deep water loses its warmth this many times faster on top
static const float kOilFriction = 4.0f;      // degrees the oil heads for over the coolant from the friction at the red zone's engine speed (it goes with the square of the speed)
static const float kOilPiston = 10.0f;       // and from cooling the pistons at full load (it goes with the load)
static const float kOilAfter = 60.0f;        // the oil is warm at least this many seconds after the coolant, whatever OilWarmUpMinutes says
static const float kOilCooling = 0.5f;       // a stopped engine's oil loses its warmth at this share of the coolant's rate
enum { kTempTrucks = 16 };                   // the trucks the model remembers; the one left longest ago makes room for a new one

// How far the thermostat is open: kThermostatLeak while it is shut, 1 when it is open.
inline float TempThermostat(float temp)
{
    return kThermostatLeak + (1.0f - kThermostatLeak) * SettingsRange((temp - kThermostatShut) / (kThermostatOpen - kThermostatShut), 0.0f, 1.0f);
}

// How hard the fan runs, 0 to 1.
inline float TempFan(float temp) { return SettingsRange((temp - kFanFrom) / (kFanFull - kFanFrom), 0.0f, 1.0f); }

// The air through the radiator at a speed in km/h, as a share of all it can get from driving, with the fan's share
// on top. It gets all of it from fullKmh on (the settings' AirFullKmh).
inline float TempAirflow(float temp, float kmh, float fullKmh)
{
    return kAirStill + (1.0f - kAirStill) * SettingsRange(kmh / fmaxf(fullKmh, kAirFullRange[0]), 0.0f, 1.0f) + kFan * TempFan(temp);
}

// The depth of the water at the truck's front in metres, where nearly every truck has its engine: the mean over the
// wheels that stand furthest forward (within half a metre of the foremost). 0 on dry ground, and when the wheels'
// places and depths were not read.
inline float TruckWaterDepth(const TruckRaw &t)
{
    if (!t.waterKnown || t.wheels <= 0) return 0.0f;
    float along[kMaxWheels], foremost = -1.0e9f, sum = 0.0f;
    int n = 0;
    for (int i = 0; i < t.wheels; i++)
    {
        along[i] = (t.wheelAt[i][0] - t.position[0]) * t.forward[0] + (t.wheelAt[i][1] - t.position[1]) * t.forward[1] + (t.wheelAt[i][2] - t.position[2]) * t.forward[2];
        if (along[i] > foremost) foremost = along[i];
    }
    for (int i = 0; i < t.wheels; i++)
        if (along[i] >= foremost - 0.5f) { sum += t.wheelWater[i]; n++; }
    return n ? sum / (float)n : 0.0f; // 0 too for places that are no numbers
}

// How much of its cooling the water does at a depth: none up to kWaterFrom, all of it from kWaterFull.
inline float TempWater(float depth) { return SettingsRange((depth - kWaterFrom) / (kWaterFull - kWaterFrom), 0.0f, 1.0f); }

// How hard the engine works, 0 to 1: the fuel it burns as a share of what it burns floored at the red zone, the pedal
// times the engine speed (speed: TachSpeed, a share of the red zone's), and kLoadNeutral of that in neutral. With the
// foot off a diesel burns next to nothing, whatever the engine speed.
inline float TruckLoad(const TruckRaw &t, float speed)
{
    const float engine = speed > 0.0f ? fminf(speed, 1.0f) : 0.0f; // 0 too for a speed that is no number
    const float load = TruckPedal(t) * engine;
    return t.gearEngaged == 0 ? load * kLoadNeutral : load;
}

// Degrees the oil heads for over the coolant at a load and an engine speed (TachSpeed): the friction's share, which
// goes with the square of the speed, and the pistons', which goes with the load.
inline float OilOver(float load, float speed)
{
    const float engine = speed > 0.0f ? fminf(speed, 1.0f) : 0.0f;
    return kOilFriction * engine * engine + kOilPiston * SettingsRange(load, 0.0f, 1.0f);
}

// The heat the engine makes at a load, as a share of its heat at full load.
inline float TempHeat(float load) { return kHeatIdle + (1.0f - kHeatIdle) * SettingsRange(load, 0.0f, 1.0f); }

// The heat the radiator takes away, in the same measure: 1 is all of a full load's heat, which it takes from coolant
// at kTempCruise with the thermostat open, all the air and kTempOutside outside.
// water: how much of its cooling the water around the truck does, 0 to 1 (TempWater). It counts like air through
// the radiator, kWater times as much as all the air that driving gives.
inline float TempCooling(float temp, float outside, float kmh, float fullKmh, float water)
{
    return (temp - outside) / (kTempCruise - kTempOutside) * TempThermostat(temp) * (TempAirflow(temp, kmh, fullKmh) + kWater * water);
}

// How fast the temperature rises, and how fast a stopped engine cools. warm and hot: degrees a second for each share
// of a full load's heat that the radiator does not take away, up to kTempWarm and from kTempHot (a blend of the two
// between them). off: the share of its warmth over the outside air that a stopped engine loses a second. The way
// down with the engine running has one rate whatever the settings (kTempFall), so a longer time to overheat does not
// make a hot engine slower to come back.
struct TempRates { float warm, hot, off; };

// The seconds the coolant takes from one temperature up to another at a rate of 1, at a load and a speed with
// kTempOutside outside and the radiator's full airflow from fullKmh: each quarter of a degree over the heat that is
// left over at it.
inline float TempPath(float from, float to, float load, float kmh, float fullKmh)
{
    const float step = 0.25f;
    float seconds = 0.0f;
    for (float temp = from + step * 0.5f; temp < to; temp += step)
        seconds += step / fmaxf(TempHeat(load) - TempCooling(temp, kTempOutside, kmh, fullKmh, 0.0f), 0.001f);
    return seconds;
}

// The rates that make the settings' three times come true. The warm-up and the overheating each have a rate of
// their own, so each setting moves its own time and leaves the other two alone. None of them moves the
// temperatures the engine settles at: those come from the constants above, the outside temperature and AirFullKmh.
// The overheating is measured at a standstill, where the truck's speed brings no air. The warm-up is measured at
// kUsualKmh, so its way is worked out again when AirFullKmh changes, and WarmUpMinutes stays true with any of it.
inline TempRates TempRatesOf(const GaugeSettings &s)
{
    static const float hotPath = TempPath(kTempHot, kTempRed, 1.0f, 0.0f, kAirFullRange[0]);
    static float warmFull = -1.0f, warmPath = 0.0f; // the warm-up's way, and the AirFullKmh it was worked out for (none yet)
    if (s.airFullKmh != warmFull)
    {
        warmPath = TempPath(kTempOutside, kTempWarm, kUsualLoad, kUsualKmh, s.airFullKmh);
        warmFull = s.airFullKmh;
    }
    TempRates r;
    r.warm = warmPath / (fmaxf(s.warmUpMinutes, kWarmUpRange[0]) * 60.0f);
    r.hot = hotPath / (fmaxf(s.overheatMinutes, kOverheatRange[0]) * 60.0f);
    r.off = logf((kTempHot - kTempOutside) / (kTempCooled - kTempOutside)) / (fmaxf(s.coolDownMinutes, kCoolDownRange[0]) * 60.0f);
    return r;
}

// The seconds the oil lags the coolant by, so that driven as usual from kTempOutside it reaches kTempWarm after the
// settings' OilWarmUpMinutes, and at least kOilAfter after the coolant does. Worked out by running the coolant's
// warm-up with the oil behind it, and again only when one of the settings it depends on changes.
inline float OilLag(const GaugeSettings &s, const TempRates &r)
{
    static float warmUp = -1.0f, overheat = -1.0f, full = -1.0f, oilWarmUp = -1.0f, lag = 60.0f;
    if (s.warmUpMinutes == warmUp && s.overheatMinutes == overheat && s.airFullKmh == full && s.oilWarmUpMinutes == oilWarmUp) return lag;
    warmUp = s.warmUpMinutes;
    overheat = s.overheatMinutes;
    full = s.airFullKmh;
    oilWarmUp = s.oilWarmUpMinutes;
    const float target = fmaxf(fmaxf(s.oilWarmUpMinutes, kOilWarmUpRange[0]) * 60.0f, fmaxf(s.warmUpMinutes, kWarmUpRange[0]) * 60.0f + kOilAfter);
    const float step = 0.25f, heat = TempHeat(kUsualLoad), over = OilOver(kUsualLoad, kUsualLoad);
    // the seconds the oil takes to kTempWarm with a lag of tau; longer than limit counts as never
    auto time = [&](float tau, float limit) {
        float temp = kTempOutside, oil = kTempOutside;
        for (float at = step; at <= limit; at += step)
        {
            const float cooling = TempCooling(temp, kTempOutside, kUsualKmh, s.airFullKmh, 0.0f);
            const float rate = heat < cooling ? kTempFall : r.warm + (r.hot - r.warm) * SettingsRange((temp - kTempWarm) / (kTempHot - kTempWarm), 0.0f, 1.0f);
            temp += rate * (heat - cooling) * step;
            oil += (temp + over - oil) * (1.0f - expf(-step / tau));
            if (oil >= kTempWarm) return at;
        }
        return limit * 2.0f;
    };
    // a longer lag is a longer time: halve the way between the shortest and the longest lag until it fits
    float lo = 1.0f, hi = 3600.0f;
    for (int i = 0; i < 40; i++)
    {
        const float mid = sqrtf(lo * hi);
        (time(mid, target * 2.0f + 600.0f) < target ? lo : hi) = mid;
    }
    lag = sqrtf(lo * hi);
    return lag;
}

struct TempModel
{
    // A kind of truck the model has driven (TruckKeyNumber of its key), its coolant and oil temperatures, and the
    // model's clock when it was last driven. id 0 is a free place.
    struct Truck { uint64_t id; float temp, oil; double left; } truck[kTempTrucks];
    bool known;      // truck[at] is the truck being driven
    int at;
    double clock;    // seconds the model has been stepped for, whichever truck was driven
    float temp;      // the driven truck's coolant temperature in C
    float oil;       // and its oil temperature in C
    float load, heat, cooling, perSecond; // the last step, for the probe: the load, the heat made and taken away, degrees a second
    float depth, water;                   // and the metres of water at the truck's front, and how much of its cooling the water did (0 to 1)
    float oilTarget;                      // and where the oil heads for
};

// One step of dt seconds for a truck of the kind id (TruckKeyNumber of its key, never 0); speed is TachSpeed. A
// kind driven for the first time is as cold as the air outside. One driven before has cooled as a stopped engine
// does for as long as other trucks were driven; the time the game spends in its menus does not count. The kind is
// all the model knows a truck by: a truck the game puts back into the world or takes out of the garage again is as
// warm as it was left, less the cooling, and two trucks of one kind share their temperature. The oil goes with the
// coolant in all of it.
inline void TempStep(TempModel &m, uint64_t id, const TruckRaw &t, float speed, const GaugeSettings &s, float dt)
{
    const TempRates r = TempRatesOf(s);
    const float lag = OilLag(s, r);
    if (!m.known || m.truck[m.at].id != id)
    {
        int to = -1, room = -1;
        for (int i = 0; i < kTempTrucks; i++)
        {
            const TempModel::Truck &k = m.truck[i];
            if (k.id == id) to = i;
            if (m.known && i == m.at) continue;
            if (room < 0 || (m.truck[room].id && (!k.id || k.left < m.truck[room].left))) room = i;
        }
        if (to >= 0)
        {
            const float away = (float)(m.clock - m.truck[to].left);
            m.truck[to].temp = s.ambientC + (m.truck[to].temp - s.ambientC) * expf(-r.off * away);
            m.truck[to].oil = s.ambientC + (m.truck[to].oil - s.ambientC) * expf(-r.off * kOilCooling * away);
        }
        else
        {
            to = room;
            m.truck[to] = { id, s.ambientC, s.ambientC, m.clock };
        }
        m.at = to;
        m.known = true;
    }
    TempModel::Truck &k = m.truck[m.at];
    m.clock += dt;
    k.left = m.clock;
    const float was = k.temp, oilWas = k.oil;
    m.depth = TruckWaterDepth(t);
    m.water = TempWater(m.depth);
    if (TruckEngineOn(t))
    {
        m.load = TruckLoad(t, speed);
        m.heat = TempHeat(m.load);
        m.cooling = TempCooling(k.temp, s.ambientC, fabsf(t.speed) * 3.6f, s.airFullKmh, m.water);
        const float rate = m.heat < m.cooling ? kTempFall : r.warm + (r.hot - r.warm) * SettingsRange((k.temp - kTempWarm) / (kTempHot - kTempWarm), 0.0f, 1.0f);
        k.temp += rate * (m.heat - m.cooling) * dt;
        // the oil: towards the coolant and over it by the friction and the pistons' heat, slowly
        m.oilTarget = k.temp + OilOver(m.load, speed);
        k.oil += (m.oilTarget - k.oil) * (1.0f - expf(-dt / lag));
    }
    else
    {
        // a stopped engine: slowly towards the air outside, and faster the deeper it stands in water; the oil slower
        // than the coolant
        m.load = m.heat = m.cooling = 0.0f;
        const float off = r.off * (1.0f + kWaterStopped * m.water) * dt;
        k.temp = s.ambientC + (k.temp - s.ambientC) * expf(-off);
        k.oil = s.ambientC + (k.oil - s.ambientC) * expf(-off * kOilCooling);
        m.oilTarget = s.ambientC;
    }
    if (!isfinite(k.temp)) k.temp = isfinite(was) ? was : s.ambientC; // a step that gave no number leaves the temperature as it was
    if (!isfinite(k.oil)) k.oil = isfinite(oilWas) ? oilWas : s.ambientC;
    if (k.temp > kTempMost) k.temp = kTempMost;
    if (k.oil > kTempMost) k.oil = kTempMost;
    m.perSecond = dt > 0.0f ? (k.temp - was) / dt : 0.0f;
    m.temp = k.temp;
    m.oil = k.oil;
}

// The cold lamp: on while the driven truck's coolant is below kTempCold. The temperature moves too slowly for the
// lamp to flicker at it.
inline bool TempCold(const TempModel &m) { return m.known && m.temp < kTempCold; }

// Where the temperature stands: 0 the cold lamp is on, 1 between, 2 in the red zone; below 0 not known.
inline int TempZone(const TempModel &m) { return !m.known ? -1 : m.temp < kTempCold ? 0 : m.temp < kTempRed ? 1 : 2; }

// ---- the oil warning ----

static const float kOilBand = 2.0f;         // the warning ends this many degrees under the setting's temperature, so it does not flicker on and off
static const float kOilBeepEvery = 4.0f;    // seconds from one warning sound to the next while the oil stays too hot
static const int kOilBlinkMs = 400;         // the oil lamp's blink: this long lit, then this long dark

// on: the oil is too hot; wait: seconds until the next warning sound; beeps: how many have sounded since the model began.
struct OilWarning { bool on; float wait; int beeps; };

// One step of dt seconds. Returns true when the warning is to sound: at once when the running engine's oil reaches
// the settings' OilWarnC, and every kOilBeepEvery while it stays there. It ends kOilBand under OilWarnC, and with the
// engine stopped.
inline bool OilWarnStep(OilWarning &w, const TempModel &m, const TruckRaw &t, const GaugeSettings &s, float dt)
{
    const bool hot = m.known && TruckEngineOn(t) && m.oil >= (w.on ? s.oilWarnC - kOilBand : s.oilWarnC);
    if (!hot)
    {
        w.on = false;
        w.wait = 0.0f;
        return false;
    }
    w.on = true;
    w.wait -= dt;
    if (w.wait > 0.0f) return false;
    w.wait = kOilBeepEvery;
    w.beeps++;
    return true;
}

// The oil lamp at a time in milliseconds: it blinks while the warning is on, and is dark otherwise.
inline bool OilLamp(const OilWarning &w, unsigned long long ms) { return w.on && (ms / kOilBlinkMs) % 2 == 0; }

// How loud of 1 the oil warning sounds at the settings' SoundVolume (0 to 100): a little under the turbo at its loudest.
static const float kOilBeepLoud = 0.8f;
inline float OilBeepGain(int soundVolume) { return (float)(soundVolume < 0 ? 0 : soundVolume > 100 ? 100 : soundVolume) / 100.0f * kOilBeepLoud; }

// The coolant in a line of text for the probe log, with what the last step made of the truck.
inline void TempProbeText(const TruckRaw &t, const TempModel &m, const GaugeSettings &s, char *out, size_t size)
{
    if (!m.known)
    {
        sprintf_s(out, size, "coolant not known yet: no truck was driven since the game started | %.0f C outside", s.ambientC);
        return;
    }
    const float kmh = fabsf(t.speed) * 3.6f;
    sprintf_s(out, size, "coolant %.1f C, %+.2f C/s, cold lamp %s%s | oil %.1f C, heading for %.1f%s | heat %.2f of a full load's (load %.2f), %.2f taken away (thermostat %.2f, air %.2f at %.1f km/h, fan %.2f, water %.2f at %.2f m%s) | %.0f C outside",
              m.temp, m.perSecond, TempCold(m) ? "on" : "off", TempZone(m) == 2 ? ", in the red zone" : "", m.oil, m.oilTarget, m.oil >= s.oilWarnC ? ", too hot" : "",
              m.heat, m.load, m.cooling, TempThermostat(m.temp), TempAirflow(m.temp, kmh, s.airFullKmh) - kFan * TempFan(m.temp), kmh, TempFan(m.temp), m.water, m.depth,
              t.waterKnown ? "" : " (not read)", s.ambientC);
}

// ---- the boost ----

static const float kBoostRevsFrom = 0.2f, kBoostRevsFull = 0.65f; // the standard turbo starts to blow with the needle this far from idle to the red zone, and blows all it can from here
static const float kBoostPedal = 1.3f;       // the boost goes with the pedal to this power: little of it at a light foot
static const float kBoostSpool = 0.35f, kBoostSpoolLow = 0.9f; // seconds the standard boost lags by on the way up at the red zone, and this much more at idle
static const float kBoostFall = 0.18f, kBoostLift = 0.1f; // seconds on the way down, and with the foot off the pedal

// A turbo's character (the settings' BoostProfile). Its boost comes from one or two stages: each makes its share of
// the boost, starting with the needle at from of the way to the red zone and all of it from full, and lags by spool
// seconds at the red zone and spoolLow more at idle. Two stages are a small turbo early and a big one after it; the
// lag on the way up is the small one's until the boost has reached what the small one makes. fall and lift: seconds
// the boost takes on the way down, and with the foot off the pedal.
struct SpoolStage { float share, from, full, spool, spoolLow; };
struct SpoolProfile { SpoolStage stage[2]; float fall, lift; };
static const SpoolProfile kSpoolProfiles[kSpoolCount] = {
    { { { 1.0f, kBoostRevsFrom, kBoostRevsFull, kBoostSpool, kBoostSpoolLow }, {} }, kBoostFall, kBoostLift }, // standard
    { { { 1.0f, 0.08f, 0.45f, 0.18f, 0.35f }, {} }, 0.15f, 0.08f },                                         // small and quick: early and little lag
    { { { 1.0f, 0.4f, 0.8f, 0.45f, 2.2f }, {} }, 0.25f, 0.12f },                                             // big and slow: late, and long to come at low revs
    { { { 0.4f, 0.08f, 0.35f, 0.18f, 0.35f }, { 0.6f, 0.45f, 0.8f, 0.4f, 1.6f } }, 0.2f, 0.1f } };          // two stages: the small one early, the big one after it
inline const SpoolProfile &SpoolOf(int profile) { return kSpoolProfiles[profile < 0 || profile >= kSpoolCount ? kSpoolStandard : profile]; }

// The share of a stage's boost that the needle (0 to 1) brings.
inline float SpoolRamp(const SpoolStage &k, float needle) { return k.share <= 0.0f ? 0.0f : k.share * SettingsRange((needle - k.from) / (k.full - k.from), 0.0f, 1.0f); }
static const float kLiftWindow = 0.25f;      // seconds: a lift is the pedal coming up within this time
static const float kLiftForBlowOff = 0.5f, kLiftForFlutter = 0.25f; // how far the pedal has to come up for a blow-off, and for a flutter
static const float kBlowOffFrom = 0.22f, kFlutterFrom = 0.11f; // the boost it takes, as a share of the most the turbo reaches
static const float kLiftPause = 0.8f;        // seconds from one lift's sound to the next
enum { kLiftNone, kLiftFlutter, kLiftBlowOff }; // BoostStep's answer: nothing, a flutter, a blow-off
enum { kLiftMarks = 8 };                     // pedal positions kept for kLiftWindow: a few more than the mod's steps in it

struct BoostModel
{
    bool running;        // the model has a truck
    float bar;           // the boost in bar
    float target;        // where it heads, for the probe
    struct { float pedal, age; } mark[kLiftMarks]; // the pedal on the last steps, newest last, and how long ago
    int marks;
    bool lifting;        // the pedal came up on the last step
    float liftBar;       // the boost when it began to come up: a lift sounds by this, as the boost is falling while the pedal comes up
    float pause;         // seconds until a lift may sound again
    int flutters, blowOffs;  // how many of each since the model began
    int last;            // the last lift's sound (kLiftNone: none yet), the boost it came at, and how loud of 1
    float lastBar, lastVolume;
};

// The boost the turbo heads for, in bar: most (the truck's BoostMax) with the pedal floored and the tachometer's
// needle (revs, TachShare) where the profile's last stage blows all it can. Nothing below where its first stage
// starts, with the engine off or while it starts, and little in neutral, where nothing pulls against the engine.
// first: what the first stage makes of it.
inline float BoostTarget(const TruckRaw &t, float revs, float most, int profile, float *first = nullptr)
{
    if (first) *first = 0.0f;
    if (!TruckEngineOn(t) || (t.startTimer > 0.0f && !(t.flags & 2))) return 0.0f;
    const SpoolProfile &p = SpoolOf(profile);
    const float needle = revs > 0.0f ? fminf(revs, 1.0f) : 0.0f; // 0 too for revs that are no number
    const float foot = most * powf(TruckPedal(t), kBoostPedal) * (t.gearEngaged == 0 ? kLoadNeutral : 1.0f);
    if (first) *first = foot * SpoolRamp(p.stage[0], needle);
    return foot * (SpoolRamp(p.stage[0], needle) + SpoolRamp(p.stage[1], needle));
}

// One step of dt seconds. Returns what a lift of the pedal on this step sounds like: kLiftBlowOff as soon as the
// pedal has come up by kLiftForBlowOff within kLiftWindow with the boost at kBlowOffFrom of most or above when it
// began to; kLiftFlutter once the pedal has stopped after a smaller lift, or after one with less boost; kLiftNone
// otherwise and for kLiftPause after a sound. The model keeps the boost the sound came at and how loud it is
// (lastBar, lastVolume).
inline int BoostStep(BoostModel &m, const TruckRaw &t, float revs, float most, int profile, float dt)
{
    const float pedal = TruckPedal(t);
    if (!m.running)
    {
        m = BoostModel{};
        m.running = true;
    }
    // is the pedal coming up, and with how much boost did it begin to
    const bool lifting = m.marks > 0 && pedal < m.mark[m.marks - 1].pedal - 0.01f;
    if (lifting && !m.lifting) m.liftBar = m.bar;
    m.lifting = lifting;
    // how far the pedal came up within the window
    int kept = 0;
    float highest = pedal;
    for (int i = 0; i < m.marks; i++)
    {
        m.mark[i].age += dt;
        if (m.mark[i].age > kLiftWindow) continue;
        if (m.mark[i].pedal > highest) highest = m.mark[i].pedal;
        m.mark[kept++] = m.mark[i];
    }
    if (kept == kLiftMarks) { memmove(m.mark, m.mark + 1, sizeof m.mark[0] * (kLiftMarks - 1)); kept--; }
    m.mark[kept].pedal = pedal;
    m.mark[kept].age = 0.0f;
    m.marks = kept + 1;
    const float lift = highest - pedal;
    int sound = kLiftNone;
    if (m.pause > 0.0f) m.pause -= dt;
    else if (most > 0.0f && TruckEngineOn(t))
    {
        if (lift >= kLiftForBlowOff && m.liftBar >= kBlowOffFrom * most) sound = kLiftBlowOff;
        else if (!lifting && lift >= kLiftForFlutter && m.liftBar >= kFlutterFrom * most) sound = kLiftFlutter;
    }
    if (sound != kLiftNone)
    {
        m.last = sound;
        m.lastBar = m.liftBar;
        m.lastVolume = powf(SettingsRange(m.liftBar / most, 0.0f, 1.0f), 1.5f);
        (sound == kLiftBlowOff ? m.blowOffs : m.flutters)++;
        m.pause = kLiftPause;
        // the lift is told: the pedal's way up to here does not count again
        m.mark[0] = m.mark[m.marks - 1];
        m.marks = 1;
    }
    float first = 0.0f;
    m.target = BoostTarget(t, revs, most, profile, &first);
    const SpoolProfile &p = SpoolOf(profile);
    const float needle = revs > 0.0f ? fminf(revs, 1.0f) : 0.0f;
    const SpoolStage &building = p.stage[1].share > 0.0f && m.bar >= first ? p.stage[1] : p.stage[0];
    const float tau = m.target > m.bar ? building.spool + building.spoolLow * (1.0f - needle) : pedal < 0.05f ? p.lift : p.fall;
    m.bar += (m.target - m.bar) * (1.0f - expf(-dt / tau));
    if (!(m.bar >= 0.0f)) m.bar = 0.0f; // 0 too for a boost that is no number
    return sound;
}

// ---- the sound of a lift ----

static const float kLiftQuietest = 0.2f;     // how loud of 1 a lift with next to no boost is; one at full boost is 1
static const float kLiftPitchSpread = 0.06f; // no two lifts sound quite alike: each plays up to this share faster or slower
static const float kLiftStrong = 0.5f;       // how loud of 1 the boost has to make a lift for the stronger of its two sounds

// How a lift of the pedal is played: which of the turbo's three sounds (kTurboHigh, kTurboMid, kTurboLow), how loud
// of 1 and how fast (1 = as recorded).
struct LiftSound { int tier; float gain, pitch; };

// The sound of a lift BoostStep told (kLiftBlowOff, kLiftFlutter). loud: how loud of 1 the boost makes it (the
// model's lastVolume); soundVolume: the setting, 0 to 100; chance: -1 to 1, for the pitch. False when nothing is to
// be played: no lift, or the sound switched off. A full lift (kLiftBlowOff) with much boost is the strong sound and
// with less the middle one; a small lift (kLiftFlutter) is the middle one with much boost and the weak one with less.
inline bool LiftSoundOf(int lift, float loud, int soundVolume, float chance, LiftSound &out)
{
    out = LiftSound{};
    if ((lift != kLiftBlowOff && lift != kLiftFlutter) || soundVolume <= 0) return false;
    out.tier = lift == kLiftBlowOff ? (loud >= kLiftStrong ? kTurboHigh : kTurboMid) : (loud >= kLiftStrong ? kTurboMid : kTurboLow);
    out.gain = (float)(soundVolume > 100 ? 100 : soundVolume) / 100.0f * (kLiftQuietest + (1.0f - kLiftQuietest) * SettingsRange(loud, 0.0f, 1.0f));
    out.pitch = 1.0f + kLiftPitchSpread * SettingsRange(chance, -1.0f, 1.0f);
    return true;
}

// A number from -1 to 1 that differs from call to call.
inline float LiftChance(uint32_t &seed)
{
    if (!seed) seed = 0x9e3779b9;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (float)((double)seed / 4294967296.0 * 2.0 - 1.0);
}

// The boost in a line of text for the probe log.
inline void BoostProbeText(const TruckRaw &t, const BoostModel &m, float revs, float most, char *out, size_t size)
{
    char last[96] = "none yet";
    if (m.last != kLiftNone) sprintf_s(last, "the last a %s at %.2f bar, %.2f of its full sound", m.last == kLiftBlowOff ? "blow-off" : "flutter", m.lastBar, m.lastVolume);
    sprintf_s(out, size, "boost %.2f bar of the turbo's %.2f, heading for %.2f (pedal %.2f, the needle %.2f of the way to the red zone) | lifts: %d blow-offs, %d flutters, %s",
              m.bar, most, m.target, TruckPedal(t), revs, m.blowOffs, m.flutters, last);
}

// ---- the powertrain ----

static const float kThrottleCurve = 1.8f;    // the shaped pedal is the pedal to this power: half way down gives 0.29
static const float kThrottleUp = 0.6f;       // seconds the shaped pedal takes from up to floored when the pedal goes down at once
static const float kThrottleDown = 0.15f;    // and from floored to up
static const float kColdTorque = 0.15f;      // the torque the engine loses with its oil at 0 C, none from kColdTorqueTo up
static const float kColdTorqueTo = 60.0f;
static const float kBoostTorque = 0.35f;     // the share of the torque that comes from the boost: without it the engine has the rest
enum { kCurvePoints = 8 };                   // points a torque curve holds at most

// A torque curve: the torque as a share of the engine's most (y, 0 to 1) at engine speeds as a share of where the
// red zone starts (x, rising). Between two points the torque goes straight from one to the other; below the first
// it is the first's; from the last point to the tachometer's top it falls to nothing, as the governor cuts the fuel.
struct TorqueCurve { int n; float x[kCurvePoints], y[kCurvePoints]; };

// The curves by truck class: trucks and heavy trucks a diesel's, broad and early, scouts a later peak.
static const TorqueCurve kClassCurves[kClassCount] = {
    { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } },
    { 5, { 0.15f, 0.35f, 0.6f, 0.75f, 1.0f }, { 0.5f, 0.75f, 0.95f, 1.0f, 0.85f } },
    { 5, { 0.25f, 0.4f, 0.55f, 0.75f, 1.0f }, { 0.6f, 0.9f, 1.0f, 1.0f, 0.8f } } };

// A curve written as text, "x:y,x:y,...", as a truck's own will stand in Gauges.ini. False, and out untouched, for
// text that is not 2 to kCurvePoints points with x rising from above 0 and y from 0 to 1.
inline bool TorqueCurveParse(const char *text, TorqueCurve &out)
{
    TorqueCurve c = {};
    const char *p = text;
    while (*p)
    {
        float x = 0.0f, y = 0.0f;
        int used = 0;
        if (c.n >= kCurvePoints || sscanf_s(p, " %f : %f%n", &x, &y, &used) != 2 || !(x > 0.0f) || !(y >= 0.0f && y <= 1.0f) || (c.n && !(x > c.x[c.n - 1]))) return false;
        c.x[c.n] = x;
        c.y[c.n] = y;
        c.n++;
        p += used;
        while (*p == ' ') p++;
        if (*p == ',')
        {
            p++;
            while (*p == ' ') p++;
            if (!*p) return false; // a comma with no point after it
        }
        else if (*p) return false;
    }
    if (c.n < 2) return false;
    out = c;
    return true;
}

// The torque at an engine speed (rpm over the red zone's), from the curve; top is the tachometer's top over the red
// zone's, where the governor leaves nothing.
inline float TorqueAt(const TorqueCurve &c, float speed, float top)
{
    if (c.n < 1 || !(speed >= 0.0f)) return 0.0f;
    if (speed <= c.x[0]) return c.y[0];
    for (int i = 1; i < c.n; i++)
        if (speed <= c.x[i]) return c.y[i - 1] + (c.y[i] - c.y[i - 1]) * (speed - c.x[i - 1]) / (c.x[i] - c.x[i - 1]);
    const float last = c.x[c.n - 1];
    return top > last ? c.y[c.n - 1] * SettingsRange(1.0f - (speed - last) / (top - last), 0.0f, 1.0f) : 0.0f;
}

// The pedal shaped by the curve, the way it is ramped. pedal: as the driver holds it, 0 to 1.
inline float ThrottleShape(float pedal) { return powf(SettingsRange(pedal, 0.0f, 1.0f), kThrottleCurve); }

struct PowertrainModel
{
    bool running;      // the model has a truck
    float pedal;       // the shaped pedal, 0 to 1, after the ramp
    float torque;      // the torque the engine has now, as a share of its most: the curve, the cold and the boost
    float curve, cold, boost; // the three, for the probe
    float power;       // what it delivers: the torque times the shaped pedal, 1 = floored at the peak, warm, at full boost
    float load;        // the fuel it burns, as a share of floored at the red zone: the power times the engine speed
};

// One step of dt seconds. speed: the engine speed over the red zone's (TachSpeed); top: the tachometer's top over the
// red zone's; oil: the oil temperature in C (TempModel::oil); boost: the boost as a share of the most the turbo makes.
// The shaped pedal follows the driver's at the ramp's pace; with the engine off it and the power are 0.
inline void PowertrainStep(PowertrainModel &m, const TruckRaw &t, const TorqueCurve &c, float speed, float top, float oil, float boost, float dt)
{
    if (!m.running) m = PowertrainModel{ true };
    const float want = TruckEngineOn(t) ? ThrottleShape(TruckPedal(t)) : 0.0f;
    const float step = dt / (want > m.pedal ? kThrottleUp : kThrottleDown);
    m.pedal = want > m.pedal ? fminf(want, m.pedal + step) : fmaxf(want, m.pedal - step);
    if (!(m.pedal >= 0.0f)) m.pedal = 0.0f; // 0 too for a step that gave no number
    m.curve = TorqueAt(c, speed, top);
    m.cold = isfinite(oil) ? 1.0f - kColdTorque * SettingsRange((kColdTorqueTo - oil) / kColdTorqueTo, 0.0f, 1.0f) : 1.0f;
    m.boost = 1.0f - kBoostTorque + kBoostTorque * (boost > 0.0f ? fminf(boost, 1.0f) : 0.0f);
    m.torque = TruckEngineOn(t) ? m.curve * m.cold * m.boost : 0.0f;
    m.power = m.torque * m.pedal;
    m.load = m.power * (speed > 0.0f ? fminf(speed, 1.0f) : 0.0f);
}

// The powertrain in a line of text for the probe log.
inline void PowertrainProbeText(const TruckRaw &t, const PowertrainModel &m, float rpm, char *out, size_t size)
{
    sprintf_s(out, size, "powertrain (not handed to the game yet): pedal %.2f, shaped %.2f | torque %.2f of the most at %.0f rpm (curve %.2f, cold x%.2f, boost x%.2f) | "
                         "power %.2f, fuel %.2f of floored at the red zone",
              TruckPedal(t), m.pedal, m.torque, rpm, m.curve, m.cold, m.boost, m.power, m.load);
}

// ---- the automatic gearbox ----

static const float kShiftUpLight = 0.35f, kShiftUpFloored = 0.8f;   // shift up with the engine this far from idle to the red zone's speed: at a light foot, and floored
static const float kShiftDownLight = 0.12f, kShiftDownFloored = 0.3f; // shift down below this, the same way
static const float kShiftRoom = 1.1f;        // a shift up waits until the next gear's speed is this far over the point the gearbox would shift back down at
static const float kCoastDown = 0.25f, kCoastDownTo = 0.75f; // with the foot off it shifts down below this, as far as the engine stays under kCoastDownTo
static const float kShiftPause = 1.0f;       // seconds from one shift to the next
static const float kKickdown = 0.9f;         // a pedal pushed past this kicks down, once, to the lowest gear that keeps the engine under kKickdownTo
static const float kKickdownTo = 0.75f;
static const float kKickWindow = 0.5f;       // seconds after the push the kickdown may come in
static const float kStallSpeed = 0.75f;      // the converter's stall speed as a share of the red zone's: where a floored engine stops against a standing truck
static const float kStallRpmMost = 2400.0f;  // and at the most this: a fast revving engine's converter stalls well short of its red zone
static const float kConverterSlip = 0.04f;   // an unlocked converter turns the engine this much faster than the gearbox's input
static const float kCouplingPoint = 0.85f;   // from this ratio of the gearbox's input speed to the engine's the converter multiplies nothing
static const float kStallRatio = 2.0f;       // and at the stall it doubles the torque
static const float kCreep = 0.06f;           // what an idling engine pushes through the converter at a standstill, as a share of the most power
static const float kLockFrom = 0.5f, kUnlockBelow = 0.4f; // the converter locks from the second gear with the input at this share of the red zone's speed, and lets go below that or at a shift
static const float kStopShift = 0.1f;        // under this share of the red zone's speed in the first gear the truck all but stands: the first gear at once
static const float kShiftSmooth = 0.25f;     // seconds the wheels' speed is smoothed over before the gearbox goes by it
static const float kTrendSmooth = 1.0f;      // and seconds how fast it changes is smoothed over
static const float kFootOff = 0.05f;         // a pedal under this is the foot off
static const float kLabouring = 0.35f;       // rad/s each second the truck loses with the foot on, for kLabourAfter: a climb
static const float kLabourAfter = 0.5f;
static const float kClimbHold = 4.0f;        // seconds the shift points stay where floored puts them after a climb
static const float kGripSpeed = 2.5f;        // m/s the truck goes at the least for the wheels' speed against the ground's to be learned
static const float kGripSteady = 0.05f;      // and the wheels within this share of their smoothed speed and of each other, the truck gaining or losing
static const float kGripSpread = 0.15f;      // under kGripGain rad/s each second: no bump, no wheel off the ground, no start
static const float kGripGain = 1.0f;
static const float kGripFall = 2.0f, kGripRise = 20.0f; // seconds the grip takes to follow a lower reading, and a higher one
static const float kSlipShare = 1.4f, kSlipSpare = 1.0f; // wheels this much faster than the ground under them, and this many rad/s more, spin: the gearbox goes by the ground
static const float kDownMost = 0.9f;         // no shift down puts the engine, at the speed the wheels turn, further than this from idle to the red zone
static const float kShiftUpAfter = 0.4f, kShiftDownAfter = 0.5f, kCoastDownAfter = 0.3f; // seconds a shift up, a shift down and one with the foot off has to be called for before it is made
static const float kShiftUpAfterFloored = 0.15f; // and a shift up floored, where a short first gear is through in a second
static const float kPedalFall = 0.5f;        // the pedal the shift points go by follows a lifted foot down this much each second: a lift is no call to shift up

// The converter's stall speed: where a floored engine stops against a standing truck.
inline float ConverterStall(const GaugeScale &k) { return fmaxf(fminf(k.redline * kStallSpeed, kStallRpmMost), k.idle); }

// The brake as held, 0 to 1: the side of the pedal opposite to the one that drives.
inline float TruckBrake(const TruckRaw &t)
{
    const float b = t.automatic && t.gearEngaged < 0 ? -t.throttle : t.throttle;
    return SettingsRange(b, 0.0f, 1.0f);
}

struct GearboxModel
{
    bool running;      // the model has a truck
    int gear;          // the gear it would be in, 1 to the gearbox's top (TruckTopGear); 0 before it has one
    bool locked;       // the converter is locked up
    float input;       // the gearbox's input speed in rpm: what the engine turns at with the converter locked
    float rpm;         // the engine speed
    float ratio;       // how much the converter multiplies the torque, 1 to kStallRatio
    float drive;       // what goes to the wheels: the power times the ratio, and the creep, as a share of the most power
    float pause;       // seconds until it may shift again
    float spin;        // the driven wheels' speed, smoothed over kShiftSmooth
    float grip;        // rad/s the driven wheels turn for each m/s the truck goes, as learned where they grip; 0 before
    float by;          // the truck's speed in rad/s of the wheels: the ground's once grip is known, else spin
    float trend;       // how fast that changes, rad/s each second, smoothed over kTrendSmooth
    float labour;      // seconds the truck has been losing speed with the foot on
    float rolling;     // the speed the wheels will turn at after a shift: the largest of theirs and the ground's
    bool spinning;     // the wheels turn faster than the ground goes under them
    float wanted;      // seconds a shift has been called for, above 0 up and below 0 down
    float feel;        // the pedal the shift points go by: up with the foot at once, down at kPedalFall
    float pedal;       // the pedal on the last step, for the push that kicks down
    float kick;        // seconds the kickdown of a push may still come in
    float climb;       // seconds the shift points stay at floored's after a climb
    int shifts;        // shifts since the model began
};

// The gearbox's input speed in rpm in a gear with the driven wheels at spin rad/s: the red zone's speed at the gear's top.
inline float GearboxInput(const TruckRaw &t, const GaugeScale &k, int gear, float spin)
{
    const float top = TruckGearTop(t, gear);
    return top > 0.0f ? spin / top * k.redline : 0.0f;
}

// An engine speed this far from idle to the red zone's.
inline float GearboxPoint(const GaugeScale &k, float share) { return k.idle + (k.redline - k.idle) * share; }

// One step of dt seconds. pedal: the pedal as the driver holds it (TruckPedal), 0 with the engine off; the shift points
// go by where the foot is, not by the powertrain's curve, under which a fifth of the pedal reads as nothing. power:
// the power before the converter, from the powertrain (PowertrainModel). In the high gear, the low range, neutral and reverse the game's gearbox is left as it is, and the
// model only follows the wheels.
//
// The shifts go by the wheels' speed, as a truck's automatic goes by its output shaft, but neither wheelspin in mud nor
// wheels locked under the brake shift it, once the wheels' speed against the ground's is learned; no shift down puts
// the engine near the red zone at the speed the wheels turn. The shift points lie between idle and the red zone and go by a pedal that falls slowly
// after a lift, so a foot floored and lifted by turns does not shift up at each lift. With the foot off it never
// shifts up short of the red zone, and shifts down early, gears at a time when the truck slows hard. A truck slowing
// with the foot on is climbing: the points go where floored puts them, and stay there kClimbHold after, so it shifts
// down in time and does not hunt. A push of the pedal past kKickdown kicks down once.
inline void GearboxStep(GearboxModel &m, const TruckRaw &t, const GaugeScale &k, float pedal, float power, float dt)
{
    const int top = TruckTopGear(t);
    const float raw = TruckDrivenSpin(t), p = SettingsRange(pedal, 0.0f, 1.0f);
    if (!m.running)
    {
        m = GearboxModel{ true };
        m.spin = m.by = raw;
        m.pedal = p;
    }
    if (m.pause > 0.0f) m.pause -= dt;
    m.feel = fmaxf(p, m.feel - kPedalFall * dt);
    if (p >= kKickdown && m.pedal < kKickdown) m.kick = kKickWindow;
    else if (m.kick > 0.0f) m.kick -= dt;
    m.pedal = p;
    if (top < 1 || !TruckEngineOn(t) || !isfinite(raw))
    {
        m.rpm = TruckEngineOn(t) ? k.idle : 0.0f;
        m.ratio = 1.0f;
        m.drive = 0.0f;
        return;
    }
    if (m.gear < 1 || m.gear > top) m.gear = 1;
    // the wheels' speed smoothed
    m.spin += (raw - m.spin) * (1.0f - expf(-dt / kShiftSmooth));
    // the wheels' speed against the ground's, learned where they cannot turn slower than the ground goes and turn
    // steadily: the foot on, no brake, no handbrake, all driven wheels alike and the truck neither starting nor
    // braking. A lower reading is followed over kGripFall, so a bump that slows the wheels for a moment does not count.
    // The ground's speed is the truck's forwards: sliding back down a slope in a forward gear it goes nowhere
    const float ground = isfinite(t.speed) && t.speed > 0.0f ? t.speed : 0.0f;
    const bool steady = fabsf(raw - m.spin) < kGripSteady * raw && TruckDrivenSpread(t) < kGripSpread && fabsf(m.trend) < kGripGain;
    if (p > 0.2f && TruckBrake(t) == 0.0f && !t.handbrake && ground > kGripSpeed && raw > 1.0f && steady)
    {
        const float r = raw / ground;
        m.grip = m.grip > 0.0f ? m.grip + (r - m.grip) * (1.0f - expf(-dt / (r < m.grip ? kGripFall : kGripRise))) : r;
    }
    // the truck's speed in the wheels' terms (the ground's once the grip is known), and how fast it changes
    const float onGround = m.grip * ground, before = m.by;
    m.by = m.grip > 0.0f ? onGround : m.spin;
    if (dt > 0.0f) m.trend += ((m.by - before) / dt - m.trend) * (1.0f - expf(-dt / kTrendSmooth));
    // the speed the shifts go by: the wheels', at the most kSlipShare over the ground's, so wheelspin in mud does not
    // shift up; with the foot off the ground's where the wheels turn slower, so wheels locked under the brake do not
    // shift down; and the speed the wheels will turn at after a shift down, for the engine's sake
    const float pace = m.grip > 0.0f ? fminf(m.spin, onGround * kSlipShare + kSlipSpare) : m.spin;
    const float coast = m.grip > 0.0f ? fmaxf(raw, onGround) : m.spin, rolling = fmaxf(fmaxf(raw, m.spin), onGround);
    m.spinning = m.grip > 0.0f && m.spin > onGround * kSlipShare + kSlipSpare;
    m.rolling = rolling;
    // where it would shift at this pedal, at floored's while climbing
    const bool footOff = p < kFootOff;
    m.labour = !footOff && m.trend < -kLabouring ? m.labour + dt : 0.0f;
    const bool labouring = m.labour >= kLabourAfter;
    if (labouring) m.climb = kClimbHold;
    else if (m.climb > 0.0f) m.climb -= dt;
    const float feel = m.climb > 0.0f ? 1.0f : m.feel;
    const float up = GearboxPoint(k, kShiftUpLight + (kShiftUpFloored - kShiftUpLight) * feel);
    const float down = GearboxPoint(k, kShiftDownLight + (kShiftDownFloored - kShiftDownLight) * feel);
    const float coastDown = GearboxPoint(k, kCoastDown), coastTo = GearboxPoint(k, kCoastDownTo), most = GearboxPoint(k, kDownMost);
    const float by = footOff ? coast : pace, now = GearboxInput(t, k, m.gear, by), lower = GearboxInput(t, k, m.gear - 1, by);
    // a lower gear the engine can take at the speed the wheels turn
    auto fits = [&](int gear) { return GearboxInput(t, k, gear, rolling) < most; };
    int to = m.gear;
    const bool wantUp = m.gear < top && (now > k.redline || (!footOff && !labouring && now >= up && GearboxInput(t, k, m.gear + 1, by) >= down * kShiftRoom));
    const bool wantDown = m.gear > 1 && fits(m.gear - 1) && (footOff ? now < coastDown && lower < coastTo : now < down && lower < up);
    m.wanted = wantUp ? fmaxf(m.wanted, 0.0f) + dt : wantDown ? fminf(m.wanted, 0.0f) - dt : 0.0f;
    // all but standing, the first gear at once, whatever the pause: an automatic is in it by the time the truck stops,
    // also after a hard stop that left no time for the shifts down one by one
    if (m.gear > 1 && GearboxInput(t, k, 1, fmaxf(raw, m.by)) < k.redline * kStopShift)
        to = 1;
    else if (m.kick > 0.0f && m.gear > 1 && lower < GearboxPoint(k, kKickdownTo) && fits(m.gear - 1))
    {
        // a kickdown, at once and whatever the pause: the lowest gear that keeps the engine under kKickdownTo
        while (to > 1 && GearboxInput(t, k, to - 1, by) < GearboxPoint(k, kKickdownTo) && fits(to - 1)) to--;
        m.kick = 0.0f;
    }
    else if (m.pause <= 0.0f)
    {
        if (m.wanted >= kShiftUpAfter + (kShiftUpAfterFloored - kShiftUpAfter) * feel || (wantUp && now > k.redline))
            to = m.gear + 1;
        else if (footOff && m.wanted <= -kCoastDownAfter)
        {
            // coasting down: as far as the engine stays under coastTo, gears at a time when the truck slows hard
            to = m.gear - 1;
            while (to > 1 && GearboxInput(t, k, to, by) < coastDown && GearboxInput(t, k, to - 1, by) < coastTo && fits(to - 1)) to--;
        }
        else if (!footOff && m.wanted <= -kShiftDownAfter)
            to = m.gear - 1;
    }
    if (to != m.gear)
    {
        m.gear = to;
        m.locked = false;
        m.pause = kShiftPause;
        m.wanted = 0.0f;
        m.shifts++;
    }
    // the converter: locked, or slipping up to the engine's speed against the gearbox's input
    m.input = GearboxInput(t, k, m.gear, m.spin);
    if (!m.locked && m.gear >= 2 && m.pause <= 0.0f && m.input >= k.redline * kLockFrom) m.locked = true;
    else if (m.locked && m.input < k.redline * kUnlockBelow) m.locked = false;
    const float stall = k.idle + (ConverterStall(k) - k.idle) * p;
    m.rpm = m.locked ? fmaxf(m.input, k.idle) : fmaxf(fmaxf(m.input * (1.0f + kConverterSlip), stall), k.idle);
    const float speedRatio = m.rpm > 0.0f ? m.input / m.rpm : 1.0f;
    m.ratio = m.locked || speedRatio >= kCouplingPoint ? 1.0f : 1.0f + (kStallRatio - 1.0f) * (1.0f - speedRatio / kCouplingPoint);
    // the creep: what an idling engine pushes through the converter, less as the truck rolls up to the engine's speed
    const float creep = kCreep * (1.0f - p) * (1.0f - SettingsRange(speedRatio, 0.0f, 1.0f));
    m.drive = m.locked ? power : (power + creep) * m.ratio;
    if (!isfinite(m.drive)) m.drive = 0.0f;
}

// The lowest gear the engine can take at the speed the wheels turn (under kDownMost from idle to the red zone), 1 when
// the first can.
inline int GearboxLowestFit(const GearboxModel &m, const TruckRaw &t, const GaugeScale &k)
{
    const int top = TruckTopGear(t);
    int gear = 1;
    while (gear < top && GearboxInput(t, k, gear, m.rolling) >= GearboxPoint(k, kDownMost)) gear++;
    return gear;
}

// The gearbox in a line of text for the probe log, beside the game's own gear.
// driving: the mod's automatic sets the game's gears (TakeoverModel).
inline void GearboxProbeText(const TruckRaw &t, const GearboxModel &m, bool driving, char *out, size_t size)
{
    sprintf_s(out, size, "automatic (%s): gear %d of %d (the game's %d), %s, input %.0f rpm, engine %.0f rpm, torque x%.2f, to the wheels %.2f | wheels %.2f rad/s for each m/s%s | %s%s%d shifts",
              driving ? "the mod's, setting the game's gears" : "not handed to the game", m.gear, TruckTopGear(t), t.gearEngaged, m.locked ? "locked up" : "converter slipping",
              m.input, m.rpm, m.ratio, m.drive, m.grip, m.grip > 0.0f ? "" : " (not learned yet)", m.climb > 0.0f ? "climbing, " : "", m.spinning ? "wheels spinning, going by the ground, " : "", m.shifts);
}

// ---- the mod's automatic in the game ----
// With Gearbox set to the mod's, the model above picks the gears while the player has the game's shifter in A. The
// game shows A by a flag in the truck's action (TruckRaw::automatic); the mod puts that flag down, so the game's own
// automatic stops choosing, and sets the model's gear through the game's own shift function, as a manual
// transmission mod sets a gear from a key. The player gets the gearbox back by picking a gear by hand: a gear the
// mod did not set, once the last one it set has had kTakeSettle to arrive. The game's flag up again (A picked once
// more, or the shift modifier tapped) asks the game's own automatic for a gear: the mod takes that gear on and holds
// it kTakeHold before its own shift points go on from there, but never a gear lower than the engine can take at the
// speed the wheels turn: with the foot off the game's own automatic picks the first gear at any speed, and that gear
// is put back to the lowest the engine can take. Another numbered gear within that time (the game or another mod
// going back on the pick) is taken on the same way, not read as a gear picked by hand.
static const float kTakeSettle = 0.5f;       // seconds the game has to take a gear the mod set before another gear is the player's
static const float kTakeHold = 2.0f;         // seconds the gear the game's own automatic picked holds
static const float kTakeLowPower = 0.02f;    // the power factor this far under the most it was while the mod drove: a low gear picked by hand

struct TakeoverModel
{
    bool driving;      // the mod has the gearbox
    float power;       // the most the power factor was while it drove: a lower one is a low gear picked by hand
    int set;           // the gear it set last
    float since;       // seconds since then
    int shifts;        // gears set since it took the gearbox
    int handBacks;     // times it gave the gearbox back
    int lastHandBack;  // why, the last time: one of the kTake... reasons
    bool adopted;      // the gear it holds is one the game's own automatic picked, not one it set
};
enum { kTakeNone, kTakePicked, kTakeLow, kTakeOff, kTakeGone };

// What the mod does to the game on this pass.
struct TakeoverOrders
{
    bool flagDown;     // put the game's automatic flag down
    int shiftTo;       // set this gear, 0 for none
    bool took;         // it took the gearbox on this pass
    int adopted;       // the gear the game's own automatic picked on this pass, or the lowest the engine can take where
                       // it picked lower, which the model takes on; 0 for none
    bool gaveBack;     // it gave the gearbox back on this pass (lastHandBack says why)
};

// One pass of dt seconds. enabled: Gearbox is the mod's and the game's shift function is known; t: the truck as
// read; modelGear: the gear the model wants (GearboxModel::gear); lowestFit: the lowest gear the engine can take
// (GearboxLowestFit). The gearbox is taken when the game's shifter is in A
// on a numbered gear (from neutral the game puts the first gear in itself as the truck sets off), in the gear the
// game is in, and given back when the player picks a gear, the setting goes off or the engine stops. A rise of the
// power factor (another mod setting its own for the automatic) is no gear picked.
inline TakeoverOrders TakeoverStep(TakeoverModel &m, const TruckRaw &t, int modelGear, bool enabled, float dt, int lowestFit = 1)
{
    TakeoverOrders o = {};
    const int top = TruckTopGear(t);
    const int want = modelGear < 1 ? 1 : modelGear > top ? top : modelGear;
    m.since += dt;
    if (m.driving && (!enabled || !TruckEngineOn(t) || top < 1))
    {
        m.driving = false;
        m.handBacks++;
        m.lastHandBack = !enabled ? kTakeOff : kTakeGone;
        o.gaveBack = true;
        return o;
    }
    if (!enabled || !TruckEngineOn(t) || top < 1) return o;
    if (!m.driving)
    {
        // taken in the gear the game is in: the model goes on from there (the caller puts its gear there)
        if (!t.automatic || t.gear < 1 || t.gear > top) return o;
        m = TakeoverModel{ true, t.powerFactor, t.gear, 0.0f, 0, m.handBacks, m.lastHandBack };
        o.took = true;
        o.flagDown = true;
        return o;
    }
    // reverse, neutral or the high gear (picked in A or not), a gear the mod did not set, or a low gear by its power:
    // the player's
    if (t.powerFactor > m.power) m.power = t.powerFactor;
    const bool outside = t.gear < 1 || t.gear > top;
    // the game's own automatic's pick, never lower than the engine can take
    const int pick = t.gear < lowestFit ? lowestFit : t.gear;
    if (!outside && !t.automatic && t.gear != m.set && m.adopted && m.since < kTakeHold && t.powerFactor >= m.power - kTakeLowPower)
    {
        // the gear the game's own automatic picked, gone back on within the hold: the gear it went to is taken on
        o.adopted = pick;
        if (pick != t.gear) o.shiftTo = pick;
        m.set = pick;
        return o;
    }
    const bool otherGear = outside || (!t.automatic && t.gear != m.set && m.since >= kTakeSettle), lowGear = t.powerFactor < m.power - kTakeLowPower;
    if (otherGear || lowGear)
    {
        m.driving = false;
        m.handBacks++;
        m.lastHandBack = lowGear ? kTakeLow : kTakePicked;
        o.gaveBack = true;
        return o;
    }
    o.flagDown = t.automatic != 0;
    if (t.automatic && t.gear != m.set)
    {
        // the game's own automatic picked a gear (the shift modifier tapped): the model goes on from it
        o.adopted = pick;
        if (pick != t.gear) o.shiftTo = pick;
        if (m.set) m.shifts++;
        m.set = pick;
        m.since = 0.0f;
        m.adopted = true;
        return o;
    }
    if (want != m.set || (t.automatic && t.gear != want))
    {
        m.adopted = false;
        o.shiftTo = want;
        if (m.set) m.shifts++;
        m.set = want;
        m.since = 0.0f;
    }
    return o;
}

// The game did not take the gear the mod set (its shift function said no): the gear it is in counts as set, so the
// mod tries again when the model next wants another gear, and the player's gear is told by the next one.
inline void TakeoverRefused(TakeoverModel &m, const TruckRaw &t) { m.set = t.gear; }

inline const char *TakeoverWhy(int reason)
{
    return reason == kTakePicked ? "a gear was picked by hand" : reason == kTakeLow ? "a low gear was picked by hand" : reason == kTakeOff ? "the setting is off" : reason == kTakeGone ? "the engine stopped" : "";
}

// ---- the clutch ----

static const float kClutchFree = 0.85f;      // a pedal pressed past this share of its travel holds nothing
static const float kClutchBite = 0.35f;      // and one let up past this holds all it can; the bite lies between
static const float kClutchHold = 1.5f;       // what a clutch fully let up holds, as a share of the engine's most torque
static const float kEngineRevTime = 0.8f;    // seconds an engine takes from idle to the red zone floored with nothing to drive
static const float kGovernorBand = 0.1f;     // the governor gives full torque this far under the speed it holds, as a share of idle to red zone
static const float kEngineDrag = 0.4f;       // what the engine's friction and pumping take at the red zone, as a share of its most torque; it grows with the speed
static const float kClutchStall = 0.5f;      // an engine dragged under this share of its idle speed dies
static const float kStallRest = 2.0f;        // seconds a stalled engine stays dead in the model at the least, before it can be started again
static const float kAutoClutchSpan = 0.25f;  // a clutch that works by itself holds fully this far from idle to the red zone

// The clutch pedal's grip, 0 to 1, from its travel, 0 let up to 1 pressed: nothing past kClutchFree, all of it short
// of kClutchBite, and between them the bite, eased in.
inline float ClutchGrip(float travel)
{
    const float g = SettingsRange((kClutchFree - travel) / (kClutchFree - kClutchBite), 0.0f, 1.0f);
    return g * g * (3.0f - 2.0f * g);
}

struct ClutchModel
{
    bool running;      // the model has a truck
    float rpm;         // the engine speed
    float input;       // the gearbox's input speed, from the driven wheels and the gear; 0 in neutral
    float grip;        // the clutch's grip, 0 to 1 (ClutchGrip)
    bool locked;       // the engine turns with the gearbox
    bool connected;    // a gear is in
    float torque;      // what the fuel gives, as a share of the engine's most: by its governor, at most its curve's
    float carried;     // what the clutch passes on, the same way: below 0 the engine brakes the truck
    bool dead;         // the engine stalled, and has not been started again
    float stalled;     // seconds left of the stall's rest
    bool automatic;    // the clutch works by itself (no pedal)
    int stalls;        // stalls since the model began
};

// One step of dt seconds of the engine behind a clutch, for a manual gearbox with a clutch pedal (a wheel's, through
// the manual transmission mod; smt.h). c: the engine's torque curve; travel: the clutch pedal, 0 let up to 1 pressed;
// pedal: the shaped pedal and factor: the cold and the boost's share of the torque, both from the powertrain
// (PowertrainModel::pedal, and ::cold times ::boost). The engine's governor holds the speed the pedal asks for (idle at
// rest), with up to full torque; the clutch passes on up to its grip times kClutchHold. Let up, it turns the engine
// with the gearbox where it can hold what the engine gives; slipping, it pulls the engine towards the gearbox's
// speed. An engine dragged under kClutchStall of its idle speed stalls: a clutch let up too fast with the truck
// standing, or in too high a gear at a crawl. A stalled engine is started again after kStallRest once the clutch is
// pressed or no gear is in. automatic: no pedal, so travel is a button's (1 held) or 0, and the clutch takes hold by
// the engine's speed, from none at idle to all of it kAutoClutchSpan on, and fully with the gearbox turning at idle
// or over; it never stalls the engine. The wheels are the game's, so the model only follows them. Returns true on the
// step the engine stalls.
inline bool ClutchStep(ClutchModel &m, const TruckRaw &t, const GaugeScale &k, const TorqueCurve &c, float travel, float pedal, float factor, bool automatic, float dt)
{
    if (!m.running)
    {
        m = ClutchModel{ true };
        m.rpm = TruckEngineOn(t) ? k.idle : 0.0f;
    }
    const float range = k.redline - k.idle;
    const int gear = t.gearEngaged;
    const float top = gear ? TruckGearTop(t, gear) : 0.0f, spin = TruckDrivenSpin(t);
    m.connected = top > 0.0f && isfinite(spin);
    m.input = m.connected ? spin / top * k.redline : 0.0f;
    m.automatic = automatic;
    m.grip = m.connected ? ClutchGrip(SettingsRange(travel, 0.0f, 1.0f)) : 0.0f;
    if (!TruckEngineOn(t) || !(range > 0.0f))
    {
        m.rpm = 0.0f;
        m.torque = m.carried = 0.0f;
        m.locked = m.dead = false;
        m.stalled = 0.0f;
        return false;
    }
    if (m.dead)
    {
        m.stalled = fmaxf(m.stalled - dt, 0.0f);
        m.rpm = 0.0f;
        m.torque = m.carried = 0.0f;
        m.locked = false;
        // started again as a driver would: the clutch pressed, or no gear in
        if (m.stalled <= 0.0f && (automatic || !m.connected || travel >= kClutchFree))
        {
            m.dead = false;
            m.rpm = k.idle;
        }
        return false;
    }
    if (automatic && m.connected && m.input < k.idle)
    {
        // the clutch that works by itself: none of it at idle, all of it kAutoClutchSpan of the way to the red zone
        const float g = SettingsRange((m.rpm - k.idle) / (range * kAutoClutchSpan), 0.0f, 1.0f);
        m.grip = fminf(m.grip, g * g * (3.0f - 2.0f * g));
    }
    // the governor: the speed the pedal asks for, and the torque to hold it, up to the curve's
    const float want = k.idle + range * kTachFree * SettingsRange(pedal, 0.0f, 1.0f);
    const float speed = m.rpm / k.redline;
    const float full = TorqueAt(c, speed, k.max / k.redline) * (factor > 0.0f ? factor : 1.0f);
    const float drag = kEngineDrag * speed;
    m.torque = SettingsRange((want - m.rpm) / (range * kGovernorBand), 0.0f, full);
    const float hold = m.grip * kClutchHold;
    const float rate = range / kEngineRevTime; // rpm each second for each share of the most torque
    // locked: the engine turns with the gearbox while the clutch holds what the engine gives, or brakes with
    if (m.locked && (hold < fabsf(m.torque - drag) || m.grip <= 0.0f)) m.locked = false;
    if (m.locked)
    {
        m.rpm = m.input;
        m.carried = m.torque - drag;
    }
    else
    {
        const float slip = m.rpm - m.input;
        m.carried = slip > 0.0f ? hold : slip < 0.0f ? -hold : 0.0f;
        float next = m.rpm + (m.torque - drag - m.carried) * rate * dt;
        // the clutch closes the slip: past the gearbox's speed it holds there, locked if it can
        if (hold > 0.0f && (slip > 0.0f) != (next - m.input > 0.0f))
        {
            next = m.input;
            m.locked = hold >= fabsf(m.torque - drag);
        }
        m.rpm = next;
    }
    if (automatic && m.rpm < k.idle * kClutchStall) m.rpm = k.idle * kClutchStall; // let go before it could stall
    else if (m.rpm < k.idle * kClutchStall)
    {
        m.dead = true;
        m.stalled = kStallRest;
        m.stalls++;
        m.rpm = 0.0f;
        m.locked = false;
        m.torque = m.carried = 0.0f;
        return true;
    }
    return false;
}

// The clutch in a line of text for the probe log.
inline void ClutchProbeText(const ClutchModel &m, float travel, char *out, size_t size)
{
    if (m.dead)
    {
        sprintf_s(out, size, "clutch (not handed to the game yet): the engine stalled, %s, %d stalls",
                  m.stalled > 0.0f ? "resting" : "waiting for the clutch pressed or neutral to start again", m.stalls);
        return;
    }
    sprintf_s(out, size, "clutch (not handed to the game yet): %s %.2f, grip %.2f, %s | engine %.0f rpm, gearbox %.0f rpm | torque %.2f, through the clutch %.2f | %d stalls",
              m.automatic ? "works by itself, button" : "pedal", travel, m.grip, !m.connected ? "no gear in" : m.locked ? "locked" : m.grip > 0.0f ? "slipping" : "free",
              m.rpm, m.input, m.torque, m.carried, m.stalls);
}

// ---- the brake air ----

static const float kAirCutOut = 8.2f;        // bar: the governor stops the compressor here
static const float kAirCutIn = 6.9f;         // and starts it again here
static const float kAirFlow = 0.23f;         // bar each second the compressor gives at the red zone's speed into an empty tank
static const float kAirMost = 12.0f;         // bar the compressor could reach at all: it gives less as the tank fills
static const float kBrakeAir = 0.35f;        // bar a full brake application takes from the tank; a lighter one its share
static const float kSpringAir = 0.4f;        // bar the spring brakes take to be held off when the handbrake is let go
static const float kAirLeak = 0.1f / 60.0f;  // bar each second the system loses through its fittings
static const float kAirLeakBraking = 0.25f / 60.0f; // and on top of that while the brake is held
static const float kAirWarn = 5.5f;          // under this the dial's red zone: too little air to brake safely

// The air in the brakes' tanks of a truck, scouts aside (they brake by hydraulics; the dial is not drawn for them).
// The engine drives the compressor, which gives more air the faster the engine turns and less as the tank fills; the
// governor runs it from kAirCutIn up to kAirCutOut and lets it rest in between, so the needle climbs and settles at
// the cut-out and wanders down to the cut-in as the brakes draw air. Each press of the brake takes air by how far it
// goes down (a gamepad's trigger, often floored, takes the most), letting the handbrake go takes the air that holds
// the spring brakes off, and the system leaks a little, more with the brake held. Filled from empty in about three
// minutes at idle and one at the red zone; eight full brake applications with the engine off take a full tank into
// the red zone.
struct AirModel
{
    bool running;      // the model has a truck
    float bar;         // the tank's pressure
    bool pumping;      // the governor runs the compressor
    float brake;       // the brake as held on the last step, 0 to 1
    bool handbrake;    // the handbrake was on on the last step
    int cutOuts;       // times the governor stopped the compressor: the air dryer's purge
};

// One step of dt seconds. speed: the engine speed over the red zone's (TachSpeed). A new truck starts with a full
// tank. Returns true on the step the governor stops the compressor, when a truck's air dryer purges with a hiss.
inline bool AirStep(AirModel &m, const TruckRaw &t, float speed, float dt)
{
    const float brake = TruckBrake(t);
    const bool handbrake = t.handbrake != 0;
    if (!m.running)
    {
        m = AirModel{ true, kAirCutOut };
        m.brake = brake;
        m.handbrake = handbrake;
    }
    // what the brakes take: a press by how much further it goes down, the spring brakes when the handbrake goes off
    float used = fmaxf(brake - m.brake, 0.0f) * kBrakeAir + (m.handbrake && !handbrake ? kSpringAir : 0.0f);
    used += (kAirLeak + (brake > 0.05f ? kAirLeakBraking : 0.0f)) * dt;
    m.brake = brake;
    m.handbrake = handbrake;
    // the compressor, by the governor
    bool cutOut = false;
    if (m.bar <= kAirCutIn) m.pumping = true;
    const float gives = m.pumping && TruckEngineOn(t) ? kAirFlow * SettingsRange(speed, 0.0f, 1.0f) * fmaxf(1.0f - m.bar / kAirMost, 0.0f) * dt : 0.0f;
    m.bar = fmaxf(m.bar + gives - used, 0.0f);
    if (!isfinite(m.bar)) m.bar = kAirCutOut;
    if (m.pumping && m.bar >= kAirCutOut)
    {
        m.bar = kAirCutOut;
        m.pumping = false;
        m.cutOuts++;
        cutOut = true;
    }
    return cutOut;
}

// The brake air in a line of text for the probe log.
inline void AirProbeText(const TruckRaw &t, const AirModel &m, char *out, size_t size)
{
    sprintf_s(out, size, "brake air: %.2f bar, the compressor %s (cut-in %.1f, cut-out %.1f bar) | brake %.2f, handbrake %d | %d cut-outs",
              m.bar, m.pumping ? (TruckEngineOn(t) ? "running" : "waiting for the engine") : "resting", kAirCutIn, kAirCutOut, m.brake, t.handbrake, m.cutOuts);
}

// ---- the game's own clock ----

static const unsigned long long kClockHoldMs = 1000; // the game's count of simulated seconds standing still for this long: the game is not simulating

// The game counts the seconds it has simulated a truck for (TruckRaw::clock). While that count stands still the
// game's values are the last ones it left, and the models wait for it. running: the watch has seen a count; seen
// and movedAt: the count last read and when it last moved.
struct ClockWatch { bool running; float seen; unsigned long long movedAt; };

// True when the game has not simulated the truck for kClockHoldMs: the count has stood still that long. A truck
// whose count was not read is taken as simulated.
inline bool ClockStands(ClockWatch &c, const TruckRaw &t, unsigned long long ms)
{
    if (!t.clockKnown)
    {
        c = ClockWatch{};
        return false;
    }
    if (!c.running || !TruckSameBits(t.clock, c.seen))
    {
        c.running = true;
        c.seen = t.clock;
        c.movedAt = ms;
    }
    return ms - c.movedAt >= kClockHoldMs;
}

// ---- what the temperature costs the engine ----

static const float kDamageLoad = 0.5f;  // how hard a cold engine has to work (TruckLoad) before it suffers from it
static const int kDamageBlinkMs = 250;  // the damage lamp's blink: this long lit, then this long dark

struct DamageModel
{
    uint64_t id;      // the game's object for the truck the owed share belongs to
    float owed;       // the share of a point of engine damage that has come together and is not handed over yet
    float hot, cold;  // percent of the engine a minute on the last step: from the heat, and from working a cold engine hard
};

// Percent of what the engine takes that it loses a minute in the red zone: nothing at kTempRed, the settings'
// HotDamagePercentPerMinute at kTempHotDamage, and more in the same measure above that.
inline float DamageHot(float temp, const GaugeSettings &s)
{
    return temp > kTempRed ? s.hotDamage * (temp - kTempRed) / (kTempHotDamage - kTempRed) : 0.0f;
}

// The same for a cold engine worked hard: the settings' ColdDamagePercentPerMinute floored at the red zone (a load of
// 1, TruckLoad) at 0 C, less the warmer the engine and the lighter the work. Nothing from kTempCold up, and nothing at
// a load of kDamageLoad or less: pulling away and driving about at moderate revs cost nothing.
inline float DamageCold(float temp, float load, const GaugeSettings &s)
{
    if (!(temp < kTempCold) || !(load > kDamageLoad)) return 0.0f;
    const float hard = (fminf(load, 1.0f) - kDamageLoad) / (1.0f - kDamageLoad);
    return s.coldDamage * (kTempCold - temp) / kTempCold * hard * hard;
}

// One step of dt seconds for the truck the game holds in the object id; speed is TachSpeed. Returns the whole points
// of the game's engine damage that have come together since the last step; the rest of a point is kept for the next
// one. Only a running engine wears. With the settings' Damage off nothing comes together, and the two rates are
// still worked out, so the probe shows what the temperature would cost.
inline int DamageStep(DamageModel &m, uint64_t id, const TruckRaw &t, const TempModel &temp, float speed, const GaugeSettings &s, float dt)
{
    if (m.id != id)
    {
        m = DamageModel{};
        m.id = id;
    }
    m.hot = m.cold = 0.0f;
    if (!s.damage) m.owed = 0.0f;
    if (!temp.known || !TruckEngineOn(t) || !t.damageKnown) return 0;
    m.hot = DamageHot(temp.temp, s);
    m.cold = DamageCold(temp.temp, TruckLoad(t, speed), s);
    if (!s.damage) return 0;
    const float points = (m.hot + m.cold) / 100.0f * (float)t.damageCapacity * dt / 60.0f;
    if (points > 0.0f && points < 100000.0f) m.owed += points; // nothing for a rate that is no number
    const int whole = (int)m.owed;
    m.owed -= (float)whole;
    return whole;
}

// Whether the model is adding to the engine's damage now.
inline bool DamageAdding(const DamageModel &m, const GaugeSettings &s) { return s.damage && m.hot + m.cold > 0.0f; }

// The damage lamp at a time in milliseconds: lit while the engine's damage is above half of what it takes (steady),
// and blinking while damage is being added.
inline bool DamageLamp(bool steady, bool adding, unsigned long long ms) { return adding ? (ms / kDamageBlinkMs) % 2 == 0 : steady; }

// The engine's damage in a line of text for the probe log: what the game holds, what its own rule makes of that
// with the engine's record (truck.h), and what the model is adding. Without a record the rule's numbers are a
// guess and are marked as one.
inline void DamageProbeText(const TruckRaw &t, const EngineRecord &e, const DamageModel &m, const GaugeSettings &s, char *out, size_t size)
{
    if (!t.damageKnown)
    {
        sprintf_s(out, size, "engine damage: nothing behind the engine's handle");
        return;
    }
    float effect = 0.0f, torque = 1.0f;
    TruckEngineEffect(t.damage, t.damageCapacity, e, effect, torque);
    const bool same = fabsf(effect - t.damageEffect) < 0.0005f && fabsf(torque - t.torqueFactor) < 0.0005f;
    char record[256];
    if (e.found) sprintf_s(record, "record %.80s: takes %d, the torque falls past %.2f of it, from x%.2f to x%.2f", e.name, e.capacity, e.threshold, e.torqueMin, e.torqueMax);
    else if (!e.name[0]) sprintf_s(record, "the engine's record is looked up once the truck is driven");
    else sprintf_s(record, "no record of the engine \"%.80s\"", e.name);
    sprintf_s(out, size,
              "engine damage %d of %d, effect %.3f, torque x%.3f; by the game's rule effect %.3f and torque x%.3f (%s) | all parts %d of %d | %s | "
              "the model: %.2f %% a minute from the heat, %.2f from the cold, %.2f of a point owed, Damage %s",
              t.damage, t.damageCapacity, t.damageEffect, t.torqueFactor, effect, torque, !e.found ? "a guess without the record" : same ? "as read" : "NOT as read",
              t.damageSum, t.damageSumCapacity, record, m.hot, m.cold, m.owed, s.damage ? "on" : "off");
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
