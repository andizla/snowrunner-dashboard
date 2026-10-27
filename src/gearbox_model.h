// gearbox_model.h: the mod's own gearbox, as a model with no game access. It holds the position the player picked (A,
// a numbered gear, N, R, H, or the low range L-, L, L+), the options, the clutch, and the stalls and shifts it caused.
// Each pass takes the player's actions (from the input reader, as booleans and edges), the truck as read and the gear
// the mod's automatic wants, and gives orders: the gear to set (as the game numbers them: below 0 reverse, 0 neutral,
// 1 to the top, the top + 1 the high gear), the power factor to set, a stall to cause, and the game's automatic flag
// to put down. The rules follow the manual transmission mod's (SMT, by drafty46; the game's functions found by
// Ferrster): up and down walk the gears, a gear key sets its gear, a shift without the clutch under RequireClutch
// stalls the engine, RequireGearHeld goes to neutral while no gear key is held, as an H shifter does, SkipNeutral
// steps from the first gear to reverse and back without stopping in neutral, and DownStopsAtFirst keeps down in the
// forward gears, as SMT's automatic did. A position the game's own shifter asked for (GearboxAsk) counts as its key.
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { kPosA, kPosGear, kPosN, kPosR, kPosH, kPosLowMinus, kPosLow, kPosLowPlus, kPosCount };

// The power factor of the low range, L-, L and L+, which the game gives a truck in its first gear: the figures truck.h
// has (TruckGearMode reads the letters by them). L's 0.45 is confirmed by a drive (2026-10-26, the d110 with Gearbox=0:
// SetPowerFactor 0.450 in L; A, H, N and R 1.000). The d110 has no L- or L+, so 0.2 and 1.0 stand until a heavy truck's
// drive through L- and L+ with Gearbox=0 confirms them in the probe's SetPowerFactor line (review 7, R8).
static const float kLowRangePower[3] = { 0.20f, 0.45f, 1.00f };
static const float kLowMinusUnder = 0.325f, kLowUnder = 0.725f; // a factor in gear 1 under these is L-, and L (truck.h)
// What a shift without the clutch brings the truck's stall seconds to (vehicle + 0x11C): the game kills an engine there
// from 1.4 on (SMT writes 5 at once).
static const float kStallSeconds = 1.5f;
// The stall is ramped from 0 to kStallSeconds over kStallRamp seconds (gauges.cpp), so the game's own "engine stalling"
// notice shows as it does when the game stalls an engine itself (the user's hill test, 27 October; a drive confirms
// the notice), where 5 at once killed the engine without it.
static const float kStallRamp = 0.3f;

// What the mod writes into the game's stall seconds from pass to pass: a stall ordered (RequireClutch) ramped from 0
// to kStallSeconds over kStallRamp; while the automatic clutch lugs the engine at speed (ClutchModel::lugging), its own
// seconds rising by dt as the game's would, and 0 once, the moment the engine is back above idle; nothing with the
// engine off (it died, or was never running). StallStep's answer is the seconds to write on this pass, below 0 none.
struct StallWrite
{
    bool ramping;
    float ramp; // seconds into the ramp
    float lug;  // the seconds the lugging has raised
};
inline float StallStep(StallWrite &w, bool ordered, bool lugging, bool engineOn, float dt)
{
    if (!engineOn)
    {
        w = StallWrite{};
        return -1.0f;
    }
    if (ordered)
    {
        w.ramping = true;
        w.ramp = 0.0f;
    }
    if (w.ramping)
    {
        w.ramp += dt;
        if (w.ramp >= kStallRamp) w.ramping = false;
        return fminf(w.ramp / kStallRamp, 1.0f) * kStallSeconds;
    }
    if (lugging)
    {
        w.lug += dt;
        return w.lug;
    }
    if (w.lug > 0.0f)
    {
        w.lug = 0.0f;
        return 0.0f;
    }
    return -1.0f;
}
static const float kPowerStep = 0.005f;   // a power factor this close to the one wanted is left as it is
static const float kStandStill = 0.5f;    // m/s under which the truck stands, for the setting off in A
static const float kSetOffPedal = 0.1f;   // the pedal over which a truck standing in N in A sets off in the first gear

struct GearboxOptions
{
    bool requireClutch;   // RequireClutch: a shift into a gear with the clutch not held stalls the engine
    bool requireGearHeld; // RequireGearHeld: neutral while no gear key is held, as an H shifter
    bool skipNeutral;     // SkipNeutral: up and down step from the first gear to reverse and back past neutral
    bool downStopsAtFirst; // DownStopsAtFirst: down never leaves the forward gears (N and R on their own keys); wins over SkipNeutral
};

// A position the game's own shifter asked for at Gearbox 2, read by the hooks from the door its call came through
// (gearbox_hooks.h, GearboxAskTake): R, N, H, A, a numbered gear (the game's number; past the top it is H), or the low
// range once the power factor came. L+ has the plain factor and reads as the first gear.
enum { kAskNone, kAskA, kAskN, kAskR, kAskH, kAskGear, kAskLowMinus, kAskLow };
struct GearboxAsk
{
    int kind; // kAsk...
    int gear; // the gear asked for, kAskGear only
};

// The player's actions on this pass. The edges are true on the pass a key goes down; gearHeld is the gear key held now.
struct GearboxActions
{
    bool up, down;
    int gearPressed;         // 1 to 12: that gear key went down on this pass; 0 none
    int gearHeld;            // 1 to 12: a gear key is held now (RequireGearHeld); 0 none
    bool a, n, r, h, lowMinus, low, lowPlus;
    bool rangeHigh, rangeLow; // for a shifter without a range of its own: between the low range and the numbered gears
    bool clutch;              // the clutch is held
    float clutchTravel = -1.0f; // its travel on an axis, 0 let up to 1 pressed; below 0 no axis (a button, or none) (review 7, R7)
    bool shifter;             // the position on this pass came from the game's shifter (GearboxActionsAsk)
};

// The game's shifter's request as this pass's action, the same as the key for that position. top: the truck's top
// numbered gear.
inline void GearboxActionsAsk(GearboxActions &a, const GearboxAsk &ask, int top)
{
    switch (ask.kind)
    {
    case kAskA: a.a = true; break;
    case kAskN: a.n = true; break;
    case kAskR: a.r = true; break;
    case kAskH: a.h = true; break;
    case kAskLowMinus: a.lowMinus = true; break;
    case kAskLow: a.low = true; break;
    case kAskGear:
        if (ask.gear > top) a.h = true;
        else a.gearPressed = ask.gear < 1 ? 1 : ask.gear;
        break;
    default: return;
    }
    a.shifter = true;
}

// The truck as read on this pass.
struct GearboxTruck
{
    int gear, top;        // the game's gear (as above) and its top numbered gear
    float speed;          // m/s over the ground, below 0 rolling backwards
    bool engineOn;
    float stall;          // the stall seconds (vehicle + 0x11C): above 0 the engine is already dying
    float power;          // the power factor (action + 0x38)
    bool automatic;       // the game's automatic flag (action + 0x3C)
    float pedal;          // the pedal, 0 to 1
};

struct GearboxPosModel
{
    bool running;         // it has the truck
    int pos;              // one of kPos...
    int gear;             // the numbered gear while pos is kPosGear
    float power;          // the truck's own power factor when it was taken: A, the numbered gears and H keep it
    bool clutch;          // the clutch held on the last pass
    float travel;         // its travel, 0 to 1
    int lastSet;          // the gear it set last, as the game numbers them
    int stalls, shifts;
    bool refusedSaid;     // a refusal of the game's was told since the position last changed
    bool waitBack;        // A or a gear picked from R while the truck still rolled back: N until it stands
    bool byShifter;       // the position was picked on the game's shifter, which stays where it was put: RequireGearHeld lets it be
};

struct GearboxPosOrders
{
    bool setGear;
    int gear;             // as the game numbers them
    bool setPower;
    float power;
    bool stall;           // write kStallSeconds into the stall seconds
    bool flagDown;        // put the game's automatic flag down
    bool moved;           // the position changed on this pass
    const char *why;      // what happened, for the log, a string literal; "" for nothing to say
};

inline const char *GearboxPosName(int pos)
{
    static const char *names[kPosCount] = { "A", "a numbered gear", "N", "R", "H", "L-", "L", "L+" };
    return pos >= 0 && pos < kPosCount ? names[pos] : "?";
}

// The letter the cluster shows for it ("" for a numbered gear, which shows its number).
inline const char *GearboxPosLetter(int pos)
{
    static const char *letters[kPosCount] = { "A", "", "N", "R", "H", "L-", "L", "L+" };
    return pos >= 0 && pos < kPosCount ? letters[pos] : "";
}

// Takes the truck in the position the game has it in: A when its automatic flag is up, else by its gear. The first gear
// at a factor under kLowUnder is L- or L, and the other positions get the plain 1 then: kept, the low factor would
// pull every gear (review 7, R3). L+ has the plain factor and reads as the first gear.
inline void GearboxPosTake(GearboxPosModel &m, const GearboxTruck &t)
{
    m = GearboxPosModel{ true };
    m.power = t.power > 0.0f ? t.power : 1.0f;
    m.lastSet = t.gear;
    if (t.automatic) m.pos = kPosA;
    else if (t.gear < 0) m.pos = kPosR;
    else if (t.gear == 0) m.pos = kPosN;
    else if (t.gear > t.top) m.pos = kPosH;
    else if (t.gear == 1 && t.power > 0.0f && t.power < kLowUnder)
    {
        m.pos = t.power < kLowMinusUnder ? kPosLowMinus : kPosLow;
        m.power = 1.0f;
    }
    else
    {
        m.pos = kPosGear;
        m.gear = t.gear;
    }
}

// The gear a position asks of the game. autoGear: the automatic's (A only).
inline int GearboxPosGear(const GearboxPosModel &m, const GearboxTruck &t, int autoGear)
{
    switch (m.pos)
    {
    case kPosA: return autoGear < 1 ? 1 : autoGear > t.top ? t.top : autoGear;
    case kPosGear: return m.gear < 1 ? 1 : m.gear > t.top ? t.top : m.gear;
    case kPosN: return 0;
    case kPosR: return -1;
    case kPosH: return t.top + 1;
    default: return 1; // the low range: the first gear at its own power
    }
}

inline float GearboxPosPower(const GearboxPosModel &m)
{
    return m.pos >= kPosLowMinus && m.pos <= kPosLowPlus ? kLowRangePower[m.pos - kPosLowMinus] : m.power;
}

// The numbered gear a manual position stands for, for up and down: a numbered gear its own, the low range and A the
// game's gear (at least 1), H the top + 1 (so down from H lands on the top, as SMT's walk does; review 7, R2), N 0,
// R -1.
inline int GearboxPosAsGear(const GearboxPosModel &m, const GearboxTruck &t)
{
    switch (m.pos)
    {
    case kPosGear: return m.gear;
    case kPosN: return 0;
    case kPosR: return -1;
    case kPosH: return t.top + 1;
    default: return t.gear < 1 ? 1 : t.gear > t.top ? t.top : t.gear;
    }
}

inline void GearboxPosToGear(GearboxPosModel &m, int g)
{
    if (g < 0) m.pos = kPosR;
    else if (g == 0) m.pos = kPosN;
    else
    {
        m.pos = kPosGear;
        m.gear = g;
    }
}

// One pass. autoGear: the gear the mod's automatic wants (A's gear).
inline GearboxPosOrders GearboxPosStep(GearboxPosModel &m, const GearboxOptions &o, const GearboxActions &a, const GearboxTruck &t, int autoGear)
{
    GearboxPosOrders r = {};
    r.why = "";
    if (!m.running) GearboxPosTake(m, t);
    const int top = t.top < 1 ? 1 : t.top;
    m.clutch = a.clutch || (a.clutchTravel >= 0.5f);
    m.travel = a.clutchTravel >= 0.0f ? (a.clutchTravel > 1.0f ? 1.0f : a.clutchTravel) : (a.clutch ? 1.0f : 0.0f);
    // the position the actions pick: a position key first, then a gear key, then up or down, then the range keys
    const int was = m.pos, wasGear = m.gear;
    if (a.a) m.pos = kPosA;
    else if (a.n) m.pos = kPosN;
    else if (a.r) m.pos = kPosR;
    else if (a.h) m.pos = kPosH;
    else if (a.lowMinus) m.pos = kPosLowMinus;
    else if (a.low) m.pos = kPosLow;
    else if (a.lowPlus) m.pos = kPosLowPlus;
    else if (a.gearPressed >= 1) GearboxPosToGear(m, a.gearPressed > top ? top : a.gearPressed);
    else if ((a.up || a.down) && !(a.up && m.pos == kPosH) && // up from H stays in H
             !(a.down && o.downStopsAtFirst && GearboxPosAsGear(m, t) <= 1)) // DownStopsAtFirst: down from the first gear, N or R does nothing
    {
        const int g = GearboxPosAsGear(m, t);
        int to = g;
        if (a.up) to = g >= top ? top : g == -1 && o.skipNeutral ? 1 : g + 1;
        else to = g <= -1 ? -1 : g == 1 && o.skipNeutral ? -1 : g - 1;
        GearboxPosToGear(m, to > top ? top : to);
    }
    else if (a.rangeLow && !(m.pos >= kPosLowMinus && m.pos <= kPosLowPlus)) m.pos = kPosLow;
    else if (a.rangeHigh && m.pos >= kPosLowMinus && m.pos <= kPosLowPlus) GearboxPosToGear(m, 1);
    // the game's shifter stays where it was put, so a gear picked there is held; a key's position is not
    if (a.shifter) m.byShifter = true;
    else if (m.pos != was || m.gear != wasGear) m.byShifter = false;
    // an H shifter: neutral the moment no gear key is held
    if (o.requireGearHeld && m.pos == kPosGear && a.gearHeld < 1 && !m.byShifter) m.pos = kPosN;
    r.moved = m.pos != was || (m.pos == kPosGear && m.gear != wasGear);
    if (r.moved) m.refusedSaid = false;
    // A or a gear picked from R while the truck still rolls back: neutral until it stands or rolls forward, so no
    // forward gear goes in against the rolling truck (the first drive put gear 1 in at 2.6 m/s backwards)
    const bool forward = m.pos == kPosA || m.pos == kPosGear;
    if (r.moved && was == kPosR && forward && t.speed < -kStandStill)
    {
        m.waitBack = true;
        r.why = m.pos == kPosA ? "A picked while rolling back: neutral until the truck stands" : "a gear picked while rolling back: neutral until the truck stands";
    }
    if (m.waitBack && (!forward || t.speed >= -kStandStill)) m.waitBack = false;
    // the gear and the power the position asks for; A sets off from N only once the pedal is down
    int want = m.waitBack ? 0 : GearboxPosGear(m, t, autoGear);
    if (m.pos == kPosA && t.gear == 0 && fabsf(t.speed) < kStandStill && t.pedal < kSetOffPedal) want = 0;
    if (want != t.gear && (r.moved || want != m.lastSet))
    {
        r.setGear = true;
        r.gear = want;
        m.lastSet = want;
        m.shifts++;
    }
    const float power = GearboxPosPower(m);
    if (fabsf(power - t.power) > kPowerStep)
    {
        r.setPower = true;
        r.power = power;
    }
    r.flagDown = t.automatic;
    // a shift by the player into a gear without the clutch stalls the engine once, not into neutral, and not when the
    // engine is already dying or off
    if (o.requireClutch && r.moved && m.pos != kPosN && m.pos != kPosA && !m.clutch && t.engineOn && !(t.stall > 0.0f))
    {
        r.stall = true;
        m.stalls++;
        r.why = "a shift without the clutch stalled the engine";
    }
    return r;
}

// The gearbox in a line of text for the probe log.
inline void GearboxPosProbeText(const GearboxPosModel &m, const GearboxOptions &o, int gear, char *out, size_t size)
{
    char at[24] = "";
    if (m.pos == kPosGear) snprintf(at, sizeof at, " %d", m.gear);
    snprintf(out, size, "gearbox: %s%s (the game's gear %d), clutch %s at %.2f, %d shifts, %d stalls | RequireClutch %d, RequireGearHeld %d, SkipNeutral %d, DownStopsAtFirst %d",
             GearboxPosName(m.pos), at, gear, m.clutch ? "held" : "let up", m.travel, m.shifts, m.stalls, o.requireClutch, o.requireGearHeld, o.skipNeutral,
             o.downStopsAtFirst);
}

// The game did not take the gear the mod set: it counts as asked, so the mod asks again when the position (or A's
// gear) next asks for another, not on every pass (review 7, R1). True the first time since the position changed: the
// refusal is logged once a position.
inline bool GearboxPosRefused(GearboxPosModel &m, int gear)
{
    m.lastSet = gear;
    const bool say = !m.refusedSaid;
    m.refusedSaid = true;
    return say;
}
