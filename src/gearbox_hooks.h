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
// The policy (phase 2). While on, the mod's gearbox has the truck: the game's own calls of ShiftGear,
// ShiftToAutoGear, ShiftToHigh, ShiftToReverse, ShiftToNeutral, AutoOffAndShift and SetPowerFactor are refused, the
// game's function not called (false, or nothing), and counted; SetCurrentVehicle always passes. As the counts nest
// (reverse and neutral go through auto off, auto off through ShiftGear), refusing ShiftGear alone would refuse them
// all; each is refused at its own door. The mod's own calls go through GearboxOriginal and are never refused. Turned
// on, it tells each kind afresh: GearboxRefusedNew gives the kinds (1 << kFn...) refused for the first time since.
// vehicle: the held truck (TruckRefs::vehicle): only its calls are refused. Turned on for another truck, it tells
// each kind afresh; off and on again for the same truck (the garage, the map), it does not.
void GearboxHolding(bool on, void *vehicle);
bool GearboxHoldingNow();
unsigned GearboxRefusedNew();
// The way through to the game's own function behind a hook (fn one of kFn*), nullptr while it has none: the mod's own
// calls go this way, so the probe counts only the game's.
void *GearboxOriginal(int fn);
