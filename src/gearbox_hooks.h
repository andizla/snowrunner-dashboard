// gearbox_hooks.h: the game's gearbox functions (game_code.h) hooked through Microsoft Detours (src\detours), the
// ground for the mod's own gearbox. The method is the manual transmission mod's (SMT, by drafty46, MIT; the game's
// functions reverse engineered by Ferrster): a detour on each function, attached in one transaction and detached in
// one. Phase 1: every hook calls the game's own function through with its arguments unchanged and returns its result,
// and counts its calls for the probe, so a drive shows which of them the game calls when. Nothing a player feels
// changes. GetMaxGear is only ever called, never hooked.
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
};

// Attaches n detours in one transaction: each targets[i] (a function's address, set to the way through to it on
// success) to hooks[i]. NO_ERROR, or the error of the first that failed, and then none is attached.
long GearboxAttach(void **targets[], void *const hooks[], int n);
// Detaches them in one transaction; each targets[i] holds the function's own address again.
long GearboxDetach(void **targets[], void *const hooks[], int n);

// Hooks the game's gearbox functions the finder found once and not held by another mod, at base (the exe's). why:
// one line per function that is not hooked, and why. The number hooked; 0 when the transaction failed.
int GearboxHook(uintptr_t base, const FunctionFind finds[kFnCount], char *why, size_t size);
// Takes every hook out again (the mod unloading).
void GearboxUnhook();
bool GearboxHooked();
void GearboxCountsGet(GearboxCounts &out);
// The way through to the game's own function behind a hook (fn one of kFn*), nullptr while it has none: the mod's own
// calls go this way, so the probe counts only the game's.
void *GearboxOriginal(int fn);
