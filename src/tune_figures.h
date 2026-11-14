// tune_figures.h: the figures Gameplay publishes for the tuning tool (ng_tune.h; BRIEF_rolling_drag_sliders.md with the
// Reviewer's addendum 3). Four settings of the Powertrain: the diff lock's bite and the low range's slip, which have
// their sliders on the Powertrain tab too, and the wheel cap and the push share, which are ini keys with no slider
// there, so the tool's is their only one. And the slip room of the numbered gears (BRIEF_held_gear_slip.md): a slider
// of 0, 1 and 2, since the block has no choice of three, with a line under it that says the choice by name and the cap
// in force. And the two shares of the steering with a lock in (BRIEF_lock_steering.md, M7), with a line that says the
// share in force.
//
// The mod's own thread alone touches the block. Each pass it takes what the tool asked into a copy of the settings the
// tab shows; gauges.cpp hands that copy back by SettingsPut with the turn it was read at, the road the tab's own edit
// takes: in force on that pass, saved to the ini once nothing new came for a second. The tab then shows the same
// figure, as it reads the same settings. The texts are the player's: no apostrophe in them.
#pragma once
#include <math.h>
#include "gauges.h"
#include "ng_tune.h"
#include "gearbox_model.h"

enum { kGtBite, kGtCap, kGtPush, kGtLowSlip, kGtHeldSlip, kGtSteerFront, kGtSteerRear, kGtSoundTop, kGtSoundCurve, kGtRatioPull, kGtEngineInertia, kGtCount }; // the figures; after them the lines to read
enum { kGtHeldLine = kGtCount, kGtSteerLine, kGtItems };
struct GtInfo { const char *key, *name, *tip; float least, most, def; };
static const char kGtGroup[] = "Powertrain";
static const char kGtWhyOff[] = "Real engine is off.";
static const GtInfo kGt[kGtCount] = {
    { "DiffLockBite", "Diff lock bite", "How firmly the diff lock ties the driven wheels together. 0 leaves the game lock as it is. 100 turns them as one shaft.", 0.0f, 100.0f, 0.0f },
    { "WheelCap", "Wheel cap", "In L-, L, L+ and R no driven wheel runs faster than this percent of the top speed of its gear. Needs the mod gearbox.", 110.0f, 200.0f, 150.0f },
    { "PushShare", "Push share", "How much of the extra push the game gives the chassis at speed is left in, in percent. 0 takes all of it back.", 0.0f, 100.0f, 0.0f },
    { "LowRangeSlip", "Low range slip", "How much wheel slip L-, L and R may use. 100 gives them the same room as L+. 0 holds each wheel near the top of its gear.", 0.0f, 100.0f, 100.0f },
    { "HeldGearSlip", "Held gear slip", "Wheel spin in a numbered gear held by hand. 0 Engine held: to the engine limit. 1 Low gears: as L+. 2 All gears: by Wheel cap.", 0.0f, 2.0f, 2.0f },
    { "LockSteerFront", "Steering with front lock", "Steering left while an axle that steers is locked. 100 leaves it all. A truck whose lock is always on has this all the time.", 30.0f, 100.0f, 60.0f },
    { "LockSteerRear", "Steering with rear lock", "Steering left while another driven axle is locked. 100 leaves it all. With both locked the smaller figure counts.", 30.0f, 100.0f, 85.0f },
    { "EngineSoundTop", "Engine sound top", "How high the engine sound is at the start of the red zone, in percent of its top note. Lower it if it sounds too fast.", 40.0f, 100.0f, 100.0f },
    { "EngineSoundCurve", "Engine sound curve", "How the engine sound rises with the revs. 100 is straight. 180 bends as the pedal does: slow at first, fast near the top.", 100.0f, 300.0f, 100.0f },
    { "GearRatioPull", "Gear ratio pull", "More pull in the low gears held by hand. 0 is the game. The engine braking of a low gear grows with it. The fuel does not.", 0.0f, 100.0f, 0.0f },
    { "EngineInertia", "Engine inertia", "How long spinning wheels run on after the foot comes off, as an engine and driveline keep them turning. 0 is the game.", 0.0f, 100.0f, 0.0f },
};
inline int *GtField(GaugeSettings &s, int i)
{
    return i == kGtBite ? &s.diffLockBite : i == kGtCap ? &s.wheelCap : i == kGtPush ? &s.pushShare : i == kGtHeldSlip ? &s.heldGearSlip : i == kGtSteerFront ? &s.lockSteerFront :
           i == kGtSteerRear ? &s.lockSteerRear : i == kGtSoundTop ? &s.engineSoundTop : i == kGtSoundCurve ? &s.engineSoundCurve : i == kGtRatioPull ? &s.gearRatioPull : i == kGtEngineInertia ? &s.engineInertia : &s.lowRangeSlip;
}

inline int GtItems(NgTuneItem *items)
{
    for (int i = 0; i < kGtCount; i++) items[i] = { kNgTuneSlider, kGt[i].name, kGtGroup, kGt[i].tip, kGt[i].least, kGt[i].most, kGt[i].def, 1.0f, 0 };
    items[kGtHeldLine] = { kNgTuneLine, "Held gear slip now", kGtGroup, "The choice in force by name and the wheel cap of the gear held, in rad/s.", 0.0f, 0.0f, 0.0f, 0.0f, 0 };
    items[kGtSteerLine] = { kNgTuneLine, "Steering now", kGtGroup, "The share of the steering left now, in percent.", 0.0f, 0.0f, 0.0f, 0.0f, 0 };
    return kGtItems;
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
// line: the held gear slip's line (HeldSlipLine), from the truck's pass; nullptr with no truck. steer: the steering's
// line from the same pass ("60 %"); nullptr or empty where nothing limits it.
inline void GtPublish(uint8_t *block, GaugeSettings s, uint64_t now, const char *line = nullptr, const char *steer = nullptr)
{
    const bool off = !s.powertrain;
    NgTuneBegin(block);
    for (int i = 0; i < kGtCount; i++) NgTuneValue(block, i, (float)*GtField(s, i), off && i == 0 ? kGtWhyOff : nullptr, off);
    NgTuneValue(block, kGtHeldLine, 0.0f, line && line[0] ? line : "no gear held", off);
    NgTuneValue(block, kGtSteerLine, 0.0f, steer && steer[0] ? steer : "all of it", off);
    NgTuneEnd(block, now);
}
