// gearbox_test.cpp: the mod's own gearbox (src\gearbox_model.h) on made up passes, and the hooks' policy while it has
// the truck (src\gearbox_hooks.h) on functions of the test's own. Offline; build.bat makes out\test\gearbox_test.exe
// and test.bat runs it.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gauges.h" // BackToA's default and the Sequential button's
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

// One pass of dt seconds with these actions; the game takes the gear set.
static GearboxPosOrders Pass(GearboxPosModel &m, const GearboxOptions &o, GearboxActions a, GearboxTruck &t, int autoGear = 1, float dt = 0.05f)
{
    const GearboxPosOrders r = GearboxPosStep(m, o, a, t, autoGear, dt);
    if (r.setGear) t.gear = r.gear;
    if (r.setPower) t.power = r.power;
    return r;
}

static GearboxActions Up() { GearboxActions a = {}; a.up = true; return a; }
static GearboxActions Down() { GearboxActions a = {}; a.down = true; return a; }
static GearboxActions None() { return GearboxActions{}; }

// Floored at the red zone for seconds with Sequential and BackToA 3, both fed as gauges.cpp feeds them (GearboxPosHeld),
// A's gear the held one, so the foot down holds no return; the position just shifted by hand. True once either took it
// back to A (D).
static bool FloorToD(GearboxPosModel &m, GearboxTruck &t, float seconds)
{
    const float dt = 0.05f;
    SeqModel sm = {};
    BackModel bm = {};
    bool hand = true;
    for (float at = 0.0f; at < seconds; at += dt)
    {
        SeqInputs in = {};
        in.backToA = 3;
        in.pos = m.pos;
        in.gear = m.gear;
        in.top = t.top;
        in.byShifter = GearboxPosHeld(m);
        in.redline = 3000.0f;
        in.input = 0.95f * in.redline;
        in.idle = 700.0f;
        in.pedal = 1.0f;
        in.gripKnown = in.engineOn = true;
        BackInputs b = {};
        b.pos = m.pos;
        b.heldGear = m.gear;
        b.autoGear = m.gear < 1 ? 1 : m.gear;
        b.byShifter = GearboxPosHeld(m);
        b.pedal = 1.0f;
        b.grip = 1.2f;
        b.speed = 5.0f;
        b.spin = 6.0f;
        b.pitchKnown = b.engineOn = true;
        const bool back = BackToAStep(bm, 3, hand, b, dt);
        const int seq = back ? 0 : SequentialStep(sm, in, dt);
        hand = false;
        GearboxActions a = {};
        a.a = back || seq != 0;
        Pass(m, GearboxOptions{}, a, t, m.gear < 1 ? 1 : m.gear, dt);
        if (m.pos == kPosA) return true;
    }
    return false;
}

// The truck walked up from L into L+ (the hold starts), from a 1st in the game.
static void WalkToLowPlus(GearboxPosModel &m, GearboxTruck &t)
{
    GearboxActions l = {};
    l.low = true;
    Pass(m, GearboxOptions{}, l, t);
    Pass(m, GearboxOptions{}, Up(), t, 1, 0.05f);
}

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
        for (int i = 4; i >= 1; i--)
        {
            const GearboxPosOrders r = Pass(m, plain, Up(), t);
            up = up && m.pos == row[i] && t.gear == gears[i] && fabsf(t.power - powers[i]) < 0.01f && !r.lowUp;
        }
        // up from L+ stays in the low range within a second of the walk getting there (BRIEF_low_range_up_to_1.md), the
        // refusal told once a visit; a press after it goes to the 1st, held
        bool stays = true;
        int said = 0;
        for (int i = 0; i < 3; i++)
        {
            const GearboxPosOrders r = Pass(m, plain, Up(), t);
            stays = stays && m.pos == kPosLowPlus && t.gear == 1 && !r.moved && r.lowUp && !r.setGear;
            said += r.lowUpSay;
        }
        for (int i = 0; i < 17; i++) Pass(m, plain, None(), t);
        const GearboxPosOrders go = Pass(m, plain, Up(), t);
        const bool first = m.pos == kPosGear && m.gear == 1 && t.gear == 1 && go.lowToFirst && m.heldFromLow && GearboxPosHeld(m);
        Pass(m, plain, Down(), t);
        const bool backLowPlus = m.pos == kPosLowPlus && !m.heldFromLow;
        Pass(m, plain, Down(), t);
        const bool downLow = m.pos == kPosLow;
        Check(upTop && at2 && down && up && stays && said == 1 && first && backLowPlus && downLow,
              "gearbox: up walks to the top and stops there; down from 2 walks 1, L+ (1.00), L (0.45), L- (0.20), N, R and stays; up from R walks back to L+, "
              "where up stays within a second (told once) and then goes to a held 1st; down from it is L+, then L");
    }
    // the low range never goes to D by itself: L+ floored at the red zone with Sequential and BackToA 3 stays L+; a 1st
    // reached from the low range (up from L+ after the hold, RangeHigh from L, the key 1 from L-) stays held floored 5 s
    // with them, and up from it gives a 2nd that Sequential takes to D at the red zone as any held gear; a gear key 3
    // from L gives an ordinary 3rd; down from a numbered 1st is L+ (BRIEF_low_range_up_to_1.md, the lead's calls)
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        WalkToLowPlus(m, t);
        const bool lowStays = !FloorToD(m, t, 5.0f) && m.pos == kPosLowPlus;
        // floored 5 s in L+ counts: up goes now, to a held 1st
        Pass(m, plain, Up(), t);
        const bool upHeld = m.pos == kPosGear && m.gear == 1 && !FloorToD(m, t, 5.0f) && m.pos == kPosGear && m.gear == 1;
        Pass(m, plain, Up(), t);
        const bool second = m.pos == kPosGear && m.gear == 2 && !m.heldFromLow && FloorToD(m, t, 5.0f);
        GearboxPosModel r = {};
        GearboxTruck tr = Truck(1);
        GearboxActions lo = {};
        lo.low = true;
        Pass(r, plain, lo, tr);
        GearboxActions hi = {};
        hi.rangeHigh = true;
        Pass(r, plain, hi, tr);
        const bool rangeHeld = r.pos == kPosGear && r.gear == 1 && r.heldFromLow && !FloorToD(r, tr, 5.0f) && r.pos == kPosGear && r.gear == 1;
        GearboxPosModel k = {};
        GearboxTruck tk = Truck(1);
        GearboxActions lm = {};
        lm.lowMinus = true;
        Pass(k, plain, lm, tk);
        GearboxActions g1 = {};
        g1.gearPressed = 1;
        Pass(k, plain, g1, tk);
        const bool keyHeld = k.pos == kPosGear && k.gear == 1 && k.heldFromLow && !FloorToD(k, tk, 5.0f) && k.pos == kPosGear && k.gear == 1;
        GearboxPosModel g = {};
        GearboxTruck tg = Truck(1);
        Pass(g, plain, lo, tg);
        GearboxActions g3 = {};
        g3.gearPressed = 3;
        Pass(g, plain, g3, tg);
        const bool third = g.pos == kPosGear && g.gear == 3 && tg.gear == 3 && !g.heldFromLow && FloorToD(g, tg, 5.0f);
        GearboxPosModel d = {};
        GearboxTruck td = Truck(1);
        Pass(d, plain, g1, td);
        Pass(d, plain, Down(), td);
        const bool lowPlus = d.pos == kPosLowPlus;
        Check(lowStays && upHeld && second && rangeHeld && keyHeld && third && lowPlus,
              "gearbox: L+ floored at the red zone with Sequential and BackToA 3 stays L+; a 1st by up from L+, RangeHigh from L or the key 1 from L- stays held "
              "there 5 s; up from it gives a 2nd that goes to D; a gear key 3 from L gives an ordinary 3rd that goes to D; down from the 1st is L+");
    }
    // the hold on up from L+ (BRIEF_low_range_up_to_1.md and the Reviewer's addendum): it starts only when an up press
    // brought the truck to L+, a refused press does not restart it, and the time away on the map counts for nothing
    {
        // up from R pressed 0.3 s apart: the 5th refused (0.3 s in L+), told; the 6th and 7th refused untold; the 8th,
        // 1.2 s in L+, goes to the 1st
        GearboxPosModel m = {};
        GearboxTruck t = Truck(-1);
        t.speed = 0.0f;
        GearboxActions rr = {};
        rr.r = true;
        Pass(m, plain, rr, t);
        int presses = 0;
        bool fifth = false, untold = true, eighth = false;
        float refusedAt = -1.0f;
        for (presses = 1; presses <= 8; presses++)
        {
            const GearboxPosOrders o = Pass(m, plain, Up(), t, 1, 0.3f);
            if (presses == 5) fifth = m.pos == kPosLowPlus && o.lowUp && o.lowUpSay, refusedAt = o.lowUpAfter;
            if (presses == 6 || presses == 7) untold = untold && m.pos == kPosLowPlus && o.lowUp && !o.lowUpSay;
            if (presses == 8) eighth = m.pos == kPosGear && m.gear == 1 && o.lowToFirst && m.heldFromLow;
        }
        // in L+ 0.5 s, then up: stays; 0.6 s later up again: the 1st
        GearboxPosModel h = {};
        GearboxTruck th = Truck(1);
        WalkToLowPlus(h, th);
        Pass(h, plain, None(), th, 1, 0.25f);
        Pass(h, plain, Up(), th, 1, 0.25f);
        const bool halfStays = h.pos == kPosLowPlus;
        Pass(h, plain, None(), th, 1, 0.3f);
        Pass(h, plain, Up(), th, 1, 0.3f);
        const bool againGoes = h.pos == kPosGear && h.gear == 1;
        // down from the 1st into L+, up 0.2 s later: the 1st
        GearboxPosModel dn = {};
        GearboxTruck td = Truck(1);
        GearboxActions g1 = {};
        g1.gearPressed = 1;
        Pass(dn, plain, g1, td);
        Pass(dn, plain, Down(), td);
        Pass(dn, plain, Up(), td, 1, 0.2f);
        const bool downUp = dn.pos == kPosGear && dn.gear == 1;
        // the L+ key, up 0.2 s later: the 1st
        GearboxPosModel ky = {};
        GearboxTruck tk = Truck(1);
        GearboxActions lp = {};
        lp.lowPlus = true;
        Pass(ky, plain, lp, tk);
        Pass(ky, plain, Up(), tk, 1, 0.2f);
        const bool keyUp = ky.pos == kPosGear && ky.gear == 1;
        // a second walk into L+ tells its refusal again
        GearboxActions lo = {};
        lo.low = true;
        Pass(ky, plain, lo, tk);
        Pass(ky, plain, Up(), tk);
        const bool toldAgain = ky.pos == kPosLowPlus && Pass(ky, plain, Up(), tk).lowUpSay;
        // L+ kept through a map pass with 0.6 s counted, then up 0.5 s after the return: the 1st
        GearboxPosModel mp = {};
        GearboxTruck tm = Truck(1);
        WalkToLowPlus(mp, tm);
        Pass(mp, plain, None(), tm, 1, 0.6f);
        const bool kept = GearboxPosKeeps(mp, tm); // the map: no pass while away
        Pass(mp, plain, None(), tm, 1, 0.3f);
        Pass(mp, plain, Up(), tm, 1, 0.2f);
        const bool mapUp = kept && mp.pos == kPosGear && mp.gear == 1;
        // the held 1st kept through a map pass stays held; read again (a gear changed while away) it is not
        const bool heldKept = GearboxPosKeeps(mp, tm) && mp.heldFromLow;
        GearboxTruck changed = tm;
        changed.gear = 2;
        GearboxPosModel re = mp;
        if (!GearboxPosKeeps(re, changed)) GearboxPosTake(re, changed);
        const bool readAgain = re.pos == kPosGear && re.gear == 2 && !re.heldFromLow && !GearboxPosHeld(re);
        Check(fifth && refusedAt > 0.29f && refusedAt < 0.31f && untold && eighth && halfStays && againGoes && downUp && keyUp && toldAgain && mapUp && heldKept && readAgain,
              "gearbox: up from L+ waits a second after an up press got there: from R 0.3 s apart the 5th is refused (told once a visit), the 8th goes to a held 1st; "
              "0.5 s refused, 0.6 s later goes; down from the 1st or the L+ key let up go at once; the map keeps the time and the held 1st, a read again does not");
    }
    // back in the world after the map, the garage or the viewer (BRIEF_low_range_never_d.md, the Reviewer's point 1):
    // the position stays while the game has the gear and the power the mod set (L+ stays L+, A in the game's gear 2
    // stays A), and is read again when they changed
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        GearboxActions l = {};
        l.lowPlus = true;
        Pass(m, plain, l, t);
        const bool lowPlusKept = GearboxPosKeeps(m, t) && m.pos == kPosLowPlus;
        GearboxActions aa = {};
        aa.a = true;
        Pass(m, plain, aa, t, 2);
        const bool aKept = t.gear == 2 && GearboxPosKeeps(m, t) && m.pos == kPosA;
        GearboxTruck changed = t;
        changed.gear = 3;
        GearboxTruck flagUp = t;
        GearboxPosModel g = m;
        GearboxActions g4 = {};
        g4.gearPressed = 4;
        GearboxTruck tg = t;
        Pass(g, plain, g4, tg);
        flagUp = tg;
        flagUp.automatic = true;
        GearboxTruck power = tg;
        power.power = 0.45f;
        const bool readAgain = !GearboxPosKeeps(m, changed) && !GearboxPosKeeps(g, flagUp) && !GearboxPosKeeps(g, power) && GearboxPosKeeps(g, tg);
        GearboxPosModel took = m;
        if (!GearboxPosKeeps(took, changed)) GearboxPosTake(took, changed);
        const bool takenAgain = took.pos == kPosGear && took.gear == 3;
        // A at a stop in the game's N (it waits for the pedal there), though the mod set the 1st last: kept as A
        GearboxPosModel w = {};
        GearboxTruck tw = Truck(1);
        Pass(w, plain, aa, tw, 1);
        tw.gear = 0;
        tw.speed = 0.0f;
        const bool waiting = w.pos == kPosA && w.lastSet == 1 && GearboxPosKeeps(w, tw);
        // L+ kept stays there floored with Sequential and BackToA 3
        GearboxPosModel lp = {};
        GearboxTruck tl = Truck(1);
        Pass(lp, plain, l, tl);
        if (!GearboxPosKeeps(lp, tl)) GearboxPosTake(lp, tl);
        SeqInputs in = {};
        in.backToA = 3;
        in.pos = lp.pos;
        in.gear = lp.gear;
        in.top = 6;
        in.redline = 3000.0f;
        in.input = 0.95f * in.redline;
        in.idle = 700.0f;
        in.pedal = 1.0f;
        in.gripKnown = in.engineOn = true;
        SeqModel sm = {};
        int seq = 0;
        for (int i = 0; i < 40; i++) seq |= SequentialStep(sm, in, 0.05f);
        // read again, as before, the same L+ is a numbered 1st: the case the keep is for
        GearboxPosModel re = lp;
        GearboxPosTake(re, tl);
        Check(lowPlusKept && aKept && readAgain && takenAgain && waiting && lp.pos == kPosLowPlus && seq == 0 && re.pos == kPosGear && re.gear == 1,
              "gearbox: back in the world L+ and A (in the game's gear 2, or waiting in N) stay; a gear, a power or the game's automatic changed meanwhile reads the truck again");
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
    // BackToA (review 17, Sequential): the default never returns; at 10 s a shift by hand returns after the timer and a
    // second steady, and the next shift by hand restarts it; each condition holds the return on its own; a failure
    // inside the steady second starts it over; R, N, the low range and H never return, nor a gear held by
    // RequireGearHeld or the game's shifter; a lock switched holds it 3 s; A's gear more than one away holds it while
    // the truck moves, not while it stands with the foot off
    {
        const float dt = 0.05f;
        BackInputs good = {};
        good.pos = kPosGear;
        good.heldGear = 4;
        good.autoGear = 5;
        good.pedal = 0.3f;
        good.grip = 1.2f;
        good.speed = 10.0f;
        good.spin = 12.0f;
        good.pitchKnown = true;
        good.engineOn = true;
        // seconds until the return after a shift by hand at 0, or -1 within `most`
        auto run = [&](int seconds, const BackInputs &in, float most, BackModel *keep = nullptr, float handAgainAt = -1.0f) {
            BackModel m = {};
            for (int i = 0; i * dt < most; i++)
            {
                const float t = i * dt;
                const bool hand = i == 0 || (handAgainAt >= 0.0f && fabsf(t - handAgainAt) < dt * 0.5f);
                if (BackToAStep(m, seconds, hand, in, dt)) { if (keep) *keep = m; return t; }
            }
            if (keep) *keep = m;
            return -1.0f;
        };
        const bool byDefault = run(kBackToADefault, good, 60.0f) < 0.0f && kBackToADefault == 0;
        const float at = run(kBackToASequential, good, 60.0f);
        const bool tenSteady = at >= 10.8f && at <= 11.15f;
        const float again = run(10, good, 60.0f, nullptr, 5.0f);
        const bool restarts = again >= 15.8f && again <= 16.15f;
        bool eachHolds = true;
        char which[96] = "";
        auto holds = [&](BackInputs in, int why, const char *name) {
            BackModel m = {};
            const bool back = run(10, in, 20.0f, &m) >= 0.0f;
            if (back || m.why != why)
            {
                eachHolds = false;
                if (!which[0]) snprintf(which, sizeof which, "%s (why %d)", name, m.why);
            }
        };
        BackInputs c = good;
        c.pedal = 0.8f; holds(c, kBackPedalDown, "pedal");
        c = good; c.grip = 0.0f; holds(c, kBackNoGrip, "grip");
        c = good; c.spin = 30.0f; holds(c, kBackSpin, "spin");
        c = good; c.pitchDeg = 10.0f; c.pedal = 0.5f; holds(c, kBackClimb, "climb");
        c = good; c.climbHold = true; holds(c, kBackClimb, "climb hold");
        c = good; c.pitchDeg = -7.0f; holds(c, kBackDescent, "descent");
        c = good; c.speed = -1.0f; c.spin = 1.2f; holds(c, kBackRollingBack, "rolling back");
        c = good; c.autoGear = 7; holds(c, kBackGap, "gap");
        c = good; c.handbrake = true; holds(c, kBackHandbrake, "handbrake");
        c = good; c.engineOn = false; holds(c, kBackEngine, "engine");
        c = good; c.dying = true; holds(c, kBackEngine, "dying");
        c = good; c.shiftBusy = true; holds(c, kBackShifting, "shift");
        c = good; c.byShifter = true; holds(c, kBackHeld, "game's shifter");
        c = good; c.gearHeldRule = true; holds(c, kBackHeld, "RequireGearHeld");
        static const int never[] = { kPosR, kPosN, kPosLowMinus, kPosLow, kPosLowPlus, kPosH };
        for (int pos : never)
        {
            c = good;
            c.pos = pos;
            holds(c, kBackNotHand, "R, N, the low range or H");
        }
        // a climb with the foot light is no climb
        c = good; c.pitchDeg = 10.0f; c.pedal = 0.2f;
        const bool lightClimb = run(10, c, 20.0f) > 0.0f;
        // standing with the foot off, A's gear anywhere
        c = good; c.autoGear = 1; c.speed = 0.0f; c.spin = 0.0f; c.pedal = 0.0f;
        const bool standing = run(10, c, 20.0f) > 0.0f;
        // a failure inside the steady second: the pedal down for one pass at 10.5 s, so the second starts over at 10.55
        bool steadyAgain = false;
        {
            BackModel m = {};
            float back = -1.0f;
            for (int i = 0; i * dt < 20.0f && back < 0.0f; i++)
            {
                BackInputs in = good;
                if (i == 210) in.pedal = 0.9f;
                if (BackToAStep(m, 10, i == 0, in, dt)) back = i * dt;
            }
            steadyAgain = back >= 11.4f && back <= 11.7f;
        }
        // a lock switched just as the timer runs out holds it kBackAfterSwitch
        bool switched = false;
        {
            BackModel m = {};
            float back = -1.0f;
            for (int i = 0; i * dt < 30.0f && back < 0.0f; i++)
            {
                BackInputs in = good;
                in.diffLock = i >= 200;
                if (BackToAStep(m, 10, i == 0, in, dt)) back = i * dt;
            }
            switched = back >= 13.8f && back <= 14.2f;
        }
        Check(byDefault && tenSteady && restarts && eachHolds && lightClimb && standing && steadyAgain && switched && BackToARange(1) == 3 && BackToARange(45) == 30 &&
                  BackToARange(-2) == 0 && BackToARange(12) == 12,
              which[0] ? which : "BackToA: never by default; after 10 s and a steady second, restarted by a shift by hand; each condition holds it, R, N, L and H never, "
                                 "nor a gear held; a failure in the second starts it over; a lock switched holds it 3 s; any gap standing with the foot off");
    }
    // Back to A floored (the held gear power brief): with A's gear the held one it returns after BackToA and a second,
    // the pedal at 1 as a key held gives it; with A's gear one off the pedal still holds it
    {
        const float dt = 0.05f;
        BackInputs in = {};
        in.pos = kPosGear;
        in.heldGear = in.autoGear = 4;
        in.pedal = 1.0f;
        in.grip = 1.2f;
        in.speed = 10.0f;
        in.spin = 12.0f;
        in.pitchKnown = in.engineOn = true;
        auto backAt = [&](const BackInputs &b) {
            BackModel m = {};
            for (int i = 0; i * dt < 20.0f; i++)
                if (BackToAStep(m, 5, i == 0, b, dt)) return i * dt;
            return -1.0f;
        };
        const float same = backAt(in);
        BackInputs off = in;
        off.autoGear = 5;
        BackModel m = {};
        for (int i = 0; i < 300; i++) BackToAStep(m, 5, i == 0, off, dt);
        Check(same >= 5.8f && same <= 6.15f && backAt(off) < 0.0f && m.why == kBackPedalDown,
              "BackToA: floored, or a key held, it returns when A's gear is the held one; with A's gear one off the pedal holds it");
    }
    // Sequential's own shifts (the held gear power brief, point 3): a held 3rd at the red zone 0.3 s goes up to 4th
    // through the gearbox's own up; a spike of one pass does nothing; nothing with RequireClutch, RequireGearHeld, BackToA
    // 0, the wheels spinning, the grip unknown, a gear from the game's shifter or a shift under way; a held 4th whose
    // gearbox input sinks under 1.1 times idle with the foot on goes down to 3rd after 0.5 s (by the input, whatever
    // the engine shows behind a slipping clutch)
    {
        const float dt = 0.05f;
        SeqInputs in = {};
        in.backToA = 10;
        in.pos = kPosGear;
        in.gear = 3;
        in.top = 6;
        // the input as ClutchStep has it: the wheels' share of the gear's top times the red zone's rpm; a floored held gear
        // settles near 0.95 of its top
        auto inputAt = [](float spin, float top, float redline) { return spin / top * redline; };
        in.redline = 3000.0f;
        in.input = inputAt(0.95f * 12.0f, 12.0f, in.redline);
        in.idle = 700.0f;
        in.pedal = 1.0f;
        in.gripKnown = in.engineOn = true;
        auto firstAt = [&](const SeqInputs &s, int want) {
            SeqModel m = {};
            for (int i = 0; i < 60; i++)
                if (SequentialStep(m, s, dt) == want) return i * dt;
            return -1.0f;
        };
        const float upAt = firstAt(in, 1);
        GearboxPosModel pm = {};
        GearboxTruck t = Truck(1);
        GearboxActions a = {};
        a.gearPressed = 3;
        Pass(pm, plain, a, t);
        Pass(pm, plain, Up(), t);
        const bool toFourth = t.gear == 4 && pm.pos == kPosGear && pm.gear == 4;
        bool spike = false;
        {
            SeqModel m = {};
            SeqInputs low = in;
            low.input = inputAt(0.8f * 12.0f, 12.0f, in.redline);
            for (int i = 0; i < 60; i++) spike = spike || SequentialStep(m, i % 5 == 0 ? in : low, dt) != 0;
        }
        bool none = true;
        SeqInputs c = in;
        c.requireClutch = true; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.requireGearHeld = true; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.backToA = 0; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.spinning = true; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.gripKnown = false; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.byShifter = true; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.busy = true; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.gear = 6; none = none && firstAt(c, 1) < 0.0f;
        c = in; c.input = inputAt(0.85f * 12.0f, 12.0f, in.redline); none = none && firstAt(c, 1) < 0.0f;
        SeqInputs slow = in;
        slow.gear = 4;
        slow.input = inputAt(0.2f * 12.0f, 12.0f, in.redline); // 600 rpm, under 1.1 times the idle's 700
        slow.pedal = 0.3f;
        slow.gripKnown = false; // a down-shift needs no grip
        const float downAt = firstAt(slow, -1);
        c = slow; c.byShifter = true; none = none && firstAt(c, -1) < 0.0f;
        // the foot off: the down still comes (braking with the clutch that works by itself never stalls), 0.2 s under the
        // brake; the up never, so a low gear held on a descent keeps its engine braking (the sequential to D brief)
        c = slow; c.pedal = 0.0f;
        const float footOffDown = firstAt(c, -1);
        c.braking = true;
        const float brakeDown = firstAt(c, -1);
        c = in; c.pedal = 0.0f; none = none && firstAt(c, 1) < 0.0f;
        // back to D: the position goes to A and D's gear is set in one shift, two gears down from a held 6th
        GearboxPosModel six = {};
        GearboxTruck st = Truck(1);
        GearboxActions g6 = {};
        g6.gearPressed = 6;
        Pass(six, plain, g6, st);
        GearboxActions toD = {};
        toD.a = true;
        const GearboxPosOrders o = Pass(six, plain, toD, st, 4);
        const bool backToD = six.pos == kPosA && st.gear == 4 && o.setGear && o.gear == 4;
        c = slow; c.gear = 1; none = none && firstAt(c, -1) < 0.0f;
        char line[200];
        snprintf(line, sizeof line, "sequential: up at the red zone after %.2f s with the foot on, never off; down before a stall after %.2f s, foot on or off, %.2f s "
                                    "braking; back to D in one shift; none where it may not", upAt, downAt, brakeDown);
        Check(upAt >= 0.25f && upAt <= 0.31f && toFourth && !spike && none && downAt >= 0.45f && downAt <= 0.51f && footOffDown >= 0.45f && footOffDown <= 0.51f &&
                  brakeDown >= 0.15f && brakeDown <= 0.21f && backToD,
              line);
    }
    // the gear keys to 16: Gear16 picks a 16-speed's 16th gear; Gear13 on a six-speed picks its top, as any key over it
    {
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        t.top = 16;
        GearboxActions a = {};
        a.gearPressed = 16;
        Pass(m, plain, a, t);
        const bool sixteenth = t.gear == 16 && m.pos == kPosGear && m.gear == 16;
        GearboxPosModel six = {};
        GearboxTruck u = Truck(1);
        a.gearPressed = 13;
        Pass(six, plain, a, u);
        Check(sixteenth && u.gear == 6 && six.gear == 6, "gearbox: Gear16 picks the 16th gear of a 16-speed; Gear13 on a six-speed picks its top");
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
    // WalkSkipsLow (BRIEF_sequential_no_low.md): up and down walk R, N, 1 and on, in each of the four combinations of
    // SkipNeutral and DownStaysForward; the jump into the 1st is an ordinary shift by hand (review R2)
    {
        bool walks = true, plainFirst = true;
        for (int c = 0; c < 4; c++)
        {
            const bool sn = (c & 1) != 0, dsf = (c & 2) != 0;
            const GearboxOptions o = { false, false, sn, dsf, true };
            GearboxActions r = {}, n = {}, g1 = {};
            r.r = true;
            n.n = true;
            g1.gearPressed = 1;
            auto press = [&](GearboxActions key, GearboxActions p, GearboxPosOrders *out) {
                GearboxPosModel m = {};
                GearboxTruck t = Truck(1);
                Pass(m, o, key, t);
                const GearboxPosOrders got = Pass(m, o, p, t);
                if (out) *out = got;
                return m;
            };
            GearboxPosOrders upR = {}, upN = {};
            const GearboxPosModel a = press(r, Up(), &upR), b = press(n, Up(), &upN), d = press(g1, Down(), nullptr), e = press(n, Down(), nullptr),
                                  f = press(r, Down(), nullptr);
            const bool upFromR = sn ? a.pos == kPosGear && a.gear == 1 : a.pos == kPosN;
            const bool upFromN = b.pos == kPosGear && b.gear == 1;
            const bool downFromFirst = dsf ? d.pos == kPosGear && d.gear == 1 : sn ? d.pos == kPosR : d.pos == kPosN;
            const bool downFromN = dsf ? e.pos == kPosN : e.pos == kPosR;
            const bool downFromR = f.pos == kPosR;
            // R2: neither the jump from N nor the one from R with SkipNeutral counts as a 1st from the low range
            const bool notFromLow = !upN.lowToFirst && !b.heldFromLow && (!sn || (!upR.lowToFirst && !a.heldFromLow));
            const bool all = upFromR && upFromN && downFromFirst && downFromN && downFromR && notFromLow;
            walks = walks && all;
            if (!all)
                printf("     WalkSkipsLow, SkipNeutral %d, DownStaysForward %d: up from R %d, up from N %d, down from 1 %d, from N %d, from R %d, not from low %d\n",
                       sn, dsf, upFromR, upFromN, downFromFirst, downFromN, downFromR, notFromLow);
        }
        // the option off: down from the 1st is L+ as before
        {
            GearboxPosModel m = {};
            GearboxTruck t = Truck(1);
            GearboxActions g1 = {};
            g1.gearPressed = 1;
            const GearboxOptions off = { false, false, false, false, false };
            Pass(m, off, g1, t);
            Pass(m, off, Down(), t);
            plainFirst = m.pos == kPosLowPlus;
        }
        Check(walks && plainFirst, "gearbox: WalkSkipsLow walks R, N, 1 past the low range with each mix of SkipNeutral and DownStaysForward; down from 1 stops there with DownStaysForward; the jump is no 1st from the low range; off, down from 1 is L+");
    }
    // WalkSkipsLow inside the low range: up and down step it as before, the hold on L+ kept, down from L- to N, R with
    // SkipNeutral, nothing with DownStaysForward
    {
        const GearboxOptions o = { false, false, false, false, true }, sn = { false, false, true, false, true }, dsf = { false, false, false, true, true };
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        GearboxActions lm = {};
        lm.lowMinus = true;
        Pass(m, o, lm, t);
        Pass(m, o, Up(), t);
        const bool toL = m.pos == kPosLow;
        Pass(m, o, Up(), t);
        const bool toLp = m.pos == kPosLowPlus;
        Pass(m, o, Up(), t);
        const bool lpHeld = m.pos == kPosLowPlus;
        Pass(m, o, None(), t, 1, 1.0f);
        const GearboxPosOrders out = Pass(m, o, Up(), t);
        const bool toFirst = m.pos == kPosGear && m.gear == 1 && out.lowToFirst;
        GearboxActions lp = {};
        lp.lowPlus = true;
        Pass(m, o, lp, t);
        Pass(m, o, Down(), t);
        const bool downL = m.pos == kPosLow;
        Pass(m, o, Down(), t);
        const bool downLm = m.pos == kPosLowMinus;
        Pass(m, o, Down(), t);
        const bool downN = m.pos == kPosN;
        GearboxPosModel k = {};
        GearboxTruck u = Truck(1);
        Pass(k, sn, lm, u);
        Pass(k, sn, Down(), u);
        const bool downR = k.pos == kPosR;
        GearboxPosModel q = {};
        GearboxTruck v = Truck(1);
        Pass(q, dsf, lm, v);
        Pass(q, dsf, Down(), v);
        const bool stays = q.pos == kPosLowMinus;
        Check(toL && toLp && lpHeld && toFirst && downL && downLm && downN && downR && stays,
              "gearbox: WalkSkipsLow inside the low range steps L-, L, L+ and on to 1 after the hold, down L+, L, L- and N (R with SkipNeutral, nothing with DownStaysForward)");
    }
    // WalkSkipsLow and A: down from D in the game's 1st or standing gives the 1st held, never N or R (review S1), in each
    // mix; down from D in the 3rd gives the 2nd; down from H gives the top
    {
        bool first = true, standing = true;
        for (int c = 0; c < 4; c++)
        {
            const GearboxOptions o = { false, false, (c & 1) != 0, (c & 2) != 0, true };
            GearboxActions d = {};
            d.a = true;
            GearboxPosModel m = {};
            GearboxTruck t = Truck(0);
            Pass(m, o, d, t, 1);
            Pass(m, o, Down(), t, 1);
            first = first && m.pos == kPosGear && m.gear == 1 && t.gear == 1;
            GearboxPosModel k = {};
            GearboxTruck u = Truck(0);
            u.speed = 0.0f;
            Pass(k, o, d, u, 1);
            const bool stood = u.gear == 0 && k.pos == kPosA;
            Pass(k, o, Down(), u, 1);
            standing = standing && stood && k.pos == kPosGear && k.gear == 1;
        }
        const GearboxOptions o = { false, false, false, false, true };
        GearboxActions d = {}, h = {};
        d.a = true;
        h.h = true;
        GearboxPosModel m = {};
        GearboxTruck t = Truck(0);
        Pass(m, o, d, t, 3);
        Pass(m, o, Down(), t, 3);
        const bool third = m.pos == kPosGear && m.gear == 2;
        GearboxPosModel k = {};
        GearboxTruck u = Truck(6);
        Pass(k, o, h, u);
        Pass(k, o, Down(), u);
        const bool fromH = k.pos == kPosGear && k.gear == 6;
        Check(first && standing && third && fromH, "gearbox: WalkSkipsLow, down from D in the 1st or standing gives the 1st held in each mix; from D in the 3rd the 2nd; from H the top");
    }
    // positions by gearbox (BRIEF_positions_by_gearbox.md): the walk for the masks H and L only, L only, none and all,
    // with Skip low range off and on and each mix of Skip neutral and Down stays forward, from R, N, L and the 1st, up
    // and down, against the row as a list: R, N unless Skip neutral, each low position the gearbox has (with Skip low
    // range only from inside the low range), then 1; Down stays forward refuses a down press onto N or R
    {
        const int masks[4] = { kHasLowPlus | kHasLowMinus, kHasLowPlus | kHasLowMinus | kHasH, kHasPositions, 0 };
        int failures = 0;
        for (int mi = 0; mi < 4; mi++)
            for (int c = 0; c < 8; c++)
            {
                const int miss = masks[mi];
                const GearboxOptions o = { false, false, (c & 1) != 0, (c & 2) != 0, (c & 4) != 0 };
                const int starts[4] = { kPosR, kPosN, kPosLow, kPosGear };
                for (int si = 0; si < 4; si++)
                {
                    const int start = starts[si];
                    if (start == kPosLow && (miss & kHasL)) continue;
                    for (int dir = -1; dir <= 1; dir += 2)
                    {
                        // the row as a list from this start
                        const bool inside = start == kPosLow;
                        int row[6], n = 0;
                        row[n++] = kPosR;
                        if (!o.skipNeutral) row[n++] = kPosN;
                        const int lows[3] = { kPosLowMinus, kPosLow, kPosLowPlus };
                        for (int l : lows)
                            if (!(miss & GearboxPosHasBit(l)) && (!o.walkSkipsLow || inside)) row[n++] = l;
                        row[n++] = kPosGear;
                        int k = 0;
                        while (k < n && row[k] != start) k++;
                        if (k == n) // a start the row skips (N with Skip neutral): its neighbours by the row's order
                        {
                            k = 0;
                            while (k < n && row[k] != kPosR) k++;
                        }
                        int want = start == kPosN && o.skipNeutral ? (dir > 0 ? row[1] : kPosR) : row[k + dir < 0 ? 0 : k + dir >= n ? n - 1 : k + dir];
                        if (start == kPosGear && dir > 0) want = kPosGear; // up from the 1st is the 2nd
                        if (dir < 0 && o.downStaysForward && (want == kPosN || want == kPosR)) want = start;
                        GearboxPosModel m = {};
                        GearboxTruck t = Truck(1);
                        t.missing = miss;
                        GearboxActions key = {};
                        if (start == kPosR) key.r = true;
                        else if (start == kPosN) key.n = true;
                        else if (start == kPosLow) key.low = true;
                        else key.gearPressed = 1;
                        Pass(m, o, key, t);
                        Pass(m, o, dir > 0 ? Up() : Down(), t);
                        const bool ok = m.pos == want && (want != kPosGear || m.gear == (start == kPosGear && dir > 0 ? 2 : 1));
                        if (!ok)
                        {
                            failures++;
                            printf("     positions: missing %d, SkipNeutral %d, DownStaysForward %d, WalkSkipsLow %d: %s from %s gives %s %d, the row %s\n", miss, o.skipNeutral,
                                   o.downStaysForward, o.walkSkipsLow, dir > 0 ? "up" : "down", GearboxPosName(start), GearboxPosName(m.pos), m.gear, GearboxPosName(want));
                        }
                    }
                }
            }
        Check(!failures, "gearbox: positions by gearbox, the walk for H and L only, L only, none and all, with Skip low range, Skip neutral and Down stays forward, as the row of the positions the gearbox has");
    }
    // the keys for a missing position do nothing and say so; Range low needs L; the game's shifter is taken and says the
    // read is wrong; up from L without L+ goes to the 1st at once, held
    {
        const GearboxOptions o = {};
        GearboxPosModel m = {};
        GearboxTruck t = Truck(1);
        t.missing = kHasLowPlus | kHasLowMinus | kHasH;
        GearboxActions g1 = {}, lm = {}, h = {}, rl = {}, l = {};
        g1.gearPressed = 1;
        lm.lowMinus = true;
        h.h = true;
        rl.rangeLow = true;
        l.low = true;
        Pass(m, o, g1, t);
        const GearboxPosOrders noLm = Pass(m, o, lm, t);
        const bool lmRefused = noLm.keyRefused && noLm.refusedPos == kPosLowMinus && !noLm.moved && m.pos == kPosGear;
        const GearboxPosOrders noH = Pass(m, o, h, t);
        const bool hRefused = noH.keyRefused && noH.refusedPos == kPosH && m.pos == kPosGear && t.gear == 1;
        Pass(m, o, l, t);
        const GearboxPosOrders up = Pass(m, o, Up(), t);
        const bool upFirst = m.pos == kPosGear && m.gear == 1 && up.lowToFirst && m.heldFromLow;
        GearboxPosModel k = {};
        GearboxTruck u = Truck(1);
        u.missing = kHasPositions;
        Pass(k, o, g1, u);
        const GearboxPosOrders noRange = Pass(k, o, rl, u);
        const bool rangeRefused = noRange.keyRefused && noRange.refusedPos == kPosLow && k.pos == kPosGear;
        GearboxPosModel q = {};
        GearboxTruck v = Truck(1);
        v.missing = kHasLowMinus;
        GearboxActions ask = {};
        GearboxActionsAsk(ask, GearboxAsk{ kAskLowMinus, 0 }, v.top);
        const GearboxPosOrders shifter = Pass(q, o, ask, v);
        const bool shifterTaken = shifter.shifterMissing && shifter.refusedPos == kPosLowMinus && q.pos == kPosLowMinus;
        const GearboxPosOrders after = Pass(q, o, None(), v);
        const bool shifterStays = q.pos == kPosLowMinus && !after.fitted; // the shifter wins: not bounced (review S4)
        Check(lmRefused && hRefused && upFirst && rangeRefused && shifterTaken && shifterStays,
              "gearbox: positions by gearbox, the L- and H keys of a gearbox without them do nothing and say so, Range low needs L, up from L without L+ is the 1st at once and held, "
              "the game shifter's L- is taken and says the read is wrong, and stays");
    }
    // standing in a position that turns missing: L- and L+ go to L where it is there, else the 1st held; H to the top
    {
        const GearboxOptions o = {};
        auto stand = [&](GearboxActions key, int miss, GearboxPosOrders &r, GearboxPosModel &m, GearboxTruck &t) {
            m = GearboxPosModel{};
            t = Truck(1);
            Pass(m, o, key, t);
            t.missing = miss;
            r = Pass(m, o, None(), t);
        };
        GearboxActions lm = {}, lp = {}, h = {};
        lm.lowMinus = true;
        lp.lowPlus = true;
        h.h = true;
        GearboxPosOrders r = {};
        GearboxPosModel m = {};
        GearboxTruck t = {};
        stand(lm, kHasLowMinus, r, m, t);
        const bool toL = r.fitted && r.fittedFrom == kPosLowMinus && m.pos == kPosLow && fabsf(t.power - 0.45f) < 0.01f;
        stand(lp, kHasLowPlus | kHasLowMinus, r, m, t);
        const bool plusToL = r.fitted && m.pos == kPosLow;
        stand(lm, kHasPositions, r, m, t);
        const bool toFirst = r.fitted && m.pos == kPosGear && m.gear == 1 && m.heldFromLow && fabsf(t.power - 1.0f) < 0.01f;
        stand(h, kHasH, r, m, t);
        const bool toTop = r.fitted && m.pos == kPosGear && m.gear == 6 && t.gear == 6;
        stand(lm, 0, r, m, t);
        const bool zeroKeeps = !r.fitted && m.pos == kPosLowMinus;
        Check(toL && plusToL && toFirst && toTop && zeroKeeps,
              "gearbox: positions by gearbox, a truck in L- or L+ the gearbox lacks goes to L, with no low range to the 1st held, in H the gearbox lacks to the top; mask zero keeps L-");
        // the fit under RequireClutch (the review of 108, S1): the pick with the clutch held, then the clutch up, then the
        // read lands; the fit is no shift of the player, so no stall standing and no N at speed; off, the same
        auto fit = [&](bool requireClutch, GearboxActions key, int miss, float speed, GearboxPosOrders &out, GearboxPosModel &mm) {
            GearboxOptions oc = {};
            oc.requireClutch = requireClutch;
            mm = GearboxPosModel{};
            GearboxTruck tt = Truck(1);
            tt.speed = speed;
            GearboxActions held = key;
            held.clutch = true;
            Pass(mm, oc, held, tt);
            Pass(mm, oc, None(), tt);
            Pass(mm, oc, None(), tt);
            tt.missing = miss;
            out = Pass(mm, oc, None(), tt);
        };
        bool fitsClean = true;
        for (int rc = 0; rc < 2; rc++)
        {
            GearboxPosOrders fo = {};
            GearboxPosModel fm = {};
            fit(rc != 0, lm, kHasLowMinus, 0.0f, fo, fm);
            fitsClean = fitsClean && fo.fitted && !fo.stall && fm.pos == kPosLow;
            fit(rc != 0, lm, kHasPositions, 0.0f, fo, fm);
            fitsClean = fitsClean && fo.fitted && !fo.stall && fm.pos == kPosGear && fm.gear == 1;
            fit(rc != 0, lp, kHasLowPlus | kHasLowMinus, 3.0f, fo, fm);
            fitsClean = fitsClean && fo.fitted && !fo.stall && fm.pos == kPosLow;
            fit(rc != 0, h, kHasH, 12.0f, fo, fm);
            fitsClean = fitsClean && fo.fitted && !fo.stall && fm.pos == kPosGear && fm.gear == 6;
        }
        Check(fitsClean, "gearbox: a fit of a position the gearbox lacks is not a shift without the clutch: with RequireClutch on or off no stall standing and no N at speed");
    }
    // the probe's words for the positions
    {
        char a[40], b[40], c[40], d[40];
        GearboxPosHasText(0, a, sizeof a);
        GearboxPosHasText(kHasLowPlus | kHasLowMinus, b, sizeof b);
        GearboxPosHasText(kHasPositions, c, sizeof c);
        GearboxPosHasText(kHasL | kHasLowPlus | kHasLowMinus, d, sizeof d);
        Check(!strcmp(a, "L-, L, L+ and H") && !strcmp(b, "L and H") && !strcmp(c, "none of L-, L, L+ and H") && !strcmp(d, "H"),
              "gearbox: the positions in words for the log and the probe");
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
        // true of the merge alone: whether an ask is a pick is decided after it (ShifterAskStep, the game's side below)
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
        GearboxPosStep(atSpeed, clutch, None(), ts, 1, 0.05f);
        const GearboxPosOrders grind = Pass(atSpeed, clutch, Up(), ts);
        const bool out = !grind.stall && atSpeed.pos == kPosN && ts.gear == 0 && atSpeed.stalls == 0 && grind.why[0];
        GearboxPosModel m = {};
        GearboxTruck t = Truck(2);
        t.speed = 0.5f * kGrindSpeed;
        GearboxPosStep(m, clutch, None(), t, 1, 0.05f);
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
        const GearboxPosOrders first = GearboxPosStep(m, plain, Up(), t, 1, 0.05f); // not taken: t stays in 2
        const bool sayFirst = GearboxPosRefused(m, first.gear);
        int asked = 0;
        for (int i = 0; i < 20; i++) asked += GearboxPosStep(m, plain, None(), t, 1, 0.05f).setGear;
        const bool sayAgain = GearboxPosRefused(m, first.gear);
        const GearboxPosOrders next = GearboxPosStep(m, plain, Up(), t, 1, 0.05f);
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
        GearboxPosStep(m, plain, a, t, 1, 0.05f);
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
                            twoOn.truckUpdate && GearboxHooksWanted(1, false, true).truckUpdate && GearboxHooksWanted(2, false, true).truckUpdate &&
                            !GearboxHooksWanted(0, true, true).truckUpdate && // ShiftOnGameThread: the truck update at 1 and 2 (review 17, step 1)
                            GearboxHooksWanted(2, false, false, true).truckUpdate && !GearboxHooksWanted(1, false, false, true).truckUpdate &&
                            !GearboxHooksWanted(0, false, false, true).truckUpdate; // PositionsByGearbox: the truck update at 2 (review M1)
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
        // the gearbox damage's kick (GearboxKicks, BRIEF_drivetrain_2_torque_cut.md): a bare ShiftGear(0) refused for the
        // held truck counts; a gear other than 0 and a neutral refused at its own door do not
        const long kicks0 = GearboxKicks();
        const bool kickRefused = !FakeShift(nullptr, 0);
        const long kicks1 = GearboxKicks();
        FakeShift(nullptr, 2);
        FakeNeutral(nullptr);
        const bool kicked = kickRefused && kicks1 == kicks0 + 1 && GearboxKicks() == kicks1 && g_ran[kFnShiftGear] == 3;
        GearboxPolicy(kPolicyPass, nullptr, false);
        const bool passAgain = FakeHigh(nullptr) && g_ran[kFnShiftToHigh] == 1;
        const bool passKick = FakeShift(nullptr, 0) && GearboxKicks() == kicks1;
        g_ran[kFnShiftGear] = g_ran[kFnShiftGear] - 1;
        GearboxUnhook();
        Check(kicked && passKick, "gearbox hooks: the game's own ShiftGear(0) refused for the held truck counts as the damage kick to N, once; another gear, a "
                                  "neutral refused at its own door and a ShiftGear(0) that passes do not");
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

    // the game's side (BRIEF_lock_press_shifts_to_a.md, M3): a shifter that keeps a position and sends it through the
    // doors each time its button is let go, moved or not. Through the hooks, the take, ShifterAskStep and the positions'
    // model, as gauges.cpp runs them. A pad and the keyboard share this one path (the asks are on for both); with a
    // wheel's pedals the asks are off. The rule before (every ask followed) runs as the counter-case.
    {
        FunctionFind finds[kFnCount] = {};
        void *fakes[kFnCount] = { (void *)&FakeShift, (void *)&FakeAuto, (void *)&FakeHigh, (void *)&FakeReverse, (void *)&FakeNeutral, nullptr,
                                  (void *)&FakeAutoOff, (void *)&FakePower, (void *)&FakeVehicle };
        for (int f = 0; f < kFnCount; f++)
            if (fakes[f]) finds[f] = FunctionFind{ kFnFound, (size_t)(uintptr_t)fakes[f], 1, false };
        char why[512];
        const bool hooked = GearboxHook(0, finds, why, sizeof why) == 8;
        GearboxAsk drop = {};
        GearboxAskTake(drop);
        GearboxPolicy(kPolicyAll, nullptr, false);
        GearboxShifterAsks(true);
        auto letGo = [](int kind, int gear) {
            switch (kind)
            {
            case kAskA: FakeAuto(nullptr); break;
            case kAskR: FakeReverse(nullptr); break;
            case kAskN: FakeNeutral(nullptr); break;
            case kAskH: FakeHigh(nullptr); break;
            case kAskLow: FakeAutoOff(nullptr, 1), FakeShift(nullptr, 1), FakePower(nullptr, 0.45f); break;
            case kAskLowMinus: FakeAutoOff(nullptr, 1), FakeShift(nullptr, 1), FakePower(nullptr, 0.2f); break;
            default: FakeAutoOff(nullptr, gear), FakeShift(nullptr, gear); break;
            }
        };
        GearboxPosModel m = {};
        GearboxTruck t = {};
        ShifterKept kept = {};
        int sideKind = kAskA, sideGear = 0; // where the game's shifter stands
        bool rule = true;
        const GearboxOptions o = {};
        auto take = [&]() {
            t = Truck(1);
            t.speed = 0.0f;
            t.automatic = true;
            GearboxPosTake(m, t);
            t.automatic = false;
            kept = ShifterKept{};
            kept.last = GearboxAskOfPos(m.pos, m.gear);
            sideKind = kAskA, sideGear = 0;
        };
        auto pass = [&](GearboxActions acts) {
            GearboxAsk a = {};
            if (GearboxAskTake(a))
            {
                if (rule) ShifterAskStep(kept, a, acts, t.top);
                else GearboxActionsAsk(acts, a, t.top);
            }
            Pass(m, o, acts, t);
        };
        // the button pressed and let go with nothing moved, as the lock on the same button does
        auto tap = [&]() { letGo(sideKind, sideGear), pass(None()), pass(None()); };
        auto pick = [&](int kind, int gear) { sideKind = kind, sideGear = gear, tap(); };
        auto bind = [&](int pos) {
            GearboxActions a = {};
            switch (pos)
            {
            case kPosA: a.a = true; break;
            case kPosN: a.n = true; break;
            case kPosR: a.r = true; break;
            case kPosH: a.h = true; break;
            case kPosLowMinus: a.lowMinus = true; break;
            case kPosLow: a.low = true; break;
            case kPosLowPlus: a.lowPlus = true; break;
            default: a.gearPressed = 3; break;
            }
            pass(a);
        };
        static const int places[] = { kPosLowPlus, kPosLow, kPosLowMinus, kPosGear, kPosR, kPosN, kPosH };
        // each place reached by its binding with the game's shifter at A, then three taps: how many stay, -1 a place not reached
        auto fault = [&](bool withRule) {
            rule = withRule;
            int stays = 0;
            for (int place : places)
            {
                take();
                if (m.pos != kPosA) return -2;
                bind(place);
                if (m.pos != place) return -1;
                tap(), tap(), tap();
                if (m.pos == place) stays++;
                else if (m.pos != kPosA) return -3;
            }
            return stays;
        };
        const int staysNow = fault(true), staysBefore = fault(false);
        if (staysNow != 7 || staysBefore != 0) printf("     the taps: %d of 7 stay with the rule, %d without\n", staysNow, staysBefore);
        Check(hooked && staysNow == 7 && staysBefore == 0,
              "game's shifter: with it at A and the mod's gearbox in L+, L, L-, a numbered gear, R, N and H by a binding, three taps of its button leave each "
              "position; with every ask followed, as before, each falls to A");
        // the repeat of L and of R: the shifter at L, the mod in L+ and in L- by a binding; the shifter at R, the mod in A
        rule = true;
        take();
        pick(kAskLow, 0);
        const bool toLow = m.pos == kPosLow;
        bind(kPosLowPlus), tap();
        const bool plusStays = m.pos == kPosLowPlus;
        bind(kPosLowMinus), tap();
        const bool minusStays = m.pos == kPosLowMinus && kept.last.kind == kAskLow; // the bindings left the kept ask alone
        pick(kAskR, 0);
        const bool toR = m.pos == kPosR;
        bind(kPosA), tap();
        const bool aStays = m.pos == kPosA;
        Check(toLow && plusStays && minusStays && toR && aStays,
              "game's shifter: at L a tap leaves the mod's L+ and L-, at R a tap leaves the mod's A; a binding does not change the kept ask");
        // the picks still work, each a new ask
        take();
        pick(kAskR, 0);
        const bool pR = m.pos == kPosR;
        pick(kAskA, 0);
        const bool pA = m.pos == kPosA;
        pick(kAskLow, 0);
        const bool pL = m.pos == kPosLow;
        pick(kAskLowMinus, 0);
        const bool pLm = m.pos == kPosLowMinus;
        pick(kAskGear, 4);
        const bool pG = m.pos == kPosGear && m.gear == 4;
        pick(kAskH, 0);
        const bool pH = m.pos == kPosH;
        pick(kAskN, 0);
        const bool pN = m.pos == kPosN;
        Check(pR && pA && pL && pLm && pG && pH && pN, "game's shifter: A to R, R to A, A to L, L to L-, a gear, H and N are each a new ask and are followed");
        // the cost, meant (the addendum's M2): the position the shifter already stands at cannot be picked on it again
        take();
        bind(kPosLowPlus);
        pick(kAskA, 0);
        const bool costA = m.pos == kPosLowPlus;
        take();
        pick(kAskR, 0);
        bind(kPosA);
        pick(kAskR, 0);
        const bool costR = m.pos == kPosA;
        pick(kAskN, 0), pick(kAskR, 0);
        const bool viaN = m.pos == kPosR;
        Check(costA && costR && viaN,
              "game's shifter, the cost and no miss: at A with the mod in L+, A picked on the shifter again changes nothing; the same for R from A, where N "
              "then R reaches reverse");
        // with a wheel's pedals the asks are off: nothing is taken
        GearboxShifterAsks(false);
        letGo(kAskA, 0);
        GearboxAsk none = {};
        const bool wheel = !GearboxAskTake(none);
        GearboxPolicy(kPolicyPass, nullptr, false);
        GearboxUnhook();
        // the rule alone: the first repeat of a kind told apart, the count after a minute, the kept ask of each position
        ShifterKept k = {};
        k.last = GearboxAskOfPos(kPosA, 0);
        GearboxActions acts = {};
        const bool first = ShifterAskStep(k, GearboxAsk{ kAskA, 0 }, acts, 6) == kShifterAgainFirst && !acts.a && !acts.shifter;
        const bool second = ShifterAskStep(k, GearboxAsk{ kAskA, 0 }, acts, 6) == kShifterAgain && k.again == 1;
        int early = 0; // passes of half a second: nothing for 119 of them, the count on the 120th, nothing after
        for (int i = 0; i < 119; i++) early += ShifterAgainTold(k, 0.5f);
        const bool told = early == 0 && ShifterAgainTold(k, 0.5f) == 1 && ShifterAgainTold(k, 0.5f) == 0 && k.again == 0;
        const bool picked = ShifterAskStep(k, GearboxAsk{ kAskGear, 2 }, acts, 6) == kShifterPick && acts.shifter && k.last.kind == kAskGear && k.last.gear == 2;
        const bool otherGear = ShifterAskStep(k, GearboxAsk{ kAskGear, 3 }, acts, 6) == kShifterPick && ShifterAskStep(k, GearboxAsk{ kAskGear, 3 }, acts, 6) == kShifterAgainFirst;
        const bool ofPos = GearboxAskOfPos(kPosLowPlus, 0).kind == kAskGear && GearboxAskOfPos(kPosLowPlus, 0).gear == 1 && GearboxAskOfPos(kPosGear, 5).gear == 5 &&
                           GearboxAskOfPos(kPosLow, 0).kind == kAskLow && GearboxAskOfPos(kPosLowMinus, 0).kind == kAskLowMinus && GearboxAskOfPos(kPosR, 0).kind == kAskR &&
                           GearboxAskOfPos(kPosN, 0).kind == kAskN && GearboxAskOfPos(kPosH, 0).kind == kAskH;
        ShifterKept gone = {};
        GearboxActions actsGone = {};
        const bool letGoFollows = ShifterAskStep(gone, GearboxAsk{ kAskA, 0 }, actsGone, 6) == kShifterPick; // nothing kept: any ask is a pick
        Check(wheel && first && second && told && picked && otherGear && ofPos && letGoFollows,
              "game's shifter: the first repeat of a position is told apart and the count comes a minute after; another gear is a pick; the kept ask of each "
              "position (L+ the first gear); with the asks off nothing is taken");
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

    // at Gearbox 0 and 1 the mod's orders go out only in A and a gear only forward (0.18.2: no N or R from the mod's
    // thread there)
    // the creep only where an automatic picks the gear (BRIEF_manual_creep_and_hold.md): the mod's A, its reverse and range
    // included; the game's automatic up; nothing in a gear picked by hand on either shifter, an R or L included, and a
    // shift from A into a gear picked by hand drops it at once
    {
        bool hand = true;
        const int byHand[] = { kPosGear, kPosN, kPosR, kPosH, kPosLowMinus, kPosLow, kPosLowPlus };
        for (int pos : byHand) hand &= !CreepAutoPicks(true, true, pos, 1);
        const bool modA = CreepAutoPicks(true, true, kPosA, 0) && CreepAutoPicks(true, true, kPosA, 1) && !CreepAutoPicks(true, false, kPosA, 1);
        const bool gameAuto = CreepAutoPicks(false, false, kPosA, 1);
        // the game's shifter in a numbered gear, in L or an R picked by hand: its automatic is off
        const bool gameHand = !CreepAutoPicks(false, false, kPosA, 0) && !CreepAutoPicks(false, true, kPosR, 0);
        const bool dropped = CreepAutoPicks(true, true, kPosA, 0) && !CreepAutoPicks(true, true, kPosGear, 0);
        Check(hand && modA && gameAuto && gameHand && dropped, "creep gate: the mod's A creeps, the game's automatic creeps; no position picked by hand on the mod's gearbox, "
                                                               "nor the game's shifter with its automatic off (a numbered gear, L, an R), and a shift from A into a hand-picked gear drops it");
    }
    Check(GearboxOwnOrders(kPosA, 3, true) && GearboxOwnOrders(kPosA, 0, false) && !GearboxOwnOrders(kPosA, 0, true) && !GearboxOwnOrders(kPosA, -1, true) &&
              !GearboxOwnOrders(kPosN, 0, true) && !GearboxOwnOrders(kPosR, -1, true) && !GearboxOwnOrders(kPosLowMinus, 1, false),
          "gearbox (0.18.2): at Gearbox 0 and 1 the mod's orders only in A, its shifts only into a forward gear");
    // the positions that never stall (BRIEF_tester_1114.md, part 189): the first gear and L-, L and L+; the second gear up,
    // A, N, R and H as today
    Check(StallSpared(kPosGear, 1) && !StallSpared(kPosGear, 2) && !StallSpared(kPosGear, 5) && StallSpared(kPosLowMinus, 1) && StallSpared(kPosLow, 1) &&
              StallSpared(kPosLowPlus, 1) && !StallSpared(kPosA, 1) && !StallSpared(kPosN, 0) && !StallSpared(kPosR, -1) && !StallSpared(kPosH, 1),
          "gearbox: the first gear and the low range never stall, the second gear and up, A, N, R and H as before");

    printf(g_failed ? "GEARBOX TEST FAILED: %d\n" : "GEARBOX TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
