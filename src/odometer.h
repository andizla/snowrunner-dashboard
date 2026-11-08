// odometer.h: the count each truck keeps (BRIEF_odometer.md, PLANNED 8, with the Reviewer's addendum of 2026-11-08 21:33):
// metres driven and engine seconds, counted on the models' pass inside its gate (driving, not held, the same truck), so
// a held game, the map, the garage and a menu count nothing. Kept in [Odometer:<id>] of the kind's file by gauges.cpp;
// here only what a pass decides, so the tests can drive it by a made-up clock.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const float kOdoMoving = 0.5f;             // m/s: slower than this is standing (as kRangeMoving)
static const unsigned long long kOdoEvery = 60000; // ms: a write every minute while the count changed
static const unsigned long long kOdoStand = 2000;  // ms standing after moving: a stop
static const unsigned long long kOdoStopGap = 10000; // ms: stop writes at most this often (stop and go in mud)
static const double kOdoMostKm = 10000000.0, kOdoMostHours = 1000000.0; // Set to's range: 10 million km or miles, 1 million h
static const double kOdoMile = 1609.344;

// One pass's count on the models' pass: metres above kOdoMoving, and real seconds with the engine on.
inline void OdoCount(double &metres, double &seconds, float speed, float dt, bool engineOn)
{
    if (!(dt > 0.0f)) return;
    if (fabsf(speed) >= kOdoMoving) metres += fabsf(speed) * dt;
    if (engineOn) seconds += dt;
}

// The count of the driven truck on the mod's thread. Before the id has decided (the take-over, the match: the
// Reviewer's must 3) it is counted in pending and nothing is written; at the decision the figure loaded is the start
// and pending goes on top of it.
struct OdoState
{
    bool decided;               // the id has decided: the count is the truck's and may be written
    bool saves;                 // it has an id to write to (an id not read counts from 0 for the drive, never saved)
    bool loaded;                // a section was there at the decision
    double metres, seconds;     // the truck's count (decided), else what was counted since the change
    double wroteMetres, wroteSeconds; // as last written (or loaded)
    unsigned long long wroteAt; // when last written, or decided
    unsigned long long stopAt;  // when a stop last wrote
    bool moving;                // it moved on the last pass
    unsigned long long standSince; // when it stopped moving (0 moving, or never moved since the last write)
    bool stepping;              // the models stepped on the last pass
};

// Another truck (or none): the count starts over in memory, nothing decided.
inline void OdoChange(OdoState &o) { o = OdoState{}; }

// The decision for the id: the figure loaded (0 without a section), the count since the change on top of it. saves:
// the id was read. at: now.
inline void OdoDecide(OdoState &o, bool saves, bool loaded, double metres, double seconds, unsigned long long at)
{
    if (o.decided) return;
    o.decided = true;
    o.saves = saves;
    o.loaded = loaded;
    o.wroteMetres = metres, o.wroteSeconds = seconds;
    o.metres += metres, o.seconds += seconds;
    o.wroteAt = at;
}

// Set to from the tab: the count is this figure (metres, or seconds with hours), the other kept.
inline void OdoSet(OdoState &o, bool hours, double value)
{
    if (hours) o.seconds = value;
    else o.metres = value;
}

enum { kOdoNoWrite, kOdoMinute, kOdoStop, kOdoHeld };

// One pass: adds what the models counted and says whether to write now. moving: above kOdoMoving on this pass;
// stepping: the models stepped (a menu, the map or the garage hold them). A write only where decided, saving and
// changed since the last: every kOdoEvery, at a stop (moving, then standing kOdoStand, one at most every kOdoStopGap),
// and when the models stop stepping (a normal quit goes through the menu). The caller writes and calls OdoWritten.
inline int OdoPass(OdoState &o, double metres, double seconds, bool moving, bool stepping, unsigned long long now)
{
    o.metres += metres, o.seconds += seconds;
    const bool was = o.moving, stepped = o.stepping;
    if (stepping)
    {
        o.moving = moving;
        if (was && !moving) o.standSince = now;
        if (moving) o.standSince = 0;
    }
    o.stepping = stepping;
    const bool changed = o.metres != o.wroteMetres || o.seconds != o.wroteSeconds;
    if (!o.decided || !o.saves || !changed) return kOdoNoWrite;
    if (stepped && !stepping) return kOdoHeld;
    if (now - o.wroteAt >= kOdoEvery) return kOdoMinute;
    if (stepping && !moving && o.standSince && now - o.standSince >= kOdoStand && (!o.stopAt || now - o.stopAt >= kOdoStopGap))
    {
        o.stopAt = now;
        o.standSince = 0;
        return kOdoStop;
    }
    return kOdoNoWrite;
}

// The count written: what it holds now.
inline void OdoWritten(OdoState &o, unsigned long long at)
{
    o.wroteMetres = o.metres, o.wroteSeconds = o.seconds;
    o.wroteAt = at;
}

// Whether a write is owed (decided, saving, changed): at a change of truck the old id's figure goes first.
inline bool OdoOwed(const OdoState &o) { return o.decided && o.saves && (o.metres != o.wroteMetres || o.seconds != o.wroteSeconds); }

// The readout: kilometres (miles with mph) with one decimal under 100 and whole from 100, or engine hours with one.
inline void OdoText(char *out, size_t size, double metres, double seconds, bool hours, bool mph)
{
    if (hours)
    {
        snprintf(out, size, "%.1f h", seconds / 3600.0);
        return;
    }
    const double v = metres / (mph ? kOdoMile : 1000.0);
    const char *unit = mph ? "mi" : "km";
    if (v < 99.95) snprintf(out, size, "%.1f %s", v, unit);
    else snprintf(out, size, "%.0f %s", v, unit);
}

// Set to's figure from the tab's text in the unit shown: true with metres (or seconds with hours) where it is a number
// in range, 0 to kOdoMostKm (km or miles) or kOdoMostHours.
inline bool OdoSetValue(const char *text, bool hours, bool mph, double &out)
{
    if (!text) return false;
    while (*text == ' ') text++;
    char *end = nullptr;
    const double v = strtod(text, &end);
    if (end == text || !isfinite(v)) return false;
    while (*end == ' ') end++;
    if (*end) return false;
    if (v < 0.0 || v > (hours ? kOdoMostHours : kOdoMostKm)) return false;
    out = hours ? v * 3600.0 : v * (mph ? kOdoMile : 1000.0);
    return true;
}
