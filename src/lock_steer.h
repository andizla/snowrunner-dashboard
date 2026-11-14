// lock_steer.h: a locked differential limits the steering (BRIEF_lock_steering.md with the Reviewer's addendum and the
// change of 2026-11-12 21:31; review\research_lock_steering.md).
//
// The game steers each wheel by one product a step: the wheel's most angle (wheel + 0xB8, radians, set once when the
// truck is loaded) times the truck's steer value. While an axle is locked the mod writes each steered wheel's most
// angle as its stock figure times one share, the same for every wheel of the truck, so the wheels keep their angles to
// each other. With the stick at the middle the product is 0 at any share, so nothing moves by itself. Never the steer
// value (the game's script adds to it each frame), never the pace of the turn, and never a figure over the stock one.
//
// Nothing here reads the game by itself: the mod's thread hands in its TruckMemory, the tests a made-up one.

#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "drive_display.h"
#include "truck.h"

static const int kLockSteerLeast = 30, kLockSteerFrontDefault = 60, kLockSteerRearDefault = 85; // LockSteerFront, LockSteerRear: percent of the steering left
static const float kLockSteerPace = 0.8f;     // the share in force moves toward its target by this much a second: 100 to 60 in half a second
static const float kLockSteerPassMost = 0.1f; // s: a pass counts as this long at the most
static const uint64_t kSteerMostAt = 0xB8;    // wheel + 0xB8: the wheel's most steering angle, a float in radians
static const float kSteerSteers = 0.01f;      // rad: an axle steers where one of its wheels has a stock most angle over this, by its size
static const float kSteerSaneUnder = 1.5707964f; // a most angle is under a quarter turn by its size, or it is not one. Its sign is the
                                                 // wheel's own: a steered rear axle turns against the front and holds a negative one
                                                 // (BRIEF_drive_1114_findings.md, part 175, point 3: -0.1745 rad on an 8x8 closed the record)

// On at all (M5): with Powertrain, and one of the two sliders under 100. Off, nothing is read and nothing is written.
inline bool LockSteerOn(bool powertrain, int front, int rear) { return powertrain && (front < 100 || rear < 100); }

// ---- the target share (M2, as changed by the user's ruling) ----
struct LockSteerIn
{
    AxleRows rows;               // the truck's axles; axles 0: its axles are not read as pairs
    AxlePlan plan;               // each axle's type in force, and whether it is driven now
    bool steers[kAxleRowsMost];  // the axle steers, by its wheels' stock most angle
    bool gameLock;               // the lock byte in force: set, and the handbrake not (the mod's own byte counts too)
    bool alwaysOn;               // the truck's lock kind is always on
    bool cutActs;                // the torque cut acts: only then do a row and a lever count
    bool tied;                   // a Locked axle beside another type is tied: Diff lock bite above 0 and the lock's slot published
    int front, rear;             // the two sliders, percent
};
// 1: nothing limits. A steered axle locked gives the first slider, another driven axle locked the second, both the
// smaller. Locked by the game (the byte in force, or a lock that is always on): every axle driven now, wherever the
// truck is driven. Locked by a row or a lever: the axle's type in force, while the cut acts and the axle is driven
// now; where the driven axles differ, only while something ties a Locked one ("Locked, no bite" costs no steering).
// A truck with no rows: the game's lock gives the smaller of the two.
inline float LockSteerTarget(const LockSteerIn &in)
{
    auto share = [](int percent) { return (float)(percent < kLockSteerLeast ? kLockSteerLeast : percent > 100 ? 100 : percent) / 100.0f; };
    const float front = share(in.front), rear = share(in.rear), both = front < rear ? front : rear;
    const bool game = in.gameLock || in.alwaysOn;
    if (in.rows.axles <= 0) return game ? both : 1.0f;
    const bool differ = in.plan.common < 0;
    bool steered = false, other = false;
    for (int a = 0; a < in.rows.axles && a < kAxleRowsMost; a++)
    {
        if (!in.plan.now[a]) continue;
        const bool byRow = in.cutActs && in.plan.type[a] == kDiffLocked && (!differ || in.tied);
        if (!game && !byRow) continue;
        if (in.steers[a]) steered = true;
        else other = true;
    }
    return steered && other ? both : steered ? front : other ? rear : 1.0f;
}

// ---- the ease (M3) ----
// The share in force one pass on: toward the target at kLockSteerPace a second, a pass's seconds held to
// kLockSteerPassMost. A slider moved in the drive goes through it too.
inline float LockSteerEase(float share, float target, float dt)
{
    if (!(dt > 0.0f) || !isfinite(dt)) return share;
    const float step = kLockSteerPace * (dt > kLockSteerPassMost ? kLockSteerPassMost : dt);
    if (target > share) return share + step < target ? share + step : target;
    return share - step > target ? share - step : target;
}

// ---- the stock figures: kept, checked, never compounded (M4), and put back (M5) ----
// One record, for the truck driven: each wheel's pointer at its place in the truck's list, its stock most angle and
// the mod's last write. A truck is known by its id where that is read, else by its vehicle.
struct LockSteerRec
{
    bool known;
    uint64_t vehicle;
    char id[40];
    char kind[96];   // the truck's kind as the game's files name it, "" where it is not read: what a figure let go is kept and met by
    int n;
    uint64_t wheel[kDdWheels];
    float stock[kDdWheels], last[kDdWheels], read[kDdWheels]; // read: what the wheel held at this pass's look
    bool has[kDdWheels];
    bool closed;     // a figure was not the mod's: nothing is written to this truck again until it is another truck
    int closedAt;    // the place of the wheel that closed it, -1 a read that failed nowhere in particular
    float closedRead;
    float share;     // the share in force, 1 with nothing limited
    long writes, backs;
    long mended;     // figures met again after they were let go (LockSteerKept), and the last of them
    float mendedRead, mendedStock;
};
inline bool LockSteerSame(float a, float b) { return !memcmp(&a, &b, sizeof a); } // to the bit

// ---- the figures let go (review_lock_steering_151.md, M1) ----
// A record is thrown away when the truck is left and when another truck is driven. Its wheels get their stock figure
// back first, but only through the pointers the record holds. Where the game has moved or rebuilt the wheels by then,
// nothing is put back, and the wheels turn up at a new place holding stock times the share. Taken as a stock figure,
// that would be lowered again: 60 percent, then 36, then 22.
// So every wheel let go with the mod's figure still in it leaves a pair here: its stock figure and the mod's last
// write, with the truck's kind. The memory outlives every record, for the session: kLockSteerKeptMost pairs, the oldest
// dropped. A figure about to be taken as a stock figure is looked up among the last writes first, to the bit, for the
// same kind of truck (or none on both sides). Found: it is the mod's own figure on a moved wheel, and the pair is spent.
// By the kind and not by the truck's id (review_lock_steering_151_fix.md, S1): the game hands out a new id at a
// recovery, and two trucks of one kind have the same stock angles, so a figure of the mod's on one is never another's
// true stock. A truck of another kind is never met: its true stock could have the same bits as a figure let go.
enum { kLockSteerKeptMost = 64 };
struct LockSteerKept
{
    int n;
    float stock[kLockSteerKeptMost], last[kLockSteerKeptMost];
    char kind[kLockSteerKeptMost][96];
    long kept, met; // pairs kept and pairs met again, over the session
};
inline LockSteerKept &LockSteerMemory()
{
    static LockSteerKept kept = {};
    return kept;
}
inline void LockSteerKeep(LockSteerKept &k, const char *kind, float stock, float last)
{
    if (k.n >= kLockSteerKeptMost)
    {
        memmove(k.stock, k.stock + 1, sizeof(float) * (kLockSteerKeptMost - 1));
        memmove(k.last, k.last + 1, sizeof(float) * (kLockSteerKeptMost - 1));
        memmove(k.kind, k.kind + 1, sizeof k.kind[0] * (kLockSteerKeptMost - 1));
        k.n = kLockSteerKeptMost - 1;
    }
    k.stock[k.n] = stock, k.last[k.n] = last;
    strncpy_s(k.kind[k.n], kind, _TRUNCATE);
    k.n++;
    k.kept++;
}
// The stock figure of the newest pair whose last write is `figure` to the bit, for the kind of truck; the pair is spent.
inline bool LockSteerMeet(LockSteerKept &k, const char *kind, float figure, float &stock)
{
    for (int i = k.n - 1; i >= 0; i--)
    {
        if (!LockSteerSame(k.last[i], figure) || strcmp(k.kind[i], kind)) continue;
        stock = k.stock[i];
        memmove(k.stock + i, k.stock + i + 1, sizeof(float) * (size_t)(k.n - 1 - i));
        memmove(k.last + i, k.last + i + 1, sizeof(float) * (size_t)(k.n - 1 - i));
        memmove(k.kind + i, k.kind + i + 1, sizeof k.kind[0] * (size_t)(k.n - 1 - i));
        k.n--;
        k.met++;
        return true;
    }
    return false;
}

// The stock figure back into every wheel that still reads as the mod's own write. A figure that reads as anything
// else is left alone. The share in force is 1 after it. How many were written.
// A wheel that is not put back (it is not read, it reads as another figure than the stock one, or the write fails) is
// let go with the mod's figure in it, wherever the game has it now: its pair goes to the memory of the figures let go,
// and the record no longer claims it.
inline int LockSteerBack(LockSteerRec &r, const TruckMemory &m)
{
    int back = 0;
    for (int i = 0; i < r.n && i < kDdWheels; i++)
    {
        if (!r.has[i] || r.stock[i] == 0.0f || LockSteerSame(r.last[i], r.stock[i])) continue;
        float now = 0.0f;
        const bool read = m.get(r.wheel[i] + kSteerMostAt, now);
        if (read && LockSteerSame(now, r.last[i]) && m.put(r.wheel[i] + kSteerMostAt, r.stock[i]))
        {
            r.last[i] = r.stock[i], back++;
            continue;
        }
        if (!(read && LockSteerSame(now, r.stock[i]))) LockSteerKeep(LockSteerMemory(), r.kind, r.stock[i], r.last[i]);
        r.last[i] = r.stock[i];
    }
    r.backs += back;
    r.share = 1.0f;
    return back;
}

// The record for this truck. The same truck (its id, or its vehicle where no id is read): kept, also where the vehicle
// is another, as a truck rebuilt in place is. Another truck: the old one's wheels put back, a new record. True where it
// is a new record.
inline bool LockSteerFor(LockSteerRec &r, const TruckMemory &m, uint64_t vehicle, const char *id, const char *kind = "")
{
    const bool same = r.known && (id[0] ? !strcmp(r.id, id) : !r.id[0] && r.vehicle == vehicle);
    if (same)
    {
        r.vehicle = vehicle;
        return false;
    }
    if (r.known) LockSteerBack(r, m);
    r = LockSteerRec{};
    r.known = true, r.vehicle = vehicle, r.share = 1.0f, r.closedAt = -1;
    strncpy_s(r.id, id, _TRUNCATE);
    strncpy_s(r.kind, kind, _TRUNCATE);
    return true;
}

// Forgets the truck: its wheels put back first. For a truck left, and for the feature turned off.
inline int LockSteerDrop(LockSteerRec &r, const TruckMemory &m)
{
    const int back = r.known ? LockSteerBack(r, m) : 0;
    r = LockSteerRec{};
    return back;
}

// One pass's look at the wheels, before any write. Each wheel's figure must be its stock one or the mod's last write,
// to the bit.
//   A place with no record: the figure is taken as the stock (0: a wheel that does not steer, never written).
//   A new pointer at a place: the mod's last write there, and that write not the stock, is the old wheel moved, and
//   the stock stays; the stock figure is a fresh wheel of the same kind.
//   Anything else while the share in force is 1 is taken as a new stock figure. Under 1 it is not the mod's: the
//   record is closed, the wheels that still read as the mod's are put back, and nothing more is written to this truck.
//   A figure about to be taken as a stock figure is first looked up among the figures let go (LockSteerMeet). Met: it
//   is the mod's own write on a moved wheel. The stock is the pair's, the last write is the figure read, and the share
//   in force is no more than figure over stock, so the next write goes on from there and never lowers it twice.
// A figure that is no most angle (a quarter turn or more by its size, no number) closes the record too. False: closed
// (now or before), or a wheel was not read (nothing is done in this pass).
inline bool LockSteerLook(LockSteerRec &r, const TruckMemory &m, const uint64_t *wheel, int n)
{
    if (!r.known || r.closed) return false;
    if (n < 0) n = 0;
    if (n > kDdWheels) n = kDdWheels;
    float now[kDdWheels];
    for (int i = 0; i < n; i++)
        if (!m.get(wheel[i] + kSteerMostAt, now[i])) return false;
    auto sane = [](float a) { return a == 0.0f || (fabsf(a) > 0.0f && fabsf(a) < kSteerSaneUnder); };
    int bad = -1;
    const bool unlimited = r.share >= 1.0f; // as this look began: a figure met again lowers the share in the loop
    for (int i = 0; i < n && bad < 0; i++)
    {
        const bool fresh = !r.has[i] || i >= r.n;
        // a new pointer needs no case of its own: the mod's last write there is the old wheel moved (the stock stays),
        // the stock figure is a fresh wheel of the same kind, and anything else is judged as at the old pointer
        const bool mine = !fresh && LockSteerSame(now[i], r.last[i]), stock = !fresh && LockSteerSame(now[i], r.stock[i]);
        if (fresh || (!mine && !stock && unlimited))
        {
            // taken as the stock: a place with no record, or another figure while nothing is limited
            float kept = 0.0f;
            if (!sane(now[i])) bad = i;
            else if (now[i] != 0.0f && LockSteerMeet(LockSteerMemory(), r.kind, now[i], kept) && fabsf(kept) > fabsf(now[i]) && (kept > 0.0f) == (now[i] > 0.0f) &&
                     sane(kept))
            {
                // the mod's own figure on a wheel that moved after its record was thrown away
                r.has[i] = true, r.stock[i] = kept, r.last[i] = now[i];
                if (now[i] / kept < r.share) r.share = now[i] / kept;
                r.mended++, r.mendedRead = now[i], r.mendedStock = kept;
            }
            else r.has[i] = true, r.stock[i] = r.last[i] = now[i];
        }
        else if (stock) r.last[i] = r.stock[i];       // the stock figure stands there: a fresh wheel, or put back by another hand
        else if (!mine) bad = i;                      // not the mod's, while the share is in force
        r.wheel[i] = wheel[i];
        r.read[i] = now[i];
    }
    for (int i = n; i < kDdWheels; i++) r.has[i] = false;
    if (bad >= 0)
    {
        r.closedAt = bad, r.closedRead = now[bad];
        r.n = n > r.n ? n : r.n;
        r.has[bad] = false;
        LockSteerBack(r, m);
        r.closed = true;
        return false;
    }
    r.n = n;
    return true;
}

// Which axles steer, by the record's stock figures and the rows.
inline void LockSteerAxles(const LockSteerRec &r, const AxleRows &rows, bool *steers)
{
    for (int a = 0; a < kAxleRowsMost; a++) steers[a] = false;
    for (int i = 0; i < r.n && i < kDdWheels; i++)
        if (r.has[i] && fabsf(r.stock[i]) > kSteerSteers && rows.ofWheel[i] >= 0 && rows.ofWheel[i] < kAxleRowsMost) steers[rows.ofWheel[i]] = true;
}

// After a look that passed: the share eased one pass toward the target, and each steered wheel's most angle written as
// its stock figure times the share, where it does not hold that already. At a share of 1 it is the stock figure
// itself. Never over the stock by its size, and the sign is the stock's. How many wheels were written.
inline int LockSteerWrite(LockSteerRec &r, const TruckMemory &m, float target, float dt)
{
    if (!r.known || r.closed) return 0;
    if (!(target >= (float)kLockSteerLeast / 100.0f)) target = (float)kLockSteerLeast / 100.0f;
    if (target > 1.0f) target = 1.0f;
    r.share = LockSteerEase(r.share, target, dt);
    int wrote = 0;
    for (int i = 0; i < r.n && i < kDdWheels; i++)
    {
        if (!r.has[i] || r.stock[i] == 0.0f || !isfinite(r.stock[i])) continue;
        float want = r.share >= 1.0f ? r.stock[i] : r.stock[i] * r.share;
        if (!(fabsf(want) < fabsf(r.stock[i]))) want = r.stock[i];
        if (LockSteerSame(want, r.read[i])) continue;
        if (!m.put(r.wheel[i] + kSteerMostAt, want)) continue;
        r.last[i] = r.read[i] = want;
        wrote++;
    }
    r.writes += wrote;
    return wrote;
}

// The Drive display's and the tool's words: "steering 60 %" while the share in force is under 1, else nothing.
inline int LockSteerPercent(float share) { return share < 1.0f ? (int)floorf(share * 100.0f + 0.5f) : 0; }
