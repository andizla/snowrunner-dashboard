// shifter_read.h: the five flags of the fitted gearbox as the game's own shifter is told them
// (BRIEF_positions_by_shifter.md with the Reviewer's addendum). The HUD's script parses the fitted gearbox at every
// truck change and asks the engine's native for each flag by its name: bool(node, {text, length}, default, flag). The
// mod hooks that one native and keeps what the script was told for the five names, so whatever file the script landed
// on and whatever default it handed in, the answer is the one the shifter is built from. No table of the script's is
// walked and no rule of its lookup is copied.
//
// In the hook (ShifterNote): the name compared by its length first, then its bytes; any other name returns at once.
// No lock, no log, no allocation, no read of game memory but the name handed in. Pure, so the tests reach every part.
#pragma once
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include "gearbox_model.h"
#include "gearbox_positions.h"

// The native's name argument: the text and its length (the thunk builds it from the script's string).
struct XmlName
{
    const char *text;
    uint32_t length;
};

// The five names in the order the script's parse asks them, each with the kHas... bit it sets.
static const int kShifterNames = 5;
static const char *const kShifterName[kShifterNames] = { "IsHighGearExists", "IsLowerGearExists", "IsLowerPlusGearExists", "IsLowerMinusGearExists", "IsManualLowGear" };
static const int kShifterBit[kShifterNames] = { kHasH, kHasL, kHasLowPlus, kHasLowMinus, kHasManual };
static const int kShifterAll = kHasPositions | kHasManual;

// Which of the five a name is, -1 none: by the length first, so any other name costs a few instructions.
inline int ShifterNameIndex(const char *text, uint32_t length)
{
    if (length < 15 || length > 22 || !text) return -1;
    for (int i = 0; i < kShifterNames; i++)
        if (strlen(kShifterName[i]) == length && !memcmp(text, kShifterName[i], length)) return i;
    return -1;
}

// One parse as the hook saw it: the node asked, the flags asked so far as kHas... bits, their answers, the tick of the
// last one.
struct ShifterCapture
{
    uint64_t node;
    int asked, answers;
    unsigned long long tick;
};
inline bool ShifterComplete(const ShifterCapture &c) { return c.node && (c.asked & kShifterAll) == kShifterAll; }

// The last four captures. seq is odd while one is being written, so a reader never takes a capture half written;
// writing keeps two threads of the game from writing at once (the second one's flag is dropped, never waited for).
static const int kShifterRing = 4;
static const unsigned long long kShifterParseMs = 1000; // a flag asked of a node this long after its last one starts a capture anew
struct ShifterRing
{
    volatile LONG seq, writing;
    volatile LONG fault;       // the hook's own code faulted: the route is off for the session
    volatile LONG calls, kept; // every call of the native, and those with one of the five names
    ShifterCapture c[kShifterRing];
    int next;
};

// In the hook, after the game's own native has answered. A flag of a node goes into that node's capture while the
// capture still lacks it and is fresh; else a new capture starts in the oldest place.
inline void ShifterNote(ShifterRing &r, uint64_t node, const char *text, uint32_t length, bool answer, unsigned long long now)
{
    const int i = ShifterNameIndex(text, length);
    if (i < 0 || !node) return;
    if (InterlockedCompareExchange(&r.writing, 1, 0) != 0) return;
    const int bit = kShifterBit[i];
    int slot = -1;
    for (int k = 0; k < kShifterRing; k++)
        if (r.c[k].node == node && !(r.c[k].asked & bit) && now - r.c[k].tick < kShifterParseMs && (slot < 0 || r.c[k].tick > r.c[slot].tick)) slot = k;
    InterlockedIncrement(&r.seq);
    if (slot < 0)
    {
        slot = r.next;
        r.next = (r.next + 1) % kShifterRing;
        r.c[slot] = ShifterCapture{ node, 0, 0, 0 };
    }
    r.c[slot].asked |= bit;
    if (answer) r.c[slot].answers |= bit;
    r.c[slot].tick = now;
    InterlockedIncrement(&r.seq);
    InterlockedIncrement(&r.kept);
    InterlockedExchange(&r.writing, 0);
}

// On the mod's thread: the four captures, whole. False where the hook wrote during every one of four tries.
inline bool ShifterRingRead(const ShifterRing &r, ShifterCapture out[kShifterRing])
{
    for (int tries = 0; tries < 4; tries++)
    {
        const LONG before = r.seq;
        if (before & 1) continue;
        for (int k = 0; k < kShifterRing; k++) out[k] = r.c[k];
        if (r.seq == before) return true;
    }
    return false;
}

// Which capture is the driven truck's: the newest complete one whose tick is no older than kShifterBeforeMs before the
// last pass on which the mod saw the truck before (changedAt; 0 for the first truck of a session: any whole capture),
// and newer than the one taken before (takenTick, 0 none). -1 none. The parse has one caller, the truck change, so
// while the mod sees no truck every parse is of the truck to come: at a load or coming from the garage the script may
// parse well before the mod's first pass with the truck (review of 159, M1b). The second covers a change the mod saw
// a pass late. Never a capture from while the truck before was still driven.
static const unsigned long long kShifterBeforeMs = 1000, kShifterWaitMs = 2000;

// The trucks as the mod's passes saw them: the driven one, when it was first and last seen, and the last pass on which
// the truck before it was seen (0: there was none). A pass with no truck changes nothing.
struct ShifterSeen
{
    uint64_t truck;
    unsigned long long since, at, beforeAt;
};
inline void ShifterSeenStep(ShifterSeen &s, uint64_t driven, unsigned long long now)
{
    if (!driven) return;
    if (driven != s.truck)
    {
        s.beforeAt = s.truck ? s.at : 0;
        s.truck = driven;
        s.since = now;
    }
    s.at = now;
}
inline int ShifterPick(const ShifterCapture *c, int n, unsigned long long changedAt, unsigned long long takenTick)
{
    int best = -1;
    for (int k = 0; k < n; k++)
        if (ShifterComplete(c[k]) && c[k].tick + kShifterBeforeMs >= changedAt && c[k].tick > takenTick && (best < 0 || c[k].tick > c[best].tick)) best = k;
    return best;
}

// A capture's answers as the five flags. GearboxParams was there: the script asks no flag without it.
inline GearboxFlags ShifterFlags(const ShifterCapture &c)
{
    GearboxFlags f = {};
    f.params = true;
    f.high = (c.answers & kHasH) != 0;
    f.low = (c.answers & kHasL) != 0;
    f.lowPlus = (c.answers & kHasLowPlus) != 0;
    f.lowMinus = (c.answers & kHasLowMinus) != 0;
    f.manual = (c.answers & kHasManual) != 0;
    f.defaulted = 0; // the shifter's read does not say which were defaults
    return f;
}
inline bool GearboxFlagsSame(const GearboxFlags &a, const GearboxFlags &b)
{
    return a.params == b.params && (!a.params || (a.low == b.low && a.lowPlus == b.lowPlus && a.lowMinus == b.lowMinus && a.high == b.high && a.manual == b.manual));
}

// The hook is wanted only where the positions go by the fitted gearbox: the mod's own gearbox with the box on.
inline bool ShifterHookWanted(int level, bool positions) { return level >= 2 && positions; }
// ---- which read is in force for the driven truck ----
enum { kRouteNone, kRouteShifter, kRouteDesc };
// Why the desc's read is the one in force, for the line.
enum { kDescWhyNone, kDescWhyNoHook, kDescWhyNoParams, kDescWhyLate, kDescWhyFault, kDescWhyHookAfter };
static const char *const kDescWhy[] = {
    "", "the shifter's own read is not hooked in this game", "no GearboxParams, so the shifter asks no flag",
    "no read of the shifter came in two seconds", "the mod's part of the shifter's read faulted",
    "the shifter's read was hooked after this truck was taken, so it comes with the next truck change",
};
struct PositionsRoute
{
    int route, why;                  // kRoute..., kDescWhy...
    bool haveShifter, haveDesc;
    GearboxFlags shifter, desc;
    int descStep;                    // kRead... of the desc's read
    uint64_t shifterNode, descNode;  // the GearboxParams node of each (the shifter's: the pugi node behind the native asked, 0 not read)
    unsigned long long changedAt;    // the last pass with the truck before, or with the fitted name before; 0 the first truck
    unsigned long long takenTick;    // the tick of the capture taken, 0 none
};

// The read in force and the mask it gives: the shifter's as soon as there is one, whenever it comes. Else the desc's,
// once it is back and either the shifter's cannot come (no hook, a fault, no GearboxParams) or two seconds have gone
// since the truck was first seen. Until then none, and every position is there (missing 0) while the caller waits.
// hooked: the native is hooked and its capture has not faulted. hookAfter: the hook went in after this truck was
// taken, so its parse was not caught and none will come: the desc's read at once, no wait. Returns true where the
// route or its flags changed.
inline bool PositionsRouteStep(PositionsRoute &r, bool hooked, bool faulted, unsigned long long since, unsigned long long now, int &missing, bool hookAfter = false)
{
    const int routeWas = r.route, whyWas = r.why;
    if (r.haveShifter && !faulted)
    {
        r.route = kRouteShifter, r.why = kDescWhyNone;
        missing = GearboxMissingFromHas(GearboxHasFromFlags(r.shifter));
    }
    else if (r.haveDesc)
    {
        const int why = faulted ? kDescWhyFault : !hooked ? kDescWhyNoHook : r.descStep == kReadOk && !r.desc.params ? kDescWhyNoParams : hookAfter ? kDescWhyHookAfter :
                        now - since >= kShifterWaitMs ? kDescWhyLate : kDescWhyNone;
        if (why == kDescWhyNone)
        {
            r.route = kRouteNone, r.why = kDescWhyNone;
            missing = 0;
        }
        else
        {
            r.route = kRouteDesc, r.why = why;
            missing = r.descStep == kReadOk ? GearboxMissingFromHas(GearboxHasFromFlags(r.desc)) : 0;
        }
    }
    else
    {
        r.route = kRouteNone, r.why = kDescWhyNone;
        missing = 0;
    }
    return r.route != routeWas || r.why != whyWas;
}
// The desc is back and the shifter's read may still come: what depends on the low gears waits (PositionsWaiting).
inline bool PositionsRoutePending(const PositionsRoute &r) { return r.haveDesc && r.route == kRouteNone; }

// The flags in a line's words.
inline int ShifterFlagsText(char *out, size_t size, const GearboxFlags &f)
{
    if (!f.params) return snprintf(out, size, "no GearboxParams");
    return snprintf(out, size, "IsHighGearExists %d, IsLowerGearExists %d, IsLowerPlusGearExists %d, IsLowerMinusGearExists %d, IsManualLowGear %d", f.high, f.low, f.lowPlus, f.lowMinus,
                    f.manual);
}
// The line of the read in force: the route, its five flags, what the truck has, and the other read where it is known:
// that it agrees, or both sets with both nodes where the two differ. has: the positions in words (GearboxPosHasText).
inline void PositionsRouteLine(char *out, size_t size, const char *name, const PositionsRoute &r, const char *has)
{
    char mine[200], other[200];
    out[0] = 0;
    if (r.route == kRouteShifter)
    {
        ShifterFlagsText(mine, sizeof mine, r.shifter);
        int n = snprintf(out, size, "%s by the game shifter's own read (node %llx): %s: the truck has %s", name, (unsigned long long)r.shifterNode, mine, has);
        if (n < 0 || (size_t)n >= size) return;
        if (!r.haveDesc) snprintf(out + n, size - n, "; the desc's read is not back yet");
        else if (r.descStep != kReadOk) snprintf(out + n, size - n, "; the desc's read failed at %s", kReadSteps[r.descStep]);
        else if (GearboxFlagsSame(r.shifter, r.desc)) snprintf(out + n, size - n, "; the desc's read agrees (node %llx)", (unsigned long long)r.descNode);
        else
        {
            ShifterFlagsText(other, sizeof other, r.desc);
            snprintf(out + n, size - n, "; THE DESC'S READ DIFFERS (node %llx): %s: the two reads land on different files", (unsigned long long)r.descNode, other);
        }
    }
    else if (r.route == kRouteDesc)
    {
        if (r.descStep != kReadOk)
        {
            snprintf(out, size, "%s: the desc's read failed at %s and %s, so the truck has all positions", name, kReadSteps[r.descStep], kDescWhy[r.why]);
            return;
        }
        ShifterFlagsText(mine, sizeof mine, r.desc);
        snprintf(out, size, "%s by the desc's read (node %llx; %s): %s: the truck has %s", name, (unsigned long long)r.descNode, kDescWhy[r.why], mine, has);
    }
}
