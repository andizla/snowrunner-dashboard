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
    bool ok;
};
struct CapTruckRead
{
    CapWheelRead w[kCapWheels];
    int n, refused;
    int gear;
    float sign;
};
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
static bool CapReadBefore(const void *vehicle, const void *action, CapTruckRead &t)
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
static bool CapWrite(const CapTruckRead &t, const float *post, const CapStepOut &o)
{
    __try
    {
        for (int i = 0; i < t.n; i++)
        {
            if (!t.w[i].ok || o.final[i] == post[i]) continue;
            CapVec omega;
            CapGet3(t.w[i].body + 0x240, omega);
            if (!CapMove(omega, t.w[i].axle, t.sign, o.final[i] - post[i])) continue;
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
static void CapMost(volatile LONG &bits, float x)
{
    if (isfinite(x) && x > BitsFloat(bits)) InterlockedExchange(&bits, FloatBits(x));
}
static void CapAdd(volatile LONG &bits, float x)
{
    if (isfinite(x) && x > 0.0f) InterlockedExchange(&bits, FloatBits(BitsFloat(bits) + x));
}

// The drive with the cap around it: the wheels read before, the arming (M3), the original drive, the wheels after, the
// cap with the spread (CapStep), the writes and the kept share for the stand-in (M1). A read that faults leaves the
// drive's own result.
struct CapAround
{
    int clamps;
    float kept;
    bool clamped[kCapWheels];
    uint8_t *wheel[kCapWheels];
    int n;
};
static CapAround CapStepAround(const TorqueCutSlot *cut, void *action, float throttle, float dt)
{
    CapAround out = {};
    out.kept = 1.0f;
    const uint64_t vehicle = cut->vehicle;
    const float cap = cut->cap;
    if (g_capVehicle != vehicle)
    {
        g_capVehicle = vehicle;
        g_capArm = CapArm{ 0x7FFFFFFF, false };
        g_capKept = 1.0f;
    }
    static CapTruckRead t; // the physics thread's own: one vehicle under the cut, one call at a time
    const bool readOk = CapReadBefore((const void *)(uintptr_t)vehicle, action, t);
    if (!readOk) InterlockedIncrement(&g_capFaults);
    if (t.refused) InterlockedExchangeAdd(&g_capRefused, t.refused);
    float mean = 0.0f, preMost = 0.0f;
    int m = 0;
    for (int i = 0; i < t.n; i++)
        if (t.w[i].ok) mean += t.w[i].pre, m++, preMost = fmaxf(preMost, t.w[i].pre - cap);
    if (m) mean /= (float)m;
    const bool armed = readOk && CapArmStep(g_capArm, t.gear, mean, m > 0, cap);
    InterlockedExchange64(&g_capKeptVehicle, (LONG64)vehicle);
    InterlockedExchange(&g_capArmedNow, armed ? 1 : 0);
    ((DriveFn)g_creepWay)(action, throttle, dt);
    if (!armed)
    {
        InterlockedIncrement(&g_capWaitSteps);
        InterlockedExchange(&g_capClampedNow, 0);
        g_capKept = CapKeptSmooth(g_capKept, 1.0f);
        InterlockedExchange(&g_capKeptBits, FloatBits(g_capKept));
        return out;
    }
    InterlockedIncrement(&g_capArmedSteps);
    CapMost(g_capPreMost, preMost);
    float pre[kCapWheels], post[kCapWheels], gain[kCapWheels];
    bool ok[kCapWheels];
    for (int i = 0; i < t.n; i++) pre[i] = t.w[i].pre, post[i] = t.w[i].pre, gain[i] = t.w[i].gain, ok[i] = t.w[i].ok;
    if (!CapReadAfter(t, post))
    {
        InterlockedIncrement(&g_capFaults);
        return out;
    }
    const CapStepOut o = CapStep(pre, post, gain, ok, t.n, cap, true);
    if (!CapWrite(t, post, o)) InterlockedIncrement(&g_capFaults);
    InterlockedIncrement(&g_capSteps);
    if (o.clamps) InterlockedExchangeAdd(&g_capClamps, o.clamps);
    CapMost(g_capExcessMost, o.excess);
    CapAdd(g_capSpreadBits, o.spread);
    InterlockedExchange(&g_capClampedNow, o.clamps);
    g_capKept = CapKeptSmooth(g_capKept, o.kept);
    InterlockedExchange(&g_capKeptBits, FloatBits(g_capKept));
    out.clamps = o.clamps;
    out.kept = o.kept;
    out.n = t.n;
    for (int i = 0; i < t.n; i++) out.clamped[i] = o.clamped[i], out.wheel[i] = t.w[i].wheel;
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
static void CreepDrive(void *action, float throttle, float dt)
{
    InterlockedIncrement(&g_creepCalls);
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
    uint64_t at = 0;
    const bool pushOn = !cutHere && push && (uint64_t)(uintptr_t)action == push->action && push->back > 0.0f && (at = GetTickCount64()) >= push->at && at - push->at <= kTorqueFresh;
    const bool capPush = capOn && cut->pushBack > 0.0f;
    static PushRead pr; // the physics thread's own: one driven truck, one call at a time
    const uint64_t pushVehicle = pushOn ? push->vehicle : capPush ? cut->vehicle : 0;
    const bool pushRead = pushVehicle && PushReadBefore((const void *)(uintptr_t)pushVehicle, action, pr);
    if (pushVehicle && !pushRead) InterlockedIncrement(&g_pushFaults);
    float b4 = NAN;
    if (pushOn && pushRead) b4 = CutB4Read(action);
    if (!capOn)
    {
        ((DriveFn)g_creepWay)(action, throttle, dt);
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
    const CapAround around = CapStepAround(cut, action, throttle, dt);
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

static const int kCutWheels = 16;
// The vehicle's driven wheels' speeds, its power factor (action + 0x38), its game clutch (action + 0xD4) and its
// EngineResponsiveness (action + 0xC4, B4's rate a step over the power factor), read as the drive reads them; false where
// a read faults or the list is out of bounds.
static bool CutWheelsRead(const void *vehicle, float *omega, bool *driven, int &n, float &pf, float &d4, float &response, float &forward, float &radius)
{
    n = 0;
    forward = radius = NAN;
    __try
    {
        const uint8_t *v = (const uint8_t *)vehicle;
        const uint8_t *action = *(const uint8_t *const *)(v + 0x68);
        if ((uintptr_t)action < 0x10000) return false;
        float radiusSum = 0.0f;
        int radiusN = 0;
        pf = *(const volatile float *)(action + 0x38);
        d4 = *(const volatile float *)(action + 0xD4);
        response = *(const volatile float *)(action + 0xC4);
        const bool awd = *(const volatile uint8_t *)(action + 0x49) != 0;
        const uintptr_t begin = *(const uintptr_t *)(v + 0x200), end = *(const uintptr_t *)(v + 0x208);
        if (end < begin || (end - begin) / 8 > (uintptr_t)kCutWheels) return false;
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
                // M5: the driven wheels' radius (+ 0x94)
                const float r = *(const volatile float *)(wheel + 0x94);
                if (isfinite(r) && r > 0.0f) radiusSum += r, radiusN++;
            }
            n++;
        }
        if (radiusN) radius = radiusSum / (float)radiusN;
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
        float omega[kCutWheels];
        bool driven[kCutWheels];
        int n = 0;
        float pf = 1.0f, d4 = 1.0f, response = 1.0f, forward = NAN, radius = NAN;
        CutSpeeds sp = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false };
        if (CutWheelsRead(vehicle, omega, driven, n, pf, d4, response, forward, radius))
        {
            sp = TorqueCutSpeeds(omega, driven, n, gear < 0 ? -1 : 1);
            CutSpeedsTruck(sp, forward, radius, gear < 0 ? -1 : 1);
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
}

long GearboxKicks() { return g_kicks; }
