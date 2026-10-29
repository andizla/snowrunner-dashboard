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
    float scale[kSuspValues];   // the sliders on the last pass
    bool moved;                 // they moved since the last pass
    SuspWheel wheel[kMaxWheels];
};

// A value the tune may take as a stock: the spring and the damping a factor of Havok's, a limit on its side of 0.
inline bool SuspValueSane(int k, float v)
{
    if (k == kSuspMin) return v < 0.0f && v > -5.0f;
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

// One pass for the driven truck: vtable is the constraint's vtable in the game (0 = not known: nothing is done).
// scale: SpringScale, DampingScale, TravelMinScale, TravelMaxScale. Returns the wheels written on this pass.
// restored: the values of the truck driven before put back on this pass (another truck is driven now).
inline int SuspTuneStep(SuspTune &t, const TruckMemory &m, const TruckRefs &r, uint64_t vtable, const float scale[kSuspValues], int *restored = nullptr)
{
    if (restored) *restored = 0;
    if (t.vehicle != r.vehicle)
    {
        const int back = SuspTuneRestore(t, m, vtable);
        if (restored) *restored = back;
        t = SuspTune{ r.vehicle };
        memcpy(t.scale, scale, sizeof t.scale);
    }
    t.moved = memcmp(t.scale, scale, sizeof t.scale) != 0;
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
            w = SuspWheel{ c };
            memcpy(w.stock, now, sizeof w.stock);
            w.fresh = true;
        }
        else
            for (int k = 0; k < kSuspValues; k++)
                if (!(w.ours[k] && now[k] == w.wrote[k]) && now[k] != w.stock[k])
                {
                    // the game wrote this one anew: its new stock (never what the tune wrote, so nothing compounds)
                    w.stock[k] = now[k];
                    w.ours[k] = false;
                    w.fresh = true;
                }
        for (int k = 0; k < kSuspValues; k++)
        {
            if (!SuspValueSane(k, w.stock[k])) continue; // not a value of the kind known: left as it is
            float want = w.stock[k] * scale[k];
            if (k == kSuspStrength || k == kSuspDamping) want = fminf(1.0f, want);
            if (scale[k] == 1.0f) want = w.stock[k];
            if (now[k] == want || (scale[k] == 1.0f && !w.ours[k])) continue;
            uint64_t still = 0;
            if (!m.get(r.wheel[i] + kSuspConstraintAt, still) || still != c || !m.get(c, vt) || vt != vtable) break;
            if (!m.put(c + kSuspValueAt[k], want)) continue;
            w.wrote[k] = want;
            w.ours[k] = scale[k] != 1.0f;
            w.wroteNow = true;
        }
        if (w.wroteNow)
        {
            written++;
            if (t.moved) w.settle = true;
            else w.writes++;
            for (int k = 0; k < kSuspValues; k++) m.get(c + kSuspValueAt[k], w.back[k]);
        }
    }
    return written;
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

struct SuspTravel
{
    uint64_t vehicle;
    int looks;                   // passes in this window
    float clock;                 // the game's clock at the last pass (TruckRaw::clock)
    float last[kMaxWheels];      // each wheel's height in the chassis's frame at the last pass
    float least[kMaxWheels], most[kMaxWheels], fastest[kMaxWheels]; // in this window; fastest signed, rising positive
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
