// The warning boxes while the Layout is Off (BRIEF_dials_off_option.md, the lead's part of 2026-11-12 with the
// Reviewer's addendum 2). One function decides which boxes there are, from the settings and the figures of a pass. The
// overlay draws the list and decides nothing.
//
// With dials shown the list is today's: the clutch's box in the first place and the engine's in the second, each at
// its own points, whatever HiddenWarnings holds. With the Layout at Off the choice acts:
//   None            no box. The damage still acts. No warning sound either (WarnSoundsOn).
//   As with dials   the clutch's and the engine's box as with dials.
//   At my limits    those two, the engine's amber at the player's coolant limit in place of the red mark, and a box
//                   each for fuel, engine damage, gearbox damage, oil, tilt and brake air.
// In Off the boxes stack: at most kWarnBoxes at once, the red ones first, then in the order of the enum below. A box
// that does not fit waits until one goes.
//
// A figure that was not read gives no box, never a box from a zero. Without a driven truck there is no box.

#pragma once
#include <math.h>
#include <stdio.h>
#include "gauges.h"

enum { kWlClutch, kWlEngine, kWlFuel, kWlEngineWorn, kWlGearboxWorn, kWlOil, kWlTilt, kWlAir, kWlCount };

// How far a figure must come back past its limit before its box goes, so a box does not flicker at the limit: fuel in
// points of the tank, the coolant in points of the way from warm to the red mark, the brake air in bar. Engine and
// gearbox damage have none (damage falls only by a repair). The oil's band is OilWarnStep's and the tilt's TiltStep's.
static const float kWlFuelBack = 2.0f, kWlCoolantBack = 3.0f, kWlAirBack = 0.3f;

// What a pass read. A figure with its "known" false was not read.
struct WarnFigures
{
    bool driving;            // a truck is driven
    WarnBox clutch, engine;  // today's boxes, as ClutchWarn and EngineWarn give them
    bool fuelKnown;          // the tank's size is above 0 and the fuel a number
    float fuelPercent;       // of the tank
    bool engineKnown, gearboxKnown;
    float enginePercent, gearboxPercent; // damage in percent of what the part takes
    bool coolantKnown;
    float coolantC;
    bool oilHot;             // OilWarning::on: at OilWarnC with the engine running, back by its own band
    float oilC;
    int tilt;                // TiltModel::lamp: 0 level, 1 amber, 2 red, below 0 not known
    float rollDeg, pitchDeg;
    bool airKnown;           // the truck brakes by air (no scout)
    float airBar;
};

// Which of the limits with a margin warned on the last pass. The caller keeps it from pass to pass.
struct WarnMargins { bool fuel, coolant, air; };

// stacked: the boxes take the first free slots and the displays start under the last one shown (Off). Not stacked:
// box 0 and box 1 are the clutch's and the engine's in their fixed slots, either may be empty (dials shown).
struct WarnList
{
    int count;
    bool stacked;
    WarnBox box[kWarnBoxes];
    int what[kWarnBoxes]; // kWl of each box, below 0 for an empty place
};

// No warning sound at all: the Layout is Off and the choice is None. The other sounds go by SoundVolume as before.
inline bool WarnSoundsOn(const GaugeSettings &s) { return !(s.layout == kLayoutOff && s.hiddenWarnings == kHwNone); }

inline int WarnPercent(float v) { return !isfinite(v) || v < 0.0f ? 0 : v > 100.0f ? 100 : (int)v; }

inline WarnList WarnListMake(const GaugeSettings &s, const WarnFigures &f, WarnMargins &m)
{
    const WarnBox none = { kWarnNone, "", "", 0 };
    WarnList l = {};
    for (int i = 0; i < kWarnBoxes; i++) l.box[i] = none, l.what[i] = -1;
    if (s.layout != kLayoutOff)
    {
        m = WarnMargins{};
        l.count = 2;
        if (f.driving) l.box[0] = f.clutch, l.box[1] = f.engine;
        l.what[0] = kWlClutch, l.what[1] = kWlEngine;
        return l;
    }
    l.stacked = true;
    if (!f.driving || s.hiddenWarnings == kHwNone || s.hiddenWarnings != kHwLimits)
    {
        m = WarnMargins{};
        if (!f.driving || s.hiddenWarnings == kHwNone) return l;
    }
    WarnBox all[kWlCount];
    for (int i = 0; i < kWlCount; i++) all[i] = none;
    all[kWlClutch] = f.clutch;
    all[kWlEngine] = f.engine;
    if (s.hiddenWarnings == kHwLimits)
    {
        // the engine's amber at the coolant's limit, as a share of the way from warm to the red mark; the red box of
        // damage acting comes whatever the limit is
        const float way = (f.coolantC - kTempWarm) / (kTempRed - kTempWarm) * 100.0f;
        m.coolant = f.coolantKnown && isfinite(way) && s.warnCoolant < 100 && way >= (float)s.warnCoolant - (m.coolant ? kWlCoolantBack : 0.0f);
        if (f.engine.state != kWarnRed)
        {
            all[kWlEngine] = none;
            if (m.coolant) all[kWlEngine] = { kWarnAmber, "ENGINE HOT", "The coolant is over your limit", WarnPercent(way) };
        }
        m.fuel = f.fuelKnown && isfinite(f.fuelPercent) && s.warnFuel > 0 && f.fuelPercent < (float)s.warnFuel + (m.fuel ? kWlFuelBack : 0.0f);
        if (m.fuel) all[kWlFuel] = { kWarnAmber, "LOW FUEL", "Refuel soon", WarnPercent(f.fuelPercent) };
        if (f.engineKnown && isfinite(f.enginePercent) && s.warnEngine < 100 && f.enginePercent > (float)s.warnEngine)
            all[kWlEngineWorn] = { kWarnAmber, "ENGINE WORN", "Repair the engine", WarnPercent(f.enginePercent) };
        if (f.gearboxKnown && isfinite(f.gearboxPercent) && s.warnGearbox < 100 && f.gearboxPercent > (float)s.warnGearbox)
            all[kWlGearboxWorn] = { kWarnAmber, "GEARBOX WORN", "Repair the gearbox", WarnPercent(f.gearboxPercent) };
        if (f.oilHot && isfinite(f.oilC))
        {
            // the bar: from 20 degrees under OilWarnC to 20 over it
            WarnBox &b = all[kWlOil];
            b = { kWarnAmber, "OIL HOT", "Ease off, the oil is too hot", WarnPercent((f.oilC - (s.oilWarnC - 20.0f)) / 40.0f * 100.0f) };
            snprintf(b.figure, sizeof b.figure, s.fahrenheit ? "%.0f F" : "%.0f C", s.fahrenheit ? f.oilC * 1.8f + 32.0f : f.oilC);
        }
        if (f.tilt == 1 || f.tilt == 2)
        {
            // the figure is the lean to a side, or the slope where that is the nearer to its red angle
            const float roll = isfinite(f.rollDeg) ? fabsf(f.rollDeg) : 0.0f, pitch = isfinite(f.pitchDeg) ? fabsf(f.pitchDeg) : 0.0f;
            const float rollShare = s.rollDangerDeg > 0.0f ? roll / s.rollDangerDeg : 0.0f, pitchShare = s.pitchDangerDeg > 0.0f ? pitch / s.pitchDangerDeg : 0.0f;
            WarnBox &b = all[kWlTilt];
            if (f.tilt == 2) b = { kWarnRed, "TILT", "Close to tipping over", 0 };
            else b = { kWarnAmber, "TILT", "The truck leans, slow down", 0 };
            b.percent = WarnPercent(fmaxf(rollShare, pitchShare) * 100.0f);
            snprintf(b.figure, sizeof b.figure, "%.0f deg", rollShare >= pitchShare ? roll : pitch);
        }
        m.air = f.airKnown && isfinite(f.airBar) && f.airBar < kAirWarn + (m.air ? kWlAirBack : 0.0f);
        if (m.air)
        {
            WarnBox &b = all[kWlAir];
            b = { kWarnAmber, "LOW BRAKE AIR", "Wait for the tanks to fill", WarnPercent(f.airBar / kAirWarn * 100.0f) };
            if (s.psi) snprintf(b.figure, sizeof b.figure, "%.0f psi", f.airBar * 14.5038f);
            else snprintf(b.figure, sizeof b.figure, "%.1f bar", f.airBar);
        }
    }
    // the red ones first, then the amber, each in the enum's order; what does not fit waits
    for (int state = kWarnRed; state >= kWarnAmber; state--)
        for (int i = 0; i < kWlCount && l.count < kWarnBoxes; i++)
            if (all[i].state == state) l.box[l.count] = all[i], l.what[l.count] = i, l.count++;
    return l;
}
