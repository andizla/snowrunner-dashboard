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
    if (cut && (uint64_t)(uintptr_t)action == cut->action && (now = GetTickCount64()) >= cut->at && now - cut->at <= kTorqueFresh)
    {
        InterlockedIncrement(&g_cutHolds);
        throttle = 1.0f;
        CutPullOne(action);
    }
    else if (slot && (uint64_t)(uintptr_t)action == slot->action)
    {
        InterlockedIncrement(&g_creepHits);
        throttle = _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(throttle), _mm_set_ss(slot->share))); // maxss itself: a ?: may be built with its operands swapped
    }
    if (const DriveStepFn step = g_driveStep) step((uint64_t)(uintptr_t)action, dt);
    ((DriveFn)g_creepWay)(action, throttle, dt);
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
    // the fuel's call under the torque cut (the Reviewer's M3): the game's own value is linear in B4 there, so it is
    // scaled to the pull the game would have without the throttle held at 1
    float b4s = 0.0f;
    if (back == g_fuelBack && TorqueCutB4sGet((uint64_t)(uintptr_t)vehicle, b4s))
    {
        if (!probe) stock = own(vehicle, withWheels, withMinThrottle);
        const float b4 = SpeedPull(vehicle);
        if (b4 < 0.0f || !isfinite(b4)) return stock;
        return b4 > 0.0f ? stock * b4s / b4 : 0.0f;
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
// the B4s of the last answer, for the fuel's call and the mod's thread
static volatile LONG64 g_b4sVehicle = 0, g_b4sAt = 0;
static volatile LONG g_b4sBits = 0;

static const int kCutWheels = 16;
// The vehicle's driven wheels' speeds, its power factor (action + 0x38) and its game clutch (action + 0xD4), read as the
// drive reads them; false where a read faults or the list is out of bounds.
static bool CutWheelsRead(const void *vehicle, float *omega, bool *driven, int &n, float &pf, float &d4)
{
    n = 0;
    __try
    {
        const uint8_t *v = (const uint8_t *)vehicle;
        const uint8_t *action = *(const uint8_t *const *)(v + 0x68);
        if ((uintptr_t)action < 0x10000) return false;
        pf = *(const volatile float *)(action + 0x38);
        d4 = *(const volatile float *)(action + 0xD4);
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
            n++;
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
        float pf = 1.0f, d4 = 1.0f;
        CutSpeeds sp = { 0.0f, 0.0f, false };
        if (CutWheelsRead(vehicle, omega, driven, n, pf, d4)) sp = TorqueCutSpeeds(omega, driven, n, gear < 0 ? -1 : 1);
        if (!sp.ok) InterlockedIncrement(&g_cutFaults);
        const TorqueCutAnswer r = TorqueCutAnswerOf(slot, t, st, aa, tp, pf, d4, sp);
        if (torque) *torque = r.torque;
        if (start) *start = -1.0f;
        if (a) *a = 0.0f;
        if (top) *top = 1e9f;
        if (gearSpeed) *gearSpeed = gs;
        InterlockedIncrement(&g_cutAnswered);
        if (BitsFloat(g_cutLastBracket) - r.bracket >= 0.3f) InterlockedIncrement(&g_cutFalls);
        InterlockedExchange(&g_cutLastBracket, FloatBits(r.bracket));
        InterlockedExchange(&g_cutS, FloatBits(r.s));
        InterlockedExchange(&g_cutF, FloatBits(r.f));
        InterlockedExchange(&g_cutBracket, FloatBits(r.bracket));
        InterlockedExchange(&g_cutStock, FloatBits(t));
        InterlockedExchange(&g_cutTorque, FloatBits(r.torque));
        InterlockedExchange(&g_cutD4, FloatBits(d4));
        InterlockedExchange(&g_cutCurve, FloatBits(r.stockCurve));
        InterlockedExchange(&g_cutB4sLast, FloatBits(r.b4s));
        InterlockedExchange(&g_b4sBits, FloatBits(r.b4s));
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
        return;
    }
    TorqueCutSlot &next = g_cutSlots[g_cutNext];
    g_cutNext ^= 1;
    next = *slot;
    if (!next.at) next.at = GetTickCount64();
    InterlockedExchangePointer(&g_cutSlot, &next);
}

bool TorqueCutB4sGet(uint64_t vehicle, float &b4s)
{
    if (!vehicle || (uint64_t)g_b4sVehicle != vehicle) return false;
    const uint64_t at = (uint64_t)g_b4sAt, now = GetTickCount64();
    if (now < at || now - at > kTorqueFresh) return false;
    b4s = BitsFloat(g_b4sBits);
    return isfinite(b4s);
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
}

long GearboxKicks() { return g_kicks; }
