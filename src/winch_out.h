// winch_out.h: the winch rope let out on a held key, a probe that works with Probe=1 alone (BRIEF_winch_out_probe.md
// with the Reviewer's addendum of 2026-11-10; review\research_winch_payout.md).
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
    volatile LONG askedBits, stepSumBits; // sums: the metres asked over the length read, the seconds of the steps
    volatile LONG lengthBits, firstBits;  // the last length read; the first of this hold, no number until there is one
    LONG holdSeen;                  // the frame's own
    float lastAsked;
};

// On the mod's thread, each pass. on: the probe may act (Probe=1, both functions found, the truck update hooked, no
// fault). vehicle: the driven truck's, 0 none. held and pressed: the key on this pass, false wherever the keys are not
// heard (the menu, a capture, the game not in front).
inline void WinchOutPost(WinchOutJob &j, bool on, uint64_t vehicle, bool held, bool pressed, float rate, float step, unsigned long long now)
{
    if (!on || !vehicle) held = pressed = false, vehicle = 0;
    InterlockedExchange(&j.rateBits, WinchBits(rate));
    InterlockedExchange(&j.stepBits, WinchBits(step));
    InterlockedExchange64(&j.vehicle, (LONG64)vehicle);
    if (held || pressed) InterlockedExchange64(&j.stamp, (LONG64)now);
    if (held && !j.held) InterlockedIncrement(&j.hold);
    InterlockedExchange(&j.held, held ? 1 : 0);
    if (pressed && step > 0.0f) InterlockedExchange(&j.press, 1);
}

// The game's two functions behind the caller's fault guards: the read gives no number where it faulted, the set false.
typedef float (*WinchGetFn)(uint64_t vehicle);
typedef bool (*WinchSetFn)(uint64_t vehicle, float length);

// On the game's thread, every frame of its truck update, before the update: first is the update's first argument (the
// vehicle at + 0x20), dt its step in seconds. True where a call was made.
//   The gate (M1): the driven truck's own rope, [vehicle + 0x8B0] not null, and a length above 0. The read alone is
//   wrong at the far end of another truck's rope (it gives 0) and on a coupled truck (it gives that truck's rope).
//   No call while the game's pull or its second active byte is on (M2): the game makes the rope anew itself then.
//   No call on a crane's winch (S6), at a step of 0 (a pause) or with a stale stamp.
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
        InterlockedExchange(&j.firstBits, WinchBits(NAN));
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
    if (pull) return none(j.skipPull);
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
    const float ask = length + (byStep ? step : rate * d); // never under the length read: the set can shorten a rope (N7)
    if (!set(vehicle, ask))
    {
        InterlockedExchange(&j.fault, 2);
        return false;
    }
    InterlockedIncrement(&j.calls);
    InterlockedExchange(&j.askedBits, WinchBits(WinchFloat(j.askedBits) + (ask - length)));
    InterlockedExchange(&j.stepSumBits, WinchBits(WinchFloat(j.stepSumBits) + d));
    j.lastAsked = ask;
    return true;
}

// Whether the probe asks for the truck update's hook (gearbox_hooks.h, GearboxHooksWanted): Probe=1 with the action bound.
inline bool WinchOutWanted(bool probe, bool bound) { return probe && bound; }
