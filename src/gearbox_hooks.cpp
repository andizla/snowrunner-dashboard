// gearbox_hooks.cpp: the hooks of gearbox_hooks.h. Each is the game's own signature (as SMT has them): ShiftGear and
// AutoOffAndShift take the vehicle and a gear and answer whether the game took it, ShiftToHigh, ShiftToReverse and
// ShiftToNeutral take the vehicle and answer the same, ShiftToAutoGear takes the vehicle, SetPowerFactor the vehicle
// and a factor, SetCurrentVehicle the truck control and the vehicle (nullptr when the driver leaves).
#include "gearbox_hooks.h"
#include "detours\detours.h"
#include <stdio.h>

typedef bool (*ShiftFn)(void *vehicle, int32_t gear);
typedef bool (*VehicleFn)(void *vehicle);
typedef void (*AutoFn)(void *vehicle);
typedef void (*PowerFn)(void *vehicle, float factor);
typedef void (*SetVehicleFn)(void *control, void *vehicle);

// the way through to each function once hooked (its address before)
static void *g_way[kFnCount] = {};
static bool g_hooked = false;
static volatile LONG g_count[kFnCount] = {};
static volatile LONG g_shiftGear = 0, g_autoOffGear = 0, g_vehicleSet = 0;
static volatile float g_power = 0.0f;

static bool HookShiftGear(void *vehicle, int32_t gear)
{
    InterlockedIncrement(&g_count[kFnShiftGear]);
    InterlockedExchange(&g_shiftGear, gear);
    return ((ShiftFn)g_way[kFnShiftGear])(vehicle, gear);
}

static void HookShiftToAuto(void *vehicle)
{
    InterlockedIncrement(&g_count[kFnShiftToAuto]);
    ((AutoFn)g_way[kFnShiftToAuto])(vehicle);
}

static bool HookShiftToHigh(void *vehicle)
{
    InterlockedIncrement(&g_count[kFnShiftToHigh]);
    return ((VehicleFn)g_way[kFnShiftToHigh])(vehicle);
}

static bool HookShiftToReverse(void *vehicle)
{
    InterlockedIncrement(&g_count[kFnShiftToReverse]);
    return ((VehicleFn)g_way[kFnShiftToReverse])(vehicle);
}

static bool HookShiftToNeutral(void *vehicle)
{
    InterlockedIncrement(&g_count[kFnShiftToNeutral]);
    return ((VehicleFn)g_way[kFnShiftToNeutral])(vehicle);
}

static bool HookAutoOffAndShift(void *vehicle, int32_t gear)
{
    InterlockedIncrement(&g_count[kFnAutoOffAndShift]);
    InterlockedExchange(&g_autoOffGear, gear);
    return ((ShiftFn)g_way[kFnAutoOffAndShift])(vehicle, gear);
}

static void HookSetPower(void *vehicle, float factor)
{
    InterlockedIncrement(&g_count[kFnSetPower]);
    g_power = factor;
    ((PowerFn)g_way[kFnSetPower])(vehicle, factor);
}

static void HookSetVehicle(void *control, void *vehicle)
{
    InterlockedIncrement(&g_count[kFnSetVehicle]);
    InterlockedExchange(&g_vehicleSet, vehicle ? 1 : 0);
    ((SetVehicleFn)g_way[kFnSetVehicle])(control, vehicle);
}

// the hook of each function, nullptr for GetMaxGear (only called)
static void *const kHooks[kFnCount] = { (void *)&HookShiftGear, (void *)&HookShiftToAuto, (void *)&HookShiftToHigh, (void *)&HookShiftToReverse,
                                        (void *)&HookShiftToNeutral, nullptr, (void *)&HookAutoOffAndShift, (void *)&HookSetPower, (void *)&HookSetVehicle };

long GearboxAttach(void **targets[], void *const hooks[], int n)
{
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) return error;
    DetourUpdateThread(GetCurrentThread());
    for (int i = 0; i < n && error == NO_ERROR; i++) error = DetourAttach(targets[i], hooks[i]);
    if (error != NO_ERROR)
    {
        DetourTransactionAbort();
        return error;
    }
    return DetourTransactionCommit();
}

long GearboxDetach(void **targets[], void *const hooks[], int n)
{
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) return error;
    DetourUpdateThread(GetCurrentThread());
    for (int i = 0; i < n && error == NO_ERROR; i++) error = DetourDetach(targets[i], hooks[i]);
    if (error != NO_ERROR)
    {
        DetourTransactionAbort();
        return error;
    }
    return DetourTransactionCommit();
}

// the functions hooked, in the order attached
static void **g_targets[kFnCount];
static void *g_hooks[kFnCount];
static int g_hookedCount = 0;

int GearboxHook(uintptr_t base, const FunctionFind finds[kFnCount], char *why, size_t size)
{
    size_t n = 0;
    why[0] = 0;
    auto say = [&](const char *name, const char *text) {
        const int w = snprintf(why + n, size - n, "%s%s: %s", n ? "\n" : "", name, text);
        if (w > 0 && (size_t)w < size - n) n += (size_t)w;
    };
    if (g_hooked) return g_hookedCount;
    int count = 0;
    for (int f = 0; f < kFnCount; f++)
    {
        if (!kHooks[f]) continue;
        const FunctionFind &r = finds[f];
        if (r.state == kFnHeld) say(kGameFunctions[f].name, "another mod holds it");
        else if (r.state == kFnUnplaced) say(kGameFunctions[f].name, "not looked for, as ShiftToHigh or ShiftToReverse was not found");
        else if (r.state != kFnFound) say(kGameFunctions[f].name, "not found in the game's code");
        else if (r.count != 1) say(kGameFunctions[f].name, "its bytes stand more than once in the game's code");
        else
        {
            g_way[f] = (void *)(base + r.at);
            g_targets[count] = &g_way[f];
            g_hooks[count] = kHooks[f];
            count++;
        }
    }
    if (!count) return 0;
    const long error = GearboxAttach(g_targets, g_hooks, count);
    if (error != NO_ERROR)
    {
        char text[96];
        snprintf(text, sizeof text, "Detours could not attach the hooks (error %ld), so none is in place", error);
        say("all", text);
        for (int f = 0; f < kFnCount; f++) g_way[f] = nullptr;
        return 0;
    }
    g_hookedCount = count;
    g_hooked = true;
    return count;
}

void GearboxUnhook()
{
    if (!g_hooked) return;
    if (GearboxDetach(g_targets, g_hooks, g_hookedCount) == NO_ERROR)
    {
        g_hooked = false;
        g_hookedCount = 0;
        for (int f = 0; f < kFnCount; f++) g_way[f] = nullptr;
    }
}

bool GearboxHooked() { return g_hooked; }

void GearboxCountsGet(GearboxCounts &out)
{
    out.shift = g_count[kFnShiftGear];
    out.autoGear = g_count[kFnShiftToAuto];
    out.high = g_count[kFnShiftToHigh];
    out.reverse = g_count[kFnShiftToReverse];
    out.neutral = g_count[kFnShiftToNeutral];
    out.autoOff = g_count[kFnAutoOffAndShift];
    out.power = g_count[kFnSetPower];
    out.vehicle = g_count[kFnSetVehicle];
    out.shiftGear = (int)g_shiftGear;
    out.autoOffGear = (int)g_autoOffGear;
    out.factor = g_power;
    out.vehicleSet = g_vehicleSet != 0;
}

void *GearboxOriginal(int fn) { return g_hooked && fn >= 0 && fn < kFnCount && kHooks[fn] ? g_way[fn] : nullptr; }
