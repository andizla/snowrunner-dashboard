// gearbox_hooks.cpp: the hooks of gearbox_hooks.h. Each is the game's own signature (as SMT has them): ShiftGear and
// AutoOffAndShift take the vehicle and a gear and answer whether the game took it, ShiftToHigh, ShiftToReverse and
// ShiftToNeutral take the vehicle and answer the same, ShiftToAutoGear takes the vehicle, SetPowerFactor the vehicle
// and a factor, SetCurrentVehicle the truck control and the vehicle (nullptr when the driver leaves).
//
// The counts nest: in the game's code ShiftToReverse and ShiftToNeutral jump into AutoOffAndShift (the gear -1 or 0),
// and AutoOffAndShift into ShiftGear, so one reverse adds one to reverse, to auto off and to ShiftGear (review 6, G2).
#include "gearbox_hooks.h"
#include "detours\detours.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <xmmintrin.h>
#include <intrin.h>

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

// The policy (gearbox_hooks.h, GearboxPolicy): for the held truck only (g_heldVehicle; another vehicle's calls pass,
// review 7, R5), the level and, for kPolicyBrakeReverse, whether the truck stands in A with the brake down. A refused
// call is counted, the game's function not called, and each kind is told once a truck (g_told, cleared when another
// truck is held) through g_new, which the mod's thread logs.
static volatile LONG g_policy = kPolicyPass, g_brakeStand = 0, g_told = 0, g_new = 0;
static volatile LONG g_brakeDoor = -1; // the hook (kFn...) that carried the last reverse refused on the brake (review 8, W3)
static void *volatile g_heldVehicle = nullptr;
static const volatile uint8_t *volatile g_autoFlag = nullptr; // the held truck's automatic flag (action + 0x3C), for level 1

// The game's automatic flag as it is at the call; false where it cannot be read (no truck, or its memory gone).
static bool AutoFlagUp()
{
    const volatile uint8_t *flag = g_autoFlag;
    if (!flag) return false;
    __try
    {
        return *flag != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
static volatile LONG g_refused[kFnCount] = {};
static volatile LONG g_kicks = 0; // the game's ShiftGear(0) refused for the held truck (GearboxKicks)

// The manual shift rebuild's probe (review 17, step 0): each hook's last thread, and the fields around a call that
// passed through.
static volatile LONG g_thread[kFnCount] = {}, g_truckThread = 0, g_driveThread = 0, g_fieldProbe = 0;
static GearboxFieldLook g_looks[kFnCount] = {};
static volatile LONG g_lookSeq[kFnCount] = {};

static void Seen(int fn)
{
    InterlockedIncrement(&g_count[fn]);
    InterlockedExchange(&g_thread[fn], (LONG)GetCurrentThreadId());
}

// The action's fields from + 0x38, through the vehicle's action pointer at + 0x68; false where the memory is gone.
static bool FieldsGrab(void *vehicle, uint8_t *out)
{
    __try
    {
        const uint8_t *action = *(uint8_t *const *)((const uint8_t *)vehicle + 0x68);
        if ((uintptr_t)action < 0x10000) return false;
        memcpy(out, action + 0x38, kFieldLookBytes);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Around a call that passes through: before (true to go on and look after), and after. The record may be torn if two
// game threads look at once; it is a probe's.
static bool LookBefore(int fn, void *vehicle) { return g_fieldProbe && FieldsGrab(vehicle, g_looks[fn].before); }
static void LookAfter(int fn, void *vehicle, int gear, float factor)
{
    if (!FieldsGrab(vehicle, g_looks[fn].after)) return;
    g_looks[fn].gear = gear;
    g_looks[fn].factor = factor;
    g_looks[fn].seq = InterlockedIncrement(&g_lookSeq[fn]);
}

void GearboxThreadsGet(DWORD out[kFnCount], DWORD &truckUpdate, DWORD &drive)
{
    for (int f = 0; f < kFnCount; f++) out[f] = (DWORD)g_thread[f];
    truckUpdate = (DWORD)g_truckThread;
    drive = (DWORD)g_driveThread;
}

void GearboxFieldProbe(bool on) { InterlockedExchange(&g_fieldProbe, on ? 1 : 0); }

long GearboxFieldLookGet(int fn, GearboxFieldLook &out)
{
    if (fn < 0 || fn >= kFnCount || !g_lookSeq[fn]) return 0;
    out = g_looks[fn];
    return out.seq;
}

static void Tell(LONG bit)
{
    if (!(InterlockedOr(&g_told, bit) & bit)) InterlockedOr(&g_new, bit);
}

// The game's shifter's request (gearbox_hooks.h, GearboxAskMerge), packed in one word for the hooks' threads and the
// mod's: the kind in bits 0 to 3, the gear in 4 to 11, and in 12 to 31 the mod's pass it was made in (g_pass, which
// GearboxAskTake steps); 0 none pending.
static volatile LONG g_asks = 0, g_ask = 0, g_pass = 0;
static const ULONG kPassMask = 0xFFFFF;

static ULONG AskPack(const GearboxAsk &a, ULONG pass) { return (ULONG)a.kind | ((ULONG)(a.gear & 0xFF) << 4) | ((pass & kPassMask) << 12); }

static GearboxAsk AskUnpack(ULONG v) { return GearboxAsk{ (int)(v & 0xF), (int)((v >> 4) & 0xFF) }; }

static void Ask(int fn, int32_t gear, float factor)
{
    for (;;)
    {
        const ULONG was = (ULONG)g_ask;
        const GearboxAsk pending = was ? AskUnpack(was) : GearboxAsk{ kAskNone, 0 };
        const GearboxAsk now = GearboxAskMerge(pending, fn, gear, factor);
        if (now.kind == kAskNone) return;
        const ULONG packed = AskPack(now, was ? was >> 12 : (ULONG)g_pass);
        if (packed == was || (ULONG)InterlockedCompareExchange(&g_ask, (LONG)packed, (LONG)was) == was) return;
    }
}

void GearboxShifterAsks(bool on) { InterlockedExchange(&g_asks, on ? 1 : 0); }

bool GearboxAskTake(GearboxAsk &out)
{
    const ULONG pass = (ULONG)InterlockedIncrement(&g_pass) & kPassMask;
    for (;;)
    {
        const ULONG was = (ULONG)g_ask;
        if (!was) return false;
        const ULONG age = (pass - (was >> 12)) & kPassMask;
        if (age == 0) return false; // made on this pass: the next takes it
        if ((ULONG)InterlockedCompareExchange(&g_ask, 0, (LONG)was) != was) continue;
        if (age > 2) return false;
        out = AskUnpack(was);
        return true;
    }
}

// gear: the gear the call asks for (ShiftGear, AutoOffAndShift), else 0; factor: SetPowerFactor's, else 0
static bool Refused(int fn, void *vehicle, int32_t gear = 0, float factor = 0.0f)
{
    const LONG policy = g_policy;
    if (policy == kPolicyPass || vehicle != g_heldVehicle) return false;
    bool refuse = policy == kPolicyAll;
    // level 1: the brake-reverse is the game's own automatic's, which acts only while its flag is up; with the flag down
    // a reverse is the player's (the shifter's R) and passes. At whichever door it comes: ShiftToReverse, or a gear below
    // 0 through AutoOffAndShift or ShiftGear
    if (policy == kPolicyBrakeReverse && g_brakeStand && (fn == kFnShiftToReverse || fn == kFnShiftGear || fn == kFnAutoOffAndShift) && AutoFlagUp())
    {
        refuse = fn == kFnShiftToReverse || gear < 0;
        if (refuse)
        {
            InterlockedExchange(&g_brakeDoor, fn);
            Tell((LONG)kRefusedBrakeReverse);
        }
    }
    if (!refuse) return false;
    InterlockedIncrement(&g_refused[fn]);
    if (policy == kPolicyAll)
    {
        Tell(1L << fn);
        if (g_asks) Ask(fn, gear, factor);
    }
    return true;
}

static bool HookShiftGear(void *vehicle, int32_t gear)
{
    Seen(kFnShiftGear);
    InterlockedExchange(&g_shiftGear, gear);
    if (Refused(kFnShiftGear, vehicle, gear))
    {
        // a bare ShiftGear(0) refused for the held truck is the gearbox damage's kick to N (GearboxKicks)
        if (gear == 0 && g_policy == kPolicyAll) InterlockedIncrement(&g_kicks);
        return false;
    }
    const bool look = LookBefore(kFnShiftGear, vehicle);
    const bool took = ((ShiftFn)g_way[kFnShiftGear])(vehicle, gear);
    if (look) LookAfter(kFnShiftGear, vehicle, gear, 0.0f);
    return took;
}

static void HookShiftToAuto(void *vehicle)
{
    Seen(kFnShiftToAuto);
    if (Refused(kFnShiftToAuto, vehicle)) return;
    const bool look = LookBefore(kFnShiftToAuto, vehicle);
    ((AutoFn)g_way[kFnShiftToAuto])(vehicle);
    if (look) LookAfter(kFnShiftToAuto, vehicle, 0, 0.0f);
}

static bool HookShiftToHigh(void *vehicle)
{
    Seen(kFnShiftToHigh);
    if (Refused(kFnShiftToHigh, vehicle)) return false;
    return ((VehicleFn)g_way[kFnShiftToHigh])(vehicle);
}

static bool HookShiftToReverse(void *vehicle)
{
    Seen(kFnShiftToReverse);
    if (Refused(kFnShiftToReverse, vehicle)) return false;
    return ((VehicleFn)g_way[kFnShiftToReverse])(vehicle);
}

static bool HookShiftToNeutral(void *vehicle)
{
    Seen(kFnShiftToNeutral);
    if (Refused(kFnShiftToNeutral, vehicle)) return false;
    return ((VehicleFn)g_way[kFnShiftToNeutral])(vehicle);
}

static bool HookAutoOffAndShift(void *vehicle, int32_t gear)
{
    Seen(kFnAutoOffAndShift);
    InterlockedExchange(&g_autoOffGear, gear);
    if (Refused(kFnAutoOffAndShift, vehicle, gear)) return false;
    const bool look = LookBefore(kFnAutoOffAndShift, vehicle);
    const bool took = ((ShiftFn)g_way[kFnAutoOffAndShift])(vehicle, gear);
    if (look) LookAfter(kFnAutoOffAndShift, vehicle, gear, 0.0f);
    return took;
}

static void HookSetPower(void *vehicle, float factor)
{
    Seen(kFnSetPower);
    g_power = factor;
    if (Refused(kFnSetPower, vehicle, 0, factor)) return;
    const bool look = LookBefore(kFnSetPower, vehicle);
    ((PowerFn)g_way[kFnSetPower])(vehicle, factor);
    if (look) LookAfter(kFnSetPower, vehicle, 0, factor);
}

void GearboxPolicy(int policy, void *vehicle, bool brakeStand, const volatile uint8_t *autoFlag)
{
    const bool on = policy != kPolicyPass;
    if (on && vehicle != g_heldVehicle) InterlockedExchange(&g_told, 0);
    if (on) InterlockedExchangePointer((void *volatile *)&g_heldVehicle, vehicle);
    InterlockedExchangePointer((void *volatile *)&g_autoFlag, (void *)autoFlag);
    InterlockedExchange(&g_brakeStand, brakeStand ? 1 : 0);
    InterlockedExchange(&g_policy, policy);
}

int GearboxPolicyNow() { return (int)g_policy; }

int GearboxBrakeDoor() { return (int)g_brakeDoor; }

bool GearboxHoldingNow() { return g_policy != kPolicyPass; }

unsigned GearboxRefusedNew() { return (unsigned)InterlockedExchange(&g_new, 0); }

static void HookSetVehicle(void *control, void *vehicle)
{
    Seen(kFnSetVehicle);
    InterlockedExchange(&g_vehicleSet, vehicle ? 1 : 0);
    ((SetVehicleFn)g_way[kFnSetVehicle])(control, vehicle);
}

// the hook of each function, nullptr for GetMaxGear (only called)
static void *const kHooks[kFnCount] = { (void *)&HookShiftGear, (void *)&HookShiftToAuto, (void *)&HookShiftToHigh, (void *)&HookShiftToReverse,
                                        (void *)&HookShiftToNeutral, nullptr, (void *)&HookAutoOffAndShift, (void *)&HookSetPower, (void *)&HookSetVehicle };

// Every other thread of the process, opened so Detours can suspend it for the commit and move it out of the bytes it
// patches: the hooks go in while the game drives a truck, and a game thread inside a function's first bytes would run
// half old, half new code (review 6, G1). Taken after the attaches, so Detours' own allocations come before any thread
// is suspended. The caller closes the handles after the commit.
static const int kMostThreads = 1024;
static int OtherThreads(HANDLE out[kMostThreads])
{
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te = {};
    te.dwSize = sizeof te;
    const DWORD process = GetCurrentProcessId(), self = GetCurrentThreadId();
    int n = 0;
    for (BOOL ok = Thread32First(snap, &te); ok && n < kMostThreads; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != process || te.th32ThreadID == self) continue;
        const HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
        if (h) out[n++] = h;
    }
    CloseHandle(snap);
    return n;
}

// One transaction of attaches (attach) or detaches, every other thread suspended for its commit; others false: none
// suspended (GearboxAttachAlone).
static long Transact(bool attach, void **targets[], void *const hooks[], int n, bool others = true)
{
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) return error;
    DetourUpdateThread(GetCurrentThread());
    for (int i = 0; i < n && error == NO_ERROR; i++) error = attach ? DetourAttach(targets[i], hooks[i]) : DetourDetach(targets[i], hooks[i]);
    if (error != NO_ERROR)
    {
        DetourTransactionAbort();
        return error;
    }
    static HANDLE threads[kMostThreads];
    const int count = others ? OtherThreads(threads) : 0;
    for (int i = 0; i < count; i++) DetourUpdateThread(threads[i]);
    error = DetourTransactionCommit();
    for (int i = 0; i < count; i++) CloseHandle(threads[i]);
    return error;
}

long GearboxAttach(void **targets[], void *const hooks[], int n) { return Transact(true, targets, hooks, n); }

long GearboxAttachAlone(void **targets[], void *const hooks[], int n) { return Transact(true, targets, hooks, n, false); }

long GearboxDetach(void **targets[], void *const hooks[], int n) { return Transact(false, targets, hooks, n); }

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
    // the ways through stay as they are: a thread still inside a hook calls on through them (review 6, G3)
    if (GearboxDetach(g_targets, g_hooks, g_hookedCount) == NO_ERROR)
    {
        g_hooked = false;
        g_hookedCount = 0;
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
    for (int f = 0; f < kFnCount; f++) out.refused[f] = g_refused[f];
    out.vehicleSet = g_vehicleSet != 0;
}

void *GearboxOriginal(int fn) { return g_hooked && fn >= 0 && fn < kFnCount && kHooks[fn] ? g_way[fn] : nullptr; }

// ---- the game's truck update ----
// Its first bytes keep its four arguments in their home slots (rcx, rdx, xmm2, xmm3): two pointers and two floats;
// whether it takes more on the stack is not known. So the call through must stay the hook's last statement, and the
// compiler must make it a tail jump (built /O2 it is: rsp restored, then jmp [g_truckWay]): a stack argument then
// reaches the game where the game left it, and what the update returns in rax or xmm0 is handed back untouched. A
// statement after it, or a build without /O2, would make it a call from the hook's own frame (review 10, D2).
typedef void (*TruckUpdateFn)(void *, void *, float, float);
static void *g_truckWay = nullptr;
static TruckFrameFn volatile g_onFrame = nullptr;
static volatile LONG g_frames = 0;
static bool g_truckHooked = false;

static void HookTruckUpdate(void *first, void *second, float third, float fourth)
{
    InterlockedIncrement(&g_frames);
    g_truckThread = (LONG)GetCurrentThreadId();
    if (const TruckFrameFn on = g_onFrame) on(first);
    ((TruckUpdateFn)g_truckWay)(first, second, third, fourth);
}

bool TruckUpdateHook(uintptr_t at, TruckFrameFn onFrame, char *why, size_t size)
{
    why[0] = 0;
    if (g_truckHooked) return true;
    g_onFrame = onFrame;
    g_truckWay = (void *)at;
    void **targets[1] = { &g_truckWay };
    void *const hooks[1] = { (void *)&HookTruckUpdate };
    const long error = GearboxAttach(targets, hooks, 1);
    if (error != NO_ERROR)
    {
        snprintf(why, size, "Detours could not attach the truck update's hook (error %ld)", error);
        g_truckWay = nullptr;
        return false;
    }
    g_truckHooked = true;
    return true;
}

void TruckUpdateUnhook()
{
    if (!g_truckHooked) return;
    void **targets[1] = { &g_truckWay };
    void *const hooks[1] = { (void *)&HookTruckUpdate };
    if (GearboxDetach(targets, hooks, 1) == NO_ERROR) g_truckHooked = false;
}

long TruckUpdateFrames() { return g_frames; }

bool TruckUpdateHooked() { return g_truckHooked; }

// ---- the game's wheels' drive (the creep, and the suspension's step probe) ----
// In the 6ab52f5d image the drive (c41da0) reads only the action in rcx, the drive throttle in xmm1 and the step's
// seconds in xmm2: rdx, r8, r9, xmm0 and xmm3 it writes before any read, it reads no argument from the stack (only
// the home slots of rcx and r9, as its own scratch), and its caller (c41d40) loads rax anew right after the call and
// takes nothing from xmm0. So a C++ function of three arguments with no answer forwards all it takes (the brief of
// the step probe, part 1). The creep's slot is one of two, filled on the mod's thread and then published whole, so
// the hook never sees an action with another truck's share: the slot it may still be reading is written again only
// two passes later, and the hook reads it within a few instructions.
struct CreepSlot
{
    uint64_t action;
    float share;
    uint32_t spare;
};
typedef void (*DriveFn)(void *action, float throttle, float dt);
static void *g_creepWay = nullptr;
static void *volatile g_creepSlot = nullptr;
static volatile LONG g_creepHits = 0, g_creepCalls = 0;
static DriveStepFn volatile g_driveStep = nullptr;
static CreepSlot g_creepSlots[2];
static int g_creepNext = 0;

// ---- the physics rate test mode (physics_rate.h) ----
// g_rateOn 0: the drive is called as it is and nothing below is touched.
static volatile LONG g_rateOn = 0, g_rateAsked = 0;
static volatile LONG64 g_rateSlot = 0;
static uint64_t g_rateWorld = 0; // the physics thread's own: the world of the last look
static volatile LONG g_rateWorlds = 0, g_rateDtBits = 0, g_rateIter = 0, g_rateHookDt = 0, g_rateFaults = 0;
static volatile LONG64 g_rateSeenWorld = 0;
static LONG FloatBits(float f);
static float BitsFloat(LONG bits);
static bool RateBefore(void *action, RateHalved &h)
{
    __try
    {
        RateHalveBefore(action, h);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        h.on[0] = h.on[1] = false;
        InterlockedIncrement(&g_rateFaults);
        return false;
    }
}
static void RateAfter(void *action, const RateHalved &h)
{
    __try
    {
        RateHalveAfter(action, h);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        InterlockedIncrement(&g_rateFaults);
    }
}
// The drive display (further down): the action whose call it reads now, set and cleared by that call's own thread;
// ShowMid reads the wheels right after the game's own drive, before the mod's cap and lock write.
static void *volatile g_showNow = nullptr;
static void ShowMid();
// The game's own drive. In the test mode with the action's MaxDeltaAngVel and EngineResponsiveness halved around it
// (the Reviewer's M1): the drive and what it calls (the torque hook's CutWheelsRead among it) read the halves, and
// the game's values stand as they were once it returns. The drive itself runs outside any guard, as before.
static void DriveCall(void *action, float throttle, float dt)
{
    if (!g_rateOn)
    {
        ((DriveFn)g_creepWay)(action, throttle, dt);
        if (action == g_showNow) ShowMid();
        return;
    }
    RateHalved h = {};
    const bool read = RateBefore(action, h);
    ((DriveFn)g_creepWay)(action, throttle, dt);
    if (read) RateAfter(action, h);
    if (action == g_showNow) ShowMid();
}
// The world of this call (the Reviewer's M3): the owner's static, the owner's world; at a new world pointer its step's
// seconds and its solver's iterations are kept for the mod's thread, which judges and logs them. Two reads a call.
static void RateWorldLook(float dt)
{
    const uint64_t slot = (uint64_t)g_rateSlot;
    if (!slot) return;
    __try
    {
        const uint64_t owner = *(const volatile uint64_t *)(uintptr_t)slot;
        if (!owner) return;
        const uint64_t world = *(const volatile uint64_t *)(uintptr_t)(owner + kRateOwnerWorld);
        if (!world || world == g_rateWorld) return;
        const uint32_t bits = *(const volatile uint32_t *)(uintptr_t)(world + kRateWorldDtAt);
        const int iterations = *(const volatile int *)(uintptr_t)(world + kRateWorldIterAt);
        g_rateWorld = world;
        InterlockedExchange64(&g_rateSeenWorld, (LONG64)world);
        InterlockedExchange(&g_rateDtBits, (LONG)bits);
        InterlockedExchange(&g_rateIter, iterations);
        InterlockedExchange(&g_rateHookDt, FloatBits(dt));
        InterlockedIncrement(&g_rateWorlds); // last: the mod's thread reads the rest once this has grown
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        InterlockedIncrement(&g_rateFaults);
    }
}
void DriveRateSet(bool on, uint64_t ownerSlot, int iterations)
{
    InterlockedExchange64(&g_rateSlot, (LONG64)ownerSlot);
    InterlockedExchange(&g_rateAsked, iterations);
    InterlockedExchange(&g_rateOn, on ? 1 : 0);
}
bool DriveRateOn() { return g_rateOn != 0; }
void DriveRateSeenGet(DriveRateSeen &out)
{
    for (int look = 0; look < 4; look++)
    {
        out.worlds = g_rateWorlds;
        out.world = (uint64_t)g_rateSeenWorld;
        out.dtBits = (uint32_t)g_rateDtBits;
        out.iterations = g_rateIter;
        out.hookDt = BitsFloat(g_rateHookDt);
        out.faults = g_rateFaults;
        if (out.worlds == g_rateWorlds) return;
    }
}
static bool g_creepHooked = false;

// Each thread the drive was called on, kept in a slot of its own (the Reviewer's addendum 1 to
// BRIEF_step_hook_writes.md: the truck's action is applied on Havok's worker threads, not on one stepping thread).
// Lock free: a slot is taken with a compare and swap; past kDriveThreads the set is marked full.
static const int kDriveThreads = 32;
static volatile LONG g_driveThreads[kDriveThreads] = {}, g_driveThreadsFull = 0;
static void DriveThreadNote(LONG id)
{
    for (int i = 0; i < kDriveThreads; i++)
    {
        const LONG at = g_driveThreads[i];
        if (at == id) return;
        if (at) continue;
        const LONG was = InterlockedCompareExchange(&g_driveThreads[i], id, 0);
        if (!was || was == id) return;
    }
    g_driveThreadsFull = 1;
}

int DriveThreadsGet(DWORD *out, int most, bool &full)
{
    int n = 0;
    for (int i = 0; i < kDriveThreads && n < most; i++)
        if (const LONG id = g_driveThreads[i]) out[n++] = (DWORD)id;
    full = g_driveThreadsFull != 0;
    return n;
}

// The torque cut's slot (TorqueCutPublish), read by the drive's hook below and by the gear torque's detour.
static TorqueCutSlot g_cutSlots[2];
static int g_cutNext = 0;
static void *volatile g_cutSlot = nullptr;
static volatile LONG g_cutHolds = 0; // the drive's calls whose throttle the cut held at 1

// B4 (action + 0xB4) as the game has it before the drive runs (the chassis push's F_excess); NaN where it faults.
static float CutB4Read(const void *action)
{
    __try
    {
        return *(const volatile float *)((const uint8_t *)action + 0xB4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NAN;
    }
}

// B4 (action + 0xB4) written 1 before the drive runs; nothing where the memory is gone.
static void CutPullOne(void *action)
{
    __try
    {
        *(volatile float *)((uint8_t *)action + 0xB4) = 1.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static LONG FloatBits(float f);
static float BitsFloat(LONG bits);
// ---- the wheel speed cap (BRIEF_drivetrain_2c_wheel_cap.md; torque_cut.h, CapStep) ----
// Read on the physics thread around the original drive, one call per vehicle a step: the driven wheels' bodies (wheel
// + 0x18, their angular velocity at + 0x240, world), their axles (wheel + 0x154, world, not normalised), the desc's
// local axle (+ 0x100) and the inverse inertia (body + 0x220), for the spread's gain. A body is used as activate checks
// it: in a world (body + 0x10) and not a fixed motion (body + 0x160 == 5); the drive has activated every wheel by the
// time it returns, so no activate of our own (the Researcher's R1). No lock, no heap, no log.
struct CapWheelRead
{
    uint8_t *wheel, *body;
    CapVec axle, localAxle, invI;
    float pre, gain, alen;
    float load, radius; // the contact share (wheel + 0x128) and the rolling radius (WheelRollRadius), for LockStepHeld
    float sink, ice;    // wheel + 0x178 (the sink, m) and + 0x1C4 (the ice blend), for the lock's by-wheel line alone
    uint8_t ground;     // wheel + 0x1A4 (the contact with a ground body), for the same
    bool ok;
};
struct CapTruckRead
{
    CapWheelRead w[kCapWheels];
    int n, refused;
    int gear;
    float sign;
    float forward; // the main body's forward speed, m/s (no number: not read), for the lock's truck speed where the cut gave none
};
// What the hook notes for the drive display in the call it reads (action == g_showNow): the differential in force,
// the cap's kept share, and by the wheel's pointer whether the cap clamped it and whether Open or the lock moved it.
struct ShowNote
{
    int force, n;
    float kept;
    uint8_t *wheel[kCapWheels];
    bool capped[kCapWheels], tied[kCapWheels];
};
static ShowNote g_showNote; // the physics thread's own
static void ShowNoteWheels(const CapTruckRead &t, const float *post, const float *work)
{
    g_showNote.n = t.n;
    for (int i = 0; i < t.n; i++) g_showNote.wheel[i] = t.w[i].wheel, g_showNote.capped[i] = false, g_showNote.tied[i] = t.w[i].ok && work[i] != post[i];
}
static void CapGet3(const uint8_t *p, CapVec &v)
{
    v.x = *(const volatile float *)p;
    v.y = *(const volatile float *)(p + 4);
    v.z = *(const volatile float *)(p + 8);
}
static bool CapBodyOk(const uint8_t *body)
{
    return (uintptr_t)body >= 0x10000 && *(void *const volatile *)(body + 0x10) != nullptr && *(const volatile uint8_t *)(body + 0x160) != 5;
}
__declspec(noinline) static bool CutCylinderRead(const uint8_t *wheel, uint64_t vtable, float &cylinder);
static bool CapReadBefore(const void *vehicle, const void *action, CapTruckRead &t, uint64_t cylinderVtable)
{
    t.n = t.refused = 0;
    __try
    {
        const uint8_t *v = (const uint8_t *)vehicle;
        if (*(const void *const volatile *)(v + 0x68) != action) return false;
        const uint8_t *a = (const uint8_t *)action;
        const bool awd = *(const volatile uint8_t *)(a + 0x49) != 0;
        t.gear = *(const volatile int *)(a + 0x74);
        t.sign = t.gear < 0 ? -1.0f : 1.0f;
        t.forward = NAN;
        {
            // as CutWheelsRead: [[vehicle + 0x60] + 0x230], its linear velocity (+ 0x230) along its rotation's first column (+ 0x170)
            const uint8_t *vm = *(const uint8_t *const *)(v + 0x60);
            const uint8_t *main = (uintptr_t)vm >= 0x10000 ? *(const uint8_t *const *)(vm + 0x230) : nullptr;
            if ((uintptr_t)main >= 0x10000)
            {
                const volatile float *vel = (const volatile float *)(main + 0x230), *f = (const volatile float *)(main + 0x170);
                t.forward = vel[0] * f[0] + vel[1] * f[1] + vel[2] * f[2];
            }
        }
        const uintptr_t begin = *(const uintptr_t *)(v + 0x200), end = *(const uintptr_t *)(v + 0x208);
        if (end < begin || (end - begin) / 8 > (uintptr_t)kCapWheels) return false;
        for (uintptr_t p = begin; p < end; p += 8)
        {
            const uint8_t *model = *(const uint8_t *const *)p;
            if ((uintptr_t)model < 0x10000) continue;
            uint8_t *wheel = *(uint8_t *const *)(model + 0x2C8);
            if ((uintptr_t)wheel < 0x10000) continue;
            const int type = *(const volatile int *)(wheel + 0xE8);
            if (!(type == 1 || (type == 2 && awd))) continue;
            CapWheelRead &r = t.w[t.n];
            r = CapWheelRead{};
            r.wheel = wheel;
            uint8_t *body = *(uint8_t *const *)(wheel + 0x18);
            if (!CapBodyOk(body))
            {
                t.refused++;
                continue;
            }
            r.body = body;
            CapVec omega;
            CapGet3(wheel + 0x154, r.axle);
            CapGet3(body + 0x240, omega);
            r.alen = sqrtf(CapDot(r.axle, r.axle));
            r.pre = CapSpeed(omega, r.axle, t.sign);
            const uint8_t *desc = *(const uint8_t *const *)(wheel + 0x28);
            if ((uintptr_t)desc >= 0x10000) CapGet3(desc + 0x100, r.localAxle);
            CapGet3(body + 0x220, r.invI);
            r.gain = CapGain(r.localAxle, r.invI, r.alen);
            r.load = *(const volatile float *)(wheel + 0x128);
            r.ground = *(const volatile uint8_t *)(wheel + 0x1A4);
            r.sink = *(const volatile float *)(wheel + 0x178);
            r.ice = *(const volatile float *)(wheel + 0x1C4);
            float cylinder = NAN;
            const bool cylinderRead = CutCylinderRead(wheel, cylinderVtable, cylinder);
            r.radius = WheelRollRadius(*(const volatile float *)(wheel + 0x94), cylinder, cylinderRead).r;
            r.ok = r.alen > 1e-6f && isfinite(r.alen);
            t.n++;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        t.n = 0;
        return false;
    }
}
// After the drive: each wheel's speed again (post); false where a read faults.
static bool CapReadAfter(const CapTruckRead &t, float *post)
{
    __try
    {
        for (int i = 0; i < t.n; i++)
        {
            if (!t.w[i].ok) continue;
            CapVec omega;
            CapGet3(t.w[i].body + 0x240, omega);
            post[i] = CapSpeed(omega, t.w[i].axle, t.sign);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
// The writes: each wheel moved about its axle by final - post (CapMove, three floats at body + 0x240, the w lane left).
// The game's own setter is one store of body + 0x240: the same here, with no call into the game.
static bool CapWriteFinal(const CapTruckRead &t, const float *post, const float *final)
{
    __try
    {
        for (int i = 0; i < t.n; i++)
        {
            if (!t.w[i].ok || final[i] == post[i]) continue;
            CapVec omega;
            CapGet3(t.w[i].body + 0x240, omega);
            if (!CapMove(omega, t.w[i].axle, t.sign, final[i] - post[i])) continue;
            volatile float *at = (volatile float *)(t.w[i].body + 0x240);
            at[0] = omega.x;
            at[1] = omega.y;
            at[2] = omega.z;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool CapWrite(const CapTruckRead &t, const float *post, const CapStepOut &o) { return CapWriteFinal(t, post, o.final); }

// ---- the diff lock's bite and the differential's type (BRIEF_diff_lock_bite.md, BRIEF_diff_types.md; torque_cut.h) ----
// The mod's thread publishes the driven truck's action, vehicle, DiffLockBite, mass and the cylinder's vtable (LockGive),
// and the truck's type in the cut's slot. In the drive's hook the lock byte and the handbrake are read once a step and
// give what is in force (DiffForceOf). After the drive: Open's evening or the lock's step (LockStepHeld) on the driven
// wheels' speeds the drive left, then the cap (when it is on), one write a wheel. No lock, no heap, no log.
static LockSlot g_lockSlots[2];
static int g_lockNext = 0;
static void *volatile g_lockSlot = nullptr;
static volatile LONG g_lockCalls = 0, g_lockOff = 0, g_lockHand = 0, g_lockWrites = 0, g_lockFaults = 0, g_lockSkipped = 0, g_lockWheels = 0;
static volatile LONG g_lockKBits = 0, g_lockBeforeBits = 0, g_lockAfterBits = 0, g_lockCBits = 0, g_lockRBits = 0, g_lockOverBits = 0;
static volatile LONG64 g_lockVehicle = 0;
// M1 of BRIEF_lock_wheels_backward.md. g_lockHeld is the physics thread's own: a written step that waits for the next
// step's speeds before the drive. g_lockLast is the pair given to the mod's thread, g_lockLastSeq odd while it is
// written (the reader takes a copy and asks the count again).
static volatile LONG g_lockCUnder = 0, g_lockUnder = 0, g_lockBounded = 0, g_lockBreach = 0, g_lockLastSeq = 0;
static LockLast g_lockHeld, g_lockLast;
static const uint8_t *g_lockHeldWheel[kCapWheels];
static bool g_lockHeldOn = false;
static volatile LONG g_evenSteps = 0, g_evenWrites = 0, g_evenFaults = 0, g_evenN = 0, g_evenJBits = 0, g_diffForceNow = kDiffForceLimited;
// The lock byte (action + 0x4A) and the handbrake (action + 0x48), once a step; false where the read faults.
static bool DiffBytesRead(const void *action, bool &lockByte, bool &handbrake)
{
    lockByte = handbrake = false;
    __try
    {
        const uint8_t *a = (const uint8_t *)action;
        lockByte = *(const volatile uint8_t *)(a + 0x4A) != 0;
        handbrake = *(const volatile uint8_t *)(a + 0x48) != 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        lockByte = handbrake = false;
        return false;
    }
}
// LockStepHeld over a read truck, counted; locked gets the speeds to write (post where nothing moves). True where it
// wrote: the step is then kept for the by-wheel line, and the caller with a cap sets its capped and ceiling. truck no
// number (the lock without the cut, whose hook alone works the truck's speed out): from the read's forward speed.
static bool LockOver(const CapTruckRead &t, const float *post, float k, float *locked, uint64_t vehicle, float mass, float truck)
{
    if (g_lockHeldOn)
    {
        // the step after a written one: its speeds before the drive are what the ground left of the write
        g_lockHeldOn = false;
        bool same = g_lockHeld.vehicle == vehicle && g_lockHeld.n == t.n;
        for (int i = 0; same && i < t.n; i++) same = g_lockHeldWheel[i] == t.w[i].wheel;
        if (same)
        {
            for (int i = 0; i < t.n; i++) g_lockHeld.next[i] = t.w[i].ok ? t.w[i].pre : NAN;
            InterlockedIncrement(&g_lockLastSeq);
            g_lockLast = g_lockHeld;
            InterlockedIncrement(&g_lockLastSeq);
        }
    }
    float pre[kCapWheels], gain[kCapWheels], load[kCapWheels], radius[kCapWheels];
    bool ok[kCapWheels];
    for (int i = 0; i < t.n; i++) pre[i] = t.w[i].pre, gain[i] = t.w[i].gain, load[i] = t.w[i].load, radius[i] = t.w[i].radius, ok[i] = t.w[i].ok;
    const LockStepOut o = LockStepHeld(pre, post, gain, load, radius, ok, t.n, k, mass);
    for (int i = 0; i < t.n; i++) locked[i] = o.final[i];
    if (o.skipped) InterlockedExchangeAdd(&g_lockSkipped, o.skipped);
    if (o.j > 0.0f && o.c < 0.0f) InterlockedIncrement(&g_lockCUnder);
    if (o.under) InterlockedExchangeAdd(&g_lockUnder, o.under);
    if (o.bounded) InterlockedIncrement(&g_lockBounded);
    if (o.breach) InterlockedExchangeAdd(&g_lockBreach, o.breach);
    if (!o.wrote) return false;
    g_lockHeld.vehicle = vehicle;
    g_lockHeld.n = t.n;
    g_lockHeld.truck = isfinite(truck) ? truck : TruckSpeedInWheels(t.forward, radius, ok, t.n, t.sign);
    g_lockHeld.ceiling = 0.0f;
    for (int i = 0; i < t.n; i++)
    {
        g_lockHeldWheel[i] = t.w[i].wheel;
        g_lockHeld.pre[i] = t.w[i].ok ? pre[i] : NAN;
        g_lockHeld.post[i] = t.w[i].ok ? post[i] : NAN;
        g_lockHeld.final[i] = t.w[i].ok ? o.final[i] : NAN;
        g_lockHeld.load[i] = load[i], g_lockHeld.gain[i] = gain[i], g_lockHeld.radius[i] = radius[i];
        g_lockHeld.capped[i] = g_lockHeld.final[i];
        g_lockHeld.ground[i] = t.w[i].ground, g_lockHeld.sink[i] = t.w[i].sink, g_lockHeld.ice[i] = t.w[i].ice;
    }
    g_lockHeldOn = true;
    InterlockedIncrement(&g_lockWrites);
    InterlockedExchange(&g_lockWheels, o.used);
    InterlockedExchange(&g_lockKBits, FloatBits(k));
    InterlockedExchange(&g_lockBeforeBits, FloatBits(o.spreadBefore));
    InterlockedExchange(&g_lockAfterBits, FloatBits(o.spreadAfter));
    InterlockedExchange(&g_lockCBits, FloatBits(o.c));
    InterlockedExchange(&g_lockRBits, FloatBits(o.r));
    InterlockedExchange(&g_lockOverBits, FloatBits(o.over));
    InterlockedExchange64(&g_lockVehicle, (LONG64)vehicle);
    return true;
}

// The cap's state, the physics thread's own (one vehicle under the cut): the arming, the kept share smoothed. A clear
// asks for a reset through g_capReset. For the detour on the same thread inside the drive: the kept share. For the mod's
// thread: the counters.
static uint64_t g_capVehicle = 0;
static CapArm g_capArm = { 0x7FFFFFFF, false };
static float g_capKept = 1.0f;
static volatile LONG g_capReset = 1;
static volatile LONG64 g_capKeptVehicle = 0;
static volatile LONG g_capKeptBits = 0x3F800000; // 1.0f
static volatile LONG g_capClamps = 0, g_capSteps = 0, g_capRefused = 0, g_capArmedSteps = 0, g_capWaitSteps = 0, g_capFaults = 0, g_capClampedNow = 0, g_capArmedNow = 0;
static volatile LONG g_capExcessMost = 0, g_capPreMost = 0, g_capSpreadBits = 0;
// M5 of BRIEF_governor_sway.md: the driven mean after the drive (before the ceiling), summed over the steps and its
// largest since the last read; the least driven wheel before the drive since the last read (kCapNone: no step); the
// last step's ceiling.
static const float kCapNone = 1e9f;
static volatile LONG g_capPostSum = 0, g_capPostMost = 0, g_capPreLeast = 0x4E6E6B28 /* 1e9f */, g_capCeiling = 0;
// the truck's own speed of this step for the ceiling: no number before the drive, the torque hook's inside it
static volatile LONG g_capTruck = 0x7FC00000;
static void CapMost(volatile LONG &bits, float x)
{
    if (isfinite(x) && x > BitsFloat(bits)) InterlockedExchange(&bits, FloatBits(x));
}
static void CapAdd(volatile LONG &bits, float x)
{
    if (isfinite(x) && x > 0.0f) InterlockedExchange(&bits, FloatBits(BitsFloat(bits) + x));
}

// The drive with the cap around it: the wheels read before, the arming's state (M3), the original drive, the wheels
// after, the ceiling by the truck's own speed of this step (the torque hook's, inside the drive; none where it did not
// answer in this step), the cap's step with the spread (CapStep), the writes and the kept share for the stand-in (M1).
// A read that faults leaves the drive's own result.
struct CapAround
{
    int clamps;
    float kept;
    bool clamped[kCapWheels];
    uint8_t *wheel[kCapWheels];
    int n;
};
// ---- Open by axle: the driven truck's axles (BRIEF_open_by_axle.md; torque_cut.h, AxleGroups) ----
// Made here, on the physics thread, in a step with Open in force: reads only, under the fault guard, no heap, no lock.
// Every wheel of the truck (vehicle + 0x200 to + 0x208, driven or not) with its parent body (wheel + 0x10) and its
// centre (wheel + 0x13C, world) in that body's own frame (the body's forward at + 0x170, left at + 0x190, place at
// + 0x1A0). Kept by the wheel objects' pointers: made when the vehicle or one of its wheel pointers is another (the
// list is compared each step, the grouping is not made each step), and looked at again every kAxleLookAgain Open steps
// while a wheel stands alone, where only a result with fewer wheels alone is taken (a truck read before its wheels were
// placed). A read that faults leaves every wheel an axle of its own (M2).
struct AxleTable
{
    uint64_t vehicle;
    int n;
    uint64_t wheel[kCapWheels];
    int axle[kCapWheels];
    int alone, since;
};
static const int kAxleLookAgain = 240;
static const int kAxleSay = kDiffAxleSay; // gearbox_hooks.h, DiffCounts
static AxleTable g_axle; // the physics thread's own
static volatile LONG g_axleGen = 0, g_axleAxles = 0, g_axleAlone = 0, g_axleWheels = 0, g_axleFaults = 0;
static volatile LONG64 g_axleVehicle = 0;
static volatile LONG g_axleSayN = 0, g_axleSayMean[kAxleSay] = {}, g_axleSayK[kAxleSay] = {}, g_axleSayWheels[kAxleSay] = {}, g_axleSayGot[kAxleSay] = {};
static bool AxleWheelsRead(const void *vehicle, uint8_t **wheel, int &n)
{
    n = 0;
    __try
    {
        const uint8_t *v = (const uint8_t *)vehicle;
        const uintptr_t begin = *(const uintptr_t *)(v + 0x200), end = *(const uintptr_t *)(v + 0x208);
        if (end < begin || (end - begin) / 8 > (uintptr_t)kCapWheels) return false;
        for (uintptr_t p = begin; p < end; p += 8)
        {
            const uint8_t *model = *(const uint8_t *const *)p;
            if ((uintptr_t)model < 0x10000) continue;
            uint8_t *w = *(uint8_t *const *)(model + 0x2C8);
            if ((uintptr_t)w < 0x10000) continue;
            wheel[n++] = w;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        n = 0;
        return false;
    }
}
static bool AxleMake(uint8_t *const *wheel, int n, AxleGroupsOut &g)
{
    uint64_t body[kCapWheels];
    float along[kCapWheels], side[kCapWheels];
    bool ok[kCapWheels];
    __try
    {
        for (int i = 0; i < n; i++)
        {
            ok[i] = false, body[i] = 0, along[i] = side[i] = 0.0f;
            const uint8_t *b = *(const uint8_t *const *)(wheel[i] + 0x10);
            if ((uintptr_t)b < 0x10000) continue;
            CapVec c, at, f, l;
            CapGet3(wheel[i] + 0x13C, c);
            CapGet3(b + 0x1A0, at);
            CapGet3(b + 0x170, f);
            CapGet3(b + 0x190, l);
            const float cc[3] = { c.x, c.y, c.z }, aa[3] = { at.x, at.y, at.z }, ff[3] = { f.x, f.y, f.z }, ll[3] = { l.x, l.y, l.z };
            ok[i] = AxlePlace(cc, aa, ff, ll, along[i], side[i]);
            body[i] = (uint64_t)(uintptr_t)b;
        }
        g = AxleGroups(body, along, side, ok, n);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
static void AxleTableStep(uint64_t vehicle)
{
    uint8_t *now[kCapWheels];
    int n = 0;
    if (!AxleWheelsRead((const void *)(uintptr_t)vehicle, now, n))
    {
        InterlockedIncrement(&g_axleFaults);
        g_axle.vehicle = 0, g_axle.n = 0;
        return;
    }
    bool same = g_axle.vehicle == vehicle && g_axle.n == n;
    for (int i = 0; same && i < n; i++) same = g_axle.wheel[i] == (uint64_t)(uintptr_t)now[i];
    if (same && !(g_axle.alone > 0 && ++g_axle.since >= kAxleLookAgain)) return;
    g_axle.since = 0;
    AxleGroupsOut g = {};
    if (!AxleMake(now, n, g))
    {
        InterlockedIncrement(&g_axleFaults);
        if (same) return;
        for (int i = 0; i < n; i++) g.axle[i] = i;
        g.axles = g.alone = n;
    }
    else if (same && g.alone >= g_axle.alone) return;
    g_axle.vehicle = vehicle, g_axle.n = n, g_axle.alone = g.alone;
    for (int i = 0; i < n; i++) g_axle.wheel[i] = (uint64_t)(uintptr_t)now[i], g_axle.axle[i] = g.axle[i];
    InterlockedExchange64(&g_axleVehicle, (LONG64)vehicle);
    InterlockedExchange(&g_axleAxles, g.axles);
    InterlockedExchange(&g_axleAlone, g.alone);
    InterlockedExchange(&g_axleWheels, n);
    InterlockedIncrement(&g_axleGen);
}
// The driven wheels' axles from the table: a wheel not in it is an axle of its own (M2). place: the wheel's place in the
// truck's own list, for the probe.
static void AxleLook(const CapTruckRead &t, int *axle, int *place)
{
    for (int i = 0; i < t.n; i++) axle[i] = AxleOfWheel(g_axle.wheel, g_axle.axle, g_axle.n, (uint64_t)(uintptr_t)t.w[i].wheel, kCapWheels + i, &place[i]);
}
// The limit by axle, with each axle's mean, factor and wheels left for the probe (the first kAxleSay of them; the wheels
// a bit each by their place in the truck's list). A ceiling of 0 limits nothing and only leaves the figures.
static CapStepOut OpenAxlesStep(const float *pre, const float *even, const float *gain, const bool *ok, const int *axle, const int *place, int n, float ceiling)
{
    static OpenAxlesOut r; // the physics thread's own
    r = DiffOpenAxles(pre, even, gain, ok, axle, n, ceiling, true);
    const int say = r.axles < kAxleSay ? r.axles : kAxleSay;
    for (int a = 0; a < say; a++)
    {
        LONG wheels = 0;
        for (int i = 0; i < n; i++)
            if ((r.wheels[a] >> i & 1u) && place[i] >= 0) wheels |= (LONG)(1u << place[i]);
        InterlockedExchange(&g_axleSayMean[a], FloatBits(r.mean[a]));
        InterlockedExchange(&g_axleSayK[a], FloatBits(r.k[a]));
        InterlockedExchange(&g_axleSayGot[a], FloatBits(r.got[a]));
        InterlockedExchange(&g_axleSayWheels[a], wheels);
    }
    InterlockedExchange(&g_axleSayN, say);
    return r.o;
}

// force: what is in force this step (DiffForceOf). Open: by axle (the truck's axles above): the impulses of each axle's
// wheels evened (DiffEvenAxles), then the ceiling on each axle's mean (DiffOpenAxles: one factor an axle on what the drive
// added; only where the slot has a cap: L-, L, L+ and R) and nothing passed on, in the axle or between axles. Else the lock's step where lockK is above 0, the ceiling with the spread. Where the slot
// has no cap (a numbered gear) the evening and the lock act alone. A change of what is in force mid-drive (the lock
// set with a free wheel far over the ceiling) lowers no wheel: the step's limit is never under the wheel's own speed
// before the drive, and what is handed on is counted from that limit (F1 of the review).
static CapAround CapStepAround(const TorqueCutSlot *cut, void *action, float throttle, float dt, float lockK, int force, float mass)
{
    CapAround out = {};
    out.kept = 1.0f;
    const uint64_t vehicle = cut->vehicle;
    const bool open = force == kDiffForceOpen;
    if (g_capVehicle != vehicle)
    {
        g_capVehicle = vehicle;
        g_capArm = CapArm{ 0x7FFFFFFF, false };
        g_capKept = 1.0f;
    }
    static CapTruckRead t; // the physics thread's own: one vehicle under the cut, one call at a time
    const bool readOk = CapReadBefore((const void *)(uintptr_t)vehicle, action, t, cut->cylinderVtable);
    if (!readOk) InterlockedIncrement(&g_capFaults);
    if (t.refused) InterlockedExchangeAdd(&g_capRefused, t.refused);
    const bool capHere = cut->cap > 0.0f && isfinite(cut->cap);
    const float cap = capHere ? cut->cap : 0.0f;
    float mean = 0.0f, preMost = 0.0f, preLeast = kCapNone;
    int m = 0;
    for (int i = 0; i < t.n; i++)
        if (t.w[i].ok) mean += t.w[i].pre, m++, preMost = fmaxf(preMost, t.w[i].pre - cap), preLeast = fminf(preLeast, t.w[i].pre);
    if (m) mean /= (float)m;
    const bool armed = readOk && CapArmStep(g_capArm, t.gear, mean, m > 0, cap);
    InterlockedExchange64(&g_capKeptVehicle, (LONG64)vehicle);
    InterlockedExchange(&g_capArmedNow, armed ? 1 : 0);
    InterlockedExchange(&g_capTruck, 0x7FC00000);
    DriveCall(action, throttle, dt);
    const float truck = BitsFloat(g_capTruck);
    // in Open the ceiling is the limit of the driven wheels' mean, else of each wheel
    const float ceiling = CapCeiling(cap, cut->map.top, cut->map.governTo, truck);
    InterlockedExchange(&g_capCeiling, FloatBits(ceiling));
    if (!readOk)
    {
        InterlockedExchange(&g_capClampedNow, 0);
        return out;
    }
    if (capHere)
    {
        InterlockedIncrement(armed ? &g_capArmedSteps : &g_capWaitSteps);
        CapMost(g_capPreMost, preMost);
        if (preLeast < BitsFloat(g_capPreLeast)) InterlockedExchange(&g_capPreLeast, FloatBits(preLeast));
    }
    float pre[kCapWheels], post[kCapWheels], gain[kCapWheels], work[kCapWheels];
    bool ok[kCapWheels];
    for (int i = 0; i < t.n; i++) pre[i] = t.w[i].pre, post[i] = t.w[i].pre, gain[i] = t.w[i].gain, ok[i] = t.w[i].ok;
    volatile LONG &faults = capHere ? g_capFaults : open ? g_evenFaults : g_lockFaults;
    if (!CapReadAfter(t, post))
    {
        InterlockedIncrement(&faults);
        return out;
    }
    // on what the drive left: Open's evening, or the lock; then the ceiling on those speeds; the write against post
    float postMean = 0.0f;
    for (int i = 0; i < t.n; i++)
    {
        work[i] = post[i];
        if (ok[i] && m) postMean += post[i] / (float)m;
    }
    if (m && capHere)
    {
        CapAdd(g_capPostSum, postMean);
        CapMost(g_capPostMost, postMean);
    }
    int axle[kCapWheels], place[kCapWheels];
    bool lockWrote = false;
    if (open)
    {
        AxleTableStep(vehicle);
        AxleLook(t, axle, place);
        const DiffEvenOut e = DiffEvenAxles(pre, post, gain, ok, axle, t.n);
        for (int i = 0; i < t.n; i++) work[i] = e.final[i];
        InterlockedIncrement(&g_evenSteps);
        if (e.wrote) InterlockedIncrement(&g_evenWrites);
        InterlockedExchange(&g_evenN, e.used);
        InterlockedExchange(&g_evenJBits, FloatBits(e.j));
    }
    else if (lockK > 0.0f) lockWrote = LockOver(t, post, lockK, work, vehicle, mass, truck);
    if (action == g_showNow) ShowNoteWheels(t, post, work);
    if (!capHere)
    {
        // no cap in this gear: the evening or the lock alone
        if (open) OpenAxlesStep(pre, work, gain, ok, axle, place, t.n, 0.0f);
        if (!CapWriteFinal(t, post, work)) InterlockedIncrement(&faults);
        return out;
    }
    const CapStepOut o = open ? OpenAxlesStep(pre, work, gain, ok, axle, place, t.n, ceiling) : CapStep(pre, work, gain, ok, t.n, ceiling, true);
    if (!CapWrite(t, post, o)) InterlockedIncrement(&g_capFaults);
    if (lockWrote)
    {
        // the lock's kept step: what the cap made of the lock's speeds, and the ceiling it went by
        g_lockHeld.ceiling = ceiling;
        for (int i = 0; i < t.n; i++) g_lockHeld.capped[i] = ok[i] ? o.final[i] : NAN;
    }
    InterlockedIncrement(&g_capSteps);
    if (o.clamps) InterlockedExchangeAdd(&g_capClamps, o.clamps);
    CapMost(g_capExcessMost, o.excess);
    CapAdd(g_capSpreadBits, o.spread);
    InterlockedExchange(&g_capClampedNow, o.clamps);
    g_capKept = CapKeptSmooth(g_capKept, o.kept, g_rateOn ? CapKeptOldAt(dt) : kCapKeptOld); // S4: by the clock at 120
    InterlockedExchange(&g_capKeptBits, FloatBits(g_capKept));
    out.clamps = o.clamps;
    out.kept = o.kept;
    out.n = t.n;
    for (int i = 0; i < t.n; i++) out.clamped[i] = o.clamped[i], out.wheel[i] = t.w[i].wheel;
    if (action == g_showNow)
        for (int i = 0; i < t.n; i++) g_showNote.capped[i] = o.clamped[i];
    return out;
}

// ---- the chassis push taken back (BRIEF_chassis_push.md; torque_cut.h, PushTakeBack) ----
// The mod's thread publishes the factor's assumption for the driven truck's action (PushGive) while it writes the torque
// factor. Around the original drive the distinct parent bodies of the driven wheels (wheel + 0x10) are read before and
// after, and k of their change taken back before the game's brakes (which touch only the wheels). A parent with an
// inverse mass of 0 is skipped. No lock, no heap, no log.
static PushSlot g_pushSlots[2];
static int g_pushNext = 0;
static void *volatile g_pushSlot = nullptr;
static volatile LONG g_pushSteps = 0, g_pushBacks = 0, g_pushKicks = 0, g_pushFaults = 0, g_pushSkipped = 0;
static volatile LONG g_pushGivenBits = 0, g_pushBackBits = 0, g_pushK = 0, g_pushFx = 0, g_pushF = 0, g_pushB4 = 0, g_pushSpeed = 0;
static const int kPushBodies = 8;
struct PushRead
{
    uint8_t *body[kPushBodies];
    PushBody b[kPushBodies];
    bool capWheel[kPushBodies]; // the body carries a wheel the cap held this step
    int n;
    float forward; // the truck's forward speed, m/s (the main body, as d73120)
    bool may;      // PushMayTake at the call's start: over 3 m/s, no clunk (+ 0x70 = + 0x74), no stutter (+ 0xB8)
};
static bool PushReadBefore(const void *vehicle, const void *action, PushRead &r)
{
    r.n = 0;
    r.forward = NAN;
    r.may = false;
    __try
    {
        const uint8_t *v = (const uint8_t *)vehicle;
        if (*(const void *const volatile *)(v + 0x68) != action) return false;
        const bool awd = *(const volatile uint8_t *)((const uint8_t *)action + 0x49) != 0;
        const uintptr_t begin = *(const uintptr_t *)(v + 0x200), end = *(const uintptr_t *)(v + 0x208);
        if (end < begin || (end - begin) / 8 > (uintptr_t)kCapWheels) return false;
        for (uintptr_t p = begin; p < end; p += 8)
        {
            const uint8_t *model = *(const uint8_t *const *)p;
            if ((uintptr_t)model < 0x10000) continue;
            const uint8_t *wheel = *(const uint8_t *const *)(model + 0x2C8);
            if ((uintptr_t)wheel < 0x10000) continue;
            const int type = *(const volatile int *)(wheel + 0xE8);
            if (!(type == 1 || (type == 2 && awd))) continue;
            uint8_t *parent = *(uint8_t *const *)(wheel + 0x10);
            if ((uintptr_t)parent < 0x10000) continue;
            bool seen = false;
            for (int i = 0; i < r.n; i++) seen = seen || r.body[i] == parent;
            if (seen || r.n >= kPushBodies) continue;
            PushBody &b = r.b[r.n];
            b = PushBody{};
            CapGet3(parent + 0x230, b.v0);
            CapGet3(parent + 0x240, b.w0);
            b.invMass = *(const volatile float *)(parent + 0x22C);
            r.capWheel[r.n] = false;
            r.body[r.n++] = parent;
        }
        const uint8_t *model = *(const uint8_t *const *)(v + 0x60);
        const uint8_t *main = (uintptr_t)model >= 0x10000 ? *(const uint8_t *const *)(model + 0x230) : nullptr;
        if ((uintptr_t)main >= 0x10000)
        {
            CapVec vel, f;
            CapGet3(main + 0x230, vel);
            CapGet3(main + 0x170, f);
            r.forward = CapDot(vel, f);
        }
        const uint8_t *a = (const uint8_t *)action;
        r.may = PushMayTake(r.forward, *(const volatile int *)(a + 0x70), *(const volatile int *)(a + 0x74), *(const volatile float *)(a + 0xB8));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        r.n = 0;
        return false;
    }
}
static bool PushReadAfter(PushRead &r)
{
    __try
    {
        for (int i = 0; i < r.n; i++)
        {
            CapGet3(r.body[i] + 0x230, r.b[i].v1);
            CapGet3(r.body[i] + 0x240, r.b[i].w1);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
// k for each body (kAll for all, or the cap's for a body with a held wheel), written; the counters.
static void PushTake(PushRead &r, float kAll, float kCap)
{
    float given = 0.0f, taken = 0.0f;
    bool any = false;
    if (!r.may) kAll = kCap = 0.0f; // the kick path's push carries no factor: the game's own, whole
    __try
    {
        for (int i = 0; i < r.n; i++)
        {
            const PushBody &b = r.b[i];
            const CapVec dv = { b.v1.x - b.v0.x, b.v1.y - b.v0.y, b.v1.z - b.v0.z };
            const float j = b.invMass > 0.0f ? sqrtf(CapDot(dv, dv)) / b.invMass : 0.0f;
            if (isfinite(j)) given += j;
            if (!(b.invMass > 0.0f)) InterlockedIncrement(&g_pushSkipped);
            const float k = fmaxf(kAll, r.capWheel[i] ? kCap : 0.0f);
            CapVec v, w;
            if (!PushBodyBack(b, k, v, w)) continue;
            volatile float *pv = (volatile float *)(r.body[i] + 0x230), *pw = (volatile float *)(r.body[i] + 0x240);
            pv[0] = v.x, pv[1] = v.y, pv[2] = v.z;
            pw[0] = w.x, pw[1] = w.y, pw[2] = w.z;
            if (isfinite(j)) taken += k * j;
            any = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        InterlockedIncrement(&g_pushFaults);
    }
    InterlockedIncrement(&g_pushSteps);
    if (any) InterlockedIncrement(&g_pushBacks);
    // the kick path pushes at any speed: a push measured at 3 m/s or under, where s is 0, is the kick's, and so is one on
    // a call that started with the clunk or the stutter; counted, never taken back
    if (given > 0.0f && !r.may) InterlockedIncrement(&g_pushKicks);
    CapAdd(g_pushGivenBits, given);
    CapAdd(g_pushBackBits, taken);
    InterlockedExchange(&g_pushK, FloatBits(fmaxf(kAll, kCap)));
    InterlockedExchange(&g_pushSpeed, FloatBits(r.forward));
}

// The creep's larger of the two throttles, as the stub's maxss had it (the share where the game's is not a number),
// then the step probe's look, then on into the drive with the step's seconds as they came. While the torque cut has the
// action, its throttle is 1 and B4 is written 1 instead (the Reviewer's S2).
static void CreepDriveInner(void *action, float throttle, float dt)
{
    InterlockedIncrement(&g_creepCalls);
    if (g_rateOn) RateWorldLook(dt);
    const LONG thread = (LONG)GetCurrentThreadId();
    if (thread != g_driveThread) DriveThreadNote(thread);
    g_driveThread = thread;
    const TorqueCutSlot *cut = (const TorqueCutSlot *)g_cutSlot;
    const CreepSlot *slot = (const CreepSlot *)g_creepSlot;
    uint64_t now = 0;
    bool capOn = false;
    if (InterlockedExchange(&g_capReset, 0)) g_capVehicle = 0;
    if (cut && (uint64_t)(uintptr_t)action == cut->action && (now = GetTickCount64()) >= cut->at && now - cut->at <= kTorqueFresh)
    {
        InterlockedIncrement(&g_cutHolds);
        throttle = 1.0f;
        CutPullOne(action);
        capOn = cut->cap > 0.0f && isfinite(cut->cap);
    }
    else if (slot && (uint64_t)(uintptr_t)action == slot->action)
    {
        InterlockedIncrement(&g_creepHits);
        throttle = _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(throttle), _mm_set_ss(slot->share))); // maxss itself: a ?: may be built with its operands swapped
    }
    if (const DriveStepFn step = g_driveStep) step((uint64_t)(uintptr_t)action, dt);
    // the chassis push: the factor's assumption for this action (D, the factor written), or the cap's part (a held gear)
    const PushSlot *push = (const PushSlot *)g_pushSlot;
    const bool cutHere = cut && (uint64_t)(uintptr_t)action == cut->action && now && now >= cut->at && now - cut->at <= kTorqueFresh;
    // the differential (M4): the lock byte and the handbrake once a step, and with the truck's type (the cut's slot;
    // Limited slip without the cut) what is in force. The lock's slot for this action, fresh: k from DiffLockBite and
    // this step's seconds while the lock is in force
    const LockSlot *lock = (const LockSlot *)g_lockSlot;
    uint64_t lockAt = 0;
    const bool lockHere = lock && (uint64_t)(uintptr_t)action == lock->action && lock->bite > 0 && (lockAt = GetTickCount64()) >= lock->at && lockAt - lock->at <= kTorqueFresh;
    bool lockByte = false, handbrake = false;
    const bool bytesOk = (cutHere || lockHere) && DiffBytesRead(action, lockByte, handbrake);
    const int force = DiffForceOf(cutHere ? cut->diff : kDiffLimited, lockByte, handbrake);
    if (cutHere) InterlockedExchange(&g_diffForceNow, force);
    float lockK = 0.0f;
    if (lockHere)
    {
        InterlockedIncrement(&g_lockCalls);
        if (!bytesOk) InterlockedIncrement(&g_lockFaults);
        else if (force != kDiffForceLock) InterlockedIncrement(lockByte ? &g_lockHand : &g_lockOff);
        else lockK = LockK(lock->bite, dt);
    }
    const bool evenOn = cutHere && force == kDiffForceOpen;
    if (action == g_showNow) g_showNote.force = lockK > 0.0f ? kDiffForceLock : cutHere ? force : kDdGameOwn;
    uint64_t at = 0;
    const bool pushOn = !cutHere && push && (uint64_t)(uintptr_t)action == push->action && push->back > 0.0f && (at = GetTickCount64()) >= push->at && at - push->at <= kTorqueFresh;
    const bool capPush = capOn && cut->pushBack > 0.0f;
    static PushRead pr; // the physics thread's own: one driven truck, one call at a time
    const uint64_t pushVehicle = pushOn ? push->vehicle : capPush ? cut->vehicle : 0;
    const bool pushRead = pushVehicle && PushReadBefore((const void *)(uintptr_t)pushVehicle, action, pr);
    if (pushVehicle && !pushRead) InterlockedIncrement(&g_pushFaults);
    float b4 = NAN;
    if (pushOn && pushRead) b4 = CutB4Read(action);
    if (!capOn && !evenOn)
    {
        // the lock without the cap: its own read block (one driven truck, one call at a time)
        static CapTruckRead lr;
        const bool lockRead = lockK > 0.0f && CapReadBefore((const void *)(uintptr_t)lock->vehicle, action, lr, lock->cylinderVtable);
        if (lockK > 0.0f && !lockRead) InterlockedIncrement(&g_lockFaults);
        InterlockedExchange(&g_capTruck, 0x7FC00000);
        DriveCall(action, throttle, dt);
        if (lockRead)
        {
            float post[kCapWheels], locked[kCapWheels];
            for (int i = 0; i < lr.n; i++) post[i] = lr.w[i].pre;
            if (!CapReadAfter(lr, post)) InterlockedIncrement(&g_lockFaults);
            else
            {
                LockOver(lr, post, lockK, locked, lock->vehicle, lock->mass, BitsFloat(g_capTruck));
                if (action == g_showNow) ShowNoteWheels(lr, post, locked);
                if (!CapWriteFinal(lr, post, locked)) InterlockedIncrement(&g_lockFaults);
            }
        }
        if (pushOn && pushRead && PushReadAfter(pr))
        {
            const float fx = PushExcess(b4, throttle, push->assumed);
            InterlockedExchange(&g_pushFx, FloatBits(fx));
            InterlockedExchange(&g_pushF, FloatBits(push->factorOver));
            InterlockedExchange(&g_pushB4, FloatBits(b4));
            PushTake(pr, PushTakeBack(b4, throttle, push->assumed, push->factorOver, push->back), 0.0f);
        }
        return;
    }
    const bool lockSame = lockHere && lock->vehicle == cut->vehicle;
    const CapAround around = CapStepAround(cut, action, throttle, dt, lockSame ? lockK : 0.0f, force, lockSame ? lock->mass : 0.0f);
    if (action == g_showNow) g_showNote.kept = around.kept;
    if (capPush && pushRead && PushReadAfter(pr))
    {
        // a parent carries a held wheel when one of the wheels the cap held this step hangs on it
        for (int i = 0; i < pr.n; i++)
        {
            bool held = false;
            __try
            {
                for (int k = 0; k < around.n; k++)
                    held = held || (around.clamped[k] && *(uint8_t *const *)(around.wheel[k] + 0x10) == pr.body[i]);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                held = false;
            }
            pr.capWheel[i] = held;
        }
        PushTake(pr, 0.0f, PushCapBack(around.clamps > 0, around.kept, cut->pushBack));
    }
}

// ---- the drive display's feed (BRIEF_drive_display.md; drive_display.h) ----
// The mod's thread publishes the driven truck's action and vehicle while DriveDisplay=1 (DriveShowGive). Around that
// action's drive call, whatever the gear and the settings: the display's own list of the truck's first 16 wheels, driven
// or not (M1: the cap's list, order and indices are not touched), each wheel's speed about its axle before the call,
// right after the game's own drive (ShowMid, from DriveCall) and after the mod's cap and lock, the wheel body's own
// speed along its rolling direction, and what the inner hook noted of the cap and the lock (g_showNote). Averaged by the
// step's seconds (DdStep) and given out with a sequence count, odd while written (DriveShowGet, as LockLastGet).
// Reads only. No lock, no heap, no log. With no slot the hook's cost is the tests of two pointers.
struct ShowWheel
{
    uint8_t *wheel, *body;
    CapVec axle;
    float alen, gain, radius, pre, mid, truck;
    bool ok, driven, air;
};
struct ShowRead
{
    ShowWheel w[kDdWheels];
    int n, gear;
    float sign;
};
struct ShowLay
{
    uint64_t vehicle;
    int n, rows, since;
    uint64_t wheel[kDdWheels];
    uint8_t row[kDdWheels];
    bool right[kDdWheels];
    bool whole; // every wheel placed and grouped
};
static const int kShowLookAgain = 240; // calls: a list not placed whole is looked at again (a truck read before its wheels were placed)
static ShowSlot g_showSlots[2];
static int g_showNext = 0;
static void *volatile g_showSlot = nullptr;
static ShowRead g_showRead; // one call at a time: g_showBusy keeps a second call out (a truck change, two threads)
static volatile LONG g_showBusy = 0;
static bool g_showMidOk = false;
static DdAvg g_showAvg;
static ShowLay g_showLay;
static DdShown g_showRec;
static volatile LONG g_showSeq = 0, g_showSteps = 0, g_showFaults = 0;

static float ShowNorm(CapVec &v)
{
    const float len = sqrtf(CapDot(v, v));
    if (!(len > 1e-6f) || !isfinite(len)) return 0.0f;
    v.x /= len, v.y /= len, v.z /= len;
    return len;
}
static bool ShowReadBefore(const void *vehicle, const void *action, ShowRead &r, uint64_t cylinderVtable)
{
    r.n = 0;
    __try
    {
        const uint8_t *v = (const uint8_t *)vehicle;
        if (*(const void *const volatile *)(v + 0x68) != action) return false;
        const uint8_t *a = (const uint8_t *)action;
        const bool awd = *(const volatile uint8_t *)(a + 0x49) != 0;
        r.gear = *(const volatile int *)(a + 0x74);
        r.sign = r.gear < 0 ? -1.0f : 1.0f;
        // the chassis's own forward speed, for a wheel whose rolling direction is not read
        float chassis = NAN;
        CapVec mainF = {};
        bool mainOk = false;
        const uint8_t *vm = *(const uint8_t *const *)(v + 0x60);
        const uint8_t *main = (uintptr_t)vm >= 0x10000 ? *(const uint8_t *const *)(vm + 0x230) : nullptr;
        if ((uintptr_t)main >= 0x10000)
        {
            CapVec vel;
            CapGet3(main + 0x230, vel);
            CapGet3(main + 0x170, mainF);
            chassis = r.sign * CapDot(vel, mainF);
            mainOk = true;
        }
        const uintptr_t begin = *(const uintptr_t *)(v + 0x200), end = *(const uintptr_t *)(v + 0x208);
        if (end < begin) return false;
        for (uintptr_t p = begin; p < end && r.n < kDdWheels; p += 8) // over 16 wheels: the first 16 (M3)
        {
            const uint8_t *model = *(const uint8_t *const *)p;
            if ((uintptr_t)model < 0x10000) continue;
            uint8_t *wheel = *(uint8_t *const *)(model + 0x2C8);
            if ((uintptr_t)wheel < 0x10000) continue;
            ShowWheel &w = r.w[r.n++];
            w = ShowWheel{};
            w.wheel = wheel;
            w.pre = w.mid = w.truck = NAN;
            const int type = *(const volatile int *)(wheel + 0xE8);
            uint8_t *body = *(uint8_t *const *)(wheel + 0x18);
            if (!CapBodyOk(body)) continue; // a body refused: an empty cell
            w.body = body;
            w.driven = type == 1 || (type == 2 && awd);
            CapVec omega, vel;
            CapGet3(wheel + 0x154, w.axle);
            CapGet3(body + 0x240, omega);
            CapGet3(body + 0x230, vel);
            w.alen = sqrtf(CapDot(w.axle, w.axle));
            w.pre = w.mid = CapSpeed(omega, w.axle, r.sign);
            if (w.driven)
            {
                CapVec localAxle = {}, invI;
                const uint8_t *desc = *(const uint8_t *const *)(wheel + 0x28);
                if ((uintptr_t)desc >= 0x10000) CapGet3(desc + 0x100, localAxle);
                CapGet3(body + 0x220, invI);
                w.gain = CapGain(localAxle, invI, w.alen);
            }
            w.air = !(*(const volatile float *)(wheel + 0x128) > 0.0f); // S2: the contact share for now
            float cylinder = NAN;
            const bool cylinderRead = CutCylinderRead(wheel, cylinderVtable, cylinder);
            w.radius = WheelRollRadius(*(const volatile float *)(wheel + 0x94), cylinder, cylinderRead).r;
            w.ok = w.alen > 1e-6f && isfinite(w.alen) && w.radius > 0.0f && isfinite(w.radius);
            // S1: the wheel body's own speed along its rolling direction: the main body's forward less the part along
            // the axle, so a steered wheel and an articulated rear follow by the axle. Not the parent's forward: a
            // parent can be a rocker or a bone with a swing of its own (the review of 131, S2)
            w.truck = chassis;
            if (w.ok && mainOk)
            {
                CapVec f = mainF, n = w.axle;
                ShowNorm(n);
                const float along = CapDot(f, n);
                CapVec d = { f.x - along * n.x, f.y - along * n.y, f.z - along * n.z };
                if (ShowNorm(d) > 0.5f) w.truck = r.sign * CapDot(vel, d);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        r.n = 0;
        return false;
    }
}
// Each wheel's speed again: into mid right after the game's own drive, into out after the whole hook.
static bool ShowSpeeds(const ShowRead &r, float *out)
{
    __try
    {
        for (int i = 0; i < r.n; i++)
        {
            out[i] = NAN;
            if (!r.w[i].ok) continue;
            CapVec omega;
            CapGet3(r.w[i].body + 0x240, omega);
            out[i] = CapSpeed(omega, r.w[i].axle, r.sign);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
static void ShowMid()
{
    float mid[kDdWheels];
    if (!ShowSpeeds(g_showRead, mid)) return;
    for (int i = 0; i < g_showRead.n; i++) g_showRead.w[i].mid = mid[i];
    g_showMidOk = true;
}
// The cells' rows and sides for this list of wheels: the axles as Open by axle groups them (a wheel's place in its
// parent body's own frame), the rows ordered by the places in the main body's frame. Made when the list is another,
// and again every kShowLookAgain calls while a wheel was not placed. A read that faults: one wheel a row.
static void ShowLayMake(uint64_t vehicle, const ShowRead &r)
{
    ShowLay &l = g_showLay;
    bool same = l.vehicle == vehicle && l.n == r.n;
    for (int i = 0; same && i < r.n; i++) same = l.wheel[i] == (uint64_t)(uintptr_t)r.w[i].wheel;
    if (same && (l.whole || ++l.since < kShowLookAgain)) return;
    uint64_t body[kDdWheels];
    float along[kDdWheels], side[kDdWheels], rowAt[kDdWheels];
    bool ok[kDdWheels];
    int axle[kDdWheels];
    bool grouped = false;
    for (int i = 0; i < r.n; i++) ok[i] = false, body[i] = 0, along[i] = side[i] = rowAt[i] = 0.0f, axle[i] = i;
    __try
    {
        const uint8_t *vm = *(const uint8_t *const *)((const uint8_t *)(uintptr_t)vehicle + 0x60);
        const uint8_t *main = (uintptr_t)vm >= 0x10000 ? *(const uint8_t *const *)(vm + 0x230) : nullptr;
        float ma[3] = {}, mf[3] = {}, ml[3] = {};
        bool mainOk = false;
        if ((uintptr_t)main >= 0x10000)
        {
            CapVec at, f, left;
            CapGet3(main + 0x1A0, at);
            CapGet3(main + 0x170, f);
            CapGet3(main + 0x190, left);
            ma[0] = at.x, ma[1] = at.y, ma[2] = at.z, mf[0] = f.x, mf[1] = f.y, mf[2] = f.z, ml[0] = left.x, ml[1] = left.y, ml[2] = left.z;
            mainOk = true;
        }
        for (int i = 0; i < r.n; i++)
        {
            const uint8_t *b = *(const uint8_t *const *)(r.w[i].wheel + 0x10);
            if ((uintptr_t)b < 0x10000) continue;
            CapVec c, at, f, left;
            CapGet3(r.w[i].wheel + 0x13C, c);
            CapGet3(b + 0x1A0, at);
            CapGet3(b + 0x170, f);
            CapGet3(b + 0x190, left);
            const float cc[3] = { c.x, c.y, c.z }, aa[3] = { at.x, at.y, at.z }, ff[3] = { f.x, f.y, f.z }, ll[3] = { left.x, left.y, left.z };
            ok[i] = AxlePlace(cc, aa, ff, ll, along[i], side[i]);
            body[i] = (uint64_t)(uintptr_t)b;
            float mainSide = 0.0f;
            rowAt[i] = along[i];
            if (ok[i] && mainOk && !AxlePlace(cc, ma, mf, ml, rowAt[i], mainSide)) rowAt[i] = along[i];
        }
        const AxleGroupsOut g = AxleGroups(body, along, side, ok, r.n);
        for (int i = 0; i < r.n; i++) axle[i] = g.axle[i];
        grouped = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        grouped = false;
        InterlockedIncrement(&g_showFaults);
    }
    bool whole = grouped;
    for (int i = 0; i < r.n; i++) whole = whole && ok[i];
    if (same && !whole) // no better than what is kept: wait again
    {
        l.since = 0;
        return;
    }
    l.vehicle = vehicle, l.n = r.n, l.since = 0, l.whole = whole;
    for (int i = 0; i < r.n; i++) l.wheel[i] = (uint64_t)(uintptr_t)r.w[i].wheel;
    l.rows = DdLayout(axle, rowAt, side, ok, r.n, grouped, l.row, l.right);
}
static void CreepDriveInner(void *action, float throttle, float dt);
// The driven truck's drive call with the display on: the read before, the hook as it is, the read after, the record.
static void ShowDrive(const ShowSlot *show, void *action, float throttle, float dt)
{
    ShowRead &r = g_showRead;
    if (!ShowReadBefore((const void *)(uintptr_t)show->vehicle, action, r, show->cylinderVtable))
    {
        InterlockedIncrement(&g_showFaults);
        g_showAvg.has = false;
        CreepDriveInner(action, throttle, dt);
        return;
    }
    g_showNote.force = kDdGameOwn, g_showNote.kept = 1.0f, g_showNote.n = 0;
    g_showMidOk = false;
    g_showNow = action;
    CreepDriveInner(action, throttle, dt);
    g_showNow = nullptr;
    float final[kDdWheels];
    if (!g_showMidOk || !ShowSpeeds(r, final))
    {
        InterlockedIncrement(&g_showFaults);
        g_showAvg.has = false;
        return;
    }
    static DdSample s; // the physics thread's own
    s = DdSample{};
    s.vehicle = show->vehicle;
    s.n = r.n;
    s.gear = r.gear;
    s.force = g_showNote.force;
    s.kept = g_showNote.kept;
    for (int i = 0; i < r.n; i++)
    {
        const ShowWheel &w = r.w[i];
        s.wheel[i] = (uint64_t)(uintptr_t)w.wheel;
        s.ok[i] = w.ok;
        if (!w.ok) continue;
        s.air[i] = w.air;
        s.speed[i] = final[i] / w.alen * w.radius;
        s.truck[i] = w.truck;
        s.driven[i] = w.driven && w.gain > 0.0f && isfinite(w.gain);
        if (!s.driven[i]) continue;
        s.given[i] = (final[i] - w.pre) / w.gain;
        s.before[i] = (w.mid - w.pre) / w.gain;
        for (int k = 0; k < g_showNote.n; k++)
            if (g_showNote.wheel[k] == w.wheel) s.capped[i] = g_showNote.capped[k], s.tied[i] = g_showNote.tied[k];
    }
    DdStep(g_showAvg, s, dt);
    ShowLayMake(show->vehicle, r);
    InterlockedIncrement(&g_showSeq); // odd: being written
    DdShown &o = g_showRec;
    DdShow(g_showAvg, o);
    o.at = GetTickCount64();
    o.rows = 0;
    const bool lay = g_showLay.vehicle == show->vehicle && g_showLay.n == o.n;
    for (int i = 0; i < o.n; i++)
    {
        o.row[i] = lay ? g_showLay.row[i] : (uint8_t)i;
        o.right[i] = lay && g_showLay.right[i];
        if (o.row[i] + 1 > o.rows) o.rows = o.row[i] + 1;
    }
    InterlockedIncrement(&g_showSeq);
    InterlockedIncrement(&g_showSteps);
}
// The drive's hook. With no display slot: one test of a pointer, then the hook as it was. The display's read, note and
// averages are one set: a call that finds another inside (at a truck change the old truck's call can still run on
// another of the physics threads) runs the hook as it is, with no display (the review of 131, S1).
static void CreepDrive(void *action, float throttle, float dt)
{
    const ShowSlot *show = (const ShowSlot *)g_showSlot;
    uint64_t now = 0;
    if (!show || (uint64_t)(uintptr_t)action != show->action || (now = GetTickCount64()) < show->at || now - show->at > kTorqueFresh)
    {
        CreepDriveInner(action, throttle, dt);
        return;
    }
    if (InterlockedCompareExchange(&g_showBusy, 1, 0) != 0)
    {
        CreepDriveInner(action, throttle, dt);
        return;
    }
    ShowDrive(show, action, throttle, dt);
    InterlockedExchange(&g_showBusy, 0);
}

void DriveShowGive(uint64_t action, uint64_t vehicle, uint64_t cylinderVtable)
{
    if (!action || !vehicle)
    {
        InterlockedExchangePointer(&g_showSlot, nullptr);
        return;
    }
    ShowSlot &slot = g_showSlots[g_showNext];
    g_showNext ^= 1;
    slot = ShowSlot{ action, vehicle, GetTickCount64(), cylinderVtable };
    InterlockedExchangePointer(&g_showSlot, &slot);
}

bool DriveShowGet(DdShown &out)
{
    for (int tries = 0; tries < 4; tries++)
    {
        const LONG before = g_showSeq;
        if (!before) return false;
        if (before & 1) continue;
        out = g_showRec;
        if (g_showSeq != before) continue;
        out.seq = (unsigned long)before / 2;
        return true;
    }
    return false;
}

void DriveShowCountsGet(long &steps, long &faults)
{
    steps = g_showSteps;
    faults = g_showFaults;
}

void PushGive(uint64_t action, uint64_t vehicle, float assumed, float factorOver, float back)
{
    if (!action || !vehicle || !(back > 0.0f))
    {
        InterlockedExchangePointer(&g_pushSlot, nullptr);
        return;
    }
    PushSlot &slot = g_pushSlots[g_pushNext];
    g_pushNext ^= 1;
    slot = PushSlot{ action, vehicle, GetTickCount64(), assumed, factorOver, back };
    InterlockedExchangePointer(&g_pushSlot, &slot);
}

void LockGive(uint64_t action, uint64_t vehicle, int bite, int kind, float mass, uint64_t cylinderVtable)
{
    if (!action || !vehicle || bite <= 0)
    {
        InterlockedExchangePointer(&g_lockSlot, nullptr);
        return;
    }
    LockSlot &slot = g_lockSlots[g_lockNext];
    g_lockNext ^= 1;
    slot = LockSlot{ action, vehicle, GetTickCount64(), bite > 100 ? 100 : bite, kind, mass > 0.0f && isfinite(mass) ? mass : 0.0f, cylinderVtable };
    InterlockedExchangePointer(&g_lockSlot, &slot);
}

void LockCountsGet(LockCounts &out)
{
    out.calls = g_lockCalls;
    out.off = g_lockOff;
    out.handbrake = g_lockHand;
    out.writes = g_lockWrites;
    out.faults = g_lockFaults;
    out.skipped = g_lockSkipped;
    out.wheels = (int)g_lockWheels;
    out.k = BitsFloat(g_lockKBits);
    out.spreadBefore = BitsFloat(g_lockBeforeBits);
    out.spreadAfter = BitsFloat(g_lockAfterBits);
    out.vehicle = (uint64_t)g_lockVehicle;
    out.c = BitsFloat(g_lockCBits);
    out.r = BitsFloat(g_lockRBits);
    out.over = BitsFloat(g_lockOverBits);
    out.cUnder = g_lockCUnder;
    out.under = g_lockUnder;
    out.bounded = g_lockBounded;
    out.breach = g_lockBreach;
}

bool LockLastGet(LockLast &out)
{
    for (int tries = 0; tries < 4; tries++)
    {
        const LONG before = g_lockLastSeq;
        if (!before) return false;
        if (before & 1) continue;
        out = g_lockLast;
        if (g_lockLastSeq != before) continue;
        out.seq = (unsigned long)before / 2;
        return true;
    }
    return false;
}

void DiffCountsGet(DiffCounts &out)
{
    out.steps = g_evenSteps;
    out.writes = g_evenWrites;
    out.faults = g_evenFaults;
    out.wheels = (int)g_evenN;
    out.j = BitsFloat(g_evenJBits);
    out.force = (int)g_diffForceNow;
    out.axleVehicle = (uint64_t)g_axleVehicle;
    out.axleGen = g_axleGen;
    out.axles = (int)g_axleAxles;
    out.alone = (int)g_axleAlone;
    out.axleWheels = (int)g_axleWheels;
    out.axleFaults = g_axleFaults;
    out.sayN = (int)g_axleSayN;
    if (out.sayN > kDiffAxleSay) out.sayN = kDiffAxleSay;
    for (int a = 0; a < kDiffAxleSay; a++)
        out.sayMean[a] = BitsFloat(g_axleSayMean[a]), out.sayK[a] = BitsFloat(g_axleSayK[a]), out.sayGot[a] = BitsFloat(g_axleSayGot[a]), out.sayWheels[a] = (unsigned)g_axleSayWheels[a];
}

void PushCountsGet(PushCounts &out)
{
    out.steps = g_pushSteps;
    out.backs = g_pushBacks;
    out.kicks = g_pushKicks;
    out.faults = g_pushFaults;
    out.skipped = g_pushSkipped;
    out.given = BitsFloat(g_pushGivenBits);
    out.back = BitsFloat(g_pushBackBits);
    out.k = BitsFloat(g_pushK);
    out.fx = BitsFloat(g_pushFx);
    out.f = BitsFloat(g_pushF);
    out.b4 = BitsFloat(g_pushB4);
    out.speed = BitsFloat(g_pushSpeed);
}

void DriveStepSet(DriveStepFn step) { g_driveStep = step; }

bool CreepHook(uintptr_t at, char *why, size_t size)
{
    why[0] = 0;
    if (g_creepHooked) return true;
    g_creepWay = (void *)at;
    void **targets[1] = { &g_creepWay };
    void *const hooks[1] = { (void *)&CreepDrive };
    const long error = GearboxAttach(targets, hooks, 1);
    if (error != NO_ERROR)
    {
        snprintf(why, size, "Detours could not attach the wheels' drive's hook (error %ld)", error);
        g_creepWay = nullptr;
        return false;
    }
    g_creepHooked = true;
    return true;
}

bool CreepHooked() { return g_creepHooked; }

void CreepGive(uint64_t action, float share)
{
    if (!action || !(share > 0.0f))
    {
        InterlockedExchangePointer(&g_creepSlot, nullptr);
        return;
    }
    CreepSlot &slot = g_creepSlots[g_creepNext];
    g_creepNext ^= 1;
    slot.action = action;
    slot.share = share;
    InterlockedExchangePointer(&g_creepSlot, &slot);
}

void CreepCounts(long &hits, long &calls)
{
    hits = g_creepHits;
    calls = g_creepCalls;
}

// ---- the game's engine speed ----
// float(vehicle rcx, bool dl, bool r8b), no argument on the stack (its first bytes keep rbp in its own home slot and
// read none of the caller's), the answer in xmm0: a C++ function of the same three arguments forwards all it takes.
// Detours enters the hook by a jump, so _ReturnAddress is the game's caller's.
typedef float (*EngineSpeedFn)(void *vehicle, bool withWheels, bool withMinThrottle);
static void *g_speedWay = nullptr;
static bool g_speedHooked = false;
static uintptr_t g_fuelBack = 0, g_stallBack = 0;
static EngineSpeedSlot g_speedSlots[2];
static int g_speedNext = 0;
static void *volatile g_speedSlot = nullptr;
static volatile LONG g_speedProbe = 0, g_speedAnswered = 0, g_speedLast = 0, g_speedFull = 0;
static volatile LONG64 g_speedBack[kSpeedCallers] = {};
static volatile LONG g_speedCalls[kSpeedCallers] = {}, g_speedStock[kSpeedCallers] = {};

// vehicle + 0x768 bit 0, the engine on, as the game's own first bytes test it; false where the memory is gone.
static bool SpeedEngineOn(const void *vehicle)
{
    __try
    {
        return (*((const volatile uint8_t *)vehicle + 0x768) & 1) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static LONG FloatBits(float f)
{
    LONG bits;
    memcpy(&bits, &f, sizeof bits);
    return bits;
}

static float BitsFloat(LONG bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

// The probe's record of one call: its caller's slot taken with a compare and swap, as DriveThreadNote takes one.
static void SpeedNote(uintptr_t back, float stock)
{
    for (int i = 0; i < kSpeedCallers; i++)
    {
        LONG64 at = g_speedBack[i];
        if (!at) at = InterlockedCompareExchange64(&g_speedBack[i], (LONG64)back, 0);
        if (!at || at == (LONG64)back)
        {
            InterlockedIncrement(&g_speedCalls[i]);
            InterlockedExchange(&g_speedStock[i], FloatBits(stock));
            return;
        }
    }
    g_speedFull = 1;
}

// B4 at the vehicle's action ([vehicle + 0x68] + 0xB4); below 0 where the memory is gone.
static float SpeedPull(const void *vehicle)
{
    __try
    {
        const uint8_t *action = *(const uint8_t *const *)((const uint8_t *)vehicle + 0x68);
        if ((uintptr_t)action < 0x10000) return -1.0f;
        return *(const volatile float *)(action + 0xB4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1.0f;
    }
}

__declspec(noinline) static float HookEngineSpeed(void *vehicle, bool withWheels, bool withMinThrottle)
{
    const uintptr_t back = (uintptr_t)_ReturnAddress();
    const EngineSpeedFn own = (EngineSpeedFn)g_speedWay;
    const bool probe = g_speedProbe != 0;
    float stock = 0.0f;
    if (probe)
    {
        stock = own(vehicle, withWheels, withMinThrottle);
        SpeedNote(back, stock);
    }
    // the fuel's and the stall's calls under the torque cut: B4 is held at 1 there, so the stand-in (TorqueCutStandIn,
    // the cut's own pull) takes its place. The fuel's value is linear in B4 (the Reviewer's M3): stock x standIn / B4.
    // The stall's adds a wheels' term that is not (the second review's M1): the game's own formula with the stand-in,
    // its base from the game's own call without the wheels, w kept from the drive's step so this reads no wheels.
    TorqueCutPull pull = {};
    if ((back == g_fuelBack || back == g_stallBack) && TorqueCutPullGet((uint64_t)(uintptr_t)vehicle, pull))
    {
        const float b4 = SpeedPull(vehicle);
        if (back == g_stallBack)
        {
            const float noWheels = own(vehicle, false, false);
            if (b4 < 0.0f || !isfinite(b4)) return probe ? stock : own(vehicle, withWheels, withMinThrottle);
            return TorqueCutStallAnswer(pull.pedal, pull.w, pull.standIn, noWheels, b4);
        }
        if (!probe) stock = own(vehicle, withWheels, withMinThrottle);
        if (b4 < 0.0f || !isfinite(b4)) return stock;
        return b4 > 0.0f ? stock * pull.standIn / b4 : 0.0f;
    }
    const EngineSpeedSlot *slot = (const EngineSpeedSlot *)g_speedSlot;
    // the cheap tests first, so the call of another vehicle, of the fuel or of the stall reads nothing more
    if (slot && slot->vehicle == vehicle && back != g_fuelBack && back != g_stallBack &&
        EngineSpeedOurs(back, g_fuelBack, g_stallBack, slot, vehicle, SpeedEngineOn(vehicle), GetTickCount64()))
    {
        const float share = slot->share;
        InterlockedIncrement(&g_speedAnswered);
        InterlockedExchange(&g_speedLast, FloatBits(share));
        return share;
    }
    return probe ? stock : own(vehicle, withWheels, withMinThrottle);
}

bool EngineSpeedHook(uintptr_t at, uintptr_t fuelBack, uintptr_t stallBack, char *why, size_t size)
{
    why[0] = 0;
    if (g_speedHooked) return true;
    g_fuelBack = fuelBack;
    g_stallBack = stallBack;
    g_speedWay = (void *)at;
    void **targets[1] = { &g_speedWay };
    void *const hooks[1] = { (void *)&HookEngineSpeed };
    const long error = GearboxAttach(targets, hooks, 1);
    if (error != NO_ERROR)
    {
        snprintf(why, size, "Detours could not attach the engine speed's hook (error %ld)", error);
        g_speedWay = nullptr;
        return false;
    }
    g_speedHooked = true;
    return true;
}

bool EngineSpeedHooked() { return g_speedHooked; }

void EngineSpeedPublish(const void *vehicle, float share, uint64_t at)
{
    if (!vehicle || !(share >= 0.0f))
    {
        InterlockedExchangePointer(&g_speedSlot, nullptr);
        return;
    }
    EngineSpeedSlot &slot = g_speedSlots[g_speedNext];
    g_speedNext ^= 1;
    slot.vehicle = vehicle;
    slot.share = share > 1.0f ? 1.0f : share;
    slot.at = at ? at : GetTickCount64();
    InterlockedExchangePointer(&g_speedSlot, &slot);
}

void EngineSpeedProbe(bool on) { InterlockedExchange(&g_speedProbe, on ? 1 : 0); }

int EngineSpeedCallersGet(EngineSpeedCaller *out, int most, bool &full)
{
    int n = 0;
    for (int i = 0; i < kSpeedCallers && n < most; i++)
        if (const LONG64 back = g_speedBack[i]) out[n++] = EngineSpeedCaller{ (uintptr_t)back, g_speedCalls[i], BitsFloat(g_speedStock[i]) };
    full = g_speedFull != 0;
    return n;
}

void EngineSpeedAnswers(long &answered, float &last)
{
    answered = g_speedAnswered;
    last = BitsFloat(g_speedLast);
}

// ---- the engine sound: Truck.GetTruckInput (BRIEF_drivetrain_1b_sound.md) ----
// Handle *(vehicle rcx, Handle *out rdx); its one caller, the script binding, keeps no scratch register across the call
// (the Reviewer's read), so a C++ function of the same two arguments forwards all it takes. The helper takes the
// handle's address and gives the data block or null, as the game's own calls of it do right before the function returns.
typedef void **(*TruckInputFn)(void *vehicle, void **out);
typedef uint8_t *(*HandleDataFn)(void **handle);
static void *g_inputWay = nullptr;
static HandleDataFn g_inputHelper = nullptr;
static bool g_inputHooked = false;
static volatile LONG g_inputCalls = 0, g_inputWritten = 0, g_inputNotOurs = 0, g_inputFailed = 0, g_inputStock = 0, g_inputOurs = 0, g_inputGear = 0;

// The write itself, under __try: the data block by the game's helper, its gear, the stall seconds, then velocityFactor.
static bool InputWrite(void *vehicle, void **out, float share)
{
    __try
    {
        uint8_t *data = g_inputHelper(out);
        if (!data) return false;
        const int gear = *(const volatile int *)data;
        const float stall = *(const volatile float *)((const uint8_t *)vehicle + 0x11C);
        volatile float *vf = (volatile float *)(data + 0x48);
        const float stock = *vf;
        const float ours = SoundVelocityFactor(share, gear, stall);
        *vf = ours;
        InterlockedExchange(&g_inputStock, FloatBits(stock));
        InterlockedExchange(&g_inputOurs, FloatBits(ours));
        InterlockedExchange(&g_inputGear, gear);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

__declspec(noinline) static void **HookTruckInput(void *vehicle, void **out)
{
    void **const answer = ((TruckInputFn)g_inputWay)(vehicle, out);
    InterlockedIncrement(&g_inputCalls);
    const EngineSpeedSlot *slot = (const EngineSpeedSlot *)g_speedSlot;
    // the cheap tests first, so another vehicle's call reads nothing more; the places 0, 1 and 2 only make the rule's
    // fuel and stall tests pass (this call is neither)
    if (!(out && slot && slot->vehicle == vehicle && EngineSpeedOurs(0, 1, 2, slot, vehicle, SpeedEngineOn(vehicle), GetTickCount64())))
    {
        InterlockedIncrement(&g_inputNotOurs);
        return answer;
    }
    if (InputWrite(vehicle, out, slot->share)) InterlockedIncrement(&g_inputWritten);
    else InterlockedIncrement(&g_inputFailed);
    return answer;
}

bool TruckInputHook(uintptr_t at, uintptr_t helper, char *why, size_t size)
{
    why[0] = 0;
    if (g_inputHooked) return true;
    g_inputHelper = (HandleDataFn)helper;
    g_inputWay = (void *)at;
    void **targets[1] = { &g_inputWay };
    void *const hooks[1] = { (void *)&HookTruckInput };
    const long error = GearboxAttach(targets, hooks, 1);
    if (error != NO_ERROR)
    {
        snprintf(why, size, "Detours could not attach the truck input's hook (error %ld)", error);
        g_inputWay = nullptr;
        return false;
    }
    g_inputHooked = true;
    return true;
}

bool TruckInputHooked() { return g_inputHooked; }

void TruckInputCountsGet(TruckInputCounts &out)
{
    out.calls = g_inputCalls;
    out.written = g_inputWritten;
    out.notOurs = g_inputNotOurs;
    out.failed = g_inputFailed;
    out.stock = BitsFloat(g_inputStock);
    out.ours = BitsFloat(g_inputOurs);
    out.gear = g_inputGear;
}

// ---- the torque cut: the game's gear torque ----
// bool(vehicle rcx, int gear edx, float *torque r8, float *start r9, float *a, float *top, float *gearSpeed on the
// stack), every output zeroed first and any pointer may be null (review\own_drivetrain_research.md A). None of its seven
// calls reads a scratch register after it before writing it (the Reviewer's N2), so a C++ function of the same seven
// arguments stands in. The game's own is called with the detour's own locals for every output (the drive passes no
// gearSpeed, the stall only the start), then the caller's pointers get the answer.
typedef bool (*GearTorqueFn)(void *vehicle, int gear, float *torque, float *start, float *a, float *top, float *gearSpeed);
static void *g_torqueWay = nullptr;
static bool g_torqueHooked = false;
static uintptr_t g_driveBack = 0;
static volatile LONG g_cutAnswered = 0, g_cutPassed = 0, g_cutOthers = 0, g_cutFaults = 0, g_cutFalls = 0;
static volatile LONG g_cutS = 0, g_cutF = 0, g_cutBracket = 0, g_cutStock = 0, g_cutTorque = 0, g_cutD4 = 0, g_cutB4sLast = 0, g_cutCurve = 0, g_cutLastBracket = 0;
// the stand-in for B4, the stall's wheels' term and the pedal of the last answer, for the engine speed's calls and the
// mod's thread
static volatile LONG64 g_b4sVehicle = 0, g_b4sAt = 0;
static volatile LONG g_b4sBits = 0, g_cutWBits = 0, g_cutPedalBits = 0;
// the bracket the drive got last step, per vehicle (F1b: CutRise paces it); none for a new vehicle or after a clear, where
// the pace starts from the slot's gamePull
static volatile LONG64 g_riseVehicle = 0;
static volatile LONG g_riseBits = 0, g_cutUsed = 0, g_cutRate = 0, g_cutTruck = 0x7FC00000; // a quiet NaN: not read
// the last answer's rolling radius (BRIEF_cut_radius_cylinder.md, M2: the hook does not log; the mod's thread does): the
// radius used and the driven wheels' + 0x94 by the same mean, and the driven wheels by their kind (WheelRollRadius)
static volatile LONG g_cutRadius = 0x7FC00000, g_cutRadius94 = 0x7FC00000, g_cutRollCylinder = 0, g_cutRollNone = 0, g_cutRollOut = 0;

// One wheel's collision cylinder's radius (shape + 0x28, the shape at [wheel + 0x18] + 0x20 by its vtable), under its own
// guard so a fault here leaves the rest of the read (the Reviewer's S1); false with no vtable, another one, a pointer
// under 0x10000 or a fault.
__declspec(noinline) static bool CutCylinderRead(const uint8_t *wheel, uint64_t vtable, float &cylinder)
{
    cylinder = NAN;
    if (!vtable) return false;
    __try
    {
        const uint8_t *body = *(const uint8_t *const *)(wheel + 0x18);
        if ((uintptr_t)body < 0x10000) return false;
        const uint8_t *shape = *(const uint8_t *const *)(body + 0x20);
        if ((uintptr_t)shape < 0x10000 || *(const uint64_t *)shape != vtable) return false;
        cylinder = *(const volatile float *)(shape + 0x28);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        cylinder = NAN;
        return false;
    }
}

// The driven wheels' rolling radius as one read found it: the radius used (RollMeanOf), + 0x94 by the same mean, and
// the driven wheels by their kind.
struct CutRadii
{
    float radius, radius94;
    int cylinder, none, out;
};
// The vehicle's driven wheels' speeds, its power factor (action + 0x38), its game clutch (action + 0xD4) and its
// EngineResponsiveness (action + 0xC4, B4's rate a step over the power factor), read as the drive reads them; false where
// a read faults or the list is out of bounds. cylinderVtable: the slot's (M1), for each driven wheel's rolling radius.
static bool CutWheelsRead(const void *vehicle, uint64_t cylinderVtable, float *omega, bool *driven, int &n, float &pf, float &d4, float &response, float &forward,
                          CutRadii &radii)
{
    n = 0;
    forward = NAN;
    radii = CutRadii{ NAN, NAN, 0, 0, 0 };
    __try
    {
        const uint8_t *v = (const uint8_t *)vehicle;
        const uint8_t *action = *(const uint8_t *const *)(v + 0x68);
        if ((uintptr_t)action < 0x10000) return false;
        RollMean used = {}, plain = {};
        pf = *(const volatile float *)(action + 0x38);
        d4 = *(const volatile float *)(action + 0xD4);
        response = *(const volatile float *)(action + 0xC4);
        const bool awd = *(const volatile uint8_t *)(action + 0x49) != 0;
        const uintptr_t begin = *(const uintptr_t *)(v + 0x200), end = *(const uintptr_t *)(v + 0x208);
        if (end < begin || (end - begin) / 8 > (uintptr_t)kCapWheels) return false;
        for (uintptr_t p = begin; p < end; p += 8)
        {
            const uint8_t *model = *(const uint8_t *const *)p;
            if ((uintptr_t)model < 0x10000) continue;
            const uint8_t *wheel = *(const uint8_t *const *)(model + 0x2C8);
            if ((uintptr_t)wheel < 0x10000) continue;
            const int type = *(const volatile int *)(wheel + 0xE8);
            omega[n] = *(const volatile float *)(wheel + 0x16C);
            driven[n] = type == 1 || (type == 2 && awd);
            if (driven[n])
            {
                // M5: the driven wheels' rolling radius, the cylinder where it is good, else + 0x94 (BRIEF_cut_radius_cylinder.md)
                const float r94 = *(const volatile float *)(wheel + 0x94);
                float cylinder = NAN;
                const bool read = CutCylinderRead(wheel, cylinderVtable, cylinder);
                const RollRadius rr = WheelRollRadius(r94, cylinder, read);
                RollMeanAdd(used, rr.r);
                RollMeanAdd(plain, r94);
                if (rr.kind == kRollCylinder) radii.cylinder++;
                else if (rr.kind == kRollNoCylinder) radii.none++;
                else radii.out++;
            }
            n++;
        }
        radii.radius = RollMeanOf(used);
        radii.radius94 = RollMeanOf(plain);
        // the truck's forward speed from its main body, as the drive's d73120: [[vehicle + 0x60] + 0x230], its linear
        // velocity (+ 0x230) along its rotation's first column (+ 0x170); a wheel's parent can be a rocker or a frame bone
        // with a swing of its own (the review of bc224bf, S1)
        const uint8_t *model = *(const uint8_t *const *)(v + 0x60);
        const uint8_t *body = (uintptr_t)model >= 0x10000 ? *(const uint8_t *const *)(model + 0x230) : nullptr;
        if ((uintptr_t)body >= 0x10000)
        {
            const volatile float *vel = (const volatile float *)(body + 0x230), *f = (const volatile float *)(body + 0x170);
            forward = vel[0] * f[0] + vel[1] * f[1] + vel[2] * f[2];
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        n = 0;
        return false;
    }
}

__declspec(noinline) static bool HookGearTorque(void *vehicle, int gear, float *torque, float *start, float *a, float *top, float *gearSpeed)
{
    const uintptr_t back = (uintptr_t)_ReturnAddress();
    float t = 0.0f, st = 0.0f, aa = 0.0f, tp = 0.0f, gs = 0.0f;
    const bool took = ((GearTorqueFn)g_torqueWay)(vehicle, gear, &t, &st, &aa, &tp, &gs);
    const TorqueCutSlot *published = back == g_driveBack ? (const TorqueCutSlot *)g_cutSlot : nullptr;
    if (published && TorqueCutOurs(back, g_driveBack, published, (uint64_t)(uintptr_t)vehicle, took, GetTickCount64()))
    {
        const TorqueCutSlot slot = *published; // read whole: the one it points to is written again two passes later
        float omega[kCapWheels];
        bool driven[kCapWheels];
        int n = 0;
        float pf = 1.0f, d4 = 1.0f, response = 1.0f, forward = NAN;
        CutSpeeds sp = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false };
        CutRadii radii = { NAN, NAN, 0, 0, 0 };
        if (CutWheelsRead(vehicle, slot.cylinderVtable, omega, driven, n, pf, d4, response, forward, radii))
        {
            sp = TorqueCutSpeeds(omega, driven, n, gear < 0 ? -1 : 1);
            CutSpeedsTruck(sp, forward, radii.radius, gear < 0 ? -1 : 1);
        }
        if (!sp.ok) InterlockedIncrement(&g_cutFaults);
        TorqueCutAnswer r = TorqueCutAnswerOf(slot, t, st, aa, tp, pf, d4, sp);
        const float kept = (uint64_t)g_capKeptVehicle == (uint64_t)(uintptr_t)vehicle ? BitsFloat(g_capKeptBits) : 1.0f;
        // F1b: the bracket paced at the game's own B4 rate, from the last step's for this vehicle; at a take-over from
        // the game's own pull at that step (S1)
        const float rate = response * pf;
        const float usedBefore = (uint64_t)g_riseVehicle == (uint64_t)(uintptr_t)vehicle ? BitsFloat(g_riseBits) : CutClamp01(slot.gamePull);
        TorqueCutAnswerPaced(r, t, usedBefore, rate, kept);
        InterlockedExchange(&g_riseBits, FloatBits(r.used));
        InterlockedExchange64(&g_riseVehicle, (LONG64)(uintptr_t)vehicle);
        // F1: the curve flat under a finite top at the governor's end, so the game's own overspeed brake acts above it
        const CutCurveOut curve = TorqueCutCurveOut(slot.map, pf);
        if (torque) *torque = r.torque;
        if (start) *start = curve.start;
        if (a) *a = curve.a;
        if (top) *top = curve.top;
        if (gearSpeed) *gearSpeed = gs;
        InterlockedIncrement(&g_cutAnswered);
        if (BitsFloat(g_cutLastBracket) - r.used >= 0.3f) InterlockedIncrement(&g_cutFalls);
        InterlockedExchange(&g_cutLastBracket, FloatBits(r.used));
        InterlockedExchange(&g_cutUsed, FloatBits(r.used));
        InterlockedExchange(&g_cutRate, FloatBits(rate));
        InterlockedExchange(&g_cutS, FloatBits(r.s));
        InterlockedExchange(&g_cutTruck, FloatBits(sp.truckOk ? sp.truck : NAN));
        InterlockedExchange(&g_capTruck, FloatBits(sp.truckOk ? sp.truck : NAN));
        InterlockedExchange(&g_cutRadius, FloatBits(radii.radius));
        InterlockedExchange(&g_cutRadius94, FloatBits(radii.radius94));
        InterlockedExchange(&g_cutRollCylinder, radii.cylinder);
        InterlockedExchange(&g_cutRollNone, radii.none);
        InterlockedExchange(&g_cutRollOut, radii.out);
        InterlockedExchange(&g_cutF, FloatBits(r.f));
        InterlockedExchange(&g_cutBracket, FloatBits(r.bracket));
        InterlockedExchange(&g_cutStock, FloatBits(t));
        InterlockedExchange(&g_cutTorque, FloatBits(r.torque));
        InterlockedExchange(&g_cutD4, FloatBits(d4));
        InterlockedExchange(&g_cutCurve, FloatBits(r.stockCurve));
        InterlockedExchange(&g_cutB4sLast, FloatBits(r.b4s));
        InterlockedExchange(&g_b4sBits, FloatBits(r.standIn));
        InterlockedExchange(&g_cutWBits, FloatBits(r.w));
        InterlockedExchange(&g_cutPedalBits, FloatBits(slot.pedal));
        InterlockedExchange64(&g_b4sAt, (LONG64)GetTickCount64());
        InterlockedExchange64(&g_b4sVehicle, (LONG64)(uintptr_t)vehicle);
        return true;
    }
    InterlockedIncrement(back == g_driveBack ? &g_cutPassed : &g_cutOthers);
    if (torque) *torque = t;
    if (start) *start = st;
    if (a) *a = aa;
    if (top) *top = tp;
    if (gearSpeed) *gearSpeed = gs;
    return took;
}

bool GearTorqueHook(uintptr_t at, uintptr_t driveBack, char *why, size_t size)
{
    why[0] = 0;
    if (g_torqueHooked) return true;
    g_driveBack = driveBack;
    g_torqueWay = (void *)at;
    void **targets[1] = { &g_torqueWay };
    void *const hooks[1] = { (void *)&HookGearTorque };
    const long error = GearboxAttach(targets, hooks, 1);
    if (error != NO_ERROR)
    {
        snprintf(why, size, "Detours could not attach the gear torque's hook (error %ld)", error);
        g_torqueWay = nullptr;
        return false;
    }
    g_torqueHooked = true;
    return true;
}

bool GearTorqueHooked() { return g_torqueHooked; }

void TorqueCutPublish(const TorqueCutSlot *slot)
{
    if (!slot || !slot->vehicle || !slot->action)
    {
        InterlockedExchangePointer(&g_cutSlot, nullptr);
        InterlockedExchange64(&g_riseVehicle, 0); // the next take-over paces from the game's own pull at that step (gamePull)
        InterlockedExchange(&g_riseBits, 0);
        InterlockedExchange(&g_capReset, 1); // the cap waits again (M3) and its kept share starts at 1
        InterlockedExchange64(&g_capKeptVehicle, 0);
        return;
    }
    TorqueCutSlot &next = g_cutSlots[g_cutNext];
    g_cutNext ^= 1;
    next = *slot;
    if (!next.at) next.at = GetTickCount64();
    InterlockedExchangePointer(&g_cutSlot, &next);
}

bool TorqueCutPullGet(uint64_t vehicle, TorqueCutPull &out)
{
    if (!vehicle || (uint64_t)g_b4sVehicle != vehicle) return false;
    const uint64_t at = (uint64_t)g_b4sAt, now = GetTickCount64();
    if (now < at || now - at > kTorqueFresh) return false;
    out.standIn = BitsFloat(g_b4sBits);
    out.w = BitsFloat(g_cutWBits);
    out.pedal = BitsFloat(g_cutPedalBits);
    return isfinite(out.standIn) && isfinite(out.w) && isfinite(out.pedal);
}

void TorqueCutCountsGet(TorqueCutCounts &out)
{
    out.answered = g_cutAnswered;
    out.passed = g_cutPassed;
    out.others = g_cutOthers;
    out.faults = g_cutFaults;
    out.falls = g_cutFalls;
    out.s = BitsFloat(g_cutS);
    out.f = BitsFloat(g_cutF);
    out.bracket = BitsFloat(g_cutBracket);
    out.stock = BitsFloat(g_cutStock);
    out.torque = BitsFloat(g_cutTorque);
    out.d4 = BitsFloat(g_cutD4);
    out.b4s = BitsFloat(g_cutB4sLast);
    out.stockCurve = BitsFloat(g_cutCurve);
    out.standIn = BitsFloat(g_b4sBits);
    out.w = BitsFloat(g_cutWBits);
    out.used = BitsFloat(g_cutUsed);
    out.rate = BitsFloat(g_cutRate);
    out.truck = BitsFloat(g_cutTruck);
    out.radius = BitsFloat(g_cutRadius);
    out.radius94 = BitsFloat(g_cutRadius94);
    out.rollCylinder = g_cutRollCylinder;
    out.rollNone = g_cutRollNone;
    out.rollOut = g_cutRollOut;
}

void WheelCapCountsGet(WheelCapCounts &out)
{
    out.clamps = g_capClamps;
    out.steps = g_capSteps;
    out.refused = g_capRefused;
    out.armedSteps = g_capArmedSteps;
    out.waitSteps = g_capWaitSteps;
    out.faults = g_capFaults;
    out.clampedNow = g_capClampedNow;
    out.armed = g_capArmedNow != 0;
    out.kept = BitsFloat(g_capKeptBits);
    out.spread = BitsFloat(g_capSpreadBits);
    out.excessMost = BitsFloat(InterlockedExchange(&g_capExcessMost, 0)); // the largest since the last read
    out.preMost = BitsFloat(InterlockedExchange(&g_capPreMost, 0));
    out.postSum = BitsFloat(g_capPostSum);
    out.postMost = BitsFloat(InterlockedExchange(&g_capPostMost, 0));
    const float least = BitsFloat(InterlockedExchange(&g_capPreLeast, FloatBits(kCapNone)));
    out.preLeast = least < kCapNone ? least : 0.0f;
    out.ceiling = BitsFloat(g_capCeiling);
}

long GearboxKicks() { return g_kicks; }
