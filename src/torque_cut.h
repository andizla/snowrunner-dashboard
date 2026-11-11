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

// The governor: the engine's torque whole up to from (a share of the redline's speed), falling in a straight line to
// nothing at to. Its zero is the needle's stop (kTachOverrun, 1.1 of the redline), so a gear floored on a road holds
// between the redline and the stop, never past it. Its width is the needle's overrun or kGovernWidth rad/s of the wheels'
// speed, the wider: at 60 steps a second a fall as narrow as the overrun in L- (0.27 rad/s at a top of 2.74) chatters step
// to step, as one step moves the wheels by 0.17 to 0.5 rad/s there (the Reviewer's M4). The closed loop at 60 Hz
// (models_test) set the figure: at 0.6 rad/s L- floored in soft mud chatters (136 falls in 10 s), at 1.0 it takes
// 9.4 s to settle at 0.85 of its top, as the fall starts at 0.735; at 0.8 it settles in 2.1 s at 0.93, with no fall. So
// L- 0.81 to 1.1, L about 0.95 to 1.1, 1st and up 1.0 to 1.1 (the Reviewer's M1 of the review).
static const float kGovernWidth = 0.8f;
struct CutBand { float from, to; };
inline CutBand CutGovernBand(float top, float overrun)
{
    const float to = overrun > 1.0f ? overrun : 1.0f;
    float width = to - 1.0f;
    if (top > 0.0f && kGovernWidth / top > width) width = kGovernWidth / top;
    return { to - width, to };
}
inline float CutGovernor(float speed, float from, float to) { return to > from ? CutClamp01((to - speed) / (to - from)) : speed <= from ? 1.0f : 0.0f; }

// Engine braking (EngineBrake, percent): the share of the gear's torque the engine holds the truck back with at the
// redline with the foot off, nothing at idle and under it, so a standing truck is never pushed.
static const float kEngineBrakeMost = 30.0f, kEngineBrakeDefault = 15.0f;
// Above the redline the braking keeps rising with the engine speed, as below it, up to its value at this share of the
// redline (BRIEF_overrev_downshift.md, point 5): an engine over-revved by the road holds the truck harder than one at the
// redline. At idle a quarter of the redline that is 1.4 times the braking at the redline.
static const float kEngineBrakeOverTop = 1.3f;

// The map: F over the wheels' speed with the clutch fully in (the engine locked to the wheels), for one pass of the mod's
// thread. pedal: the raw pedal (TruckPedal, linear as the game's own pull is; the lead's N1); limit: the share of its torque the cold oil and the boost leave; load: the road load as a
// share of the pull at this pedal (RoadLoadShare); held: the engine held back (Torque, TorqueHeld), 1 for all of it;
// brake: EngineBrake over 100; top: the gear's top in rad/s (TruckGearTop), the wheels' speed at the redline; idle and
// redline in rpm; tachTop the tachometer's top over the redline's; governFrom and governTo the governor's band
// (CutGovernBand by the gear's top; the tests narrow it).
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
// of the drive. 0 for a speed or a map that is no number. brakeShare (0 to 1) scales the braking alone (the taper by
// the slowest driven wheel, TorqueCutBrakeTaper). governS: the speed the governor's fade goes by, the truck's own (M5);
// no number: s. holdCurve (where the wheel guard acts, the slot's cap above 0): the curve E is read at the wheels' speed
// held to the redline at most, as its own tail (to 0 at the needle's top) would be a fall in the wheels' speed under the
// guard, and a wheel running up to the guard swung on it (the sketch: the heavy with a wheel free fell by 0.3 in a step 4
// times in 10 s). Where the guard does not act (a numbered gear), E keeps its tail: it is the one limit on a wheel that
// runs free there (the review of bc224bf, M1).
// The governor's fade is in F only where the guard does not act (BRIEF_governor_sway.md, C4). Where it acts, the
// top is held by the ceiling on each wheel (CapCeiling, CapStep), set anew each step: a factor on F there was a bracket
// of a few thousandths against a drive that gives a wheel hundreds of rad/s a step at a bracket of 1, so the pull at
// the top was the wheels' run to the guard switched on and off, and the engine speed swayed with it.
// At or over the governor's end the drive's part of F is still 0 there, by the same speed as before: the cap's step
// leaves a wheel over the ceiling at its speed before the drive, so with a pull in F the drive would undo the game's
// own brake on an over-speeding wheel each step, and a truck shifted down at speed would coast on with the foot down
// where it was held back before. At the end the ceiling is the truck's own speed, so this switch moves no pull.
inline float TorqueCutF(const TorqueCutMap &m, float s, float brakeShare = 1.0f, float governS = NAN, bool holdCurve = false)
{
    if (!(m.redline > m.idle) || !(m.idle >= 0.0f)) return 0.0f;
    const float rpm = TorqueCutRpm(m, isfinite(s) ? s : 0.0f), speed = rpm / m.redline;
    const float governSpeed = isfinite(governS) ? TorqueCutRpm(m, governS) / m.redline : speed;
    const float pedal = CutClamp01(m.pedal);
    const float e = CutCurveAt(m.curve, holdCurve ? fminf(speed, 1.0f) : speed, m.tachTop) * (holdCurve ? (m.governTo > 0.0f && governSpeed >= m.governTo ? 0.0f : 1.0f) : CutGovernor(governSpeed, m.governFrom, m.governTo));
    const float limit = isfinite(m.limit) ? CutClamp01(m.limit / (pedal > 1e-3f ? pedal : 1e-3f)) : 1.0f;
    float drive = pedal * (e * limit - (isfinite(m.load) && m.load > 0.0f ? m.load : 0.0f));
    if (!(drive > 0.0f)) drive = 0.0f;
    if (m.held > 0.0f && m.held < 1.0f) drive *= m.held;
    const float brakeMost = (kEngineBrakeOverTop * m.redline - m.idle) / (m.redline - m.idle);
    const float brake = (1.0f - pedal) * CutClamp01(m.brake) * fminf(fmaxf((rpm - m.idle) / (m.redline - m.idle), 0.0f), brakeMost) * CutClamp01(brakeShare);
    const float f = drive - brake;
    return isfinite(f) ? f : 0.0f;
}

// The over-rev's share (BRIEF_overrev_downshift.md, the Reviewer's M1): the speed the governor went by over the gear's
// top, held to the wheels' own: min(the truck's own speed, the wheels' mean) over top, both from the cut's last answer
// (TorqueCutCounts s and truck). Where the truck's speed was not read (no number) the governor went by the wheels' mean,
// and so does the share. No number for a top or a mean that is no number or not above 0.
inline float OverRevShare(float mean, float truck, float top)
{
    if (!(top > 0.0f) || !isfinite(top) || !isfinite(mean)) return NAN;
    const float v = isfinite(truck) ? fminf(truck, mean) : mean;
    return v / top;
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
// (1 - D4 + D4 x the stock curve). For the probe: what the game would pull at this wheel speed.
inline float TorqueCutB4s(float pedal, float d4, float stockCurve)
{
    const float c = CutClamp01(d4);
    return CutClamp01(pedal) * (1.0f - c + c * CutClamp01(stockCurve));
}

// The stand-in for B4 while the cut holds the drive's throttle and B4 at 1: what the fuel's and the stall's calls of
// the engine speed, TruckGameRevs and the range read in B4's place. The lead's decision (the second review): the cut's
// own pull, the bracket held to 0 to 1, as it sits where B4 sits in the drive. So floored near a gear's top the truck
// burns its pull, where the game burned near idle because it pulled nothing there. One function, so the rule is in one
// place.
inline float TorqueCutStandIn(float bracket) { return CutClamp01(bracket); }

// The wheels' term of the game's engine speed for the stall's call (d73170 with wheels, the second review's M1): 0.75 x
// (0.25 x the mean + 0.75 x the max of the driven wheels' whole speeds in the gear's direction, each floored at 0) over
// the gear's list top (2 x its entry + 5, the entry + 2 in the high gear: TruckGearFormulaTop). 0 without a list top.
inline float TorqueCutStallWheels(float wholeMean, float wholeMax, float listTop)
{
    if (!(listTop > 0.0f)) return 0.0f;
    const float w = 0.75f * (0.25f * wholeMean + 0.75f * wholeMax) / listTop;
    return isfinite(w) && w > 0.0f ? w : 0.0f;
}

// The stall's call answered under the cut: the game's own formula with the stand-in in B4's place, max(p x clamp(w +
// 0.25 x standIn, 0, 1), noWheels x standIn / b4), 0 at a pedal of 0.01 or less as the game's. noWheels is the game's
// own call without the wheels (linear in B4, as the fuel's is), b4 the B4 it read (1 under the hold); p the raw pedal.
inline float TorqueCutStallAnswer(float pedal, float w, float standIn, float noWheels, float b4)
{
    if (!(pedal > 0.01f)) return 0.0f;
    const float base = b4 > 0.0f && isfinite(noWheels) ? noWheels * standIn / b4 : 0.0f;
    const float wheels = CutClamp01(pedal) * CutClamp01(w + 0.25f * standIn);
    return wheels > base ? wheels : base;
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
// listTop: the game's gear's list top by d73170's rule (TruckGearFormulaTop), for the stall's wheels' term. gamePull:
// the game's own pull at the take-over (TorqueCutGamePull, read on the pass before the first publish), where the
// detour's pace starts for a vehicle it has not paced since a clear (S1 of review_drive_2108_findings.md). cap: the
// wheel speed cap, the runaway guard, in the wheels' own measure (WheelCap percent of the gear's top,
// BRIEF_drivetrain_2c_wheel_cap.md with M5), 0 none.
enum { kCutMap, kCutScalar };
static const uint64_t kTorqueFresh = 1000; // ms: a slot older than this passes every call
// The rolling radius (BRIEF_cut_radius_cylinder.md): a wheel rolls on its collision cylinder (shape + 0x28, the shape
// at [wheel + 0x18] + 0x20 by hkpCylinderShape's vtable), which the tire mod writes with the pressure, not on
// wheel + 0x94, which the game writes only when a wheel breaks or is mended. The cylinder where it was read and lies
// within kRollRadiusLeast to kRollRadiusMost of that wheel's + 0x94 (the lead's bounds, the Reviewer's A1: the tire mod's
// deepest flat is 0.70 of the radius, a broken wheel under it 0.53), else + 0x94. kind: kRollCylinder, kRollNoCylinder
// (no vtable, another, a pointer or a fault) or kRollOutOfBounds (a radius that is no number, not above 0 or outside).
static const float kRollRadiusLeast = 0.5f, kRollRadiusMost = 1.25f;
enum { kRollCylinder, kRollNoCylinder, kRollOutOfBounds };
struct RollRadius
{
    float r;
    int kind;
};
inline RollRadius WheelRollRadius(float radius94, float cylinder, bool cylinderRead)
{
    if (!cylinderRead) return { radius94, kRollNoCylinder };
    if (!isfinite(cylinder) || !(cylinder > 0.0f) || !isfinite(radius94) || !(radius94 > 0.0f) || cylinder < kRollRadiusLeast * radius94 || cylinder > kRollRadiusMost * radius94)
        return { radius94, kRollOutOfBounds };
    return { cylinder, kRollCylinder };
}
// The driven wheels' radius for the truck's speed in their measure (the Reviewer's S2): n over the sum of 1 / r, so
// wheels of two sizes rolling without slip spin at v x mean(1 / r), the wheels' mean. Radii that are no number or not
// above 0 are left out; no number with none left.
struct RollMean
{
    float inv;
    int n;
};
inline void RollMeanAdd(RollMean &m, float r)
{
    if (isfinite(r) && r > 0.0f) m.inv += 1.0f / r, m.n++;
}
inline float RollMeanOf(const RollMean &m) { return m.n && m.inv > 0.0f ? (float)m.n / m.inv : NAN; }

struct TorqueCutSlot
{
    uint64_t vehicle, action;
    uint64_t at;    // GetTickCount64 at the publish
    uint64_t cylinderVtable; // hkpCylinderShape's vtable (gauges.cpp WheelCylinderVtable, on the mod's thread: M1), 0 not found, for the rolling radius
    int mode;
    float scalar, floor, share, pedal, sLast, listTop, gamePull, cap, pushBack; // pushBack: 1 - PushShare / 100, for the cap's part of the chassis push
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
// wheel floored at 0, the game's own s without its cut to whole rad/s (S3); wholeMean and wholeMax, each wheel cut to
// whole rad/s in the gear's direction and floored at 0, for the stall's wheels' term. ok false with no driven wheel.
// least: the slowest driven wheel in the gear's direction, not floored (below 0 turning against the gear), for the
// engine brake's taper. truck: the truck's own speed in the wheels' measure, in the gear's direction, the main body's forward speed
// over the driven wheels' rolling radius (RollMeanOf of WheelRollRadius; CutSpeedsTruck; M5 of the wheel cap's addendum: the governor's fade goes by it,
// so no wheel's kick or backspin enters that loop); truckOk false where it was not read (the governor by mean then).
struct CutSpeeds
{
    float mean, mid, wholeMean, wholeMax, least;
    bool ok;
    float truck;
    bool truckOk;
};
inline CutSpeeds TorqueCutSpeeds(const float *omega, const bool *driven, int n, int dir)
{
    CutSpeeds r = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false };
    float sum = 0.0f, lo = 0.0f, hi = 0.0f, wholeSum = 0.0f, wholeHi = 0.0f, least = 0.0f;
    int k = 0;
    for (int i = 0; i < n; i++)
    {
        if (!driven[i] || !isfinite(omega[i])) continue;
        const float w = omega[i] * (dir < 0 ? -1.0f : 1.0f), f = w > 0.0f ? w : 0.0f, whole = truncf(f);
        sum += w;
        lo = k ? fminf(lo, f) : f;
        hi = k ? fmaxf(hi, f) : f;
        wholeSum += whole;
        wholeHi = fmaxf(wholeHi, whole);
        least = k ? fminf(least, w) : w;
        k++;
    }
    if (!k) return r;
    r.mean = sum / (float)k;
    r.mid = 0.5f * (lo + hi);
    r.wholeMean = wholeSum / (float)k;
    r.wholeMax = wholeHi;
    r.least = least;
    r.ok = true;
    return r;
}

// The truck's own speed into the speeds: forward (m/s along the main body's forward axis, the drive's own d73120) over the
// driven wheels' rolling radius (RollMeanOf over WheelRollRadius: each wheel's cylinder, else its + 0x94), in the gear's
// direction (dir +1 or -1). Nothing where either is no number or the radius not above 0.
inline void CutSpeedsTruck(CutSpeeds &sp, float forward, float radius, int dir)
{
    if (!isfinite(forward) || !(radius > 0.0f) || !isfinite(radius)) return;
    const float s = forward / radius * (dir < 0 ? -1.0f : 1.0f);
    if (!isfinite(s)) return;
    sp.truck = s;
    sp.truckOk = true;
}

// The engine brake's taper by the slowest driven wheel (F3 of BRIEF_drive_2108_findings.md): a real engine's drag only
// slows the driveline toward idle, it never turns a wheel against the gear. Whole while every driven wheel turns in the
// gear's direction at twice the gear's idle speed (idle over redline times the gear's top) or more, falling in a straight
// line to 0 at the idle speed, 0 under it: a wheel stopped, turning against the gear or in the air slow keeps the brake off.
inline float TorqueCutBrakeTaper(float least, const TorqueCutMap &m)
{
    if (!(m.redline > 0.0f) || !(m.top > 0.0f) || !(m.idle > 0.0f)) return 0.0f;
    const float idleSpeed = m.idle / m.redline * m.top;
    return CutClamp01((least - idleSpeed) / idleSpeed);
}

// The bracket's pace (F1b, the Reviewer's addendum): the game's own B4 rose toward its target by EngineResponsiveness
// (action + 0xC4) x the power factor a step, and that rise is what held a free wheel's kick in the game; the cut's B4 = 1
// took it out. So the bracket the drive gets (used) moves away from 0 at that rate, used + rate x (bracket - used), and
// toward 0 at once: a cut of the pull is whole in the step, the brake's onset with the foot off is paced too, its
// fall back toward 0 is not. From one side of 0 to the other it drops to 0 at once and paces from there. rate is held to
// 0 to 1; a rate that is no number is 1.
// The game's own pull at a take-over: B4 held to the pedal, as the game pulled until that step (B4 follows its target
// through a shift and is zeroed only with the engine dead or stalled); 0 where B4 is no number.
inline float TorqueCutGamePull(float b4, float pedal)
{
    if (!isfinite(b4)) return 0.0f;
    return CutClamp01(fminf(b4, isfinite(pedal) ? pedal : 0.0f));
}

inline float CutRise(float used, float bracket, float rate)
{
    if (!isfinite(bracket)) return 0.0f;
    const float r = isfinite(rate) ? CutClamp01(rate) : 1.0f;
    const float u = isfinite(used) ? used : 0.0f;
    if (bracket >= 0.0f)
    {
        const float base = u > 0.0f ? u : 0.0f;
        return bracket <= base ? bracket : base + r * (bracket - base);
    }
    const float base = u < 0.0f ? u : 0.0f;
    return bracket >= base ? bracket : base + r * (bracket - base);
}

// The curve's outputs to the drive's call (F1): start -1e9 and a one float step under the top, so the drive's own curve
// is 1 at every speed under the top (its rise's u is exactly 1 in float32: 1e9 + s rounds to 1e9), and the top at the
// governor's end, governTo x the gear's top over the power factor, so that after the drive scales them by the power factor
// (start += (pf - 1) a, a *= pf, top *= pf) the top lands at the governor's end in the drive's units. The game's own
// overspeed brake per wheel acts above that top less 0.5, as in the stock, and stops a free wheel that runs past it. With
// no top or power factor, the old outputs: -1, 0 and 1e9 (the curve 1 everywhere, the brake out of reach).
struct CutCurveOut
{
    float start, a, top;
};
inline CutCurveOut TorqueCutCurveOut(const TorqueCutMap &m, float pf)
{
    if (!(m.top > 0.0f) || !(m.governTo > 0.0f) || !(pf > 0.0f) || !isfinite(pf)) return { -1.0f, 0.0f, 1e9f };
    const float top = m.governTo * m.top / pf;
    if (!isfinite(top) || !(top > 0.0f)) return { -1.0f, 0.0f, 1e9f };
    return { -1e9f, nextafterf(top, 0.0f), top };
}

// The answer to the drive's call: the torque to give (the game's own for the gear times the bracket), the bracket (F
// blended, at least the floor), F, B4s and the stock curve, for the probe; the stand-in for B4 and the stall's wheels'
// term w, for the engine speed's calls. stock: the game's gear torque for this gear;
// start, a, top: its curve's outputs; pf the power factor (action + 0x38); d4 the game's clutch (action + 0xD4); sp the
// wheels' speeds this step (a failed read takes the slot's sLast).
struct TorqueCutAnswer
{
    float torque, bracket, f, b4s, stockCurve, s, standIn, w, used, taper;
};
// The governor's fade goes by the truck's own speed where it was read (M5), else by the wheels' mean; the curve held to
// the redline and no fade in F only where the guard acts (slot.cap above 0): the ceiling holds the top there, and F's
// drive is 0 from the governor's end.
inline TorqueCutAnswer TorqueCutAnswerOf(const TorqueCutSlot &slot, float stock, float start, float a, float top, float pf, float d4, CutSpeeds sp)
{
    TorqueCutAnswer r = {};
    const TorqueCutMap &map = slot.map;
    if (!sp.ok) sp.mean = sp.mid = sp.wholeMean = sp.wholeMax = slot.sLast > 0.0f ? slot.sLast : 0.0f;
    if (!sp.ok) sp.wholeMean = sp.wholeMax = truncf(sp.mean);
    r.s = sp.mean;
    r.stockCurve = TorqueCutStockCurve(start, a, top, pf, sp.mid);
    r.taper = sp.ok ? TorqueCutBrakeTaper(sp.least, map) : 0.0f;
    r.f = slot.mode == kCutMap ? TorqueCutF(map, sp.mean, r.taper, sp.truckOk ? sp.truck : NAN, slot.cap > 0.0f) : (isfinite(slot.scalar) ? slot.scalar : 0.0f);
    r.bracket = TorqueCutBlend(slot.share, slot.pedal, r.stockCurve, r.f);
    if (slot.floor > 0.0f && r.bracket < slot.floor) r.bracket = slot.floor;
    r.used = r.bracket;
    r.torque = isfinite(stock) ? stock * r.bracket : 0.0f;
    r.b4s = TorqueCutB4s(slot.pedal, d4, r.stockCurve);
    r.standIn = TorqueCutStandIn(r.bracket);
    r.w = TorqueCutStallWheels(sp.wholeMean, sp.wholeMax, slot.listTop);
    return r;
}

// The answer paced (F1b): the bracket used from the last step's (CutRise at rate), the torque the stock times it, the
// stand-in for B4's readers the same times kept, the share of the drive's push the wheels kept under the cap (M1 of the
// wheel cap's addendum: at a gear's top F stays at the pedal while the cap takes most of the push away, so the burn goes
// by what the wheels keep; 1 with no cap).
inline void TorqueCutAnswerPaced(TorqueCutAnswer &r, float stock, float usedBefore, float rate, float kept = 1.0f)
{
    r.used = CutRise(usedBefore, r.bracket, rate);
    r.torque = isfinite(stock) ? stock * r.used : 0.0f;
    r.standIn = TorqueCutStandIn(r.used) * (isfinite(kept) ? CutClamp01(kept) : 1.0f);
}

// ---- the wheel speed cap (BRIEF_drivetrain_2c_wheel_cap.md, the Reviewer's addendum and the Researcher's follow-up 4) ----
// The lead's decision on M5: the governor holds the truck at the gear's top by the truck's own speed (CutSpeedsTruck),
// and the cap is the runaway guard at WheelCap percent of the top (150 by default, 110 to 200), so it binds only where the
// wheels run well past the truck: mud, a free wheel, a hop. A cap at the top itself could pass the ground only I x (cap -
// the ground's speed) a step and cost the pull at the top under load (the Reviewer's part 2).
// While the cut acts in a gear held by hand, after the game's drive has put this step's impulses into the wheel bodies'
// angular velocity (body + 0x240, world frame), each driven wheel's spin about its axle (wheel + 0x154, world frame, not
// normalised) is held to the cap: only that component changes, so the body's turn with the chassis stays. The speed is
// the game's own measure, dot(omega, axle), as c27950 builds + 0x174 and + 0x16C from it; so a wheel at the cap reads
// the cap there from the next step, whatever the axle's length.
// kCapDefault, kCapLeast and kCapMost: WheelCap in percent of the gear's top. kCapArm: the cap arms once the driven
// wheels' mean is at or under this times the cap, at the guard (M3: a downshift at speed comes down by the game's brake
// and the engine brake, not by a clamp in one step). kCapKeptOld: the kept share's smoothing, old part.
static const int kCapDefault = 150, kCapLeast = 110, kCapMost = 200;
static const float kCapArm = 1.0f, kCapKeptOld = 0.8f;
// kCapCeilFrom: the truck's speed over the gear's top where the ceiling starts down from the cap (CapCeiling).
static const float kCapCeilFrom = 0.75f;
// kCapWheels: the wheels a truck may list (vehicle + 0x200), as kMaxWheels (truck.h, which this header does not see; a
// static_assert in gauges.cpp holds them equal, the review of 113, M3). The cut, the cap, the lock and the chassis
// push read a truck of up to this many and refuse a longer list.
static const int kCapWheels = 32;

struct CapVec
{
    float x, y, z;
};
inline float CapDot(const CapVec &a, const CapVec &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// The wheel's speed about its axle in the gear's direction (sign +1 or -1), the game's measure; 0 for an axle of no
// length or a value that is no number.
inline float CapSpeed(const CapVec &omega, const CapVec &axle, float sign)
{
    const float w = sign * CapDot(omega, axle);
    return isfinite(w) ? w : 0.0f;
}

// omega moved about the axle alone so its speed (CapSpeed) changes by delta in the gear's direction: omega += sign x
// delta x axle / |axle|^2, so dot(omega, axle) moves by exactly sign x delta for any length of axle and the parts of omega
// across the axle stay. False (omega as it was) for an axle of no length or a value that is no number.
inline bool CapMove(CapVec &omega, const CapVec &axle, float sign, float delta)
{
    const float aa = CapDot(axle, axle);
    if (!(aa > 1e-12f) || !isfinite(aa) || !isfinite(delta) || !isfinite(sign)) return false;
    const float k = sign * delta / aa;
    const CapVec o = { omega.x + k * axle.x, omega.y + k * axle.y, omega.z + k * axle.z };
    if (!isfinite(o.x) || !isfinite(o.y) || !isfinite(o.z)) return false;
    omega = o;
    return true;
}

// The speed change about the axle, the game's measure, that a unit of angular impulse along it gives (S1's spread): the
// world impulse along u = axle / |axle| is taken into the body frame and scaled by the inverse inertia's diagonal there
// (motion + 0xD0 = body + 0x220, as the box and the sphere motions do), so along the wheel's own local axle (its desc's
// + 0x100, local) it is |axle| x sum(invI_k x u_k^2). 0 where it is no number or not above 0: such a wheel neither gives
// nor takes the spread.
inline float CapGain(const CapVec &localAxle, const CapVec &invInertia, float axleLength)
{
    const float ll = CapDot(localAxle, localAxle);
    if (!(ll > 1e-12f) || !isfinite(ll) || !(axleLength > 0.0f) || !isfinite(axleLength)) return 0.0f;
    const float g = axleLength * (invInertia.x * localAxle.x * localAxle.x + invInertia.y * localAxle.y * localAxle.y + invInertia.z * localAxle.z * localAxle.z) / ll;
    return isfinite(g) && g > 0.0f ? g : 0.0f;
}

// The ceiling on a driven wheel after the drive (BRIEF_governor_sway.md, C4), in the wheels' measure: the cap while
// the truck is slow, down by CutSmooth to the governor's end (governTo x the gear's top) as the truck's own speed
// (CutSpeedsTruck's, the gear's direction) goes from kCapCeilFrom x the top to the end. So under kCapCeilFrom the wheels
// have the cap's whole room, and the drive adds nothing to a wheel at the governor's end once the truck is there. With
// no truck speed read, the end; with an end at or over the cap, or no end, the cap; 0 with no cap.
inline float CapCeiling(float cap, float top, float governTo, float truck)
{
    if (!(cap > 0.0f) || !isfinite(cap)) return 0.0f;
    const float end = governTo * top;
    if (!(top > 0.0f) || !isfinite(end) || !(end > 0.0f) || end >= cap) return cap;
    if (!isfinite(truck)) return end;
    const float from = kCapCeilFrom * top;
    if (!(end > from)) return truck < end ? cap : end;
    return cap + (end - cap) * CutSmooth((truck - from) / (end - from));
}

// One step of the cap over a truck's wheels, in the gear's direction and the game's measure: pre and post each wheel's
// speed before and after the game's drive, gain its CapGain (0 unknown), ok a driven wheel read whole. Out: final, the
// speed each wheel is set to; clamped, held at the cap this step; keptEach, each wheel's share of
// its push kept (1 with no push); kept, the truck's share (M1: the sum of what each kept over the sum of the pushes, 1
// where the pushes sum to about 0; M1, the burn by what the wheels keep); excess, the largest speed over a wheel's limit
// after the drive; spread, the speed handed on.
// cap is the ceiling of this step (CapCeiling). A wheel's limit is the ceiling or its own speed before the drive, the
// larger: final = min(post, max(pre, cap)). So the step takes away only what the drive added in it: a wheel the road
// took past the ceiling (a downhill, a shift down at speed) is never lowered, the drive only adds nothing to it, and a
// wheel the drive slowed (the engine's braking) keeps the drive's own result.
// spread (S1, the lead's call): what a clamped wheel could not take, as an impulse (its excess over its gain), goes to
// the driven wheels not clamped and not pushed against the gear, by their own push's impulse (equal where none pushed),
// through each one's gain, and each is capped again; nothing to a clamped wheel and nothing to one the drive pushed back.
struct CapStepOut
{
    float final[kCapWheels], keptEach[kCapWheels];
    bool clamped[kCapWheels];
    float kept, excess, spread;
    int clamps;
};
inline CapStepOut CapStep(const float *pre, const float *post, const float *gain, const bool *ok, int n, float cap, bool spread)
{
    CapStepOut o = {};
    o.kept = 1.0f;
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    float impulse = 0.0f;
    for (int i = 0; i < n; i++)
    {
        o.final[i] = post[i];
        o.keptEach[i] = 1.0f;
        if (!ok[i] || !(cap > 0.0f) || !isfinite(post[i])) continue;
        const float limit = isfinite(pre[i]) && pre[i] > cap ? pre[i] : cap;
        if (post[i] <= limit) continue;
        const float e = post[i] - limit;
        o.excess = fmaxf(o.excess, e);
        o.final[i] = limit;
        o.clamped[i] = true;
        o.clamps++;
        if (gain[i] > 0.0f) impulse += e / gain[i];
    }
    if (spread && impulse > 0.0f && isfinite(impulse))
    {
        float weight[kCapWheels] = {}, sum = 0.0f;
        int takers = 0;
        for (int i = 0; i < n; i++)
            if (ok[i] && !o.clamped[i] && gain[i] > 0.0f && isfinite(pre[i]) && isfinite(post[i]) && post[i] >= pre[i])
            {
                weight[i] = post[i] > pre[i] ? (post[i] - pre[i]) / gain[i] : 0.0f;
                sum += weight[i];
                takers++;
            }
        for (int i = 0; i < n && takers; i++)
        {
            if (!ok[i] || o.clamped[i] || !(gain[i] > 0.0f) || !isfinite(pre[i]) || !isfinite(post[i]) || post[i] < pre[i]) continue;
            const float share = sum > 1e-9f ? weight[i] / sum : 1.0f / (float)takers;
            const float add = impulse * share * gain[i];
            if (!(add > 0.0f) || !isfinite(add)) continue;
            const float limit = pre[i] > cap ? pre[i] : cap;
            float to = o.final[i] + add;
            if (to > limit)
            {
                to = o.final[i] > limit ? o.final[i] : limit;
                o.clamped[i] = true;
                o.clamps++;
            }
            o.spread += to - o.final[i];
            o.final[i] = to;
        }
    }
    float keptSum = 0.0f, pushSum = 0.0f;
    for (int i = 0; i < n; i++)
    {
        if (!ok[i] || !isfinite(pre[i]) || !isfinite(post[i])) continue;
        const float push = post[i] - pre[i];
        const float gained = o.final[i] - pre[i];
        if (push > 0.0f) pushSum += push;
        if (gained > 0.0f) keptSum += gained;
        o.keptEach[i] = push > 1e-6f ? CutClamp01(gained / push) : 1.0f;
    }
    o.kept = pushSum > 1e-4f ? CutClamp01(keptSum / pushSum) : 1.0f;
    return o;
}

// The cap's arming (M3, at the guard): after a take-over (a clear) or a shift (the drive's gear changes), the cap waits
// until the driven wheels' mean is at or under kCapArm x the cap, then stays armed while the cut acts.
// mean: this step's before the drive, the gear's direction; ok false (no wheel read) never arms.
// Since the ceiling (BRIEF_governor_sway.md) the step is written while waiting too: it cannot lower a wheel under its
// speed before the drive, which was the arming's reason, and with the governor out of F nothing else would hold the
// drive after a shift down at speed. The state stays for the probe: waiting says the wheels came into the gear over the cap.
struct CapArm
{
    int gear;
    bool armed;
};
inline bool CapArmStep(CapArm &a, int gear, float mean, bool ok, float cap)
{
    if (gear != a.gear) a.gear = gear, a.armed = false;
    if (!a.armed && ok && cap > 0.0f && gear != 0 && isfinite(mean) && mean <= kCapArm * cap) a.armed = true;
    return a.armed;
}

// The kept share smoothed over steps, kCapKeptOld old; a value that is no number counts as 1.
inline float CapKeptSmooth(float before, float now)
{
    const float b = isfinite(before) ? CutClamp01(before) : 1.0f, k = isfinite(now) ? CutClamp01(now) : 1.0f;
    return kCapKeptOld * b + (1.0f - kCapKeptOld) * k;
}

// ---- the diff lock's bite (BRIEF_diff_lock_bite.md, the Reviewer's addendum and the lead's of 14:09) ----
// The game's lock is a torque rule: a driven wheel over the slowest loses its torque, and nothing ties the speeds. With
// the lock byte set (action + 0x4A) the mod moves each driven wheel's spin about its axle toward the driven wheels' mean,
// weighted by each one's inertia about its axle (1 / CapGain), after the game's drive: final = w + k (mean - w), so the
// wheels' angular momentum about their axles stays and the lock adds no drive and no brake of its own.
// k from DiffLockBite p and the step's own seconds: a time constant tau = 10^(-p / 50) s (the Reviewer's R2), k = 1 -
// exp(-dt / tau); 0 at p 0 (the game's own lock), 1 at p 100 (one shaft); 0 for a dt that is no number or not above 0.
inline float LockK(int p, float dt)
{
    if (p <= 0 || !(dt > 0.0f) || !isfinite(dt)) return 0.0f;
    if (p >= 100) return 1.0f;
    const float tau = powf(10.0f, -(float)p / 50.0f);
    const float k = 1.0f - expf(-dt / tau);
    return isfinite(k) ? CutClamp01(k) : 0.0f;
}
// Under this spread (the fastest minus the slowest, rad/s) nothing is written, so a standing truck is never written and
// no resting body is woken (the Reviewer's R5).
static const float kLockSpreadMin = 0.01f;
struct LockStepOut
{
    float final[kCapWheels];
    float spreadBefore, spreadAfter; // the fastest minus the slowest of the wheels used, after the drive and after the lock
    int used, skipped;               // wheels in the mean, and driven wheels read whole but with no inertia (gain 0)
    bool wrote;                      // final differs from post
};
// One step of the lock over a truck's driven wheels: post each one's speed after the drive (the gear's direction, the
// game's measure), gain its CapGain, ok read whole. Fewer than two wheels with a gain, a spread under kLockSpreadMin, no
// k or a value that is no number: final is post.
inline LockStepOut LockStep(const float *post, const float *gain, const bool *ok, int n, float k)
{
    LockStepOut o = {};
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    float sumI = 0.0f, sumIw = 0.0f, lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < n; i++)
    {
        o.final[i] = post[i];
        if (!ok[i]) continue;
        if (!(gain[i] > 0.0f) || !isfinite(gain[i]) || !isfinite(post[i]))
        {
            o.skipped++;
            continue;
        }
        const float inertia = 1.0f / gain[i];
        sumI += inertia;
        sumIw += inertia * post[i];
        lo = o.used ? fminf(lo, post[i]) : post[i];
        hi = o.used ? fmaxf(hi, post[i]) : post[i];
        o.used++;
    }
    o.spreadBefore = o.spreadAfter = o.used ? hi - lo : 0.0f;
    if (o.used < 2 || !(k > 0.0f) || !isfinite(k) || o.spreadBefore < kLockSpreadMin || !(sumI > 0.0f) || !isfinite(sumI) || !isfinite(sumIw)) return o;
    const float mean = sumIw / sumI, kk = CutClamp01(k);
    if (!isfinite(mean)) return o;
    lo = hi = mean;
    for (int i = 0; i < n; i++)
    {
        if (!ok[i] || !(gain[i] > 0.0f) || !isfinite(gain[i]) || !isfinite(post[i])) continue;
        o.final[i] = kk >= 1.0f ? mean : post[i] + kk * (mean - post[i]);
        lo = fminf(lo, o.final[i]);
        hi = fmaxf(hi, o.final[i]);
        if (o.final[i] != post[i]) o.wrote = true;
    }
    o.spreadAfter = hi - lo;
    return o;
}
// What the mod's thread publishes for the driven truck: its action and vehicle and DiffLockBite, while Powertrain is on,
// the setting above 0 and the truck's lock kind not always on (vehicle + 0xE8 is not 2, or DiffLockBiteAlways).
struct LockSlot
{
    uint64_t action, vehicle;
    uint64_t at; // GetTickCount64 at the publish
    int bite;    // DiffLockBite, 1 to 100
    int kind;    // vehicle + 0xE8: 0 a lock fitted, 1 none, 2 always on
};
// Whether the mod's thread publishes the bite for a truck of this lock kind (the lead's addendum of 14:09): only with the
// setter's pattern found once at the start, the kind read, and the kind not 2 unless DiffLockBiteAlways.
inline bool LockPublishes(bool patternFound, bool kindRead, int kind, bool always)
{
    return patternFound && kindRead && (kind == 0 || kind == 1 || (kind == 2 && always)); // a kind not known publishes nothing (the review of 105)
}
// Whether the wheels' drive's hook is asked for (gauges.cpp, CreepHookStep): Creep, Probe, the damper's settings, the
// torque cut, or the diff lock's bite (Powertrain on and DiffLockBite above 0; the Reviewer's R4).
inline bool DriveHookWanted(bool creep, bool probe, bool damper, bool cut, bool powertrain, int bite)
{
    return creep || probe || damper || cut || (powertrain && bite > 0);
}

// ---- the chassis push (BRIEF_chassis_push.md, the Reviewer's addendum and the lead's of 2026-11-10 09:58) ----
// At road speed the game's drive pays a share s of each driven wheel's torque as a push straight on the wheel's parent
// body along the ground (c42b8a, applyPointImpulse), sized by min(B4, throttle) x the torque factor at + 0xCC with no tyre
// between (the Researcher's follow-ups 5 and 6). The mod's factor is made for the game's pull it assumed, GameFall at
// the factor's own x times the pedal (TorqueLift); where B4 runs above that (it goes by the wheels' speed and lags, the
// factor by the truck's), the push carries the factor times the surplus: the sudden surge at speed. Taken back: across
// the original drive each distinct parent body's linear and angular velocity change (the push alone: the drive touches
// a parent body only there, the kick path included), and a share k of it off, v -= k dv and omega -= k domega, which keeps
// the pitch moment in proportion.
// PushTakeBack: k for a step. pull = min(B4, throttle) the drive's, assumed the game's pull the factor was written for,
// factorOver the factor over the game's own share (F, the Reviewer's S2: the raw value carries engine damage), back
// 1 - PushShare / 100. F_excess = pull / assumed; nothing where it is not above 1 (the factor's own assumption) or where F
// is not above 1 (the factor at the game's own: no mod part in the push). k = 1 - max(1 / F_excess, 1 / F): the push down
// to the mod's own pull, but never under the game's own push for the game's torque (the lead: the game's own push stays
// whole at every speed; with F_excess above F that floor binds), times back.
struct PushSlot
{
    uint64_t action, vehicle;
    uint64_t at;          // GetTickCount64 at the publish
    float assumed;        // GameFall at the factor's x times the pedal
    float factorOver;     // the factor written over the game's own share
    float back;           // 1 - PushShare / 100
};
inline float PushExcess(float b4, float throttle, float assumed)
{
    if (!isfinite(b4) || !isfinite(throttle)) return 0.0f; // fminf drops a NaN: a bad B4 read takes nothing back
    const float pull = fminf(b4, throttle);
    if (!(pull > 0.0f)) return 0.0f;
    if (!(assumed > 1e-4f) || !isfinite(assumed)) return 1e9f;
    return pull / assumed;
}
inline float PushTakeBack(float b4, float throttle, float assumed, float factorOver, float back)
{
    if (!(back > 0.0f) || !isfinite(back) || !(factorOver > 1.0f) || !isfinite(factorOver)) return 0.0f;
    const float fx = PushExcess(b4, throttle, assumed);
    if (!(fx > 1.0f)) return 0.0f;
    const float k = 1.0f - fmaxf(1.0f / fx, 1.0f / factorOver);
    return isfinite(k) ? CutClamp01(k) * CutClamp01(back) : 0.0f;
}
// Whether a step's push may be taken back at all (the Reviewer's M1 of the review of 46c52dd): the kick path's torque
// carries no factor (c42859: no + 0xCC, no B4), so its push is the game's own and stays whole. Nothing at a forward speed
// of 3 m/s or under (s is 0 there: the only push is the kick's) or where it is not read; nothing on a call that starts
// with action + 0x70 apart from + 0x74 (the clunk on engaging 1st or R, c41f03) or with + 0xB8 above 1e-4 (c41e35: every
// call there takes the kick path).
inline bool PushMayTake(float forward, int engagedWas, int gear, float stutter)
{
    return isfinite(forward) && fabsf(forward) > 3.0f && engagedWas == gear && isfinite(stutter) && !(stutter > 1e-4f);
}
// The cap's part (its M4, folded in here): where the guard bound this step, (1 - kept) of the push of each parent body
// that carries a clamped wheel, times back; nothing where none bound.
inline float PushCapBack(bool clamped, float kept, float back)
{
    if (!clamped || !(back > 0.0f) || !isfinite(back)) return 0.0f;
    return (1.0f - (isfinite(kept) ? CutClamp01(kept) : 1.0f)) * CutClamp01(back);
}
// One parent body's velocities moved back by k of their change across the drive.
struct PushBody
{
    CapVec v0, w0, v1, w1; // before and after the drive: linear (+ 0x230) and angular (+ 0x240)
    float invMass;         // + 0x22C; 0: a fixed body, skipped
};
inline bool PushBodyBack(const PushBody &b, float k, CapVec &v, CapVec &w)
{
    if (!(k > 0.0f) || !(b.invMass > 0.0f) || !isfinite(b.invMass)) return false;
    v = { b.v1.x - k * (b.v1.x - b.v0.x), b.v1.y - k * (b.v1.y - b.v0.y), b.v1.z - k * (b.v1.z - b.v0.z) };
    w = { b.w1.x - k * (b.w1.x - b.w0.x), b.w1.y - k * (b.w1.y - b.w0.y), b.w1.z - k * (b.w1.z - b.w0.z) };
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z) && isfinite(w.x) && isfinite(w.y) && isfinite(w.z);
}
