// gearbox_hooks.h: the game's gearbox functions (game_code.h) hooked through Microsoft Detours (src\detours), the
// ground for the mod's own gearbox. The method is the manual transmission mod's (SMT, by drafty46, MIT; the game's
// functions reverse engineered by Ferrster): a detour on each function, attached in one transaction and detached in
// one. Every hook counts its calls for the probe and, unless the policy below refuses the call, calls the game's own
// function through with its arguments unchanged and returns its result. GetMaxGear is only ever called, never hooked.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include "game_code.h"
#include "gearbox_model.h"
#include "torque_cut.h"
#include "physics_rate.h"

// The calls each hook has seen since the attach, and the last arguments of those that have some.
struct GearboxCounts
{
    long shift, autoGear, high, reverse, neutral, autoOff, power, vehicle;
    int shiftGear, autoOffGear; // the last gear asked of ShiftGear and of AutoOffAndShift
    float factor;               // the last factor given to SetPowerFactor
    bool vehicleSet;            // the last SetCurrentVehicle set a truck (false: cleared it)
    long refused[kFnCount];     // the game's calls refused while the mod's gearbox had the truck (GearboxHolding), by kFn...
};

// Attaches n detours in one transaction: each targets[i] (a function's address, set to the way through to it on
// success) to hooks[i]. NO_ERROR, or the error of the first that failed, and then none is attached.
long GearboxAttach(void **targets[], void *const hooks[], int n);
// The same with no other thread suspended: for functions no other thread runs while the mod starts (the exit calls,
// BRIEF_crash_line_early.md), as a thread suspended while it holds the heap's lock would hang the game at its boot.
long GearboxAttachAlone(void **targets[], void *const hooks[], int n);
// Detaches them in one transaction; each targets[i] holds the function's own address again.
long GearboxDetach(void **targets[], void *const hooks[], int n);

// Hooks the game's gearbox functions the finder found once and not held by another mod, at base (the exe's). why:
// one line per function that is not hooked, and why. The number hooked; 0 when the transaction failed.
int GearboxHook(uintptr_t base, const FunctionFind finds[kFnCount], char *why, size_t size);
// Takes every hook out again. The mod never unloads (it pins itself), so only the test calls it.
void GearboxUnhook();
bool GearboxHooked();
void GearboxCountsGet(GearboxCounts &out);
// The policy, by the level of Gearbox. kPolicyPass: everything passes (Gearbox=0, or the truck not held).
// kPolicyBrakeReverse (Gearbox=1, the mod's automatic in A): only the game's reverse while the truck stands in A with
// the brake down (brakeStand) and the game's automatic flag up (read through autoFlag at the call: it is up only for a
// moment, after the shift modifier is tapped or A picked again, before the mod puts it down) is refused, at whichever
// door it comes (ShiftToReverse, or a gear below 0 through AutoOffAndShift or ShiftGear). With the flag down a reverse
// is the player's (the shifter's R, through ShiftToReverse) and passes. kPolicyAll (Gearbox=2, the mod's own gearbox): the game's own calls of ShiftGear,
// ShiftToAutoGear, ShiftToHigh, ShiftToReverse, ShiftToNeutral, AutoOffAndShift and SetPowerFactor are refused. A
// refused call is not passed to the game's function (false, or nothing) and is counted; SetCurrentVehicle always
// passes. As the counts nest (reverse and neutral go through auto off, auto off through ShiftGear), each is refused at
// its own door. The mod's own calls go through GearboxOriginal and are never refused. vehicle: the held truck
// (TruckRefs::vehicle): only its calls are refused. On for another truck, it tells each kind afresh through
// GearboxRefusedNew (1 << kFn..., or kRefusedBrakeReverse); off and on again for the same truck (the garage, the
// map), it does not.
enum { kPolicyPass, kPolicyBrakeReverse, kPolicyAll };
static const unsigned kRefusedBrakeReverse = 1u << 30;
void GearboxPolicy(int policy, void *vehicle, bool brakeStand, const volatile uint8_t *autoFlag = nullptr);
int GearboxPolicyNow();
// The hook (kFn...) that carried the last reverse refused on the brake, -1 for none yet: the outermost of the three
// doors, as a refused call never reaches the ones nested in it.
int GearboxBrakeDoor();
bool GearboxHoldingNow();
unsigned GearboxRefusedNew();
// The way through to the game's own function behind a hook (fn one of kFn*), nullptr while it has none: the mod's own
// calls go this way, so the probe counts only the game's.
void *GearboxOriginal(int fn);

// ---- the game's shifter as a request (Gearbox=2 with a pad or the keyboard, 0.17.1) ----
// With the game's automatic flag down the game makes no gearbox call of its own at a standstill (the drives of 26
// October), so a call refused at kPolicyAll is the player's shifter. The calls stay refused (the mod's gearbox shifts
// through GearboxOriginal, so nothing is shifted twice), but the door the first one came through names the position:
// ShiftToReverse R, ShiftToNeutral N, ShiftToHigh H, ShiftToAutoGear A, a gear through AutoOffAndShift or ShiftGear (below
// 0 R, 0 N); the later calls of the same shift repeat it, and SetPowerFactor's factor under kLowUnder turns a first gear
// into L- or L. pending: the request not yet taken (kind kAskNone for none).
inline GearboxAsk GearboxAskMerge(GearboxAsk pending, int fn, int32_t gear, float factor)
{
    if (pending.kind == kAskGear && pending.gear == 1 && fn == kFnSetPower && factor > 0.0f && factor < kLowUnder)
        return GearboxAsk{ factor < kLowMinusUnder ? kAskLowMinus : kAskLow, 0 };
    if (pending.kind != kAskNone) return pending;
    switch (fn)
    {
    case kFnShiftToReverse: return GearboxAsk{ kAskR, 0 };
    case kFnShiftToNeutral: return GearboxAsk{ kAskN, 0 };
    case kFnShiftToHigh: return GearboxAsk{ kAskH, 0 };
    case kFnShiftToAuto: return GearboxAsk{ kAskA, 0 };
    case kFnShiftGear:
    case kFnAutoOffAndShift: return gear < 0 ? GearboxAsk{ kAskR, 0 } : gear == 0 ? GearboxAsk{ kAskN, 0 } : GearboxAsk{ kAskGear, gear > 255 ? 255 : gear };
    default: return pending;
    }
}
// On (the mod's thread, by Input: a pad or the keyboard), the calls refused at kPolicyAll are read as requests.
void GearboxShifterAsks(bool on);
// Once a pass on the mod's thread: the request the game's shifter made since the last pass, taken and cleared. A
// request older than two passes is dropped; one made on this very pass waits for the next. False for none.
bool GearboxAskTake(GearboxAsk &out);

// ---- the hand-back at Gearbox=1, by the door (0.17.1) ----
// A position picked on the game's shifter comes through one of four doors: AutoOffAndShift (a gear, the low range
// among them), ShiftToHigh, ShiftToNeutral or ShiftToReverse. Each counts the calls that passed it (refused ones left
// out). The mod's own shifts go through GearboxOriginal and are not counted, and a bare ShiftGear is no pick.
struct GearboxDoors { long autoOff, high, neutral, reverse; };
inline GearboxDoors GearboxDoorsOf(const GearboxCounts &c)
{
    return GearboxDoors{ c.autoOff - c.refused[kFnAutoOffAndShift], c.high - c.refused[kFnShiftToHigh], c.neutral - c.refused[kFnShiftToNeutral],
                         c.reverse - c.refused[kFnShiftToReverse] };
}
// The mod's automatic in A (inA) hands the truck back when a door passed a call since the last pass (was) with the
// game's automatic flag down: a pick of the player's, whatever gear it names. L+ is the first gear with the plain
// power, as the automatic's own first is, so the gear could not tell them apart. With the flag up a call is the
// game's own automatic's.
inline bool GearboxHandsBack(const GearboxDoors &was, const GearboxDoors &now, bool inA, bool flagUp)
{
    return inA && !flagUp && (now.autoOff != was.autoOff || now.high != was.high || now.neutral != was.neutral || now.reverse != was.reverse);
}
// The position a hand-back names, by its door and then the gear and the power factor as TruckGearMode reads the low
// range: "R", "N", "H", "L-", "L", "L+", or nullptr for a numbered gear.
inline const char *GearboxDoorPos(const GearboxDoors &was, const GearboxDoors &now, int gear, float power)
{
    if (now.reverse != was.reverse || gear < 0) return "R";
    if (now.neutral != was.neutral || gear == 0) return "N";
    if (now.high != was.high) return "H";
    if (gear == 1) return power > 0.0f && power < kLowMinusUnder ? "L-" : power > 0.0f && power < kLowUnder ? "L" : "L+";
    return nullptr;
}

// ---- the gate: which hooks a level needs (0.17.0) ----
// The eight shift hooks at Gearbox 1 and 2 (level as GaugeSettings::gearbox: 0 the game's, 1 the mod's automatic, 2
// the mod's own gearbox), the truck update's at 2 with Damage on (the stall's damage and the clutch's burn go through
// it). At Gearbox=0 none of them; the creep's hook on the wheels' drive (CreepHook) goes by Creep alone, at every level.
struct HooksWanted { bool shifts, truckUpdate; };
// The truck update also whenever the mod's gearbox may hold a truck (Gearbox 1 or 2) with ShiftOnGameThread=1: the
// mod's shifts are dealt there (review 17, step 1); and at Gearbox=2 with PositionsByGearbox, whose read of the fitted
// gearbox is a job there (BRIEF_positions_by_gearbox.md, review M1).
inline HooksWanted GearboxHooksWanted(int level, bool damage, bool shiftJobs = false, bool positions = false)
{
    return HooksWanted{ level >= 1, (level >= 2 && (damage || positions)) || (level >= 1 && shiftJobs) };
}

// What has been asked of the hooks so far. A hook is attached the first time a level needs it (at the finder's look,
// or later when the tab turns it on) and never detached while the game runs: a level that no longer needs it leaves
// it in place, said once each time.
struct HooksGate { bool shifts, truckUpdate, shiftsLeftSaid, truckLeftSaid; };
enum { kGateAttachShifts = 1, kGateAttachTruck = 2, kGateShiftsLeft = 4, kGateTruckLeft = 8 };
// One step with what the settings want now: the bits of kGate... for the caller to act on (attach, or say "left in
// place"); each attach is asked once.
inline int GearboxGateStep(HooksGate &g, const HooksWanted &w)
{
    int act = 0;
    if (w.shifts && !g.shifts) act |= kGateAttachShifts;
    if (w.truckUpdate && !g.truckUpdate) act |= kGateAttachTruck;
    if (w.shifts)
    {
        g.shifts = true;
        g.shiftsLeftSaid = false;
    }
    else if (g.shifts && !g.shiftsLeftSaid)
    {
        act |= kGateShiftsLeft;
        g.shiftsLeftSaid = true;
    }
    if (w.truckUpdate)
    {
        g.truckUpdate = true;
        g.truckLeftSaid = false;
    }
    else if (g.truckUpdate && !g.truckLeftSaid)
    {
        act |= kGateTruckLeft;
        g.truckLeftSaid = true;
    }
    return act;
}

// The game's truck update (game_code.h, DamagePathFind) detoured at address at, in a transaction of its own: on the
// game's thread, every frame and before the update runs, onFrame gets its first argument (an object with the vehicle
// at + 0x20). False with why when Detours refused it; true at once when it is in place already.
typedef void (*TruckFrameFn)(void *first);
bool TruckUpdateHook(uintptr_t at, TruckFrameFn onFrame, char *why, size_t size);
void TruckUpdateUnhook();
bool TruckUpdateHooked();
long TruckUpdateFrames(); // the frames the hook has seen

// The manual shift rebuild's probe (review 17, step 0): the thread each hook last ran on (0 none yet), by kFn..., and
// the truck update's and the wheels' drive's.
void GearboxThreadsGet(DWORD out[kFnCount], DWORD &truckUpdate, DWORD &drive);
// The action's fields + 0x38 to + 0xD0 just before and after a call of the game's own ShiftGear, AutoOffAndShift,
// SetPowerFactor or ShiftToAuto that passed through (not refused), while on: does it do more than set its field? seq
// grows with each look; 0 none yet.
static const int kFieldLookBytes = 0x98;
struct GearboxFieldLook { long seq; float factor; int gear; uint8_t before[kFieldLookBytes], after[kFieldLookBytes]; };
void GearboxFieldProbe(bool on);
long GearboxFieldLookGet(int fn, GearboxFieldLook &out);

// The game's wheels' drive (game_code.h, DrivePlaceFind) detoured at address at to a function of its own signature, in
// a transaction of its own, the first time Creep or Probe is on with a truck driven; never taken out while the game
// runs. False with why when Detours refused it; true at once when it is in place already.
bool CreepHook(uintptr_t at, char *why, size_t size);
bool CreepHooked();
// Called by the hook on the physics thread for every call, before the drive, with the call's action and the step's
// seconds (suspension_step.h, SuspStepLook); nullptr for none. It must not log, lock, allocate or fault.
typedef void (*DriveStepFn)(uint64_t action, float dt);
void DriveStepSet(DriveStepFn step);
// The share of the drive throttle the hook gives the truck whose action this is (models.h, CreepShare): the larger of
// the game's and this. An action of 0 or a share of 0 lets every call pass as it came. While the torque cut has a slot
// published for an action (TorqueCutPublish, at most kTorqueFresh old), the hook gives that action's drive a throttle
// of 1 and writes its B4 (action + 0xB4) as 1 before the drive runs, on the physics thread in the same step (the
// Reviewer's S2): the cut's torque carries the pedal, the sign and the size, and the drive's own update keeps B4 at 1
// (its target is the throttle once the curve is flat), zeroed as ever while + 0xD8 is up.
void CreepGive(uint64_t action, float share);
// The chassis push (BRIEF_chassis_push.md): while the mod writes the torque factor for this action (not under the cut),
// the game's pull the factor assumed (GameFall at its x times the pedal), the factor over the game's own share and
// 1 - PushShare / 100; the drive's hook takes back k of each parent body's change across the drive (PushTakeBack).
// back 0 or no action clears it; a publish older than kTorqueFresh passes.
void PushGive(uint64_t action, uint64_t vehicle, float assumed, float factorOver, float back);
// Its counters since the start: the drive's steps measured, those with a take-back, the steps whose push came under 3 m/s
// (the kick path), faults, bodies skipped (an inverse mass of 0); the push given and taken back summed (N s); the last
// step's k, F_excess, F, B4 and forward speed (m/s).
struct PushCounts
{
    long steps, backs, kicks, faults, skipped;
    float given, back, k, fx, f, b4, speed;
};
void PushCountsGet(PushCounts &out);
// The diff lock's bite (BRIEF_diff_lock_bite.md; torque_cut.h, LockStepHeld): the driven truck's action and vehicle,
// DiffLockBite (1 to 100), the lock kind read (vehicle + 0xE8), the truck's mass by its files (0 not known) and
// hkpCylinderShape's vtable (0 not found), published while it may bite; bite 0 or no action clears it; a publish older
// than kTorqueFresh passes. In the drive's hook, while the lock is in force (DiffLockInForce: the lock byte at
// action + 0x4A set and the handbrake at action + 0x48 not, or the cut's slot's type Locked), the driven wheels' spins
// about their axles move toward their mean, weighted by what each has behind it, after the drive and before the wheel cap.
void LockGive(uint64_t action, uint64_t vehicle, int bite, int kind, float mass = 0.0f, uint64_t cylinderVtable = 0);
// Its counters since the start: the steps the slot named the action, those with the lock byte off, those held by the
// handbrake, those written, faults, wheels skipped (no inertia); the last written step's wheels, k and spread after the
// drive and after the lock (rad/s), the vehicle it was for, and that step's weighted mean before the drive (c), the
// impulse handed to the wheels on the ground (r) and the fastest wheel after the drive over c (over).
struct LockCounts
{
    long calls, off, handbrake, writes, faults, skipped;
    int wheels;
    float k, spreadBefore, spreadAfter;
    uint64_t vehicle;
    float c, r, over;
};
void LockCountsGet(LockCounts &out);
// The differential's type in the drive's hook (BRIEF_diff_types.md): the steps Open's evening ran, those it wrote and
// its faults since the start; the last one's wheels and impulse J; and what was in force in the last step of the cut's
// action (kDiffForceOpen, kDiffForceLimited or kDiffForceLock).
struct DiffCounts
{
    long steps, writes, faults;
    int wheels;
    float j;
    int force;
    // Open by axle: the truck the axles were made for, a count that goes up when they are made anew, the axles found, the
    // wheels alone and the wheels of the truck; the reads that faulted; and of the last Open step the first kDiffAxleSay
    // axles: the mean of each after the step, its factor and its wheels (a bit each by the place in the truck's list)
    uint64_t axleVehicle;
    long axleGen, axleFaults;
    int axles, alone, axleWheels;
    int sayN;
    float sayMean[8], sayK[8], sayGot[8]; // sayGot: the speed handed to the axle from the others, summed over its wheels
    unsigned sayWheels[8];
};
enum { kDiffAxleSay = 8 };
void DiffCountsGet(DiffCounts &out);
// The hook's calls so far: for the action given (hits) and all of them.
void CreepCounts(long &hits, long &calls);
// The physics rate test mode (physics_rate.h). on: every call of the game's drive goes with the action's two figures
// a step halved around it (RateHalveBefore and After, under the hook's fault guard), the kept share's smoothing goes
// by the step's seconds, and the hook looks for the world: ownerSlot is the address of the static the world's owner
// hangs on (0: no look), iterations what the solver was asked. Off (as at the start) the hook writes nothing of this
// and reads nothing of it. Set once at the start, before the game's code is written.
void DriveRateSet(bool on, uint64_t ownerSlot, int iterations);
bool DriveRateOn();
// What the hook saw: worlds grows with each new world pointer at a drive call; that world, the step's seconds as the
// world holds them (bits) and its solver's iterations, read at that call; the seconds the call itself came with; the
// reads that faulted (the halving's and the world's).
struct DriveRateSeen
{
    long worlds;
    uint64_t world;
    uint32_t dtBits;
    int iterations;
    float hookDt;
    long faults;
};
void DriveRateSeenGet(DriveRateSeen &out);
// The threads the drive was called on so far, at most most of them; full where more were seen than are kept.
int DriveThreadsGet(DWORD *out, int most, bool &full);

// ---- the game's engine speed (game_code.h, EngineSpeedFind; BRIEF_drivetrain_1_engine_speed.md) ----
// The mod's thread publishes one value, the engine speed share 0 to 1 for one vehicle (models.h, EngineSpeedShare), in
// one of two slots, as the creep's share is published. The hook, on whatever thread the game calls from, answers with it
// only when all of these hold (EngineSpeedOurs): the call is not the fuel's or the stall's (they keep the game's own,
// the Reviewer's M1 and M2), a slot is published, for this vehicle, its engine is on (vehicle + 0x768 bit 0, read by the
// hook itself, as the game's first bytes read it) and the slot is at most kEngineSpeedFresh ms old. Else it calls the
// game's own through, so a cleared slot, a stale one or another vehicle gets exactly the game's value.
static const uint64_t kEngineSpeedFresh = 1000;
struct EngineSpeedSlot
{
    const void *vehicle;
    float share;
    uint32_t spare;
    uint64_t at; // GetTickCount64 at the publish
};
inline bool EngineSpeedOurs(uintptr_t back, uintptr_t fuelBack, uintptr_t stallBack, const EngineSpeedSlot *slot, const void *vehicle, bool engineOn, uint64_t now)
{
    return slot && vehicle && slot->vehicle == vehicle && engineOn && back != fuelBack && back != stallBack && now >= slot->at && now - slot->at <= kEngineSpeedFresh;
}
// Hooks the game's engine speed at address at (all other threads suspended, GearboxAttach), the calls returning to
// fuelBack and stallBack passed through. Never taken out while the game runs: with nothing published the hook calls
// through. False with why when Detours refused it; true at once when it is in place already.
bool EngineSpeedHook(uintptr_t at, uintptr_t fuelBack, uintptr_t stallBack, char *why, size_t size);
bool EngineSpeedHooked();
// The share for vehicle (nullptr clears the slot: a truck switch, Powertrain off, the mod not driving it). at: the
// publish's tick, 0 for now (a test gives an old one).
void EngineSpeedPublish(const void *vehicle, float share, uint64_t at = 0);
// With the probe on, the hook calls the game's own first on every call and keeps, by the address each call returns
// to, the count and the last value the game gave (the Reviewer's S3). At most kSpeedCallers callers; full past them.
void EngineSpeedProbe(bool on);
static const int kSpeedCallers = 24;
struct EngineSpeedCaller { uintptr_t back; long calls; float stock; };
int EngineSpeedCallersGet(EngineSpeedCaller *out, int most, bool &full);
// The calls answered with the mod's value so far, and the last value answered.
void EngineSpeedAnswers(long &answered, float &last);

// ---- the engine sound (game_code.h, TruckInputFind; BRIEF_drivetrain_1b_sound.md with the Reviewer's addenda) ----
// The game's engine sound reads TruckInput.velocityFactor (the data block + 0x48), which the game fills from the chassis
// speed's place in its stock gear band: 0 at a gear's bottom, 1 at its top, 0.5 in N. A detour on Truck.GetTruckInput
// calls the game's own, then, while the engine speed's slot (EngineSpeedPublish) names this vehicle, is fresh and its
// engine is on, resolves the handle in *out through the game's own helper (never a mask of its own: the helper knows
// the object's kinds) and writes SoundVelocityFactor there. Nothing else is written; gear stays the game's.
// SoundVelocityFactor: the engine speed share (0 at idle, 1 at the redline x EngineSpeedTop, before the stall's dip)
// with the dip the needle gets, (1 - 0.714 x stall)^2 (models.h kStallRate; the stall seconds at vehicle + 0x11C), and
// in N (the data block's gear 0) 0.5 + 0.5 x it, so N idles at the game's own 0.5 and a rev rises to 1 at the redline.
inline float SoundVelocityFactor(float share, int gear, float stall)
{
    float s = isfinite(share) ? fminf(fmaxf(share, 0.0f), 1.0f) : 0.0f;
    const float alive = isfinite(stall) ? fminf(fmaxf(1.0f - 0.714f * stall, 0.0f), 1.0f) : 1.0f;
    s *= alive * alive;
    return gear == 0 ? 0.5f + 0.5f * s : s;
}
// Hooks Truck.GetTruckInput at address at (all other threads suspended, GearboxAttach), its handles resolved by the
// helper at helper. Never taken out while the game runs: with nothing published every call keeps the game's own. False
// with why when Detours refused it; true at once when it is in place already.
bool TruckInputHook(uintptr_t at, uintptr_t helper, char *why, size_t size);
bool TruckInputHooked();
// The calls so far, those written, those not ours (another vehicle, no slot, a stale one, the engine off) and those whose
// handle the helper did not resolve (or whose memory faulted); the last value the game gave, the last written and the
// gear seen with them.
struct TruckInputCounts
{
    long calls, written, notOurs, failed;
    float stock, ours;
    int gear;
};
void TruckInputCountsGet(TruckInputCounts &out);
// While the torque cut holds the drive's throttle at 1 the game's B4 sits at 1 at any pedal, and its fuel burn and its
// damaged gearbox's stall read B4 through the fuel's and the stall's calls: the fuel's is answered with the game's own
// value times the stand-in / B4 (0 where B4 is 0), the stall's by the game's own formula with the stand-in in B4's place
// (TorqueCutStallAnswer). The stand-in (TorqueCutStandIn, the cut's own pull) and the stall's wheels' term are what the
// cut's detour keeps for the vehicle (TorqueCutPullGet).

// ---- the torque cut (game_code.h, GearTorqueFind; torque_cut.h; BRIEF_drivetrain_2_torque_cut.md) ----
// A detour on the game's gear torque, attached once (all other threads suspended) and never taken out: the drive's call
// (returning to driveBack) answered by TorqueCutAnswerOf while a slot is published for its vehicle (TorqueCutOurs), the
// wheels read in the detour from the vehicle's own list ([[vehicle + 0x200] + i x 8] + 0x2C8: the type at + 0xE8, driven
// at 1, or 2 with all wheel drive at action + 0x49; the speed at + 0x16C) under __try, so the speed part is this step's.
// Every other caller (the stall's, GetTruckInput's, the automatic's), another vehicle, a stale slot and the game's own
// false get the game's own outputs. No log, lock or heap in the detour; counters and the last figures for the probe.
bool GearTorqueHook(uintptr_t at, uintptr_t driveBack, char *why, size_t size);
bool GearTorqueHooked();
// The slot for the driven truck, copied into one of two and published whole; nullptr clears it (every call passes, the
// throttle hold off). slot->at 0 stamps it now.
void TorqueCutPublish(const TorqueCutSlot *slot);
// The last B4s the detour worked out for vehicle, when at most kTorqueFresh old.
// The stand-in for B4 (TorqueCutStandIn), the stall's wheels' term and the pedal of the cut's last answer for the vehicle,
// at most kTorqueFresh old; false otherwise.
struct TorqueCutPull
{
    float standIn, w, pedal;
};
bool TorqueCutPullGet(uint64_t vehicle, TorqueCutPull &out);
struct TorqueCutCounts
{
    long answered, passed, others, faults, falls; // the drive's calls answered and passed, the other callers', the wheels' reads that failed, the falls of the bracket by 0.3 or more in one step
    float s, f, bracket, stock, torque, d4, b4s, stockCurve, standIn, w, used, rate, truck; // the last answer's (truck the truck's own speed the governor went by, NaN not read) (used the bracket paced, rate its pace a step) (b4s the game's settled pull, for the probe; standIn what B4's readers get)
    float radius, radius94;                 // the last answer's rolling radius used for truck and the driven wheels' + 0x94 by the same mean, m (NaN not read)
    long rollCylinder, rollNone, rollOut;   // its driven wheels by the cylinder, without one (+ 0x94) and with one out of bounds (+ 0x94) (WheelRollRadius)
};
void TorqueCutCountsGet(TorqueCutCounts &out);
// The wheel speed cap (BRIEF_drivetrain_2c_wheel_cap.md), in the wheels' drive's hook while the cut acts with a cap in
// its slot: the counters since the start (clamps: wheels held at the cap; steps capped; bodies refused; steps armed and
// waiting, M3; reads that faulted), the wheels clamped in the last step, armed in it, the kept share smoothed (M1), the
// speed spread to the other wheels, summed (rad/s), and the largest excess over the cap after the drive and before it
// (the solver's push after the last clamp) since the last read. Since the ceiling (BRIEF_governor_sway.md, M5): steps
// counts every step written, armed or waiting; postSum the driven wheels' mean after the drive and before the ceiling,
// summed over those steps (rad/s, a forward mean alone), postMost its largest and preLeast the least driven wheel
// before the drive, both since the last read (0 with no step); ceiling the last step's.
struct WheelCapCounts
{
    long clamps, steps, refused, armedSteps, waitSteps, faults, clampedNow;
    bool armed;
    float kept, spread, excessMost, preMost, postSum, postMost, preLeast, ceiling;
};
void WheelCapCountsGet(WheelCapCounts &out);
// The game's own ShiftGear(0) refused for the held truck at kPolicyAll so far: the gearbox damage's kick to N
// (review\own_drivetrain_research.md C.5); the player's N comes through ShiftToNeutral or AutoOffAndShift, refused at its
// own door, so it never reaches ShiftGear.
long GearboxKicks();
