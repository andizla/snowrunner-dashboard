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
    float bounceAt[kMaxWheels];                   // s into the window where the bounce began
    int bounceSteps[kMaxWheels];
    float bounce[kMaxWheels][kStepBounceMost];    // the height each step from there, in m
};

struct SuspStepTraceEntry
{
    uint32_t step;
    float dt, field, h, v;
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
    uint32_t faultSerial;          // the block a fault dropped: not taken again
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
inline bool SuspStepRead(const SuspStepBlock &b, float *h, float &speed, float &field)
{
    __try
    {
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

// One call of the wheels' drive (the physics thread): for the published block's truck only.
inline void SuspStepLook(SuspStepState &s, const SuspStepBlock *published, uint64_t action, float dt)
{
    SuspStepBlock b;
    if (!published || published->action != action || !SuspStepTake(published, b) || b.serial == s.faultSerial) return;
    float h[kMaxWheels], speed = 0.0f, field = 0.0f;
    if (!SuspStepRead(b, h, speed, field))
    {
        s.faultSerial = b.serial;
        s.have = false;
        InterlockedIncrement(&s.faults);
        return;
    }
    s.serial = b.serial;
    SuspStepAdd(s, b, h, speed, field, dt);
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
