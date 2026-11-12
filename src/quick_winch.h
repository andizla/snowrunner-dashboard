// quick_winch.h: the game's quick winch only while the truck stands, or never (BRIEF_quick_winch.md part B with the
// addenda; review\research_quick_winch.md).
//
// The game's quick winch key ends in one small method that only raises a request on the player's controller; the
// player's truck update makes the winch a frame later. So a refusal is a return without the call: nothing is raised,
// nothing is pending, nothing goes to co-op. The method is found by QuickWinchFind (game_code.h) and hooked alone.
//
// The mod's thread posts the driven truck, its speed and the setting once a pass (QuickWinchPost). The hook on the
// game's thread reads only those (QuickWinchHookRule): no lock, no game memory, no log. A press for another truck
// than the driven one goes through, and so does every press while the mod's last pass is older than kQuickWinchFreshMs.

#pragma once
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "gearbox_model.h" // kStandStill

enum { kQuickWinchGame, kQuickWinchStanding, kQuickWinchOff, kQuickWinchCount }; // GaugeSettings::quickWinch
static const unsigned long long kQuickWinchFreshMs = 250; // the mod's last pass may be this old; older, nothing is refused

// The rule. mode: the setting; fresh: the mod's last pass is young enough; driven: the press is for the mod's driven
// truck; speed: that truck's, metres a second, below 0 backwards. Standing is |speed| under kStandStill. A speed that
// is no number refuses nothing under Standing only.
inline bool QuickWinchRefuse(int mode, bool fresh, bool driven, float speed)
{
    if (!fresh || !driven) return false;
    if (mode == kQuickWinchOff) return true;
    return mode == kQuickWinchStanding && isfinite(speed) && fabsf(speed) >= kStandStill;
}

struct QuickWinchState
{
    // the mod's thread writes these, once a pass
    volatile LONG64 vehicle, stamp; // the driven truck (0 none), GetTickCount64 at that pass
    volatile LONG mode, speedBits;
    // the hook writes these, on the game's thread
    volatile LONG refused, passed;  // the presses refused and let through
    volatile LONG refusedSpeedBits, refusedMode; // of the last press refused, for the mod's line
    volatile LONG fault;            // the hook's own code faulted: it only passes on from then
};

inline LONG QuickWinchBits(float f)
{
    LONG b;
    memcpy(&b, &f, 4);
    return b;
}
inline float QuickWinchFloat(LONG b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

// On the mod's thread, each pass with a truck read. vehicle: the driven truck's, 0 where none is driven.
inline void QuickWinchPost(QuickWinchState &q, uint64_t vehicle, float speed, int mode, unsigned long long now)
{
    InterlockedExchange(&q.mode, mode);
    InterlockedExchange(&q.speedBits, QuickWinchBits(speed));
    InterlockedExchange64(&q.vehicle, (LONG64)vehicle);
    InterlockedExchange64(&q.stamp, (LONG64)now);
}

// On the game's thread, in the hook. vehicle: the method's argument. True where the press is refused: the hook then
// returns without the call.
inline bool QuickWinchHookRule(QuickWinchState &q, uint64_t vehicle, unsigned long long now)
{
    const LONG mode = q.mode;
    const uint64_t driven = (uint64_t)q.vehicle;
    const unsigned long long stamp = (unsigned long long)q.stamp;
    const float speed = QuickWinchFloat(q.speedBits);
    const bool fresh = stamp && now >= stamp && now - stamp <= kQuickWinchFreshMs;
    if (!QuickWinchRefuse(mode, fresh, driven && vehicle == driven, speed))
    {
        InterlockedIncrement(&q.passed);
        return false;
    }
    InterlockedExchange(&q.refusedSpeedBits, QuickWinchBits(speed));
    InterlockedExchange(&q.refusedMode, mode);
    InterlockedIncrement(&q.refused);
    return true;
}

// The line a refusal writes, at most one a second (the mod's thread): "quick winch: refused, the truck moves at 3.1
// km/h (Standing only)", or "quick winch: refused (Off)"; with the presses where more than one was refused since.
inline void QuickWinchLine(char *out, size_t size, int mode, float speed, long presses)
{
    char more[40] = "";
    if (presses > 1) _snprintf_s(more, sizeof more, _TRUNCATE, ", %ld presses", presses);
    if (mode == kQuickWinchOff) _snprintf_s(out, size, _TRUNCATE, "quick winch: refused (Off)%s", more);
    else _snprintf_s(out, size, _TRUNCATE, "quick winch: refused, the truck moves at %.1f km/h (Standing only)%s", fabsf(speed) * 3.6f, more);
}
