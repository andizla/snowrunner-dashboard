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

// F through an open converter (BRIEF_drivetrain_3_a_on_our_chain.md, the Reviewer's M1): the map's law with the engine
// ahead of the gearbox's input. The raw pedal x (the curve at the open converter's engine speed x the governor at the
// input's speed x min(1, limit / pedal) x the converter's torque ratio, less the road load), the engine held back, and
// no braking: an open converter passes next to nothing back (R6). Before the blend with Engine share, as TorqueCutF is.
// inputRpm: the gearbox's input (the turbine); engineRpm: the open converter's engine speed; ratio: its torque ratio, 1
// at the coupling point to the stall's; most: the bound on F (the stall's ratio: nothing in the game bounds a share over
// 1, the Reviewer's R1 and M3). One function for both rates: part A calls it on the mod's thread with the model's
// speeds, part B in the detour with the wheels'. 0 for a figure that is no number.
inline float ConverterOpenF(const TorqueCutMap &m, float inputRpm, float engineRpm, float ratio, float most)
{
    if (!(m.redline > m.idle) || !(m.idle >= 0.0f) || !isfinite(inputRpm) || !isfinite(engineRpm) || !isfinite(ratio)) return 0.0f;
    const float pedal = CutClamp01(m.pedal);
    const float e = CutCurveAt(m.curve, engineRpm / m.redline, m.tachTop) * CutGovernor(inputRpm / m.redline, m.governFrom, m.governTo);
    const float limit = isfinite(m.limit) ? CutClamp01(m.limit / (pedal > 1e-3f ? pedal : 1e-3f)) : 1.0f;
    float drive = pedal * (e * limit * (ratio > 1.0f ? ratio : 1.0f) - (isfinite(m.load) && m.load > 0.0f ? m.load : 0.0f));
    if (!(drive > 0.0f)) drive = 0.0f;
    if (m.held > 0.0f && m.held < 1.0f) drive *= m.held;
    if (most > 0.0f && drive > most) drive = most;
    return isfinite(drive) ? drive : 0.0f;
}

// ---- the open converter in the detour (BRIEF_drivetrain_3_a_on_our_chain.md, part B, points 12 to 14) ----
// In part A the mod's thread worked the open converter out from the wheels' spin smoothed over a quarter second and
// handed one number a pass. Here the same law runs in the game's own call, on this step's wheels: the turbine's speed
// is the wheels' over the gear's top times the redline, the engine's the largest of the turbine's with the converter's
// least slip, the stall's speed at this pedal with its rise, and idle, and the torque ratio falls in a line from the
// stall's to 1 at the coupling point. So a wheel that spins up in one step lowers the ratio in that same step. The
// thread hands what does not depend on the wheels: the stall's speed at this pedal and its rise (models.h, GearboxStep).
// Both speeds stop at the tachometer's top, as the model's do.
static const float kConverterSlip = 0.04f; // an unlocked converter turns the engine at least this much faster than the gearbox's input
static const float kCouplingPoint = 0.87f; // from this ratio of the gearbox's input speed to the engine's the converter multiplies nothing
static const float kStallRatio = 2.0f;     // and at the stall it doubles the torque
struct ConverterNow
{
    float turbine, engine; // rpm
    float ratio;           // the torque ratio, 1 to kStallRatio
};
inline float ConverterRatio(float turbine, float engine)
{
    const float speedRatio = engine > 0.0f ? turbine / engine : 1.0f;
    if (!isfinite(speedRatio) || speedRatio >= kCouplingPoint) return 1.0f;
    return 1.0f + (kStallRatio - 1.0f) * (1.0f - (speedRatio > 0.0f ? speedRatio : 0.0f) / kCouplingPoint);
}
inline ConverterNow ConverterAt(const TorqueCutMap &m, float s, float stall, float rise)
{
    ConverterNow c = { 0.0f, m.idle > 0.0f ? m.idle : 0.0f, 1.0f };
    if (!(m.redline > m.idle) || !(m.idle >= 0.0f) || !isfinite(s)) return c;
    const float most = m.tachTop > 0.0f ? m.tachTop * m.redline : 1e9f;
    c.turbine = fminf(TorqueCutRpm(m, s), most);
    const float stood = isfinite(stall) && stall > m.idle ? stall : m.idle, rose = isfinite(rise) && rise > 0.0f ? rise : 0.0f;
    c.engine = fminf(fmaxf(fmaxf(c.turbine * (1.0f + kConverterSlip), stood + rose * c.turbine), m.idle), most);
    c.ratio = ConverterRatio(c.turbine, c.engine);
    return c;
}
// F through the open converter at the wheels' speed s: ConverterOpenF on ConverterAt's speeds. engine: the engine's
// speed it was worked out at, for the needle (point 14).
inline float ConverterOpenAt(const TorqueCutMap &m, float s, float stall, float rise, float *engine = nullptr)
{
    const ConverterNow c = ConverterAt(m, s, stall, rise);
    if (engine) *engine = c.engine;
    return ConverterOpenF(m, c.turbine, c.engine, c.ratio, kStallRatio);
}
// What the needle goes by in A (point 14): the detour's engine speed while the cut acts in A through the open
// converter and the last answer is this truck's, so the needle, the sound and the pull are one number. Else the
// model's. The tachometer's own smoothing follows either (the addendum on point 14: the detour's speed is unsmoothed).
inline float ConverterNeedleRpm(bool cutInA, bool locked, bool ours, float detourRpm, float modelRpm)
{
    return cutInA && !locked && ours && isfinite(detourRpm) && detourRpm > 0.0f ? detourRpm : modelRpm;
}

// The fall at a lock, paced (the lead's point 17): above the start gear a floored lock finds the open converter still
// multiplying, so F would fall about a quarter in one step. For kLockFallSeconds after the lock F runs in a straight
// line from what the open converter gave (from) down to the map's. By the clock, so it is the same at 60 and at 120
// steps a second. Only a fall is paced, and only the driver's share of it: a lifted pedal takes its part at once (from
// is scaled by the pedal now over the pedal at the lock), so with the foot off nothing is held up.
static const float kLockFallSeconds = 0.3f;
inline float CutLockFall(float fMap, float from, float lockPedal, float pedal, float since)
{
    if (!isfinite(fMap)) return 0.0f;
    if (!isfinite(from) || !isfinite(since) || since < 0.0f || since >= kLockFallSeconds) return fMap;
    const float foot = lockPedal > 1e-3f && isfinite(pedal) ? CutClamp01(pedal / lockPedal) : 0.0f;
    const float held = from * foot;
    if (!(held > fMap) || !(held > 0.0f)) return fMap; // nothing to hold with the foot off: the engine's braking comes in as the map has it
    return fMap + (held - fMap) * (1.0f - since / kLockFallSeconds);
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

// The watch's "the cut acted in A on the last pass" flag (the review of 162 part A, S2): the cut's own block sets it to
// what the pass found; wherever the cut is given up outside that block (the truck no longer read, not driven or held,
// the Powertrain off) it is down, whatever stood there. The automatic's two shift rules rest only while it is up.
inline bool CutInAFlag(bool givenUp, bool inA) { return !givenUp && inA; }

// Whether the mod's engine drives this truck, for the automatic (BRIEF_drive_1114_findings.md, part 177): the mod's
// torque factor is handed to the game, or the torque cut acted in A on the last pass. Under the cut the hand is given
// back, as it must be, and the automatic was then told that the game's engine drives: it shifted by that engine's whole
// schedule, with no gear hold, several gears at once. Both flags are the last pass's where the automatic steps: in the
// pass the cut takes A the hand is still on, and in the pass it lets go the cut's flag is still up, so in neither is
// the automatic told "the game's engine". With CutInA off the cut's flag is never up and this is the hand's own.
inline bool ModEngineDrives(bool handOn, bool cutInA) { return handOn || cutInA; }

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
// The truck's own speed in the wheels' measure from its forward speed (m/s) and the rolling radii of the wheels read
// whole (ok), in the gear's direction (sign), as CutSpeedsTruck has it. For a step the cut's hook gave no truck speed
// to (the lock without the cut). No number where the forward speed is none or no radius is usable.
inline float TruckSpeedInWheels(float forward, const float *radius, const bool *ok, int n, float sign)
{
    RollMean m = {};
    for (int i = 0; i < n; i++)
        if (ok[i]) RollMeanAdd(m, radius[i]);
    const float r = RollMeanOf(m);
    if (!isfinite(forward) || !(r > 0.0f) || !isfinite(r)) return NAN;
    const float s = forward / r * (sign < 0.0f ? -1.0f : 1.0f);
    return isfinite(s) ? s : NAN;
}

// ---- the differential's type (BRIEF_diff_types.md and the Reviewer's addendum of 2026-11-11) ----
// DiffType, a truck's own setting: Auto goes by the truck's lock kind (vehicle + 0xE8: 2, a lock always on, is Locked; 0, a
// lock fitted, and 1, none, are Open), the others are themselves. A kind not read or not known under Auto is Limited
// slip, the mod's behaviour before the types.
enum { kDiffAuto, kDiffOpen, kDiffLimited, kDiffLocked, kDiffTypes };
inline int DiffTypeOf(int setting, bool kindRead, int kind)
{
    if (setting == kDiffOpen || setting == kDiffLimited || setting == kDiffLocked) return setting;
    if (!kindRead) return kDiffLimited;
    return kind == 2 ? kDiffLocked : kind == 0 || kind == 1 ? kDiffOpen : kDiffLimited;
}
// The lock is in force (A3): the lock byte set (action + 0x4A) and the handbrake (action + 0x48) not, on any type, or
// the type Locked.
inline bool DiffLockInForce(int type, bool lockByte, bool handbrake) { return (lockByte && !handbrake) || type == kDiffLocked; }
// What acts in one step of the drive (M4), read once with that step's lock byte: the lock where it is in force, else
// Open for the type Open, else Limited slip.
// kDiffForceAxles: the driven axles differ in type and the lock byte is not in force, so each axle goes by its own
// (BRIEF_axle_diff.md, M3; DiffAxlesStep). DiffForceOf never gives it: the hook does, over the slot's wheels.
enum { kDiffForceOpen, kDiffForceLimited, kDiffForceLock, kDiffForceAxles };
inline int DiffForceOf(int type, bool lockByte, bool handbrake)
{
    return DiffLockInForce(type, lockByte, handbrake) ? kDiffForceLock : type == kDiffOpen ? kDiffForceOpen : kDiffForceLimited;
}
// Open's limit is on the mean of each axle's wheels (DiffOpenAxles, below DiffEvenStep), not on each wheel.
// Locked on a truck whose lock is not always on (S4): the mod holds the lock byte while the type acts. One pass of the
// mod's thread: want (the type Locked acts on this action), the byte as read. Returns the byte to write, -1 for none. A
// byte the player set first is not the mod's and is never cleared; one the mod set is cleared once the type no longer
// acts. Another action (a truck change): the old truck keeps its byte as it was left, and its action is remembered (the
// last kLockHoldLeft of them), so back on it the byte is the mod's own again and is cleared once the type no longer acts
// (G1 of the review).
static const int kLockHoldLeft = 4;
struct LockHold
{
    uint64_t action;
    bool ours;
    uint64_t left[kLockHoldLeft]; // the actions left with the mod's byte set, 0 for a free place
    int next;
};
inline int LockHoldStep(LockHold &h, uint64_t action, bool want, bool byteRead, uint8_t byte)
{
    if (action != h.action)
    {
        if (h.ours && h.action)
        {
            h.left[h.next] = h.action;
            h.next = (h.next + 1) % kLockHoldLeft;
        }
        h.action = action, h.ours = false;
        for (int i = 0; i < kLockHoldLeft; i++)
            if (action && h.left[i] == action) h.left[i] = 0, h.ours = true;
    }
    if (!action || !byteRead) return -1;
    if (want)
    {
        if (byte) return -1;
        h.ours = true;
        return 1;
    }
    if (!h.ours) return -1;
    h.ours = false;
    return byte ? 0 : -1;
}

struct TorqueCutSlot
{
    uint64_t vehicle, action;
    uint64_t at;    // GetTickCount64 at the publish
    uint64_t cylinderVtable; // hkpCylinderShape's vtable (gauges.cpp WheelCylinderVtable, on the mod's thread: M1), 0 not found, for the rolling radius
    int mode;
    float scalar, floor, share, pedal, sLast, listTop, gamePull, cap, pushBack; // pushBack: 1 - PushShare / 100, for the cap's part of the chassis push
    // the fall at a converter's lock (CutLockFall): what the open converter gave on its last pass, the pedal then and
    // the tick of the lock; fallAt 0: none
    float fallFrom, fallPedal;
    uint64_t fallAt;
    // the gear ratio pull (GearRatioFactor): the tool's share, 0 to 1, and the reference gear's a. The share is 0 in
    // any position but a held one that is not H (by the position, on the mod's thread: must 3), so in A, in N and in H
    float ratioShare, ratioRefA;
    // ... and for R and the low positions (ratioHold, by the position): gear 1's a, whose factor they may not pass
    float ratioFirstA;
    bool ratioHold;
    // the open converter in the detour (part B): with mode kCutScalar and convOpen up, F is ConverterOpenAt on this
    // step's wheels from the thread's stall speed at this pedal and its rise. The scalar stays the thread's own figure
    // for a step whose wheels were not read
    bool convOpen;
    float convStall, convRise;
    // the run-down at a lift (CutRunStep): its seconds for this gear, 0 none (the tool's Engine inertia at 0, the brake,
    // the handbrake, a shift out)
    float runSeconds;
    float capWide; // the low range's wider cap for L-, L and R (LowRangeCap, CapCeilingWide), in the wheels' measure; 0 none. cap stays the gear's own
    TorqueCutMap map;
    int diff; // the differential's type for this truck (DiffTypeOf, on the mod's thread)
    // The differential by axle (BRIEF_axle_diff.md, M5): the truck's wheels by their pointers, each with its axle's type
    // in force (kDiffOpen, kDiffLimited or kDiffLocked) and how it is driven (1 always, 2 with all wheel drive, 0 not).
    // axleN 0: the axles are not read as pairs and the whole truck goes by diff.
    int axleN;
    uint64_t axleWheel[32];
    uint8_t axleType[32], axleDrive[32];
};
enum { kSlotWheels = 32 };
// What the hook goes by in one step (M1): over the slot's wheels that are driven in this step (awd: the byte at
// action + 0x49), the type they all have, or differ where two of them have two types. With no table, or no wheel of it
// driven, the slot's own type. Where they differ, type is the slot's own too (the lock byte's rule goes by it).
struct SlotAxles
{
    int type;
    bool differ;
};
inline SlotAxles SlotAxleTypes(const TorqueCutSlot &c, bool awd)
{
    SlotAxles f = { c.diff, false };
    bool any = false;
    for (int i = 0; i < c.axleN && i < kSlotWheels; i++)
    {
        if (!(c.axleDrive[i] == 1 || (c.axleDrive[i] == 2 && awd))) continue;
        if (!any) f.type = c.axleType[i], any = true;
        else if (c.axleType[i] != f.type) f.differ = true;
    }
    if (f.differ) f.type = c.diff;
    return f;
}
// What is in force in one step with the cut on this action (M1, M2): each axle by its own (kDiffForceAxles) where the
// bytes were read, the driven axles differ and the lock byte is not in force (set, and the handbrake not). Else what
// DiffForceOf gives for the type the driven axles share: the lock byte in force is the whole truck's lock whatever
// the axles are.
inline int SlotForce(const TorqueCutSlot &c, bool bytesOk, bool lockByte, bool handbrake, bool awd)
{
    const SlotAxles sa = SlotAxleTypes(c, awd);
    if (bytesOk && sa.differ && !(lockByte && !handbrake)) return kDiffForceAxles;
    return DiffForceOf(sa.type, lockByte, handbrake);
}
// One wheel's type by its pointer. Not in the slot: Limited slip, and found false (counted for the probe).
inline int SlotWheelType(const TorqueCutSlot &c, uint64_t wheel, bool *found)
{
    for (int i = 0; i < c.axleN && i < kSlotWheels; i++)
        if (c.axleWheel[i] == wheel)
        {
            if (found) *found = true;
            return c.axleType[i] == kDiffOpen || c.axleType[i] == kDiffLocked ? c.axleType[i] : kDiffLimited;
        }
    if (found) *found = false;
    return kDiffLimited;
}

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
    float convEngine; // the open converter's engine speed this step, rpm; 0 where F was not worked out through it
    bool running;     // the run-down at a lift held the bracket up in this step
    bool falling, floored; // the lock's fall held F up in this step; the bracket stands at the slot's floor
};
// The governor's fade goes by the truck's own speed where it was read (M5), else by the wheels' mean; the curve held to
// the redline and no fade in F only where the guard acts (slot.cap above 0): the ceiling holds the top there, and F's
// drive is 0 from the governor's end. The same in every type of differential: Open has no governor of its own (C5 of
// BRIEF_governor_sway.md voids the one by the wheels' mean), its limit is the ceiling on each axle's mean (DiffOpenAxles).
inline TorqueCutAnswer TorqueCutAnswerOf(const TorqueCutSlot &slot, float stock, float start, float a, float top, float pf, float d4, CutSpeeds sp, uint64_t now = 0)
{
    TorqueCutAnswer r = {};
    const TorqueCutMap &map = slot.map;
    if (!sp.ok) sp.mean = sp.mid = sp.wholeMean = sp.wholeMax = slot.sLast > 0.0f ? slot.sLast : 0.0f;
    if (!sp.ok) sp.wholeMean = sp.wholeMax = truncf(sp.mean);
    r.s = sp.mean;
    r.stockCurve = TorqueCutStockCurve(start, a, top, pf, sp.mid);
    r.taper = sp.ok ? TorqueCutBrakeTaper(sp.least, map) : 0.0f;
    if (slot.mode == kCutMap) r.f = TorqueCutF(map, sp.mean, r.taper, sp.truckOk ? sp.truck : NAN, slot.cap > 0.0f);
    else if (slot.convOpen && sp.ok) r.f = ConverterOpenAt(map, sp.mean, slot.convStall, slot.convRise, &r.convEngine);
    else r.f = isfinite(slot.scalar) ? slot.scalar : 0.0f;
    if (slot.mode == kCutMap && slot.fallAt && now >= slot.fallAt)
    {
        const float fell = CutLockFall(r.f, slot.fallFrom, slot.fallPedal, slot.pedal, (float)(now - slot.fallAt) / 1000.0f);
        r.falling = fell > r.f;
        r.f = fell;
    }
    r.bracket = TorqueCutBlend(slot.share, slot.pedal, r.stockCurve, r.f);
    if (slot.floor > 0.0f && r.bracket <= slot.floor) r.bracket = slot.floor, r.floored = true;
    r.used = r.bracket;
    r.torque = isfinite(stock) ? stock * r.bracket : 0.0f;
    r.b4s = TorqueCutB4s(slot.pedal, d4, r.stockCurve);
    r.standIn = TorqueCutStandIn(r.bracket);
    r.w = TorqueCutStallWheels(sp.wholeMean, sp.wholeMax, slot.listTop);
    return r;
}

// ---- the run-down at a lift (BRIEF_drive_1114_findings.md, part 180, the Reviewer's check and seven musts) ----
// The game's wheel is a light body with nothing behind it: at a lift the drive hands it nothing and the ground takes
// its speed in a few steps. An engine and a driveline keep a spinning wheel turning for a while. So where the cut
// answers, a fall of the bracket at a lift is paced: from the bracket in use at the lift down to what the step asks
// (the engine's braking, in the end) in a straight line over `seconds`, by the clock, so it is the same at 60 and at
// 120 steps a second. Only a fall, only with the foot off or lifting, and only while the driven wheels' mean is over
// the truck's own speed: a gripping truck's lift is today's, and no pull comes that the driver did not ask for.
// It ends at once where the pedal comes back, the wheels grip, the gear's top changes (a shift), another truck
// answers, or the thread hands 0 seconds (the brake, the handbrake, a shift out, the tool's figure at 0).
// The seconds are the thread's: the tool's Engine inertia over 100 times kRunDownMost in the first gear, and less in
// a higher gear by the tops' ratio squared, since the engine's inertia counts at the wheels by the gear's ratio squared.
static const float kRunDownMost = 2.0f;    // s: the run-down in the first gear at an Engine inertia of 100
static const float kRunFootOff = 0.05f;    // a pedal under this is the foot off (models.h, kFootOff)
static const float kRunLift = 0.02f;       // a pedal this much under the step before is lifting; this much over the lift's is back
static const float kRunSpinShare = 1.15f, kRunSpinMore = 0.5f; // spinning: the driven mean over the truck's speed by this share and this many rad/s
inline float RunDownSeconds(int percent, float gearTop, float firstTop)
{
    if (percent <= 0 || !(gearTop > 0.0f) || !isfinite(gearTop)) return 0.0f;
    const float share = (percent > 100 ? 100 : percent) / 100.0f;
    const float ratio = firstTop > 0.0f && isfinite(firstTop) && gearTop > firstTop ? firstTop / gearTop : 1.0f; // the low range and R as the first gear
    return kRunDownMost * share * ratio * ratio;
}
// What ends it from the mod's thread (must 3): the brake pedal, the handbrake and a shift that is out. N has no slot.
// clutchOpen: a clutch worked by hand is not locked (the review of 180, S1): an engine cut off from the wheels by its
// clutch gives them nothing. The automatic's converter is no clutch here: in A it is never up.
inline bool RunDownAllowed(float brake, bool handbrake, bool shiftOut, bool clutchOpen = false)
{
    return !(brake > 0.0f) && isfinite(brake) && !handbrake && !shiftOut && !clutchOpen;
}
// How much of the run-down the wheels get (the review of 180, S2), 0 to 1: nothing at the spinning threshold, all of
// it from twice the truck's speed (and 0.5 rad/s). So one wheel slipping a little on a climb carries next to nothing
// on to the gripping ones, and a run-down lands softly as the wheels come down to the truck's speed.
inline float RunDownWeight(bool ok, float mean, bool truckOk, float truck)
{
    if (!ok || !truckOk || !isfinite(mean) || !isfinite(truck)) return 0.0f;
    const float over = truck > 0.0f ? truck : 0.0f, from = over * kRunSpinShare + kRunSpinMore, whole = over * 2.0f + kRunSpinMore;
    if (!(mean > from)) return 0.0f;
    return whole > from ? CutClamp01((mean - from) / (whole - from)) : 1.0f;
}
inline bool RunDownSpinning(bool ok, float mean, bool truckOk, float truck)
{
    if (!ok || !truckOk || !isfinite(mean) || !isfinite(truck)) return false;
    return mean > (truck > 0.0f ? truck : 0.0f) * kRunSpinShare + kRunSpinMore;
}
struct CutRun
{
    uint64_t vehicle, at; // the truck and the tick of the lift; at 0: no run-down
    float from, pedal;    // the bracket in use at the lift, and the pedal then
    float top;            // the gear's top then: another one is a shift
    float pedalWas;       // the pedal of the step before, for "lifting"
    bool spent;           // a run-down was ended: none starts again until the bracket has landed or the foot is back on
};
// One step of the detour. target: the bracket this step asks; usedBefore: the one the step before used. Returns the
// bracket to use, never under target. paced: it was held above target.
// spin: RunDownWeight's, 0 not spinning.
inline float CutRunStep(CutRun &run, float target, float usedBefore, float pedal, float top, float spin, float seconds, uint64_t now, uint64_t vehicle, bool *paced = nullptr)
{
    if (paced) *paced = false;
    const bool spinning = spin > 0.0f;
    const float pedalWas = run.vehicle == vehicle ? run.pedalWas : pedal;
    run.pedalWas = pedal;
    const bool off = !(seconds > 0.0f) || !isfinite(seconds) || !spinning || !isfinite(target) || !isfinite(pedal);
    if (off || run.vehicle != vehicle)
    {
        // nothing to pace, or another truck's first step: an ended run-down stays ended
        if (run.at) run.spent = true;
        if (run.vehicle != vehicle) run.spent = false;
        run.at = 0, run.vehicle = vehicle;
        return target;
    }
    if (run.at && (pedal > run.pedal + kRunLift || top != run.top || now < run.at)) run.at = 0, run.spent = true; // the pedal is back, or a shift
    if (!run.at)
    {
        const bool lifting = pedal < kRunFootOff || pedal < pedalWas - kRunLift;
        const bool falls = isfinite(usedBefore) && usedBefore > target && usedBefore > 0.0f;
        if (!lifting || !falls) run.spent = false; // the foot is on again or the bracket has landed: the next lift may start one
        if (run.spent || !lifting || !falls) return target;
        run.at = now ? now : 1, run.from = usedBefore, run.pedal = pedal, run.top = top;
    }
    if (pedal < run.pedal) run.pedal = pedal; // a lift that goes on: "back" is over the lowest pedal since the lift
    const float since = (float)(now - run.at) / 1000.0f;
    if (since >= seconds)
    {
        run.at = 0, run.spent = true;
        return target;
    }
    const float held = target + (run.from - target) * (1.0f - since / seconds) * (spin < 1.0f ? spin : 1.0f);
    if (!(held > target)) return target;
    if (paced) *paced = true;
    return held;
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

// ---- the gear ratio pull (BRIEF_jobs_1114.md, "161, the choice": C, and the Reviewer's addendum of 00:25) ----
// The game's gear torque falls slower over the gears than a gearbox's ratio does: it is the engine's torque over the
// root of max(0.5 a, 1), where a is the gear's own figure, while the ratio goes as 1 over the gear's top, 2a + 5. So
// against its top gear the game's low gears are short of what a real gearbox would give. This is the factor that takes
// a gear's torque toward the ratio's, held to the reference gear: T(ref) x top(ref) / top(a), over the game's T(a).
// The reference is the highest numbered gear the gearbox offers, never H. Never under 1: it adds, it never takes. Never
// over the roof (the lead's ruling: 2.0, the figure the converter's stall has), and share (0 to 1, the tuning tool's
// Gear ratio pull over 100) runs in a straight line from 1 to that. 1 where a figure is no number or not above 0 and
// at a share of 0, exactly, so a build with the slider at 0 hands the game's own figure bit for bit. The tops are the
// game's own, 2a + 5, not the mod's: those can be learned and carry the power factor, and a torque law should not move
// with a learner. roofed: the roof held it.
static const float kRatioPullRoof = 2.0f;
inline float GearRatioFactor(float a, float refA, float share, float roof = kRatioPullRoof, bool *roofed = nullptr)
{
    if (roofed) *roofed = false;
    if (!isfinite(a) || !isfinite(refA) || !isfinite(share) || !(a > 0.0f) || !(refA > 0.0f) || !(share > 0.0f)) return 1.0f;
    float full = sqrtf(fmaxf(0.5f * a, 1.0f)) / sqrtf(fmaxf(0.5f * refA, 1.0f)) * (2.0f * refA + 5.0f) / (2.0f * a + 5.0f);
    if (!isfinite(full) || !(full > 1.0f)) return 1.0f;
    if (roof > 1.0f && full > roof)
    {
        full = roof;
        if (roofed) *roofed = true;
    }
    return 1.0f + (full - 1.0f) * CutClamp01(share);
}
// R and the low positions (the lead's ruling of 2026-11-14 06:26 and the Reviewer's addendum of 06:30): they get no
// more than gear 1 gets. So with hold up the factor is the gear's own and no more than gear 1's, both by the law above
// at the same share and roof: the smaller of two factors, not the larger of two figures, since between an a of 2.0 and
// 2.5 the law rises a little with a. Where gear 1 has no figure above 0 a held position gets 1. held: gear 1's was the
// smaller, and then roofed is gear 1's own flag. With hold down this is GearRatioFactor.
inline float GearRatioHeld(float a, float refA, float firstA, bool hold, float share, float roof = kRatioPullRoof, bool *roofed = nullptr, bool *held = nullptr)
{
    if (held) *held = false;
    const float own = GearRatioFactor(a, refA, share, roof, roofed);
    if (!hold) return own;
    bool firstRoofed = false;
    const float first = GearRatioFactor(firstA, refA, share, roof, &firstRoofed);
    if (!(own > first)) return own;
    if (held) *held = true;
    if (roofed) *roofed = firstRoofed;
    return first;
}
// The slot under the factor (must 2): the air drag's share and the creep's floor are shares of the gear's torque, so a
// larger torque would make them larger forces. Both are divided by the factor and stay the same force. The engine's
// braking is left to grow: it does go through the gear's ratio on a real truck. A factor of 1 changes nothing.
// (TorqueCutSlot is declared further down: a template keeps this beside the factor's rule.)
template <class Slot> inline void GearRatioSlot(Slot &slot, float factor)
{
    if (!(factor > 1.0f) || !isfinite(factor)) return;
    if (slot.map.load > 0.0f) slot.map.load /= factor;
    if (slot.floor > 0.0f) slot.floor /= factor;
}
// The detour's one step: the game's torque t for the gear whose figure is a, scaled by that gear's own factor, and the
// slot with it. Returns the factor. At a factor of 1 neither t nor the slot is written, so both stay bit for bit.
template <class Slot> inline float GearRatioApply(Slot &slot, float &t, float a, bool *roofed = nullptr, bool *held = nullptr)
{
    const float factor = GearRatioHeld(a, slot.ratioRefA, slot.ratioFirstA, slot.ratioHold, slot.ratioShare, kRatioPullRoof, roofed, held);
    if (factor == 1.0f) return 1.0f;
    t *= factor;
    GearRatioSlot(slot, factor);
    return factor;
}

// Whether a pass's over-rev share counts: only where the map went out and the drive answered since the last pass, and
// never in A (BRIEF_drivetrain_3_a_on_our_chain.md, point 9 and the Reviewer's must of 2026-11-14): there the automatic
// picks the gear, and a hill run in the top gear is no over-rev of the driver's.
inline bool OverRevCounts(int mode, bool fresh, bool inA) { return mode == kCutMap && fresh && !inA; }

// ---- the learning under the cut (the lead's addendum of 2026-11-10 14:23) ----
// The cut's answer is the game's own gear torque times the bracket, so a gear's learned pull means the same with the
// cut: the truck's acceleration at a bracket of 1. A sample is divided by the bracket the detour used, not by what the
// mod's thread meant: the detour runs 60 or 120 times a second and the thread 20. The detour keeps the sum of the
// brackets used and their count, and counts every step that may not feed a sample (bad). They only grow; the thread
// takes the difference between two of its passes.
struct CutLearnTotals
{
    double sum;       // the brackets used, summed
    long n, bad;      // the steps answered, and those of them that may not feed a sample
    uint64_t vehicle; // the truck of the last answer
};
static const float kCutLearnPaced = 0.02f; // a bracket used within this share of the one asked is settled: CutRise only nears a rising one
// Whether a step's answer may feed a sample: only the map (no sample from the open converter's or the clutch's scalar),
// the bracket not paced (a rise by CutRise, or the lock's fall), not at the slot's floor, and the wheels keeping all of
// the push (kept under 1: the wheel cap takes some away).
inline bool CutLearnStepOk(const TorqueCutSlot &slot, const TorqueCutAnswer &r, float kept)
{
    return slot.mode == kCutMap && !r.falling && !r.running && !r.floored && r.bracket > 0.0f && fabsf(r.used - r.bracket) <= kCutLearnPaced * r.bracket && isfinite(kept) && kept >= 0.999f;
}
// The detour's sum for the learner, one step more: the bracket used times the gear ratio pull's factor, since the
// torque handed is the game's times both. So a row learned at one figure of the slider is the base learned at another.
inline double CutLearnAdd(double sum, float used, float factor)
{
    return sum + (isfinite(used) && isfinite(factor) ? (double)used * (double)factor : 0.0);
}
// The divisor for the thread's pass: the mean of the brackets the detour used since the pass before; no number where
// the detour answered nothing since (a count of 0, a stale slot), a step among them may not feed a sample, or the
// answers are another truck's.
inline float CutLearnDivisor(const CutLearnTotals &was, const CutLearnTotals &now, uint64_t vehicle)
{
    if (!vehicle || now.vehicle != vehicle || was.vehicle != vehicle) return NAN;
    const long n = now.n - was.n;
    if (n <= 0 || now.bad != was.bad) return NAN;
    const float mean = (float)((now.sum - was.sum) / (double)n);
    return isfinite(mean) && mean > 0.0f ? mean : NAN;
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
// kLowWideFrom: the truck's speed over the gear's top where the low range's wider cap starts down to the gear's own
// (CapCeilingWide); it is the gear's own from kCapCeilFrom on.
static const float kLowWideFrom = 0.25f;
// kCapWheels: the wheels a truck may list (vehicle + 0x200), as kMaxWheels (truck.h, which this header does not see; a
// static_assert in gauges.cpp holds them equal, the review of 113, M3). The cut, the cap, the lock and the chassis
// push read a truck of up to this many and refuse a longer list.
static const int kCapWheels = 32;
static_assert(kSlotWheels == kCapWheels, "the slot's wheel table holds every wheel the cap reads");

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

// The ceiling with the low range's wider cap (BRIEF_low_range_pull.md, M1 and M3): own is the gear's own cap, wide the
// cap LowRangeSlip gives L-, L and R (LowRangeCap), both in the wheels' measure. The wide part, wide - own, is whole while
// the truck is slower than from x the gear's top and is down to nothing by CutSmooth at kCapCeilFrom x the top. From
// there on the ceiling is CapCeiling of the gear's own cap to the last bit, so the top of the gear is held as without
// the wide part and the governor's band is no steeper. With no wide part, no truck speed or no top: CapCeiling of own.
// from at or over kCapCeilFrom is the tests' counter case: the wide cap whole down to the governor's band.
inline float CapCeilingWide(float own, float wide, float top, float governTo, float truck, float from = kLowWideFrom)
{
    const float base = CapCeiling(own, top, governTo, truck);
    if (!(own > 0.0f) || !isfinite(own) || !isfinite(wide) || !(wide > own)) return base;
    if (from >= kCapCeilFrom) return CapCeiling(wide, top, governTo, truck);
    if (!(top > 0.0f) || !isfinite(top) || !isfinite(truck)) return base;
    const float a = (from > 0.0f ? from : 0.0f) * top, b = kCapCeilFrom * top;
    if (truck >= b) return base;
    return base + (wide - own) * (1.0f - CutSmooth((truck - a) / (b - a)));
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

// The kept share smoothed over steps, old the old part (kCapKeptOld a step of 1/60 s); a value that is no number
// counts as 1.
inline float CapKeptSmooth(float before, float now, float old = kCapKeptOld)
{
    const float b = isfinite(before) ? CutClamp01(before) : 1.0f, k = isfinite(now) ? CutClamp01(now) : 1.0f;
    return old * b + (1.0f - old) * k;
}
// The old part for a step of dt seconds, so that the share settles as fast by the clock as at 1/60 s: kCapKeptOld to
// the power dt x 60 (0.894 at 1/120 s). A step of 1/60 s, or one that is no time, gives kCapKeptOld itself. Only the
// physics rate test mode asks (physics_rate.h): at the game's own rate the hook hands kCapKeptOld on as it is.
inline float CapKeptOldAt(float dt)
{
    if (!isfinite(dt) || !(dt > 0.0f) || dt == 1.0f / 60.0f) return kCapKeptOld;
    const float steps = dt * 60.0f;
    return powf(kCapKeptOld, steps < 0.25f ? 0.25f : steps > 4.0f ? 4.0f : steps);
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
    float c, r, over;                // LockStepHeld's: the weighted mean before the drive, the impulse handed to the wheels on the ground, the fastest wheel after the drive over c
    float j;                         // LockStepHeld's: the drive's impulse, the sum of (post - pre) x inertia (0 where it was not worked out)
    int under, bounded;              // wheels written under 0, and wheels LockBound's rules moved (every used wheel where the sum was taken down)
    int breach;                      // wheels written under 0 and under what the drive left them while the drive's impulse is over 0: LockBound forbids it, so 0
};
// The bounds on what the lock writes while the drive's impulse is over 0 (BRIEF_lock_wheels_backward.md, M2). The lock's
// law deals the drive's whole impulse out anew by contact share, so a wheel with little load is written near C, under
// 0 where C is, and a wheel with much load over every wheel the drive left. Three rules on final, the lock's speeds
// after the blend by k: 1. no wheel under min(post, 0): none leaves the lock backward unless the drive left it there,
// and then not lower. 2. no wheel over the fastest of post: the lock makes no new fastest wheel, so the spread is not
// wider and the wheel cap has nothing of the lock's to cut. 3. the sum of inertia x final not over that of post: the
// lock adds no spin. What is over is taken from every wheel by its room above rule 1's floor, which always holds
// enough. What the rules do not let through is dropped and stored nowhere. Returns the wheels moved.
inline int LockBound(const float *post, const float *gain, const bool *use, int n, float *final)
{
    float top = 0.0f, sumPost = 0.0f, sumAbs = 0.0f, sumFinal = 0.0f, room = 0.0f;
    bool first = true;
    int moved = 0, used = 0;
    for (int i = 0; i < n; i++)
    {
        if (!use[i]) continue;
        top = first ? post[i] : fmaxf(top, post[i]);
        first = false;
    }
    for (int i = 0; i < n; i++)
    {
        if (!use[i]) continue;
        const float inertia = 1.0f / gain[i], floor = fminf(post[i], 0.0f), x = fminf(fmaxf(final[i], floor), top);
        if (x != final[i]) moved++;
        final[i] = x;
        sumPost += inertia * post[i];
        sumAbs += inertia * fabsf(post[i]);
        sumFinal += inertia * x;
        room += inertia * (x - floor);
        used++;
    }
    const float surplus = sumFinal - sumPost;
    if (!(surplus > 1e-5f * sumAbs) || !(room > 0.0f)) return moved;
    const float part = fminf(surplus / room, 1.0f);
    for (int i = 0; i < n; i++)
    {
        if (!use[i]) continue;
        const float floor = fminf(post[i], 0.0f);
        final[i] = fmaxf(final[i] - part * (final[i] - floor), floor);
    }
    return used;
}
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
// The lock's step with the ground behind the wheels (M3 of the diff types' addendum). LockStep's mean is weighted by each
// wheel's own inertia, so a wheel in the air pulls the mean up and the solver takes the others back down each step: the
// free wheel runs away. Here each wheel weighs what it has behind it: I = 1 / gain, and W = I + load x mass / n x r^2
// (load the contact share at wheel + 0x128 held to 0 to 1, mass the truck's by its files, r the rolling radius, n the
// wheels used); with no mass, or no radius for that wheel, W = I x (1 + 30 x load). C is the mean of pre by W, J the
// drive's impulse (the sum of (post - pre) x I), a = J / the sum of W the speed it gives wheels and truck together, and
// R = J - a x the sum of I what is left for the ground. A wheel's locked speed is C + a + gain x R x load / the sum of
// the loads (the last term left out with the loads under 0.001), and final = post + k x (locked - post). With every
// load 0 it is LockStep itself. Fewer than two wheels, no k, both spreads (pre's and post's) under kLockSpreadMin or a
// value that is no number: final is post. A wheel on the ground that slips counts as loaded and is not held. With the
// drive's impulse J over 0 LockBound's three rules hold on final (bound false: the law without them, for the tests'
// figures of before).
inline LockStepOut LockStepHeld(const float *pre, const float *post, const float *gain, const float *load, const float *radius, const bool *ok, int n, float k, float mass,
                                bool bound = true)
{
    LockStepOut o = {};
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    bool use[kCapWheels] = {};
    float ld[kCapWheels] = {};
    float sumLoad = 0.0f, sumI = 0.0f, sumIpost = 0.0f, sumIpre = 0.0f, lo = 0.0f, hi = 0.0f, preLo = 0.0f, preHi = 0.0f;
    bool preOk = true;
    int used = 0;
    for (int i = 0; i < n; i++)
    {
        o.final[i] = post[i];
        if (!ok[i]) continue;
        if (!(gain[i] > 0.0f) || !isfinite(gain[i]) || !isfinite(post[i]))
        {
            o.skipped++;
            continue;
        }
        use[i] = true;
        ld[i] = isfinite(load[i]) ? CutClamp01(load[i]) : 0.0f;
        sumLoad += ld[i];
        const float inertia = 1.0f / gain[i];
        sumI += inertia;
        sumIpost += inertia * post[i];
        sumIpre += inertia * pre[i];
        if (!isfinite(pre[i])) preOk = false;
        lo = used ? fminf(lo, post[i]) : post[i];
        hi = used ? fmaxf(hi, post[i]) : post[i];
        preLo = used ? fminf(preLo, pre[i]) : pre[i];
        preHi = used ? fmaxf(preHi, pre[i]) : pre[i];
        used++;
    }
    if (!(sumLoad > 0.0f))
    {
        // every wheel in the air: the lock as it was, to the float
        o = LockStep(post, gain, ok, n, k);
        o.c = used >= 2 && sumI > 0.0f ? sumIpost / sumI : 0.0f;
        o.over = used >= 2 ? hi - o.c : 0.0f;
        const float jAir = preOk ? sumIpost - sumIpre : 0.0f;
        if (isfinite(jAir)) o.j = jAir;
        if (!o.wrote) return o;
        // the mean of post is never over its fastest and adds no spin: only rule 1 can move a wheel here
        if (bound && o.j > 0.0f) o.bounded = LockBound(post, gain, use, n, o.final);
        o.wrote = false;
        bool firstAir = true;
        for (int i = 0; i < n; i++)
        {
            if (!use[i]) continue;
            lo = firstAir ? o.final[i] : fminf(lo, o.final[i]);
            hi = firstAir ? o.final[i] : fmaxf(hi, o.final[i]);
            firstAir = false;
            if (o.final[i] != post[i]) o.wrote = true;
            if (o.final[i] != post[i] && o.final[i] < 0.0f) o.under++;
            if (o.j > 0.0f && o.final[i] < fminf(post[i], 0.0f)) o.breach++;
        }
        o.spreadAfter = hi - lo;
        return o;
    }
    o.used = used;
    o.spreadBefore = o.spreadAfter = used ? hi - lo : 0.0f;
    if (used < 2 || !preOk || !(k > 0.0f) || !isfinite(k) || (o.spreadBefore < kLockSpreadMin && preHi - preLo < kLockSpreadMin)) return o;
    const bool massOk = mass > 0.0f && isfinite(mass);
    float sumW = 0.0f, sumWpre = 0.0f, j = 0.0f;
    for (int i = 0; i < n; i++)
    {
        if (!use[i]) continue;
        const float inertia = 1.0f / gain[i], r = radius[i];
        const float w = massOk && r > 0.0f && isfinite(r) ? inertia + ld[i] * mass / (float)used * r * r : inertia * (1.0f + 30.0f * ld[i]);
        sumW += w;
        sumWpre += w * pre[i];
        j += (post[i] - pre[i]) * inertia;
    }
    if (!(sumW > 0.0f) || !isfinite(sumW) || !isfinite(sumWpre) || !isfinite(j) || !isfinite(sumI)) return o;
    const float c = sumWpre / sumW, a = j / sumW, rest = j - a * sumI, kk = CutClamp01(k);
    float final[kCapWheels];
    for (int i = 0; i < n; i++)
    {
        if (!use[i]) continue;
        const float locked = c + a + (sumLoad >= 0.001f ? gain[i] * rest * ld[i] / sumLoad : 0.0f);
        final[i] = kk >= 1.0f ? locked : post[i] + kk * (locked - post[i]);
        if (!isfinite(final[i])) return o;
    }
    o.j = j;
    if (bound && j > 0.0f) o.bounded = LockBound(post, gain, use, n, final);
    const float postHi = hi;
    bool first = true;
    for (int i = 0; i < n; i++)
    {
        if (!use[i]) continue;
        o.final[i] = final[i];
        lo = first ? final[i] : fminf(lo, final[i]);
        hi = first ? final[i] : fmaxf(hi, final[i]);
        first = false;
        if (final[i] != post[i]) o.wrote = true;
        if (final[i] != post[i] && final[i] < 0.0f) o.under++;
        if (j > 0.0f && final[i] < fminf(post[i], 0.0f)) o.breach++;
    }
    o.spreadAfter = hi - lo;
    o.over = postHi - c;
    o.c = c;
    o.r = rest;
    return o;
}
// Open's evening (A2): an open differential gives each of its wheels the same torque. After the drive the driven wheels
// read whole with a gain share its impulse evenly: J = the sum of (post - pre) / gain, and each one's speed is pre +
// gain x J / n. The chassis push stays the game's. Fewer than two wheels or a value that is no number: final is post.
struct DiffEvenOut
{
    float final[kCapWheels];
    float j;    // the drive's impulse over the wheels used
    int used;
    bool wrote; // final differs from post
};
inline DiffEvenOut DiffEvenStep(const float *pre, const float *post, const float *gain, const bool *ok, int n)
{
    DiffEvenOut o = {};
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    bool use[kCapWheels] = {};
    float j = 0.0f;
    for (int i = 0; i < n; i++)
    {
        o.final[i] = post[i];
        if (!ok[i] || !(gain[i] > 0.0f) || !isfinite(gain[i]) || !isfinite(post[i]) || !isfinite(pre[i])) continue;
        use[i] = true;
        j += (post[i] - pre[i]) / gain[i];
        o.used++;
    }
    o.j = isfinite(j) ? j : 0.0f;
    if (o.used < 2 || !isfinite(j)) return o;
    float final[kCapWheels];
    for (int i = 0; i < n; i++)
    {
        if (!use[i]) continue;
        final[i] = pre[i] + gain[i] * j / (float)o.used;
        if (!isfinite(final[i])) return o;
    }
    for (int i = 0; i < n; i++)
        if (use[i])
        {
            o.final[i] = final[i];
            if (final[i] != post[i]) o.wrote = true;
        }
    return o;
}
// Open's limit (F1 of the review of the types on the governor ceiling): the step's ceiling (CapCeiling) holds the driven
// wheels' mean, not each wheel. pre each wheel's speed before the drive, even its speed after the evening. What the
// drive added to a wheel in this step is scaled by one factor k for all driven wheels, so that the mean after the step is
// min(the mean after the drive, max(the mean before, the ceiling)). k lies in 0 to 1. No wheel is lowered under its speed
// before the drive, and a wheel the drive slowed (the engine's braking) keeps the drive's own result. With all wheels
// alike this is CapStep's law for each. With one wheel free and the others standing the free one runs to n times the
// ceiling and the mean sits at the ceiling. Nothing is handed on.
// Out as CapStep's: final; clamped and keptEach (k) for each wheel the drive added to while k is under 1; kept, the
// truck's share; excess, the mean's speed over its limit after the drive; spread 0.
inline CapStepOut DiffOpenStep(const float *pre, const float *even, const float *gain, const bool *ok, int n, float ceiling)
{
    (void)gain;
    CapStepOut o = {};
    o.kept = 1.0f;
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    float before = 0.0f, added = 0.0f, taken = 0.0f;
    int m = 0;
    for (int i = 0; i < n; i++)
    {
        o.final[i] = even[i];
        o.keptEach[i] = 1.0f;
        if (!ok[i] || !isfinite(pre[i]) || !isfinite(even[i])) continue;
        const float d = even[i] - pre[i];
        before += pre[i];
        if (d > 0.0f) added += d;
        else taken += d;
        m++;
    }
    if (!m || !(ceiling > 0.0f) || !isfinite(ceiling) || !(added > 0.0f) || !isfinite(added) || !isfinite(before) || !isfinite(taken)) return o;
    const float count = (float)m, meanBefore = before / count, meanAfter = (before + added + taken) / count;
    const float limit = meanBefore > ceiling ? meanBefore : ceiling;
    if (meanAfter <= limit) return o;
    // the mean with the additions scaled: (before + k x added + taken) / m = limit
    const float k = CutClamp01((limit * count - before - taken) / added);
    o.excess = meanAfter - limit;
    for (int i = 0; i < n; i++)
    {
        if (!ok[i] || !isfinite(pre[i]) || !isfinite(even[i]) || !(even[i] > pre[i])) continue;
        const float to = pre[i] + k * (even[i] - pre[i]);
        if (!isfinite(to)) continue;
        o.final[i] = to < pre[i] ? pre[i] : to > even[i] ? even[i] : to;
        o.keptEach[i] = k;
        o.clamped[i] = true;
        o.clamps++;
    }
    o.kept = k;
    return o;
}
// ---- Open by axle (BRIEF_open_by_axle.md with the Reviewer's addendum) ----
// Open is an open differential in each axle. The evening and the limit above run over the two wheels of one axle. What
// an axle's limit took from the drive is handed to the other axles (DiffOpenAxles, the tied shaft between the axles): the
// game gives a wheel far slower than the fastest next to none of the drive, so without the hand-on a free wheel would
// take the pull of the whole truck. So a free wheel takes the pull of its own axle and the other axles pull on.
// The grouping (M1). Two wheels are one axle where they hang on the same parent body (wheel + 0x10), stand on other sides
// of it, and are each the other's nearest along that body's forward, no further apart than kAxleAlong. The place is the
// wheel's centre in its parent body's own frame (AxlePlace), so a frame that bends, a steered wheel and a spring's
// travel change nothing: the first two do not move the centre along the body by more than a few centimetres, the
// third moves it along the body's up, which is not looked at.
// kAxleAlong 0.5 m: the axles of a tandem stand over 1 m apart, so half of that can not reach the next axle, and it is
// wide enough for an axle whose two wheels are not quite opposite. The mutual nearest keeps a wheel from taking a
// partner that has a nearer one.
// A wheel with no such partner, or one not read, is an axle of its own (M2): axle[i] is i. A pair has the lower index.
// The second pairing (BRIEF_tester_1114.md, part 186, must 2): the wheels left alone by the first are paired again by
// the same rule in the main body's frame (mainAlong, mainSide; nullptr: no second pairing), whatever body each hangs
// on. A tandem on beams, one beam a side, has both wheels of a beam on one side of it and its partner across the truck
// on the other beam, so the first pairing leaves all four alone and the second gives two axles of two wheels.
static const float kAxleAlong = 0.5f;
inline bool AxlePlace(const float *centre, const float *bodyAt, const float *forward, const float *left, float &along, float &side)
{
    const float d[3] = { centre[0] - bodyAt[0], centre[1] - bodyAt[1], centre[2] - bodyAt[2] };
    along = d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2];
    side = d[0] * left[0] + d[1] * left[1] + d[2] * left[2];
    return isfinite(along) && isfinite(side);
}
struct AxleGroupsOut
{
    int axle[kCapWheels];
    int axles;  // the axles found, a wheel alone counted as one
    int alone;  // the wheels with no partner, the ones not read among them
    int across; // the axles of the second pairing, in the main body's frame
    bool byMain[kCapWheels]; // the wheel was paired in the second pairing
};
// The mutual nearest among the wheels `in` gives (body 0: any body), by place along a frame and on other sides of it.
inline void AxlePairs(const uint64_t *body, const float *along, const float *side, const bool *in, int n, int *best)
{
    for (int i = 0; i < n; i++)
    {
        best[i] = -1;
        if (!in[i]) continue;
        float least = kAxleAlong;
        for (int j = 0; j < n; j++)
        {
            if (j == i || !in[j] || (body && body[j] != body[i]) || !(side[i] * side[j] < 0.0f)) continue;
            const float d = fabsf(along[i] - along[j]);
            if (!(d <= least) || (best[i] >= 0 && d == least)) continue;
            best[i] = j, least = d;
        }
    }
}
inline AxleGroupsOut AxleGroups(const uint64_t *body, const float *along, const float *side, const bool *ok, int n, const float *mainAlong = nullptr,
                                const float *mainSide = nullptr)
{
    AxleGroupsOut o = {};
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    int best[kCapWheels];
    bool in[kCapWheels], lone[kCapWheels];
    for (int i = 0; i < n; i++)
        o.axle[i] = i, in[i] = ok[i] && body[i] && isfinite(along[i]) && isfinite(side[i]);
    AxlePairs(body, along, side, in, n, best);
    int left = 0;
    for (int i = 0; i < n; i++)
    {
        const int j = best[i];
        lone[i] = !(j >= 0 && best[j] == i);
        if (!lone[i]) o.axle[i] = i < j ? i : j;
        else if (in[i]) left++;
    }
    if (mainAlong && mainSide && left >= 2)
    {
        for (int i = 0; i < n; i++) in[i] = in[i] && lone[i] && isfinite(mainAlong[i]) && isfinite(mainSide[i]);
        AxlePairs(nullptr, mainAlong, mainSide, in, n, best);
        for (int i = 0; i < n; i++)
        {
            const int j = best[i];
            if (!in[i] || j < 0 || best[j] != i) continue;
            lone[i] = false, o.byMain[i] = true, o.axle[i] = i < j ? i : j;
            if (i < j) o.across++;
        }
    }
    for (int i = 0; i < n; i++)
    {
        if (lone[i]) o.alone++;
        if (o.axle[i] == i) o.axles++;
    }
    return o;
}
// A wheel with no partner in the grouping: its axle holds it alone.
inline bool AxleLone(const AxleGroupsOut &g, int n, int i)
{
    for (int j = 0; j < n; j++)
        if (j != i && g.axle[j] == g.axle[i]) return false;
    return true;
}
// The side each wheel's left and right go by: its parent body's frame for a pair of the first pairing, the main body's
// for one of the second and for a wheel alone, where that was read (mainSide nullptr or not finite: the parent's).
inline void AxleSideBy(const AxleGroupsOut &g, int n, const float *side, const float *mainSide, float *out)
{
    for (int i = 0; i < n; i++)
        out[i] = mainSide && isfinite(mainSide[i]) && (g.byMain[i] || AxleLone(g, n, i)) ? mainSide[i] : side[i];
}
// A driven wheel's axle out of the truck's table (wheels the wheel objects' pointers, axle as AxleGroups gave it): by the
// pointer, since the hook's list holds only the driven wheels in an order of its own. A wheel not in the table gets
// notFound, an axle of its own (M2). place: where the wheel stands in the table, -1 not in it.
inline int AxleOfWheel(const uint64_t *wheels, const int *axle, int n, uint64_t wheel, int notFound, int *place)
{
    for (int s = 0; s < n; s++)
        if (wheels[s] == wheel)
        {
            if (place) *place = s;
            return axle[s];
        }
    if (place) *place = -1;
    return notFound;
}
// The evening by axle: DiffEvenStep over the wheels with the same axle[i], each axle by itself. j is the sum of the
// impulses of the axles evened and used their wheels. An axle with one wheel read is left as the drive left it.
inline DiffEvenOut DiffEvenAxles(const float *pre, const float *post, const float *gain, const bool *ok, const int *axle, int n)
{
    DiffEvenOut o = {};
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    bool done[kCapWheels] = {};
    for (int i = 0; i < n; i++) o.final[i] = post[i];
    for (int i = 0; i < n; i++)
    {
        if (done[i]) continue;
        int at[kCapWheels], m = 0;
        float p[kCapWheels], q[kCapWheels], g[kCapWheels];
        bool k[kCapWheels];
        for (int j = i; j < n; j++)
            if (!done[j] && axle[j] == axle[i]) at[m] = j, p[m] = pre[j], q[m] = post[j], g[m] = gain[j], k[m] = ok[j], done[j] = true, m++;
        const DiffEvenOut e = DiffEvenStep(p, q, g, k, m);
        if (e.used >= 2) o.j += e.j, o.used += e.used;
        for (int s = 0; s < m; s++) o.final[at[s]] = e.final[s];
        if (e.wrote) o.wrote = true;
    }
    return o;
}
// The limit by axle: DiffOpenStep over the wheels of each axle against the same ceiling, so the ceiling holds each
// axle's mean and a wheel alone is held at the ceiling itself (M2). The truck's kept (M3) is what all axles kept of what
// the drive added: the sum of k x added over the axles, over the sum of added; where every axle that got something has
// the same k it is that k itself, to the bit; 1 where the drive added nothing. clamps is the sum, excess the largest.
// The hand-on (handOn; F1 of the review of 122, the tied shaft between the axles), as CapStep hands on between wheels:
// what the limit took from an axle's wheels, as an impulse (each wheel's speed taken over its gain), goes to the axles the
// limit took nothing from and the drive did not push back, by each one's own impulse from the drive (equal where none
// got any). Within the axle that receives, each wheel gets the same impulse through its own gain, and the axle's mean
// stays under the same limit, max(its mean before the drive, the ceiling): what does not fit is dropped in this step and
// kept nowhere. Nothing goes to an axle that gave, so nothing to the partner of a free wheel. Only what the drive added
// in this step moves and no wheel is lowered. The truck's kept then counts what was handed on as kept, as CapStep's:
// the sum of what each wheel gained over the sum of what the drive added. o.spread is the speed handed on.
// For the probe: each axle's mean after the step, its factor, the wheels it holds (a bit each by the place in the list)
// and the speed handed to it, summed over its wheels.
struct OpenAxlesOut
{
    CapStepOut o;
    int axles;
    float mean[kCapWheels], k[kCapWheels], got[kCapWheels];
    uint32_t wheels[kCapWheels];
};
// type (the differential by axle, DiffAxlesStep): each wheel's axle's type, or nullptr for Open everywhere. An axle that
// is not Open is held wheel by wheel, with the spread within the axle (CapStep), in place of the limit on its mean.
inline OpenAxlesOut DiffOpenAxles(const float *pre, const float *even, const float *gain, const bool *ok, const int *axle, int n, float ceiling, bool handOn, const int *type = nullptr)
{
    OpenAxlesOut r = {};
    r.o.kept = 1.0f;
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    bool done[kCapWheels] = {};
    for (int i = 0; i < n; i++) r.o.final[i] = even[i], r.o.keptEach[i] = 1.0f;
    float addedAll = 0.0f, keptAll = 0.0f, kOne = 1.0f;
    bool any = false, alike = true;
    float gave[kCapWheels] = {}, own[kCapWheels] = {};
    bool taker[kCapWheels] = {};
    for (int i = 0; i < n; i++)
    {
        if (done[i]) continue;
        int at[kCapWheels], m = 0;
        float p[kCapWheels], q[kCapWheels], g[kCapWheels];
        bool k[kCapWheels];
        for (int j = i; j < n; j++)
            if (!done[j] && axle[j] == axle[i]) at[m] = j, p[m] = pre[j], q[m] = even[j], g[m] = gain[j], k[m] = ok[j], done[j] = true, m++;
        const CapStepOut a = !type || type[i] == kDiffOpen ? DiffOpenStep(p, q, g, k, m, ceiling) : CapStep(p, q, g, k, m, ceiling, true);
        float added = 0.0f, sum = 0.0f;
        int read = 0;
        uint32_t mask = 0;
        for (int s = 0; s < m; s++)
        {
            const int w = at[s];
            r.o.final[w] = a.final[s];
            r.o.keptEach[w] = a.keptEach[s];
            r.o.clamped[w] = a.clamped[s];
            mask |= 1u << w;
            if (!k[s] || !isfinite(p[s]) || !isfinite(q[s])) continue;
            if (q[s] > p[s]) added += q[s] - p[s];
            sum += a.final[s], read++;
        }
        r.o.clamps += a.clamps;
        if (a.excess > r.o.excess) r.o.excess = a.excess;
        if (added > 0.0f && isfinite(added))
        {
            addedAll += added, keptAll += a.kept * added;
            if (!any) kOne = a.kept, any = true;
            else if (a.kept != kOne) alike = false;
        }
        // for the hand-on: what the limit took here as an impulse, the drive's own impulse, and whether this axle may receive
        bool takes = a.clamps == 0 && read > 0;
        for (int s = 0; s < m; s++)
        {
            if (!k[s] || !isfinite(p[s]) || !isfinite(q[s])) continue;
            if (!(g[s] > 0.0f) || !isfinite(g[s]) || q[s] < p[s])
            {
                takes = false;
                continue;
            }
            gave[r.axles] += (q[s] - a.final[s]) / g[s];
            own[r.axles] += (q[s] - p[s]) / g[s];
        }
        taker[r.axles] = takes;
        r.mean[r.axles] = read ? sum / (float)read : 0.0f;
        r.k[r.axles] = a.kept;
        r.wheels[r.axles] = mask;
        r.axles++;
    }
    if (any) r.o.kept = alike ? kOne : addedAll > 0.0f && isfinite(keptAll / addedAll) ? CutClamp01(keptAll / addedAll) : 1.0f;
    float pool = 0.0f, ownSum = 0.0f;
    int takers = 0;
    for (int a = 0; a < r.axles; a++)
    {
        if (gave[a] > 0.0f && !taker[a]) pool += gave[a];
        if (taker[a]) ownSum += own[a], takers++;
    }
    if (!handOn || !takers || !(pool > 0.0f) || !isfinite(pool) || !isfinite(ownSum)) return r;
    for (int a = 0; a < r.axles; a++)
    {
        if (!taker[a]) continue;
        const float impulse = pool * (ownSum > 1e-9f ? own[a] / ownSum : 1.0f / (float)takers);
        float before = 0.0f, now = 0.0f, add = 0.0f;
        int count = 0;
        for (int i = 0; i < n; i++)
            if ((r.wheels[a] >> i & 1u) && ok[i] && isfinite(pre[i]) && isfinite(even[i])) before += pre[i], now += r.o.final[i], add += gain[i], count++;
        if (!count || !(impulse > 0.0f) || !isfinite(impulse)) continue;
        add *= impulse / (float)count; // the speed the whole share would add, summed over the axle's wheels
        const float meanBefore = before / (float)count;
        const float room = (meanBefore > ceiling ? meanBefore : ceiling) * (float)count - now;
        if (!(add > 0.0f) || !isfinite(add) || !(room > 0.0f)) continue;
        float part = 1.0f;
        if (add > room) part = room / add, r.o.clamps++;
        float sum = 0.0f;
        for (int i = 0; i < n; i++)
        {
            if (!((r.wheels[a] >> i & 1u) && ok[i] && isfinite(pre[i]) && isfinite(even[i]))) continue;
            const float to = r.o.final[i] + part * impulse / (float)count * gain[i];
            if (isfinite(to) && to > r.o.final[i])
            {
                r.got[a] += to - r.o.final[i], r.o.spread += to - r.o.final[i];
                r.o.final[i] = to;
                if (part < 1.0f) r.o.clamped[i] = true;
            }
            sum += r.o.final[i];
        }
        r.mean[a] = sum / (float)count;
    }
    if (r.o.spread > 0.0f)
    {
        float keptSum = 0.0f, pushSum = 0.0f;
        for (int i = 0; i < n; i++)
        {
            if (!ok[i] || !isfinite(pre[i]) || !isfinite(even[i])) continue;
            if (even[i] > pre[i]) pushSum += even[i] - pre[i];
            if (r.o.final[i] > pre[i]) keptSum += r.o.final[i] - pre[i];
        }
        r.o.kept = pushSum > 1e-4f ? CutClamp01(keptSum / pushSum) : 1.0f;
    }
    return r;
}
// ---- the differential by axle (BRIEF_axle_diff.md with the Reviewer's addendum, M3) ----
// Each driven axle has a type of its own. Where the axles of a step differ, after the game's drive:
//   1. within each axle, on what the drive left: Open evens the drive (DiffEvenStep), Locked ties the axle's wheels
//      (LockStepHeld over them alone, with the truck's mass times the axle's wheels over the driven wheels read, so each
//      wheel weighs what it weighs in the whole truck's step), Limited slip does nothing;
//   2. with no ceiling (a gear with no cap) that is written and nothing passes between the axles;
//   3. with a ceiling each axle goes against the same one: Open on the axle's mean (DiffOpenStep), Limited slip and
//      Locked on each wheel with the spread within the axle (CapStep);
//   4. between the axles the hand-on Open has, by DiffOpenAxles' own rule;
//   5. the kept share is DiffOpenAxles' formula over all axles.
// type: each wheel's axle's type (kDiffOpen, kDiffLimited or kDiffLocked); an axle goes by its first wheel's. k: the
// lock's factor of this step (LockK), 0 ties nothing. With every type Open this is DiffEvenAxles and then DiffOpenAxles,
// to the bit. No wheel is lowered where the drive's impulse is over 0: every part works from this step's speeds, and no
// limit is under a wheel's own speed before the drive.
struct DiffAxlesOut
{
    OpenAxlesOut r;         // the speeds to write and each axle's figures, in the order of the axles' first wheels
    float work[kCapWheels]; // the speeds after part 1, before the ceiling
    int type[kCapWheels];   // each axle's type, in r's order
    int tied;               // Locked axles the lock's step ran over (k above 0)
    int lockWrote, lockUnder, lockBounded, lockBreach; // the lock's counts, summed over those axles
    float evenJ;            // the Open axles' impulse, summed, and their wheels
    int evenUsed;
    bool evenWrote;
};
inline DiffAxlesOut DiffAxlesStep(const float *pre, const float *post, const float *gain, const float *load, const float *radius, const bool *ok, const int *axle, const int *type,
                                  int n, float k, float mass, float ceiling, bool handOn)
{
    DiffAxlesOut x = {};
    if (n > kCapWheels) n = kCapWheels;
    if (n < 0) n = 0;
    int read = 0, axles = 0;
    for (int i = 0; i < n; i++) x.work[i] = post[i], read += ok[i] ? 1 : 0;
    bool done[kCapWheels] = {};
    int first[kCapWheels]; // each wheel's axle's type, the first wheel's
    for (int i = 0; i < n; i++)
    {
        if (done[i]) continue;
        int at[kCapWheels], m = 0, used = 0;
        float p[kCapWheels], q[kCapWheels], g[kCapWheels], ld[kCapWheels], rd[kCapWheels];
        bool u[kCapWheels];
        for (int j = i; j < n; j++)
            if (!done[j] && axle[j] == axle[i])
                at[m] = j, p[m] = pre[j], q[m] = post[j], g[m] = gain[j], ld[m] = load[j], rd[m] = radius[j], u[m] = ok[j], used += ok[j] ? 1 : 0, first[j] = type[i], done[j] = true, m++;
        x.type[axles++] = type[i];
        if (type[i] == kDiffOpen)
        {
            const DiffEvenOut e = DiffEvenStep(p, q, g, u, m);
            if (e.used >= 2) x.evenJ += e.j, x.evenUsed += e.used;
            for (int s = 0; s < m; s++) x.work[at[s]] = e.final[s];
            if (e.wrote) x.evenWrote = true;
        }
        else if (type[i] == kDiffLocked && k > 0.0f)
        {
            const LockStepOut l = LockStepHeld(p, q, g, ld, rd, u, m, k, read > 0 && mass > 0.0f ? mass * (float)used / (float)read : 0.0f);
            for (int s = 0; s < m; s++) x.work[at[s]] = l.final[s];
            x.tied++;
            x.lockWrote += l.wrote ? 1 : 0, x.lockUnder += l.under, x.lockBounded += l.bounded ? 1 : 0, x.lockBreach += l.breach;
        }
    }
    x.r = DiffOpenAxles(pre, x.work, gain, ok, axle, n, ceiling, handOn, first);
    return x;
}
// What the mod's thread publishes for the driven truck: its action and vehicle and DiffLockBite, while Powertrain is on,
// the setting above 0 and the truck's lock kind read and known (vehicle + 0xE8), with what LockStepHeld needs.
struct LockSlot
{
    uint64_t action, vehicle;
    uint64_t at; // GetTickCount64 at the publish
    int bite;    // DiffLockBite, 1 to 100
    int kind;    // vehicle + 0xE8: 0 a lock fitted, 1 none, 2 always on
    float mass;  // the truck's by its files, kg; 0 not known
    uint64_t cylinderVtable; // hkpCylinderShape's vtable for the wheels' rolling radius, 0 not found
};
// Whether the mod's thread publishes the bite for a truck of this lock kind: only with the setter's pattern found once
// at the start and the kind read and known. A truck whose lock is always on (kind 2) gets the bite too (A3 of the diff
// types' addendum; DiffLockBiteAlways is gone).
inline bool LockPublishes(bool patternFound, bool kindRead, int kind)
{
    return patternFound && kindRead && (kind == 0 || kind == 1 || kind == 2); // a kind not known publishes nothing (the review of 105)
}
// Whether the wheels' drive's hook is asked for (gauges.cpp, CreepHookStep): Creep, Probe, the damper's settings, the
// torque cut, the diff lock's bite (Powertrain on and DiffLockBite above 0; the Reviewer's R4), or the drive display
// (DriveDisplay=1, whatever else is on: 131's M2).
inline bool DriveHookWanted(bool creep, bool probe, bool damper, bool cut, bool powertrain, int bite, bool display = false)
{
    return creep || probe || damper || cut || (powertrain && bite > 0) || display;
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
