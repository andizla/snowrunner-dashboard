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
