// torque_cut.h: the drivetrain's step 2, the torque cut in a gear held by hand (BRIEF_drivetrain_2_torque_cut.md with the
// Reviewer's addendum of 2026-11-09 19:08). The game's gear torque (game_code.h, GearTorqueFind) is detoured
// (gearbox_hooks.cpp, HookGearTorque) and its call from the wheels' drive answered with the game's own gear torque T times
// the mod's share F, with a flat curve (start -1, a 0, top 1e9: the curve is 1 at every speed and the drive's overspeed
// brake at the top never acts). The speed part of F is worked out here, in the detour, from the wheels as they turn in
// that very physics step: a factor published from the mod's thread came 50 to 120 ms late, and against L-'s top of about
// 3 rad/s that was a cliff the wheels crossed and recrossed about once a second (the 18:21 log). What changes on human
// time (the pedal, the clutch, the creep, the cold and the boost, the road load) comes from the mod's thread in a slot.
// No game access here: the detour reads the wheels and hands their speeds in, so the tests run the same code.
#pragma once
#include <math.h>
#include <stdint.h>

static const int kCutPoints = 8;
// The engine's torque curve as models.h's TorqueCurve (the class's or the engine's own, 1 at its peak): the engine speed
// over the redline's, and the torque there.
struct CutCurve
{
    int n;
    float x[kCutPoints], y[kCutPoints];
};

inline float CutClamp01(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; } // 0 too for a value that is no number

// The torque at an engine speed (rpm over the redline's), as models.h's TorqueAt: past the last point down to nothing at
// top, the tachometer's top over the redline's.
inline float CutCurveAt(const CutCurve &c, float speed, float top)
{
    if (c.n < 1 || !(speed >= 0.0f)) return 0.0f;
    if (speed <= c.x[0]) return c.y[0];
    for (int i = 1; i < c.n && i < kCutPoints; i++)
        if (speed <= c.x[i]) return c.y[i - 1] + (c.y[i] - c.y[i - 1]) * (speed - c.x[i - 1]) / (c.x[i] - c.x[i - 1]);
    const int n = c.n < kCutPoints ? c.n : kCutPoints;
    const float last = c.x[n - 1];
    return top > last ? c.y[n - 1] * CutClamp01(1.0f - (speed - last) / (top - last)) : 0.0f;
}

// The governor: the engine's torque whole up to kGovernFrom of the redline's speed, falling in a straight line to nothing
// at kGovernTo. The Reviewer's M4: at 60 steps a second a fall as narrow as the needle's overrun (the redline to 1.1 of
// it, 0.27 rad/s of L-'s wheel speed) chatters step to step, as one step moves the wheels by 0.17 to 0.5 rad/s there. The
// band is an rpm figure, so it scales with the gear's top: in L- (a top of 2.7 rad/s) it is about 1 rad/s wide, at
// least 2 x the wheels' rise a step, and the closed loop (models_test) holds L- floored in mud with it.
static const float kGovernFrom = 0.9f, kGovernTo = 1.25f;
inline float CutGovernor(float speed, float from = kGovernFrom, float to = kGovernTo) { return to > from ? CutClamp01((to - speed) / (to - from)) : speed <= from ? 1.0f : 0.0f; }

// Engine braking (EngineBrake, percent): the share of the gear's torque the engine holds the truck back with at the
// redline with the foot off, nothing at idle and under it, so a standing truck is never pushed.
static const float kEngineBrakeMost = 30.0f, kEngineBrakeDefault = 15.0f;

// The map: F over the wheels' speed with the clutch fully in (the engine locked to the wheels), for one pass of the mod's
// thread. pedal: the raw pedal (TruckPedal, linear as the game's own pull is; the lead's N1); limit: the share of its torque the cold oil and the boost leave; load: the road load as a
// share of the pull at this pedal (RoadLoadShare); held: the engine held back (Torque, TorqueHeld), 1 for all of it;
// brake: EngineBrake over 100; top: the gear's top in rad/s (TruckGearTop), the wheels' speed at the redline; idle and
// redline in rpm; tachTop the tachometer's top over the redline's; governFrom and governTo the governor's band
// (kGovernFrom, kGovernTo; the tests narrow it).
struct TorqueCutMap
{
    float pedal, limit, load, held, brake;
    float top, idle, redline, tachTop;
    CutCurve curve;
    float governFrom, governTo;
};

// The engine speed in rpm the locked needle shows at a wheel speed of s rad/s in the gear's direction: s over the gear's
// top times the redline (models.h, ClutchModel's input). Rolling against the gear it reads 0 (the Reviewer's M2).
inline float TorqueCutRpm(const TorqueCutMap &m, float s)
{
    return m.top > 0.0f && s > 0.0f ? s / m.top * m.redline : 0.0f;
}

// F at the wheels' speed s: the pedal's share of the engine's torque at the speed the needle shows (its curve, the
// governor, the cold and the boost, the road load taken off, the engine held back), less the engine's braking with the
// foot off. No lift by the wheels' spin, no make-up for the game's fall, no roof and no pacing: the game's curve is out
// of the drive. 0 for a speed or a map that is no number.
inline float TorqueCutF(const TorqueCutMap &m, float s)
{
    if (!(m.redline > m.idle) || !(m.idle >= 0.0f)) return 0.0f;
    const float rpm = TorqueCutRpm(m, isfinite(s) ? s : 0.0f), speed = rpm / m.redline;
    const float pedal = CutClamp01(m.pedal);
    const float e = CutCurveAt(m.curve, speed, m.tachTop) * CutGovernor(speed, m.governFrom, m.governTo);
    const float limit = isfinite(m.limit) ? CutClamp01(m.limit / (pedal > 1e-3f ? pedal : 1e-3f)) : 1.0f;
    float drive = pedal * (e * limit - (isfinite(m.load) && m.load > 0.0f ? m.load : 0.0f));
    if (!(drive > 0.0f)) drive = 0.0f;
    if (m.held > 0.0f && m.held < 1.0f) drive *= m.held;
    const float brake = (1.0f - pedal) * CutClamp01(m.brake) * CutClamp01((rpm - m.idle) / (m.redline - m.idle));
    const float f = drive - brake;
    return isfinite(f) ? f : 0.0f;
}

// 3u^2 - 2u^3 on u held to 0 to 1.
inline float CutSmooth(float u)
{
    u = CutClamp01(u);
    return u * u * (3.0f - 2.0f * u);
}

// The game's own curve at s (review\own_drivetrain_research.md C.1) from the gear torque's start, a and top, scaled by the
// power factor pf as the drive does (start += (pf - 1) a, a *= pf, top *= pf): 0 below the start, a smoothstep up to 1
// at a, 1 less a smoothstep down to 0 at the top, 0 past it.
inline float TorqueCutStockCurve(float start, float a, float top, float pf, float s)
{
    if (!isfinite(pf) || !(pf > 0.0f)) pf = 1.0f;
    start += (pf - 1.0f) * a;
    a *= pf;
    top *= pf;
    if (!isfinite(s) || s < start || s > top) return 0.0f;
    if (s < a) return a > start ? CutSmooth((s - start) / (a - start)) : 1.0f;
    return top > a ? 1.0f - CutSmooth((s - a) / (top - a)) : 0.0f;
}

// The game's own pull without its lag, the stock target of B4 (the Reviewer's M3 and S3): the raw pedal times
// (1 - D4 + D4 x the stock curve). The fuel's call and TruckGameRevs read it in place of B4 while the cut acts, as the
// drive's throttle is held at 1 and B4 with it.
inline float TorqueCutB4s(float pedal, float d4, float stockCurve)
{
    const float c = CutClamp01(d4);
    return CutClamp01(pedal) * (1.0f - c + c * CutClamp01(stockCurve));
}

// EngineShare (0 to 1) between the game's shape and the cut (S3): (1 - share) x the raw pedal x the stock curve at the
// float s, plus share x F. 1 is the cut alone; 0 the game's shape without its B4 lag (at 0 the slot is not published,
// so the game is the game).
inline float TorqueCutBlend(float share, float pedal, float stockCurve, float ours)
{
    const float k = CutClamp01(share);
    return (1.0f - k) * CutClamp01(pedal) * CutClamp01(stockCurve) + k * ours;
}

// What the mod's thread publishes for the driven truck, once a pass (gearbox_hooks.h, TorqueCutPublish). kCutMap: the
// clutch fully in, F from the map at the wheels' speed in the detour; kCutScalar: the clutch slipping or pressed, F the
// torque through the clutch (ClutchModel::carried), the mod engine's own inertia there taking a pass's delay. floor: the
// creep's push with CreepManual (CreepStep's share), the least F; 0 none. pedal: the raw pedal (TruckPedal), for the
// game's shape in the blend and B4s. sLast: the wheels' speed the mod's thread read, for a step whose read failed.
enum { kCutMap, kCutScalar };
static const uint64_t kTorqueFresh = 1000; // ms: a slot older than this passes every call
struct TorqueCutSlot
{
    uint64_t vehicle, action;
    uint64_t at;    // GetTickCount64 at the publish
    int mode;
    float scalar, floor, share, pedal, sLast;
    TorqueCutMap map;
};

// The cut answers a call only when all hold: it is the drive's (it returns to driveBack), a slot is published for this
// vehicle, at most kTorqueFresh old, and the game's own answer was true (a gear the game drives: not 0, a >= 0).
inline bool TorqueCutOurs(uintptr_t back, uintptr_t driveBack, const TorqueCutSlot *slot, uint64_t vehicle, bool stockTook, uint64_t now)
{
    return slot && driveBack && back == driveBack && vehicle && slot->vehicle == vehicle && stockTook && now >= slot->at && now - slot->at <= kTorqueFresh;
}

// The driven wheels' speeds in one step, in the game's gear's direction (dir +1 or -1, from the gear's sign): mean, the
// float mean the needle and the governor go by (S1, as TruckDrivenSpin, but signed: M2); mid, (max + min) / 2 of each
// wheel floored at 0, the game's own s without its cut to whole rad/s (S3). ok false with no driven wheel.
struct CutSpeeds
{
    float mean, mid;
    bool ok;
};
inline CutSpeeds TorqueCutSpeeds(const float *omega, const bool *driven, int n, int dir)
{
    CutSpeeds r = { 0.0f, 0.0f, false };
    float sum = 0.0f, lo = 0.0f, hi = 0.0f;
    int k = 0;
    for (int i = 0; i < n; i++)
    {
        if (!driven[i] || !isfinite(omega[i])) continue;
        const float w = omega[i] * (dir < 0 ? -1.0f : 1.0f), f = w > 0.0f ? w : 0.0f;
        sum += w;
        lo = k ? fminf(lo, f) : f;
        hi = k ? fmaxf(hi, f) : f;
        k++;
    }
    if (!k) return r;
    r.mean = sum / (float)k;
    r.mid = 0.5f * (lo + hi);
    r.ok = true;
    return r;
}

// The answer to the drive's call: the torque to give (the game's own for the gear times the bracket), the bracket (F
// blended, at least the floor), F, B4s and the stock curve, for the probe. stock: the game's gear torque for this gear;
// start, a, top: its curve's outputs; pf the power factor (action + 0x38); d4 the game's clutch (action + 0xD4); sp the
// wheels' speeds this step (a failed read takes the slot's sLast).
struct TorqueCutAnswer
{
    float torque, bracket, f, b4s, stockCurve, s;
};
inline TorqueCutAnswer TorqueCutAnswerOf(const TorqueCutSlot &slot, float stock, float start, float a, float top, float pf, float d4, CutSpeeds sp)
{
    TorqueCutAnswer r = {};
    if (!sp.ok) sp.mean = sp.mid = slot.sLast > 0.0f ? slot.sLast : 0.0f;
    r.s = sp.mean;
    r.stockCurve = TorqueCutStockCurve(start, a, top, pf, sp.mid);
    r.f = slot.mode == kCutMap ? TorqueCutF(slot.map, sp.mean) : (isfinite(slot.scalar) ? slot.scalar : 0.0f);
    r.bracket = TorqueCutBlend(slot.share, slot.pedal, r.stockCurve, r.f);
    if (slot.floor > 0.0f && r.bracket < slot.floor) r.bracket = slot.floor;
    r.torque = isfinite(stock) ? stock * r.bracket : 0.0f;
    r.b4s = TorqueCutB4s(slot.pedal, d4, r.stockCurve);
    return r;
}
