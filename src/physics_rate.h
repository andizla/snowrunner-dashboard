// The physics rate test mode, stage 1 (BRIEF_physics_rate_stage1.md with the Reviewer's addendum): the game's physics
// stepped 120 times a second in place of 60. A test, off by default, read once at the start. Pure: the plan is made on
// a copy of the exe, the writes go through the caller's read and write, the halving works on a block handed in.
//
// At PhysicsRate=60 nothing here is called but RateOf and IterationsOf.
//
// What the game does a step is found by pattern and the operand of each place is pointed at a float of the mod's own
// (one page within 2 GB of the exe, filled once, then read only). The game's own floats are never written: the 1/60
// alone has 20 other readers. The solver's iteration count is an immediate in the code and is written there.
//
// The idea of a higher physics rate, and the first map of what has to be scaled, come from True 240Hz Physics Patch by
// alounana on mod.io. No bytes, figures or text of that patch are here.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "game_code.h"

static const int kRateStock = 60, kRateFast = 120;
// PhysicsRate as it reads: 60 or 120, anything else 60 with known false (one log line).
inline int RateOf(int asked, bool &known)
{
    known = asked == kRateStock || asked == kRateFast;
    return asked == kRateFast ? kRateFast : kRateStock;
}
// PhysicsIterations as it reads at 120: 0 is the mod's choice (2), 2 and 4 themselves, anything else 2 with known false.
static const int kRateIterStock = 4, kRateIterFast = 2;
inline int IterationsOf(int asked, bool &known)
{
    known = asked == 0 || asked == kRateIterFast || asked == kRateIterStock;
    return asked == kRateIterStock ? kRateIterStock : kRateIterFast;
}

// The places whose operand is a float read from the exe's data. operand: the disp32's place from the match; end: where
// the instruction ends (the disp32 counts from there). stock: the bits the game's float holds. The order is the order
// in the mod's page; the step stands first and is written last.
struct RateSite
{
    const char *name, *pattern;
    int operand, end;
    uint32_t stock, wanted;
};
enum { kRateStep, kRateWheel, kRateChassis, kRateBrake, kRateReadOld, kRateReadNew, kRateFloats };
// The readout at w + 0x16C blends 0.6 of the old with 0.4 of the new a step. Two steps of a blend with old share q make
// q x q, so q is the root of 0.6: 0.7745967 (3F464BF8), and the new share the rest, 0.2254033 (3E66D020).
static const RateSite kRateSites[kRateFloats] = {
    { "the step", "84 C0 75 49 F3 0F 10 35 ? ? ? ? 66 66 0F 1F 84 00 00 00 00 00 49 8B 4F 20 80 3D ? ? ? ? 00", 8, 12, 0x3C888889, 0x3C088889 },
    { "the drive's wheel impulse", "F3 44 0F 59 15 ? ? ? ? 41 0F 28 DA 0F C6 DB 00", 5, 9, 0x3C888889, 0x3C088889 },
    { "the drive's chassis push", "41 0F 28 DA F3 0F 59 9F 28 01 00 00 F3 0F 59 1D ? ? ? ?", 16, 20, 0x3C088889, 0x3B888889 },
    { "the overspeed brake", "F3 0F 59 C0 83 7E 74 00 7D 07 0F 57 05 ? ? ? ? F3 0F 59 05 ? ? ? ? F3 0F 59 05 ? ? ? ?", 29, 33, 0xBC888889, 0xBC088889 },
    { "the readout's old share", "F3 0F 10 83 6C 01 00 00 F3 0F 59 05 ? ? ? ? F3 0F 59 0D ? ? ? ?", 12, 16, 0x3F19999A, 0x3F464BF8 },
    { "the readout's new share", "F3 0F 10 83 6C 01 00 00 F3 0F 59 05 ? ? ? ? F3 0F 59 0D ? ? ? ?", 20, 24, 0x3ECCCCCD, 0x3E66D020 },
};
// The solver's iterations: the last of three stores into the solver's set-up (0.6, 1.0, then 4 into + 0x90) before the
// return. The 0.6 and the 1.0 tell it from the two shapes beside it. The immediate stands kRateIterAt into the match.
static const char *const kRateIterPattern = "C7 81 88 00 00 00 9A 99 19 3F C7 81 8C 00 00 00 00 00 80 3F C7 81 90 00 00 00 04 00 00 00 C3";
static const int kRateIterAt = 26;
// The physics world's owner: its constructor asks a small function for the static it hangs itself on. The world is
// the owner's + 0x20, the world's step seconds its + 0x2A8 and its solver's iterations its + 0x3DC (the constructor
// copies the set-up's + 0x90 there).
static const char *const kRateOwnerPattern = "E8 ? ? ? ? 48 89 30 48 8D 05 ? ? ? ? 48 89 06 33 DB 48 89 5E 20 48 89 5E 28 48 89 5E 30";
static const uint64_t kRateOwnerWorld = 0x20, kRateWorldDtAt = 0x2A8, kRateWorldIterAt = 0x3DC;
static const uint32_t kRateDtBits = 0x3C088889; // 1/120 as the step holds it

// Stage 2, part A (BRIEF_physics_rate_stage2.md with the Reviewer's addendum): the substance action's lift and drag, in
// water and in mud alike. The action builds two vectors a step, the lift with the drag and the drag on the spin's side,
// multiplies each by four 60s loaded once into a register, and hands each to the body's motion as an impulse at a point
// (the motion's applyPointImpulse). Nothing in the two vectors holds the step's seconds: the speeds in them are a move
// over 1/60 s times 60, a pair that is left as it is (M1). So the amount is one a step, and at 120 steps the four 60s
// are 30. The load is a movaps, so its operand is pointed at four floats of the mod's page that stand 16 bytes aligned.
// The pattern is the load and what follows to the first multiply: the lift put into the vector's second place.
static const char *const kRateSubstanceName = "the substance's lift and drag";
static const char *const kRateSubstancePattern = "44 0F 28 35 ? ? ? ? 0F 28 45 40 0F C6 C0 E1 F3 0F 10 C1 0F C6 C0 E1 41 0F 59 C6";
static const int kRateSubstanceOperand = 4, kRateSubstanceEnd = 8;
static const uint32_t kRateSubstanceStock = 0x42700000, kRateSubstanceWanted = 0x41F00000; // 60 and 30
// Stage 2, part B1 (addendum 2, M1): the same four 60s have two more readers, the action's two multiplies for a wheel.
// Each is followed by the body's activate and the motion's impulse call with one vector, so each is an impulse a step
// as the chassis's is. With the load alone corrected the chassis got the right lift and drag at 120 and each wheel twice
// it. All three or none.
struct RateQuad
{
    const char *name, *pattern;
    int operand, end;
};
enum { kRateQuadLoad, kRateQuadWheelFirst, kRateQuadWheelSecond, kRateQuads };
static const RateQuad kRateQuadSites[kRateQuads] = {
    { kRateSubstanceName, kRateSubstancePattern, kRateSubstanceOperand, kRateSubstanceEnd },
    { "the substance's first impulse on a wheel", "44 0F 28 5C 24 40 44 0F 59 1D ? ? ? ? 44 0F 29 5C 24 40 48 8B 5F 18 48 8B CB E8", 10, 14 },
    { "the substance's second impulse on a wheel", "0F 28 44 24 30 0F 59 05 ? ? ? ? 0F 29 44 24 30 48 8B 5F 18 48 8B CB E8", 8, 12 },
};
static const int kRateSubstanceWord = 8; // where the four stand on the mod's page, in floats: 32 bytes in

// Stage 2, part B2 (addendum 2, M3 and M4): a second group of operands, all or nothing in itself. Refused, 120 runs
// with the first group alone and the start's line and the tab say so.
// The two blends with constant weights. A blend that keeps q of the old a step keeps q x q over two, so at 120 the
// old's weight is the root of the game's and the new's is 1 less that: each pair sums to 1 to the bit.
//   w + 0x118: the old's 0.3 becomes 0.5477226 (3F0C378C), the new's 0.7 becomes 0.4522774 (3EE790E8)
//   w + 0x110: the old's 0.8 becomes 0.8944272 (3F64F92E), the new's 0.2 becomes 0.1055728 (3DD83690)
// The game's own constants are shared with other code and are not written. w + 0x128 is left, both weights: its old
// weight comes in a register. w + 0x170 is left: a mean of two steps.
// The fixed 1/60 force, in two places with the same code: a body that lies under a surface the action asks for has its
// linear and its angular velocity set to zero that step and is then pushed up by a force (its depth over a range, at
// most 1, times a gain, at most 10 for each kilogram of its mass) for a fixed 1/60 s. At 120 steps the time is 1/120 s,
// so the push stays the one that belongs to the step's gravity. Both places or neither. Not the substance action's own
// 1/60 beside its 60 (the first addendum's M1): that pair stays and no pattern here matches it.
enum { kRate2SpeedNew, kRate2SpeedOld, kRate2ShareOld, kRate2ShareNew, kRate2Force, kRate2ForceTwin, kRate2Sites };
static const char *const kRate2SpeedPattern = "F3 0F 5C 88 14 01 00 00 F3 0F 11 80 14 01 00 00 F3 0F 59 0D ? ? ? ? F3 0F 10 80 18 01 00 00 F3 0F 59 05 ? ? ? ?";
static const char *const kRate2SharePattern = "F3 0F 10 8A 10 01 00 00 F3 0F 59 0D ? ? ? ? F3 0F 59 05 ? ? ? ? F3 0F 58 C8 F3 0F 11 8A 10 01 00 00";
static const RateSite kRate2[kRate2Sites] = {
    { "the blend at w + 0x118, the new's weight", kRate2SpeedPattern, 20, 24, 0x3F333333, 0x3EE790E8 },
    { "the blend at w + 0x118, the old's weight", kRate2SpeedPattern, 36, 40, 0x3E99999A, 0x3F0C378C },
    { "the blend at w + 0x110, the old's weight", kRate2SharePattern, 12, 16, 0x3F4CCCCD, 0x3F64F92E },
    { "the blend at w + 0x110, the new's weight", kRate2SharePattern, 20, 24, 0x3E4CCCCD, 0x3DD83690 },
    { "the fixed 1/60 force", "48 8B 06 4C 8D 44 24 40 F3 0F 10 0D ? ? ? ? 48 8B CE FF 90 D8 00 00 00", 12, 16, 0x3C888889, 0x3C088889 },
    { "the fixed 1/60 force's twin", "48 8B 07 4C 8D 44 24 40 F3 0F 10 0D ? ? ? ? 48 8B CF FF 90 D8 00 00 00", 12, 16, 0x3C888889, 0x3C088889 },
};
static const int kRate2Word = kRateSubstanceWord + 4; // the second group's floats on the mod's page, after the four 30s
static const int kRatePageWords = kRate2Word + kRate2Sites;

// The page's floats, in the sites' order.
inline void RatePageFill(uint32_t out[kRatePageWords])
{
    for (int i = 0; i < kRatePageWords; i++) out[i] = 0;
    for (int i = 0; i < kRateFloats; i++) out[i] = kRateSites[i].wanted;
    for (int i = 0; i < 4; i++) out[kRateSubstanceWord + i] = kRateSubstanceWanted;
    for (int i = 0; i < kRate2Sites; i++) out[kRate2Word + i] = kRate2[i].wanted;
}

// One write of four bytes into the exe's code: where (from the exe's base), what stands there and what goes there.
struct RateWrite
{
    size_t at;
    uint32_t stock, value;
    const char *name;
};
static const int kRateWritesMax = kRateFloats + 1 + kRateQuads;
struct RatePlan
{
    bool ok;
    char why[160];            // the place that failed, for the log
    size_t match[kRateFloats]; // each float place's match
    size_t iterAt;            // the iterations' immediate
    size_t substanceAt;       // the substance action's load of the four 60s
    size_t quadAt[kRateQuads]; // the three readers of the four 60s: the load (substanceAt) and the wheels' two multiplies
    size_t ownerSlot;         // the owner's static
    RateWrite writes[kRateWritesMax]; // in the order they go in: the step last
    int count;
    bool foreignStep;         // the plan failed on the step's own float: it does not hold the game's 1/60 (another mod has changed the step)
};

inline bool RateFail(RatePlan &p, const char *name, const char *what)
{
    snprintf(p.why, sizeof p.why, "%s: %s", name, what);
    p.ok = false;
    p.count = 0;
    return false;
}

// The plan on a copy of the whole exe (image, size bytes, at its own offsets; the code from..to). base: where the exe
// stands; page: where the mod's floats stand, or 0 for a look that only checks (no write is planned then).
// iterations: 2 or 4 (at 4 the immediate is checked and left). All or nothing: each pattern exactly once, each float
// pointed at the stock one, the immediate 4, each new operand within reach, or ok is false and why names the place.
inline bool RatePlanMake(const uint8_t *image, size_t size, size_t from, size_t to, uint64_t base, uint64_t page, int iterations, RatePlan &p)
{
    p = RatePlan{};
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    if (to > size || from >= to) return RateFail(p, "the exe", "no code to look through");
    RateWrite floats[kRateFloats];
    for (int i = 0; i < kRateFloats; i++)
    {
        const RateSite &s = kRateSites[i];
        const int length = PatternParse(s.pattern, bytes, mask);
        size_t at = 0;
        int count = 0;
        if (!length || !PatternFind(image, from, to, bytes, mask, length, at, count) || count != 1)
            return RateFail(p, s.name, count > 1 ? "its code stands more than once" : "its code is not found");
        int32_t disp = 0;
        memcpy(&disp, image + at + s.operand, 4);
        const int64_t target = (int64_t)at + s.end + disp;
        if (target < 0 || (uint64_t)target + 4 > size) return RateFail(p, s.name, "its operand points outside the exe");
        uint32_t held = 0;
        memcpy(&held, image + target, 4);
        if (held != s.stock)
        {
            // what it holds, and at the step's own place the seconds: there only another mod's write explains it (BRIEF_step_check_always.md, S3)
            char what[120];
            float seconds;
            memcpy(&seconds, &held, 4);
            if (i == kRateStep) snprintf(what, sizeof what, "the float it reads is not the game's own, it holds %08x, %.6f s: another mod has changed the step", held, isfinite(seconds) ? seconds : 0.0f);
            else snprintf(what, sizeof what, "the float it reads is not the game's own, it holds %08x", held);
            RateFail(p, s.name, what);
            p.foreignStep = i == kRateStep;
            return false;
        }
        const int64_t reach = (int64_t)(page + 4 * (uint64_t)i) - (int64_t)(base + at + s.end);
        if (page && (reach > 0x7FFFFFFFll || reach < -0x80000000ll)) return RateFail(p, s.name, "the mod's page is out of its reach");
        p.match[i] = at;
        floats[i] = RateWrite{ at + s.operand, (uint32_t)disp, (uint32_t)(int32_t)reach, s.name };
    }
    // the three readers of the game's four 60s: the action's load for the chassis and its two multiplies for a wheel
    // (stage 2, part A and B1). Each once, each at the same four 60s, each new operand 16 bytes aligned and in reach
    RateWrite quads[kRateQuads] = {};
    int64_t fourAt = 0;
    for (int q = 0; q < kRateQuads; q++)
    {
        const RateQuad &s = kRateQuadSites[q];
        const int length = PatternParse(s.pattern, bytes, mask);
        size_t at = 0;
        int count = 0;
        if (!length || !PatternFind(image, from, to, bytes, mask, length, at, count) || count != 1)
            return RateFail(p, s.name, count > 1 ? "its code stands more than once" : "its code is not found");
        int32_t disp = 0;
        memcpy(&disp, image + at + s.operand, 4);
        const int64_t target = (int64_t)at + s.end + disp;
        if (target < 0 || (uint64_t)target + 16 > size) return RateFail(p, s.name, "its operand points outside the exe");
        uint32_t held[4];
        memcpy(held, image + target, 16);
        // all four: a single 60 with other figures after it is another constant (the action's own 60 for its speeds is one)
        if (held[0] != kRateSubstanceStock || held[1] != kRateSubstanceStock || held[2] != kRateSubstanceStock || held[3] != kRateSubstanceStock)
        {
            char what[120];
            snprintf(what, sizeof what, "the four floats it reads are not the game's four 60s, they hold %08x %08x %08x %08x", held[0], held[1], held[2], held[3]);
            return RateFail(p, s.name, what);
        }
        // the wheels' two read the very four the load reads, not four 60s somewhere else (addendum 2, M1)
        if (q == 0) fourAt = target;
        else if (target != fourAt) return RateFail(p, s.name, "it does not read the four 60s the substance action's load reads");
        const uint64_t slot = page + 4 * (uint64_t)kRateSubstanceWord;
        const int64_t reach = (int64_t)slot - (int64_t)(base + at + s.end);
        if (page && (slot & 15)) return RateFail(p, s.name, "the mod's page does not stand 16 bytes aligned");
        if (page && (reach > 0x7FFFFFFFll || reach < -0x80000000ll)) return RateFail(p, s.name, "the mod's page is out of its reach");
        p.quadAt[q] = at;
        quads[q] = RateWrite{ at + s.operand, (uint32_t)disp, (uint32_t)(int32_t)reach, s.name };
    }
    p.substanceAt = p.quadAt[kRateQuadLoad];
    {
        const int length = PatternParse(kRateIterPattern, bytes, mask);
        size_t at = 0;
        int count = 0;
        if (!length || !PatternFind(image, from, to, bytes, mask, length, at, count) || count != 1)
            return RateFail(p, "the solver's iterations", count > 1 ? "its code stands more than once" : "its code is not found");
        p.iterAt = at + kRateIterAt;
    }
    {
        const int length = PatternParse(kRateOwnerPattern, bytes, mask);
        size_t at = 0;
        int count = 0;
        if (!length || !PatternFind(image, from, to, bytes, mask, length, at, count) || count != 1)
            return RateFail(p, "the world's owner", count > 1 ? "its code stands more than once" : "its code is not found");
        int32_t rel = 0;
        memcpy(&rel, image + at + 1, 4);
        const int64_t getter = (int64_t)at + 5 + rel;
        // the small function: lea rax, [rip + disp32]; ret
        if (getter < (int64_t)from || (uint64_t)getter + 8 > to || image[getter] != 0x48 || image[getter + 1] != 0x8D || image[getter + 2] != 0x05 || image[getter + 7] != 0xC3)
            return RateFail(p, "the world's owner", "the function that gives its static is not the one known");
        memcpy(&rel, image + getter + 3, 4);
        const int64_t slot = getter + 7 + rel;
        if (slot < (int64_t)to || (uint64_t)slot + 8 > size) return RateFail(p, "the world's owner", "its static lies outside the exe's data");
        p.ownerSlot = (size_t)slot;
    }
    p.ok = true;
    if (!page) return true;
    // the order: the drive's three, the readout's two, the substance's three readers, the iterations, the step last
    for (int i = 1; i < kRateFloats; i++) p.writes[p.count++] = floats[i];
    for (int q = 0; q < kRateQuads; q++) p.writes[p.count++] = quads[q];
    if (iterations == kRateIterFast) p.writes[p.count++] = RateWrite{ p.iterAt, (uint32_t)kRateIterStock, (uint32_t)kRateIterFast, "the solver's iterations" };
    p.writes[p.count++] = floats[kRateStep];
    return true;
}

// The second group's plan (stage 2, part B2), on the same copy and with the same page: each of its six places once,
// each at the game's own float, each new operand in reach. One missing, twice or at another float: no plan and no
// write of the group, the place named. The first group is not touched by it either way.
struct RatePlan2
{
    bool ok;
    char why[160];
    size_t match[kRate2Sites];
    RateWrite writes[kRate2Sites];
    int count;
};
inline bool Rate2PlanMake(const uint8_t *image, size_t size, size_t from, size_t to, uint64_t base, uint64_t page, RatePlan2 &p)
{
    p = RatePlan2{};
    const auto fail = [&p](const char *name, const char *what) {
        snprintf(p.why, sizeof p.why, "%s: %s", name, what);
        p.ok = false;
        p.count = 0;
        return false;
    };
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    if (to > size || from >= to) return fail("the exe", "no code to look through");
    RateWrite writes[kRate2Sites];
    for (int i = 0; i < kRate2Sites; i++)
    {
        const RateSite &s = kRate2[i];
        const int length = PatternParse(s.pattern, bytes, mask);
        size_t at = 0;
        int count = 0;
        if (!length || !PatternFind(image, from, to, bytes, mask, length, at, count) || count != 1)
            return fail(s.name, count > 1 ? "its code stands more than once" : "its code is not found");
        int32_t disp = 0;
        memcpy(&disp, image + at + s.operand, 4);
        const int64_t target = (int64_t)at + s.end + disp;
        if (target < 0 || (uint64_t)target + 4 > size) return fail(s.name, "its operand points outside the exe");
        uint32_t held = 0;
        memcpy(&held, image + target, 4);
        if (held != s.stock)
        {
            char what[120];
            snprintf(what, sizeof what, "the float it reads is not the game's own, it holds %08x", held);
            return fail(s.name, what);
        }
        const int64_t reach = (int64_t)(page + 4 * (uint64_t)(kRate2Word + i)) - (int64_t)(base + at + s.end);
        if (page && (reach > 0x7FFFFFFFll || reach < -0x80000000ll)) return fail(s.name, "the mod's page is out of its reach");
        p.match[i] = at;
        writes[i] = RateWrite{ at + s.operand, (uint32_t)disp, (uint32_t)(int32_t)reach, s.name };
    }
    p.ok = true;
    if (!page) return true;
    for (int i = 0; i < kRate2Sites; i++) p.writes[p.count++] = writes[i];
    return true;
}

// ---- the brakes (stage 2, part B2; addendum 2, M2) ----
// The game's brakes take a share of each wheel's spin a step: the wheel body's angular velocity times (1 - brake),
// set once a call, with no step's seconds in it. Two steps of that leave (1 - brake) squared, so at 120 the factor is
// the root of (1 - brake). The share is the input and no constant, so there is no operand to point elsewhere. The hook
// on the brakes' function reads each wheel body's angular velocity before the call and after it. Only where the
// velocity after is the velocity before times (1 - brake) on all three parts, to the bit, is it written as before
// times the root. Anything else is left: the lock's zero vector, a wheel the function did not touch, a brake at or
// under the game's 0.0001, a figure that is no number. The brake is the one the function used: the first of its
// delayed inputs (the action's + 0x78), read after the call. The input itself is not changed: it also sets the
// lock's threshold.
static const int kBrakeWheels = 32;
static const float kBrakeLeast = 0.0001f;
inline uint32_t BrakeBits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}
// True with out to write where after is the game's braked before; false leaves the wheel as it is.
inline bool BrakeRootOf(const float before[3], const float after[3], float brake, float out[3])
{
    if (!isfinite(brake) || !(brake > kBrakeLeast) || !(brake < 1.0f)) return false;
    const float share = 1.0f - brake;
    for (int i = 0; i < 3; i++)
    {
        if (!isfinite(before[i])) return false;
        const float game = before[i] * share;
        if (BrakeBits(game) != BrakeBits(after[i])) return false;
    }
    const float root = sqrtf(share);
    bool moved = false;
    for (int i = 0; i < 3; i++)
    {
        out[i] = before[i] * root;
        moved = moved || BrakeBits(out[i]) != BrakeBits(after[i]);
    }
    return moved;
}

// The writes put in. read(at, value) and write(at, value) take a place from the exe's base and four bytes; write
// changes the protection, puts it back and flushes the instruction cache (the caller's), read is a plain read.
// First every place is read: one that does not hold its stock bytes means nothing is written. Then each write is read
// back. One that differs after an earlier one went in: failed names it, and every one so far is put back to stock and
// read back again (restored says whether all of them hold stock again).
struct RateApplied
{
    bool ok;
    int failed;    // the write that failed, -1 none
    int done;      // how many went in and held
    bool written;  // false: nothing was written at all
    bool restored; // after a failure: every one written holds stock again
};
template <class Read, class Write> inline RateApplied RateApply(const RateWrite *w, int n, Read read, Write write)
{
    RateApplied r = { false, -1, 0, false, true };
    for (int i = 0; i < n; i++)
    {
        uint32_t held = 0;
        if (!read(w[i].at, held) || held != w[i].stock)
        {
            r.failed = i;
            return r;
        }
    }
    for (int i = 0; i < n; i++)
    {
        uint32_t held = 0;
        const bool wrote = write(w[i].at, w[i].value);
        r.written = true;
        if (wrote && read(w[i].at, held) && held == w[i].value)
        {
            r.done++;
            continue;
        }
        r.failed = i;
        for (int j = i; j >= 0; j--)
        {
            const bool back = write(w[j].at, w[j].stock);
            if (!read(w[j].at, held) || held != w[j].stock) r.restored = false;
            (void)back;
        }
        return r;
    }
    r.ok = true;
    return r;
}

// ---- the drive's two figures a step, halved around the call (the Reviewer's M1) ----
// The action's MaxDeltaAngVel (+ 0x4C) and EngineResponsiveness (+ 0xC4) are amounts a step and are read only inside
// the drive. No memory: before the call each is read; a finite number over 0 is written halved (exact in a float).
// After the call the stock value goes back only where the field still holds the half to the bit: anything else is the
// game's own write during the call and stays. Between steps the game's values are stock.
static const size_t kRateActionFields[2] = { 0x4C, 0xC4 };
struct RateHalved
{
    uint32_t was[2], half[2];
    bool on[2];
};
inline bool RateHalfOf(uint32_t bits, uint32_t &half)
{
    float f;
    memcpy(&f, &bits, 4);
    if (!isfinite(f) || !(f > 0.0f)) return false;
    const float h = f * 0.5f;
    memcpy(&half, &h, 4);
    return half != bits;
}
inline void RateHalveBefore(void *action, RateHalved &h)
{
    for (int i = 0; i < 2; i++)
    {
        volatile uint32_t *field = (volatile uint32_t *)((uint8_t *)action + kRateActionFields[i]);
        h.was[i] = *field;
        h.on[i] = RateHalfOf(h.was[i], h.half[i]);
        if (h.on[i]) *field = h.half[i];
    }
}
inline void RateHalveAfter(void *action, const RateHalved &h)
{
    for (int i = 0; i < 2; i++)
    {
        volatile uint32_t *field = (volatile uint32_t *)((uint8_t *)action + kRateActionFields[i]);
        if (h.on[i] && *field == h.half[i]) *field = h.was[i];
    }
}

// ---- the proof a world (the Reviewer's M3) ----
// What a world's first step showed against what was asked: the step's seconds within kRateStepNear of 1/120 and the
// iterations the ones in force, exactly. The step is not held to the bit: the world's own figure (by the engine's layout a difference of
// two of its times) comes out a little off 1/120 (a first step was seen at 3c089000 against 3c088889, 0.02
// percent over), while a world still at 60 is 100 percent over. No number is no step.
static const float kRateStepNear = 0.2f;
inline uint32_t RateBits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}
enum { kRateWorldGood = 0, kRateBadIter = 1, kRateBadStep = 2 }; // bits
// The probe's line a second with the corrected terms as they read back (stage 2, S2): with Probe on and 120 in force
// alone. At 60, or without Probe, no line and no read.
inline bool RateProbeDue(bool probe, int rateInForce) { return probe && rateInForce == kRateFast; }

// ---- the step as the drive is handed it, judged at both rates (BRIEF_step_check_always.md: the head note of 21:10, the
// Reviewer's first addendum M1, M3 and M4 and the third) ----
// Another mod can change the game's physics rate. The mod's figures are for the rate in force, so the step each drive
// call is handed is held against it: right within kRateStepNear. At 120 in force a step near 1/60 is the mod's own miss
// (a world made before the write), not another mod. Anything else is foreign. A step that is no number, 0 or under is
// counted and not judged. A verdict counts after kStepRow calls in a row agree, and not before kStepWarm seconds of
// drive calls in a world, so a loading step says nothing. After a right verdict a wrong one needs a second of wrong
// drive calls as well. Once a foreign step was the verdict the sign is on and stays.
enum { kStepNone, kStepRight, kStepOwnMiss, kStepForeign };
static const int kStepRow = 8;
static const float kStepWarm = 1.0f;
inline bool StepNear(float dt, float want) { return fabsf(dt - want) <= kRateStepNear * want; }
inline int StepVerdictOf(float dt, int rate)
{
    if (!isfinite(dt) || !(dt > 0.0f)) return kStepNone;
    if (StepNear(dt, 1.0f / (float)(rate == kRateFast ? kRateFast : kRateStock))) return kStepRight;
    if (rate == kRateFast && StepNear(dt, 1.0f / (float)kRateStock)) return kStepOwnMiss;
    return kStepForeign;
}
struct StepJudge
{
    int verdict;       // the one kept, kStepNone before the first
    float dt;          // the step it was made on
    int candidate, row; // the verdict of the last calls and how many in a row
    float candidateDt;
    float warm;        // seconds of drive calls since the world began, counted to kStepWarm
    long notJudged;    // calls whose step was no number, 0 or under
    float wrongFor;    // seconds of drive calls at a wrong step since the last right one, while the kept verdict is right
    bool sign;         // a foreign step was the verdict once
};
// A new world: its first second is not judged. The verdict kept and the sign stay.
inline void StepJudgeWorld(StepJudge &j)
{
    j.warm = 0.0f;
    j.row = 0;
    j.candidate = kStepNone;
    j.wrongFor = 0.0f;
}
// One drive call. True when the kept verdict changed, or a wrong step moved by more than kRateStepNear: the mod's
// thread then has a line to write.
inline bool StepJudgeCall(StepJudge &j, float dt, int rate)
{
    const int v = StepVerdictOf(dt, rate);
    if (v == kStepNone)
    {
        j.notJudged++;
        return false;
    }
    if (j.warm < kStepWarm)
    {
        j.warm += dt;
        j.row = 0;
        j.candidate = kStepNone;
        return false;
    }
    if (v == j.candidate && (v == kStepRight || StepNear(dt, j.candidateDt))) j.row = j.row < kStepRow ? j.row + 1 : kStepRow;
    else j.candidate = v, j.candidateDt = dt, j.row = 1;
    // after a right verdict a wrong step must last: kStepWarm seconds of drive calls with no right one among them, and the
    // row. A later map's load, a pause or a very low frame rate may hand the drive an odd step for a few calls; another
    // mod's rate lasts for good, so nothing true is lost (the review of 129, S1). At both rates, and with no world known
    if (v == kStepRight) j.wrongFor = 0.0f;
    else if (j.verdict == kStepRight)
    {
        j.wrongFor += dt;
        if (j.wrongFor < kStepWarm) return false;
    }
    if (j.row < kStepRow) return false;
    if (v == j.verdict && (v == kStepRight || StepNear(dt, j.dt))) return false;
    j.verdict = v;
    j.dt = dt;
    if (v == kStepForeign) j.sign = true;
    return true;
}
// Nothing left to learn from a right step: the hook then compares one float a call and does nothing else (M4).
inline bool StepJudgeCalm(const StepJudge &j) { return j.verdict == kStepRight && j.candidate == kStepRight && j.row >= kStepRow && j.warm >= kStepWarm; }
inline bool RateStepGood(uint32_t dtBits)
{
    float dt;
    memcpy(&dt, &dtBits, 4);
    float want;
    const uint32_t wantBits = kRateDtBits;
    memcpy(&want, &wantBits, 4);
    return isfinite(dt) && fabsf(dt - want) <= kRateStepNear * want;
}
inline int RateWorldJudge(uint32_t dtBits, int iterations, int asked)
{
    return (RateStepGood(dtBits) ? 0 : kRateBadStep) | (iterations != asked ? kRateBadIter : 0);
}
