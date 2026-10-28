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
// power is that torque times the shaped pedal, 1 being the engine floored at its peak, warm and at full boost. With
// Powertrain=1 the mod hands it to the game through the game's torque factor (TorqueLift); without, the probe log
// says what it would have done.
//
// The automatic gearbox. What a truck's automatic with a torque converter would do with the driven wheels' speed and
// the shaped pedal: the converter lets the engine run up to its stall speed against a standing truck and multiplies
// the torque (twice at the stall, none from the coupling point), lets an idling truck creep, and locks up from the
// second gear once the engine and the wheels turn together. It shifts up later the further the pedal is down, down
// well below that so a gear holds, kicks down on a floored pedal, holds its gear rolling downhill with the foot off,
// and lets a second pass between two shifts. A lifted foot is no call to shift up at once, and wheelspin shifts
// nothing. On a 20 minute drive logged in the game it shifts as often as the game's own automatic, which hunts the same. With
// Gearbox=1 it drives the game's gears (TakeoverStep); without, it is worked out and logged.
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

// The wheel speed in rad/s the game governs a gear to by the gearbox's number a for it alone: 2 a + 5, and a + 2 in the
// high gear; 0 for neutral or a gear the gearbox does not have. Before the low range.
inline float TruckGearFormulaTop(const TruckRaw &t, int gear)
{
    if (gear == 0 || gear >= t.gearSpeeds) return 0.0f;
    const float a = t.gearSpeed[gear < 0 ? 0 : gear];
    if (!(a > 0.0f)) return 0.0f;
    return gear == TruckTopGear(t) + 1 ? a + 2.0f : 2.0f * a + 5.0f;
}

// The wheel speed in rad/s the game governs a gear to: a numbered gear's own top where it is learned (t.gearTop, from
// where the game's pull reaches nothing: GearTopStep; review 11 read 0.90 to 0.99 of 2 a + 5 in some gears), else
// TruckGearFormulaTop. The high gear's a + 2 holds (an HX 520 sat at 15.8 to 16.3 rad/s in H against 16 in 11 floored
// plateaus). The low range is the first gear held to a share of that by the power factor.
inline float TruckGearTop(const TruckRaw &t, int gear)
{
    float top = TruckGearFormulaTop(t, gear);
    if (!(top > 0.0f)) return 0.0f;
    if (gear >= 1 && gear <= TruckTopGear(t) && gear < kMaxGears && t.gearTop[gear] > 0.0f && isfinite(t.gearTop[gear])) top = t.gearTop[gear];
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

// ---- each gear's top, learned (review 11, finding 8) ----
// A floored plateau: the pedal at kPlateauPedal or more, the game's own pull (action + 0xB4) at kPlateauPull or under,
// the power factor whole (not the low range), the wheels between kPlateauFrom and kPlateauTo of 2 a + 5 and steady
// within kPlateauSteady of their mean for kPlateauFor, in a numbered gear (the high gear's a + 2 holds). The wheels' mean
// over that second is where the game governs the gear: its top. Each plateau gives one; a gear's top learned again is
// the mean of the old and the new.
static const float kPlateauPedal = 0.9f, kPlateauPull = 0.02f, kPlateauFrom = 0.85f, kPlateauTo = 1.15f, kPlateauSteady = 0.01f, kPlateauFor = 1.0f;
struct GearTopLearn
{
    int gear;        // the gear of the plateau being watched, 0 none
    float time, sum; // its seconds so far and the wheels' sum over its passes
    int passes;
    float lo, hi;    // the wheels' least and most over it
    bool done;       // this plateau has given its top: no other until it ends
};
// One pass: true when a plateau has just completed, with the gear and the wheels' mean in gear and top.
inline bool GearTopStep(GearTopLearn &l, const TruckRaw &t, float dt, int &gear, float &top)
{
    const int g = t.gearEngaged;
    const float spin = TruckDrivenSpin(t), formula = g >= 1 && g <= TruckTopGear(t) ? TruckGearFormulaTop(t, g) : 0.0f;
    const bool plateau = TruckEngineOn(t) && formula > 0.0f && g == t.gear && TruckPedal(t) >= kPlateauPedal && isfinite(t.actionB4) &&
                         t.actionB4 <= kPlateauPull && t.powerFactor >= 0.99f && isfinite(spin) && spin >= kPlateauFrom * formula && spin <= kPlateauTo * formula;
    if (!plateau || g != l.gear)
    {
        l = GearTopLearn{};
        if (!plateau) return false;
        l.gear = g;
        l.lo = l.hi = spin;
    }
    if (l.done) return false;
    l.lo = fminf(l.lo, spin);
    l.hi = fmaxf(l.hi, spin);
    l.sum += spin;
    l.passes++;
    l.time += dt;
    const float mean = l.sum / (float)l.passes;
    if (l.hi - l.lo > 2.0f * kPlateauSteady * mean)
    {
        // not steady: start again from this pass
        l = GearTopLearn{};
        l.gear = g;
        l.lo = l.hi = l.sum = spin;
        l.passes = 1;
        l.time = dt;
        return false;
    }
    if (l.time < kPlateauFor) return false;
    gear = g;
    top = mean;
    l.done = true;
    return true;
}

// A gear's top learned again: the mean of the one kept and the new plateau's (the first plateau as it is).
inline float GearTopMerge(float kept, float plateau) { return kept > 0.0f && isfinite(kept) ? 0.5f * (kept + plateau) : plateau; }

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
// drives the game's gears), with the needle's own lag; starting, stalling and stopped as TachTarget has them, and no
// further past the red zone than wheelspin takes it there (kTachOverrun): the converter's speed has no top of its own.
inline void TachFollow(TachModel &m, const TruckRaw &t, const GaugeScale &k, float rpm, float dt)
{
    float target = !TruckEngineOn(t) ? 0.0f : t.startTimer > 0.0f && !(t.flags & 2) ? TachTarget(t, k) : rpm;
    const float alive = SettingsRange(1.0f - kStallRate * t.stall, 0.0f, 1.0f);
    target *= alive * alive;
    target = fminf(target, k.idle + (k.redline - k.idle) * kTachOverrun);
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
static const float kUsualLoad = 0.5f, kUsualKmh = 13.0f; // "driven as usual", for WarmUpMinutes: the mean load and speed on a drive logged in the game on 2026-10-08 (0.49 at 13.1 km/h)
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
static const float kBoostTorque = 0.45f;     // the share of the torque that comes from the boost: without it the engine has the rest. A turbo
                                             // diesel stepped on at low speed has 30 to 50 percent at first (review 11, from 0.35; for a drive to confirm)
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

// An engine's torque curve from the engine speed of its peak torque (peak: over the red zone's), as the engine's own
// profile gives it (Gauges\engines\<name>.ini, PeakTorqueRpm): a turbo diesel's broad plateau, from a little under
// the peak to half as much again over it, with three fifths of the torque at half the peak's speed and four fifths
// left at the red zone; a naturally aspirated engine's narrower hump around the peak, falling off sooner after it.
// Beyond the last point TorqueAt takes it down to nothing at the tachometer's top, as for every curve.
static const float kPeakFrom = 0.85f, kPeakTo = 1.5f; // a turbo diesel's plateau, from and to these times the peak's speed
inline TorqueCurve EngineCurveFromPeak(float peak, bool natural)
{
    TorqueCurve c = {};
    const float p = SettingsRange(isfinite(peak) ? peak : 0.5f, 0.15f, 0.95f);
    auto add = [&](float x, float y) {
        if (c.n < kCurvePoints && (c.n == 0 || x > c.x[c.n - 1] + 0.005f)) { c.x[c.n] = x; c.y[c.n] = y; c.n++; }
    };
    if (natural)
    {
        add(p * 0.4f, 0.7f);
        add(p, 1.0f);
        add(p + (1.0f - p) * 0.5f, 0.92f);
        add(1.0f, 0.8f);
    }
    else
    {
        add(p * 0.5f, 0.6f);
        add(p * kPeakFrom, 1.0f);
        add(fminf(p * kPeakTo, 0.95f), 1.0f);
        add(1.0f, 0.8f);
    }
    return c;
}

// An engine's own profile (Gauges\engines\<name>.ini): the engine speed of its peak torque in rpm (0: none), how it
// breathes, and a whole curve of its own where one is written (custom). own: its file holds any of them.
struct EngineProfile
{
    bool own;
    float peakRpm;
    int aspiration;
    bool custom;
    TorqueCurve curve;
    float torque;   // Torque: the share of its torque the truck gets (kTorqueShareRange), 0 for none: all of it
};

// The curve an engine pulls by: its own written one, else one from its peak torque's speed (redline: the red zone's
// rpm), else the class's.
inline TorqueCurve EngineCurveOf(const EngineProfile &p, const TorqueCurve &classCurve, float redline)
{
    if (p.custom && p.curve.n >= 2) return p.curve;
    if (p.peakRpm > 0.0f && redline > 0.0f) return EngineCurveFromPeak(p.peakRpm / redline, p.aspiration == kAspNatural);
    return classCurve;
}

// Where a curve first reaches its peak: the engine speed over the red zone's at which it has 0.98 of its most.
inline float CurvePeak(const TorqueCurve &c)
{
    float most = 0.0f;
    for (int i = 0; i < c.n; i++) most = fmaxf(most, c.y[i]);
    for (int i = 0; i < c.n; i++)
        if (c.y[i] >= most * 0.98f) return c.x[i];
    return 1.0f;
}

// Where a curve first reaches share of its most, between its points: the engine speed over the red zone's.
inline float CurveReach(const TorqueCurve &c, float share)
{
    float most = 0.0f;
    for (int i = 0; i < c.n; i++) most = fmaxf(most, c.y[i]);
    const float want = most * share;
    if (c.n < 1 || c.y[0] >= want) return c.n ? c.x[0] : 1.0f;
    for (int i = 1; i < c.n; i++)
        if (c.y[i] >= want) return c.x[i - 1] + (c.x[i] - c.x[i - 1]) * (want - c.y[i - 1]) / fmaxf(c.y[i] - c.y[i - 1], 1e-6f);
    return 1.0f;
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

// The powertrain in a line of text for the probe log. handed: the torque factor is written (Powertrain=1), written:
// the last factor written, game: the game's own share; x, roof and heldBack the factor's own terms (review 11): the
// wheels' share of the gear's top it went by (LiftShare; below 0 not given), the roof there (LiftRoof) and whether the
// pace held this pass's rise back. The factor goes by the game's own pedal (the line's "pedal"); the power and the fuel
// are the model's own, by the shaped pedal ("shaped"), which the game does not use. The fields up to "rpm" stay as
// they are, for the tools that read the line.
inline void PowertrainProbeText(const TruckRaw &t, const PowertrainModel &m, float rpm, bool handed, float written, float game, char *out, size_t size, float x = -1.0f,
                                float roof = 0.0f, bool heldBack = false)
{
    char hand[96], terms[128] = "";
    if (handed) sprintf_s(hand, "handed to the game: the torque factor %.2f, the game's own %.2f", written, game);
    else strcpy_s(hand, "not handed to the game");
    if (x >= 0.0f) snprintf(terms, sizeof terms, " | the factor's terms: the game's pedal, the wheels at %.2f of the gear's top, the roof %.2f there, the rise %s", x, roof, heldBack ? "paced" : "free");
    snprintf(out, size, "powertrain (%s): pedal %.2f, shaped %.2f | torque %.2f of the most at %.0f rpm (curve %.2f, cold x%.2f, boost x%.2f) | "
                         "power %.2f, fuel %.2f of floored at the red zone (the model's own, by the shaped pedal)%s",
              hand, TruckPedal(t), m.pedal, m.torque, rpm, m.curve, m.cold, m.boost, m.power, m.load, terms);
}

// ---- the automatic gearbox ----

static const float kShiftUpLight = 0.35f;    // at a light foot it shifts up with the engine this far from idle to the red zone's speed
static const float kShiftDownLight = 0.12f;  // and down below this; floored, where the next gear pulls more in the game (GearUpShare, GearDownShare)
static const float kShiftRoom = 1.1f;        // at a light foot a shift up waits until the next gear's speed is this far over the point the gearbox would shift back down at
static const float kCoastDown = 0.15f, kCoastDownTo = 0.75f; // with the foot off it shifts down below this, as far as the engine stays under kCoastDownTo:
                                             // a car's nine-speed automatic, filmed on the road (2026-10-19), coasted in its gear down to
                                             // 1000 to 1300 rpm of a 4500 rpm red zone, 0.07 to 0.15 of the way from idle
static const float kCoastDownBraking = 0.25f; // and below this with the brake on, the engine braking a truck downhill
static const float kShiftPause = 1.0f;       // seconds from one shift to the next
static const float kShiftAgain = 0.5f;       // and from a shift to the next one the same way: a truck goes through its low gears fast, and
                                             // slows through them fast up a steep climb
static const float kKickdown = 0.9f;         // a pedal pushed past this kicks down at once, to the gear that pulls the most in the game
static const float kKickWindow = 0.5f;       // seconds after the push the kickdown may come in
// The converter is a truck's (review 11, realism over the tight one of 28 October): a floored engine stalls at 0.8 of
// the red zone (a truck 1760 rpm, a heavy truck 1520; Allison's converters stall at 1500 to 2000 on a heavy truck), a
// scout's 5500 rpm engine at the cap, 2600; open, the engine rises from its stall with the gearbox's input (kStallRise,
// as the converter's K-factor rises with the speed ratio), so the needle climbs through a launch; it couples at 0.87
// and slips 4 percent past that, and a launch locks once the speed ratio reaches kLockRatio, so the lock drops the
// engine no more than that. The loaded launch keeps its doubled torque: the truck's and the heavy truck's curves are
// on their plateau at the stall.
static const float kStallSpeed = 0.8f;       // the converter's stall speed as a share of the red zone's: where a floored engine stops against a standing truck
static const float kStallRpmMost = 2600.0f;  // and at the most this: a fast revving engine's converter stalls well short of its red zone
static const float kStallRise = 0.15f;       // open, the engine runs at the stall's speed plus this share of the gearbox's input
static const float kConverterSlip = 0.04f;   // an unlocked converter turns the engine at least this much faster than the gearbox's input
static const float kCouplingPoint = 0.87f;   // from this ratio of the gearbox's input speed to the engine's the converter multiplies nothing
static const float kLockRatio = 0.92f;       // an open converter locks once its speed ratio is this (a shift made locked locks again at once)
static const float kStallRatio = 2.0f;       // and at the stall it doubles the torque
static const float kCreep = 0.06f;           // what an idling engine pushes through the converter at a standstill, as a share of the most power
static const float kLockFrom = 0.5f, kUnlockBelow = 0.4f; // the converter locks from the first gear with the input at this share of the red zone's speed, and lets go below the second share of it, at a stop or for a shift's own pass (the game's own engine; with the mod's, kLockIdle and kUnlockIdle)
static const float kStopShift = 0.1f;        // under this share of the red zone's speed in the first gear the truck all but stands: the first gear at once
static const float kShiftSmooth = 0.25f;     // seconds the wheels' speed is smoothed over before the gearbox goes by it
static const float kTrendSmooth = 1.0f;      // and seconds how fast it changes is smoothed over
static const float kFootOff = 0.05f;         // a pedal under this is the foot off
static const float kFootGap = 0.3f;          // seconds a pedal has to stay down from where it was before the drop counts: the game cuts the
                                             // throttle for a moment at each shift, and now and then besides, which is no lift
static const float kKickArm = 0.3f;          // seconds the pedal has to be short of kKickdown before a push past it kicks down
static const float kKickFrom = 0.2f;         // and at this or more for those seconds: a kickdown is a push from part throttle. A press from the foot off (every
                                             // press of a key) drives on the floored points, which shift down where the truck is slow
static const float kKickPadFrom = 0.75f;     // a pad's trigger arms a kickdown only under this: one held near its end dips under kKickdown and comes
                                             // back with no push meant. Between the two the arm runs out, in the time it took to build
static const float kLabouring = 0.35f;       // rad/s each second the truck loses with the foot at kClimbPedal or more, for kLabourAfter: a climb
static const float kClimbPedal = 0.4f;       // a lighter foot that loses speed is no climb: the engine gives a fifth of its torque or less
static const float kLabourAfter = 0.5f;
static const float kClimbHold = 4.0f;        // seconds the shift points stay where floored puts them after a climb
static const float kGripSpeed = 2.5f;        // m/s the truck goes at the least for the wheels' speed against the ground's to be learned
static const float kGripSteady = 0.05f;      // and the wheels within this share of their smoothed speed and of each other, the truck gaining or losing
static const float kGripSpread = 0.15f;      // under kGripGain rad/s each second: no bump, no wheel off the ground, no start
static const float kGripGain = 1.0f;
static const float kGripFall = 2.0f, kGripRise = 20.0f; // seconds the grip takes to follow a lower reading, and a higher one
static const float kSlipUp = 1.05f;          // a shift up goes by the wheels at most this much faster than the ground: wheelspin in rough ground shifts nothing up
static const float kSlipShare = 1.2f, kSlipSpare = 1.0f; // wheels this much faster than the ground under them, and this many rad/s more, spin: the gearbox goes by the ground
                                                        // (floored in drives logged in the game the wheels turned within 1.13 times the learned grip nine times in ten)
static const float kDownMost = 0.9f;         // no shift down puts the engine, at the speed the wheels turn, further than this from idle to the red zone
static const float kShiftUpAfter = 0.4f, kShiftDownAfter = 0.5f, kCoastDownAfter = 0.3f; // seconds a shift up, a shift down and one with the foot off has to be called for before it is made
static const float kShiftUpAfterFloored = 0.15f; // and a shift up floored, where a short first gear is through in a second
static const float kPedalFall = 0.5f;        // the pedal the shift points go by follows a lifted foot down this much each second: a lift is no call to shift up
// Cruising and lifting, as the same car's automatic did: a light foot held steady shifts up as soon as the next gear
// lands at kLightPeak of the peak torque's speed or over (EngineHand::least) and kShiftRoom over its own point down,
// short of the light point up (the car cruised at 1200 to 1300 rpm against a peak at about 1600); and a lift at speed,
// the truck neither gaining nor slowing hard, shifts up one gear at a time while the next lands over the same floor and
// kShiftRoom over the coast point. Gaining with the foot off (downhill) it holds the gear for the engine's braking, and
// a truck slowing hard (mud, a climb) is no lift to shift up for.
static const float kCruisePedal = 0.5f;      // a pedal at or under this is a light foot
static const float kCruiseSteady = 0.02f;    // the truck's speed changing by no more than this share of itself each second is steady
static const float kCruiseAfter = 2.0f;      // seconds a light foot has to be held steady before it cruises
static const float kLiftSlowing = 0.04f;     // with the foot off, slowing by more than this share each second is no lift to shift up for
static const float kLiftAfter = 2.0f;        // driven with keys, seconds the key has to be let go before a lift shifts up: a key tapped to hold the speed is no lift (a tester's keyboard drive on 0.15.8: 376 of 412 lifts under a second, and 19 of 23 lift upshifts taken straight back by the next tap)
static const float kLiftBlock = 10.0f, kLiftBlockMost = 60.0f; // and with keys a lift's shift up taken back within kHuntWindow keeps lifts from shifting up this long, twice as long each time again, up to kLiftBlockMost
// The game's pull: the share of the engine's torque the game gives the wheels with them at x of the gear's top speed
// (action + 0xB4, floored and steady on the Defender and the sooty alike): all of it from about 0.45 to 0.52, falling in
// a line to nothing at the gear's top, and less under 0.45. The game multiplies the torque as a gear's top is lower (in
// 0.12.5's test the power factor doubled, a top twice as high, pulled less), so at a given speed a gear pulls x times
// GamePull(x) against the others: the most with the wheels at half the gear's top, and a gear revved near its top
// pulls a fraction of the next one's.
static const float kGamePullRise = 2.8f, kGamePullRiseFrom = 0.25f, kGamePullFall = 2.1f;
static const float kShiftPullGain = 0.05f;   // floored, a shift is made for a gear that pulls this much more

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
    bool relock;       // the converter was locked when the last shift opened it: it locks again on the next pass over the lock point
    float upAt, downAt; // for the probe (review 11): the points in force for the gear it is in, rpm on the gearbox's input,
    float lockAt, unlockAt; // the converter's lock and unlock points,
    int startGear;     // the gear it sets off in,
    bool heldNow;      // and whether the gear's hold keeps it from shifting up on this pass
    float input;       // the gearbox's input speed in rpm: what the engine turns at with the converter locked
    float rpm;         // the engine speed
    float ratio;       // how much the converter multiplies the torque, 1 to kStallRatio
    float drive;       // what goes to the wheels: the power times the ratio, and the creep, as a share of the most power
    float pause;       // seconds until it may shift again
    bool upLast;       // the last shift was up
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
    float held;        // the pedal the shifts go by: up with the foot at once, down once it has stayed down kFootGap
    float dropFor;     // seconds the pedal has been down from held
    float shortFor;    // seconds the pedal has been at part throttle: kKickFrom or more, short of kKickdown
    float kick;        // seconds the kickdown of a push may still come in
    float climb;       // seconds the shift points stay at floored's after a climb
    float stuck;       // seconds the truck has been stuck floored (kStuckShare): the crawling gears are let in
    float moving;      // seconds in the gear with the truck moving: 0 at a shift and while it stands
    bool holdUp;       // the gear came by a shift up or from a standstill: with the mod's engine it is held kEngineGearHold
    int shifts;        // shifts since the model began
    int upFrom;        // the gear the last shift up with the foot on came from, while kHuntWindow lasts; 0 for none
    float sinceUp;     // seconds since that shift up
    int keep;          // the gear a shift up taken back keeps it in: none up past it while keepFor lasts
    float keepFor;     // seconds it is still kept
    float keepWait;    // seconds the last keep lasted: the next is twice as long, up to kHuntKeepMost; 0 after a shift up that held
    int hunts;         // shifts up taken back since the model began
    float cruise;      // seconds a light foot has been held steady (kCruiseAfter)
    float liftFor;     // seconds the foot has been off (kLiftAfter)
    float liftUpFor;   // seconds a lift's shift up is still watched (kHuntWindow); 0 for none
    int liftUpFrom;    // the gear that shift up came from
    float liftBlock;   // seconds lifts still shift nothing up (kLiftBlock)
    float liftBlockWait; // seconds the last block lasted: the next is twice as long, up to kLiftBlockMost; 0 after a lift's shift up that held
};

// The hunt guard let go: a lift of the foot, a stop, a kickdown, or the gears taken anew.
inline void GearboxHuntClear(GearboxModel &m)
{
    m.upFrom = 0;
    m.keep = 0;
    m.keepFor = 0.0f;
    m.keepWait = 0.0f;
}

// The gearbox's input speed in rpm in a gear with the driven wheels at spin rad/s: the red zone's speed at the gear's top.
inline float GearboxInput(const TruckRaw &t, const GaugeScale &k, int gear, float spin)
{
    const float top = TruckGearTop(t, gear);
    return top > 0.0f ? spin / top * k.redline : 0.0f;
}

// An engine speed this far from idle to the red zone's.
inline float GearboxPoint(const GaugeScale &k, float share) { return k.idle + (k.redline - k.idle) * share; }

// The share of the engine's torque the game gives the wheels with them at x of the gear's top speed.
inline float GamePull(float x) { return isfinite(x) ? SettingsRange(fminf(kGamePullRise * x - kGamePullRiseFrom, kGamePullFall * (1.0f - x)), 0.0f, 1.0f) : 0.0f; }

// ---- the engine's torque handed to the game (Powertrain=1) ----
// With Powertrain on the mod writes the torque factor at action + 0xCC on each pass: the game's own share (what it
// leaves a damaged engine) times the engine's torque at the needle's speed (the curve; the cold oil and the boost
// where the pedal asks more than they leave) times what the converter multiplies, over what the game's curve has
// fallen to at the wheels' share of the gear's top (GameFall, by the truck's speed over the ground: wheels spinning
// in mud keep the game's fall, which holds them back), at the most kTorqueFactorMost. So the truck pulls as the mod's engine
// does: all its torque where the game's curve gives all of its own, and as much where the game's falls away toward
// the gear's top, to 0.92 of it (at 3 times it held only to 0.84: a tester's log had the factor there in 1133 of its
// readings, nearly all with the engine near the red zone), then down to nothing at the top, as a diesel's governor
// cuts the fuel between its rated speed and its high idle. At the top and past it (rolling downhill) the factor is the
// game's own: the fall would be divided by nothing there. Under half the top the game's curve is left as it is: there
// it reads low only as the engine picks up. The roof is 6 for every engine: near a gear's top the lift divides by the
// fitted fall, and where the fit is off a higher roof turns it into a kick (a d110 with a roof of 24 leapt at the top
// of 2nd to 5th gear, and a tester's RLM trucks at the roof of 6, 2026-10-25). An engine with several times a stock
// engine's torque is held back with its Torque instead.
// The roof is the truck's class's (kTorqueRoof: a scout's 3, a truck's and a heavy truck's 6) and it fades over a gear's
// last three tenths (LiftRoof): flat up to kLiftFadeFrom of the top, then in a straight line down to 1 at the top, so
// near the top the truck gets less and less of the lift and at the top the game's own fall, where its limiter lets the
// pull die. With the flat roof a vanilla Defender 90 (1776 kg, a scout) floored on a road sat at the roof of 6 from 0.85
// of each gear's top, shifted at 0.97 to 1.09 of it and went from 24 to 58 km/h in 7 seconds through 3rd, 4th and 5th
// (the user's drive, 27 October: "the super acceleration bug"). The fade began at 0.8 until 28 October: over the last
// fifth the push fell so steeply that a heavy truck held near its top gear's limit at part throttle overshot into the
// dead zone, slowed, got the push back in one jump and surged, every two seconds (a tester's log: the factor swinging
// 1.0 to 2.4, an HX 520 at 36 to 39 km/h). From 0.7 the roof falls two thirds as steeply (the roof less 1 over 0.3 of
// the top against 0.2), and the pull's own fall near 0.9 of the top a quarter less (about 9.1 a unit of x from 12.6;
// review 11); a rise after a cut comes back at kLiftRise.
static const float kTorqueFactorMost = 6.0f;    // the factor written at the most (the largest roof), for the game's own share of 1
static const float kLiftFadeFrom = 0.7f;        // the share of a gear's top from which the roof fades to 1 at the top
static const float kLiftRise = 1.0f;            // the factor a second a rising write may climb within a gear after a cut (TorqueHandStep): 1.0 to 2.4 in 1.4 s
static const float kLiftCutWindow = 2.0f;       // seconds after a cut (a write lower than the one before) a rise is paced
static const float kTorqueRoof[kClassCount] = { 6.0f, 3.0f, 6.0f }; // the roof by the truck's class: truck, scout, heavy
static const float kTorqueFactorLeast = 0.05f;  // and at the least
static const float kTorqueFactorStep = 0.005f;  // a change of the factor smaller than this is not written

// The wheels' share of the gear's top the lift goes by: by the truck's speed over the ground once the wheels' grip is
// known (the wheels at most kSlipUp over it: wheels spinning in mud keep the game's fall), and before that no more than
// kLiftBlind, where the game's curve has not begun to fall: wheels spun up from a standstill get no lift for it.
static const float kLiftBlind = 0.5f;
inline float LiftShare(float spin, float ground, float grip, float top)
{
    if (!(top > 0.0f) || !isfinite(spin)) return 0.0f;
    const float wheels = grip > 0.0f ? fminf(spin, grip * fmaxf(ground, 0.0f) * kSlipUp) : fminf(spin, kLiftBlind * top);
    return fmaxf(wheels, 0.0f) / top;
}

// What the game's curve has fallen to with the wheels at x of the gear's top: 1 up to about half, nothing at the top.
inline float GameFall(float x) { return isfinite(x) ? SettingsRange(kGamePullFall * (1.0f - x), 0.0f, 1.0f) : 0.0f; }

// The roof of a truck's class (kClassTruck, kClassScout, kClassHeavy).
inline float TorqueRoof(int tachClass) { return tachClass >= 0 && tachClass < kClassCount ? kTorqueRoof[tachClass] : kTorqueFactorMost; }

// The roof most with the wheels at x of the gear's top: most up to kLiftFadeFrom, then in a straight line down to 1 at
// the top (6: 3.5 at 0.85, 1.75 at 0.955; a scout's 3: 2 at 0.85).
inline float LiftRoof(float most, float x)
{
    if (!isfinite(x) || x <= kLiftFadeFrom) return most;
    return 1.0f + (most - 1.0f) * SettingsRange((1.0f - x) / (1.0f - kLiftFadeFrom), 0.0f, 1.0f);
}

// The factor over the game's own share, by the engine's torque curve at its speed (0 to 1), the share of it the cold
// oil and the boost leave (limit), the pedal (the game asks the engine for that share of its torque, which it has as
// far as the limit goes: at a light foot a turbo engine without boost gives what the pedal asks), the converter's
// ratio, over GameFall at x, at most the roof (most, the truck's class's: TorqueRoof; kTorqueFactorMost says why) as it
// fades over the gear's last three tenths (LiftRoof); 1 (the game's own) with the wheels at the gear's top or past it.
inline float TorqueLift(float curve, float limit, float pedal, float ratio, float x, float most = kTorqueFactorMost)
{
    if (isfinite(x) && x >= 1.0f) return 1.0f;
    const float lift = curve * ratio * fminf(1.0f, limit / fmaxf(pedal, 1e-3f)) / fmaxf(GameFall(x), 1e-3f);
    return SettingsRange(isfinite(lift) ? lift : 1.0f, kTorqueFactorLeast, fmaxf(LiftRoof(most, x), kTorqueFactorLeast));
}

// The lift as far as the setting hands the mod's engine to the game (EngineShare, 0 to 1): 0 leaves the game's own, 1
// the whole lift, and between the two the pull lies between the game's and the mod's engine's.
inline float TorqueBlend(float lift, float share) { return 1.0f + SettingsRange(share, 0.0f, 1.0f) * (lift - 1.0f); }

// An engine held back (Torque, kTorqueShareRange): the whole push the hand gives, the game's own share included,
// times the share, no lower than the factor's least; 0 (none set) or 1 leaves it as it is. For a mod's engine with
// several times a stock engine's torque, such as RLM's (a tester's trucks, 2026-10-24).
inline float TorqueHeld(float factor, float share) { return share > 0.0f && share < 1.0f ? fmaxf(kTorqueFactorLeast, factor * share) : factor; }

// What the factor's writes stand on.
struct TorqueHand
{
    bool on;           // the mod writes the factor
    uint64_t action, vehicle; // the truck it writes it for
    float game;        // the game's own share: the factor before the first write, and whatever the game writes since
    float written;     // the factor last written
    int gear;          // the game's gear on the last pass: a new one is written at once
    bool paced;        // a write has been made since the hand took the truck: rises may be paced from then on
    float cutFor;      // seconds a rise is still paced after the last cut (a write lower than the one before): kLiftCutWindow
    bool heldBack;     // this pass's rise was held back by the pace (for the probe)
};
struct TorqueOrders
{
    bool write;
    uint64_t action, vehicle; // write the factor at this action's + 0xCC, of this vehicle
    float factor;
};

// One pass. want: Powertrain on, a truck driven with its engine running, no pull test; action and vehicle: the driven
// truck's; read: the factor as read at its action now; lift: TorqueLift. With want gone, the game's own share is
// written back once, where the factor was written (the caller checks that truck is still there). dt: the pass's
// seconds, and gear the game's gear: within a gear and within kLiftCutWindow of a cut (a write lower than the one
// before) a factor higher than the last written one climbs toward it by kLiftRise a second (at least
// kTorqueFactorStep a pass, so it is written), so after an overshoot into the fade the push comes back gradually and the
// truck settles where it matches the drag; an ordinary rise through a gear, with no cut before it, is written at once
// (review 11: the pace held a quick truck's rising lift back late in each short gear), as are a lower one, a new gear's
// and the first write after the hand takes the truck. dt 0 writes every factor at once.
inline TorqueOrders TorqueHandStep(TorqueHand &h, bool want, uint64_t action, uint64_t vehicle, float read, float lift, float dt = 0.0f, int gear = 0)
{
    TorqueOrders o = {};
    const float share = isfinite(read) && read > 0.0f ? fminf(read, 1.0f) : 1.0f;
    // a rebuilt action that reads the factor the mod wrote last carries the mod's own value, not the game's share (the
    // game seeds it from the truck as it stands): the game's share is kept then. Read as a share, a factor above 1 lost
    // the damage cut and one below 1 sank with each rebuild (review 5, G-R1, 2026-10-24).
    const bool carries = h.on && isfinite(read) && fabsf(read - h.written) <= 1e-4f;
    const float carried = carries ? h.game : share;
    if (!want || !action || (h.on && h.action != action))
    {
        if (h.on) o = TorqueOrders{ true, h.action, h.vehicle, h.game };
        h = TorqueHand{};
        if (!want || !action) return o;
    }
    if (!h.on) h = TorqueHand{ true, action, vehicle, carried, carries ? read : -1.0f }; // what it reads is what the mod wrote: no write due
    else if (fabsf(read - h.written) > 1e-4f) h.game = share; // the game wrote its own, as engine damage moved it
    float factor = h.game * lift;
    const bool newGear = gear != h.gear;
    h.gear = gear;
    if (h.cutFor > 0.0f) h.cutFor -= dt;
    h.heldBack = false;
    if (dt > 0.0f && h.paced && h.cutFor > 0.0f && !newGear && factor > h.written)
    {
        const float paced = fminf(factor, h.written + fmaxf(kLiftRise * dt, kTorqueFactorStep));
        h.heldBack = paced < factor;
        factor = paced;
    }
    if (!o.write && fabsf(factor - h.written) >= kTorqueFactorStep)
    {
        if (h.paced && factor < h.written && !newGear) h.cutFor = kLiftCutWindow;
        o = TorqueOrders{ true, action, vehicle, factor };
        h.written = factor;
        h.paced = true;
    }
    return o;
}

// With the mod's engine handed to the game (Powertrain=1) the shift points are a truck automatic's schedule. Up: as far
// from idle toward the floored point as the pedal is down (a fifth of the pedal, a fifth of the way: a scout's diesel
// at about 1500 rpm, as a car's nine-speed automatic shifts), but not under kLightPeak of the
// engine's peak torque speed, where it begins to pull; floored where the next gear pulls more (EngineUpShare: at
// EngineShare 50 a scout's road gearbox at 0.86 to 0.87 of a gear's top, at 100 at 0.90 to 0.92; with the roof of 6 a
// ZIL-131's at 0.945 to 0.955), or where none does on the limiter
// (kEngineLimit). Until 0.15.10 it also went no further than kEngineUpFloored, a rule from before the fade toward a
// gear's top was worked out, which with the roof of 6 sat just over where the next gear pulls more. Down: where the gear below would come in kEngineDownRoom under its own point up, so no shift is taken
// back whatever the gearbox's steps, and not under idle's kEngineDownIdle: a truck's part-throttle points land at about
// 0.55 to 0.75 of its governed speed and a modern diesel pulls cleanly only to about 1000 to 1100 rpm, so at a light
// foot it shifts down at 1.6 times idle (a truck about 1040 rpm) and the converter locks from kLockIdle (1070) and
// lets go with the foot on under kUnlockIdle (910), never by the pedal, so a press never opens it: a press that needs
// more pull gets a shift down by the schedule or a kickdown (review 11; before, it locked from 1.3 times idle, shifted
// down at 1.15 and let go under 0.8 of a lock point that rose with the pedal, so a press from a low cruise flared it).
// With the foot off it keeps the lock down to kUnlockCoast, for the engine's braking.
static const float kEngineUpFloored = 0.95f;   // floored, up this far from idle to the red zone's speed where there is no curve to go by
static const float kLightPeak = 0.8f;          // at a light foot, up no sooner than this share of the speed where the curve first reaches kLightReach
static const float kLightReach = 0.9f;         // of its most, and no later than kLightMost of the red zone: a scout's curve reaches 0.98 only at 0.75
static const float kLightMost = 0.45f;         // of the red zone, which kept a scout from cruising under 3300 rpm (review 11)
static const float kEngineDownRoom = 1.15f, kEngineDownIdle = 1.6f; // the light foot's; floored kEngineDownIdleFloored, as before, so a
static const float kEngineDownIdleFloored = 1.15f;                    // floored shift up across a wide step (1.9) still lands over it
static const float kLockIdle = 1.65f;          // with the mod's engine the converter locks from this times idle up to kLockLight of the pedal,
static const float kLockLight = 0.3f;          // and from there up to kLockFrom of the red zone floored: a light foot's shift up lands locked
static const float kUnlockIdle = 1.4f;         // with the mod's engine, locked it lets go with the foot on under this times idle,
static const float kUnlockCoast = 1.04f;       // and with the foot off under this, for the engine's braking on a coast
static const float kEngineKickTo = 0.85f;   // the kickdown goes to the lowest gear that keeps the engine under this,
static const float kKickRoom = 0.92f;       // and under this share of the gear's floored point up, so it is not left again at once
static const float kEngineLimit = 0.98f;    // past this the engine is on the limiter: up at once, after kLimiterHold of the gear's hold
static const float kLimiterHold = 0.9f;     // seconds of moving a gear is held on the limiter at the least (review 11: the rest of the hold sat a light truck on the governor)

// The engine speed in rpm a light foot does not shift up under (EngineHand::least): kLightPeak of where the curve first
// reaches kLightReach of its most, at the most kLightMost of the red zone (a truck's class curve 880 rpm, a scout's 2370).
inline float EngineLightFloor(const TorqueCurve &c, float redline) { return fminf(kLightPeak * CurveReach(c, kLightReach), kLightMost) * redline; }
// Going up through the gears, from a standstill or from a shift up, each gear is held this many seconds of moving,
// on the limiter if it comes to that (the game governs the wheels at the gear's top): an empty Fleetstar went 1 to 5
// in two seconds and pulled like a late truck; driven in the game, two to three seconds a gear felt right. A gear a shift down or a
// kickdown brought is not held. A scout shifts sooner (kEngineGearHoldScout): at 2.5 s the scout of the drives, a light
// 4x4, sat in its gears too long for its kind (driven in the game, 2026-10-11), where trucks and heavy trucks were right.
static const float kEngineGearHold = 2.5f, kEngineGearHoldScout = 1.5f;
// A shift up the gear above cannot hold: up a grade or through mud the next gear loses speed and comes back down, and
// the gear below is at its point up again a second later (a tester's drives on 0.15.0: a Tayga swapped its first
// and second 18 times in six minutes, another truck every second up a climb). A truck automatic's grade logic keeps the
// lower gear: a shift up taken back down with the foot on within kHuntWindow keeps the gear it left, with no shift up
// past it, the limiter's neither (the engine sits at its governed speed, as a loaded truck's does on a hill), for
// kHuntKeep, then one more try; each try taken back doubles the wait, up to kHuntKeepMost. A lift of the foot, a stop
// or a kickdown ends it, and so does a shift up that holds past kHuntWindow.
static const float kHuntWindow = 6.0f, kHuntKeep = 8.0f, kHuntKeepMost = 30.0f;
// A game's gearbox has its first gears for crawling: on a scout's road gearbox the first can be an eighth of the top
// gear's speed, where a real one's is a fifth to a seventh. With the mod's engine the automatic sets off in the lowest gear at
// least kStartShare of the top gear's (as a truck automatic starts in the second), and takes a lower one only stuck:
// floored with the truck under kStuckShare of that gear's top and not gaining for kStuckAfter.
static const float kStartShare = 1.0f / 7.0f;
// And lower while the engine is weak for the truck: the engine's Torque over the truck's mass (both from its files)
// over the gear's top under kStartPull. A scout of 9007 kg with 195000 has 1.67 in a second gear of 13 rad/s and sets
// off in it; with 100000, 0.85, and sets off in the first (1.39 in a first of 8).
static const float kStartPull = 1.2f;
static const float kStuckShare = 0.3f, kStuckGain = 0.5f, kStuckAfter = 1.0f;
// The class a truck without scales of its own goes by in the automatic (each gear's hold, and the engine's curve where
// it has no profile of its own), by its mass (TruckMass: without addons, cargo or a trailer): the game's scouts weigh
// up to 6 t (a TUZ 108 4.1 t), its trucks under 16 t (a Tayga 12.0 t), its heavy trucks more (a Western Star 6900
// twin 20.3 t). A tester's plain scales were a scout's, and every truck without its own shifted like one. The weight
// cannot tell every scout from a truck: the Fleetstar (6314 kg) is a truck, and a modded scout of 9007 kg shifts as a
// truck until scales of its own name it a scout.
static const float kScoutMassMost = 6000.0f, kHeavyMassLeast = 16000.0f;

// The class of a truck of kg by its files; plain where the mass is not known.
inline int ClassByMass(float kg, int plain)
{
    if (!(kg > 0.0f) || !isfinite(kg)) return plain;
    return kg <= kScoutMassMost ? kClassScout : kg < kHeavyMassLeast ? kClassTruck : kClassHeavy;
}

// The gear the automatic sets off in with the mod's engine: the lowest whose top is at least kStartShare of the top
// gear's, and lower while launch (the engine's Torque over the truck's mass; 0 where either is not known) over the
// gear's top is under kStartPull.
inline int GearboxStartGear(const TruckRaw &t, float launch = 0.0f)
{
    const int top = TruckTopGear(t);
    const float most = TruckGearTop(t, top);
    int gear = 1;
    while (gear < top && TruckGearTop(t, gear) < most * kStartShare) gear++;
    if (launch > 0.0f && isfinite(launch))
        while (gear > 1 && launch < kStartPull * TruckGearTop(t, gear)) gear--;
    return gear;
}

// What a gear pulls with the wheels at x of its top in the game, against the other gears at the same speed.
inline float GearPull(float x) { return x * GamePull(x); }

// x of a gear's top where the next gear, r times as tall, pulls kShiftPullGain more: the floored shift up with the
// game's own engine. 1 for a next gear no taller.
inline float GearUpShare(float r)
{
    if (!(r > 1.01f)) return 1.0f;
    for (float x = 0.5f; x < 1.0f; x += 0.005f)
        if (GearPull(x / r) >= GearPull(x) * (1.0f + kShiftPullGain)) return x;
    return 1.0f;
}

// x of a gear's top under which the gear below, r times shorter, pulls kShiftPullGain more: the floored shift down.
inline float GearDownShare(float r)
{
    if (!(r > 1.01f)) return 0.0f;
    for (float x = 0.6f; x > 0.1f; x -= 0.005f)
        if (x * r < 1.0f && GearPull(x * r) >= GearPull(x) * (1.0f + kShiftPullGain)) return x;
    return 0.0f;
}

// The mod's engine as the automatic's shift points go by it while it is handed to the game (Powertrain=1): its torque
// curve, the tachometer's top over the red zone's (where the governor leaves nothing), how much of it the game gets
// (EngineShare, 0 to 1), the engine's Torque over the truck's mass (0 where not known; GearboxStartGear), the
// seconds each gear is held going up (0: kEngineGearHold), and the engine speed in rpm under which a light foot does
// not shift up (kLightPeak of the peak torque's; 0: idle). on false: the game's own engine.
struct EngineHand
{
    bool on;
    const TorqueCurve *curve;
    float top, share, launch, hold, least;
    int input; // what the pedal is (kInputWheel, kInputPad, kInputKeys): the kickdown goes by it, with the engine handed over or not
    float roof = kTorqueFactorMost; // the lift's roof, the truck's class's (TorqueRoof)
};

// What a gear pulls with the wheels at x of its top with the engine handed over, floored, the converter locked: the
// game's curve as far as the factor lifts it (TorqueLift, TorqueBlend), against the other gears at the same speed.
// As the roof fades over a gear's last fifth (LiftRoof) the pull dies down toward the top, so the floored point up
// (EngineUpShare) comes earlier by itself.
inline float EngineGearPull(const EngineHand &e, float x)
{
    if (!e.curve) return GearPull(x);
    return x * GamePull(x) * TorqueBlend(TorqueLift(TorqueAt(*e.curve, x, e.top), 1.0f, 1.0f, 1.0f, x, e.roof), e.share);
}

// x of a gear's top where the next gear, r times as tall, pulls kShiftPullGain more with the engine handed over: the
// game's curve falls toward a gear's top whatever the factor, so floored a truck may never reach the schedule's
// point, and the next gear pulls more short of it. 1 where it never does.
inline float EngineUpShare(const EngineHand &e, float r)
{
    if (!(r > 1.01f)) return 1.0f;
    for (float x = 0.5f; x < 1.0f; x += 0.005f)
        if (EngineGearPull(e, x / r) >= EngineGearPull(e, x) * (1.0f + kShiftPullGain)) return x;
    return 1.0f;
}

// The gearbox's input speed a gear shifts up at, with feel the pedal the points go by: the light point at a light foot,
// the floored one floored, and between them between. engine: the mod's engine is handed to the game, and the point is
// the schedule's: as far from idle toward the floored point (where the next gear pulls more, EngineUpShare, or
// kEngineUpFloored without a curve) as the pedal is down, not under engine.least; else the floored point is where the next gear pulls
// more in the game (GearUpShare).
inline float GearboxUpPoint(const TruckRaw &t, const GaugeScale &k, int gear, float feel, const EngineHand &engine = EngineHand{})
{
    const float low = TruckGearTop(t, gear), high = TruckGearTop(t, gear + 1);
    if (engine.on)
    {
        const float least = fmaxf(engine.least, k.idle);
        float hard = engine.curve && low > 0.0f && high > 0.0f ? EngineUpShare(engine, high / low) * k.redline : GearboxPoint(k, kEngineUpFloored);
        hard = fmaxf(hard, least);
        return fmaxf(k.idle + (hard - k.idle) * SettingsRange(feel, 0.0f, 1.0f), least);
    }
    const float light = GearboxPoint(k, kShiftUpLight);
    const float floored = low > 0.0f && high > 0.0f ? fmaxf(GearUpShare(high / low) * k.redline, light) : light;
    return light + (floored - light) * SettingsRange(feel, 0.0f, 1.0f);
}

// The gearbox's input speed a gear shifts down below, the same way. engine: where the gear below would come in
// kEngineDownRoom under its own point up at this pedal, and not under kEngineDownIdle times idle; else GearDownShare's.
inline float GearboxDownPoint(const TruckRaw &t, const GaugeScale &k, int gear, float feel, const EngineHand &engine = EngineHand{})
{
    if (engine.on)
    {
        const float below = TruckGearTop(t, gear - 1), here = TruckGearTop(t, gear);
        if (gear < 2 || !(below > 0.0f) || !(here > 0.0f)) return 0.0f;
        const float floor = kEngineDownIdle + (kEngineDownIdleFloored - kEngineDownIdle) * SettingsRange(feel, 0.0f, 1.0f);
        return fmaxf(GearboxUpPoint(t, k, gear - 1, feel, engine) * below / here / kEngineDownRoom, k.idle * floor);
    }
    const float light = GearboxPoint(k, kShiftDownLight), low = TruckGearTop(t, gear - 1), high = TruckGearTop(t, gear);
    const float floored = low > 0.0f && high > 0.0f ? fmaxf(GearDownShare(high / low) * k.redline, light) : light;
    return light + (floored - light) * SettingsRange(feel, 0.0f, 1.0f);
}

// One step of dt seconds. pedal: the pedal as the driver holds it (TruckPedal), 0 with the engine off; the shift points
// go by where the foot is, not by the powertrain's curve, under which a fifth of the pedal reads as nothing. power:
// the power before the converter, from the powertrain (PowertrainModel). In the high gear, the low range, neutral and reverse the game's gearbox is left as it is, and the
// model only follows the wheels.
//
// The shifts go by the wheels' speed, as a truck's automatic goes by its output shaft, but neither wheelspin in mud nor
// wheels locked under the brake shift it, once the wheels' speed against the ground's is learned. With the mod's engine
// the first gear (and the start gear) differs: its shift up goes by the wheels spinning or not, as the game's pull
// falls by the wheel speed and a spinning first gear would sit until the ground caught up, pulling almost nothing. No shift down puts
// the engine near the red zone at the speed the wheels turn. The shift points go by a pedal that falls slowly after a
// lift, so a foot floored and lifted by turns does not shift up at each lift: at a light foot between idle and the red
// zone, floored where the next gear or the one below pulls more in the game (GamePull), which gives the most with the
// wheels at half the gear's top. With the foot off it shifts up only on a lift at speed, and shifts down early,
// gears at a time when the truck slows hard. With the engine handed to the game (Powertrain=1, engine) the points are
// a truck automatic's schedule (kLightPeak and the rest): floored it revs to the red zone, at a light foot it
// shifts early, down well under where the gear above lands, one gear up at a time and a second in each but on the
// limiter, each gear held kEngineGearHold going up, and a kickdown to the lowest gear under kEngineKickTo; it sets off in the start gear (GearboxStartGear) and
// takes the gears under it only stuck (kStuckShare). A truck slowing with the foot on is climbing: the points go where floored
// puts them, and stay there kClimbHold after, so it shifts down in time; a shift up the next gear cannot hold, taken
// back within kHuntWindow, keeps the gear below kHuntKeep and longer, so it does not hunt. A push of the pedal from
// part throttle (kKickFrom or more for kKickArm) past kKickdown kicks down at once to the gear that pulls the most; a
// press from the foot off does not. By the input in use (hand.input): a pad's trigger kicks down only from under
// kKickPadFrom, and keys kick nothing down, shift as floored whenever one is held, and shift up at a lift only once
// it has lasted kLiftAfter, and none for kLiftBlock after such a shift up was taken back (a key tapped to hold the speed).
inline void GearboxStep(GearboxModel &m, const TruckRaw &t, const GaugeScale &k, float pedal, float power, float dt, const EngineHand &hand = EngineHand{})
{
    const bool engine = hand.on;
    const int top = TruckTopGear(t);
    const float raw = TruckDrivenSpin(t), pedalNow = SettingsRange(pedal, 0.0f, 1.0f);
    float p = pedalNow; // the shifts' pedal; the converter goes by pedalNow
    if (hand.input == kInputKeys && p >= kFootOff) p = 1.0f; // keys have no part throttle: a key held is the pedal floored, however the game ramps it
    if (!m.running)
    {
        m = GearboxModel{ true };
        m.spin = m.by = raw;
        m.pedal = m.held = p;
    }
    // a drop of the pedal counts once it has lasted kFootGap: the game's throttle cut at a shift is no lift
    if (p >= m.held - 0.05f || m.dropFor >= kFootGap)
    {
        m.held = p;
        m.dropFor = 0.0f;
    }
    else
    {
        m.dropFor += dt;
        p = m.held;
    }
    if (m.pause > 0.0f) m.pause -= dt;
    m.feel = fmaxf(p, m.feel - kPedalFall * dt);
    // a push from part throttle kicks down. A pad's trigger has to come from under kKickPadFrom; keys kick nothing down
    const float partTo = hand.input == kInputPad ? kKickPadFrom : kKickdown;
    if (hand.input != kInputKeys && p >= kKickdown && m.pedal < kKickdown && m.shortFor >= kKickArm) m.kick = kKickWindow;
    else if (m.kick > 0.0f) m.kick -= dt;
    if (p >= kKickFrom && p < partTo) m.shortFor = fminf(m.shortFor + dt, 2.0f * kKickArm);
    else if (p >= partTo && p < kKickdown) m.shortFor = fmaxf(0.0f, m.shortFor - dt);
    else m.shortFor = 0.0f;
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
    // the speed the wheels will turn at after a shift down: the ground's, and the wheels' as far as they do not spin
    const float coast = m.grip > 0.0f ? fmaxf(raw, onGround) : m.spin;
    const float rolling = m.grip > 0.0f ? fmaxf(onGround, fminf(m.spin, onGround * kSlipShare + kSlipSpare)) : m.spin;
    m.spinning = m.grip > 0.0f && m.spin > onGround * kSlipShare + kSlipSpare;
    m.rolling = rolling;
    // where it would shift at this pedal, at floored's while climbing
    const bool footOff = p < kFootOff;
    m.liftFor = footOff ? m.liftFor + dt : 0.0f;
    m.labour = p >= kClimbPedal && m.trend < -kLabouring ? m.labour + dt : 0.0f;
    const bool labouring = m.labour >= kLabourAfter;
    if (labouring) m.climb = kClimbHold;
    else if (m.climb > 0.0f) m.climb -= dt;
    const float feel = m.climb > 0.0f ? 1.0f : m.feel;
    // with the mod's engine the lowest gear it shifts down to is the start gear (GearboxStartGear), and the first only
    // stuck: floored under kStuckShare of the start gear's top, gaining less than kStuckGain for kStuckAfter. Stuck
    // lasts until the foot comes off or the truck is past that speed
    const int start = engine ? GearboxStartGear(t, hand.launch) : 1;
    const bool slow = m.by < kStuckShare * TruckGearTop(t, start);
    if (!engine || footOff || !slow) m.stuck = 0.0f;
    else if (m.stuck >= kStuckAfter || (p >= kKickdown && m.trend < kStuckGain)) m.stuck += dt;
    else m.stuck = 0.0f;
    const int least = m.stuck >= kStuckAfter ? 1 : start;
    // the brake on with the foot off shifts down early (kCoastDownBraking), for the engine's braking. It lands low for an
    // engine brake (a truck at about 1040 rpm, where a driver descending keeps 1500 to 1900 for a Jake brake): the
    // game's engine braking does not go by the engine's speed, so this matters only once the mod has an engine brake of
    // its own, and the point should rise with it then (review 11)
    const bool braking = TruckBrake(t) > 0.0f || t.handbrake;
    const float coastDown = GearboxPoint(k, braking ? kCoastDownBraking : kCoastDown), coastTo = GearboxPoint(k, kCoastDownTo), most = GearboxPoint(k, kDownMost);
    const float by = footOff ? coast : pace, now = GearboxInput(t, k, m.gear, by), lower = GearboxInput(t, k, m.gear - 1, by);
    // a shift up with the foot on, from the second gear up, goes by the ground: wheels spinning in mud or on rocks pull no
    // harder in a taller gear. With the mod's engine, in the gears it sets off in (the first, and the start gear), it
    // goes by the wheels as they turn, spinning or not: the game's pull falls by the wheel speed, so a spinning first
    // gear gets almost none and gains nothing from staying, and the next gear turns the wheels slower at the same engine
    // speed (a tester's log, 27 October: a ZIL-157 on dirt sat in first 10 to 40 s, its wheels near the gear's top).
    // The gear's hold (kEngineGearHold) still gives a launch with wheelspin its seconds there, and a truck going under
    // kStuckShare of the start gear's top (slow: stuck, spinning where it stands) keeps the ground's rule, so it does
    // not shift up and back. The wheels as they turn now, not smoothed: a scout's first gear is through in a second, a
    // quarter of it in the smoothing's lag. The limiter goes by this too (pastIn)
    const bool setOff = engine && m.gear <= start && !slow;
    const float byUp = footOff || m.grip <= 0.0f ? by : setOff ? raw : fminf(raw, onGround * kSlipUp);
    // a lower gear the engine can take at the speed the wheels turn
    auto fits = [&](int gear) { return GearboxInput(t, k, gear, rolling) < most; };
    // what a gear pulls in the game at this speed, against the others
    auto pulls = [&](int gear) { return GearPull(GearboxInput(t, k, gear, by) / k.redline); };
    int to = m.gear;
    bool liftUp = false; // with keys, a lift's shift up (lifted, not cruising)
    // the room a shift up leaves over the next gear's point down: kShiftRoom at a light foot, and with the game's own
    // engine none floored, where the points lie where one gear pulls kShiftPullGain more than the other, either way
    const float room = engine ? kShiftRoom : kShiftRoom + (1.0f - kShiftRoom) * feel;
    // a gear the engine is past the red zone in, or, the grip known, past what it can take at the speed the truck goes
    // (the floor the takeover keeps the game's gear over, GearboxLowestFit); with the mod's engine, on the limiter, by
    // the wheels as they turn too (byUp): the smoothed speed lags a quick truck's by a fifth of a short gear
    const float limit = engine ? GearboxPoint(k, kEngineLimit) : k.redline;
    auto pastIn = [&](int gear) {
        return engine ? GearboxInput(t, k, gear, fmaxf(by, byUp)) >= limit || (m.grip > 0.0f && GearboxInput(t, k, gear, rolling) >= limit)
                      : GearboxInput(t, k, gear, by) > k.redline || (m.grip > 0.0f && !fits(gear));
    };
    // that, or a gear the pedal would shift up from
    auto upFrom = [&](int gear) {
        return pastIn(gear) ||
               (!footOff && !labouring && GearboxInput(t, k, gear, byUp) >= GearboxUpPoint(t, k, gear, feel, hand) &&
                GearboxInput(t, k, gear + 1, byUp) >= GearboxDownPoint(t, k, gear + 1, feel, hand) * room);
    };
    // the gear a shift up lands in: gears at a time while the next one would shift up at once too, as a close-ratio
    // box goes through its gears faster than a shift a second; with the mod's engine one at a time, but past the limiter
    auto upTo = [&](int gear) {
        int g = gear + 1;
        while (g < top && !(engine && m.keepFor > 0.0f && g >= m.keep) && (engine ? pastIn(g) : upFrom(g))) g++;
        return g;
    };
    // with the foot on, a gear the pedal would shift down from, the one below taking the engine and not shifting
    // straight back up; a shift down lands as far down as the gears would each shift down at once too
    auto downFrom = [&](int gear) {
        return gear > least && fits(gear - 1) && GearboxInput(t, k, gear, by) < GearboxDownPoint(t, k, gear, feel, hand) &&
               GearboxInput(t, k, gear - 1, by) < GearboxUpPoint(t, k, gear - 1, feel, hand);
    };
    auto downTo = [&](int gear) {
        int g = gear - 1;
        while (g > least && downFrom(g)) g--;
        return g;
    };
    // with the mod's engine, a gear a kickdown may land in: under kEngineKickTo, and short of its own floored point up
    auto kickable = [&](int gear) {
        return GearboxInput(t, k, gear, by) < fminf(GearboxPoint(k, kEngineKickTo), GearboxUpPoint(t, k, gear, 1.0f, hand) * kKickRoom);
    };
    // cruising: a light foot held steady kCruiseAfter, no climb. A lift at speed: the foot off (with keys for kLiftAfter,
    // and lifts not blocked, kLiftBlock: a key tapped to hold the speed is no lift), no brake, the truck neither gaining
    // (downhill: the engine brakes it) nor slowing hard (mud, a climb). Either shifts up one gear where
    // the next lands over the floor: kLightPeak of the peak torque's speed (hand.least), and kShiftRoom over the next
    // gear's point down, with the foot off the coast point
    const float pace1 = fmaxf(m.by, 1.0f);
    const bool even = fabsf(m.trend) <= kCruiseSteady * pace1;
    m.cruise = !footOff && p <= kCruisePedal && m.feel <= kCruisePedal && even && !labouring && m.climb <= 0.0f ? m.cruise + dt : 0.0f;
    const bool cruising = m.cruise >= kCruiseAfter;
    const bool keys = hand.input == kInputKeys;
    const bool lifted = footOff && (!keys || (m.liftFor >= kLiftAfter && m.liftBlock <= 0.0f)) && !braking && m.trend <= kCruiseSteady * pace1 && m.trend >= -kLiftSlowing * pace1;
    auto easyUp = [&](int gear) {
        if (gear >= top || !(cruising || lifted)) return false;
        const float next = GearboxInput(t, k, gear + 1, footOff ? by : byUp);
        return next >= fmaxf(hand.least, (footOff ? coastDown : GearboxDownPoint(t, k, gear + 1, feel, hand)) * kShiftRoom);
    };
    const bool pedalUp = m.gear < top && upFrom(m.gear);
    const bool wantUp = pedalUp || easyUp(m.gear);
    const bool pastIt = m.gear < top && pastIn(m.gear);
    const bool wantDown = footOff ? m.gear > least && fits(m.gear - 1) && now < coastDown && lower < coastTo : downFrom(m.gear);
    m.wanted = wantUp ? fmaxf(m.wanted, 0.0f) + dt : wantDown ? fminf(m.wanted, 0.0f) - dt : 0.0f;
    // all but standing, the first gear (the start gear) at once, whatever the pause: an automatic is in it by the time
    // the truck stops, also after a hard stop that left no time for the shifts down one by one
    const bool standing = GearboxInput(t, k, 1, fmaxf(raw, m.by)) < k.redline * kStopShift;
    if (standing)
    {
        m.moving = 0.0f;
        m.holdUp = true;
    }
    else m.moving += dt;
    // with the mod's engine, a gear come by a shift up or from a standstill is held kEngineGearHold
    const bool held = engine && m.holdUp && m.moving < (hand.hold > 0.0f ? hand.hold : kEngineGearHold);
    // the hunt guard (kHuntWindow): a shift up that held past the window clears the wait; a lift of the foot or a stop
    // lets go. Kept, no shift up past the gear kept, the limiter's neither, but halfway into the red zone (rolling faster
    // down a slope than the engine can take) it goes up all the same
    if (m.upFrom)
    {
        m.sinceUp += dt;
        if (m.sinceUp > kHuntWindow)
        {
            m.upFrom = 0;
            m.keepWait = 0.0f;
        }
    }
    if (m.keepFor > 0.0f) m.keepFor -= dt;
    if (footOff || standing) GearboxHuntClear(m);
    // with keys a lift's shift up is watched kHuntWindow too: one that held past it lets the block's wait go; a stop ends both
    if (m.liftBlock > 0.0f) m.liftBlock -= dt;
    if (m.liftUpFor > 0.0f)
    {
        m.liftUpFor -= dt;
        if (m.liftUpFor <= 0.0f) m.liftBlockWait = 0.0f;
    }
    if (standing) m.liftUpFor = m.liftBlock = m.liftBlockWait = 0.0f;
    const bool kept = engine && m.keepFor > 0.0f && m.gear >= m.keep;
    const bool overRed = GearboxInput(t, k, m.gear, fmaxf(by, byUp)) >= (k.redline + fmaxf(k.max, k.redline)) * 0.5f;
    if (m.gear != least && standing)
        to = least;
    else if (m.kick > 0.0f && m.gear > least && fits(m.gear - 1) &&
             (engine ? kickable(m.gear - 1) : pulls(m.gear - 1) >= pulls(m.gear) * (1.0f + kShiftPullGain)))
    {
        // a kickdown, at once and whatever the pause: gears at a time, down to the one that pulls the most (with the
        // mod's engine, the lowest under kEngineKickTo)
        while (to > least && fits(to - 1) &&
               (engine ? kickable(to - 1) : pulls(to - 1) >= pulls(to) * (1.0f + kShiftPullGain)))
            to--;
        m.kick = 0.0f;
        GearboxHuntClear(m); // the driver's call: the gear kept, if any, is let go
    }
    else if (pastIt && (!held || m.moving >= kLimiterHold) && (!kept || overRed))
        to = upTo(m.gear); // past the red zone or what the engine can take: up at once, whatever the pause
    else if (!held && !kept && m.pause <= (m.upLast && !engine ? kShiftPause - kShiftAgain : 0.0f) &&
             m.wanted >= kShiftUpAfter + (kShiftUpAfterFloored - kShiftUpAfter) * feel)
    {
        to = pedalUp ? upTo(m.gear) : m.gear + 1; // cruising or lifted: one gear
        liftUp = keys && !pedalUp && footOff;
    }
    else if (m.pause <= 0.0f && footOff && m.wanted <= -kCoastDownAfter)
    {
        // coasting down: as far as the engine stays under coastTo, gears at a time when the truck slows hard
        to = m.gear - 1;
        while (to > least && GearboxInput(t, k, to, by) < coastDown && GearboxInput(t, k, to - 1, by) < coastTo && fits(to - 1)) to--;
    }
    else if (m.pause <= (m.upLast ? 0.0f : kShiftPause - kShiftAgain) && !footOff && m.wanted <= -kShiftDownAfter)
        to = downTo(m.gear);
    const bool shifting = to != m.gear; // the converter opens for this pass and may lock again from the next
    if (shifting)
    {
        // with keys a lift's shift up is watched kHuntWindow: taken back down in that time (the key pressed again, the
        // brake), lifts shift nothing up kLiftBlock, twice as long each time again
        if (liftUp)
        {
            m.liftUpFor = kHuntWindow;
            m.liftUpFrom = m.gear;
        }
        else if (to < m.gear && m.liftUpFor > 0.0f && to <= m.liftUpFrom)
        {
            m.liftBlockWait = m.liftBlockWait > 0.0f ? fminf(m.liftBlockWait * 2.0f, kLiftBlockMost) : kLiftBlock;
            m.liftBlock = m.liftBlockWait;
            m.liftUpFor = 0.0f;
        }
        // the hunt guard: a shift up with the foot on is watched for kHuntWindow; taken back down with the foot on in
        // that time, the gear it left is kept (or the one it comes down to, where that is higher)
        if (engine && to > m.gear && !footOff)
        {
            m.upFrom = m.gear;
            m.sinceUp = 0.0f;
        }
        else if (engine && to < m.gear && m.upFrom && !footOff)
        {
            m.keep = to > m.upFrom ? to : m.upFrom;
            m.keepWait = m.keepWait > 0.0f ? fminf(m.keepWait * 2.0f, kHuntKeepMost) : kHuntKeep;
            m.keepFor = m.keepWait;
            m.upFrom = 0;
            m.hunts++;
        }
        else if (to < m.gear) m.upFrom = 0;
        m.upLast = to > m.gear;
        m.holdUp = to > m.gear || standing;
        m.moving = 0.0f;
        m.gear = to;
        m.relock = m.locked;
        m.locked = false;
        m.pause = kShiftPause;
        m.wanted = 0.0f;
        m.shifts++;
    }
    // the converter: locked, or open with the engine ahead of the gearbox's input. Allison's sequence (review 11): a launch
    // runs through the converter, the engine rising from its stall with the input (kStallRise), and locks once the
    // input is past the lock point and the speed ratio past kLockRatio, so the lock drops the engine little; a shift
    // made locked opens it for its own pass and locks again on the next one over the lock point, so from there up the
    // gears go lock to lock (the shifts' spacing, m.pause, does not hold it open). A lift keeps it locked, as a truck's
    // automatic does: the engine follows the wheels down, and the game's own engine braking slows the truck in the gear
    // held (the mod adds no braking of its own). Before d0940b4 it opened on every lift and every shift and never locked
    // in first: the truck felt like a CVT or a rubber band (two players' reports, 28 October). With the mod's engine the
    // lock point rises with the pedal but the unlock point does not (kUnlockIdle with the foot on, kUnlockCoast off), so
    // a press never opens it (review 11: from a low cruise a press lifted the unlock point over the input and flared
    // the engine); with the game's own engine both are shares of the red zone, as before
    m.input = GearboxInput(t, k, m.gear, m.spin);
    const float lockAt = engine ? k.idle * kLockIdle + (k.redline * kLockFrom - k.idle * kLockIdle) * SettingsRange((m.feel - kLockLight) / (1.0f - kLockLight), 0.0f, 1.0f)
                                : k.redline * kLockFrom;
    const float unlockAt = engine ? k.idle * (footOff ? kUnlockCoast : kUnlockIdle) : k.redline * kUnlockBelow;
    const float stall = k.idle + (ConverterStall(k) - k.idle) * pedalNow;
    const float open = fmaxf(fmaxf(m.input * (1.0f + kConverterSlip), stall + kStallRise * m.input), k.idle);
    // the speed ratio waited for only in the gears it sets off in (the first, the start gear): from there up it locks
    // over the lock point, as Allison's 2C-2L and lock to lock from the third
    const bool coupled = m.gear > start || m.input >= open * kLockRatio;
    if (!m.locked && !shifting && m.input >= lockAt && (coupled || m.relock)) m.locked = true;
    else if (m.locked && m.input < unlockAt) m.locked = false;
    if (!shifting) m.relock = false;
    m.rpm = m.locked ? fmaxf(m.input, k.idle) : open;
    // what the rules went by, for the probe
    m.upAt = GearboxUpPoint(t, k, m.gear, feel, hand);
    m.downAt = GearboxDownPoint(t, k, m.gear, feel, hand);
    m.lockAt = lockAt;
    m.unlockAt = unlockAt;
    m.startGear = start;
    m.heldNow = held;
    const float speedRatio = m.rpm > 0.0f ? m.input / m.rpm : 1.0f;
    m.ratio = m.locked || speedRatio >= kCouplingPoint ? 1.0f : 1.0f + (kStallRatio - 1.0f) * (1.0f - speedRatio / kCouplingPoint);
    // the creep: what an idling engine pushes through the converter, less as the truck rolls up to the engine's speed
    const float creep = kCreep * (1.0f - pedalNow) * (1.0f - SettingsRange(speedRatio, 0.0f, 1.0f));
    m.drive = m.locked ? power : (power + creep) * m.ratio;
    if (!isfinite(m.drive)) m.drive = 0.0f;
}

// The lowest gear the engine can take at the speed the truck goes (under kDownMost from idle to the red zone; with the
// mod's engine, engine, under the tachometer's top), 1 when the first can, and 1 before the wheels' grip is
// learned: a wheel spinning by itself at a standstill is no speed.
inline int GearboxLowestFit(const GearboxModel &m, const TruckRaw &t, const GaugeScale &k, bool engine = false)
{
    if (m.grip <= 0.0f) return 1;
    const int top = TruckTopGear(t);
    const float most = engine ? k.max : GearboxPoint(k, kDownMost); // with the mod's engine, the tachometer's top: a gear held on the limiter (kEngineGearHold) stays
    int gear = 1;
    while (gear < top && GearboxInput(t, k, gear, m.rolling) >= most) gear++;
    return gear;
}

// The game's gear lifted past the model's by the floor (TakeoverAutoGear over GearboxLowestFit: the wheels past what
// the engine can take in a gear held, downhill): the model's gear moves with it, as a shift up, so the converter's
// input, the needle and the next shifts go from the gear the truck is in (review 11).
inline void GearboxFollowFloor(GearboxModel &m, int gameGear)
{
    if (!m.running || gameGear <= m.gear) return;
    m.gear = gameGear;
    m.relock = m.locked;
    m.locked = false;
    m.holdUp = true;
    m.moving = 0.0f;
    m.pause = kShiftPause;
    m.wanted = 0.0f;
    m.shifts++;
}

// The gearbox in a line of text for the probe log, beside the game's own gear.
// driving: the mod's automatic sets the game's gears (TakeoverModel).
// Past the counts, what the rules decide by (review 11): the pedal they go by (feel), the points up and down in force for
// the model's gear, the converter's lock and unlock points, the pause, how long a shift has been called for (wanted, up
// over 0, down under) and the gear's hold, the kickdown's arm and window, the cruise, lift and stuck timers, the start
// gear, and the wheels against the model's own gear's top. The fields up to "its pull" stay as they are, for the tools
// that read the line.
inline void GearboxProbeText(const TruckRaw &t, const GearboxModel &m, bool driving, char *out, size_t size)
{
    const float top = TruckGearTop(t, t.gearEngaged), x = top > 0.0f ? m.by / top : 0.0f;
    const float ownTop = TruckGearTop(t, m.gear), ownX = ownTop > 0.0f ? m.by / ownTop : 0.0f;
    char kept[96] = "";
    if (m.keepFor > 0.0f) sprintf_s(kept, sizeof kept, "kept in gear %d or under for %.0f s more (the grade), ", m.keep, m.keepFor);
    char rules[400];
    snprintf(rules, sizeof rules, " | rules: feel %.2f, up at %.0f rpm, down at %.0f, lock at %.0f, unlock at %.0f, pause %.1f s, wanted %.2f s, %s (%.1f s moving), "
                     "kickdown armed %.1f s (window %.1f), cruise %.1f s, lift %.1f s, stuck %.1f s, start gear %d, the wheels at %.2f of the model's gear's top",
              m.feel, m.upAt, m.downAt, m.lockAt, m.unlockAt, fmaxf(m.pause, 0.0f), m.wanted, m.heldNow ? "held" : "not held", m.moving, m.shortFor, fmaxf(m.kick, 0.0f),
              m.cruise, m.liftFor, m.stuck, m.startGear, ownX);
    snprintf(out, size, "automatic (%s): gear %d of %d (the game's %d), %s, input %.0f rpm, engine %.0f rpm, torque x%.2f, to the wheels %.2f | wheels %.2f rad/s for each m/s%s | "
                         "gaining %.2f rad/s each second, the wheels at %.2f of the game's gear's top (its pull %.2f) | %s%s%s%d shifts, %d taken back%s",
              driving ? "the mod's, setting the game's gears" : "not handed to the game", m.gear, TruckTopGear(t), t.gearEngaged, m.locked ? "locked up" : "converter slipping",
              m.input, m.rpm, m.ratio, m.drive, m.grip, m.grip > 0.0f ? "" : " (not learned yet)", m.trend, x, GamePull(x), m.climb > 0.0f ? "climbing, " : "",
              m.spinning ? "wheels spinning, going by the ground, " : "", kept, m.shifts, m.hunts, rules);
}

// ---- the mod's gearbox in the game ----
// With Gearbox set to the mod's and the game's shift function known, the mod's own gearbox (gearbox_model.h) takes a
// truck the moment it is driven with its engine running, in whatever position the game has it (A when the game's
// automatic flag is up). From then on every shift is the gearbox's: the hooks refuse the game's own (gearbox_hooks.h,
// GearboxHolding), the player's position changes come only through the gearbox's actions, and in A the automatic's
// model above picks the gear. It keeps the truck through an engine stop (a stall from RequireClutch among them) and
// gives it back when the setting goes off; the truck gone (another driven, or none read) the caller lets go
// (review 7, R4).
struct TakeoverModel
{
    bool driving;      // the mod's gearbox has the truck
    int shifts;        // gears set since it took it
    int handBacks;     // times it gave a truck back
    int lastHandBack;  // why, the last time: one of the kTake... reasons
};
enum { kTakeNone, kTakeOff, kTakeGone };

struct TakeoverOrders
{
    bool took;         // it took the truck on this pass
    bool gaveBack;     // it gave the truck back on this pass (lastHandBack says why)
};

// One pass. enabled: Gearbox is the mod's and the game's shift function is known; t: the truck as read; fromA: the
// truck is taken only while the game's automatic flag is up (Gearbox=1, where the game's shifter is the player's).
inline TakeoverOrders TakeoverStep(TakeoverModel &m, const TruckRaw &t, bool enabled, bool fromA = false)
{
    TakeoverOrders o = {};
    if (m.driving && !enabled)
    {
        m.driving = false;
        m.handBacks++;
        m.lastHandBack = kTakeOff;
        o.gaveBack = true;
        return o;
    }
    if (!m.driving && enabled && TruckEngineOn(t) && TruckTopGear(t) >= 1 && (!fromA || t.automatic))
    {
        m.driving = true;
        m.shifts = 0;
        o.took = true;
    }
    return o;
}

inline const char *TakeoverWhy(int reason)
{
    return reason == kTakeOff ? "the setting is off" : reason == kTakeGone ? "the truck is gone" : "";
}

// The automatic's gear for A: the model's, never lower than the engine can take at the speed the wheels turn
// (lowestFit, GearboxLowestFit), and within the gears.
inline int TakeoverAutoGear(const TruckRaw &t, int modelGear, int lowestFit)
{
    const int top = TruckTopGear(t), asked = modelGear < lowestFit ? lowestFit : modelGear;
    return asked < 1 ? 1 : asked > top ? top : asked;
}

// ---- the pull test ----
// Whether a write of the game's torque factor (action + 0xCC, the share of its torque the game leaves a damaged
// engine) changes the pull, and whether the game keeps the write: the answer decides whether the engine's torque
// (module 12) reaches the game through it. The power factor (action + 0x38) was tested so in 0.12.4: a write counts at
// once and is kept, but it moves the gear's top speed (as the low gears are made), so halved the truck brakes hard
// to the lower top and doubled the gear pulls less: no torque. Armed from the tab, the test runs once the truck is
// driven floored in the fourth gear or higher, the wheels well under the gear's top, the game's automatic flag down
// (the mod's automatic driving, or a gear picked by hand), and holds the gear through kPullPhase seconds a phase: the
// factor as it is, halved, as it is, raised by half, as it is, at three quarters, as it is. A phase's pull is the
// slope of the truck's speed over its time after its first kPullSettle seconds, which are measured on their own. The
// foot lifted, the brake, another gear, the game's automatic, the gear's top or the engine stopping ends it, and the
// factor is put back as it was.
static const float kPullPhase = 1.0f, kPullSettle = 0.3f;
static const float kPullPedal = 0.9f;        // the pedal floored
static const float kPullTopMost = 0.55f;     // the wheels at most this share of the gear's top for it to begin
static const float kPullTopEnd = 0.95f;      // and it ends past this share
static const int kPullGearLeast = 4;
enum { kPullOff, kPullArmed, kPullRunning, kPullDone, kPullStopped };
struct PullPhase { const char *name; float share; };
static const PullPhase kPullPhases[] = { { "as it is", 1.0f }, { "halved", 0.5f },          { "as it is", 1.0f }, { "raised by half", 1.5f },
                                         { "as it is", 1.0f }, { "at three quarters", 0.75f }, { "as it is", 1.0f } };
static const int kPullPhaseCount = (int)(sizeof kPullPhases / sizeof kPullPhases[0]);

// A line fitted to the truck's speed against the time by least squares.
struct PullFit { float n, sx, sy, sxx, sxy; };
inline void PullFitAdd(PullFit &f, float x, float y)
{
    f.n += 1.0f;
    f.sx += x;
    f.sy += y;
    f.sxx += x * x;
    f.sxy += x * y;
}
// Its slope, the truck's gain in m/s each second; NAN with fewer than three points.
inline float PullFitSlope(const PullFit &f)
{
    const float d = f.n * f.sxx - f.sx * f.sx;
    return f.n >= 3.0f && d > 0.0f ? (f.n * f.sxy - f.sx * f.sy) / d : NAN;
}

struct PullTest
{
    int state;         // one of kPullOff to kPullStopped
    int phase;         // the phase running, or the one it stopped in
    float at;          // seconds into the phase
    float factor;      // the torque factor as it was when the test began
    int gear;          // the gear it runs in
    float from, to;    // the truck's speed at the start and at the end, m/s
    float topShare;    // the wheels' highest share of the gear's top
    PullFit pull[kPullPhaseCount], early[kPullPhaseCount];
    float seen[kPullPhaseCount]; // the factor read back at the end of each phase: whether the game kept the write
    float b4[kPullPhaseCount];   // the highest of action + 0xB4 (how hard the engine pulls) in each phase
    const char *why;   // why it stopped
};

// What the test does to the game on this pass.
struct PullOrders
{
    bool write;        // write factor into the torque factor
    float factor;
    bool hold;         // the gearbox holds its gear
    bool ended;        // it ended on this pass (state says how)
};

// One pass of dt seconds. pedal: the pedal as held (TruckPedal).
inline PullOrders PullTestStep(PullTest &m, const TruckRaw &t, float pedal, float dt)
{
    PullOrders o = {};
    if (m.state != kPullArmed && m.state != kPullRunning) return o;
    const int top = TruckTopGear(t);
    const float gearTop = TruckGearTop(t, t.gear), share = gearTop > 0.0f ? TruckDrivenSpin(t) / gearTop : 1.0f;
    const bool engine = TruckEngineOn(t), braking = TruckBrake(t) > 0.0f || t.handbrake;
    if (m.state == kPullArmed)
    {
        if (!engine || t.automatic || t.gear < kPullGearLeast || t.gear > top || t.gearEngaged != t.gear || pedal < kPullPedal || braking || t.speed < 1.0f ||
            share > kPullTopMost || !(t.torqueFactor > 0.05f) || !isfinite(t.speed))
            return o;
        m = PullTest{ kPullRunning, 0, 0.0f, t.torqueFactor, t.gear, t.speed, t.speed, share };
        m.why = "";
    }
    const char *stop = !engine ? "the engine stopped" : t.automatic ? "the game's automatic took the gears" : t.gear != m.gear ? "another gear was put in" :
                       pedal < kPullPedal ? "the pedal was lifted" : braking ? "the brake was put on" : share > kPullTopEnd ? "the wheels reached the gear's top" : nullptr;
    if (stop)
    {
        m.state = kPullStopped;
        m.why = stop;
        o.write = true;
        o.factor = m.factor;
        o.ended = true;
        return o;
    }
    // this pass's speed is the one the last pass's factor made: the first kPullSettle seconds of a phase apart
    PullFitAdd(m.at >= kPullSettle ? m.pull[m.phase] : m.early[m.phase], m.at, t.speed);
    m.to = t.speed;
    m.topShare = fmaxf(m.topShare, share);
    m.b4[m.phase] = fmaxf(m.b4[m.phase], t.actionB4);
    o.write = true;
    o.factor = m.factor * kPullPhases[m.phase].share;
    o.hold = true;
    m.at += dt;
    if (m.at >= kPullPhase)
    {
        m.seen[m.phase] = t.torqueFactor;
        m.at = 0.0f;
        if (++m.phase >= kPullPhaseCount)
        {
            m.phase = kPullPhaseCount - 1;
            m.state = kPullDone;
            o.factor = m.factor;
            o.hold = false;
            o.ended = true;
        }
    }
    return o;
}

// A test phase's pull against the mean of the phases as it is on either side, NAN where one is missing.
inline float PullRatio(const PullTest &m, int phase)
{
    if (phase < 1 || phase + 1 >= kPullPhaseCount) return NAN;
    const float before = PullFitSlope(m.pull[phase - 1]), after = PullFitSlope(m.pull[phase + 1]), test = PullFitSlope(m.pull[phase]);
    const float base = isfinite(after) ? (before + after) * 0.5f : before;
    return isfinite(test) && isfinite(base) && fabsf(base) > 0.02f ? test / base : NAN;
}

// The test's numbers in lines for the log: what was measured, then each change against the phases as it is.
inline void PullTestText(const PullTest &m, char *first, size_t firstSize, char *second, size_t secondSize)
{
    size_t n = (size_t)snprintf(first, firstSize, "pull test: in gear %d from %.1f to %.1f m/s (the wheels up to %.2f of the gear's top), the torque factor %.3f:", m.gear,
                                m.from, m.to, m.topShare, m.factor);
    for (int p = 0; p < kPullPhaseCount && n < firstSize; p++)
    {
        const float pull = PullFitSlope(m.pull[p]), early = PullFitSlope(m.early[p]);
        if (!isfinite(pull) && !isfinite(early)) break;
        n += (size_t)snprintf(first + n, firstSize - n, "%s %s %.2f m/s each second (the first %.1f s %.2f), the factor read %.3f, B4 up to %.3f", p ? ";" : "",
                              kPullPhases[p].name, pull, kPullSettle, early, m.seen[p], m.b4[p]);
    }
    snprintf(second, secondSize, "pull test: the pull with the torque factor halved x%.2f, raised by half x%.2f, at three quarters x%.2f "
                                 "(x1.00 is no change; nan where the phase was not measured)%s%s",
             PullRatio(m, 1), PullRatio(m, 3), PullRatio(m, 5), m.state == kPullStopped ? "; stopped: " : "", m.state == kPullStopped ? m.why : "");
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
static const float kAutoTakeUp = 0.9f;       // a clutch that works by itself slips with the engine this far to the pedal's governor speed
static const float kAutoLockKmh = 15.0f;     // and from this speed up stays locked: a lugging engine stalls the game's way (kClutchHeatSlowKmh, the same)

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
    bool lugging;      // it does, locked at kAutoLockKmh or more with the engine held under its idle speed in gear: the
                       // game's stall seconds rise (gauges.cpp), so the game's own notice shows and it dies by its rule
    int stalls;        // stalls since the model began
};

// One step of dt seconds of the engine behind a clutch, for a manual gearbox with a clutch pedal (the mod's own
// gearbox in a position of the player's; gearbox_model.h). c: the engine's torque curve; travel: the clutch pedal, 0 let up to 1 pressed;
// pedal: the shaped pedal and factor: the cold and the boost's share of the torque, both from the powertrain
// (PowertrainModel::pedal, and ::cold times ::boost). The engine's governor holds the speed the pedal asks for (idle at
// rest), with up to full torque; the clutch passes on up to its grip times kClutchHold. Let up, it turns the engine
// with the gearbox where it can hold what the engine gives; slipping, it pulls the engine towards the gearbox's
// speed. An engine dragged under kClutchStall of its idle speed stalls: a clutch let up too fast with the truck
// standing, or in too high a gear at a crawl. A stalled engine is started again after kStallRest once the clutch is
// pressed or no gear is in. automatic: no pedal, so travel is a button's (1 held) or 0, and the clutch rides as a
// driver's foot would: the engine at kAutoTakeUp of the pedal's speed while it slips, until the gearbox's input has
// come up to it (see below); it never stalls the engine. The wheels are the game's, so the model only follows them.
// Returns true on the step the engine stalls.
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
    // the governors, as a road truck's min-max one (review 11): the pedal is a torque, its share of the curve's, between
    // the idle governor, which gives what holds idle, and the high-speed one, which is the curve's own fall from the red
    // zone to the tachometer's top. want: the speed the pedal would ask of an all-speed governor, which the clutch that
    // works by itself still rides to
    const float want = k.idle + range * kTachFree * SettingsRange(pedal, 0.0f, 1.0f);
    const float speed = m.rpm / k.redline;
    const float full = TorqueAt(c, speed, k.max / k.redline) * (factor > 0.0f ? factor : 1.0f);
    const float drag = kEngineDrag * speed;
    const float idleHold = SettingsRange((k.idle - m.rpm) / (range * kGovernorBand), 0.0f, full);
    m.torque = fmaxf(idleHold, full * SettingsRange(pedal, 0.0f, 1.0f));
    const float hold = m.grip * kClutchHold;
    const float rate = range / kEngineRevTime; // rpm each second for each share of the most torque
    // the clutch that works by itself rides as a driver's foot would: it lets go when the gearbox's input falls under
    // idle (the truck slowing in a gear), and while it slips the engine goes where the pedal asks, kAutoTakeUp of the
    // governor's speed, at the free engine's pace, and the clutch passes on all the engine gives there, until the
    // gearbox's input has come up to the engine's speed: then it locks. So it never drags the engine down, and a start
    // in a gear the truck cannot pull keeps it slipping (and heating, ClutchHeat) as long as the pedal is down. A
    // button held (grip 0) frees it, as a pedal pressed does. From kAutoLockKmh up it does not slip at all (the user's
    // hill test, 27 October: at speed in too high a gear the game's own "engine stalling", not the clutch's heat): it
    // stays locked, and an engine held under its idle speed lugs (lugging), for the game's stall seconds to rise
    m.lugging = false;
    const bool fast = isfinite(t.speed) && fabsf(t.speed) * 3.6f >= kAutoLockKmh;
    if (automatic && m.connected && hold > 0.0f && fast)
    {
        m.locked = true;
        m.rpm = m.input;
        m.carried = m.torque - drag;
        m.lugging = m.input < k.idle;
        return false;
    }
    if (automatic && m.connected && hold > 0.0f)
    {
        if (m.locked && m.input < k.idle) m.locked = false;
        if (!m.locked)
        {
            const float target = fmaxf(k.idle + (want - k.idle) * kAutoTakeUp, k.idle);
            const float pace = fmaxf(full - drag, 0.1f) * rate * dt;
            m.rpm = m.rpm < target ? fminf(m.rpm + pace, target) : fmaxf(m.rpm - pace, target);
            // the engine gives what holds it there under the clutch's load: none of it at idle, all of it a tenth of
            // the way to the red zone, so at rest in gear it takes nothing and a little pedal pulls away
            const float bite = SettingsRange((m.rpm - k.idle) / (range * 0.1f), 0.0f, 1.0f);
            m.torque = full * bite;
            m.carried = SettingsRange(m.torque - drag, 0.0f, hold);
            if (m.input >= m.rpm)
            {
                m.rpm = m.input;
                m.locked = true;
            }
            return false;
        }
    }
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
        sprintf_s(out, size, "clutch (the needle's engine; a stall on a pedal is the game's): the engine stalled, %s, %d stalls",
                  m.stalled > 0.0f ? "resting" : "waiting for the clutch pressed or neutral to start again", m.stalls);
        return;
    }
    sprintf_s(out, size, "clutch (the needle's engine; a stall on a pedal is the game's): %s %.2f, grip %.2f, %s | engine %.0f rpm, gearbox %.0f rpm | torque %.2f, through the clutch %.2f | %d stalls",
              m.automatic ? "works by itself, button" : "pedal", travel, m.grip, !m.connected ? "no gear in" : m.locked ? "locked" : m.grip > 0.0f ? "slipping" : "free",
              m.rpm, m.input, m.torque, m.carried, m.stalls);
}

// ---- the clutch's heat ----
// A slipping clutch under load turns power into heat: what it carries (ClutchModel::carried, a share of the engine's
// most torque, 0 where it brakes) times the engine's most torque in the game's figures (EngineRecord::torque, times the
// profile's Torque) times the slip (the engine's speed less the gearbox's, rad/s). The heat q rises by that power over
// kClutchHeatCapacity and falls by kClutchCooling of itself a second; a locked or open clutch only cools. Past
// kClutchBurnFrom the clutch burns: (q - kClutchBurnFrom) / kClutchBurnFrom * kClutchBurnRate of the gearbox's capacity
// a second, dealt through the stall damage's path.
//
// The tuning. One unit of heat is a second of the stock truck's engine (Torque 195000, the d110's and a scout's
// figure) carrying its most torque at 1000 rpm of slip: 195000 * 1000 * 2pi/60 = 2.04e7, kClutchHeatCapacity. With
// P the power in those units and the cooling k, q(t) = P/k * (1 - e^-kt) from cold.
//   - a floored start in third: the clutch carries about its most (1.0) at about 1600 rpm of slip, P = 1.6. With
//     k = 0.05 (a clutch that cools from the burn point in about a minute), q reaches 2.85 at 1.87 s: kClutchBurnFrom.
//     In the two seconds after, (q - 2.85)/2.85 grows to 0.98 summed over them, so kClutchBurnRate 0.1 costs the
//     gearbox 0.098 of what it takes: about a tenth every two seconds.
//   - three times the torque: P = 4.8, the burn begins at 0.60 s and costs 0.315 in the two seconds after: 3.2 times.
//   - a hill held at half throttle on the slipping clutch: about half its most (0.5) at 1300 rpm, P = 0.65; q
//     reaches 2.85 at 4.95 s.
//   - a start in first with a normal slip: its most at 1200 rpm falling to none over 1.5 s, P from 1.2 to 0: 0.9 of
//     heat at the most, a third of the burn point.
//   - the clutch that works by itself (keyboard, pad, a button clutch), floored from a standstill (models test, a truck
//     of gears 3, 6, 12 and 16 gaining 2000 rpm of input a second a share in first, by the square of the gears'
//     speeds in the others): the engine goes to kAutoTakeUp 0.9 of the pedal's speed at the free engine's pace and
//     the clutch passes on what the engine gives there until the input comes up to it. First at most 0.57 of the burn
//     point, second 1.66; third burns after 3.5 s, fourth after 3.2 s, a gear the truck cannot pull at all after 2.8
//     s (the engine's rise to the take-up speed is what sets that floor). Take-ups of 0.5, 0.7 and 1.0 gave third no
//     burn, 3.9 s and 3.5 s with first 0.37, 0.47 and 0.61.
//
// The retune after the user's first test (26 October: "the damage works, but it queues up damage. so when you repair it
// will keep damaging. also, theres no warning notice like when tires wear. the damage also comes so fast that theres no
// chance to react"): the cost is dealt only while the clutch slips under load past the burn point, never from stored
// heat (locked or open it only cools, and what it owed is dropped), a repair clears the heat, and a warning box shows
// first (ClutchWarn, as the tire mod's TIRE DAMAGE box): amber from half the burn point (kClutchWarnFrom), red from it. On the clutch model's
// own states (the automatic clutch floored in third on the models test's truck) the heat is 1.76 at 2 s, 3.17 at 4 s
// and 4.04 at 6 s; stuck in a gear it cannot pull, 1.97 at 2 s and 3.98 at 4 s. So kClutchBurnFrom 3.2 puts amber at
// about 1.9 s and red at about 4 s in third, red at about 3.2 s stuck, and a first-gear start (0.57 at the most) never
// brings the warning up. From red the clutch costs kClutchBurnRate times q / kClutchBurnFrom of the gearbox's capacity a
// second: 0.025 at red, about a twentieth every two seconds while the slip goes on, half of the first tuning's pace.
//
// After the user's second test (26 October: uphill, pinned while trying to shift down, it "started overheating veeery
// quickly, and didn't go back down for a good while"): the clutch cools in seconds once it grips. kClutchCooling
// ln 2 / 4 = 0.173 halves the heat in 4 s with the clutch locked or open (0.05 took 14 s), so from the end of a red
// slip (about 4.5 in third) the box is amber after about 2 s and gone after about 6, from a stuck start (about 6.3)
// gone after about 8. The faster cooling also takes heat away during a slip, so kClutchHeatCapacity came down from
// 2.04e7 to 1.6e7 to keep the pace on the clutch model's states: the automatic clutch floored in third amber after
// 1.6 s and red after 4.0 s, fourth red after 3.4 s, a gear it cannot pull after 2.9 s, first 0.66 at most (1.7e7 gave
// red after 4.6 s, 1.8e7 none in third, 1.4e7 after 3.1 s). And the heating goes by the truck's speed
// (ClutchHeatShare): full under kClutchHeatFullKmh, a fifth from kClutchHeatSlowKmh up, linear between, as a clutch
// slipping while the truck rolls is a short event and a truck pinned at a crawl is the abuse; cooling is not scaled.
// So a start from a standstill keeps the pace above, a crawl up a hill at 8 km/h heats at 0.76 of it, and from 15 km/h
// it heats five times slower.
static const float kClutchHeatCapacity = 1.6e7f; // game torque times rad/s times s for one unit of heat
static const float kClutchCooling = 0.173f;      // the share of its heat the clutch loses a second: half of it in 4 s
static const float kClutchHeatFullKmh = 5.0f;    // under this speed the clutch heats at its full rate
static const float kClutchHeatSlowKmh = 15.0f;   // from this speed up at kClutchHeatSlowShare of it
static const float kClutchHeatSlowShare = 0.2f;
static const float kClutchBurnFrom = 3.2f;        // the heat from which it burns (the lamp red)
static const float kClutchWarnFrom = 1.6f;        // the heat from which the lamp warns amber
static const float kClutchBurnRate = 0.025f;      // the gearbox's capacity a second it costs at kClutchBurnFrom, more as it gets hotter
enum { kClutchLampOff, kClutchLampAmber, kClutchLampRed, kClutchLampDealing }; // ClutchLamp: the clutch's state for its warning box

struct ClutchHeat
{
    // A kind of truck the model has driven (TruckKeyNumber of its key), its clutch's heat and the model's clock when
    // it was last driven, as the temperatures keep theirs (TempModel). id 0 is a free place.
    struct Truck { uint64_t id; float q; double left; } truck[kTempTrucks];
    bool known;     // truck[at] is the truck being driven
    int at;
    double clock;   // seconds the model has been stepped for
    float q;        // the driven truck's clutch heat
    float power;    // the last step's power, in units of heat a second
    bool burning;   // q is past kClutchBurnFrom
    bool dealing;   // and the clutch slips under load with on: the burn costs on this step
    float owed;     // the share of the gearbox's capacity the burn has cost and is not dealt yet (with on)
};

// One step of dt seconds for a truck of the kind id (never 0). c: its clutch (an empty one while the mod's gearbox has
// no position of the player's: it only cools); torque: the engine's most in the game's figures, times its profile's
// share (0 not known: no heat). on: Damage and ClutchBurn; off, the heat is kept but costs nothing. A kind driven
// before has cooled for as long as other trucks were driven. A change of truck forgives what the burn owed and was not
// dealt yet (owed, less than one of the gearbox's points); the heat itself stays with the kind (review 10, D6).
// kmh: the truck's speed (its size), by which the heating goes (ClutchHeatShare, below); 0 standing. rate: the
// heating's scale, ClutchHeatPercent / 100 (1 the model's own pace); the cooling is not scaled.
inline float ClutchHeatShare(float kmh);
inline void ClutchHeatStep(ClutchHeat &h, uint64_t id, const ClutchModel &c, float torque, bool on, float dt, float kmh = 0.0f, float rate = 1.0f)
{
    if (!h.known || h.truck[h.at].id != id)
    {
        if (h.known) h.truck[h.at].left = h.clock;
        int to = -1, room = -1;
        for (int i = 0; i < kTempTrucks; i++)
        {
            const ClutchHeat::Truck &k = h.truck[i];
            if (k.id == id) to = i;
            if (h.known && i == h.at) continue;
            if (room < 0 || (h.truck[room].id && (!k.id || k.left < h.truck[room].left))) room = i;
        }
        if (to >= 0) h.truck[to].q *= expf(-kClutchCooling * (float)(h.clock - h.truck[to].left));
        else
        {
            to = room;
            h.truck[to] = { id, 0.0f, h.clock };
        }
        h.at = to;
        h.known = true;
        h.owed = 0.0f;
        h.burning = false;
    }
    float &q = h.truck[h.at].q;
    const bool slipping = c.running && c.connected && !c.locked && !c.dead;
    const float slip = slipping ? fmaxf(c.rpm - c.input, 0.0f) * 6.2831853f / 60.0f : 0.0f;
    h.power = slipping && torque > 0.0f ? fmaxf(c.carried, 0.0f) * torque * slip / kClutchHeatCapacity * ClutchHeatShare(kmh) * fmaxf(rate, 0.0f) : 0.0f;
    q = fmaxf(q + (h.power - q * kClutchCooling) * dt, 0.0f);
    h.q = q;
    h.burning = q > kClutchBurnFrom;
    // the cost only while it slips under load past the burn point: stored heat costs nothing, and what was owed when
    // the slip ended is dropped, so the cost stops the moment the clutch grips or the pedal comes up
    h.dealing = on && h.burning && h.power > 0.0f;
    if (h.dealing) h.owed = fminf(h.owed + q / kClutchBurnFrom * kClutchBurnRate * dt, 1.0f);
    else h.owed = 0.0f;
    h.clock += dt;
    h.truck[h.at].left = h.clock;
}

// The share of the clutch's heating at a truck's speed: 1 under kClutchHeatFullKmh, kClutchHeatSlowShare from
// kClutchHeatSlowKmh up, linear between.
inline float ClutchHeatShare(float kmh)
{
    const float x = SettingsRange((kmh - kClutchHeatFullKmh) / (kClutchHeatSlowKmh - kClutchHeatFullKmh), 0.0f, 1.0f);
    return 1.0f - (1.0f - kClutchHeatSlowShare) * x;
}

// A repair (the gearbox's damage as read fell: the game's repair or a repair point): the driven truck's clutch cold
// again, and nothing owed.
inline void ClutchHeatRepair(ClutchHeat &h)
{
    if (h.known) h.truck[h.at].q = 0.0f;
    h.q = h.power = h.owed = 0.0f;
    h.burning = h.dealing = false;
}

// The clutch's state by the heat: off under kClutchWarnFrom, amber from it, red from kClutchBurnFrom, and dealing
// while the burn costs.
inline int ClutchLamp(const ClutchHeat &h)
{
    return h.dealing ? kClutchLampDealing : h.q >= kClutchBurnFrom ? kClutchLampRed : h.q >= kClutchWarnFrom ? kClutchLampAmber : kClutchLampOff;
}

// ---- the warning boxes (gauges.h, WarnBox: the tire mod's TIRE DAMAGE box, the same shape, below it) ----

// The clutch's box, with ClutchBurn and Damage on (on): amber from half the burn point, red from it; the number is the
// heat in percent of the burn point, at most 100.
inline WarnBox ClutchWarn(const ClutchHeat &h, bool on)
{
    WarnBox w = { kWarnNone, "", "", 0 };
    if (!on) return w;
    const int lamp = ClutchLamp(h);
    if (lamp == kClutchLampOff) return w;
    w.percent = (int)fminf(h.q / kClutchBurnFrom * 100.0f, 100.0f);
    if (lamp == kClutchLampAmber)
    {
        w.state = kWarnAmber;
        w.title = "CLUTCH HOT";
        w.line = "Ease off or shift down";
    }
    else
    {
        w.state = kWarnRed;
        w.title = "CLUTCH DAMAGE";
        w.line = "Shift down, the clutch is wearing"; // the cost lands on the game's gearbox part; the words speak of the clutch
    }
    return w;
}

// The engine's box, with Damage on (on): amber from the coolant's red zone mark (kTempRed), red while the heat costs the
// engine (hot: DamageHot above 0); the number is the coolant's degrees over the red mark in percent of the way to
// kTempHotDamage, at most 100. Red with the boost at the turbo's full and the pedal floored (fullBoost) it reads DANGER TO
// MANIFOLD, a line from a film, with "Keep an eye on the floor pan", and ENGINE DAMAGE again the moment the boost drops;
// once a point of the heat's damage has landed on the engine under that title (dealt), "The floor pan fell off" for the
// rest of that red spell (floorPan, kept by the caller from pass to pass, cleared once the box is amber or gone).
inline WarnBox EngineWarn(float coolant, float hot, bool on, bool fullBoost, bool dealt, bool &floorPan)
{
    WarnBox w = { kWarnNone, "", "", 0 };
    if (!on || !(coolant >= kTempRed || hot > 0.0f))
    {
        floorPan = false;
        return w;
    }
    w.percent = (int)fminf(fmaxf((coolant - kTempRed) / (kTempHotDamage - kTempRed) * 100.0f, 0.0f), 100.0f);
    if (hot > 0.0f)
    {
        w.state = kWarnRed;
        if (fullBoost && dealt) floorPan = true;
        w.title = fullBoost ? "DANGER TO MANIFOLD" : "ENGINE DAMAGE";
        w.line = floorPan ? "The floor pan fell off" : fullBoost ? "Keep an eye on the floor pan" : "Ease off, the engine is wearing";
    }
    else
    {
        floorPan = false;
        w.state = kWarnAmber;
        w.title = "ENGINE HOT";
        w.line = "Ease off, the coolant is in the red";
    }
    return w;
}

// The boost at the turbo's full with the pedal floored, for EngineWarn: the bar within 3 percent of the boost's top
// (scale.boost) and the pedal past 95 percent.
inline bool BoostFull(float bar, float boostMax, float pedal) { return boostMax > 0.0f && bar >= boostMax * 0.97f && pedal >= 0.95f; }

// What the burn owes, in whole points of a gearbox that takes capacity, posted to the stall damage's job (truck.h,
// StallJob) as a percent of it. The points posted; 0 while less than one is owed or the job is busy.
inline int ClutchBurnPost(ClutchHeat &h, StallJob &j, const TruckRefs &r, int capacity, unsigned long long now)
{
    if (!(h.owed > 0.0f) || capacity <= 0) return 0;
    const float points = floorf(h.owed * (float)capacity);
    if (points < 1.0f || !StallJobPost(j, r, points / (float)capacity * 100.0f, now, true)) return 0;
    h.owed = fmaxf(h.owed - points / (float)capacity, 0.0f);
    return (int)points;
}

// The heat in a few words for the probe's clutch line.
inline void ClutchHeatProbeText(const ClutchHeat &h, char *out, size_t size)
{
    static const char *const lamp[] = { "none", "amber", "red", "red: the gearbox pays" };
    sprintf_s(out, size, " | heat %.2f, %.0f %% of the burn point, %.2f a second, the warning %s", h.q, h.q / kClutchBurnFrom * 100.0f, h.power, lamp[ClutchLamp(h)]);
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

// Percent of what the engine takes that it loses a minute when hot: nothing at the settings' HotDamageFrom (kTempRed
// by default), their HotDamagePercentPerMinute at kTempHotDamage, and more in the same measure above that.
inline float DamageHot(float temp, const GaugeSettings &s)
{
    const float from = SettingsRange(s.hotDamageFrom, kHotDamageFromRange[0], kHotDamageFromRange[1]);
    return temp > from ? s.hotDamage * (temp - from) / (kTempHotDamage - from) : 0.0f;
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
    char gearbox[64];
    if (t.gearboxKnown) sprintf_s(gearbox, "gearbox damage %d of %d, effect %.3f", t.gearboxDamage, t.gearboxCapacity, t.gearboxEffect);
    else sprintf_s(gearbox, "gearbox damage not read");
    sprintf_s(out, size,
              "engine damage %d of %d, effect %.3f, torque x%.3f; by the game's rule effect %.3f and torque x%.3f (%s) | %s | all parts %d of %d | %s | "
              "the model: %.2f %% a minute from the heat, %.2f from the cold, %.2f of a point owed, Damage %s",
              t.damage, t.damageCapacity, t.damageEffect, t.torqueFactor, effect, torque, !e.found ? "a guess without the record" : same ? "as read" : "NOT as read",
              gearbox, t.damageSum, t.damageSumCapacity, record, m.hot, m.cold, m.owed, s.damage ? "on" : "off");
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
