// suspension_scan.h: a probe for the suspension tune (review 14), read only. The game's wheels likely ride on Havok's
// wheel constraint (hkpWheelConstraintData), whose spring and damping are what a tune would set, but where each
// wheel's constraint is kept is not known. With Probe=1 the mod looks for it among the objects the driven truck's
// wheel objects and wheel models point to, two levels deep: an object whose first word is the constraint's vtable
// (found by its RTTI name, as game_globals.h's VtableByName finds the wheel's class). The log then shows the path to
// each one and its floats, and every kSuspEvery ms the floats of the first two again, so a drive over bumps shows
// which of them are the spring, the damping and the travel.
#pragma once
#include <stdint.h>
#include <string.h>
#include "truck.h"
#include "clock_scan.h"
#include "suspension_step.h"

static const char *const kSuspConstraintName = ".?AVhkpWheelConstraintData@@";
static const uint32_t kSuspFirst = 0x800, kSuspSecond = 0x400; // bytes read of a wheel object or model, and of an object it points to
static const uint32_t kSuspFloats = 0x100;                    // bytes of a constraint logged
static const int kSuspHitsMost = 64;
static const uint64_t kSuspEvery = 2000;                       // ms from one look at the first two to the next

// A constraint found: the wheel's index, from its wheel object or its model, and the offsets that led there
// ([root + off[0]] at depth 1, [[root + off[0]] + off[1]] at depth 2).
struct SuspHit
{
    int wheel;
    bool fromModel;
    int depth;
    uint32_t off[2];
    uint64_t at;
};

// Reads size bytes at at, or the first 0x100 where the whole is not there (an object near the end of its memory).
inline uint32_t SuspReadObject(const TruckMemory &m, uint64_t at, uint32_t size, uint64_t *words)
{
    if (m.peek(at, words, size)) return size;
    return size > 0x100 && m.peek(at, words, 0x100) ? 0x100 : 0;
}

// The constraints behind one root (a wheel object or a wheel model), each once. vtable: the constraint's vtable's
// address in the game. Returns how many it added to out (count of them already there), at most most in all.
inline int SuspScanRoot(const TruckMemory &m, uint64_t imageSize, uint64_t root, int wheel, bool fromModel, uint64_t vtable, SuspHit *out, int count, int most)
{
    static uint64_t first[kSuspFirst / 8], second[kSuspSecond / 8];
    const int before = count;
    auto add = [&](uint64_t at, int depth, uint32_t a, uint32_t b) {
        for (int i = before; i < count; i++)
            if (out[i].at == at) return;
        if (count < most) out[count++] = SuspHit{ wheel, fromModel, depth, { a, b }, at };
    };
    const uint32_t size = root ? SuspReadObject(m, root, kSuspFirst, first) : 0;
    ClockSeen seen(size / 8 + 1);
    for (uint32_t a = 0; a < size && count < most; a += 8)
    {
        const uint64_t q = first[a / 8];
        uint64_t vt = 0;
        if (!ClockPointer(q, m.base, imageSize) || !seen.add(q) || !m.get(q, vt)) continue;
        if (vt == vtable) { add(q, 1, a, 0); continue; }
        const uint32_t inner = SuspReadObject(m, q, kSuspSecond, second);
        for (uint32_t b = 0; b < inner && count < most; b += 8)
        {
            const uint64_t r = second[b / 8];
            if (ClockPointer(r, m.base, imageSize) && m.get(r, vt) && vt == vtable) add(r, 2, a, b);
        }
    }
    return count - before;
}

// Every wheel's constraints: from its wheel object first, then from its model. Returns how many were found.
inline int SuspScan(const TruckMemory &m, uint64_t imageSize, const uint64_t *wheels, const uint64_t *models, int n, uint64_t vtable, SuspHit *out, int most)
{
    int count = 0;
    for (int i = 0; i < n && count < most; i++)
    {
        count += SuspScanRoot(m, imageSize, wheels[i], i, false, vtable, out, count, most);
        count += SuspScanRoot(m, imageSize, models ? models[i] : 0, i, true, vtable, out, count, most);
    }
    return count;
}

// The path in words for the log: wheel 2 [+0x18]+0x40.
inline void SuspHitWords(const SuspHit &h, char *out, size_t size)
{
    if (h.depth == 1) snprintf(out, size, "wheel %d's %s [+0x%x]", h.wheel, h.fromModel ? "model" : "object", h.off[0]);
    else snprintf(out, size, "wheel %d's %s [[+0x%x]+0x%x]", h.wheel, h.fromModel ? "model" : "object", h.off[0], h.off[1]);
}

// The floats of a constraint's first kSuspFloats bytes, as "+0x10 1.5 ..." in groups of four; false where it is gone.
inline bool SuspFloatsText(const TruckMemory &m, uint64_t at, uint64_t vtable, char *out, size_t size)
{
    float f[kSuspFloats / 4];
    uint64_t vt = 0;
    if (!m.get(at, vt) || vt != vtable || !m.peek(at, f, sizeof f)) return false;
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < (int)(kSuspFloats / 4) && used + 32 < size; i++)
    {
        if (i % 4) used += (size_t)snprintf(out + used, size - used, " %.5g", f[i]);
        else used += (size_t)snprintf(out + used, size - used, "%s+%x %.5g", i ? " | " : "", i * 4, f[i]);
    }
    return true;
}

// ---- the tune (BRIEF_suspension_simple.md, review 14's probe drive) ----
// Each wheel's own constraint is [wheel object + 0x28]. In it are the truck file's SuspensionMin (+0xB4) and
// SuspensionMax (+0xB8), Havok's travel limits in metres, and Strength (+0xC4) and Damping (+0xC8), Havok's soft
// atom's factors from 0 to 1. The game does not rewrite them while driving. The tune writes each as the stock value
// times its slider; the spring and the damping are held to 0 to 1, and a limit keeps its side of 0. The stock is read
// once per truck and wheel, again when [wheel + 0x28] changes, and again for a value that is neither the stock nor
// what the tune wrote (the game rebuilt the constraint in place). A slider at 1 writes nothing, but it puts the stock
// back where the tune wrote.
enum { kSuspStrength, kSuspDamping, kSuspMin, kSuspMax, kSuspValues };
static const uint32_t kSuspConstraintAt = 0x28;
static const uint32_t kSuspValueAt[kSuspValues] = { 0xC4, 0xC8, 0xB4, 0xB8 };

struct SuspWheel
{
    uint64_t constraint;        // where the stock was read, 0 = not yet
    float stock[kSuspValues];   // as the truck has them
    float wrote[kSuspValues];   // what the tune last wrote, where ours
    bool ours[kSuspValues];     // the value there is the tune's
    bool fresh;                 // a stock was read on this pass (for the log)
    bool wroteNow;              // a value was written on this pass, and what was read back right after
    float back[kSuspValues];
    bool settle;                // written while a slider moved: the values are logged once the sliders stand still
    int writes;                 // passes that wrote it while the sliders stood still, from the stock read on
};

struct SuspTune
{
    uint64_t vehicle;
    float scale[2][kSuspValues]; // the sliders on the last pass: the front's and the rear's
    bool moved;                 // they moved since the last pass
    bool held;                  // a value was left as it is on this pass: front and rear are set apart and the wheel's end is not known
    SuspWheel wheel[kMaxWheels];
};

// ---- front and rear (BRIEF_suspension_front_rear.md and its addendum, M1 and M2) ----
// Each wheel's end, the one source for the tune's write, the step's bases and the tab's figures. Made once a vehicle,
// at the first pass where every wheel's place is read, and kept until the vehicle or its wheel count changes: never
// from the pose of a later pass, so a truck that bends, steers or rolls over keeps its ends. Front is the first axle,
// every other axle is rear (a second steered axle too). Standing straight that is SuspPairs' rule, the one Ride front
// goes by: within kPairAlong of the frontmost wheel along the chassis's forward. So that a truck first seen bent or at
// full steer has the same ends, the rule here is by what is ahead: a wheel is rear where another wheel is more than
// kPairAlong ahead of it and less aside than ahead (within 45 degrees of straight ahead), front where none is. A wheel
// on another body is placed by its centre in the chassis's frame like any other. A single wheel whose place stays
// unread for kEndsWait passes on a truck of three wheels or more is rear (unread says which); with more unread no end
// is known and nothing is guessed.
struct SuspEnds
{
    uint64_t vehicle;
    int wheels;
    bool known;                 // every wheel has its end
    bool front[kMaxWheels];
    uint32_t unread;            // the wheel taken as rear because its place was not read
    int waited;                 // passes with all places read but one
    bool fresh;                 // made on this pass (for the log)
    bool told;                  // "the ends are not known yet" said for this truck (gauges.cpp, SuspTunePass)
};
static const int kEndsWait = 20;

// read: a bit for each wheel whose place is known; a wheel without one is rear.
inline void SuspEndsRule(int n, const float *forward, const float *left, uint32_t read, bool *front)
{
    for (int i = 0; i < n; i++)
    {
        front[i] = false;
        if (!(read >> i & 1u)) continue;
        bool ahead = false;
        for (int j = 0; j < n && !ahead; j++)
        {
            const float by = forward[j] - forward[i];
            ahead = j != i && (read >> j & 1u) && by > kPairAlong && fabsf(left[j] - left[i]) < by;
        }
        front[i] = !ahead;
    }
}

// One pass: true where the ends are known. forward, left: each wheel's place in the chassis's frame.
inline bool SuspEndsStep(SuspEnds &e, uint64_t vehicle, int wheels, const float *forward, const float *left, uint32_t read)
{
    if (e.vehicle != vehicle || e.wheels != wheels) e = SuspEnds{ vehicle, wheels };
    e.fresh = false;
    const int n = wheels < kMaxWheels ? wheels : kMaxWheels;
    if (e.known || !vehicle || n <= 0) return e.known;
    const uint32_t all = n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u, unread = all & ~read;
    int missing = 0;
    for (int i = 0; i < n; i++) missing += unread >> i & 1u;
    if (missing > 1 || (missing == 1 && n < 3))
    {
        e.waited = 0;
        return false;
    }
    if (missing == 1 && ++e.waited < kEndsWait) return false;
    SuspEndsRule(n, forward, left, read & all, e.front);
    e.unread = unread;
    e.known = e.fresh = true;
    return true;
}

// Wheel i's factor, the front's or the rear's by its end. With both alike that one, whatever is known; false where
// they differ and the wheel's end is not known (never a guess, never one end's value on all wheels).
inline bool SuspEndFactor(const SuspEnds *e, uint64_t vehicle, int i, float front, float rear, float &out)
{
    if (front == rear)
    {
        out = front;
        return true;
    }
    if (!e || !e->known || e->vehicle != vehicle || i < 0 || i >= kMaxWheels) return false;
    out = e->front[i] ? front : rear;
    return true;
}

// What a wheel's spring or damping is to be: its stock times its end's factor, held to the game's 1. The step's block
// takes its bases from here and the tune writes the same figure; 0 where SuspEndFactor gives none.
inline float SuspEndBase(const SuspEnds *e, uint64_t vehicle, int i, float stock, float front, float rear)
{
    float factor = 1.0f;
    return SuspEndFactor(e, vehicle, i, front, rear, factor) ? fminf(1.0f, stock * factor) : 0.0f;
}

// A value the tune may take as a stock: the spring and the damping a factor of Havok's, a limit on its side of 0.
inline bool SuspValueSane(int k, float v)
{
    if (k == kSuspMin) return v <= 0.0f && v > -5.0f; // a file's 0.0 is common (a modder's note, 2 November)
    if (k == kSuspMax) return v > 0.0f && v < 5.0f;
    return v > 0.0f && v <= 1.0f;
}

// The stock put back into every value the tune wrote, where the constraint is still that class and the value still
// the tune's (review 15: a truck left tuned would else give its scaled values as the stock when driven again, and the
// slider would scale them anew). Returns the values put back.
inline int SuspTuneRestore(SuspTune &t, const TruckMemory &m, uint64_t vtable)
{
    int restored = 0;
    for (SuspWheel &w : t.wheel)
    {
        uint64_t vt = 0;
        if (!w.constraint || !vtable || !m.get(w.constraint, vt) || vt != vtable) continue;
        for (int k = 0; k < kSuspValues; k++)
        {
            float now = 0.0f;
            if (w.ours[k] && m.get(w.constraint + kSuspValueAt[k], now) && now == w.wrote[k] && m.put(w.constraint + kSuspValueAt[k], w.stock[k])) restored++;
            w.ours[k] = false;
        }
    }
    return restored;
}

// SuspStepTest's write (suspension_step.h): one wheel's value at factor times what the tune gives it, made by the tune
// and recorded as its own, so it never becomes a new stock (review 15). Its wheel's writes are not counted while a
// test runs, as they are no sign of the game putting its values back. A factor of 1 is the test between its writes.
struct SuspTuneTest
{
    int wheel, value;
    float factor;
};

// One pass for the driven truck: vtable is the constraint's vtable in the game (0 = not known: nothing is done).
// scale: the front's and the rear's spring and damping factors and two factors of 1 each (the travel's ends go by
// extra); ends gives each wheel its one (SuspEndFactor). Where the two differ and the wheel's end is not known, that
// value is left as it is and held is set. Returns the wheels written on
// this pass. extra: metres past the file's compression end (ExtraTravel), 0 for the file's; it never shortens.
// restored: the values of the truck driven before put back on this pass (another truck is driven now).
// test: SuspStepTest's write, nullptr for none. damper: per wheel, the damper owns its damping (suspension_step.h):
// the tune neither writes it nor takes what it finds there for a new stock, so the damper's values never compound
// (review 15); nullptr for none. spring: the same for the strength (the spring's features, BRIEF_suspension_extras.md),
// and both for the tune on the step (BRIEF_step_hook_writes.md). words: the mod's last words in each wheel's strength
// and damping (the step hook's and the let go's), nullptr for none: each is taken as the tune's own write, so it puts
// the stock back on a truck switch and is never a new stock, in place or at a new address (the Reviewer's must 3).
inline int SuspTuneStepEnds(SuspTune &t, const TruckMemory &m, const TruckRefs &r, uint64_t vtable, const float scale[2][kSuspValues], const SuspEnds *ends,
                            int *restored = nullptr, const SuspTuneTest *test = nullptr, const bool *damper = nullptr, const bool *spring = nullptr, float extra = 0.0f,
                            const SuspModWords *words = nullptr)
{
    if (words && words->vehicle && words->vehicle == t.vehicle)
        for (int i = 0; i < kMaxWheels; i++)
        {
            SuspWheel &w = t.wheel[i];
            if (!w.constraint || words->c[i] != w.constraint) continue;
            for (int k = 0; k < 2; k++)
                if (words->has[i][k]) w.wrote[k] = words->word[i][k], w.ours[k] = !SuspSame(w.wrote[k], w.stock[k]);
        }
    // the fields the physics thread owns on a wheel: the damping (the damper, the zones) and the strength (the spring's features)
    auto owned = [&](int i, int k) { return (damper && damper[i] && k == kSuspDamping) || (spring && spring[i] && k == kSuspStrength); };
    if (restored) *restored = 0;
    if (t.vehicle != r.vehicle)
    {
        const int back = SuspTuneRestore(t, m, vtable);
        if (restored) *restored = back;
        t = SuspTune{ r.vehicle };
        memcpy(t.scale, scale, sizeof t.scale);
    }
    t.moved = memcmp(t.scale, scale, sizeof t.scale) != 0;
    t.held = false;
    memcpy(t.scale, scale, sizeof t.scale);
    int written = 0;
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
    {
        SuspWheel &w = t.wheel[i];
        w.fresh = w.wroteNow = false;
        uint64_t c = 0, vt = 0;
        float now[kSuspValues] = {};
        bool read = vtable && m.get(r.wheel[i] + kSuspConstraintAt, c) && m.get(c, vt) && vt == vtable;
        for (int k = 0; read && k < kSuspValues; k++) read = m.get(c + kSuspValueAt[k], now[k]);
        if (!read) continue;
        if (w.constraint != c)
        {
            // a constraint at a new address: a value that is the word the mod last wrote for this wheel is the mod's, so
            // the stock stays (the game copied the tuned values over: x2 never becomes x4, review 1 F1); any other is
            // the game's new stock
            const SuspWheel old = w;
            w = SuspWheel{ c };
            for (int k = 0; k < kSuspValues; k++)
            {
                const bool mine = old.constraint && old.ours[k] && SuspSame(now[k], old.wrote[k]);
                w.stock[k] = mine ? old.stock[k] : now[k];
                if (mine) w.wrote[k] = now[k], w.ours[k] = true;
            }
            w.fresh = true;
        }
        else
            for (int k = 0; k < kSuspValues; k++)
                if (!owned(i, k) && !(w.ours[k] && now[k] == w.wrote[k]) && now[k] != w.stock[k])
                {
                    // the game wrote this one anew: its new stock (never what the tune wrote, so nothing compounds)
                    w.stock[k] = now[k];
                    w.ours[k] = false;
                    w.fresh = true;
                }
        for (int k = 0; k < kSuspValues; k++)
        {
            if (!SuspValueSane(k, w.stock[k]) || owned(i, k)) continue; // not a value of the kind known, or the damper's: left as it is
            float factor = 1.0f;
            if (!SuspEndFactor(ends, r.vehicle, i, scale[0][k], scale[1][k], factor))
            {
                t.held = true; // front and rear apart and this wheel's end not known: nothing new is written
                continue;
            }
            float want = w.stock[k] * factor;
            if (k == kSuspStrength || k == kSuspDamping) want = fminf(1.0f, want);
            if (factor == 1.0f) want = w.stock[k];
            if (k == kSuspMin && extra > 0.0f) want = w.stock[k] - extra; // past the file's end, from a 0.0 too
            if (test && test->wheel == i && test->value == k && test->factor != 1.0f) want *= test->factor;
            const bool tuned = want != w.stock[k];
            if (now[k] == want || (!tuned && !w.ours[k])) continue;
            uint64_t still = 0;
            if (!m.get(r.wheel[i] + kSuspConstraintAt, still) || still != c || !m.get(c, vt) || vt != vtable) break;
            if (!m.put(c + kSuspValueAt[k], want)) continue;
            w.wrote[k] = want;
            w.ours[k] = tuned;
            w.wroteNow = true;
        }
        if (w.wroteNow)
        {
            written++;
            if (t.moved) w.settle = true;
            else if (!(test && test->wheel == i)) w.writes++;
            for (int k = 0; k < kSuspValues; k++) m.get(c + kSuspValueAt[k], w.back[k]);
        }
    }
    return written;
}

// Both ends at the same factors: the tune of before front and rear were apart.
inline int SuspTuneStep(SuspTune &t, const TruckMemory &m, const TruckRefs &r, uint64_t vtable, const float scale[kSuspValues], int *restored = nullptr,
                        const SuspTuneTest *test = nullptr, const bool *damper = nullptr, const bool *spring = nullptr, float extra = 0.0f,
                        const SuspModWords *words = nullptr)
{
    float both[2][kSuspValues];
    memcpy(both[0], scale, sizeof both[0]);
    memcpy(both[1], scale, sizeof both[1]);
    return SuspTuneStepEnds(t, m, r, vtable, both, nullptr, restored, test, damper, spring, extra, words);
}

// A value the step hook found the game had set itself in a field the hook writes (suspension_step.h, SuspGameSet): the
// wheel's new stock, where the tune is still on that truck (vehicle, the block's) and the wheel's constraint is c. k:
// kSuspStrength or kSuspDamping. True where taken; the next block then wants the slider times it.
inline bool SuspTuneTake(SuspTune &t, uint64_t vehicle, int i, uint64_t c, int k, float value)
{
    if (t.vehicle != vehicle || i < 0 || i >= kMaxWheels || (k != kSuspStrength && k != kSuspDamping) || !c || t.wheel[i].constraint != c) return false;
    SuspWheel &w = t.wheel[i];
    w.stock[k] = value;
    w.ours[k] = false;
    w.fresh = true;
    return true;
}

// ---- the travel probe (review 14: constraint + 0xA4 is the attach point the game's deviation moves, not the travel) ----
// A wheel's centre in the chassis's frame: the world's offset from the chassis along its forward, up and left axes.
// Along up it is the candidate for the wheel's travel, rising as the suspension compresses; the probe logs it with
// its range and its fastest speed, so a drive over bumps and with a load shows whether it is.
inline void SuspLocal(const TruckRaw &t, int i, float out[3])
{
    const float d[3] = { t.wheelAt[i][0] - t.position[0], t.wheelAt[i][1] - t.position[1], t.wheelAt[i][2] - t.position[2] };
    const float *axis[3] = { t.forward, t.up, t.left };
    for (int k = 0; k < 3; k++) out[k] = d[0] * axis[k][0] + d[1] * axis[k][1] + d[2] * axis[k][2];
}

// The ends from the truck as read: each wheel's place in the chassis's frame, by the places read on this pass.
inline bool SuspEndsPass(SuspEnds &e, const TruckRaw &t, uint64_t vehicle, int wheels)
{
    float forward[kMaxWheels] = {}, left[kMaxWheels] = {};
    for (int i = 0; i < wheels && i < t.wheels && i < kMaxWheels; i++)
    {
        if (!(t.placeRead >> i & 1u)) continue;
        float local[3];
        SuspLocal(t, i, local);
        forward[i] = local[0], left[i] = local[2];
    }
    return SuspEndsStep(e, vehicle, wheels, forward, left, t.placeRead);
}

struct SuspTravel
{
    uint64_t vehicle;
    int looks;                   // passes in this window
    float clock;                 // the game's clock at the last pass (TruckRaw::clock)
    float last[kMaxWheels];      // each wheel's height in the chassis's frame at the last pass
    float least[kMaxWheels], most[kMaxWheels], fastest[kMaxWheels]; // in this window; fastest signed, rising positive
    uint32_t generation;         // the step probe's (gauges.cpp, SuspStepPass) the window is of
};

// One pass: false where the truck's places were not read. A new truck starts over; a pass without the game's clock
// moving (a menu, a pause) adds nothing.
inline bool SuspTravelStep(SuspTravel &s, const TruckRaw &t, uint64_t vehicle)
{
    if (!t.waterKnown || !t.clockKnown || t.wheels <= 0) return false;
    if (s.vehicle != vehicle)
    {
        s = SuspTravel{ vehicle };
        s.clock = t.clock;
    }
    const float dt = t.clock - s.clock;
    if (s.looks && dt <= 0.0f) return true;
    for (int i = 0; i < t.wheels && i < kMaxWheels; i++)
    {
        float local[3];
        SuspLocal(t, i, local);
        const float h = local[1];
        if (!s.looks)
        {
            s.least[i] = s.most[i] = h;
            s.fastest[i] = 0.0f;
        }
        else
        {
            s.least[i] = fminf(s.least[i], h);
            s.most[i] = fmaxf(s.most[i], h);
            const float v = dt > 0.0f ? (h - s.last[i]) / dt : 0.0f;
            if (fabsf(v) > fabsf(s.fastest[i])) s.fastest[i] = v;
        }
        s.last[i] = h;
    }
    s.clock = t.clock;
    s.looks++;
    return true;
}

// A new window: the ranges start from the last heights.
inline void SuspTravelWindow(SuspTravel &s, int wheels)
{
    for (int i = 0; i < wheels && i < kMaxWheels; i++)
    {
        s.least[i] = s.most[i] = s.last[i];
        s.fastest[i] = 0.0f;
    }
    s.looks = 1;
}
