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

    // the walk up and down: up to the top and no further; down from the 2nd through 1, L+, L, L- and N to R, each a
    // pass (the low range is the first gear at 1.00, 0.45 and 0.20); up from R the same row back to the 2nd
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        GearboxActions a = {};
        a.gearPressed = 1;
        Pass(m, plain, a, t);
        int tops = 0;
        for (int i = 0; i < 8; i++) Pass(m, plain, Up(), t), tops += t.gear == 6;
        const bool upTop = t.gear == 6 && m.pos == kPosGear && tops >= 3;
        for (int i = 0; i < 4; i++) Pass(m, plain, Down(), t);
        const bool at2 = t.gear == 2 && m.pos == kPosGear;
        const int row[7] = { kPosGear, kPosLowPlus, kPosLow, kPosLowMinus, kPosN, kPosR, kPosR };
        const int gears[7] = { 1, 1, 1, 1, 0, -1, -1 };
        const float powers[7] = { 1.0f, 1.0f, 0.45f, 0.2f, 1.0f, 1.0f, 1.0f };
        bool down = true;
        for (int i = 0; i < 7; i++)
        {
            Pass(m, plain, Down(), t);
            down = down && m.pos == row[i] && t.gear == gears[i] && fabsf(t.power - powers[i]) < 0.01f && (row[i] != kPosGear || m.gear == 1);
        }
        bool up = true;
        for (int i = 4; i >= 0; i--)
        {
            Pass(m, plain, Up(), t);
            up = up && m.pos == row[i] && t.gear == gears[i] && fabsf(t.power - powers[i]) < 0.01f;
        }
        Pass(m, plain, Up(), t);
        up = up && m.pos == kPosGear && m.gear == 2 && t.gear == 2;
        Check(upTop && at2 && down && up, "gearbox: up walks to the top and stops there; down from 2 walks 1, L+ (1.00), L (0.45), L- (0.20), N, R and stays; up from R walks the same row back to 2");
    }
    // H by its key: up from H stays, down from H lands on the top; A at the game's gear 1, down gives L+; the range keys
    // as before (RangeLow from a gear lands in L, RangeHigh from the low range in the 1st)
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(3);
        GearboxActions h = {};
        h.h = true;
        Pass(m, plain, h, t);
        Pass(m, plain, Up(), t);
        const bool staysH = m.pos == kPosH && t.gear == 7;
        Pass(m, plain, Down(), t);
        const bool toTop = m.pos == kPosGear && m.gear == 6 && t.gear == 6;
        GearboxActions aa = {};
        aa.a = true;
        Pass(m, plain, aa, t, 1);
        Pass(m, plain, None(), t, 1);
        const bool inA = m.pos == kPosA && t.gear == 1;
        Pass(m, plain, Down(), t, 1);
        const bool aToLowPlus = m.pos == kPosLowPlus && t.gear == 1;
        GearboxActions g = {};
        g.gearPressed = 3;
        Pass(m, plain, g, t);
        GearboxActions lo = {};
        lo.rangeLow = true;
        Pass(m, plain, lo, t);
        const bool rangeLow = m.pos == kPosLow && t.gear == 1 && fabsf(t.power - 0.45f) < 0.01f;
        GearboxActions hi = {};
        hi.rangeHigh = true;
        Pass(m, plain, hi, t);
        const bool rangeHigh = m.pos == kPosGear && m.gear == 1 && t.gear == 1 && fabsf(t.power - 1.0f) < 0.01f;
        Check(staysH && toTop && inA && aToLowPlus && rangeLow && rangeHigh,
              "gearbox: up from H stays, down from H gives the top; down from A in the game's gear 1 gives L+; RangeLow from 3 gives L, RangeHigh from L the 1st");
    }
    // SkipNeutral: from L- down to R and from R up to L-, past N; from the 1st down still walks L+
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        GearboxActions a = {};
        a.lowMinus = true;
        Pass(m, skip, a, t);
        Pass(m, skip, Down(), t);
        const bool toR = t.gear == -1 && m.pos == kPosR;
        Pass(m, skip, Up(), t);
        const bool toLowMinus = m.pos == kPosLowMinus && t.gear == 1 && fabsf(t.power - 0.2f) < 0.01f;
        a = {};
        a.gearPressed = 1;
        Pass(m, skip, a, t);
        Pass(m, skip, Down(), t);
        Check(toR && toLowMinus && m.pos == kPosLowPlus, "gearbox: SkipNeutral steps from L- to reverse and back without stopping in neutral; from the 1st down is still L+");
    }
    // DownStaysForward: down from the 1st gives L+, then L and L-, and from L- nothing; from N and R nothing; up from R
    // still walks N and L-; with SkipNeutral on too this one wins
    {
        const GearboxOptions stop = { false, false, false, true }, both = { false, false, true, true };
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        GearboxActions a = {};
        a.gearPressed = 1;
        Pass(m, stop, a, t);
        Pass(m, stop, Down(), t);
        const bool toLowPlus = m.pos == kPosLowPlus && t.gear == 1;
        Pass(m, stop, Down(), t);
        Pass(m, stop, Down(), t);
        const GearboxPosOrders atLowMinus = Pass(m, stop, Down(), t);
        const bool staysLowMinus = m.pos == kPosLowMinus && t.gear == 1 && !atLowMinus.moved;
        GearboxActions n = {};
        n.n = true;
        Pass(m, stop, n, t);
        const GearboxPosOrders fromN = Pass(m, stop, Down(), t);
        const bool staysN = t.gear == 0 && m.pos == kPosN && !fromN.moved;
        GearboxActions r = {};
        r.r = true;
        Pass(m, stop, r, t);
        Pass(m, stop, Down(), t);
        const bool staysR = t.gear == -1 && m.pos == kPosR;
        Pass(m, stop, Up(), t);
        const bool upN = t.gear == 0;
        Pass(m, stop, Up(), t);
        const bool upLowMinus = m.pos == kPosLowMinus;
        GearboxPosModel k = {};
        GearboxTruck u = Truck(1);
        a = {};
        a.lowMinus = true;
        Pass(k, both, a, u);
        Pass(k, both, Down(), u);
        const bool wins = u.gear == 1 && k.pos == kPosLowMinus;
        Check(toLowPlus && staysLowMinus && staysN && staysR && upN && upLowMinus && wins,
              "gearbox: DownStaysForward steps down from the 1st to L+, L and L- and no further, does nothing from N or R, up from R still walks N and L-; it wins over SkipNeutral");
    }
    // the game's shifter's request (Gearbox=2 with a pad or the keyboard) as that pass's action: R, N, H, A, a gear (past
    // the top H) and L; under RequireClutch it stalls as a key would; DownStaysForward does not hold it back; a gear it
    // picked stays under RequireGearHeld, as the shifter stays where it was put
    {
        const GearboxOptions stop = { false, false, false, true };
        GearboxPosModel m = {};
        GearboxTruck t = Truck(3);
        Pass(m, stop, None(), t);
        auto ask = [&](int kind, int gear, const GearboxOptions &o) {
            GearboxActions a = {};
            GearboxActionsAsk(a, GearboxAsk{ kind, gear }, t.top);
            return Pass(m, o, a, t);
        };
        ask(kAskR, 0, stop);
        const bool r = m.pos == kPosR && t.gear == -1;
        ask(kAskN, 0, stop);
        const bool n = m.pos == kPosN && t.gear == 0;
        ask(kAskGear, 4, stop);
        const bool g4 = m.pos == kPosGear && t.gear == 4;
        ask(kAskH, 0, stop);
        const bool h = m.pos == kPosH && t.gear == 7;
        ask(kAskGear, 7, stop);
        const bool past = m.pos == kPosH;
        ask(kAskLow, 0, stop);
        const bool low = m.pos == kPosLow && t.gear == 1 && fabsf(t.power - 0.45f) < 0.01f;
        t.automatic = false;
        ask(kAskA, 0, stop);
        const bool isA = m.pos == kPosA;
        const GearboxOptions clutchOn = { true, false, false, false }, heldOn = { false, true, false, false };
        // crawling, a gear forced in without the clutch stalls; at speed it stays out (review 11)
        const GearboxPosOrders grind = ask(kAskGear, 2, clutchOn);
        const bool staysOut = !grind.stall && m.pos == kPosN && t.gear == 0 && grind.why[0];
        t.speed = 0.5f * kGrindSpeed;
        const GearboxPosOrders stalled = ask(kAskGear, 2, clutchOn);
        const bool stalls = stalled.stall && t.gear == 2 && staysOut;
        t.speed = 5.0f;
        ask(kAskGear, 3, heldOn);
        Pass(m, heldOn, None(), t);
        const bool held = m.pos == kPosGear && t.gear == 3;
        GearboxActions none = {};
        GearboxActionsAsk(none, GearboxAsk{ kAskNone, 0 }, t.top);
        Check(r && n && g4 && h && past && low && isA && stalls && held && !none.shifter,
              "gearbox: the game's shifter's request picks R, N, a gear, H (a gear past the top too), L and A as their keys do; under RequireClutch it stalls "
              "crawling and stays out at speed; "
              "a gear it picked stays under RequireGearHeld");
    }
    // the doors' calls as one request: the first names it, the later ones of the same shift repeat it, the power factor
    // turns a first gear into L or L-, a factor alone asks nothing
    {
        GearboxAsk p = {};
        p = GearboxAskMerge(p, kFnShiftToReverse, 0, 0.0f);
        p = GearboxAskMerge(p, kFnAutoOffAndShift, -1, 0.0f);
        const bool rev = p.kind == kAskR;
        GearboxAsk g = GearboxAskMerge(GearboxAsk{}, kFnAutoOffAndShift, 1, 0.0f);
        g = GearboxAskMerge(g, kFnShiftGear, 1, 0.0f);
        const bool first = g.kind == kAskGear && g.gear == 1;
        const GearboxAsk low = GearboxAskMerge(g, kFnSetPower, 0, 0.45f), lowMinus = GearboxAskMerge(g, kFnSetPower, 0, 0.2f), plus = GearboxAskMerge(g, kFnSetPower, 0, 1.0f);
        const bool alone = GearboxAskMerge(GearboxAsk{}, kFnSetPower, 0, 0.45f).kind == kAskNone;
        const bool doors = GearboxAskMerge(GearboxAsk{}, kFnShiftToNeutral, 0, 0.0f).kind == kAskN && GearboxAskMerge(GearboxAsk{}, kFnShiftToHigh, 0, 0.0f).kind == kAskH &&
                           GearboxAskMerge(GearboxAsk{}, kFnShiftToAuto, 0, 0.0f).kind == kAskA && GearboxAskMerge(GearboxAsk{}, kFnShiftGear, 0, 0.0f).kind == kAskN;
        Check(rev && first && low.kind == kAskLow && lowMinus.kind == kAskLowMinus && plus.kind == kAskGear && alone && doors,
              "gearbox: a shift's doors make one request, the first door naming it; the power factor after a first gear makes L or L- (L+ stays the first gear); a factor alone asks nothing");
    }
    // the hand-back at level 1 by the door, on counts as the hooks nest them (reverse and neutral through auto off,
    // auto off through ShiftGear): L+ (AutoOffAndShift(1), the first gear the automatic set last) hands back as H, N
    // and R do; a bare ShiftGear does not; nothing out of a position other than A, with the game's automatic flag up,
    // or from a refused call
    {
        GearboxCounts c = {};
        c.shift = 40;
        c.autoOff = 7;
        const GearboxDoors was = GearboxDoorsOf(c);
        auto after = [&](long shift, long autoOff, long high, long neutral, long reverse) {
            GearboxCounts n = c;
            n.shift += shift;
            n.autoOff += autoOff;
            n.high += high;
            n.neutral += neutral;
            n.reverse += reverse;
            return n;
        };
        const GearboxDoors lowPlus = GearboxDoorsOf(after(1, 1, 0, 0, 0)), high = GearboxDoorsOf(after(1, 0, 1, 0, 0)), neutral = GearboxDoorsOf(after(1, 1, 0, 1, 0)),
                           reverse = GearboxDoorsOf(after(1, 1, 0, 0, 1)), bare = GearboxDoorsOf(after(1, 0, 0, 0, 0));
        GearboxCounts refusedCounts = after(1, 1, 0, 0, 1);
        refusedCounts.refused[kFnShiftToReverse] = 1;
        refusedCounts.refused[kFnAutoOffAndShift] = 1;
        const GearboxDoors refused = GearboxDoorsOf(refusedCounts);
        const bool picks = GearboxHandsBack(was, lowPlus, true, false) && GearboxHandsBack(was, high, true, false) && GearboxHandsBack(was, neutral, true, false) &&
                           GearboxHandsBack(was, reverse, true, false);
        const bool keeps = !GearboxHandsBack(was, bare, true, false) && !GearboxHandsBack(was, was, true, false) && !GearboxHandsBack(was, lowPlus, false, false) &&
                           !GearboxHandsBack(was, reverse, false, false) && !GearboxHandsBack(was, lowPlus, true, true) && !GearboxHandsBack(was, refused, true, false);
        const char *plusName = GearboxDoorPos(was, lowPlus, 1, 1.0f), *lowName = GearboxDoorPos(was, lowPlus, 1, 0.45f), *minusName = GearboxDoorPos(was, lowPlus, 1, 0.2f);
        const char *hName = GearboxDoorPos(was, high, 7, 1.0f), *nName = GearboxDoorPos(was, neutral, 0, 1.0f), *rName = GearboxDoorPos(was, reverse, -1, 1.0f);
        const char *gearName = GearboxDoorPos(was, GearboxDoorsOf(after(1, 1, 0, 0, 0)), 3, 1.0f);
        const bool names = plusName && !strcmp(plusName, "L+") && lowName && !strcmp(lowName, "L") && minusName && !strcmp(minusName, "L-") && hName && !strcmp(hName, "H") &&
                           nName && !strcmp(nName, "N") && rName && !strcmp(rName, "R") && !gearName;
        Check(picks && keeps && names,
              "gearbox: at level 1 a door hands the truck back whatever gear it names (L+ though the automatic's last gear was 1, H, N, R); a bare ShiftGear, a position other "
              "than A, the flag up or a refused call does not; the log names L+, L, L-, H, N, R or the gear");
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
    // RequireClutch, crawling: a shift without the clutch shifts and stalls once; into N no stall; with the clutch none; an
    // engine already dying none. At speed (review 11) the gear grinds and stays out, neutral, with no stall
    {
        GearboxPosModel atSpeed = {};
        GearboxTruck ts = Truck(2);
        GearboxPosStep(atSpeed, clutch, None(), ts, 1);
        const GearboxPosOrders grind = Pass(atSpeed, clutch, Up(), ts);
        const bool out = !grind.stall && atSpeed.pos == kPosN && ts.gear == 0 && atSpeed.stalls == 0 && grind.why[0];
        GearboxPosModel m = {};
        GearboxTruck t = Truck(2);
        t.speed = 0.5f * kGrindSpeed;
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
        Check(out && stalled && !again.stall && !toN.stall && t.gear >= 1 && !clean.stall && !dying.stall && m.stalls == 1,
              "gearbox: RequireClutch, crawling, stalls the engine once for a shift into a gear without the clutch, not into neutral, not with the clutch, not "
              "when it is already dying; at speed the gear stays out instead");
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

    // the game's stall seconds (the user's hill test, 27 October): a stall ordered ramps from 0 to kStallSeconds over
    // kStallRamp; lugging raises them by dt a pass, and the recovery writes 0 once; nothing with the engine off
    {
        StallWrite w = {};
        const float dt = 0.05f;
        const float first = StallStep(w, true, false, true, dt);
        float last = first;
        int passes = 1;
        while (w.ramping && passes < 20)
        {
            last = StallStep(w, false, false, true, dt);
            passes++;
        }
        const bool ramped = first > 0.0f && first < kStallSeconds && last == kStallSeconds && passes == (int)(kStallRamp / dt + 0.5f) &&
                            StallStep(w, false, false, true, dt) < 0.0f;
        float lug = 0.0f;
        for (int i = 0; i < 10; i++) lug = StallStep(w, false, true, true, dt);
        const bool lugging = fabsf(lug - 0.5f) < 0.001f;
        const bool cleared = StallStep(w, false, false, true, dt) == 0.0f && StallStep(w, false, false, true, dt) < 0.0f;
        StallStep(w, false, true, true, dt);
        const bool dead = StallStep(w, false, true, false, dt) < 0.0f && w.lug == 0.0f && !w.ramping;
        Check(ramped && lugging && cleared && dead,
              "gearbox: a stall ramps the game's stall seconds from 0 past 1.4 in 0.3 s; lugging raises them by the pass's time and the recovery clears "
              "them once; nothing with the engine off");
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

    // the game's shifter at Gearbox=2 with a pad or the keyboard: its calls stay refused, the first door of a shift becomes
    // one request taken on the next pass, the later doors of the same shift do not repeat it; with the asks off (a wheel's
    // pedals) none is made
    {
        FunctionFind finds[kFnCount] = {};
        void *fakes[kFnCount] = { (void *)&FakeShift, (void *)&FakeAuto, (void *)&FakeHigh, (void *)&FakeReverse, (void *)&FakeNeutral, nullptr,
                                  (void *)&FakeAutoOff, (void *)&FakePower, (void *)&FakeVehicle };
        for (int f = 0; f < kFnCount; f++)
            if (fakes[f]) finds[f] = FunctionFind{ kFnFound, (size_t)(uintptr_t)fakes[f], 1, false };
        char why[512];
        const bool hooked = GearboxHook(0, finds, why, sizeof why) == 8;
        for (int f = 0; f < kFnCount; f++) g_ran[f] = 0;
        GearboxAsk a = {};
        GearboxAskTake(a); // nothing left from before
        GearboxPolicy(kPolicyAll, nullptr, false);
        GearboxShifterAsks(false);
        const bool offRefused = !FakeReverse(nullptr);
        const bool offNone = !GearboxAskTake(a);
        GearboxShifterAsks(true);
        const bool refused = !FakeAutoOff(nullptr, 1) && !FakeShift(nullptr, 1);
        FakePower(nullptr, 0.45f);
        const bool low = GearboxAskTake(a) && a.kind == kAskLow;
        const bool once = !GearboxAskTake(a);
        const bool rev = !FakeReverse(nullptr) && !FakeAutoOff(nullptr, -1) && GearboxAskTake(a) && a.kind == kAskR && !GearboxAskTake(a);
        const bool gear = !FakeAutoOff(nullptr, 5) && GearboxAskTake(a) && a.kind == kAskGear && a.gear == 5;
        const bool other = FakeReverse((void *)0x1234) && !GearboxAskTake(a); // another vehicle's call passes and asks nothing
        GearboxShifterAsks(false);
        GearboxPolicy(kPolicyPass, nullptr, false);
        GearboxUnhook();
        Check(hooked && offRefused && offNone && refused && low && once && rev && gear && other && g_ran[kFnShiftGear] == 0 && g_ran[kFnAutoOffAndShift] == 0,
              "gearbox hooks: at Gearbox=2 with the asks on the game's shifter's calls stay refused and its first door is one request, taken once on the "
              "next pass (L from the power factor after the first gear); with the asks off none is made");
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
