// suspension_step.h: each wheel's travel read every physics step (BRIEF_suspension_step_probe.md, review 14's phase 1).
// The hook on the game's wheels' drive (gearbox_hooks.cpp, CreepDrive) calls SuspStepLook on the physics thread once a
// step for every truck. With Probe=1 the mod's thread publishes one block (SuspStepBlock) for the driven truck: its
// action, its chassis, each wheel's object and the body its height is taken against, and a generation. For that
// truck's calls only, the look reads each wheel's centre (wheel + 0x13C) against that body's position (+ 0x1A0) along
// its up axis (+ 0x180): the height, rising as the suspension compresses. Its change over the step's seconds is the
// shaft speed. Every kStepWindow seconds of steps the figures (the steps, the speeds' histogram, the range, a bounce)
// go into a published slot the mod's thread logs. With SuspStepTest on, wheel 0's every step goes into a ring too.
// On the physics thread: no log, no lock, no heap, no file; every read under __try, a fault drops the block until the
// mod's thread publishes a new one.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "truck.h"

static const float kStepWindow = 10.0f;       // s of steps in one window
static const float kStepWindowLeast = 1.0f;   // s: a window cut shorter by a new generation is not published
static const float kStepDtMost = 0.1f;        // s: a longer step (a pause, a load) gives no speed
static const float kStepMoving = 1.0f;        // m/s of the chassis: a repeated height counts only above it
static const float kStepBounceFrom = 0.5f;    // m/s: the first speed over it in a window starts its bounce
static const float kStepBounce = 1.5f;        // s of a bounce
static const int kStepBounceMost = 512;       // steps of a bounce kept
static const int kStepBins = 8;
static const float kStepBinTop[kStepBins - 1] = { 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f }; // m/s; the last bin is over 2
static const int kStepRing = 2048;            // steps of wheel 0's trace

// ---- the damper (BRIEF_suspension_damper.md with its addendum; review 14's phase 2) ----
// Havok's damper is linear: its force is the damping factor (constraint + 0xC8) times the shaft speed. The damper
// writes that factor each step so the force follows a curve of its own: the truck's damping (its stock times
// DampingScale) times G(v), v the shaft speed of the step just read, compression (rising) positive. L and H are the
// low and high speed factors of compression for v > 0, of rebound for v < 0; with u = |v| / knee and n 1 linear, 0.6
// digressive, 1.6 progressive:
//   G = L for u <= 1 (and under kDamperStill);
//   G = (L + H (u^n - 1)) / u above it: the force L knee up to the knee, then H knee (u^n - 1) on top of it, over |v|.
// All four at 1 and linear give G = 1 at every speed, and then the damper does not run at all.
static const float kDamperStill = 0.005f;     // m/s: under it G is L
static const float kDamperLeast = 0.01f, kDamperMost = 1.0f; // the factor written is held to these (Havok's range, and never 0)
static const float kDamperPower[3] = { 1.0f, 0.6f, 1.6f };
struct DamperSet
{
    int curve;                    // 0 linear, 1 digressive, 2 progressive
    float low[2], high[2];        // [0] compression, [1] rebound
    float knee;                   // m/s
};

// Any setting off its default: the damper runs.
inline bool DamperOn(const DamperSet &d)
{
    return d.curve != 0 || d.low[0] != 1.0f || d.low[1] != 1.0f || d.high[0] != 1.0f || d.high[1] != 1.0f;
}

inline float DamperG(const DamperSet &d, float v)
{
    const int side = v >= 0.0f ? 0 : 1;
    const float L = d.low[side], H = d.high[side], speed = fabsf(v), knee = d.knee > 0.0f ? d.knee : 0.1f;
    const float u = speed / knee;
    if (speed < kDamperStill || u <= 1.0f) return L;
    const int curve = d.curve >= 0 && d.curve < 3 ? d.curve : 0;
    return (L + H * (powf(u, kDamperPower[curve]) - 1.0f)) / u;
}

// ---- the spring's features and the damping by position (BRIEF_suspension_extras.md, build A) ----
// x is a wheel's place along its constraint's axis, from its attach point, where the spring is at zero: compression is
// negative (halving the spring raised the F350's wheel from 150 to 249 mm against the chassis, test 1 of 31 October),
// and SuspensionMin (+ 0xB4) is the compression end. L is the compression travel |SuspensionMin| as tuned, and
// u = -x / L, held to 0 to 1, how far into it the wheel is. Havok's spring force is the strength times x, so a
// feature is given as a rate r(u), the slope dF/dx as a factor of the stock, and written as its secant
// f(u) = (integral of r from 0 to u) / u: the force is then continuous at every zone's edge.
//   - bump stops (the addendum of 1 November, 11:39): with s the share of the stop the wheel is into (BumpZone metres
//     before the end, at most L / 3), the force adds (BumpForce - 1) x the spring's own force at the end x s^3, so at
//     the end it is BumpForce times the spring's, and the compression damping is times 1 + (BumpDamping - 1) s^2. Both
//     go by where the wheel will be a step on (x + v dt, held to the end), as at 2.5 m/s it crosses the stop in two
//     steps and the write lands a step late; the curve and the zones go by where it is;
//   - progressive: r = 1 + (SpringRise - 1) u^2; dual rate: r = 1 under SpringCross, SpringRise over it, blended over
//     kSpringBlend;
//   - the damping zones: ZoneSoft from ZoneFrom to ZoneTo, ZoneFirm toward either end, blended over kZoneBlend.
// The droop side (x > 0) stays at 1.
static const float kSpringBlend = 0.05f, kZoneBlend = 0.1f;
static const float kSpringLeast = 0.001f; // the strength written is held to this and 1
struct SpringSet
{
    int curve;                    // 0 linear, 1 progressive, 2 dual rate
    float rise, cross;
    bool bump;
    float zone, bumpForce, bumpDamping; // m before the end (at most L / 3), factors at the end
    bool zones;
    float soft, firm, from, to;
};

// Two sets alike, field by field: the struct has padding after its bools, which a memcmp would read (the drive of
// 1 November, 01:53: a new generation on every pass with a feature on).
inline bool SpringSame(const SpringSet &a, const SpringSet &b)
{
    return a.curve == b.curve && a.rise == b.rise && a.cross == b.cross && a.bump == b.bump && a.zone == b.zone && a.bumpForce == b.bumpForce &&
           a.bumpDamping == b.bumpDamping && a.zones == b.zones && a.soft == b.soft && a.firm == b.firm && a.from == b.from && a.to == b.to;
}

// The strength is written each step: a curve or the bump stops.
inline bool SpringOn(const SpringSet &k) { return k.curve != 0 || (k.bump && k.bumpForce != 1.0f); }
// The damping goes by position: the zones or the bump stops' damping.
inline bool ZonesOn(const SpringSet &k) { return k.zones || (k.bump && k.bumpDamping != 1.0f); }

// u from x and L.
inline float SuspDepth(float x, float travel)
{
    if (!(travel > 0.0f)) return 0.0f;
    const float u = -x / travel;
    return u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
}

// The stop's metres, at most a third of the travel; where it starts, as u, and the share s of it at u.
inline float BumpZoneOf(const SpringSet &k, float travel) { return fminf(k.zone, travel / 3.0f); }
inline float BumpStart(const SpringSet &k, float travel) { return travel > 0.0f ? 1.0f - BumpZoneOf(k, travel) / travel : 0.0f; }
inline float BumpShare(const SpringSet &k, float u, float travel)
{
    const float u0 = BumpStart(k, travel);
    if (!k.bump || u <= u0) return 0.0f;
    const float s = (u - u0) / (1.0f - u0);
    return s > 1.0f ? 1.0f : s;
}

// The integral from 0 to u of a ramp from 0 at a to 1 at b (a <= b).
inline float RampArea(float u, float a, float b)
{
    if (u <= a) return 0.0f;
    if (b <= a) return u - a;
    if (u <= b) return (u - a) * (u - a) / (2.0f * (b - a));
    return (b - a) / 2.0f + (u - b);
}

// The curve's rate at u and its area from 0 to u (the force over the stock strength times L), the stop left out.
inline float CurveRate(const SpringSet &k, float u)
{
    float r = 1.0f;
    if (k.curve == 1) r += (k.rise - 1.0f) * u * u;
    if (k.curve == 2)
    {
        const float a = k.cross - kSpringBlend / 2.0f, b = k.cross + kSpringBlend / 2.0f;
        r += (k.rise - 1.0f) * (u <= a ? 0.0f : u >= b ? 1.0f : (u - a) / (b - a));
    }
    return r;
}
inline float CurveArea(const SpringSet &k, float u)
{
    float area = u;
    if (k.curve == 1) area += (k.rise - 1.0f) * u * u * u / 3.0f;
    if (k.curve == 2) area += (k.rise - 1.0f) * RampArea(u, k.cross - kSpringBlend / 2.0f, k.cross + kSpringBlend / 2.0f);
    return area;
}

// The stop's force added at u, over the stock strength times L: (BumpForce - 1) x the curve's force at the end x s^3.
inline float BumpArea(const SpringSet &k, float u, float travel)
{
    if (!k.bump) return 0.0f;
    const float s = BumpShare(k, u, travel);
    return (k.bumpForce - 1.0f) * CurveArea(k, 1.0f) * s * s * s;
}

// The slope of the whole force at u (the curve's and the stop's, at one place).
inline float SpringRate(const SpringSet &k, float u, float travel)
{
    float r = CurveRate(k, u);
    if (k.bump)
    {
        const float u0 = BumpStart(k, travel), s = BumpShare(k, u, travel);
        // at the end too: the slope as the wheel arrives there
        if (u0 < 1.0f && s > 0.0f) r += (k.bumpForce - 1.0f) * CurveArea(k, 1.0f) * 3.0f * s * s / (1.0f - u0);
    }
    return r;
}

// The factor on the strength: the whole force over the stock's at u. ahead: where the stop goes by (the place a step on,
// held to the end), -1 for u itself.
inline float SpringF(const SpringSet &k, float u, float travel, float ahead = -1.0f)
{
    if (u < 1e-4f) return 1.0f;
    return (CurveArea(k, u) + BumpArea(k, ahead >= 0.0f ? ahead : u, travel)) / u;
}

// The damping's factor by position, and the bump stops' on compression (v > 0).
inline float ZoneFactor(const SpringSet &k, float u, float v, float travel, float ahead = -1.0f)
{
    float f = 1.0f;
    if (k.zones)
    {
        // 1 inside the soft part, 0 a blend's width outside it
        const float h = kZoneBlend / 2.0f;
        const float in = fminf(fmaxf((u - (k.from - h)) / kZoneBlend, 0.0f), 1.0f) * fminf(fmaxf(((k.to + h) - u) / kZoneBlend, 0.0f), 1.0f);
        f = k.firm + (k.soft - k.firm) * in;
    }
    if (k.bump && v > 0.0f)
    {
        const float s = BumpShare(k, ahead >= 0.0f ? ahead : u, travel);
        f *= 1.0f + (k.bumpDamping - 1.0f) * s * s;
    }
    return f;
}

// The factor written for a wheel whose damping is base (stock times DampingScale) at shaft speed v; clamped is set
// where the clamp held.
inline float DamperFactor(const DamperSet &d, float base, float v, bool &clamped)
{
    const float want = base * DamperG(d, v);
    clamped = want < kDamperLeast || want > kDamperMost;
    return want < kDamperLeast ? kDamperLeast : want > kDamperMost ? kDamperMost : want;
}

// What the mod's thread hands the physics thread, published whole (SuspStepPublish). serial and seal match when it is.
struct SuspStepBlock
{
    uint32_t serial;
    uint32_t generation;          // up on a truck switch, a slider move, a constraint at a new address, a recovery
    uint64_t action, chassis;
    int wheels;
    uint64_t wheel[kMaxWheels];
    uint64_t body[kMaxWheels];    // the body each wheel is measured against: the chassis, or the one its constraint is on
    uint64_t traced;              // read each step for wheel 0's trace (its constraint + 0xC4 or + 0xC8), 0 = no trace
    bool damping;                 // the damper runs on this block
    DamperSet damper;
    uint64_t vtable;              // the constraints' class, checked before each write
    uint64_t constraint[kMaxWheels]; // [wheel + 0x28] when the block was made; 0 = the damper leaves this wheel alone
    float base[kMaxWheels];       // the wheel's damping as the tune gives it (stock times DampingScale), never the field's value
    bool springing;               // the spring's strength is written each step (a curve or the bump stops)
    SpringSet spring;
    float baseK[kMaxWheels];      // the wheel's strength as the tune gives it (stock times SpringScale)
    float attachUp[kMaxWheels], axisUp[kMaxWheels], travel[kMaxWheels]; // for x = (h - attachUp) / axisUp, and L; axisUp 0 = no place
    uint64_t attachAt[kMaxWheels]; // the attach point's component along up, read each step (the game's ride height moves it), 0 = attachUp
    // the attach point's features (build B): the physics thread writes stock + shift along the axis each step
    bool shifting;
    uint64_t attachC[kMaxWheels];  // the constraint whose attach point is written, 0 = this wheel's is left alone
    float attachStock[kMaxWheels]; // its component along up as the game set it
    float ride[kMaxWheels];        // the ride height's shift in m, toward the droop (the corner rises)
    float testShift;               // SuspStepTest=3: wheel 0's shift on top, m
    int partner[kMaxWheels];       // the other wheel of its axle, -1 none
    bool isLeft[kMaxWheels];
    float sway[kMaxWheels];        // its pair's bar as a share of the axle's spring rate, 0 none or disconnected
    float springK[kMaxWheels];     // its strength as the tune gives it (stock times SpringScale), for the bar
    bool air;                      // AirSuspension (build C): each axle held at its rest place, on top of the ride height
    float airKeys;                 // the keys' height set, m toward the droop (followed in 3 s; faded out with air off)
    bool display;                  // the display's figures are published each step
    float travelMax[kMaxWheels];   // the droop end, for the display's bars
    uint32_t seal;
};

// One window's figures, filled on the physics thread.
struct SuspStepWin
{
    uint32_t generation;
    int wheels;
    float seconds;
    int steps;
    float dtLeast, dtMost;
    int moving[kMaxWheels], repeats[kMaxWheels];  // steps over kStepMoving, and of them those with the last step's height
    int bins[kMaxWheels][2][kStepBins];           // [0] compression (rising, v >= 0), [1] rebound
    float least[kMaxWheels], most[kMaxWheels];    // the height's range
    int written[kMaxWheels], clamped[kMaxWheels]; // the damper's writes, and of them those the clamp held
    float writtenSum[kMaxWheels];                 // the factors it wrote, summed
    int springWritten[kMaxWheels];                // the strength's writes, and what they wrote, summed
    float springSum[kMaxWheels];
    int inZone[kMaxWheels], pastEnd[kMaxWheels];  // steps in the bump zone, and at or past the compression end
    bool rested[kMaxWheels];                      // the rest place was taken: x at the window's first standstill
    float rest[kMaxWheels];
    float bounceAt[kMaxWheels];                   // s into the window where the bounce began
    int bounceSteps[kMaxWheels];
    float bounce[kMaxWheels][kStepBounceMost];    // the height each step from there, in m
};

struct SuspStepTraceEntry
{
    uint32_t step;
    float dt, field, h, v;
};

// The display's figures each step (build B, item 4): each wheel's place along its axis and its travel, its rest place,
// whether it went past the compression end in the last second and whether it hangs (at or past its spring's zero).
struct SuspDisplaySlot
{
    int wheels;
    float x[kMaxWheels], travel[kMaxWheels], travelMax[kMaxWheels], rest[kMaxWheels];
    bool hit[kMaxWheels], hanging[kMaxWheels], placed[kMaxWheels];
};

// The air suspension's levelling (build C), kept for the truck (its action) across generations, so a recovery or a
// constraint at a new address keeps the rest places taken.
struct SuspLevel
{
    uint64_t action;               // the truck it was taken on, 0 none
    bool taken;                    // the rest places are taken
    float stand;                   // s standing toward the take, and the steps summed for each wheel
    float count[kMaxWheels];
    float sum[kMaxWheels];
    float ref[kMaxWheels];         // each wheel's rest place from the stock attach point, less the ride height's shift, m
    float level[kMaxWheels];       // the levelling's shift, m toward the droop
    float still;                   // s under kLevelDrive without a break: the levelling acts only after kLevelStand of it
    float keys;                    // the keys' height in force, m, following the one set with the same time constant
};

struct SuspStepState
{
    uint32_t serial, generation;   // of the block the last look took
    bool have;                     // the wheels' last heights are this generation's
    uint32_t step;
    float h[kMaxWheels], v[kMaxWheels];
    float bounceLeft[kMaxWheels];
    bool bounceTaken[kMaxWheels];
    SuspStepWin win;
    SuspStepWin published[2];      // the window ended last is published[(windows - 1) & 1]
    volatile LONG windows;
    SuspStepTraceEntry ring[kStepRing];
    volatile LONG ringHead;        // entries written so far; the newest is ring[(ringHead - 1) % kStepRing]
    volatile LONG faults;          // looks that faulted and dropped the block
    volatile LONG refused;         // writes not made, the wheel's constraint no longer the block's or not of its class
    uint32_t faultSerial;          // the block a fault or a refused write dropped: not taken again
    float clock;                   // s of steps taken
    uint64_t wroteC[kMaxWheels];   // the constraint each attach point write went to, and what it wrote (the guard's)
    float wroteAttach[kMaxWheels];
    volatile LONG attachMoved;     // the game moved an attach point the mod wrote: the features stop for this truck
    uint32_t movedSerial;          // the block the guard stopped: no writes from it
    float restX[kMaxWheels];       // each wheel's place at the last standstill, and when it last went past the end
    float endAt[kMaxWheels];
    SuspDisplaySlot display[2];    // the display's figures, the newest at display[(displays - 1) & 1]
    volatile LONG displays;
    SuspLevel level;               // the air suspension's (read by the mod's thread for the tab and the log)
    volatile LONG levelTaken;      // rest places taken so far
};

// The block published: written into the slot the physics thread is not reading (the one published two passes ago),
// sealed, then swapped in.
inline void SuspStepPublish(SuspStepBlock slots[2], int &next, SuspStepBlock *volatile *slot, const SuspStepBlock &b, uint32_t &serial)
{
    SuspStepBlock &to = slots[next];
    next ^= 1;
    uint32_t mark = ++serial;
    if (!mark) mark = ++serial;
    SuspStepBlock c = b;
    c.serial = mark;
    c.seal = 0;
    to.seal = 0;
    _WriteBarrier();
    memcpy(&to, &c, sizeof to);
    _WriteBarrier();
    to.seal = mark;
    InterlockedExchangePointer((void *volatile *)slot, &to);
}

// A whole copy of the block, or false where it is being written (the serial and the seal differ).
inline bool SuspStepTake(const SuspStepBlock *from, SuspStepBlock &out)
{
    if (!from) return false;
    const uint32_t serial = *(const volatile uint32_t *)&from->serial;
    _ReadBarrier();
    memcpy(&out, from, sizeof out);
    _ReadBarrier();
    const uint32_t seal = *(const volatile uint32_t *)&from->seal;
    return serial && serial == seal && out.serial == serial;
}

// The speed's bin: the first top it is under.
inline int SuspStepBin(float speed)
{
    int k = 0;
    while (k < kStepBins - 1 && fabsf(speed) >= kStepBinTop[k]) k++;
    return k;
}

inline void SuspStepWinStart(SuspStepState &s, uint32_t generation, int wheels)
{
    memset(&s.win, 0, sizeof s.win);
    s.win.generation = generation;
    s.win.wheels = wheels;
    for (int i = 0; i < kMaxWheels; i++)
    {
        s.bounceLeft[i] = 0.0f;
        s.bounceTaken[i] = false;
    }
}

inline void SuspStepWinEnd(SuspStepState &s)
{
    if (s.win.seconds >= kStepWindowLeast && s.win.steps > 0)
    {
        const LONG n = s.windows;
        memcpy(&s.published[n & 1], &s.win, sizeof s.win);
        _WriteBarrier();
        InterlockedExchange(&s.windows, n + 1);
    }
}

// The reads of one step: each wheel's height against its body, the chassis's speed and the traced field. False on a
// fault (an object freed under the look).
inline bool SuspStepRead(const SuspStepBlock &b, float *h, float &speed, float &field, float *attach = nullptr)
{
    __try
    {
        for (int i = 0; attach && i < b.wheels && i < kMaxWheels; i++) attach[i] = b.attachAt[i] ? *(const float *)(uintptr_t)b.attachAt[i] : b.attachUp[i];
        for (int i = 0; i < b.wheels && i < kMaxWheels; i++)
        {
            const float *at = (const float *)(uintptr_t)(b.wheel[i] + 0x13C);
            const float *up = (const float *)(uintptr_t)(b.body[i] + 0x180);
            const float *pos = (const float *)(uintptr_t)(b.body[i] + 0x1A0);
            h[i] = (at[0] - pos[0]) * up[0] + (at[1] - pos[1]) * up[1] + (at[2] - pos[2]) * up[2];
        }
        const float *vel = (const float *)(uintptr_t)(b.chassis + 0x230);
        speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        field = b.traced ? *(const float *)(uintptr_t)b.traced : 0.0f;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The step's figures from the heights read: kept apart from the reads so a test can feed it heights.
inline void SuspStepAdd(SuspStepState &s, const SuspStepBlock &b, const float *h, float speed, float field, float dt)
{
    const int n = b.wheels < kMaxWheels ? b.wheels : kMaxWheels;
    if (!s.have || s.generation != b.generation)
    {
        // a new generation: each wheel's state dropped, a new window
        if (s.have) SuspStepWinEnd(s);
        SuspStepWinStart(s, b.generation, n);
        s.generation = b.generation;
        for (int i = 0; i < n; i++)
        {
            s.h[i] = s.win.least[i] = s.win.most[i] = h[i];
            s.v[i] = 0.0f;
        }
        s.have = true;
        return;
    }
    s.step++;
    const bool timed = dt > 0.0f && dt <= kStepDtMost;
    if (timed)
    {
        SuspStepWin &w = s.win;
        if (!w.steps || dt < w.dtLeast) w.dtLeast = dt;
        if (!w.steps || dt > w.dtMost) w.dtMost = dt;
        w.steps++;
        w.seconds += dt;
        for (int i = 0; i < n; i++)
        {
            const float v = (h[i] - s.h[i]) / dt;
            if (speed > kStepMoving)
            {
                w.moving[i]++;
                if (h[i] == s.h[i]) w.repeats[i]++;
            }
            w.bins[i][v >= 0.0f ? 0 : 1][SuspStepBin(v)]++;
            w.least[i] = fminf(w.least[i], h[i]);
            w.most[i] = fmaxf(w.most[i], h[i]);
            if (!s.bounceTaken[i] && fabsf(v) > kStepBounceFrom)
            {
                s.bounceTaken[i] = true;
                s.bounceLeft[i] = kStepBounce;
                w.bounceAt[i] = w.seconds;
            }
            if (s.bounceLeft[i] > 0.0f)
            {
                if (w.bounceSteps[i] < kStepBounceMost) w.bounce[i][w.bounceSteps[i]++] = h[i];
                s.bounceLeft[i] -= dt;
            }
            s.v[i] = v;
        }
    }
    if (b.traced && n > 0)
    {
        const LONG at = s.ringHead;
        s.ring[at % kStepRing] = SuspStepTraceEntry{ s.step, dt, field, h[0], timed ? s.v[0] : 0.0f };
        _WriteBarrier();
        InterlockedExchange(&s.ringHead, at + 1);
    }
    for (int i = 0; i < n; i++) s.h[i] = h[i];
    if (s.win.seconds >= kStepWindow)
    {
        SuspStepWinEnd(s);
        SuspStepWinStart(s, b.generation, n);
        for (int i = 0; i < n; i++) s.win.least[i] = s.win.most[i] = h[i];
    }
}

// One damping write (review 16, finding 1): right before it, [wheel + 0x28] must still be the block's constraint and
// its first word the known class, all under __try. 1 written, 0 refused (a mismatch), -1 a fault.
inline int SuspDamperWrite(uint64_t wheel, uint64_t constraint, uint64_t vtable, float factor, float strength = -1.0f)
{
    __try
    {
        if (*(const uint64_t *)(uintptr_t)(wheel + 0x28) != constraint || *(const uint64_t *)(uintptr_t)constraint != vtable) return 0;
        if (factor >= 0.0f) *(volatile float *)(uintptr_t)(constraint + 0xC8) = factor;
        if (strength >= 0.0f) *(volatile float *)(uintptr_t)(constraint + 0xC4) = strength;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// A wheel's place x along its axis from the step's height, and whether the block knows it.
inline bool SuspPlace(const SuspStepBlock &b, int i, float h, float attach, float &x)
{
    if (b.axisUp[i] == 0.0f) return false;
    x = (h - attach) / b.axisUp[i];
    return true;
}

// The place's figures each step (the bump zone, the compression end, the rest place), with or without a feature on.
inline void SuspPlaceAdd(SuspStepState &s, const SuspStepBlock &b, const float *h, const float *attach, float speed)
{
    for (int i = 0; i < b.wheels && i < kMaxWheels; i++)
    {
        float x = 0.0f;
        if (!SuspPlace(b, i, h[i], attach[i], x) || !(b.travel[i] > 0.0f)) continue;
        if (-x >= b.travel[i]) s.win.pastEnd[i]++;
        else if (-x >= b.travel[i] - BumpZoneOf(b.spring, b.travel[i])) s.win.inZone[i]++;
        if (!s.win.rested[i] && speed < 0.1f)
        {
            s.win.rested[i] = true;
            s.win.rest[i] = x;
        }
    }
}

// The damper's and the spring's step, after the figures: each wheel the block names, at the speed just worked out and
// the place just read. False where a write was refused or faulted: the block is then dropped.
inline bool SuspDamperStep(SuspStepState &s, const SuspStepBlock &b, const float *h, const float *attach, float dt, bool timed)
{
    if ((!b.damping && !b.springing) || !timed || !s.have || s.generation != b.generation) return true;
    for (int i = 0; i < b.wheels && i < kMaxWheels; i++)
    {
        if (!b.constraint[i]) continue;
        float x = 0.0f;
        float next = 0.0f;
        const bool placed = SuspPlace(b, i, h[i], attach[i], x) && b.travel[i] > 0.0f && SuspPlace(b, i, h[i] + s.v[i] * dt, attach[i], next);
        const float u = placed ? SuspDepth(x, b.travel[i]) : 0.0f, ahead = placed ? SuspDepth(next, b.travel[i]) : 0.0f;
        bool clamped = false;
        float f = -1.0f, k = -1.0f;
        if (b.damping && b.base[i] > 0.0f)
        {
            // stock x DampingScale x G(v) x the zone and the bump stop, held once
            const float zone = placed && ZonesOn(b.spring) ? ZoneFactor(b.spring, u, s.v[i], b.travel[i], ahead) : 1.0f;
            f = DamperFactor(b.damper, b.base[i] * zone, s.v[i], clamped);
        }
        if (b.springing && b.baseK[i] > 0.0f && placed)
        {
            const float want = b.baseK[i] * SpringF(b.spring, u, b.travel[i], ahead);
            k = want < kSpringLeast ? kSpringLeast : want > 1.0f ? 1.0f : want;
        }
        if (f < 0.0f && k < 0.0f) continue;
        const int done = SuspDamperWrite(b.wheel[i], b.constraint[i], b.vtable, f, k);
        if (done <= 0)
        {
            InterlockedIncrement(done < 0 ? &s.faults : &s.refused);
            return false;
        }
        if (f >= 0.0f)
        {
            s.win.written[i]++;
            s.win.clamped[i] += clamped;
            s.win.writtenSum[i] += f;
        }
        if (k >= 0.0f)
        {
            s.win.springWritten[i]++;
            s.win.springSum[i] += k;
        }
    }
    return true;
}

// ---- the attach point's features (BRIEF_suspension_extras.md, the addendum of 2 November 13:47: build B) ----
// The spring's zero and both travel limits are measured from the attach point (+ 0xA0 to + 0xA8, in the frame of the
// body the constraint is on), so a shift d of it along the axis toward the droop moves the wheel's rest place and its
// travel by d and the corner rises by about d: what the file's Height does, and the Arocs' Active ride height within
// DeviationMax. Only its component along up is written (the axis is the body's down on every truck seen).
static const float kShiftMost = 0.12f;            // m, the game's own DeviationMax range
static const float kPairAlong = 0.2f;             // m along forward: two wheels of one axle
inline float SuspShiftHold(float d) { return d > kShiftMost ? kShiftMost : d < -kShiftMost ? -kShiftMost : d; }

// The axles: each wheel's partner at the same forward place within kPairAlong on the other side (left by the chassis's
// left axis), whether it is the left one, whether its axle is the frontmost and whether it is the rearmost.
inline void SuspPairs(int n, const float *forward, const float *left, int *partner, bool *isLeft, bool *front, bool *rearmost)
{
    float most = -1e9f, least = 1e9f;
    for (int i = 0; i < n; i++) most = fmaxf(most, forward[i]), least = fminf(least, forward[i]);
    for (int i = 0; i < n; i++)
    {
        partner[i] = -1;
        isLeft[i] = left[i] > 0.0f;
        front[i] = forward[i] > most - kPairAlong;
        rearmost[i] = forward[i] < least + kPairAlong;
        float best = kPairAlong;
        for (int j = 0; j < n; j++)
            if (j != i && (left[j] > 0.0f) != (left[i] > 0.0f) && fabsf(forward[j] - forward[i]) < best) best = fabsf(forward[j] - forward[i]), partner[i] = j;
    }
}

// A bar's shifts on a pair, c the compression (-x) and k the strength of each side: F = K (cL - cR) with K the setting
// times the pair's mean k; the left side shifts F / (2 kL) toward the droop and the right the same toward the bump,
// so the more compressed side is pushed down and the body is held level.
inline void SuspSwayShift(float setting, float cL, float cR, float kL, float kR, float &sL, float &sR)
{
    const float K = setting * 0.5f * (kL + kR), F = K * (cL - cR);
    sL = kL > 0.0f ? F / (2.0f * kL) : 0.0f;
    sR = kR > 0.0f ? -F / (2.0f * kR) : 0.0f;
}

// ---- the air suspension (the addendum of 2 November 13:47: build C) ----
// At the first standstill after the truck is taken (kLevelStand s under kLevelStill m/s), each wheel's place along the
// axis from its stock attach point, less the ride height's shift then, is its rest place. From then on each wheel's
// levelling shift follows its own error with a time constant of kLevelTau, so cargo no longer makes the truck sag or
// lean to a side (the author, 2 November: "it should level it side to side too"). It acts after kLevelStand s under
// kLevelDrive (10 km/h, the author's), and an axle with a wheel hanging or at its compression end keeps its shift, so a
// crest, a ditch or a log never winds it up (review of build C, R1). The ride height's shift (the sliders and the keys)
// goes on top of the rest place, so the levelling never takes it back. The keys' height follows the one set with the
// same time constant.
static const float kLevelTau = 3.0f, kLevelStand = 1.0f, kLevelStill = 0.1f, kLevelDrive = 10.0f / 3.6f;

// One step of the levelling. g: each wheel's place from its stock attach point (x plus all its shift), r: its ride
// height's shift without the keys, placed: it has a place, held: it hangs or is at its compression end, keys: the
// keys' height set (m). Each wheel levels on its own place; an axle is a wheel and its partner, held while either is.
// The shift kept is held so that it and the ride height stay within kShiftMost. True on the step the rest places are
// taken.
inline bool SuspLevelStep(SuspLevel &l, int n, const int *partner, const float *g, const float *r, const bool *placed, const bool *held, float keys,
                          float speed, float dt)
{
    if (!(dt > 0.0f)) return false;
    l.keys += (keys - l.keys) * dt / kLevelTau;
    const bool standing = fabsf(speed) < kLevelStill;
    l.still = fabsf(speed) < kLevelDrive ? l.still + dt : 0.0f;
    bool axleHeld[kMaxWheels];
    for (int i = 0; i < n; i++)
    {
        const int j = partner[i];
        axleHeld[i] = held[i] || (j >= 0 && j < n && placed[j] && held[j]);
    }
    if (!l.taken)
    {
        // a wheel hanging or at its end (a ditch, a log) restarts the take as moving does, so no axle takes such a place
        // as its rest (review of build C, R4)
        bool anyHeld = false;
        for (int i = 0; i < n; i++) anyHeld |= placed[i] && axleHeld[i];
        if (!standing || anyHeld)
        {
            l.stand = 0.0f;
            memset(l.count, 0, sizeof l.count);
            memset(l.sum, 0, sizeof l.sum);
            return false;
        }
        l.stand += dt;
        for (int i = 0; i < n; i++)
            if (placed[i]) l.sum[i] += g[i] - r[i] - l.keys, l.count[i] += 1.0f;
        if (l.stand < kLevelStand) return false;
        for (int i = 0; i < n; i++) l.ref[i] = l.count[i] > 0.0f ? l.sum[i] / l.count[i] : 0.0f;
        l.taken = true;
        return true;
    }
    if (l.still < kLevelStand) return false;
    for (int i = 0; i < n; i++)
    {
        if (!placed[i] || axleHeld[i] || !(l.count[i] > 0.0f)) continue;
        const float err = l.ref[i] + r[i] + l.keys - g[i];
        const float lo = -kShiftMost - r[i] - l.keys, hi = kShiftMost - r[i] - l.keys;
        l.level[i] = fminf(hi, fmaxf(lo, l.level[i] + err * dt / kLevelTau));
    }
    return false;
}

// The air suspension turned off (review of build C, R2): the levelling's shift and the keys' height go to 0 with the
// same time constant instead of in one step. True once both are within half a millimetre of 0: then it is reset.
inline bool SuspLevelFade(SuspLevel &l, int n, float dt)
{
    if (!(dt > 0.0f)) return false;
    const float k = dt / kLevelTau;
    bool done = fabsf(l.keys) < 0.0005f;
    l.keys -= l.keys * k;
    for (int i = 0; i < n; i++)
    {
        done &= fabsf(l.level[i]) < 0.0005f;
        l.level[i] -= l.level[i] * k;
    }
    return done;
}

// One attach point write: right before it, [wheel + 0x28] must still be the constraint and of its class. 1 written,
// 0 refused, -1 a fault.
inline int SuspAttachWrite(uint64_t wheel, uint64_t constraint, uint64_t vtable, uint64_t at, float value)
{
    __try
    {
        if (*(const uint64_t *)(uintptr_t)(wheel + 0x28) != constraint || *(const uint64_t *)(uintptr_t)constraint != vtable) return 0;
        *(volatile float *)(uintptr_t)at = value;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The attach point's step: the guard first (a field that holds neither what was written nor the stock was moved by the
// game: the features stop for the truck, the mod's thread puts the stock back), then stock + the ride height, the test's
// shift, the bar's and the levelling's, held to kShiftMost, along the axis. The bar works off the place just read. False
// where a write was refused or faulted.
inline bool SuspAttachStep(SuspStepState &s, const SuspStepBlock &b, const float *h, const float *attach, float speed = 0.0f, float dt = 0.0f)
{
    // the levelling starts over at once when another truck is taken or the block stops shifting (the guard); turned off,
    // it fades out first (below)
    if ((!b.shifting || s.level.action != b.action) && s.level.action) s.level = SuspLevel{};
    if (!b.shifting || s.movedSerial == b.serial) return true;
    const int n = b.wheels < kMaxWheels ? b.wheels : kMaxWheels;
    for (int i = 0; i < n; i++)
        if (b.attachC[i] && s.wroteC[i] == b.attachC[i] && attach[i] != s.wroteAttach[i] && attach[i] != b.attachStock[i])
        {
            InterlockedIncrement(&s.attachMoved);
            s.movedSerial = b.serial;
            return true;
        }
    float shift[kMaxWheels];
    for (int i = 0; i < n; i++) shift[i] = b.ride[i] + (i == 0 ? b.testShift : 0.0f);
    for (int i = 0; i < n; i++)
    {
        const int j = b.partner[i];
        if (j <= i || j >= n || !b.isLeft[i] == !b.isLeft[j] || !(b.sway[i] > 0.0f) || b.axisUp[i] == 0.0f || b.axisUp[j] == 0.0f) continue;
        const int l = b.isLeft[i] ? i : j, r = b.isLeft[i] ? j : i;
        const float cL = -(h[l] - attach[l]) / b.axisUp[l], cR = -(h[r] - attach[r]) / b.axisUp[r];
        float sL = 0.0f, sR = 0.0f;
        SuspSwayShift(b.sway[i], cL, cR, b.springK[l], b.springK[r], sL, sR);
        shift[l] += sL, shift[r] += sR;
    }
    if (b.air)
    {
        // each wheel's place from its stock attach point, all of the last step's shift in it; held while it hangs or is
        // at its compression end, by its place from the live attach point
        float g[kMaxWheels], r[kMaxWheels];
        bool placed[kMaxWheels], held[kMaxWheels];
        for (int i = 0; i < n; i++)
        {
            float x = 0.0f;
            placed[i] = b.attachC[i] && SuspPlace(b, i, h[i], attach[i], x);
            g[i] = placed[i] ? (h[i] - b.attachStock[i]) / b.axisUp[i] : 0.0f;
            r[i] = b.ride[i];
            held[i] = placed[i] && (x >= 0.0f || (b.travel[i] > 0.0f && -x >= b.travel[i]));
        }
        if (!s.level.action) s.level.action = b.action;
        if (SuspLevelStep(s.level, n, b.partner, g, r, placed, held, b.airKeys, speed, dt)) InterlockedIncrement(&s.levelTaken);
    }
    else if (s.level.action && SuspLevelFade(s.level, n, dt)) s.level = SuspLevel{};
    if (s.level.action)
        for (int i = 0; i < n; i++) shift[i] += s.level.keys + s.level.level[i];
    for (int i = 0; i < n; i++)
    {
        if (!b.attachC[i] || !b.attachAt[i] || b.axisUp[i] == 0.0f) continue;
        const float value = b.attachStock[i] + SuspShiftHold(shift[i]) * b.axisUp[i];
        const int done = SuspAttachWrite(b.wheel[i], b.attachC[i], b.vtable, b.attachAt[i], value);
        if (done <= 0)
        {
            InterlockedIncrement(done < 0 ? &s.faults : &s.refused);
            return false;
        }
        s.wroteC[i] = b.attachC[i];
        s.wroteAttach[i] = value;
    }
    return true;
}

// The display's figures of this step, published whole into the slot not last published.
inline void SuspDisplayStep(SuspStepState &s, const SuspStepBlock &b, const float *h, const float *attach, float speed)
{
    if (!b.display) return;
    const int n = b.wheels < kMaxWheels ? b.wheels : kMaxWheels;
    const LONG count = s.displays;
    SuspDisplaySlot &d = s.display[count & 1];
    d.wheels = n;
    for (int i = 0; i < n; i++)
    {
        float x = 0.0f;
        d.placed[i] = SuspPlace(b, i, h[i], attach[i], x) && b.travel[i] > 0.0f;
        if (!d.placed[i]) continue;
        if (speed < 0.1f) s.restX[i] = x;
        if (-x >= b.travel[i]) s.endAt[i] = s.clock;
        d.x[i] = x;
        d.travel[i] = b.travel[i];
        d.travelMax[i] = b.travelMax[i];
        d.rest[i] = s.restX[i];
        d.hit[i] = s.endAt[i] > 0.0f && s.clock - s.endAt[i] < 1.0f;
        d.hanging[i] = x >= 0.0f;
    }
    _WriteBarrier();
    InterlockedExchange(&s.displays, count + 1);
}

// One call of the wheels' drive (the physics thread): for the published block's truck only.
inline void SuspStepLook(SuspStepState &s, const SuspStepBlock *published, uint64_t action, float dt)
{
    SuspStepBlock b;
    if (!published || published->action != action || !SuspStepTake(published, b) || b.serial == s.faultSerial) return;
    float h[kMaxWheels], attach[kMaxWheels], speed = 0.0f, field = 0.0f;
    if (!SuspStepRead(b, h, speed, field, attach))
    {
        s.faultSerial = b.serial;
        s.have = false;
        InterlockedIncrement(&s.faults);
        return;
    }
    s.serial = b.serial;
    const bool had = s.have && s.generation == b.generation;
    SuspStepAdd(s, b, h, speed, field, dt);
    if (had) SuspPlaceAdd(s, b, h, attach, speed);
    if (dt > 0.0f && dt <= kStepDtMost) s.clock += dt;
    if (had) SuspDisplayStep(s, b, h, attach, speed);
    if (!SuspDamperStep(s, b, h, attach, dt, had && dt > 0.0f && dt <= kStepDtMost) || (had && !SuspAttachStep(s, b, h, attach, speed, dt > 0.0f && dt <= kStepDtMost ? dt : 0.0f)))
    {
        s.faultSerial = b.serial;
        s.have = false;
    }
}

// The damper let go (the mod's thread, once the physics thread has stepped past the block that ran it): each wheel's
// damping and strength as the tune gives them (stock times DampingScale and SpringScale), where the physics thread
// wrote them and the constraint is still the block's and of its class. Returns the wheels written.
inline int SuspDamperLetGo(const SuspStepBlock &b, const TruckMemory &m)
{
    int back = 0;
    for (int i = 0; i < b.wheels && i < kMaxWheels; i++)
    {
        uint64_t c = 0, vt = 0;
        bool put = false;
        // the attach point's stock, where the physics thread wrote it
        if (b.shifting && b.attachC[i] && b.attachAt[i] && m.get(b.wheel[i] + 0x28, c) && c == b.attachC[i] && m.get(c, vt) && vt == b.vtable)
            put |= m.put(b.attachAt[i], b.attachStock[i]);
        if (!b.constraint[i] || !m.get(b.wheel[i] + 0x28, c) || c != b.constraint[i] || !m.get(c, vt) || vt != b.vtable)
        {
            back += put;
            continue;
        }
        if (b.damping && b.base[i] > 0.0f) put |= m.put(c + 0xC8, b.base[i]);
        if (b.springing && b.baseK[i] > 0.0f) put |= m.put(c + 0xC4, b.baseK[i]);
        back += put;
    }
    return back;
}

// ---- on the mod's thread ----

// The window published since seen: true with a copy where there is a new one (windows moves past seen).
inline bool SuspStepWindowTake(const SuspStepState &s, LONG &seen, SuspStepWin &out)
{
    const LONG n = s.windows;
    if (n == seen) return false;
    _ReadBarrier();
    memcpy(&out, &s.published[(n - 1) & 1], sizeof out);
    _ReadBarrier();
    const bool whole = s.windows == n;
    seen = n;
    return whole;
}

// The ring's entries written since tail, oldest first, at most most of them (the newest where more were written).
inline int SuspStepDrain(const SuspStepState &s, LONG &tail, SuspStepTraceEntry *out, int most)
{
    const LONG head = s.ringHead;
    _ReadBarrier();
    LONG from = tail;
    if (head - from > kStepRing) from = head - kStepRing;
    if (head - from > most) from = head - most;
    int n = 0;
    for (LONG i = from; i < head; i++) out[n++] = s.ring[i % kStepRing];
    tail = head;
    return n;
}

// A body fits a wheel when the constraint's attach point, in that body's frame (frame: + 0x170 forward, + 0x180 up,
// + 0x190 left, + 0x1A0 position, four floats each), lies within 0.5 m of the wheel's centre along the body's forward
// axis and 1 m across. On the F350 the attach point is 0.02 m from its wheel along and 0.27 m inboard; on the Arocs'
// front wheels it is 3.2 m off against the chassis (review 14), so they ride on another body.
inline bool SuspBodyFits(const float frame[16], const float attach[3], const float wheel[3])
{
    const float *f = frame, *u = frame + 4, *l = frame + 8, *p = frame + 12;
    float d[3];
    for (int k = 0; k < 3; k++) d[k] = wheel[k] - (p[k] + attach[0] * f[k] + attach[1] * u[k] + attach[2] * l[k]);
    const float along = d[0] * f[0] + d[1] * f[1] + d[2] * f[2], across = d[0] * l[0] + d[1] * l[1] + d[2] * l[2];
    return fabsf(along) < 0.5f && fabsf(across) < 1.0f;
}

// ---- SuspStepTest: does a write act on the next step (review 14, phase 1 item 3) ----

// The first entry from from on whose field is value, or -1.
inline int SuspStepFirst(const SuspStepTraceEntry *e, int n, int from, float value)
{
    for (int i = from < 0 ? 0 : from; i < n; i++)
        if (e[i].field == value) return i;
    return -1;
}

// The steps from at to the first whose height leaves its trend by more than gap (m): the trend is the line fitted to
// the heights of the steps in the before seconds up to at. -1 where none does, or there is no trend to go by.
inline int SuspStepLeave(const SuspStepTraceEntry *e, int n, int at, float before, float gap)
{
    if (at <= 0 || at >= n) return -1;
    double t = 0.0, st = 0.0, sh = 0.0, stt = 0.0, sth = 0.0;
    int m = 0;
    for (int i = at - 1; i >= 0 && t < before; i--)
    {
        t += e[i + 1].dt;
        const double x = -t;
        st += x, sh += e[i].h, stt += x * x, sth += x * e[i].h;
        m++;
    }
    if (m < 3) return -1;
    const double den = m * stt - st * st;
    const double slope = den != 0.0 ? (m * sth - st * sh) / den : 0.0, base = (sh - slope * st) / m;
    double x = 0.0;
    for (int i = at; i < n; i++)
    {
        if (i > at) x += e[i].dt;
        if (fabs(e[i].h - (base + slope * x)) > gap) return i - at;
    }
    return -1;
}

// The speed's swing over the entries from from up to the end of span seconds: its root mean square and its largest
// size.
inline void SuspStepSwing(const SuspStepTraceEntry *e, int n, int from, float span, float &rms, float &most, int &steps)
{
    double sum = 0.0, t = 0.0;
    rms = most = 0.0f;
    steps = 0;
    for (int i = from < 0 ? 0 : from; i < n && t < span; i++)
    {
        t += e[i].dt;
        sum += (double)e[i].v * e[i].v;
        most = fmaxf(most, fabsf(e[i].v));
        steps++;
    }
    if (steps) rms = (float)sqrt(sum / steps);
}

// The entry where the span seconds before at begin.
inline int SuspStepBack(const SuspStepTraceEntry *e, int at, float span)
{
    double t = 0.0;
    int i = at;
    while (i > 0 && t < span) t += e[i--].dt;
    return i;
}

// A bounce's decay (the addendum of 01:18, item 4): its heights h (m) about where it comes to rest (the mean of its
// last kBounceRest steps), the swings past it larger than kBounceSwing, each the other way from the one before, and
// the second's size over the first's. False where it has fewer than two swings.
static const int kBounceRest = 20;
static const float kBounceSwing = 0.002f;
inline bool SuspBounceRatio(const float *h, int n, float &ratio)
{
    ratio = 0.0f;
    if (n < kBounceRest + 3) return false;
    double rest = 0.0;
    for (int i = n - kBounceRest; i < n; i++) rest += h[i];
    rest /= kBounceRest;
    float swing[2] = {};
    int swings = 0;
    for (int i = 1; i + 1 < n && swings < 2; i++)
    {
        const float e = (float)(h[i] - rest), before = (float)(h[i - 1] - rest), after = (float)(h[i + 1] - rest);
        const bool peak = (e > before && e >= after) || (e < before && e <= after);
        if (!peak || fabsf(e) <= kBounceSwing) continue;
        if (swings && (e > 0.0f) == (swing[swings - 1] > 0.0f))
        {
            // the same way as the last: the larger of the two is the swing
            if (fabsf(e) > fabsf(swing[swings - 1])) swing[swings - 1] = e;
            continue;
        }
        swing[swings++] = e;
    }
    if (swings < 2) return false;
    ratio = fabsf(swing[1]) / fabsf(swing[0]);
    return true;
}
