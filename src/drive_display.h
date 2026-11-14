// The drive display's own reckoning (ini DriveDisplay=1, BRIEF_drive_display.md with the Reviewer's addendum): which
// wheels spin, which get the drive, and what the mod's cap and lock do. Read only. This file reads no game memory, so
// truck_test checks it offline: the spin bar (DdSpinOf), the averaging by the step's seconds (DdStep), the shares
// (DdShares) and the cells' rows and sides (DdLayout). gearbox_hooks.cpp does the reads around the game's drive and
// gives the record out (DriveShowGet); gauge_draw.h draws it.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "torque_cut.h"

const int kDdWheels = 16;          // the display's own list: the truck's first 16 wheels, driven or not (M1, M3)
const float kDdTau = 0.15f;        // s: the averages' time constant, by the step's own seconds (M4)
const float kDdSpanTimes = 3.0f;   // the spin bar ends at the truck's speed plus the larger of 3 times it
const float kDdSpanLeast = 3.0f;   // and 3 m/s (S1: one rule, no step at 1 m/s)
const float kDdBand = 0.15f;       // plain within 15 percent of the truck's speed
const float kDdBandLeast = 1.0f;   // m/s: a truck slower than this has the band of this speed
const float kDdFloor = 20.0f;      // the driven wheels' parts above 0 must sum to this (impulse over the step's seconds) for a share (M5)
const float kDdLampOn = 0.1f;      // a lamp shows while its average is over this: about a third of a second after the last step
const float kDdTiedMin = 0.2f;     // rad/s: the diff lamp counts a step only where the lock or Open left the wheel further than this from what the drive left it
const float kDdTiedShare = 0.05f;  // and further than this share of what the drive left it
// A step counts for the diff lamp: the wheel as the lock or Open wrote it against the wheel as the drive left it. Open
// writes every wheel on every step, so a lamp for any write was always lit and said nothing.
inline bool DdTied(float written, float left)
{
    const float d = fabsf(written - left);
    return isfinite(d) && d > kDdTiedMin && d > kDdTiedShare * fabsf(left);
}
enum { kDdPlain, kDdAmber, kDdRed, kDdBlue };
const int kDdGameOwn = -1;         // the differential in force where neither the cut nor the lock has the truck; else kDiffForce...

// The spin bar. wheel: the wheel's speed at the ground (spin x rolling radius), truck: the truck's speed at this wheel,
// both m/s in the gear's direction. fill: 0 at the mark in the middle (the truck's speed), up to 1 at the bar's upper end
// for a faster wheel, down to -1 for a slower one. The colour: blue while the wheel turns against the gear (not in
// neutral), red at the bar's end, amber outside the band, else plain.
struct DdSpin
{
    float fill;
    int colour;
    bool ok;
};
inline DdSpin DdSpinOf(float wheel, float truck, int gear)
{
    DdSpin s = { 0.0f, kDdPlain, false };
    if (!isfinite(wheel) || !isfinite(truck)) return s;
    const float span = fmaxf(kDdSpanTimes * fabsf(truck), kDdSpanLeast);
    const float band = kDdBand * fmaxf(fabsf(truck), kDdBandLeast);
    const float f = (wheel - truck) / span;
    s.fill = f > 1.0f ? 1.0f : f < -1.0f ? -1.0f : f;
    s.ok = true;
    if (gear != 0 && wheel < -band) s.colour = kDdBlue;
    else if (fabsf(f) >= 1.0f) s.colour = kDdRed;
    else if (fabsf(wheel - truck) > band) s.colour = kDdAmber;
    return s;
}

// One drive call as the hook read it. given: the impulse the drive's call left on the wheel after the mod's cap and lock
// (the spin added over the wheel's gain); before: the same as the game's drive left it, before the mod wrote. Both only
// for a driven wheel with a gain. capped: the wheel cap clamped this wheel; tied: the lock or Open moved it (DdTied).
struct DdSample
{
    uint64_t vehicle;
    int n;
    uint64_t wheel[kDdWheels];
    bool ok[kDdWheels], driven[kDdWheels], air[kDdWheels], capped[kDdWheels], tied[kDdWheels];
    float speed[kDdWheels], truck[kDdWheels];
    float given[kDdWheels], before[kDdWheels];
    int force, gear;
    float kept;
    uint8_t type[kDdWheels]; // the differential by axle: the wheel's axle's type in force in this step (kDdAx...), 0 where the axles are alike
};

// The averages, the physics thread's own. given and before are kept as impulse over the step's seconds, so a step of
// 1/120 s weighs as two of 1/60 s. has: a step was taken since the last start.
struct DdAvg
{
    uint64_t vehicle;
    int n;
    uint64_t wheel[kDdWheels];
    bool has;
    bool ok[kDdWheels], driven[kDdWheels];
    float speed[kDdWheels], truck[kDdWheels], given[kDdWheels], before[kDdWheels], air[kDdWheels], capped[kDdWheels], tied[kDdWheels];
    float kept;
    int force, gear;
    uint8_t type[kDdWheels]; // the last step's
};

inline bool DdSameWheels(const DdAvg &a, const DdSample &s)
{
    if (a.vehicle != s.vehicle || a.n != s.n) return false;
    for (int i = 0; i < s.n && i < kDdWheels; i++)
        if (a.wheel[i] != s.wheel[i]) return false;
    return true;
}

// One step into the averages: each figure moves toward the step's by 1 - exp(-dt / kDdTau). Another truck, another
// list of wheels or a dt that is no number (or not above 0) starts them anew: the next good step is taken whole.
// A wheel that was not read, or not driven, and now is, takes its step whole too. Returns whether a step was taken.
inline bool DdStep(DdAvg &a, const DdSample &s, float dt)
{
    if (!(dt > 0.0f) || !isfinite(dt) || s.n < 0 || s.n > kDdWheels)
    {
        a.has = false;
        return false;
    }
    if (!a.has || !DdSameWheels(a, s))
    {
        a = DdAvg{};
        a.vehicle = s.vehicle;
        a.n = s.n;
        for (int i = 0; i < s.n; i++) a.wheel[i] = s.wheel[i];
        a.kept = 1.0f;
    }
    const float k = a.has ? 1.0f - expf(-dt / kDdTau) : 1.0f;
    auto to = [](float &avg, float now, float k) { avg = isfinite(now) ? avg + k * (now - avg) : avg; };
    for (int i = 0; i < s.n; i++)
    {
        const bool ok = s.ok[i] && isfinite(s.speed[i]) && isfinite(s.truck[i]);
        const bool driven = ok && s.driven[i] && isfinite(s.given[i]) && isfinite(s.before[i]);
        const float kw = a.ok[i] ? k : 1.0f, kd = a.driven[i] ? k : 1.0f;
        if (ok)
        {
            to(a.speed[i], s.speed[i], kw);
            to(a.truck[i], s.truck[i], kw);
            to(a.air[i], s.air[i] ? 1.0f : 0.0f, kw);
        }
        if (driven)
        {
            to(a.given[i], s.given[i] / dt, kd);
            to(a.before[i], s.before[i] / dt, kd);
            to(a.capped[i], s.capped[i] ? 1.0f : 0.0f, kd);
            to(a.tied[i], s.tied[i] ? 1.0f : 0.0f, kd);
        }
        else a.given[i] = a.before[i] = a.capped[i] = a.tied[i] = 0.0f;
        a.ok[i] = ok;
        a.driven[i] = driven;
        a.type[i] = driven ? s.type[i] : 0;
    }
    to(a.kept, s.kept, k);
    a.force = s.force;
    a.gear = s.gear;
    a.has = true;
    return true;
}

// The record the display reads. row and right: the wheel's cell, row 0 the front. share and shareBefore: 0 to 1, or
// under 0 for an empty bar with no figure (a wheel not driven, or no drive to share). seq and at are the hook's.
struct DdShown
{
    uint64_t vehicle, at;
    unsigned long seq;
    int n, rows, force, gear;
    float kept;
    bool braking;
    uint8_t row[kDdWheels];
    bool right[kDdWheels];
    bool ok[kDdWheels], driven[kDdWheels], air[kDdWheels], capLamp[kDdWheels], lockLamp[kDdWheels];
    float speed[kDdWheels], truck[kDdWheels];
    float share[kDdWheels], shareBefore[kDdWheels];
    uint8_t type[kDdWheels]; // each driven wheel's axle's type in force where the axles differ (kDdAx...), else 0
    uint8_t steer;           // the percent of the steering left while a lock limits it, 0 none (lock_steer.h); the mod's thread sets it, not the hook
};

// The shares (M5): each driven wheel's part above 0 over the sum of the parts above 0. A wheel under 0 gets an empty
// bar and the lock's lamp. A sum under the floor: no share for any wheel, and braking where the parts under 0 pass
// the floor the other way (the engine holds the truck back). No division where the sum is 0.
inline void DdSharesOf(const float *part, const bool *driven, int n, float *share, bool *under, bool *braking)
{
    float up = 0.0f, down = 0.0f;
    for (int i = 0; i < n; i++)
    {
        share[i] = -1.0f;
        if (under) under[i] = false;
        if (!driven[i] || !isfinite(part[i])) continue;
        if (part[i] > 0.0f) up += part[i];
        else down -= part[i];
    }
    const bool any = up >= kDdFloor && isfinite(up);
    if (braking) *braking = !any && down >= kDdFloor;
    if (!any) return;
    for (int i = 0; i < n; i++)
    {
        if (!driven[i] || !isfinite(part[i])) continue;
        share[i] = part[i] > 0.0f ? part[i] / up : 0.0f;
        if (under && part[i] < 0.0f) under[i] = true;
    }
}

// The averages into the record: the figures, the shares after and before the mod, the lamps. The cells' rows and
// sides, seq and at are the caller's.
inline void DdShow(const DdAvg &a, DdShown &o)
{
    o.vehicle = a.vehicle;
    o.n = a.has ? a.n : 0;
    o.force = a.force;
    o.gear = a.gear;
    o.kept = isfinite(a.kept) ? (a.kept < 0.0f ? 0.0f : a.kept > 1.0f ? 1.0f : a.kept) : 1.0f;
    bool under[kDdWheels];
    DdSharesOf(a.given, a.driven, o.n, o.share, under, &o.braking);
    DdSharesOf(a.before, a.driven, o.n, o.shareBefore, nullptr, nullptr);
    for (int i = 0; i < o.n; i++)
    {
        o.ok[i] = a.ok[i];
        o.driven[i] = a.driven[i];
        o.type[i] = a.type[i];
        o.speed[i] = a.speed[i];
        o.truck[i] = a.truck[i];
        o.air[i] = a.ok[i] && a.air[i] > 0.5f;
        o.capLamp[i] = a.driven[i] && a.capped[i] > kDdLampOn;
        o.lockLamp[i] = a.driven[i] && (a.tied[i] > kDdLampOn || under[i]);
    }
}

// The cells (M3). axle: AxleGroups' table over all the wheels (a pair has the lower index, a wheel alone is its own);
// along: how far forward each wheel stands, for the rows' order (the front at the top); side: above 0 on the left;
// placed: the wheel's place was read. grouped false (the read faulted) or a wheel not placed: one wheel a row in the
// list's order. Else a row an axle, ordered by its wheels' mean along, the further forward first, the list's order
// where two are level. A pair on one side keeps the lower index on the left. Returns the rows.
inline int DdLayout(const int *axle, const float *along, const float *side, const bool *placed, int n, bool grouped, uint8_t *row, bool *right)
{
    if (n > kDdWheels) n = kDdWheels;
    if (n < 0) n = 0;
    bool all = grouped;
    for (int i = 0; i < n && all; i++) all = placed[i] && isfinite(along[i]) && isfinite(side[i]) && axle[i] >= 0 && axle[i] <= i && axle[axle[i]] == axle[i];
    if (!all)
    {
        for (int i = 0; i < n; i++) row[i] = (uint8_t)i, right[i] = placed[i] && isfinite(side[i]) && side[i] < 0.0f;
        return n;
    }
    int head[kDdWheels], rows = 0;
    float key[kDdWheels];
    for (int i = 0; i < n; i++)
    {
        if (axle[i] != i) continue;
        float sum = 0.0f;
        int m = 0;
        for (int j = i; j < n; j++)
            if (axle[j] == i) sum += along[j], m++;
        head[rows] = i, key[rows] = sum / (float)m, rows++;
    }
    // the rows by their key, the further forward first; an insertion sort keeps the list's order where two are level
    for (int r = 1; r < rows; r++)
    {
        const int h = head[r];
        const float k = key[r];
        int at = r;
        while (at > 0 && key[at - 1] < k) head[at] = head[at - 1], key[at] = key[at - 1], at--;
        head[at] = h, key[at] = k;
    }
    for (int r = 0; r < rows; r++)
    {
        int first = -1;
        for (int j = 0; j < n; j++)
        {
            if (axle[j] != head[r]) continue;
            row[j] = (uint8_t)r;
            right[j] = side[j] < 0.0f;
            if (first >= 0 && right[first] == right[j]) right[first] = false, right[j] = true; // both on one side: the lower index on the left
            if (first < 0) first = j;
        }
    }
    return rows;
}

// The line under the wheels: the differential in force and what the wheel cap kept of the drive.
inline const char *DdForceName(int force)
{
    return force == kDiffForceOpen ? "Open" : force == kDiffForceLimited ? "Limited slip" : force == kDiffForceLock ? "Locked" : force == kDiffForceAxles ? "By axle" : "Game diff";
}
// The differential by axle (BRIEF_axle_diff.md, M4 and M8): an axle's type in force as the display names it. The first
// three are kDiffOpen, kDiffLimited and kDiffLocked; kDdAxNoBite is Locked with nothing to tie it (Diff lock bite 0, or
// no lock slot published).
enum { kDdAxNone, kDdAxOpen, kDdAxLimited, kDdAxLocked, kDdAxNoBite };
inline const char *DdAxleName(int type)
{
    return type == kDdAxOpen ? "Open" : type == kDdAxLimited ? "Limited slip" : type == kDdAxLocked ? "Locked" : type == kDdAxNoBite ? "Locked, no bite" : "";
}
// From four axles on the line has the short names, so that it stays in the box.
inline const char *DdAxleShort(int type)
{
    return type == kDdAxOpen ? "O" : type == kDdAxLimited ? "LS" : type == kDdAxLocked ? "L" : type == kDdAxNoBite ? "L, no bite" : "";
}
// The line under the wheels. Where the axles are alike: the one name, as before. Where they differ: each driven axle's
// type front to back, by the rows, " / " between them, short from four axles on. An axle with no wheel driven in this
// step is left out. After it "steering 60 %" while a lock limits the steering (lock_steer.h).
inline void DdAxlesLine(const DdShown &d, char *out, size_t size)
{
    if (!size) return;
    out[0] = 0;
    size_t len = 0;
    auto typeOf = [&](int r) {
        int type = kDdAxNone;
        for (int i = 0; i < d.n && i < kDdWheels && type == kDdAxNone; i++)
            if (d.row[i] == r && d.driven[i]) type = d.type[i];
        return type;
    };
    auto steering = [&]() {
        if (d.steer > 0 && d.steer < 100 && len < size) snprintf(out + len, size - len, ", steering %d %%", d.steer);
    };
    int named = 0;
    if (d.force == kDiffForceAxles)
        for (int r = 0; r < d.rows; r++) named += DdAxleName(typeOf(r))[0] ? 1 : 0;
    if (d.force == kDiffForceAxles)
        for (int r = 0; r < d.rows; r++)
        {
            const int type = typeOf(r);
            const char *name = named >= 4 ? DdAxleShort(type) : DdAxleName(type);
            if (!name[0]) continue;
            const int put = snprintf(out + len, size - len, "%s%s", len ? " / " : "", name);
            if (put < 0 || (size_t)put >= size - len)
            {
                out[size - 1] = 0;
                return;
            }
            len += (size_t)put;
        }
    if (!len)
    {
        const int put = snprintf(out, size, "%s", DdForceName(d.force));
        len = put < 0 || (size_t)put >= size ? size : (size_t)put;
    }
    steering();
}

// ---- the differential by axle: the rows of the Power tab (BRIEF_axle_diff.md, M5 to M7) ----
// The truck's axles front to back, from what the mod's thread read of every wheel: its parent body, its place in that
// body's frame (along, side; AxlePlace), its place along the main body (rowAt, the order's key) and how it is driven
// (0 not, 1 always, 2 with all wheel drive). One grouping: AxleGroups, with its second pairing in the main body's frame
// (rowAt along it, mainSide across it; nullptr: the main body was not read). One order: DdLayout's, the further
// forward first. A wheel left with no partner is an axle and a row of its own (BRIEF_tester_1114.md, part 186, must 1).
// axles 0: no rows (a wheel not placed, no pair at all, more wheels than the display holds or more than kAxleRowsMost
// axles); the whole truck then goes by DiffType as before. The wheels left alone are kept for the log either way.
enum { kAxleRowsMost = 8 };
struct AxleRows
{
    int axles;                // every axle of the truck, driven or not
    int ofWheel[kDdWheels];   // each wheel's axle, 0 the front
    int drive[kAxleRowsMost]; // 0 not driven (no row), 1 always, 2 with all wheel drive
    int alone;                // the wheels with no partner (0 with a wheel not placed: nothing was grouped)
    int across;               // the axles paired in the main body's frame
    bool lone[kDdWheels];     // the wheel has no partner
};
inline AxleRows AxleRowsMake(const uint64_t *body, const float *along, const float *side, const float *rowAt, const bool *ok, const int *drive, int n,
                             const float *mainSide = nullptr)
{
    AxleRows r = {};
    if (n < 2 || n > kDdWheels) return r;
    for (int i = 0; i < n; i++)
        if (!ok[i]) return r;
    const AxleGroupsOut g = AxleGroups(body, along, side, ok, n, mainSide ? rowAt : nullptr, mainSide);
    r.alone = g.alone, r.across = g.across;
    for (int i = 0; i < n; i++) r.lone[i] = AxleLone(g, n, i);
    if (g.alone >= n) return r;
    float sideBy[kDdWheels];
    AxleSideBy(g, n, side, mainSide, sideBy);
    uint8_t row[kDdWheels];
    bool right[kDdWheels];
    const int rows = DdLayout(g.axle, rowAt, sideBy, ok, n, true, row, right);
    if (rows < 1 || rows > kAxleRowsMost || rows >= n) return r;
    r.axles = rows;
    for (int i = 0; i < n; i++)
    {
        r.ofWheel[i] = row[i];
        int &k = r.drive[row[i]];
        if (drive[i] == 1) k = 1;
        else if (drive[i] == 2 && k == 0) k = 2;
    }
    return r;
}
// An axle's set type (M6): its own key where it holds 0 to 3, else the truck's DiffType.
inline int AxleSetOf(const int *diffAxle, int axle, int diffType)
{
    const int v = axle >= 0 && axle < kAxleRowsMost ? diffAxle[axle] : -1;
    return v >= kDiffAuto && v <= kDiffLocked ? v : diffType;
}
// The two levers (M7): the front one has axle 1 where it can be driven, the rear one every other axle that can.
inline bool AxleLeverHas(const AxleRows &r, bool front)
{
    if (front) return r.axles > 0 && r.drive[0] != 0;
    for (int a = 1; a < r.axles; a++)
        if (r.drive[a]) return true;
    return false;
}
// The levers' state, kept with the truck's watch by vehicle and never written (M7). Another truck starts with both
// off, and back on the first they are off too. A press: -1 nothing to lock (no state), else 1 on or 0 off.
struct AxleLevers
{
    uint64_t vehicle;
    bool front, rear;
};
inline void AxleLeversFor(AxleLevers &l, uint64_t vehicle)
{
    if (vehicle && l.vehicle != vehicle) l = AxleLevers{ vehicle, false, false };
}
inline int AxleLeverPressed(AxleLevers &l, uint64_t vehicle, const AxleRows &r, bool front)
{
    if (!vehicle || !AxleLeverHas(r, front)) return -1;
    AxleLeversFor(l, vehicle);
    bool &on = front ? l.front : l.rear;
    on = !on;
    return on ? 1 : 0;
}
// What is in force on each axle in this pass: its set type through DiffTypeOf (Auto by the lock kind, the same for
// every axle), or Locked while its lever is on. now: driven in this pass (always, or with all wheel drive on).
// common: the type every axle driven now has, -1 where two differ; with none driven the whole truck's by DiffType.
// byAxle: the set types of the axles that can be driven differ (the Diff row then reads "By axle").
struct AxlePlan
{
    int type[kAxleRowsMost];
    bool lever[kAxleRowsMost], now[kAxleRowsMost];
    int common;
    bool byAxle;
};
inline AxlePlan AxlePlanOf(const AxleRows &r, const int *diffAxle, int diffType, bool kindRead, int kind, bool awd, bool leverFront, bool leverRear)
{
    AxlePlan p = {};
    int first = -1, firstSet = -1;
    bool differ = false;
    for (int a = 0; a < r.axles && a < kAxleRowsMost; a++)
    {
        const int set = AxleSetOf(diffAxle, a, diffType);
        p.lever[a] = r.drive[a] != 0 && (a == 0 ? leverFront : leverRear);
        p.type[a] = p.lever[a] ? kDiffLocked : DiffTypeOf(set, kindRead, kind);
        p.now[a] = r.drive[a] == 1 || (r.drive[a] == 2 && awd);
        if (r.drive[a])
        {
            if (firstSet < 0) firstSet = set;
            else if (set != firstSet) p.byAxle = true;
        }
        if (!p.now[a]) continue;
        if (first < 0) first = p.type[a];
        else if (p.type[a] != first) differ = true;
    }
    p.common = differ ? -1 : first >= 0 ? first : DiffTypeOf(diffType, kindRead, kind);
    return p;
}
