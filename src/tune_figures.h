// tune_figures.h: the figures Gameplay publishes for the tuning tool (ng_tune.h; BRIEF_rolling_drag_sliders.md with the
// Reviewer's addendum 3). Four settings of the Powertrain: the diff lock's bite and the low range's slip, which have
// their sliders on the Powertrain tab too, and the wheel cap and the push share, which are ini keys with no slider
// there, so the tool's is their only one.
//
// The mod's own thread alone touches the block. Each pass it takes what the tool asked into a copy of the settings the
// tab shows; gauges.cpp hands that copy back by SettingsPut with the turn it was read at, the road the tab's own edit
// takes: in force on that pass, saved to the ini once nothing new came for a second. The tab then shows the same
// figure, as it reads the same settings. The texts are the player's: no apostrophe in them.
#pragma once
#include <math.h>
#include "gauges.h"
#include "ng_tune.h"

enum { kGtBite, kGtCap, kGtPush, kGtLowSlip, kGtCount };
struct GtInfo { const char *key, *name, *tip; float least, most, def; };
static const char kGtGroup[] = "Powertrain";
static const char kGtWhyOff[] = "Real engine is off.";
static const GtInfo kGt[kGtCount] = {
    { "DiffLockBite", "Diff lock bite", "How firmly the diff lock ties the driven wheels together. 0 leaves the game lock as it is. 100 turns them as one shaft.", 0.0f, 100.0f, 0.0f },
    { "WheelCap", "Wheel cap", "In L-, L, L+ and R no driven wheel runs faster than this percent of the top speed of its gear. Needs the mod gearbox.", 110.0f, 200.0f, 150.0f },
    { "PushShare", "Push share", "How much of the extra push the game gives the chassis at speed is left in, in percent. 0 takes all of it back.", 0.0f, 100.0f, 0.0f },
    { "LowRangeSlip", "Low range slip", "How much wheel slip L-, L and R may use. 100 gives them the same room as L+. 0 holds each wheel near the top of its gear.", 0.0f, 100.0f, 100.0f },
};
inline int *GtField(GaugeSettings &s, int i) { return i == kGtBite ? &s.diffLockBite : i == kGtCap ? &s.wheelCap : i == kGtPush ? &s.pushShare : &s.lowRangeSlip; }

inline int GtItems(NgTuneItem *items)
{
    for (int i = 0; i < kGtCount; i++) items[i] = { kNgTuneSlider, kGt[i].name, kGtGroup, kGt[i].tip, kGt[i].least, kGt[i].most, kGt[i].def, 1.0f, 0 };
    return kGtCount;
}
// What the tool asked since the last pass, into s (the settings the tab shows). NgTuneTake has held each to its range
// and to whole percent, and drops what is no number and anything asked of a greyed entry. True: s changed.
inline bool GtTake(uint8_t *block, GaugeSettings &s)
{
    bool any = false;
    for (int i = 0; i < kGtCount; i++)
    {
        float held = 0.0f;
        if (NgTuneTake(block, i, held) <= 0) continue;
        *GtField(s, i) = (int)floorf(held + 0.5f);
        any = true;
    }
    return any;
}
// The figures as the tab shows them, under the block's sequence. With Real engine off none of them acts: greyed, and the
// first carries why.
inline void GtPublish(uint8_t *block, GaugeSettings s, uint64_t now)
{
    const bool off = !s.powertrain;
    NgTuneBegin(block);
    for (int i = 0; i < kGtCount; i++) NgTuneValue(block, i, (float)*GtField(s, i), off && i == 0 ? kGtWhyOff : nullptr, off);
    NgTuneEnd(block, now);
}
