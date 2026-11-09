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
};
struct SimResult
{
    float settledAt;      // seconds until s stayed within 5 % of the top of its later mean, -1 never
    float mean, sd;       // s over the 10 s after it settled (or the last 10 s)
    int cuts;             // steps where the bracket fell by 0.3 or more in one step
    int cutsAfter;        // of them, in the measured span
    float cutsPerSecond;
    float period;         // the mean seconds between cuts (0 none)
    float vEnd, sEnd;     // the ground's and the wheels' speed at the end
    float fMin, fMax;     // the bracket over the measured span
    float pinned;         // the share of the steps after the first second with s at or over the needle's stop (1.1 of the top)
    float wMax;           // the fastest wheel after the first second, rad/s
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
    float wMax = 0.0f;
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
        const CutSpeeds sp = TorqueCutSpeeds(w, driven, n, 1);
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
                    held = TorqueCutAnswerOf(u.slot, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, sp).bracket;
                    nextPass = t + u.passFrom + (u.passTo - u.passFrom) * SimRand(seed);
                }
                bracket = held;
            }
            else bracket = TorqueCutAnswerOf(u.slot, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, sp).bracket;
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
    r.cuts = cuts;
    for (int k = from + 1; k < to; k++)
        if (brackets[k - 1] - brackets[k] >= 0.3f) r.cutsAfter++;
    r.cutsPerSecond = cuts / (total * dt);
    r.period = cuts > 1 ? (lastCut - firstCut) * dt / (float)(cuts - 1) : 0.0f;
    r.vEnd = v;
    r.pinned = countedSteps ? (float)pinnedSteps / (float)countedSteps : 0.0f;
    r.wMax = wMax;
    r.sEnd = trace[total > 0 ? total - 1 : 0];
    return r;
}
