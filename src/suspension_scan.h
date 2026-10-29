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
// Each wheel's own constraint is [wheel object + 0x28], and in it the truck file's Strength (+0xC4) and Damping
// (+0xC8) as Havok's soft atom keeps them, factors from 0 to 1 that the game does not rewrite while driving. The tune
// writes each as the stock value times its slider, held to 0 to 1. The stock is read once per truck and wheel, again
// when [wheel + 0x28] changes, and again when the value there is neither the stock nor what the tune wrote (the game
// rebuilt the constraint in place). The slider at 1 writes nothing, but it puts the stock back where the tune wrote.
static const uint32_t kSuspConstraintAt = 0x28, kSuspStrengthAt = 0xC4, kSuspDampingAt = 0xC8;

struct SuspWheel
{
    uint64_t constraint;    // where the stock was read, 0 = not yet
    float stock[2];         // Strength, Damping as the truck has them
    float wrote[2];         // what the tune last wrote, below 0 = nothing
    bool fresh;             // the stock was read on this pass (for the log)
    bool wroteNow;          // a value was written on this pass (for the log), and what was read back right after
    float back[2];
    int writes;             // passes that wrote it, from the stock read on
};

struct SuspTune
{
    uint64_t vehicle;
    SuspWheel wheel[kMaxWheels];
};

inline bool SuspFactor(float v) { return v > 0.0f && v <= 1.0f; }

// One pass for the driven truck: vtable is the constraint's vtable in the game (0 = not known: nothing is done).
// scale: SpringScale and DampingScale. Returns the wheels written on this pass.
inline int SuspTuneStep(SuspTune &t, const TruckMemory &m, const TruckRefs &r, uint64_t vtable, const float scale[2])
{
    if (t.vehicle != r.vehicle) t = SuspTune{ r.vehicle };
    int written = 0;
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
    {
        SuspWheel &w = t.wheel[i];
        w.fresh = w.wroteNow = false;
        uint64_t c = 0, vt = 0;
        float now[2] = {};
        if (!vtable || !m.get(r.wheel[i] + kSuspConstraintAt, c) || !m.get(c, vt) || vt != vtable || !m.get(c + kSuspStrengthAt, now[0]) ||
            !m.get(c + kSuspDampingAt, now[1]) || !SuspFactor(now[0]) || !SuspFactor(now[1]))
            continue;
        if (w.constraint != c) w = SuspWheel{ c, { now[0], now[1] }, { -1.0f, -1.0f }, true };
        else
            for (int k = 0; k < 2; k++)
                if (now[k] != w.wrote[k] && now[k] != w.stock[k])
                {
                    // the game wrote this one anew: its new stock (never what the tune wrote, so nothing compounds)
                    w.stock[k] = now[k];
                    w.wrote[k] = -1.0f;
                    w.fresh = true;
                }
        for (int k = 0; k < 2; k++)
        {
            const float want = scale[k] == 1.0f ? w.stock[k] : fminf(1.0f, fmaxf(0.0f, w.stock[k] * scale[k]));
            if (now[k] == want || (scale[k] == 1.0f && w.wrote[k] < 0.0f)) continue;
            uint64_t still = 0;
            if (!m.get(r.wheel[i] + kSuspConstraintAt, still) || still != c || !m.get(c, vt) || vt != vtable) break;
            if (!m.put(c + (k ? kSuspDampingAt : kSuspStrengthAt), want)) continue;
            w.wrote[k] = scale[k] == 1.0f ? -1.0f : want;
            w.wroteNow = true;
        }
        if (w.wroteNow)
        {
            written++;
            w.writes++;
            m.get(c + kSuspStrengthAt, w.back[0]);
            m.get(c + kSuspDampingAt, w.back[1]);
        }
    }
    return written;
}
