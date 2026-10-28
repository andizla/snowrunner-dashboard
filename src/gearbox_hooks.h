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
inline HooksWanted GearboxHooksWanted(int level, bool damage) { return HooksWanted{ level >= 1, level >= 2 && damage }; }

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
long TruckUpdateFrames(); // the frames the hook has seen

// The game's wheels' drive (game_code.h, DrivePlaceFind) detoured at address at to the stub of creep_stub.asm, in a
// transaction of its own, the first time Creep is on with a truck driven; never taken out while the game runs. False
// with why when Detours refused it; true at once when it is in place already.
bool CreepHook(uintptr_t at, char *why, size_t size);
bool CreepHooked();
// The share of the drive throttle the hook gives the truck whose action this is (models.h, CreepShare): the larger of
// the game's and this. An action of 0 or a share of 0 lets every call pass as it came.
void CreepGive(uint64_t action, float share);
// The hook's calls so far: for the action given (hits) and all of them.
void CreepCounts(long &hits, long &calls);
