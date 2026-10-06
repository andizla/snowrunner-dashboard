// models.h: what the cluster shows that the game does not hold: the engine speed and the fuel range, worked out from
// the truck's values (truck.h) on every pass of the mod's thread. No game memory is touched here, so the offline test
// (test\models_test.cpp) runs the models on drives it makes up.
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
        m.rpm = target;
        m.running = true;
        return;
    }
    const float tau = !TruckEngineOn(t) ? kTachOff : target > m.rpm ? kTachUp : kTachDown;
    m.rpm += (target - m.rpm) * (1.0f - expf(-dt / tau));
    if (!(m.rpm >= 0.0f)) m.rpm = 0.0f;
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
