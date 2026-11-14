// winch_out.h: the winch rope let out on a held key (BRIEF_winch_out_probe.md with the Reviewer's addendum of
// 2026-11-10; review\research_winch_payout.md). It began as a probe behind Probe=1. Since BRIEF_jobs_1114.md, part 163,
// the key works with its binding alone: the rate is a setting of every player, the rope stops at the winch's own
// length, and the step mode and the lines of each hold stay the probe's.
//
// The game makes the rope's constraint anew on every step while a winch is active. Its own function for a rope of a
// given length does the same once: L = max(min(span, length now), length asked), capped at the larger of the winch's
// own length and 50 m. So in the driven truck's own update, while the key is held, the probe reads the rope's length by
// the game's function and asks for that length plus rate x step. Nothing latches: with no call the rope stays as it is,
// and the game's own pull closes the slack by itself.
//
// The mod's thread keeps the job (WinchOutPost). The truck update's frame does the work (WinchOutFrame): no log, no
// heap, no lock. Every end of a hold is "no more calls": the frame makes none once the mod's last pass with the key
// held is older than kWinchFreshMs.

#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "truck.h"

static const float kWinchRateRange[2] = { 0.1f, 2.0f }, kWinchRateDefault = 0.5f; // WinchOutRate, metres a second
static const float kWinchStepRange[2] = { 0.0f, 2.0f };                           // WinchOutStep, metres a press; 0: the hold pays out at the rate
static const float kWinchDtMost = 0.05f;              // s: the step's time is held to this
static const unsigned long long kWinchFreshMs = 100;  // the mod's last pass with the key held may be this old
// The vehicle's fields the frame reads through the peek. + 0x8C8 and + 0x8C9 stand side by side in the game's own test
// (game_code.h, WinchPlaceFind); the others are the three known builds' offsets.
static const uint64_t kWinchRopeAt = 0x8B0;   // the truck's own rope constraint, null with none
static const uint64_t kWinchPullAt = 0x8C8;   // the game's pull is on
static const uint64_t kWinchActiveAt = 0x8C9; // the game's second active byte
static const uint64_t kWinchOwnAt = 0x8F0;    // the winch's own length, metres
static const uint64_t kWinchCraneAt = 0x904;  // the winch is a crane's: the game's own control pays out there

// ---- the drawn wire's sag, a probe (BRIEF_jobs_1114.md, part 164, and the Reviewer's addendum of 2026-11-14 00:21;
// review\research_winch_slack.md) ----
// The wire on screen is its own object at [vehicle + 0x8D0], 0x80 bytes, made anew inside every call of the set at
// the span of that moment, so it is drawn straight however long the limit is. The probe, behind Probe=1 and the ini
// key WinchSag (1 or 2), on the one build its offsets are from: right after the set's call, on the game's thread,
// the link's length (rope + 0x30) is written longer by the limit over the length the rope was made with (rope + 0x5C),
// 1.5 times at the most. With WinchSag=2 the rope's length is written too. The count of links, every pointer and
// every other byte are never written. The pointer is read fresh after each call and never kept.
static const uint32_t kWinchSagStamp = 0x6adb8627; // the game build the two offsets are from; on any other the key does nothing
static const uint64_t kWinchDrawnAt = 0x8D0;       // the vehicle's drawn rope, null with none
enum { kDrawnSize = 0x80, kDrawnLinkAt = 0x30, kDrawnLengthAt = 0x5C };
static_assert(kDrawnLinkAt + 4 <= kDrawnSize && kDrawnLengthAt + 4 <= kDrawnSize, "the probe's two writes stay inside the drawn rope's 0x80 bytes");
static const float kSagLengthLeast = 0.05f; // m: a drawn rope is at least this long
static const float kSagOver = 0.03f;        // the drawn rope's length may pass the limit read back by this share at the most
static const float kSagWhole = 0.01f;       // its length over a link's is a whole number to within this
static const float kSagFactorMost = 1.5f;   // the probe makes a link this many times longer at the most
static const int kSagLinksMost = 4096;      // more links than this is no rope (a wide bound until the first log fixes one)
// What a check says. kSagWrite: write; kSagNone: nothing to write, the limit is not above the rope's length; every
// other one is a failed check and ends the probe's writes for the session.
enum { kSagWrite, kSagNone, kSagNoRope, kSagNoRead, kSagLimit, kSagLength, kSagOverLimit, kSagLink, kSagCount, kSagNoWrite, kSagReasons };
inline const char *WinchSagWhy(int why)
{
    static const char *const names[kSagReasons] = { "a write", "nothing to write", "the drawn rope is not there", "a read failed", "the limit read back is no length",
                                                    "the rope's length is no number or out of range", "the rope's length is over the limit read back",
                                                    "the link's length is no number or not above 0", "the rope's length over a link's is no whole count of links",
                                                    "a write failed" };
    return why >= 0 && why < kSagReasons ? names[why] : "?";
}
// Whether the probe may write: both keys, and the one build (musts 4, 7 and 9).
inline int WinchSagMode(bool probe, int key, uint32_t stamp) { return probe && stamp == kWinchSagStamp && (key == 1 || key == 2) ? key : 0; }
struct WinchSagPlan
{
    int why;          // kSagWrite, or why not
    float link;       // what goes to rope + 0x30
    float length;     // what goes to rope + 0x5C where writeLength
    bool writeLength; // mode 2
    float factor;     // the link's length times this, 1 to kSagFactorMost
    int links;        // the rope's links as its two floats give them
    float linkWas, lengthWas;
};
// The check, pure: the rope's 0x80 bytes as read in one go (null: none), the limit the game's read gave after the
// set, the set's cap and the mode. The rope's length must be a number from 0.05 m to the cap and not above the limit
// by more than 3 percent: a chain made at the span is never longer than the limit, and it is shorter by the slack
// itself. A link's length must be above 0 and the length over it a whole count from 2 to kSagLinksMost.
inline WinchSagPlan WinchSagCheck(const uint8_t *rope, float limit, float cap, int mode)
{
    WinchSagPlan p = { kSagNoRope, 0.0f, 0.0f, false, 1.0f, 0, NAN, NAN };
    if (!rope) return p;
    memcpy(&p.linkWas, rope + kDrawnLinkAt, 4);
    memcpy(&p.lengthWas, rope + kDrawnLengthAt, 4);
    if (!isfinite(limit) || !(limit >= kSagLengthLeast) || !isfinite(cap) || !(limit <= cap)) return p.why = kSagLimit, p;
    if (!isfinite(p.lengthWas) || !(p.lengthWas >= kSagLengthLeast) || !(p.lengthWas <= cap)) return p.why = kSagLength, p;
    if (p.lengthWas > limit * (1.0f + kSagOver)) return p.why = kSagOverLimit, p;
    if (!isfinite(p.linkWas) || !(p.linkWas > 0.0f)) return p.why = kSagLink, p;
    const float count = p.lengthWas / p.linkWas, whole = roundf(count);
    if (!isfinite(count) || fabsf(count - whole) > kSagWhole || whole < 2.0f || whole > (float)kSagLinksMost) return p.why = kSagCount, p;
    p.links = (int)whole;
    float factor = limit / p.lengthWas;
    if (!(factor > 1.0f)) return p.why = kSagNone, p;
    if (factor > kSagFactorMost) factor = kSagFactorMost;
    p.factor = factor;
    p.link = p.linkWas * factor;
    p.length = p.lengthWas * factor; // the limit itself, but where the factor was held: the links times the count stay the length
    p.writeLength = mode == 2;
    p.why = mode == 1 || mode == 2 ? kSagWrite : kSagNone;
    return p;
}

inline LONG WinchBits(float f)
{
    LONG b;
    memcpy(&b, &f, 4);
    return b;
}
inline float WinchFloat(LONG b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

struct WinchOutJob
{
    // the mod's thread writes these
    volatile LONG64 vehicle, stamp; // the driven truck (0: the probe rests), GetTickCount64 at the last pass with the key held or pressed
    volatile LONG held, press, hold; // the key is held; a press posted for the step mode, taken once; the holds so far
    volatile LONG rateBits, stepBits;
    // the frame writes these, on the game's thread
    volatile LONG fault;            // 1 the read faulted, 2 the set did: no more calls in this session
    volatile LONG frames, calls;    // this truck's frames with the key held, and the calls made
    volatile LONG skipPull, skipActive, noRope, noLength, crane, stale, zeroStep; // the frames with no call, each by its reason
    volatile LONG shrunk;           // a read more than 1 mm under what the frame before asked: that call was no step
    volatile LONG refused;          // presses of the step mode that made no call
    volatile LONG noOwn, atEnd;     // no call: the winch's own length not read or not above 0; the ask would pass it
    volatile LONG askedBits, stepSumBits; // sums: the metres asked over the length read, the seconds of the steps
    volatile LONG lengthBits, firstBits;  // the last length read; the first of this hold, no number until there is one
    volatile LONG pullBits;               // the length read under the game's pull (read only, no call), no number until there is one
    LONG holdSeen;                  // the frame's own
    float lastAsked;
    // the drawn wire's sag (the probe of part 164). The mod's thread writes sagMode (WinchSagMode); the rest is the frame's
    volatile LONG sagMode;
    volatile LONG sagStop;          // a failed check, read or write, by its reason: no more writes in this session
    volatile LONG sagWrites, sagNone; // the frames with a write, and with nothing to write
    volatile LONG sagKept, sagBack; // the next frame found the link's length as written, or not (the game put it back or made the rope anew)
    volatile LONG sagLinkWas, sagLengthWas, sagLinkNow, sagLengthNow; // the two floats before the last write and read back after it
    volatile LONG sagLinkNext, sagLengthNext; // the two floats as the next frame found them, before its own call
    volatile LONG sagLimitBits, sagFactorBits, sagLinks;
    volatile LONG sagDumpHold;      // the hold whose first write's 0x80 bytes are in sagDump (set after the copy)
    uint8_t sagDump[kDrawnSize];
    float sagWrote;                 // the frame's own: the link's length it wrote last, 0 none in this hold
};
// The game's two functions behind the caller's fault guards: the read gives no number where it faulted, the set false.
typedef float (*WinchGetFn)(uint64_t vehicle);
typedef bool (*WinchSetFn)(uint64_t vehicle, float length);
// Before the frame's call of the set: what the frame before wrote, as the game left it. Reads only.
inline void WinchSagBefore(WinchOutJob &j, const TruckMemory &m, uint64_t vehicle)
{
    if (!j.sagMode || j.sagStop || !(j.sagWrote > 0.0f)) return;
    uint64_t rope = 0;
    uint8_t bytes[kDrawnSize];
    if (!m.get(vehicle + kWinchDrawnAt, rope) || (rope && (rope < 0x10000 || !m.peek(rope, bytes, sizeof bytes))))
    {
        InterlockedExchange(&j.sagStop, kSagNoRead);
        return;
    }
    float link = NAN, length = NAN;
    if (rope) memcpy(&link, bytes + kDrawnLinkAt, 4), memcpy(&length, bytes + kDrawnLengthAt, 4);
    InterlockedExchange(&j.sagLinkNext, WinchBits(link));
    InterlockedExchange(&j.sagLengthNext, WinchBits(length));
    InterlockedIncrement(rope && link == j.sagWrote ? &j.sagKept : &j.sagBack);
    j.sagWrote = 0.0f;
}
// Right after the set's call, on the game's thread: the limit read back, the rope's pointer read fresh, its 0x80
// bytes read in one go and checked, then the write or writes and both floats read back. No log, no heap, no lock.
// False where the game's read faulted (the caller ends Winch out for the session).
inline bool WinchSagFrame(WinchOutJob &j, const TruckMemory &m, uint64_t vehicle, float own, WinchGetFn get)
{
    const int mode = j.sagMode;
    if ((mode != 1 && mode != 2) || j.sagStop) return true;
    const float limit = get(vehicle);
    if (!isfinite(limit)) return false;
    uint64_t rope = 0;
    uint8_t bytes[kDrawnSize];
    if (!m.get(vehicle + kWinchDrawnAt, rope) || (rope && (rope < 0x10000 || !m.peek(rope, bytes, sizeof bytes))))
    {
        InterlockedExchange(&j.sagStop, kSagNoRead);
        return true;
    }
    const WinchSagPlan p = WinchSagCheck(rope ? bytes : nullptr, limit, own > 50.0f ? own : 50.0f, mode);
    InterlockedExchange(&j.sagLimitBits, WinchBits(limit));
    if (p.why == kSagNone)
    {
        InterlockedIncrement(&j.sagNone);
        return true;
    }
    if (p.why != kSagWrite)
    {
        InterlockedExchange(&j.sagStop, p.why);
        return true;
    }
    if (j.sagDumpHold != j.hold)
    {
        memcpy(j.sagDump, bytes, sizeof bytes);
        InterlockedExchange(&j.sagDumpHold, j.hold);
    }
    InterlockedExchange(&j.sagLinkWas, WinchBits(p.linkWas));
    InterlockedExchange(&j.sagLengthWas, WinchBits(p.lengthWas));
    InterlockedExchange(&j.sagFactorBits, WinchBits(p.factor));
    InterlockedExchange(&j.sagLinks, p.links);
    if (!m.put(rope + kDrawnLinkAt, p.link) || (p.writeLength && !m.put(rope + kDrawnLengthAt, p.length)))
    {
        InterlockedExchange(&j.sagStop, kSagNoWrite);
        return true;
    }
    float link = NAN, length = NAN;
    if (!m.get(rope + kDrawnLinkAt, link) || !m.get(rope + kDrawnLengthAt, length))
    {
        InterlockedExchange(&j.sagStop, kSagNoRead);
        return true;
    }
    InterlockedExchange(&j.sagLinkNow, WinchBits(link));
    InterlockedExchange(&j.sagLengthNow, WinchBits(length));
    InterlockedIncrement(&j.sagWrites);
    j.sagWrote = p.link;
    return true;
}

// On the mod's thread, each pass. on: the key may act (bound, both functions found, the truck update hooked, no
// fault). step: above 0 only with Probe=1. vehicle: the driven truck's, 0 none. held and pressed: the key on this pass, false wherever the keys are not
// heard (the menu, a capture, the game not in front).
inline void WinchOutPost(WinchOutJob &j, bool on, uint64_t vehicle, bool held, bool pressed, float rate, float step, unsigned long long now, int sag = 0)
{
    if (!on || !vehicle) held = pressed = false, vehicle = 0;
    InterlockedExchange(&j.sagMode, sag); // WinchSagMode's: 0 unless Probe=1, the key and the one build
    InterlockedExchange(&j.rateBits, WinchBits(rate));
    InterlockedExchange(&j.stepBits, WinchBits(step));
    InterlockedExchange64(&j.vehicle, (LONG64)vehicle);
    if (held || pressed) InterlockedExchange64(&j.stamp, (LONG64)now);
    if (held && !j.held) InterlockedIncrement(&j.hold);
    InterlockedExchange(&j.held, held ? 1 : 0);
    if (pressed && step > 0.0f) InterlockedExchange(&j.press, 1);
}


// On the game's thread, every frame of its truck update, before the update: first is the update's first argument (the
// vehicle at + 0x20), dt its step in seconds. True where a call was made.
//   The gate (M1): the driven truck's own rope, [vehicle + 0x8B0] not null, and a length above 0. The read alone is
//   wrong at the far end of another truck's rope (it gives 0) and on a coupled truck (it gives that truck's rope).
//   No call while the game's pull or its second active byte is on (M2): the game makes the rope anew itself then.
//   No call on a crane's winch (S6), at a step of 0 (a pause) or with a stale stamp.
//   The stop (163, must 1): no call where the ask would pass the winch's own length (vehicle + 0x8F0), and none where
//   that length is not read or not above 0. The game's own cap is 50 m or more, so without this the rope has no end.
//   The step mode (step above 0): one call a press, the press taken once whether it is refused or not.
inline bool WinchOutFrame(WinchOutJob &j, const TruckMemory &m, uint64_t first, float dt, WinchGetFn get, WinchSetFn set, unsigned long long now)
{
    if (j.fault) return false;
    const uint64_t want = (uint64_t)j.vehicle;
    const bool press = j.press != 0;
    if (!want || (!j.held && !press)) return false;
    uint64_t vehicle = 0;
    if (!m.get(first + 0x20, vehicle) || vehicle != want) return false;
    const float step = WinchFloat(j.stepBits), rate = WinchFloat(j.rateBits);
    const bool byStep = step > 0.0f;
    if (byStep && !press) return false;
    if (press) InterlockedExchange(&j.press, 0);
    InterlockedIncrement(&j.frames);
    if (j.holdSeen != j.hold)
    {
        j.holdSeen = j.hold;
        j.lastAsked = 0.0f;
        j.sagWrote = 0.0f;
        InterlockedExchange(&j.firstBits, WinchBits(NAN));
        InterlockedExchange(&j.pullBits, WinchBits(NAN));
    }
    auto none = [&](volatile LONG &count) {
        InterlockedIncrement(&count);
        if (byStep) InterlockedIncrement(&j.refused);
        j.lastAsked = 0.0f;
        return false;
    };
    const unsigned long long stamp = (unsigned long long)j.stamp;
    if (now < stamp || now - stamp > kWinchFreshMs) return none(j.stale);
    float d = dt;
    if (!(d > 0.0f) || !isfinite(d)) return none(j.zeroStep);
    if (d > kWinchDtMost) d = kWinchDtMost;
    uint64_t rope = 0;
    uint8_t pull = 0, active = 0, crane = 0;
    if (!m.get(vehicle + kWinchCraneAt, crane) || !m.get(vehicle + kWinchRopeAt, rope) || !m.get(vehicle + kWinchPullAt, pull) || !m.get(vehicle + kWinchActiveAt, active)) return none(j.noRope);
    if (crane) return none(j.crane);
    if (!rope) return none(j.noRope);
    if (pull)
    {
        // read only, for the closing speed in the probe's line (BRIEF_winch_pull_120.md, S1): no set, no call counted
        const float under = get(vehicle);
        if (isfinite(under) && under > 0.0f) InterlockedExchange(&j.pullBits, WinchBits(under));
        return none(j.skipPull);
    }
    if (active) return none(j.skipActive);
    const float length = get(vehicle);
    if (!isfinite(length))
    {
        InterlockedExchange(&j.fault, 1);
        return false;
    }
    if (!(length > 0.0f)) return none(j.noLength);
    if (j.lastAsked > 0.0f && length < j.lastAsked - 0.001f) InterlockedIncrement(&j.shrunk);
    if (!isfinite(WinchFloat(j.firstBits))) InterlockedExchange(&j.firstBits, WinchBits(length));
    InterlockedExchange(&j.lengthBits, WinchBits(length));
    float own = 0.0f;
    if (!m.get(vehicle + kWinchOwnAt, own) || !isfinite(own) || !(own > 0.0f)) return none(j.noOwn);
    const float ask = length + (byStep ? step : rate * d); // never under the length read: the set can shorten a rope (N7)
    if (ask > own) return none(j.atEnd);
    WinchSagBefore(j, m, vehicle);
    if (!set(vehicle, ask))
    {
        InterlockedExchange(&j.fault, 2);
        return false;
    }
    if (!WinchSagFrame(j, m, vehicle, own, get))
    {
        InterlockedExchange(&j.fault, 1);
        return false;
    }
    InterlockedIncrement(&j.calls);
    InterlockedExchange(&j.askedBits, WinchBits(WinchFloat(j.askedBits) + (ask - length)));
    InterlockedExchange(&j.stepSumBits, WinchBits(WinchFloat(j.stepSumBits) + d));
    j.lastAsked = ask;
    return true;
}

// The closing speed under a pull from two lengths read seconds apart, m/s, above 0 while the rope shortens; no number
// where either length is none or no time passed.
inline float WinchClosing(float before, float now, float seconds)
{
    if (!isfinite(before) || !isfinite(now) || !(before > 0.0f) || !(now > 0.0f) || !(seconds > 0.0f)) return NAN;
    return (before - now) / seconds;
}

// Whether Winch out asks for the truck update's hook (gearbox_hooks.h, GearboxHooksWanted): the action bound, and
// nothing else. It has no default binding, so nothing more runs for a player who binds nothing (163, must 2).
inline bool WinchOutWanted(bool bound) { return bound; }
