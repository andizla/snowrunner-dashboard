// torque_cut_sim.h: the closed loop of the torque cut (BRIEF_drivetrain_2_torque_cut.md, the Reviewer's M4), for
// models_test: the game's wheels' drive c41d10 sketched with its own terms at its 60 steps a second (dt 1/60, no sub
// steps), on driven wheels in mud of a noisy grip. The drive's terms (the Reviewer's addendum, read on 6adb8627):
// - each wheel's speed turned into the gear's direction, floored at 0 and cut to whole rad/s (c42113 to c42130);
// - w + 0x118, each wheel's change of its whole speed a step, 0.7 new and 0.3 old (c42324 to c42350);
// - the clamp, clamp01(clamp01(0.5 - 2 x w118 / MaxDeltaAngVel) + 1 - clamp01((|w| - 1) x 0.5)), with the float |w| (the
//   + 0x170 term taken as 0);
// - the share w + 0x110 from the same integers, 0.8 old and 0.2 new, an open diff's: a wheel loses share by (the fastest
//   - its own) / clamp(mean / 4, 0.1, 2);
// - B4: 1 under the cut (the throttle held at 1, B4 written 1 before the drive); in the game's own, its stock rule
//   (target (1 - D4 + D4 x curve) x throttle at the integers' (max + min) / 2, rate EngineResponsiveness x pf a step);
// - the torque per wheel: min(B4, throttle) x T x clamp x share over the wheels.
// - the overspeed brake per wheel (c42c5b to c42d75, BRIEF_drive_2108_findings.md): above min(the fastest whole speed -
//   0.5, the curve's top - 0.5), floored at 0, min((w - that) x 0.2, 1)^2 x 1024 / 60 of angular impulse a step against
//   the gear, over the wheel's inertia (inertia 0 leaves the brake out). The cut's top: the governor's end with F1
//   (cutTop), 1e9 without (the old outputs, so only the fastest - 0.5 binds);
// - the cut's bracket paced as the game's B4 rose (F1b, CutRise at rise a step; 0 leaves it whole at once).
// - the wheel speed cap (BRIEF_drivetrain_2c_wheel_cap.md, cap above 0): the drive's impulse at once, the overspeed brake,
//   then CapStep (the cap, the spread to the other wheels, the kept share) on the wheels' speeds before the ground, and F
//   without the governor's fall while armed (CapArmStep, M3; capAtOnce arms from the first step, the counter-case).
// The answer F is the detour's (torque_cut.h) from this step's wheels, or in the counter-case held from a pass of the
// mod's thread every 100 ms (or 50 to 120 ms), as the factor reached the game before. freeWheels of the wheels (the first
// ones) have no grip and little drag: a wheel lifted or in a slurry.
#pragma once
#include "torque_cut.h"
#include <math.h>
#include <stdint.h>

enum { kSimCut, kSimHeld, kSimGame, kSimFlat };
struct SimGround
{
    float grip;      // the most torque the ground takes off a wheel, rad/s^2 of its own speed (its mean)
    float noise;     // its spread, a share of grip (low passed over kSimNoiseLag)
    float drag;      // the mud's drag on a spinning wheel, rad/s^2 per rad/s
    float roll;      // what the ground holds the truck back by, rad/s^2 of the wheels' speed over the ground
    float grade;     // the slope's pull, the same units (above 0 downhill: it pushes the truck)
};
static const float kSimNoiseLag = 0.08f;
struct SimSetup
{
    int mode;              // kSimCut, kSimHeld (F held between passes of the mod's thread) or kSimGame
    float passFrom, passTo; // the mod's pass in seconds, a fresh one drawn between the two each pass (kSimHeld)
    float drive;           // rad/s^2 a wheel gets at a bracket of 1 with clamp and share at 1 (the game's T for the gear)
    float mdav;            // MaxDeltaAngVel (the 18:21 log: 0.02 and 0.05)
    float w170;            // the clamp's + 0x170 term's figure (0.1 x max(it - 2, 0)); 0 leaves it out
    float flat;            // kSimFlat: the bracket held at this, the game's terms and the mud alone
    float pf, gameA, gameStart, gameTop, response; // the game's own: power factor, the gear's a, start and top, EngineResponsiveness
    int wheels;
    float s0, v0;          // the wheels' and the ground's speed at the start, rad/s
    float seconds;
    TorqueCutSlot slot;    // what the mod's thread published (mode kCutMap)
    SimGround ground;
    uint32_t seed;
    float inertia;         // a wheel's, kg m^2, for the overspeed brake's impulse; 0 no brake
    bool cutTop;           // the cut answers with the finite top (F1); false: top 1e9
    float rise;            // the bracket's pace a step (F1b, EngineResponsiveness x pf); 0 whole at once
    float used0;           // where the pace starts (S1: the game's own pull at a take-over); 0 from 0
    int freeWheels;        // the first ones without grip
    float cap;             // the wheel speed cap over the gear's top (WheelCap over 100: the guard), 0 none
    bool capAtOnce;        // the cap armed from the first step (without M3's wait)
    bool governTruck;      // the governor's fade by the truck's own speed (M5): the ground's speed, in the wheels' units here
};
struct SimResult
{
    float settledAt;      // seconds until s stayed within 5 % of the top of its later mean, -1 never
    float mean, sd;       // s over the 10 s after it settled (or the last 10 s)
    int cuts;             // steps where the bracket fell by 0.3 or more in one step
    int cutsAfter;        // of them, in the measured span
    int smallFalls;       // steps in the measured span where the bracket fell by 0.15 or more (BRIEF_overrev_downshift.md, R2)
    float sMin;           // the least s over the measured span
    float cutsPerSecond;
    float period;         // the mean seconds between cuts (0 none)
    float vEnd, sEnd;     // the ground's and the wheels' speed at the end
    float fMin, fMax;     // the bracket over the measured span
    float pinned;         // the share of the steps after the first second with s at or over the needle's stop (1.1 of the top)
    float wMax;           // the fastest wheel after the first second, rad/s
    float wIn;            // the cap: the fastest wheel handed to the ground after the cap, after the first second, rad/s
    float needle;         // the cap: the needle's mean over the measured span (a wheel held at the cap reads it, M2)
    float kept;           // the cap: the mean kept share over the measured span (M1)
    float vHalf;          // the ground's speed at 0.5 s
};

inline float SimRand(uint32_t &s)
{
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFF) / 65535.0f;
}

inline SimResult SimRun(const SimSetup &u)
{
    const float dt = 1.0f / 60.0f;
    const int n = u.wheels > 0 && u.wheels <= 8 ? u.wheels : 4;
    float w[8], d118[8] = {}, share[8], noise[8] = {};
    int whole[8] = {};
    for (int i = 0; i < n; i++) w[i] = u.s0 * (1.0f + 0.02f * (float)(i - n / 2)), share[i] = 1.0f, whole[i] = (int)fmaxf(w[i], 0.0f);
    float v = u.v0, b4 = 0.0f, held = 0.0f, nextPass = 0.0f, lastBracket = 0.0f, used = CutClamp01(u.used0);
    int pinnedSteps = 0, countedSteps = 0;
    float wMax = 0.0f, wIn = 0.0f, vHalf = u.v0;
    CapArm arm = { 0x7FFFFFFF, false };
    const float capV = u.cap > 0.0f ? u.cap * u.slot.map.top : 0.0f;
    TorqueCutSlot slotNow = u.slot; // the slot as the mod publishes it, its cap with it
    slotNow.cap = capV;
    bool armed = false;
    static float needles[60 * 60], keeps[60 * 60];
    uint32_t seed = u.seed ? u.seed : 1u;
    const int steps = (int)(u.seconds / dt);
    // the s trace, for the settle and the spread
    static float trace[60 * 60];
    static float brackets[60 * 60];
    int cuts = 0, firstCut = -1, lastCut = -1;
    for (int k = 0; k < steps && k < 60 * 60; k++)
    {
        const float t = k * dt;
        int wi[8], lo = 0, hi = 0, sum = 0;
        bool driven[8];
        for (int i = 0; i < n; i++)
        {
            wi[i] = (int)fmaxf(w[i], 0.0f);
            lo = i ? (wi[i] < lo ? wi[i] : lo) : wi[i];
            hi = i ? (wi[i] > hi ? wi[i] : hi) : wi[i];
            sum += wi[i];
            driven[i] = true;
        }
        const float mid = 0.5f * (float)(lo + hi), meanInt = (float)sum / (float)n;
        // the detour's answer, or the game's own
        float bracket;
        CutSpeeds sp = TorqueCutSpeeds(w, driven, n, 1);
        if (u.governTruck) CutSpeedsTruck(sp, v, 1.0f, 1);
        if (capV > 0.0f) armed = u.capAtOnce || CapArmStep(arm, 1, sp.mean, sp.ok, capV);
        if (u.mode == kSimFlat) bracket = u.flat;
        else if (u.mode == kSimGame)
        {
            const float curve = TorqueCutStockCurve(u.gameStart, u.gameA, u.gameTop, u.pf, mid);
            const float r = u.response * u.pf;
            b4 = (1.0f - r) * b4 + r * curve; // throttle 1, D4 1
            bracket = b4;
        }
        else
        {
            b4 = 1.0f;
            if (u.mode == kSimHeld)
            {
                if (t >= nextPass)
                {
                    held = TorqueCutAnswerOf(slotNow, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, sp).bracket;
                    nextPass = t + u.passFrom + (u.passTo - u.passFrom) * SimRand(seed);
                }
                bracket = held;
            }
            else bracket = TorqueCutAnswerOf(slotNow, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, sp).bracket;
            if (u.rise > 0.0f) bracket = used = CutRise(used, bracket, u.rise);
        }
        if (k > 0 && lastBracket - bracket >= 0.3f)
        {
            cuts++;
            if (firstCut < 0) firstCut = k;
            lastCut = k;
        }
        lastBracket = bracket;
        // the drive's curve top for the brake, in its units: the game's own scaled by pf, the cut's at the governor's end
        const float curveTop = u.mode == kSimGame ? u.gameTop * u.pf : u.cutTop ? u.slot.map.governTo * u.slot.map.top : 1e9f;
        // the drive's terms per wheel, once a step: the torque it hands the physics for the whole step
        float torque[8], grip[8];
        for (int i = 0; i < n; i++)
        {
            d118[i] = 0.7f * (float)(wi[i] - whole[i]) + 0.3f * d118[i];
            whole[i] = wi[i];
            const float traction = CutClamp01(0.5f - 2.0f * d118[i] / u.mdav);
            const float clamp = CutClamp01(0.1f * fmaxf(u.w170 - 2.0f, 0.0f) + traction + 1.0f - CutClamp01((fabsf(w[i]) - 1.0f) * 0.5f));
            const float target = CutClamp01(1.0f - (float)(hi - wi[i]) / fminf(fmaxf(meanInt / 4.0f, 0.1f), 2.0f));
            share[i] = 0.8f * share[i] + 0.2f * target;
            torque[i] = bracket * u.drive * clamp * share[i];
            // the ground's grip with noise, low passed
            noise[i] += (1.0f - expf(-dt / kSimNoiseLag)) * ((SimRand(seed) * 2.0f - 1.0f) * 1.732f - noise[i]);
            grip[i] = i < u.freeWheels ? 0.0f : u.ground.grip * fmaxf(0.0f, 1.0f + u.ground.noise * noise[i] * 2.5f);
        }
        // the overspeed brake, an impulse this step, applied with the drive's torque before the physics
        if (u.inertia > 0.0f)
        {
            const float threshold = fmaxf(fminf((float)hi - 0.5f, curveTop - 0.5f), 0.0f);
            for (int i = 0; i < n; i++)
                if (w[i] > threshold)
                {
                    const float x = fminf((w[i] - threshold) * 0.2f, 1.0f);
                    w[i] -= fminf(x * x * 1024.0f / 60.0f / u.inertia, w[i] - threshold);
                }
        }
        // the cap: the drive's impulse at once, then the cap and the spread over the speeds the ground gets (equal wheels:
        // gain 1); the ground's sub steps then without the drive's torque
        // (BRIEF_governor_sway.md, C4: the step on every step, armed or waiting, its ceiling by the truck's speed)
        float needleNow = sp.mean, keptNow = 1.0f;
        bool clampedNow[8] = {};
        if (capV > 0.0f)
        {
            float pre[8], post[8], gain[8];
            bool ok[8];
            for (int i = 0; i < n; i++) pre[i] = w[i], post[i] = w[i] + torque[i] * dt, gain[i] = 1.0f, ok[i] = true, torque[i] = 0.0f;
            const CapStepOut o = CapStep(pre, post, gain, ok, n, CapCeiling(capV, u.slot.map.top, u.slot.map.governTo, u.governTruck ? v : NAN), true);
            for (int i = 0; i < n; i++) w[i] = o.final[i], clampedNow[i] = o.clamped[i];
            keptNow = o.kept;
            for (int i = 0; i < n; i++)
                if (t >= 1.0f) wIn = fmaxf(wIn, w[i]);
        }
        // the physics (Havok's own solver, stable where the ground couples the wheel to the truck stiffly): ten small
        // steps inside the drive's one, the drive's torque held through them
        for (int sub = 0; sub < 10; sub++)
        {
            const float h = dt / 10.0f;
            float pushSum = 0.0f;
            for (int i = 0; i < n; i++)
            {
                const float ground = grip[i] * tanhf((w[i] - v) / 0.3f);
                w[i] += (torque[i] - ground - (i < u.freeWheels ? 0.2f : u.ground.drag) * w[i]) * h;
                pushSum += ground;
            }
            // the truck: the wheels' push over its mass (a wheel's push moves it a tenth as much), the ground's hold, the grade
            const float pushV = pushSum / (float)n * 0.1f;
            v += (pushV - (v > 0.0f ? u.ground.roll : v < 0.0f ? -u.ground.roll : 0.0f) + u.ground.grade) * h;
            if (fabsf(v) < u.ground.roll * h && fabsf(pushV + u.ground.grade) < u.ground.roll) v = 0.0f;
        }
        trace[k] = sp.mean;
        brackets[k] = bracket;
        if (armed)
        {
            // M2: the needle after the solve, a wheel held at the cap this step reading the cap
            float nsum = 0.0f;
            for (int i = 0; i < n; i++) nsum += clampedNow[i] ? capV : fmaxf(w[i], 0.0f);
            needleNow = nsum / (float)n;
        }
        needles[k] = needleNow;
        keeps[k] = keptNow;
        if (k == 30) vHalf = v;
        if (t >= 1.0f)
        {
            countedSteps++;
            const float topNow = u.slot.map.top > 0.0f ? u.slot.map.top : 1.0f;
            if (sp.mean >= 1.1f * topNow) pinnedSteps++;
            for (int i = 0; i < n; i++) wMax = fmaxf(wMax, w[i]);
        }
    }
    SimResult r = {};
    const int total = steps < 60 * 60 ? steps : 60 * 60;
    const float top = u.slot.map.top > 0.0f ? u.slot.map.top : 1.0f;
    // settled: from the first step after which s stays within 5 % of the top of the mean of the last 10 s
    const int span = (int)(10.0f / dt);
    float tail = 0.0f;
    const int tailFrom = total > span ? total - span : 0;
    for (int k = tailFrom; k < total; k++) tail += trace[k];
    tail /= (float)(total - tailFrom > 0 ? total - tailFrom : 1);
    int settle = -1;
    for (int k = total - 1; k >= 0; k--)
        if (fabsf(trace[k] - tail) > 0.05f * top) { settle = k + 1; break; }
    if (settle < 0) settle = 0;
    r.settledAt = settle < total ? settle * dt : -1.0f;
    const int from = settle < total - span ? settle : tailFrom, to = from + span < total ? from + span : total;
    float m = 0.0f, q = 0.0f;
    r.fMin = 1e9f, r.fMax = -1e9f;
    for (int k = from; k < to; k++) m += trace[k], r.fMin = fminf(r.fMin, brackets[k]), r.fMax = fmaxf(r.fMax, brackets[k]);
    m /= (float)(to - from > 0 ? to - from : 1);
    for (int k = from; k < to; k++) q += (trace[k] - m) * (trace[k] - m);
    r.mean = m;
    r.sd = sqrtf(q / (float)(to - from > 0 ? to - from : 1));
    float nm = 0.0f, km = 0.0f;
    for (int k = from; k < to; k++) nm += needles[k], km += keeps[k];
    r.needle = nm / (float)(to - from > 0 ? to - from : 1);
    r.kept = km / (float)(to - from > 0 ? to - from : 1);
    r.wIn = wIn;
    r.vHalf = vHalf;
    r.cuts = cuts;
    r.sMin = 1e9f;
    for (int k = from; k < to; k++) r.sMin = fminf(r.sMin, trace[k]);
    for (int k = from + 1; k < to; k++)
    {
        if (brackets[k - 1] - brackets[k] >= 0.3f) r.cutsAfter++;
        if (brackets[k - 1] - brackets[k] >= 0.15f) r.smallFalls++;
    }
    r.cutsPerSecond = cuts / (total * dt);
    r.period = cuts > 1 ? (lastCut - firstCut) * dt / (float)(cuts - 1) : 0.0f;
    r.vEnd = v;
    r.pinned = countedSteps ? (float)pinnedSteps / (float)countedSteps : 0.0f;
    r.wMax = wMax;
    r.sEnd = trace[total > 0 ? total - 1 : 0];
    return r;
}

// The governor's end in a low gear (BRIEF_governor_sway.md, B4 and C6): each driven wheel a body of its own light inertia,
// a grip from its slip, and the truck behind them. A step, in the drive's order: the cut's answer from the wheels and the
// truck (or a bracket held flat), paced by CutRise; the game's clamp and share terms and its overspeed brake as in SimRun;
// the drive's kick at once (kick rad/s a step at a bracket of 1); the wheel cap's step; then the ground in ten sub steps,
// each wheel giving up at most grip rad/s a step and never more than its slip, the truck gaining couple times what a wheel
// gives up (a wheel's inertia over the truck's mass times the radius squared); then the truck's drag and the grade.
// Speeds in rad/s of the wheels, the truck's over the rolling radius.
// The law: the ceiling by the truck's speed (CapCeiling) in the cap's step on every step, no fade in F. oldLaw: the
// law before it, kept here for the counter-cases: the governor's factor by the truck's speed on the bracket, the cap flat
// and written only once armed by the mean.
struct TopSetup
{
    TorqueCutSlot slot;   // mode kCutMap, the map's top and band
    float cap;            // the wheel cap over the gear's top, 0 none
    int wheels, freeWheels;
    float kick;           // rad/s a wheel gains a step at a bracket of 1 with the game's clamp and share at 1
    float grip, gripNoise; // rad/s a step the ground takes off a slipping wheel, and its spread
    float couple;         // the truck's gain per rad/s a wheel gives up
    float drag, dragNoise; // rad/s a step the truck loses, and its slow spread
    float grade;          // rad/s a step (above 0 downhill)
    float mdav, inertia;
    float flat;           // at or above 0: the bracket held at this, unpaced (C3)
    float holdV;          // at or above 0: the truck's speed held at this (C3)
    float s0, v0, seconds, rise;
    float used0;          // the bracket used before the first step (a take-over's pull)
    float footUpAt;       // above 0: the pedal goes to 0 from this second on
    bool oldLaw;          // the law before the ceiling: the governor on the bracket, the cap flat and armed by the mean
    uint32_t seed;
};
struct TopResult
{
    float vMin, vMax, vEnd;     // the truck after the first 3 s
    float vMost;                // the truck's largest over the whole run
    float gainOver;             // the truck's gain from the wheels while it is over the governor's end by 2 percent of the top, summed
    float freeMost;             // the fastest free wheel after the cap's step, the whole run
    float clampShare;           // clamped wheel steps over driven wheel steps, after 3 s
    float preMin, preMax, postMin, postMax; // the driven mean before and after the drive, after 3 s
    float preSwing, postSwing;  // the largest change from one second's sample to the next, as the probe samples
    float fMin, fMax, usedMax;
    float push, pushJump;       // the truck's gain a step from the wheels: its mean and its largest change step to step, after 3 s
    float needleMin, needleMax; // rpm, after 3 s
    float wMax;                 // the fastest gripping wheel handed to the ground, after 3 s
    int lowered;                // wheel steps where the cap's step left a wheel under min(pre, post)
    float pull;                 // what the gripping wheels gave up to the ground, summed over the run, rad/s
};

inline TopResult TopRun(const TopSetup &u)
{
    const int n = u.wheels > 0 && u.wheels <= 8 ? u.wheels : 4;
    const float top = u.slot.map.top, capV = u.cap > 0.0f ? u.cap * top : 0.0f;
    float w[8], d118[8] = {}, share[8], noise[8] = {};
    int whole[8] = {};
    for (int i = 0; i < n; i++) w[i] = u.s0, share[i] = 1.0f, whole[i] = (int)fmaxf(w[i], 0.0f);
    float v = u.holdV >= 0.0f ? u.holdV : u.v0, used = u.used0, slow = 0.0f, lastPush = 0.0f, lastPre = 0.0f, lastPost = 0.0f;
    TorqueCutSlot slot = u.slot;
    slot.cap = capV;
    CapArm arm = { 0x7FFFFFFF, false };
    uint32_t seed = u.seed ? u.seed : 1u;
    TopResult r = {};
    r.vMin = r.preMin = r.postMin = r.fMin = r.needleMin = 1e9f;
    r.vMax = r.preMax = r.postMax = r.fMax = r.needleMax = -1e9f;
    int counted = 0, clamps = 0;
    double pushSum = 0.0;
    const int steps = (int)(u.seconds * 60.0f);
    for (int k = 0; k < steps; k++)
    {
        const bool late = k >= 180;
        if (u.footUpAt > 0.0f && k >= (int)(u.footUpAt * 60.0f)) slot.pedal = slot.map.pedal = 0.0f;
        int wi[8], hi = 0, sum = 0;
        bool driven[8], ok[8];
        for (int i = 0; i < n; i++)
        {
            wi[i] = (int)fmaxf(w[i], 0.0f);
            hi = i ? (wi[i] > hi ? wi[i] : hi) : wi[i];
            sum += wi[i];
            driven[i] = ok[i] = true;
        }
        const float meanInt = (float)sum / (float)n;
        CutSpeeds sp = TorqueCutSpeeds(w, driven, n, 1);
        CutSpeedsTruck(sp, v, 1.0f, 1);
        float bracket;
        if (u.flat >= 0.0f) bracket = used = u.flat;
        else
        {
            bracket = TorqueCutAnswerOf(slot, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, sp).bracket;
            if (u.oldLaw && bracket > 0.0f) bracket *= CutGovernor(v / top, slot.map.governFrom, slot.map.governTo);
            used = u.rise > 0.0f ? CutRise(used, bracket, u.rise) : bracket;
        }
        float pre[8], post[8], gain[8];
        const float threshold = fmaxf(fminf((float)hi - 0.5f, u.slot.map.governTo * top - 0.5f), 0.0f);
        for (int i = 0; i < n; i++)
        {
            d118[i] = 0.7f * (float)(wi[i] - whole[i]) + 0.3f * d118[i];
            whole[i] = wi[i];
            const float traction = CutClamp01(0.5f - 2.0f * d118[i] / u.mdav);
            const float clamp = CutClamp01(traction + 1.0f - CutClamp01((fabsf(w[i]) - 1.0f) * 0.5f));
            const float target = CutClamp01(1.0f - (float)(hi - wi[i]) / fminf(fmaxf(meanInt / 4.0f, 0.1f), 2.0f));
            share[i] = 0.8f * share[i] + 0.2f * target;
            pre[i] = w[i];
            gain[i] = 1.0f;
            float brake = 0.0f;
            if (u.inertia > 0.0f && w[i] > threshold)
            {
                const float x = fminf((w[i] - threshold) * 0.2f, 1.0f);
                brake = fminf(x * x * 1024.0f / 60.0f / u.inertia, w[i] - threshold);
            }
            post[i] = w[i] - brake + used * u.kick * clamp * share[i];
            noise[i] += (1.0f - expf(-1.0f / 60.0f / kSimNoiseLag)) * ((SimRand(seed) * 2.0f - 1.0f) * 1.732f - noise[i]);
        }
        const bool armed = capV > 0.0f && CapArmStep(arm, 1, sp.mean, sp.ok, capV);
        float postMean = 0.0f;
        int clampsNow = 0;
        if (capV > 0.0f && (armed || !u.oldLaw))
        {
            const CapStepOut o = CapStep(pre, post, gain, ok, n, u.oldLaw ? capV : CapCeiling(capV, top, slot.map.governTo, v), true);
            for (int i = 0; i < n; i++)
            {
                if (o.final[i] < fminf(pre[i], post[i]) - 1e-5f) r.lowered++;
                w[i] = o.final[i];
            }
            clampsNow = o.clamps;
        }
        else
            for (int i = 0; i < n; i++) w[i] = post[i];
        for (int i = 0; i < n; i++) postMean += w[i] / (float)n;
        for (int i = 0; i < u.freeWheels && i < n; i++) r.freeMost = fmaxf(r.freeMost, w[i]);
        // the ground
        float push = 0.0f;
        for (int sub = 0; sub < 10; sub++)
            for (int i = 0; i < n; i++)
            {
                if (i < u.freeWheels)
                {
                    w[i] -= 0.2f * w[i] / 600.0f;
                    continue;
                }
                const float slip = w[i] - v, g = u.grip * fmaxf(0.0f, 1.0f + u.gripNoise * noise[i] * 2.5f) / 10.0f;
                float a = g * tanhf(slip / 0.3f);
                const float most = fabsf(slip) / (1.0f + u.couple);
                if (fabsf(a) > most) a = slip > 0.0f ? most : -most;
                w[i] -= a;
                if (a > 0.0f && v > (slot.map.governTo + 0.02f) * top) r.gainOver += u.couple * a;
                if (u.holdV < 0.0f) v += u.couple * a;
                push += u.couple * a;
                r.pull += a;
            }
        slow += (1.0f - expf(-1.0f / 60.0f)) * ((SimRand(seed) * 2.0f - 1.0f) * 1.732f - slow);
        if (u.holdV < 0.0f)
        {
            v += u.grade - u.drag * fmaxf(0.0f, 1.0f + u.dragNoise * slow * 2.5f);
            if (v < 0.0f) v = 0.0f;
        }
        r.vMost = fmaxf(r.vMost, v);
        if (late)
        {
            counted += n;
            clamps += clampsNow;
            pushSum += push;
            r.vMin = fminf(r.vMin, v), r.vMax = fmaxf(r.vMax, v);
            r.preMin = fminf(r.preMin, sp.mean), r.preMax = fmaxf(r.preMax, sp.mean);
            r.postMin = fminf(r.postMin, postMean), r.postMax = fmaxf(r.postMax, postMean);
            r.fMin = fminf(r.fMin, bracket), r.fMax = fmaxf(r.fMax, bracket);
            r.usedMax = fmaxf(r.usedMax, used);
            const float rpm = TorqueCutRpm(u.slot.map, postMean);
            r.needleMin = fminf(r.needleMin, rpm), r.needleMax = fmaxf(r.needleMax, rpm);
            for (int i = u.freeWheels; i < n; i++) r.wMax = fmaxf(r.wMax, w[i]);
            if (k > 180) r.pushJump = fmaxf(r.pushJump, fabsf(push - lastPush));
            if (k % 60 == 0)
            {
                if (k > 180) r.preSwing = fmaxf(r.preSwing, fabsf(sp.mean - lastPre)), r.postSwing = fmaxf(r.postSwing, fabsf(postMean - lastPost));
                lastPre = sp.mean, lastPost = postMean;
            }
        }
        lastPush = push;
    }
    r.vEnd = v;
    r.clampShare = counted ? (float)clamps / (float)counted : 0.0f;
    r.push = counted ? (float)(pushSum / (double)(counted / n)) : 0.0f;
    return r;
}
