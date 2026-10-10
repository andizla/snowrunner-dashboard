// clock_scan.h: the game's time of day, found in its memory and read once found. The game keeps the hour of its day
// somewhere among its objects (trainers freeze it at 12:00 or 00:00), but where is not known for this build, so the
// mod looks for it while the player drives (Probe=1): every value from 0 to 24 in the objects the exe's statics around
// the game's main objects point to, and in the objects those point to, kept while it rises at a steady pace against
// the game's own clock (the seconds the game has simulated the truck for, vehicle + 0x114), so the game standing in
// a menu is no rise. A place that keeps rising and then jumps (the night skipped on the map, or time passed in a
// garage) is the clock; one that is the only steady place after kClockAlone seconds is taken as well. The place is
// kept as a path from a static of the exe ([exe + rva] + offset, [[exe + rva] + offset] + offset), which holds from
// one start of the game to the next, and Gauges.ini keeps it under [Clock] for the exe it was found in.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include "truck.h"

// Where the clock is: depth 0 the value at exe + rva, 1 at [exe + rva] + off[0], 2 at [[exe + rva] + off[0]] + off[1].
// scale: 1 for hours, 24 for a share of the day.
struct ClockPath
{
    uint32_t rva;
    int depth;
    uint32_t off[2];
    bool isDouble;
    float scale;
};

// The hour of the game's day at path, 0 to 24; false where it cannot be read or is no hour.
inline bool ClockRead(const TruckMemory &m, const ClockPath &p, float &hour)
{
    uint64_t at = m.base + p.rva;
    for (int i = 0; i < p.depth; i++)
    {
        uint64_t next = 0;
        if (!m.get(at, next) || next < 0x10000) return false;
        at = next + p.off[i];
    }
    double v = 0.0;
    if (p.isDouble) { if (!m.get(at, v)) return false; }
    else
    {
        float f = 0.0f;
        if (!m.get(at, f)) return false;
        v = f;
    }
    v *= p.scale;
    if (!isfinite(v) || v < 0.0 || v > 24.5) return false;
    hour = (float)fmod(v, 24.0);
    return true;
}

// The path as Gauges.ini keeps it: rva,depth,off0,off1,f or d,scale (hex for the offsets).
inline void ClockPathText(const ClockPath &p, char *out, size_t size)
{
    snprintf(out, size, "%x,%d,%x,%x,%c,%g", p.rva, p.depth, p.off[0], p.off[1], p.isDouble ? 'd' : 'f', p.scale);
}
inline bool ClockPathParse(const char *text, ClockPath &p)
{
    unsigned rva = 0, a = 0, b = 0;
    int depth = 0;
    char type = 0;
    float scale = 0.0f;
    if (sscanf(text, "%x,%d,%x,%x,%c,%f", &rva, &depth, &a, &b, &type, &scale) != 6) return false;
    if (depth < 0 || depth > 2 || (type != 'f' && type != 'd') || !(scale == 1.0f || scale == 24.0f)) return false;
    p = ClockPath{ rva, depth, { a, b }, type == 'd', scale };
    return true;
}

// The path in words for the log: [exe+2a8eb90]+1a8.
inline void ClockPathWords(const ClockPath &p, char *out, size_t size)
{
    if (p.depth == 0) snprintf(out, size, "exe+%x (%s)", p.rva, p.isDouble ? "double" : "float");
    else if (p.depth == 1) snprintf(out, size, "[exe+%x]+%x (%s)", p.rva, p.off[0], p.isDouble ? "double" : "float");
    else snprintf(out, size, "[[exe+%x]+%x]+%x (%s)", p.rva, p.off[0], p.off[1], p.isDouble ? "double" : "float");
}

// ---- the search ----
static const uint32_t kClockStaticFrom = 0x2a80000, kClockStaticTo = 0x2ac0000; // the exe's statics looked through: around the game's main objects
static const uint32_t kClockFirst = 0x800, kClockSecond = 0x200; // bytes read of an object the statics point to, and of one those point to
static const int kClockFirstMost = 4096, kClockSecondMost = 16384;
static const size_t kClockSlotsMost = 1000000; // places kept at the start at the most (about 50 MB), the first look drops most
static const float kClockEvery = 10.0f;      // seconds of the game's clock from one look to the next while the search narrows
static const float kClockEveryKept = 2.0f;   // and once only steady places are left
static const int kClockSteady = 4;           // looks in a row at one pace that make a place steady
static const float kClockPace = 0.1f;        // the share a look's pace may differ from the place's pace
static const float kClockRateLeast = 2.3e-5f, kClockRateMost = 0.08f; // hours (or shares of the day) a second: a day of 5 minutes to 12 hours
static const float kClockJump = 1.0f;        // hours more or less than the pace says: a jump of the clock
static const int kClockConfirm = 2;          // looks at the old pace after a jump that make it the clock set
static const float kClockAlone = 180.0f;     // seconds of the game's clock after which the only steady place is taken
static const int kClockMisses = 3;           // looks off the pace in a row, without a jump, that drop a steady place

enum { kClockIdle, kClockSearching, kClockFound, kClockFailed };

struct ClockObject
{
    uint64_t at;
    uint32_t size;
    ClockPath path; // to the object's start (depth 0: the statics, off[] unused)
};

struct ClockSlot
{
    uint32_t object, offset;
    bool isDouble;
    double last, most; // the value at the last look, and the highest seen
    double rate;       // its pace, per second of the game's clock; 0 before two looks
    int steady;        // looks in a row at that pace
    int misses;        // looks off the pace in a row since it became steady
    int jumpAfter;     // looks at its pace still owed after a jump
    bool jumped;       // it jumped once it was steady, and kept its pace after
    bool told;         // the log has it as steady
};

struct ClockScan
{
    int state;
    std::vector<ClockObject> objects;
    std::vector<ClockSlot> slots;
    uint64_t vehicle;  // the truck whose clock the looks go by
    float clockAt;     // its clock at the last look
    float since;       // seconds of the game's clock since the search began
    bool gap;          // the truck was not driven since the last look
    int looks;
    uint32_t bytes;    // read at the start
    ClockPath found;
};

// A value that may be an address in the game's heap: not in the exe's image, inside the user space, aligned.
inline bool ClockPointer(uint64_t q, uint64_t base, uint64_t imageSize)
{
    return q >= 0x10000 && q < 0x7FFFFFFFFFFFull && !(q & 7) && !(q >= base && q < base + imageSize);
}

// A set of addresses, open addressing; kept small and free of the standard library's hash set, whose header does not
// stand Windows' min and max macros everywhere.
struct ClockSeen
{
    std::vector<uint64_t> table;
    size_t count = 0;
    explicit ClockSeen(size_t room) : table(room * 2 + 1, 0) {}
    // true when at was not in the set (and is now)
    bool add(uint64_t at)
    {
        if (count * 2 >= table.size()) return false;
        size_t i = (size_t)((at >> 3) * 0x9E3779B97F4A7C15ull % table.size());
        while (table[i])
        {
            if (table[i] == at) return false;
            i = (i + 1) % table.size();
        }
        table[i] = at;
        count++;
        return true;
    }
};

inline bool ClockValue(double v) { return isfinite(v) && v > 1e-4 && v < 24.5; }

// The path of a place in an object.
inline ClockPath ClockSlotPath(const ClockScan &s, const ClockSlot &slot, float scale)
{
    const ClockObject &o = s.objects[slot.object];
    ClockPath p = o.path;
    if (p.depth == 0) p.rva += slot.offset;
    else p.off[p.depth - 1] = slot.offset;
    p.isDouble = slot.isDouble;
    p.scale = scale;
    return p;
}

// Starts the search: reads the statics, the objects they point to and the objects those point to, and keeps every
// place with a value from 0 to 24 (not 0). imageSize: the exe's image in memory, which the pointers may not point into.
inline void ClockScanBegin(ClockScan &s, const TruckMemory &m, uint64_t imageSize)
{
    s = ClockScan{ kClockSearching };
    const uint32_t staticSize = kClockStaticTo - kClockStaticFrom;
    std::vector<uint8_t> statics(staticSize);
    if (!m.peek(m.base + kClockStaticFrom, statics.data(), staticSize))
    {
        s.state = kClockFailed;
        return;
    }
    ClockSeen seen(kClockFirstMost + kClockSecondMost);
    s.objects.push_back(ClockObject{ m.base + kClockStaticFrom, staticSize, ClockPath{ kClockStaticFrom, 0, { 0, 0 }, false, 1.0f } });
    std::vector<std::vector<uint8_t>> read;
    read.push_back(statics);
    std::vector<uint8_t> buf(kClockFirst);
    size_t firstEnd = 0;
    for (uint32_t o = 0; o + 8 <= staticSize && (int)s.objects.size() - 1 < kClockFirstMost; o += 8)
    {
        uint64_t q = 0;
        memcpy(&q, statics.data() + o, 8);
        if (!ClockPointer(q, m.base, imageSize) || !seen.add(q) || !m.peek(q, buf.data(), kClockFirst)) continue;
        s.objects.push_back(ClockObject{ q, kClockFirst, ClockPath{ kClockStaticFrom + o, 1, { 0, 0 }, false, 1.0f } });
        read.push_back(buf);
    }
    firstEnd = s.objects.size();
    std::vector<uint8_t> tail(kClockSecond);
    int second = 0;
    for (size_t i = 1; i < firstEnd && second < kClockSecondMost; i++)
        for (uint32_t o = 0; o + 8 <= kClockFirst && second < kClockSecondMost; o += 8)
        {
            uint64_t q = 0;
            memcpy(&q, read[i].data() + o, 8);
            if (!ClockPointer(q, m.base, imageSize) || !seen.add(q) || !m.peek(q, tail.data(), kClockSecond)) continue;
            const ClockPath &up = s.objects[i].path;
            s.objects.push_back(ClockObject{ q, kClockSecond, ClockPath{ up.rva, 2, { o, 0 }, false, 1.0f } });
            read.push_back(tail);
            second++;
        }
    for (size_t i = 0; i < s.objects.size() && s.slots.size() < kClockSlotsMost; i++)
    {
        const std::vector<uint8_t> &b = read[i];
        s.bytes += (uint32_t)b.size();
        for (uint32_t o = 0; o + 4 <= b.size(); o += 4)
        {
            float f = 0.0f;
            memcpy(&f, b.data() + o, 4);
            if (ClockValue(f)) s.slots.push_back(ClockSlot{ (uint32_t)i, o, false, f, f });
            if (o % 8 == 0 && o + 8 <= b.size())
            {
                double d = 0.0;
                memcpy(&d, b.data() + o, 8);
                if (ClockValue(d) && fabs(d) > 1e-6) s.slots.push_back(ClockSlot{ (uint32_t)i, o, true, d, d });
            }
        }
    }
    s.gap = true;
}

// What a look found, for the log.
struct ClockNews
{
    int looked;        // places looked at, 0 when there was no look on this pass
    int left;          // places still in the search
    int steadyNow;     // places that became steady on this look
    int jumpedNow;     // places that jumped on this look
    bool found;        // the clock was found on this pass
    bool failed;       // nothing is left
};

// One pass: vehicle and clock the driven truck's object and its clock (vehicle + 0x114), driving whether it is driven
// in the world. Looks every kClockEvery seconds of the game's clock (kClockEveryKept once only steady places are
// left). The first look on a truck only takes the values; after a stretch the truck was not driven (a menu, the map,
// a garage) a place not yet steady begins anew, and a steady one is checked for a jump.
inline ClockNews ClockScanStep(ClockScan &s, const TruckMemory &m, uint64_t vehicle, float clock, bool driving)
{
    ClockNews n = {};
    if (s.state != kClockSearching) return n;
    if (!driving || !isfinite(clock))
    {
        s.gap = true;
        return n;
    }
    bool baseline = false;
    if (vehicle != s.vehicle || clock < s.clockAt)
    {
        // another truck, another clock (or this one made anew): the values are taken again
        s.vehicle = vehicle;
        baseline = true;
    }
    const float dt = clock - s.clockAt;
    bool allSteady = !s.slots.empty();
    for (const ClockSlot &slot : s.slots)
        if (slot.steady < kClockSteady) { allSteady = false; break; }
    if (!baseline && dt < (allSteady ? kClockEveryKept : kClockEvery)) return n;
    const bool afterGap = s.gap;
    s.gap = false;
    if (!baseline) s.since += dt;
    s.clockAt = clock;
    s.looks++;
    // read each object with places left once
    std::vector<uint8_t> buf;
    uint32_t object = UINT32_MAX;
    bool readOk = false;
    size_t keep = 0;
    for (size_t i = 0; i < s.slots.size(); i++)
    {
        ClockSlot slot = s.slots[i];
        if (slot.object != object)
        {
            object = slot.object;
            const ClockObject &o = s.objects[object];
            buf.resize(o.size);
            readOk = m.peek(o.at, buf.data(), o.size);
        }
        n.looked++;
        if (!readOk) continue;
        double v = 0.0;
        if (slot.isDouble) memcpy(&v, buf.data() + slot.offset, 8);
        else
        {
            float f = 0.0f;
            memcpy(&f, buf.data() + slot.offset, 4);
            v = f;
        }
        if (!ClockValue(v)) continue;
        const double range = slot.most > 1.05 ? 24.0 : 1.0, hours = 24.0 / range;
        double dv = v - slot.last;
        if (dv < 0.0 && slot.last > range * 0.85 && v < range * 0.15) dv += range; // past midnight
        bool keepIt = true;
        if (baseline || (afterGap && slot.steady < kClockSteady))
        {
            if (slot.steady < kClockSteady)
            {
                slot.rate = 0.0;
                slot.steady = 0;
            }
        }
        else if (slot.steady < kClockSteady)
        {
            const double rate = dv / dt;
            if (rate < kClockRateLeast || rate > kClockRateMost) keepIt = false;
            else if (slot.rate > 0.0 && fabs(rate - slot.rate) > kClockPace * slot.rate) keepIt = false;
            else
            {
                slot.rate = slot.rate > 0.0 ? (slot.rate * slot.steady + rate) / (slot.steady + 1) : rate;
                if (++slot.steady == kClockSteady) n.steadyNow++;
            }
        }
        else
        {
            // steady: a jump of kClockJump hours or more away from its pace is the clock set (the night skipped,
            // time passed in a garage), counted once the place keeps its pace for kClockConfirm looks after it; a few
            // looks off the pace without a jump drop it
            const double off = fabs(dv - slot.rate * dt) * hours;
            if (off >= kClockJump)
            {
                slot.jumpAfter = kClockConfirm;
                slot.misses = 0;
            }
            else if (fabs(dv / dt - slot.rate) > kClockPace * slot.rate)
            {
                slot.jumpAfter = 0;
                if (++slot.misses >= kClockMisses) keepIt = false;
            }
            else
            {
                slot.misses = 0;
                if (slot.jumpAfter > 0 && --slot.jumpAfter == 0)
                {
                    slot.jumped = true;
                    n.jumpedNow++;
                }
            }
        }
        if (!keepIt) continue;
        slot.last = v;
        if (v > slot.most) slot.most = v;
        s.slots[keep++] = slot;
    }
    s.slots.resize(keep);
    n.left = (int)keep;
    // found: a steady place that jumped, or the only steady place after kClockAlone
    int steady = 0;
    const ClockSlot *only = nullptr, *jumped = nullptr;
    for (const ClockSlot &slot : s.slots)
    {
        if (slot.steady < kClockSteady) continue;
        steady++;
        only = &slot;
        if (slot.jumped && !jumped) jumped = &slot;
    }
    const ClockSlot *pick = jumped ? jumped : steady == 1 && s.since >= kClockAlone ? only : nullptr;
    if (pick)
    {
        s.found = ClockSlotPath(s, *pick, pick->most > 1.05 ? 1.0f : 24.0f);
        s.state = kClockFound;
        n.found = true;
        s.objects.clear();
        s.slots.clear();
    }
    else if (keep == 0 && s.looks > 1)
    {
        s.state = kClockFailed;
        n.failed = true;
    }
    return n;
}

// The day's length in minutes of the game's clock that a steady place's pace gives.
inline float ClockDayMinutes(const ClockSlot &slot) { return slot.rate > 0.0 ? (float)((slot.most > 1.05 ? 24.0 : 1.0) / slot.rate / 60.0) : 0.0f; }
