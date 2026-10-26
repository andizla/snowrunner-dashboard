// gearbox_test.cpp: the mod's own gearbox (src\gearbox_model.h) on made up passes, and the hooks' policy while it has
// the truck (src\gearbox_hooks.h) on functions of the test's own. Offline; build.bat makes out\test\gearbox_test.exe
// and test.bat runs it.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gearbox_model.h"
#include "gearbox_hooks.h"

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

// A truck of six gears in the game's gear g, its power 1, the engine running, rolling.
static GearboxTruck Truck(int g)
{
    GearboxTruck t = {};
    t.gear = g;
    t.top = 6;
    t.speed = 5.0f;
    t.engineOn = true;
    t.power = 1.0f;
    return t;
}

// One pass with these actions; the game takes the gear set.
static GearboxPosOrders Pass(GearboxPosModel &m, const GearboxOptions &o, GearboxActions a, GearboxTruck &t, int autoGear = 1)
{
    const GearboxPosOrders r = GearboxPosStep(m, o, a, t, autoGear);
    if (r.setGear) t.gear = r.gear;
    if (r.setPower) t.power = r.power;
    return r;
}

static GearboxActions Up() { GearboxActions a = {}; a.up = true; return a; }
static GearboxActions Down() { GearboxActions a = {}; a.down = true; return a; }
static GearboxActions None() { return GearboxActions{}; }

// ---- the hooks' policy, on functions with the game's signatures ----
static volatile int g_ran[kFnCount] = {};
__declspec(noinline) static bool FakeShift(void *, int32_t gear) { g_ran[kFnShiftGear] = g_ran[kFnShiftGear] + 1; return gear != 99; }
__declspec(noinline) static void FakeAuto(void *) { g_ran[kFnShiftToAuto] = g_ran[kFnShiftToAuto] + 1; }
__declspec(noinline) static bool FakeHigh(void *) { g_ran[kFnShiftToHigh] = g_ran[kFnShiftToHigh] + 1; return true; }
__declspec(noinline) static bool FakeReverse(void *) { g_ran[kFnShiftToReverse] = g_ran[kFnShiftToReverse] + 1; return true; }
__declspec(noinline) static bool FakeNeutral(void *) { g_ran[kFnShiftToNeutral] = g_ran[kFnShiftToNeutral] + 1; return true; }
__declspec(noinline) static bool FakeAutoOff(void *, int32_t gear) { g_ran[kFnAutoOffAndShift] = g_ran[kFnAutoOffAndShift] + 1; return gear != 99; }
__declspec(noinline) static void FakePower(void *, float) { g_ran[kFnSetPower] = g_ran[kFnSetPower] + 1; }
__declspec(noinline) static void FakeVehicle(void *, void *) { g_ran[kFnSetVehicle] = g_ran[kFnSetVehicle] + 1; }

int main()
{
    const GearboxOptions plain = {}, skip = { false, false, true }, held = { false, true, false }, clutch = { true, false, false };

    // the walk up and down: 1 to the top and no further, down through N to R
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        GearboxActions a = {};
        a.gearPressed = 1;
        Pass(m, plain, a, t);
        int tops = 0;
        for (int i = 0; i < 8; i++) Pass(m, plain, Up(), t), tops += t.gear == 6;
        const bool upTop = t.gear == 6 && m.pos == kPosGear && tops >= 3;
        for (int i = 0; i < 6; i++) Pass(m, plain, Down(), t);
        const bool atN = t.gear == 0 && m.pos == kPosN;
        Pass(m, plain, Down(), t);
        const bool atR = t.gear == -1 && m.pos == kPosR;
        Pass(m, plain, Down(), t);
        const bool staysR = t.gear == -1;
        Pass(m, plain, Up(), t);
        const bool backN = t.gear == 0;
        Check(upTop && atN && atR && staysR && backN, "gearbox: up walks 1 to the top and stops there, down walks to N and then R, and up from R to N");
    }
    // SkipNeutral: from 1 down to R and from R up to 1, past N
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        GearboxActions a = {};
        a.gearPressed = 1;
        Pass(m, skip, a, t);
        Pass(m, skip, Down(), t);
        const bool toR = t.gear == -1;
        Pass(m, skip, Up(), t);
        Check(toR && t.gear == 1, "gearbox: SkipNeutral steps from the first gear to reverse and back without stopping in neutral");
    }
    // RequireGearHeld: a gear key held keeps its gear, let go is neutral
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(0);
        GearboxActions a = {};
        a.gearPressed = 3;
        a.gearHeld = 3;
        Pass(m, held, a, t);
        const bool in3 = t.gear == 3;
        a.gearPressed = 0;
        Pass(m, held, a, t);
        const bool still3 = t.gear == 3;
        Pass(m, held, None(), t);
        Check(in3 && still3 && t.gear == 0 && m.pos == kPosN, "gearbox: RequireGearHeld keeps a gear while its key is held and goes to neutral when it is let go");
    }
    // RequireClutch: a shift without the clutch shifts and stalls once; into N no stall; with the clutch none; an engine
    // already dying none
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(2);
        GearboxPosStep(m, clutch, None(), t, 1);
        const GearboxPosOrders noClutch = Pass(m, clutch, Up(), t);
        const bool stalled = noClutch.stall && noClutch.setGear && t.gear == 3 && m.stalls == 1 && noClutch.why[0];
        const GearboxPosOrders again = Pass(m, clutch, None(), t);
        GearboxActions n = {};
        n.n = true;
        const GearboxPosOrders toN = Pass(m, clutch, n, t);
        GearboxActions withClutch = Up();
        withClutch.clutch = true;
        const GearboxPosOrders clean = Pass(m, clutch, withClutch, t);
        t.stall = 0.5f;
        const GearboxPosOrders dying = Pass(m, clutch, Up(), t);
        Check(stalled && !again.stall && !toN.stall && t.gear >= 1 && !clean.stall && !dying.stall && m.stalls == 1,
              "gearbox: RequireClutch stalls the engine once for a shift into a gear without the clutch, not into neutral, not with the clutch, not when it is already dying");
    }
    // A: the automatic's gear set through; a gear key ends A; A sets off from N only with the pedal down
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(2);
        t.automatic = true;
        const GearboxPosOrders took = Pass(m, plain, None(), t, 3);
        const bool inA = m.pos == kPosA && took.flagDown && t.gear == 3;
        t.automatic = false;
        Pass(m, plain, None(), t, 4);
        const bool follows = t.gear == 4;
        GearboxActions a = {};
        a.gearPressed = 2;
        Pass(m, plain, a, t, 5);
        const bool ended = m.pos == kPosGear && t.gear == 2;
        Pass(m, plain, None(), t, 5);
        const bool stays = t.gear == 2;
        GearboxActions backA = {};
        backA.a = true;
        t = Truck(0);
        t.speed = 0.0f;
        Pass(m, plain, backA, t, 1);
        const bool waits = t.gear == 0;
        t.pedal = 0.5f;
        Pass(m, plain, None(), t, 1);
        Check(inA && follows && ended && stays && waits && t.gear == 1,
              "gearbox: A sets the automatic's gear and puts the game's flag down; a gear key ends A; in A a truck standing in N sets off in the first gear once the pedal is down");
    }
    // the power factor per position: the truck's own in A, the gears and H, the low range's table in L-, L and L+
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(3);
        t.power = 1.05f;
        Pass(m, plain, None(), t);
        GearboxActions lm = {}, l = {}, lp = {}, h = {}, g = {};
        lm.lowMinus = true;
        l.low = true;
        lp.lowPlus = true;
        h.h = true;
        g.gearPressed = 4;
        Pass(m, plain, lm, t);
        const bool lowMinus = t.gear == 1 && t.power == kLowRangePower[0];
        Pass(m, plain, l, t);
        const bool low = t.power == kLowRangePower[1];
        Pass(m, plain, lp, t);
        const bool lowPlus = t.power == kLowRangePower[2];
        Pass(m, plain, h, t);
        const bool high = t.gear == 7 && t.power == 1.05f;
        Pass(m, plain, g, t);
        const bool geared = t.gear == 4 && t.power == 1.05f;
        GearboxActions down = {};
        down.rangeLow = true;
        Pass(m, plain, down, t);
        const bool rangedLow = m.pos == kPosLow && t.gear == 1;
        GearboxActions upRange = {};
        upRange.rangeHigh = true;
        Pass(m, plain, upRange, t);
        Check(lowMinus && low && lowPlus && high && geared && rangedLow && m.pos == kPosGear && t.gear == 1 && t.power == 1.05f,
              "gearbox: the power factor is the truck's own in A, the gears and H, the low range's in L-, L and L+; the range keys move between the low range and the gears");
    }
    // a gear the game refused is not asked on every pass after, and told once a position; a new position asks again
    // (review 7, R1)
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(2);
        Pass(m, plain, None(), t);
        const GearboxPosOrders first = GearboxPosStep(m, plain, Up(), t, 1); // not taken: t stays in 2
        const bool sayFirst = GearboxPosRefused(m, first.gear);
        int asked = 0;
        for (int i = 0; i < 20; i++) asked += GearboxPosStep(m, plain, None(), t, 1).setGear;
        const bool sayAgain = GearboxPosRefused(m, first.gear);
        const GearboxPosOrders next = GearboxPosStep(m, plain, Up(), t, 1);
        Check(first.setGear && first.gear == 3 && sayFirst && asked == 0 && !sayAgain && next.setGear && next.gear == 4 && GearboxPosRefused(m, 4),
              "gearbox: a gear the game did not take is not asked on every pass after, the refusal told once a position; the next position asks again");
    }
    // H: down lands on the top gear, up stays in H; up from A starts from the game's gear; the range keys from A and
    // from H go to L, and high from the gears does nothing (review 7, R2, R11)
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(3);
        GearboxActions h = {};
        h.h = true;
        Pass(m, plain, h, t);
        Pass(m, plain, Up(), t);
        const bool upStays = m.pos == kPosH && t.gear == 7;
        Pass(m, plain, Down(), t);
        const bool downTop = m.pos == kPosGear && t.gear == 6;
        GearboxPosModel ma = {};
        GearboxTruck ta = Truck(3);
        ta.automatic = true;
        Pass(ma, plain, None(), ta, 3);
        ta.automatic = false;
        Pass(ma, plain, Up(), ta, 3);
        const bool upFromA = ma.pos == kPosGear && ta.gear == 4;
        GearboxActions low = {}, high = {}, a = {};
        low.rangeLow = true;
        high.rangeHigh = true;
        a.a = true;
        Pass(ma, plain, a, ta, 3);
        Pass(ma, plain, low, ta, 3);
        const bool lowFromA = ma.pos == kPosLow && ta.gear == 1;
        Pass(m, plain, h, t);
        Pass(m, plain, low, t);
        const bool lowFromH = m.pos == kPosLow;
        GearboxActions g = {};
        g.gearPressed = 2;
        Pass(m, plain, g, t);
        Pass(m, plain, high, t);
        Check(upStays && downTop && upFromA && lowFromA && lowFromH && m.pos == kPosGear && t.gear == 2,
              "gearbox: down from H lands on the top gear and up stays in H; up from A goes on from the game's gear; the low range key from A or H gives L");
    }
    // a truck taken in the game's L- or L reads as that, the other positions at the plain factor (review 7, R3)
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        t.power = 0.45f;
        Pass(m, plain, None(), t);
        const bool inL = m.pos == kPosLow && t.power == kLowRangePower[1];
        GearboxActions g = {};
        g.gearPressed = 3;
        Pass(m, plain, g, t);
        GearboxPosModel mm = {};
        GearboxTruck tm = Truck(1);
        tm.power = 0.2f;
        GearboxPosTake(mm, tm);
        Check(inL && t.gear == 3 && t.power == 1.0f && mm.pos == kPosLowMinus && mm.power == 1.0f && !strcmp(GearboxPosLetter(kPosLow), "L"),
              "gearbox: a truck taken in the game's L- or L reads as that, and its gears get the plain factor, not the low one");
    }
    // no clutch axis is the default of the actions (review 7, R7)
    {
        GearboxActions a = {};
        GearboxPosModel m = {};
        GearboxTruck t = Truck(2);
        GearboxPosStep(m, plain, a, t, 1);
        Check(a.clutchTravel < 0.0f && m.travel == 0.0f && !m.clutch, "gearbox: actions read no clutch axis unless one is given");
    }

    // the gate (0.17.0): which hooks a level needs, attached once and never taken out while the game runs
    {
        const HooksWanted none = GearboxHooksWanted(0, true), one = GearboxHooksWanted(1, true), twoOff = GearboxHooksWanted(2, false),
                          twoOn = GearboxHooksWanted(2, true);
        const bool chosen = !none.shifts && !none.truckUpdate && one.shifts && !one.truckUpdate && twoOff.shifts && !twoOff.truckUpdate && twoOn.shifts &&
                            twoOn.truckUpdate;
        HooksGate g = {};
        const bool idle = GearboxGateStep(g, none) == 0 && !g.shifts;                   // Gearbox=0: nothing asked
        const bool shiftsNow = GearboxGateStep(g, one) == kGateAttachShifts;              // the tab turns 1 on mid-game
        const bool onceOnly = GearboxGateStep(g, one) == 0;                               // a second pass attaches nothing
        const bool truckNow = GearboxGateStep(g, twoOn) == kGateAttachTruck;              // 2 with Damage: the truck update too
        const bool leftTruck = GearboxGateStep(g, twoOff) == kGateTruckLeft && GearboxGateStep(g, twoOff) == 0; // Damage off: left, said once
        const bool leftAll = GearboxGateStep(g, none) == kGateShiftsLeft && GearboxGateStep(g, none) == 0;     // back to 0: left, said once
        const bool again = GearboxGateStep(g, twoOn) == 0 && g.shifts && g.truckUpdate;   // on again: in place already, nothing twice
        Check(chosen && idle && shiftsNow && onceOnly && truckNow && leftTruck && leftAll && again,
              "gearbox gate: Gearbox=0 hooks nothing, 1 the eight, 2 the eight and with Damage the truck update; each attached once, also mid-game, "
              "and left in place when no longer needed, said once");
    }

    // A or a gear picked from R while the truck still rolls back: N until it stands or rolls forward, told once; a truck
    // rolling back in a forward gear it was already in keeps it (the first drives, 2026-10-26)
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(-1);
        t.speed = -2.6f;
        Pass(m, plain, None(), t);
        GearboxActions pickA = {};
        pickA.a = true;
        const GearboxPosOrders first = Pass(m, plain, pickA, t, 2);
        const bool waitsA = m.pos == kPosA && t.gear == 0 && m.waitBack && !strcmp(first.why, "A picked while rolling back: neutral until the truck stands");
        const GearboxPosOrders still = Pass(m, plain, None(), t, 2);
        const bool once = t.gear == 0 && !still.why[0];
        t.speed = -0.2f;
        t.pedal = 0.5f;
        Pass(m, plain, None(), t, 2);
        const bool goesA = t.gear == 2 && !m.waitBack;

        GearboxPosModel g = {};
        GearboxTruck u = Truck(-1);
        u.speed = -3.0f;
        Pass(g, plain, None(), u);
        GearboxActions pick2 = {};
        pick2.gearPressed = 2;
        const GearboxPosOrders key = Pass(g, plain, pick2, u);
        const bool waitsGear = g.pos == kPosGear && u.gear == 0 && !strcmp(key.why, "a gear picked while rolling back: neutral until the truck stands");
        u.speed = 0.3f;
        Pass(g, plain, None(), u);
        const bool goesGear = u.gear == 2;

        GearboxPosModel f = {};
        GearboxTruck v = Truck(1);
        v.speed = -2.0f; // rolling back down a grade in the first gear
        Pass(f, plain, None(), v);
        const GearboxPosOrders keep = Pass(f, plain, None(), v);
        const bool kept = v.gear == 1 && !keep.setGear && !f.waitBack;
        Check(waitsA && once && goesA && waitsGear && goesGear && kept,
              "gearbox: A or a gear picked from R while rolling back waits in N until the truck stands, told once; a forward gear already in stays");
    }

    // the hooks' policy: while the gearbox has the truck the game's own six shifts and its power factor are refused, the
    // vehicle change passes, and the mod's own calls through GearboxOriginal go through; with the flag off all pass
    {
        FunctionFind finds[kFnCount] = {};
        void *fakes[kFnCount] = { (void *)&FakeShift, (void *)&FakeAuto, (void *)&FakeHigh, (void *)&FakeReverse, (void *)&FakeNeutral, nullptr,
                                  (void *)&FakeAutoOff, (void *)&FakePower, (void *)&FakeVehicle };
        for (int f = 0; f < kFnCount; f++)
            if (fakes[f]) finds[f] = FunctionFind{ kFnFound, (size_t)(uintptr_t)fakes[f], 1, false };
        char why[512];
        const bool hooked = GearboxHook(0, finds, why, sizeof why) == 8;
        // flag off: everything passes
        const bool passOff = FakeShift(nullptr, 3) && FakeReverse(nullptr) && g_ran[kFnShiftGear] == 1 && g_ran[kFnShiftToReverse] == 1;
        GearboxPolicy(kPolicyAll, nullptr, false);
        const bool refShift = !FakeShift(nullptr, 3);
        FakeAuto(nullptr);
        const bool refHigh = !FakeHigh(nullptr), refReverse = !FakeReverse(nullptr), refNeutral = !FakeNeutral(nullptr), refOff = !FakeAutoOff(nullptr, 2);
        FakePower(nullptr, 0.5f);
        FakeVehicle(nullptr, nullptr);
        const bool bodies = g_ran[kFnShiftGear] == 1 && g_ran[kFnShiftToAuto] == 0 && g_ran[kFnShiftToHigh] == 0 && g_ran[kFnShiftToReverse] == 1 &&
                            g_ran[kFnShiftToNeutral] == 0 && g_ran[kFnAutoOffAndShift] == 0 && g_ran[kFnSetPower] == 0 && g_ran[kFnSetVehicle] == 1;
        GearboxCounts c;
        GearboxCountsGet(c);
        const bool counted = c.refused[kFnShiftGear] == 1 && c.refused[kFnShiftToAuto] == 1 && c.refused[kFnSetPower] == 1 && c.refused[kFnSetVehicle] == 0;
        const unsigned firsts = GearboxRefusedNew(), none = GearboxRefusedNew();
        // the mod's own call goes through the way past the hook
        typedef bool (*Shift)(void *, int32_t);
        const bool own = ((Shift)GearboxOriginal(kFnShiftGear))(nullptr, 4) && g_ran[kFnShiftGear] == 2;
        // another vehicle's call passes while the held truck's are refused (review 7, R5)
        const bool other = FakeShift((void *)0x1234, 3) && g_ran[kFnShiftGear] == 3 && !FakeShift(nullptr, 3) && g_ran[kFnShiftGear] == 3;
        GearboxPolicy(kPolicyPass, nullptr, false);
        const bool passAgain = FakeHigh(nullptr) && g_ran[kFnShiftToHigh] == 1;
        GearboxUnhook();
        Check(hooked && other && passOff && refShift && refHigh && refReverse && refNeutral && refOff && bodies && counted && own && passAgain &&
                  (firsts & (1u << kFnShiftToReverse)) && (firsts & (1u << kFnSetPower)) && !(firsts & (1u << kFnSetVehicle)) && none == 0,
              "gearbox hooks: while the gearbox has the truck the game's six shifts and its power factor are refused and counted, each kind told once, the "
              "vehicle change passes and the mod's own call goes through; with the flag off all pass");
    }

    // the levels: at Gearbox=1 only a reverse while standing in A with the brake down and the game's automatic flag up
    // is refused, at whichever door it comes; with the flag down (the shifter's R) it passes, other shifts pass, and a
    // reverse with the truck rolling passes; at 0 nothing is refused
    {
        FunctionFind finds[kFnCount] = {};
        void *fakes[kFnCount] = { (void *)&FakeShift, (void *)&FakeAuto, (void *)&FakeHigh, (void *)&FakeReverse, (void *)&FakeNeutral, nullptr,
                                  (void *)&FakeAutoOff, (void *)&FakePower, (void *)&FakeVehicle };
        for (int f = 0; f < kFnCount; f++)
            if (fakes[f]) finds[f] = FunctionFind{ kFnFound, (size_t)(uintptr_t)fakes[f], 1, false };
        char why[512];
        const bool hooked = GearboxHook(0, finds, why, sizeof why) == 8;
        for (int f = 0; f < kFnCount; f++) g_ran[f] = 0;
        volatile uint8_t flag = 1; // the game's automatic flag: up for a moment after a tap
        GearboxPolicy(kPolicyBrakeReverse, nullptr, true, &flag);
        const bool refused = !FakeReverse(nullptr) && !FakeShift(nullptr, -1) && !FakeAutoOff(nullptr, -1);
        const bool passed = FakeShift(nullptr, 3) && FakeHigh(nullptr) && FakeNeutral(nullptr);
        const unsigned told = GearboxRefusedNew();
        const int door = GearboxBrakeDoor();
        flag = 0; // put down again: the shifter's R is the player's
        const bool flagDown = FakeReverse(nullptr) && FakeAutoOff(nullptr, -1);
        flag = 1;
        GearboxPolicy(kPolicyBrakeReverse, nullptr, false, &flag);
        const bool rolling = FakeReverse(nullptr) && FakeShift(nullptr, -1);
        GearboxPolicy(kPolicyBrakeReverse, nullptr, true, nullptr);
        const bool unknown = FakeReverse(nullptr); // no flag to read: passes
        GearboxPolicy(kPolicyPass, nullptr, true, &flag);
        const bool levelZero = FakeReverse(nullptr) && FakeAutoOff(nullptr, -1);
        GearboxUnhook();
        Check(hooked && refused && passed && flagDown && rolling && unknown && levelZero && (told & kRefusedBrakeReverse) && door == kFnAutoOffAndShift &&
                  g_ran[kFnShiftToReverse] == 4 && g_ran[kFnShiftGear] == 2 && g_ran[kFnShiftToHigh] == 1,
              "gearbox hooks: at Gearbox=1 only a reverse while the truck stands in A with the brake down and the game's automatic flag up is "
              "refused, at any of its three doors; with the flag down (the shifter's R) or rolling it passes; at 0 nothing is refused");
    }

    printf(g_failed ? "GEARBOX TEST FAILED: %d\n" : "GEARBOX TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
