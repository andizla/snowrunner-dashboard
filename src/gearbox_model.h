// gearbox_model.h: the mod's own gearbox, as a model with no game access. It holds the position the player picked (A,
// a numbered gear, N, R, H, or the low range L-, L, L+), the options, the clutch, and the stalls and shifts it caused.
// Each pass takes the player's actions (from the input reader, as booleans and edges), the truck as read and the gear
// the mod's automatic wants, and gives orders: the gear to set (as the game numbers them: below 0 reverse, 0 neutral,
// 1 to the top, the top + 1 the high gear), the power factor to set, a stall to cause, and the game's automatic flag
// to put down. The rules follow the manual transmission mod's (SMT, by drafty46; the game's functions found by
// Ferrster): up and down walk R, N, L-, L, L+ and the numbered gears, a gear key sets its gear, a shift without the
// clutch under RequireClutch stalls the engine, RequireGearHeld goes to neutral while no gear key is held, as an H
// shifter does, SkipNeutral steps from the lowest forward place to reverse and back without stopping in neutral,
// DownStaysForward stops down at the lowest forward place, as SMT's immersive mode did, and WalkSkipsLow walks R, N, 1
// and on, leaving the low range but never entering it. A position the game's own shifter asked for (GearboxAsk) counts as its key.
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { kPosA, kPosGear, kPosN, kPosR, kPosH, kPosLowMinus, kPosLow, kPosLowPlus, kPosCount };

// The positions a gearbox may lack, as bits (BRIEF_positions_by_gearbox.md): what the fitted gearbox has in the game
// (gearbox_positions.h reads it) and, on GearboxTruck::missing, what it lacks. kHasManual is read for the log alone.
enum { kHasL = 1, kHasLowPlus = 2, kHasLowMinus = 4, kHasH = 8, kHasPositions = 15, kHasManual = 16 };
// The bit of a position that a gearbox may lack; 0 for one every gearbox has.
inline int GearboxPosHasBit(int pos)
{
    return pos == kPosLow ? kHasL : pos == kPosLowPlus ? kHasLowPlus : pos == kPosLowMinus ? kHasLowMinus : pos == kPosH ? kHasH : 0;
}

// The power factor of the low range, L-, L and L+, which the game gives a truck in its first gear: the figures truck.h
// has (TruckGearMode reads the letters by them). L's 0.45 is confirmed by a drive (2026-10-26, the d110 with Gearbox=0:
// SetPowerFactor 0.450 in L; A, H, N and R 1.000). The d110 has no L- or L+, so 0.2 and 1.0 stand until a heavy truck's
// drive through L- and L+ with Gearbox=0 confirms them in the probe's SetPowerFactor line (review 7, R8).
static const float kLowRangePower[3] = { 0.20f, 0.45f, 1.00f };
// The power factor of the highest low gear the fitted gearbox has (missing: the kHas... bits it lacks), 0 with none.
inline float GearboxLowMostPower(int missing)
{
    static const int has[3] = { 4, 1, 2 }; // kHasLowMinus, kHasL, kHasLowPlus
    for (int i = 2; i >= 0; i--)
        if (!(missing & has[i])) return kLowRangePower[i];
    return 0.0f;
}
static const float kLowMinusUnder = 0.325f, kLowUnder = 0.725f; // a factor in gear 1 under these is L-, and L (truck.h)
// The road load learns gear 1 only at the plain power: L and L- are the game's gear 1 at 0.45 and 0.20, with another
// pull and top, and in gear 1's sums they would skew its a_base and, through the fold, K (review F1). The test
// GearboxPosTake uses.
inline bool BaseLearnPlainPower(int gear, float power) { return gear != 1 || !(power > 0.0f && power < kLowUnder); }
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
static const float kGrindSpeed = 1.5f;    // m/s (about 5 km/h) from which a gear picked without the clutch stays out instead of stalling the engine
static const float kSetOffPedal = 0.1f;   // the pedal over which a truck standing in N in A sets off in the first gear
// Up from L+ goes to the 1st only once the truck has been in L+ this long since an up press brought it there, so a run
// of presses up from R stops in the low range (BRIEF_low_range_up_to_1.md). L+ reached any other way lets up go at once.
static const float kLowRangeUpHold = 1.0f;

struct GearboxOptions
{
    bool requireClutch;   // RequireClutch: a shift into a gear with the clutch not held stalls the engine
    bool requireGearHeld; // RequireGearHeld: neutral while no gear key is held, as an H shifter
    bool skipNeutral;     // SkipNeutral: up and down step from the lowest forward place to reverse and back past neutral
    bool downStaysForward; // DownStaysForward: down stops at the lowest forward place (N and R on their own keys); wins over SkipNeutral
    bool walkSkipsLow;    // WalkSkipsLow: up and down walk R, N, 1, 2 and on; they leave the low range but never enter it
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
    int gearPressed;         // 1 to 16: that gear key went down on this pass; 0 none
    int gearHeld;            // 1 to 16: a gear key is held now (RequireGearHeld); 0 none
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

// ---- the game's shifter sends its position again (BRIEF_lock_press_shifts_to_a.md) ----
// The game's shifter keeps a position of its own, the last one it asked for, and sends it again each time its button is
// let go, moved or not (read from a drive's log, not from the game's code). A tap of that button, or the lock on the same
// button, came as a pick of that position and threw a low gear to A. So the mod keeps the shifter's last ask (after the
// merge) and follows only an ask that differs. A binding does not change the kept ask: L on the shifter, L+ by a binding
// and a tap would else give L again. The cost: the position the shifter already stands at cannot be picked on it again;
// its binding picks it, or another position first.
struct ShifterKept
{
    GearboxAsk last; // the shifter's last ask; at the take the position the truck was taken in; kAskNone while let go
    unsigned said;   // a bit a kind: the log told that repeat once
    int again;       // the repeats not followed since the last count
    float since;     // seconds since the first of them
};
static const float kShifterAgainTell = 60.0f; // the count of the repeats goes to the log this long after the first

// The ask the game's shifter would send for a position of the mod's: L+ reads as the first gear, as the merge has it.
inline GearboxAsk GearboxAskOfPos(int pos, int gear)
{
    switch (pos)
    {
    case kPosA: return GearboxAsk{ kAskA, 0 };
    case kPosN: return GearboxAsk{ kAskN, 0 };
    case kPosR: return GearboxAsk{ kAskR, 0 };
    case kPosH: return GearboxAsk{ kAskH, 0 };
    case kPosLowMinus: return GearboxAsk{ kAskLowMinus, 0 };
    case kPosLow: return GearboxAsk{ kAskLow, 0 };
    case kPosLowPlus: return GearboxAsk{ kAskGear, 1 };
    case kPosGear: return GearboxAsk{ kAskGear, gear };
    default: return GearboxAsk{ kAskNone, 0 };
    }
}

inline bool GearboxAskSame(const GearboxAsk &a, const GearboxAsk &b) { return a.kind == b.kind && (a.kind != kAskGear || a.gear == b.gear); }

// An ask taken on this pass: a pick (it differs from the kept one, becomes the kept one and is this pass's action), or
// the kept one again, which is not followed; the first of a kind since the take is told apart for the log.
enum { kShifterPick, kShifterAgain, kShifterAgainFirst };
inline int ShifterAskStep(ShifterKept &k, const GearboxAsk &ask, GearboxActions &acts, int top)
{
    if (ask.kind == kAskNone) return kShifterAgain;
    if (!GearboxAskSame(k.last, ask))
    {
        k.last = ask;
        GearboxActionsAsk(acts, ask, top);
        return kShifterPick;
    }
    // the first of a kind is told at once and not counted, so the count's line comes only for more (the review of 133, S2)
    const unsigned bit = 1u << ask.kind;
    if (k.said & bit)
    {
        k.again++;
        return kShifterAgain;
    }
    k.said |= bit;
    return kShifterAgainFirst;
}

// Once a pass: the count of the repeats to tell, 0 for none yet.
inline int ShifterAgainTold(ShifterKept &k, float dt)
{
    if (k.again <= 0) return 0;
    if (dt > 0.0f && dt < 10.0f) k.since += dt;
    if (k.since < kShifterAgainTell) return 0;
    const int n = k.again;
    k.again = 0;
    k.since = 0.0f;
    return n;
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
    int missing;          // kHas... bits of the positions the fitted gearbox lacks (gearbox_positions.h); 0 every position there
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
    float lowPlusFor;     // seconds in L+: 0 when an up press walked there, kLowRangeUpHold when it came any other way
    bool lowRefusedSaid;  // an up refused in L+ was told on this visit to L+
    bool heldFromLow;     // a 1st reached from the low range: held, no Back to A and no Sequential back to D, until the next shift
};

// A numbered gear that Back to A and Sequential leave where it is: picked on the game's shifter, or a 1st reached from
// the low range until the next shift (heldFromLow). gauges.cpp feeds both by it, as the tests do.
inline bool GearboxPosHeld(const GearboxPosModel &m) { return m.byShifter || m.heldFromLow; }

struct GearboxPosOrders
{
    bool setGear;
    int gear;             // as the game numbers them
    bool setPower;
    float power;
    bool stall;           // write kStallSeconds into the stall seconds
    bool flagDown;        // put the game's automatic flag down
    bool moved;           // the position changed on this pass
    bool lowUp;           // an up in L+ refused, as an up press brought the truck there under kLowRangeUpHold ago
    bool lowUpSay;        // the first refusal on this visit to L+: told in the log
    float lowUpAfter;     // the seconds in L+ at the refusal
    bool lowToFirst;      // the low range went to the 1st, which is held (heldFromLow)
    bool keyRefused;      // a key asked for refusedPos, which the gearbox lacks: nothing done
    bool shifterMissing;  // the game's shifter gave refusedPos, which the read says the gearbox lacks: taken, the read is wrong
    int refusedPos;       // kPos...
    bool fitted;          // the truck stood in fittedFrom, which the gearbox lacks, and went to the nearest it has
    int fittedFrom;       // kPos...
    const char *why;     // what happened, for the log, a string literal; "" for nothing to say
};

// ---- BackToA: the mod's own gearbox back in A by itself (review 17, Sequential) ----
// After a shift by hand into a numbered gear (GearUp, GearDown, a gear key) and BackToA seconds without another, the
// position goes back to A once every condition below has held for kBackSteady without a break. Never for a gear held
// by RequireGearHeld or picked on the game's shifter, which stay where they were put, and never from R, N, the low
// range or H. A's gear is the one the mod's automatic would ask on its first pass back (TakeoverAutoGear).
static const float kBackSteady = 1.0f, kBackPedal = 0.7f, kBackSlip = 1.05f, kBackSlipAdd = 0.5f, kBackClimbDeg = 8.0f, kBackClimbPedal = 0.4f,
                   kBackDescentDeg = 5.0f, kBackAfterSwitch = 3.0f, kBackFootOff = 0.05f;
// What holds the return, in the order the conditions are checked.
enum
{
    kBackOff, kBackNotHand, kBackHeld, kBackTimer, kBackPedalDown, kBackNoGrip, kBackSpin, kBackClimb, kBackDescent, kBackRollingBack,
    kBackGap, kBackSwitch, kBackHandbrake, kBackEngine, kBackShifting, kBackSteadying, kBackGo, kBackWhyCount
};
static const char *const kBackWhyNames[kBackWhyCount] = { "BackToA 0", "no shift by hand into a numbered gear", "the gear held (RequireGearHeld, the game's shifter or a 1st from the low range)",
                                                          "the timer", "the pedal", "the wheels' grip not learned yet (drive a moment in A or ease the pedal)", "wheelspin", "a climb",
                                                          "a descent", "rolling back", "A's gear too far", "a lock switched", "the handbrake",
                                                          "the engine", "a shift", "a second steady", "nothing" };
struct BackInputs
{
    int pos, heldGear, autoGear;
    bool byShifter, gearHeldRule; // picked on the game's shifter; RequireGearHeld on
    float pedal;
    float grip, spin, speed;      // the wheels' rad/s for each m/s learned (0 not yet), their speed, the truck's in m/s (below 0 backwards)
    bool pitchKnown;
    float pitchDeg;               // above 0 the nose up
    bool climbHold;               // the automatic's hold after a climb is running
    bool awd, diffLock, handbrake, engineOn, dying, shiftBusy;
};
struct BackModel
{
    bool timing;     // a shift by hand into a numbered gear started the timer
    float since;     // seconds since it
    float steady;    // seconds every condition has held
    bool locksSeen, awd, diffLock;
    float switchAge; // seconds since the diff lock or AWD last changed
    int why;         // what holds the return now (kBack...)
};
// One pass of dt seconds; seconds: BackToA; hand: a shift by hand into a numbered gear on the last pass. True on the
// pass the position is to go back to A.
inline bool BackToAStep(BackModel &m, int seconds, bool hand, const BackInputs &in, float dt)
{
    if (m.locksSeen && (in.awd != m.awd || in.diffLock != m.diffLock)) m.switchAge = 0.0f;
    else m.switchAge += dt;
    m.locksSeen = true;
    m.awd = in.awd;
    m.diffLock = in.diffLock;
    auto hold = [&](int why) { m.why = why; m.steady = 0.0f; return false; };
    if (seconds <= 0) { m.timing = false; return hold(kBackOff); }
    if (hand) m.timing = true, m.since = 0.0f, m.steady = 0.0f;
    if (in.pos != kPosGear) m.timing = false;
    if (!m.timing) return hold(kBackNotHand);
    if (in.byShifter || in.gearHeldRule) return hold(kBackHeld);
    m.since += dt;
    if (m.since < (float)seconds) return hold(kBackTimer);
    const float ground = in.grip * fabsf(in.speed);
    const bool standing = fabsf(in.speed) < kStandStill && in.pedal < kBackFootOff;
    // with A's gear the held one a return is no shift at all, so the foot may stay down
    if (in.pedal >= kBackPedal && in.autoGear != in.heldGear) return hold(kBackPedalDown);
    if (!(in.grip > 0.0f)) return hold(kBackNoGrip);
    if (in.spin > ground * kBackSlip + kBackSlipAdd) return hold(kBackSpin);
    if (in.climbHold || (in.pitchKnown && in.pitchDeg > kBackClimbDeg && in.pedal >= kBackClimbPedal)) return hold(kBackClimb);
    if (in.pitchKnown && in.pitchDeg < -kBackDescentDeg && in.speed > kStandStill) return hold(kBackDescent);
    if (in.speed < -kStandStill) return hold(kBackRollingBack);
    if (!standing && abs(in.autoGear - in.heldGear) > 1) return hold(kBackGap);
    if (m.switchAge < kBackAfterSwitch) return hold(kBackSwitch);
    if (in.handbrake) return hold(kBackHandbrake);
    if (!in.engineOn || in.dying) return hold(kBackEngine);
    if (in.shiftBusy) return hold(kBackShifting);
    m.steady += dt;
    if (m.steady < kBackSteady)
    {
        m.why = kBackSteadying;
        return false;
    }
    m.timing = false;
    m.steady = 0.0f;
    m.why = kBackGo;
    return true;
}

// ---- Sequential's own shifts (the held gear power brief, point 3; back to D, the sequential to D brief) ----
// With BackToA on, not with RequireClutch or RequireGearHeld (a manual with a clutch or an H shifter never shifts by
// itself) and never for a gear picked on the game's shifter, a held numbered gear goes back to D by itself, as a real
// automatic's manual mode does, and D picks the gear: near the red zone (kSeqUpShare) with the foot on, held
// kSeqUpHold, not while the wheels spin and not while their grip is unknown (spin cannot be told from speed then: it
// stays at the limiter), and never with the foot off, where a low gear held on a descent is engine braking; and before
// a stall, the gearbox's input under kSeqDownIdle times idle, held kSeqDownHold with the foot on or off, kSeqDownBrake
// under the brake. By the gearbox's input, not the engine's speed: the clutch that works by itself slips at low speed
// and holds the engine near idle. They are protective, so Back to A's pedal and lock conditions do not hold them. None
// within a shift's stages, which end kCreepQuiet after the gear engaged.
// Up at kSeqUpShare of the red zone's speed: the input is the wheels' share of the gear's top times the red zone's
// rpm, and with the game's pull nothing at the top a floored held gear settles near 0.95 of it; at 0.9 it gives about
// 0.56 of the curve, and the next gear lands near 0.63 of its top at the full curve, so the shift gains pull (review F2).
static const float kSeqUpHold = 0.3f, kSeqDownHold = 0.5f, kSeqDownBrake = 0.2f, kSeqDownIdle = 1.1f, kSeqUpShare = 0.9f;
struct SeqInputs
{
    int backToA;
    bool requireClutch, requireGearHeld, byShifter;
    int pos, gear, top;
    float input, redline, idle; // the gearbox's input speed, the red zone's and the idle's, rpm
    float pedal;
    bool braking;   // the brake or the handbrake on
    bool gripKnown, spinning, busy, engineOn;
};
struct SeqModel { float up, down; };
// One pass of dt seconds: +1 back to D at the red zone, -1 back to D before a stall, 0 none.
inline int SequentialStep(SeqModel &m, const SeqInputs &in, float dt)
{
    if (in.backToA <= 0 || in.requireClutch || in.requireGearHeld || in.byShifter || in.pos != kPosGear || !in.engineOn || in.busy)
    {
        m = SeqModel{};
        return 0;
    }
    const bool up = in.gear < in.top && in.input >= kSeqUpShare * in.redline && in.pedal >= kBackFootOff && in.gripKnown && !in.spinning;
    const bool down = in.gear > 1 && in.input < kSeqDownIdle * in.idle;
    m.up = up ? m.up + dt : 0.0f;
    m.down = down ? m.down + dt : 0.0f;
    if (m.up >= kSeqUpHold) { m = SeqModel{}; return 1; }
    if (m.down >= (in.braking ? kSeqDownBrake : kSeqDownHold)) { m = SeqModel{}; return -1; }
    return 0;
}

// At Gearbox 0 and 1 the game's shifter is the player's: the mod's orders go out only in A, and a gear only forward
// (never N or R from the mod's thread; 0.18.2's hotfix). pos: the position the model holds after its step.
inline bool GearboxOwnOrders(int pos, int gear, bool setGear) { return pos == kPosA && (!setGear || gear >= 1); }

// The creep goes only where an automatic picks the gear (the author, 1 November: "creep needs to be disabled in manual
// for now. auto handles it fine"; BRIEF_manual_creep_and_hold.md): the mod's automatic in A while its gearbox has the
// truck (the reverse and the range it drives there included), else the game's own automatic up. A gear picked by hand
// on either shifter gets none, an R included: the game turns its automatic off to shift into R (its reverse goes
// through AutoOffAndShift), so an R it took at a stop cannot be told from one picked by hand.
inline bool CreepAutoPicks(bool modDrives, bool posRunning, int pos, uint8_t gameAutomatic)
{
    return modDrives ? posRunning && pos == kPosA : gameAutomatic != 0;
}

// The creep's gate: where an automatic picks the gear, and with CreepManual (the gearbox rebuild's step 4, the creep
// hand gear brief) in a gear picked by hand too, on either shifter, as before 53f526d: the governor by the clutch's
// grip, the holding part, and at Gearbox=2 the stall; R and the low range on the game's shifter through the converter.
inline bool CreepGoes(bool creepManual, bool modDrives, bool posRunning, int pos, uint8_t gameAutomatic)
{
    return creepManual || CreepAutoPicks(modDrives, posRunning, pos, gameAutomatic);
}

inline const char *GearboxPosName(int pos)
{
    static const char *names[kPosCount] = { "A", "a numbered gear", "N", "R", "H", "L-", "L", "L+" };
    return pos >= 0 && pos < kPosCount ? names[pos] : "?";
}

// The positions the wheel cap guards, one condition in one place: the low range L-, L and L+ and R, whose tops are as
// low (the user, 2026-11-10: "L gears and R"). Not a numbered gear held by hand, where the governor on the truck's speed
// and the engine curve are the whole limit. Every held gear, as the Reviewer's part 3 had it, would add pos == kPosGear.
inline bool GearboxPosCapped(int pos) { return pos == kPosR || (pos >= kPosLowMinus && pos <= kPosLowPlus); }

// The wheel cap of L-, L and R, widened (BRIEF_low_range_pull.md, M1; R since BRIEF_reverse_pull.md: its top is as low,
// so its wheels had as little slip to pull by). R's own cap at or over most stays its own.
// The wheel cap of L- and L first: A low gear's pull comes from its wheels' slip, and a
// cap at half again a top of 2 rad/s leaves L- a third of the slip L+ has, so the deepest gear pulled the least. own:
// the cap by the gear's own top; most: the cap by the top of the highest low gear the gearbox has; share: LowRangeSlip,
// the percent of the way from own to most. 0 is own to the last bit. Never over most, so a free wheel runs no faster
// than it may in L+. Any other position, one low gear, or a most that is no number or not over own: own.
inline float LowRangeCap(int pos, float own, float most, int share)
{
    if (!(own > 0.0f) || !isfinite(own)) return 0.0f;
    if (pos != kPosLowMinus && pos != kPosLow && pos != kPosR) return own;
    if (share <= 0 || !isfinite(most) || !(most > own)) return own;
    if (share >= 100) return most;
    return own + (most - own) * (float)share / 100.0f;
}

// ---- the slip room of the numbered gears (BRIEF_held_gear_slip.md with the Reviewer's addendum) ----
// HeldGearSlip, what a numbered gear held by hand gets:
//   0 Engine held   the governor and no cap, as before the setting.
//   1 Low gears     the cap of L+ on this truck (WheelCap x the top of the game's first gear at the plain factor), in the
//                   gears where that cap is at least kSlipLeast x the gear's own top. A gear past that is as at 0.
//   2 All gears     WheelCap x the gear's own top, as L+ has it.
// A numbered gear never gets the low range's wide part. L-, L, L+ and R are the same at every choice.
#ifndef NG_SLIP_CHOICES // the same line in gauges.h and gearbox_model.h: each is read without the other by some test
#define NG_SLIP_CHOICES
enum { kSlipEngineHeld, kSlipLowGears, kSlipAllGears, kSlipCount }; // GaugeSettings::heldGearSlip (gearbox_model.h, GearboxPosCap)
#endif
static const float kSlipLeast = 1.10f; // no cap is closer to its gear's top than this (torque_cut.h's kCapLeast, in percent there)
inline const char *HeldSlipName(int choice)
{
    static const char *names[kSlipCount] = { "Engine held", "Low gears", "All gears" };
    return names[choice < 0 || choice >= kSlipCount ? kSlipEngineHeld : choice];
}
// A forward gear held by hand that has the governor at choice 0: a numbered gear, and H.
inline bool GearboxPosNumbered(int pos) { return pos == kPosGear || pos == kPosH; }
// The wheel cap of the position held, in the wheels' measure; 0 none. The one place the rule is written: the slot, the
// probe and the tests call it. cap: WheelCap over 100. top: the top of the gear held. firstTop: the top of the game's
// first gear at the plain factor, which is the top of L+ whether this gearbox has an L+ or not.
inline float GearboxPosCap(int pos, int choice, float cap, float top, float firstTop)
{
    if (!(top > 0.0f) || !isfinite(top) || !(cap > 0.0f) || !isfinite(cap)) return 0.0f;
    if (GearboxPosCapped(pos)) return cap * top;
    if (!GearboxPosNumbered(pos)) return 0.0f;
    if (choice == kSlipAllGears) return cap * top;
    if (choice != kSlipLowGears) return 0.0f;
    const float low = cap * firstTop;
    return isfinite(low) && low >= kSlipLeast * top ? low : 0.0f;
}

// The choice in force, apart from the choice set (M3). A cap that comes, grows, or shrinks and stays is in force at
// once: the cap's step lowers no wheel, and the cut's own pacing takes the rise of the pull. A cap that goes is not:
// with the wheels past the governor's end the governor's pull is 0, so the pull would fall to nothing in one step. The
// old choice stays until both laws give the same pull: the pedal at 0, or the driven wheels' mean at or under the gear's
// top with the truck at or under the governor's start. Another position or gear takes the set choice at once.
struct HeldSlip { int inForce, pos, gear; };
inline int HeldSlipStep(HeldSlip &st, int set, int pos, int gear, float cap, float top, float firstTop, float pedal, float mean, float truck, float governFrom)
{
    if (st.pos != pos || st.gear != gear)
    {
        st.pos = pos, st.gear = gear;
        st.inForce = set;
        return set;
    }
    if (st.inForce == set) return set;
    const float was = GearboxPosCap(pos, st.inForce, cap, top, firstTop), now = GearboxPosCap(pos, set, cap, top, firstTop);
    const bool goes = was > 0.0f && !(now > 0.0f);
    const bool agree = !(pedal > 0.0f) || (isfinite(mean) && mean <= top && isfinite(truck) && truck <= governFrom * top);
    if (!goes || agree) st.inForce = set;
    return st.inForce;
}
// The line the tuning tool shows under the slider (31 characters at the most), and the probe's words.
inline void HeldSlipLine(char *out, size_t size, int set, int inForce, float cap)
{
    if (set != inForce) snprintf(out, size, "%s until wheels slow", HeldSlipName(inForce));
    else if (cap > 0.0f) snprintf(out, size, "%s, cap %.1f", HeldSlipName(set), cap);
    else snprintf(out, size, "%s, no cap here", HeldSlipName(set));
}
inline void HeldSlipProbe(char *out, size_t size, int set, int inForce)
{
    if (set != inForce) snprintf(out, size, "set %d, in force %d until the wheels are under the top", set, inForce);
    else snprintf(out, size, "%d (%s)", set, HeldSlipName(set));
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

inline float GearboxPosPower(const GearboxPosModel &m);

// Back in the world after the map, the garage or the viewer: the position stays when the game still has the gear the
// mod set last at the power the position asks, and a held gear within the truck's top. Read again (GearboxPosTake),
// L+ came back as a 1st and the mod's A, which runs with the game's automatic flag down, as a held gear (the 09:20
// drive of 4 November); a garage that changed the gear or the power reads again as before (review 7, R6).
inline bool GearboxPosKeeps(const GearboxPosModel &m, const GearboxTruck &t)
{
    // A waiting in N for the pedal, or a gear waiting for the truck to stop rolling back, has the game's N without setting it
    const bool waitsInN = t.gear == 0 && (m.pos == kPosA || m.waitBack);
    if (!m.running || (t.gear != m.lastSet && !waitsInN) || fabsf(t.power - GearboxPosPower(m)) > kPowerStep) return false;
    if (t.automatic && m.pos != kPosA) return false;
    return m.pos != kPosGear || (m.gear >= 1 && m.gear <= t.top);
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

// Whether the game's gear agrees in sign with a position (F2 of BRIEF_drive_2108_findings.md): R and a gear below 0, a
// forward position (A, a numbered gear, H, the low range) and a gear above 0. N and the game's neutral never agree, so
// the steps while a shift lands pass the game's own torque.
inline bool GearboxPosAgrees(const GearboxPosModel &m, int gameGear)
{
    if (m.pos == kPosN || gameGear == 0) return false;
    return m.pos == kPosR ? gameGear < 0 : gameGear > 0;
}

inline float GearboxPosPower(const GearboxPosModel &m)
{
    return m.pos >= kPosLowMinus && m.pos <= kPosLowPlus ? kLowRangePower[m.pos - kPosLowMinus] : m.power;
}

// Where a position stands in the row up and down walk, as SMT's did: R -1, N 0, L- 1, L 2, L+ 3, a numbered gear g at
// g + 3, A at the game's gear (at least 1) + 3, H at the top + 4 (so down from H lands on the top; review 7, R2).
static const int kWalkLowMinus = 1, kWalkFirst = 4; // L-'s place and the first gear's
inline int GearboxPosWalk(const GearboxPosModel &m, const GearboxTruck &t)
{
    switch (m.pos)
    {
    case kPosGear: return m.gear + 3;
    case kPosN: return 0;
    case kPosR: return -1;
    case kPosH: return t.top + 4;
    case kPosLowMinus: return 1;
    case kPosLow: return 2;
    case kPosLowPlus: return 3;
    default: return (t.gear < 1 ? 1 : t.gear > t.top ? t.top : t.gear) + 3;
    }
}

// The position at a place in the walk (a numbered gear capped at the top).
inline void GearboxPosFromWalk(GearboxPosModel &m, int i, int top)
{
    static const int low[4] = { kPosN, kPosLowMinus, kPosLow, kPosLowPlus };
    if (i < 0) m.pos = kPosR;
    else if (i < kWalkFirst) m.pos = low[i];
    else
    {
        m.pos = kPosGear;
        m.gear = i - 3 > top ? top : i - 3;
    }
}

// Whether the walk stops at place i on a press from the position fromPos: R, the gears and H always, N unless
// SkipNeutral, a low place unless WalkSkipsLow and the press came from outside the low range (BRIEF_sequential_no_low.md)
// or the truck has no such position (missing, kHas... bits; BRIEF_positions_by_gearbox.md).
inline bool GearboxWalkStops(int i, int fromPos, const GearboxOptions &o, int missing = 0)
{
    static const int has[3] = { kHasLowMinus, kHasL, kHasLowPlus };
    if (i == 0) return !o.skipNeutral;
    if (i >= kWalkLowMinus && i < kWalkFirst)
        return !(missing & has[i - kWalkLowMinus]) && (!o.walkSkipsLow || (fromPos >= kPosLowMinus && fromPos <= kPosLowPlus));
    return true;
}

// The place one press moves to from place i (dir 1 up, -1 down), past the places the walk does not stop at, held to R
// and the top gear.
inline int GearboxWalkNext(int i, int dir, int fromPos, const GearboxOptions &o, int last, int missing = 0)
{
    int to = i;
    do to += dir; while (to > -1 && to < kWalkFirst && !GearboxWalkStops(to, fromPos, o, missing));
    return to < -1 ? -1 : to > last ? last : to;
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

// One pass of dt seconds since the last one (the time in L+ counts by it). autoGear: the gear the mod's automatic wants
// (A's gear).
inline GearboxPosOrders GearboxPosStep(GearboxPosModel &m, const GearboxOptions &o, const GearboxActions &a, const GearboxTruck &t, int autoGear, float dt)
{
    GearboxPosOrders r = {};
    r.why = "";
    if (!m.running) GearboxPosTake(m, t);
    const int top = t.top < 1 ? 1 : t.top;
    m.clutch = a.clutch || (a.clutchTravel >= 0.5f);
    m.travel = a.clutchTravel >= 0.0f ? (a.clutchTravel > 1.0f ? 1.0f : a.clutchTravel) : (a.clutch ? 1.0f : 0.0f);
    // the time since the last pass counts as time in L+ if the truck was there
    if (m.pos == kPosLowPlus) m.lowPlusFor += dt;
    // up from L+ goes to the 1st once the truck has been in L+ kLowRangeUpHold (a hair under, for the sum of the passes)
    const bool lowUpWaits = m.pos == kPosLowPlus && m.lowPlusFor < kLowRangeUpHold - 1e-4f;
    // the position the actions pick: a position key first, then a gear key, then up or down, then the range keys
    const int was = m.pos, wasGear = m.gear;
    // where up or down would go, worked out first so a refused press still falls through to the range keys
    const int walkAt = GearboxPosWalk(m, t), last = top + 3;
    int walkTo = GearboxWalkNext(walkAt, a.up ? 1 : -1, m.pos, o, last, t.missing);
    // down from A never lands on N or R: with WalkSkipsLow A in the 1st shares the 1st's place, and a down press in D
    // asks for a lower gear held, so it gives the 1st held (review S1; with the option off A never reaches N this way)
    if (!a.up && m.pos == kPosA && walkTo <= 0) walkTo = kWalkFirst;
    // up from H stays in H, and up from L+ stays in the low range for kLowRangeUpHold after an up press brought it there:
    // L+ and the 1st are the same gear at the same power, so a quick run of presses walked on into the 1st unseen
    // (the user, 4 November: "It should never go to D from low range"). A press of its own goes to the 1st (the user,
    // 5 November: "i cant shift out of L+ into 1"); a refused press does not restart the hold. DownStaysForward: a down
    // press that would land on N or R does nothing
    const bool walkRefused = a.up ? (m.pos == kPosH || lowUpWaits) : (o.downStaysForward && walkTo <= 0);
    // a key for a position the gearbox lacks does nothing, and Range low needs L (BRIEF_positions_by_gearbox.md); the
    // game's shifter is never refused: it can give only what the game's gearbox has, so a position of its own that the
    // read lacks says the read is wrong for this truck (review S4)
    auto pick = [&](int pos) {
        if (t.missing & GearboxPosHasBit(pos))
        {
            if (!a.shifter)
            {
                r.keyRefused = true;
                r.refusedPos = pos;
                return;
            }
            r.shifterMissing = true;
            r.refusedPos = pos;
        }
        m.pos = pos;
    };
    if (a.a) m.pos = kPosA;
    else if (a.n) m.pos = kPosN;
    else if (a.r) m.pos = kPosR;
    else if (a.h) pick(kPosH);
    else if (a.lowMinus) pick(kPosLowMinus);
    else if (a.low) pick(kPosLow);
    else if (a.lowPlus) pick(kPosLowPlus);
    else if (a.gearPressed >= 1) GearboxPosToGear(m, a.gearPressed > top ? top : a.gearPressed);
    else if ((a.up || a.down) && !walkRefused) GearboxPosFromWalk(m, walkTo, top);
    else if (a.rangeLow && !(m.pos >= kPosLowMinus && m.pos <= kPosLowPlus)) pick(kPosLow);
    else if (a.rangeHigh && m.pos >= kPosLowMinus && m.pos <= kPosLowPlus) GearboxPosToGear(m, 1);
    // standing in a position the gearbox lacks (the box ticked while there, a read that came late, a truck parked in
    // the mod's L- before the update): L- and L+ go to L where it is there, else to the 1st held, L to the 1st held and
    // H to the top gear. Never a position the game's shifter gave (review S4)
    if ((t.missing & GearboxPosHasBit(m.pos)) && !m.byShifter && !a.shifter)
    {
        r.fittedFrom = m.pos;
        r.fitted = true;
        if ((m.pos == kPosLowMinus || m.pos == kPosLowPlus) && !(t.missing & kHasL)) m.pos = kPosLow;
        else if (m.pos == kPosH) GearboxPosToGear(m, top);
        else GearboxPosToGear(m, 1);
    }
    r.lowUp = a.up && was == kPosLowPlus && m.pos == kPosLowPlus;
    r.lowUpSay = r.lowUp && !m.lowRefusedSaid;
    r.lowUpAfter = m.lowPlusFor;
    if (r.lowUp) m.lowRefusedSaid = true;
    // into L+: the hold starts on an up press from L, else up goes at once (down from the 1st, the L+ key, the shifter)
    if (m.pos == kPosLowPlus && was != kPosLowPlus)
    {
        m.lowPlusFor = a.up && was == kPosLow ? 0.0f : kLowRangeUpHold;
        m.lowRefusedSaid = false;
    }
    // a 1st reached from the low range (up from L+, RangeHigh or the key 1) stays held until the player's next shift:
    // up, down, A, a gear key or any other move end it (the lead's calls on BRIEF_low_range_up_to_1.md, so the low range
    // never goes on to D by itself). A gear key above 1 from the low range gives an ordinary held gear
    r.lowToFirst = was >= kPosLowMinus && was <= kPosLowPlus && m.pos == kPosGear && m.gear == 1;
    if (r.lowToFirst) m.heldFromLow = true;
    else if (a.up || a.down || a.a || a.gearPressed >= 1 || m.pos != was || m.gear != wasGear) m.heldFromLow = false;
    // the game's shifter stays where it was put, so a gear picked there is held; a key's position is not
    if (a.shifter) m.byShifter = true;
    else if (m.pos != was || m.gear != wasGear) m.byShifter = false;
    // an H shifter: neutral the moment no gear key is held
    if (o.requireGearHeld && m.pos == kPosGear && a.gearHeld < 1 && !m.byShifter) m.pos = kPosN;
    r.moved = m.pos != was || (m.pos == kPosGear && m.gear != wasGear);
    if (r.moved) m.refusedSaid = false;
    // with RequireClutch a gear picked without the clutch while the truck rolls at kGrindSpeed or more does not go in: it
    // grinds and stays out, as a running truck's does unless the speeds match (review 11: it stalled the engine at any
    // speed). Standing or crawling the gear is forced in and stalls the engine (below). A fit (a position the gearbox
    // lacks, moved to the nearest it has) is not the player's shift and goes in as it is (the review of 108, S1)
    const bool clutchless = o.requireClutch && r.moved && !r.fitted && m.pos != kPosN && m.pos != kPosA && !m.clutch && t.engineOn && !(t.stall > 0.0f);
    if (clutchless && fabsf(t.speed) >= kGrindSpeed)
    {
        m.pos = kPosN;
        r.why = "a shift without the clutch at speed: the gear grinds and stays out, neutral";
    }
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
    if (m.pos != kPosGear) m.heldFromLow = false; // a grind to N or RequireGearHeld's N ends it too
    r.flagDown = t.automatic;
    // a shift by the player into a gear without the clutch, standing or crawling, stalls the engine once, not into
    // neutral, and not when the engine is already dying or off
    if (clutchless && fabsf(t.speed) < kGrindSpeed)
    {
        r.stall = true;
        m.stalls++;
        r.why = "a shift without the clutch stalled the engine";
    }
    return r;
}

// The positions of a gearbox that lacks the kHas... bits missing, in words: "L-, L, L+ and H", "L and H", "none".
inline void GearboxPosHasText(int missing, char *out, size_t size)
{
    const char *names[4];
    int n = 0;
    if (!(missing & kHasLowMinus) && !(missing & kHasL)) names[n++] = "L-";
    if (!(missing & kHasL)) names[n++] = "L";
    if (!(missing & kHasLowPlus) && !(missing & kHasL)) names[n++] = "L+";
    if (!(missing & kHasH)) names[n++] = "H";
    if (!n) snprintf(out, size, "none of L-, L, L+ and H");
    else if (n == 1) snprintf(out, size, "%s", names[0]);
    else if (n == 2) snprintf(out, size, "%s and %s", names[0], names[1]);
    else if (n == 3) snprintf(out, size, "%s, %s and %s", names[0], names[1], names[2]);
    else snprintf(out, size, "%s, %s, %s and %s", names[0], names[1], names[2], names[3]);
}

// The gearbox in a line of text for the probe log. missing: the positions the fitted gearbox lacks (kHas... bits).
inline void GearboxPosProbeText(const GearboxPosModel &m, const GearboxOptions &o, int gear, char *out, size_t size, int missing = 0)
{
    char at[24] = "", has[40];
    if (m.pos == kPosGear) snprintf(at, sizeof at, " %d", m.gear);
    GearboxPosHasText(missing, has, sizeof has);
    snprintf(out, size, "gearbox: %s%s (the game's gear %d), clutch %s at %.2f, %d shifts, %d stalls | RequireClutch %d, RequireGearHeld %d, SkipNeutral %d, DownStaysForward %d, WalkSkipsLow %d | has %s",
             GearboxPosName(m.pos), at, gear, m.clutch ? "held" : "let up", m.travel, m.shifts, m.stalls, o.requireClutch, o.requireGearHeld, o.skipNeutral,
             o.downStaysForward, o.walkSkipsLow, has);
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
