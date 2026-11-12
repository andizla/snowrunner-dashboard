// gearbox_positions.h: which of L-, L, L+ and H the fitted gearbox has in the game (BRIEF_positions_by_gearbox.md,
// review\research_low_range_fitted.md). The game's C++ keeps no list of positions: only the HUD's script reads them, from
// GearboxParams in the gearbox's GameData. The mod reads the same node: the desc by the game's own lookup (d7d550 on
// 6adb8627, game_code.h GearboxDescFind), called on the game's thread from the truck update's hook as a job, then the
// pugixml tree walked by peeks alone. Nothing of the game's is kept past the job: the mask, the five flags and the name
// it was read for (review M2). Pure but for the peek and the call the caller hands in, so the tests reach every part.
#pragma once
#include <stdint.h>
#include <string.h>
#include "truck.h"
#include "gearbox_model.h"

typedef bool (*PositionsPeek)(uint64_t addr, void *out, size_t size);

// The five flags as read. A missing attribute keeps its default (true for the four Exists flags, false for
// IsManualLowGear), as the script's read has it; defaulted has the kHas... bit of each one not read from the file.
struct GearboxFlags
{
    bool params;    // GearboxParams was found under GameData
    bool low, lowPlus, lowMinus, high, manual;
    int defaulted;
};

// What the gearbox has, as kHas... bits (the brief's table): L by IsLowerGearExists, L+ and L- by their own flags with
// L, H by IsHighGearExists. No GearboxParams has none, as the game's shifter shows (the research). IsManualLowGear does
// not take L- and L+ away (the lead's call, review R3): kHasManual is for the log alone.
inline int GearboxHasFromFlags(const GearboxFlags &f)
{
    if (!f.params) return 0;
    int has = 0;
    if (f.low) has |= kHasL | (f.lowPlus ? kHasLowPlus : 0) | (f.lowMinus ? kHasLowMinus : 0);
    if (f.high) has |= kHasH;
    if (f.manual) has |= kHasManual;
    return has;
}
inline int GearboxMissingFromHas(int has) { return ~has & kHasPositions; }

// The steps of a read, for its log line.
enum
{
    kReadOk, kReadName, kReadSocket, kReadFault, kReadNoDesc, kReadRef, kReadNative, kReadNode, kReadNotGameData, kReadWalk,
};
static const char *const kReadSteps[] = {
    "", "the fitted name at vehicle + 0x9C0", "the gearbox socket at vehicle + 0x988", "the game's lookup (it faulted)", "the game's lookup (no desc)",
    "the desc's GameData ref at + 0x58", "the native object behind the ref", "the GameData node", "the node's name (not GameData)", "the walk of the GameData node",
};

// A C string at p into out (n bytes with its 0), read up to each page's end at a time, so a short string near the end of
// a page reads. False when it cannot be read or is n - 1 characters or longer.
inline bool PositionsText(PositionsPeek peek, uint64_t p, char *out, size_t n)
{
    out[0] = 0;
    if (p < 0x10000 || n < 2) return false;
    size_t got = 0;
    while (got + 1 < n)
    {
        const uint64_t at = p + got;
        size_t want = n - 1 - got;
        const size_t room = (size_t)(0x1000 - (at & 0xFFF));
        if (want > room) want = room;
        if (!peek(at, out + got, want)) return false;
        for (size_t i = got; i < got + want; i++)
            if (!out[i]) return true;
        got += want;
    }
    out[n - 1] = 0;
    return false;
}

// The name at [at] equals name: the pointer peeked, its text copied into a small buffer and compared there (review S3).
inline bool PositionsNameIs(PositionsPeek peek, uint64_t at, const char *name)
{
    uint64_t text = 0;
    char got[40];
    return peek(at, &text, 8) && PositionsText(peek, text, got, sizeof got) && !strcmp(got, name);
}

static const uint64_t kTagMask = 0xFF00FFFFFFFFFFFFull;
static const int kPositionsWalkMost = 64; // siblings and attributes walked at most (review S3)

// From the desc to its <GameData> node (the research's chain): the tagged ref at desc + 0x58, the object's bit 0 at
// + 0x10, the native at + 0x38, its pugi node at [+ 8], the node itself, and its name GameData at + 0x10.
inline int GearboxNodeOfDesc(PositionsPeek peek, uint64_t desc, uint64_t &node)
{
    node = 0;
    uint64_t tagged = 0, native = 0, ref = 0;
    uint8_t bits = 0;
    if (!peek(desc + 0x58, &tagged, 8) || (tagged & kTagMask) < 0x10000) return kReadRef;
    const uint64_t obj = tagged & kTagMask;
    if (!peek(obj + 0x10, &bits, 1) || !(bits & 1) || !peek(obj + 0x38, &native, 8) || native < 0x10000 || !peek(native + 8, &ref, 8) || ref < 0x10000)
        return kReadNative;
    if (!peek(ref, &node, 8) || node < 0x10000) return kReadNode;
    if (!PositionsNameIs(peek, node + 0x10, "GameData")) return kReadNotGameData;
    return kReadOk;
}

// From the <GameData> node to the flags, by this build's pugixml (the research): a node's name at + 0x10, its first
// child at + 0x20, its next sibling at + 0x30, its first attribute at + 0x38; an attribute's name at + 0x08, its value at
// + 0x10, the next at + 0x20. Names case-sensitive; a bool "true" or "false" in any case, anything else the default.
inline int GearboxFlagsOfNode(PositionsPeek peek, uint64_t node, GearboxFlags &f)
{
    f = GearboxFlags{ false, true, true, true, true, false, kHasPositions | kHasManual };
    uint64_t p = 0;
    if (!peek(node + 0x20, &p, 8)) return kReadWalk;
    int steps = 0;
    while (p && !PositionsNameIs(peek, p + 0x10, "GearboxParams"))
    {
        if (p < 0x10000 || ++steps > kPositionsWalkMost || !peek(p + 0x30, &p, 8)) return kReadWalk;
    }
    if (!p) return kReadOk; // no GearboxParams: none, as the game's shifter shows
    if (p < 0x10000) return kReadWalk;
    f.params = true;
    struct Want { const char *name; bool *flag; int bit; };
    const Want wants[5] = { { "IsLowerGearExists", &f.low, kHasL },          { "IsLowerPlusGearExists", &f.lowPlus, kHasLowPlus },
                            { "IsLowerMinusGearExists", &f.lowMinus, kHasLowMinus }, { "IsHighGearExists", &f.high, kHasH },
                            { "IsManualLowGear", &f.manual, kHasManual } };
    uint64_t a = 0;
    if (!peek(p + 0x38, &a, 8)) return kReadWalk;
    for (int n = 0; a && n < kPositionsWalkMost; n++)
    {
        uint64_t nameAt = 0, valueAt = 0;
        char name[40], value[8];
        if (a < 0x10000 || !peek(a + 0x08, &nameAt, 8) || !peek(a + 0x10, &valueAt, 8)) return kReadWalk;
        if (PositionsText(peek, nameAt, name, sizeof name))
            for (const Want &w : wants)
                if (!strcmp(name, w.name) && PositionsText(peek, valueAt, value, sizeof value))
                {
                    if (!_stricmp(value, "true")) *w.flag = true;
                    else if (!_stricmp(value, "false")) *w.flag = false;
                    else continue;
                    f.defaulted &= ~w.bit;
                }
        if (!peek(a + 0x20, &a, 8)) return kReadWalk;
    }
    return kReadOk;
}

// The socket's block of types at vehicle + 0x988 .. + 0x990 looks as the game's lookup will walk it (review S2), as
// truck.h checks the engine's sets.
inline bool PositionsSocketSane(const TruckMemory &m, uint64_t vehicle)
{
    uint64_t begin = 0, end = 0;
    return m.get(vehicle + 0x988, begin) && m.get(vehicle + 0x990, end) && begin >= 0x10000 && end > begin && !((end - begin) % 0x20) &&
           end - begin <= kMaxEngineSets * 0x20;
}

// ---- the job: posted by the mod's thread, dealt on the game's thread in the truck update for that truck ----
// One job at a time; the fields are written first and the state last, and posted to dealing is claimed by an
// interlocked exchange, as ShiftJob's (truck.h). In the deal: no log, no heap, no lock (review S1). A posted job no frame
// took lives kShiftJobMs (N1), and the mod's thread posts it again on its next peek.
enum { kPosJobIdle, kPosJobPosted, kPosJobDealing, kPosJobDone };
struct PositionsJob
{
    volatile LONG state;
    uint64_t vehicle;
    unsigned long long postedAt;
    // the result, on the game's thread
    char name[kMaxText]; // the fitted name as the job read it
    int step;            // kRead...: kReadOk read
    GearboxFlags flags;
};
// The game's lookup through a plain function under __try that the caller hands in: false on a fault.
typedef bool (*PositionsLookup)(uint64_t vehicle, uint64_t name, uint64_t &desc);

inline bool PositionsJobPost(PositionsJob &j, uint64_t vehicle, unsigned long long now)
{
    if (j.state != kPosJobIdle || !vehicle) return false;
    j.vehicle = vehicle;
    j.postedAt = now;
    j.name[0] = 0;
    j.step = kReadName;
    InterlockedExchange(&j.state, kPosJobPosted);
    return true;
}

// On the game's thread, for the truck update whose first argument is first (the vehicle at + 0x20). True when dealt.
inline bool PositionsJobFrame(PositionsJob &j, const TruckMemory &m, uint64_t first, PositionsLookup lookup, unsigned long long now)
{
    uint64_t vehicle = 0;
    if (j.state != kPosJobPosted || !m.get(first + 0x20, vehicle) || vehicle != j.vehicle || now - j.postedAt > kShiftJobMs) return false;
    if (InterlockedCompareExchange(&j.state, kPosJobDealing, kPosJobPosted) != kPosJobPosted) return false;
    j.flags = GearboxFlags{};
    size_t length = 0;
    uint64_t desc = 0, node = 0;
    if (!TruckNameText(m, vehicle + kGearboxFittedAt, j.name, length, true)) j.step = kReadName; // an empty or odd name: no call
    else if (!PositionsSocketSane(m, vehicle)) j.step = kReadSocket;
    else if (!lookup || !lookup(vehicle, vehicle + kGearboxFittedAt, desc)) j.step = kReadFault;
    else if (desc < 0x10000) j.step = kReadNoDesc;
    else
    {
        j.step = GearboxNodeOfDesc(m.peek, desc, node);
        if (j.step == kReadOk) j.step = GearboxFlagsOfNode(m.peek, node, j.flags);
    }
    InterlockedExchange(&j.state, kPosJobDone);
    return true;
}

// On the mod's thread: a posted job dropped when another truck is driven, or after kShiftJobMs. True when it was.
inline bool PositionsJobDrop(PositionsJob &j, uint64_t vehicle, unsigned long long now)
{
    if (j.state != kPosJobPosted) return false;
    if (vehicle && j.vehicle == vehicle && now - j.postedAt <= kShiftJobMs) return false;
    return InterlockedCompareExchange(&j.state, kPosJobIdle, kPosJobPosted) == kPosJobPosted;
}

// What the mod's thread keeps for the driven truck: the mask for this vehicle and this name only (review M2).
struct PositionsState
{
    uint64_t vehicle;      // the vehicle the mask is for
    char name[kMaxText];   // the fitted name the last read was for, ok or not ("" none yet)
    int missing;           // kHas... bits it lacks; 0 until read, and whenever the read failed
    unsigned long long peekAt;
    int refusedSaid;       // kHas... bits of the refused keys told for this truck
    bool shifterSaid;      // the shifter gave a position the read lacks: told, the mask zeroed
    unsigned long long since; // when this vehicle's read was first asked for (the mod's clock, ms)
};

// The read for this vehicle has been asked for and has not come back: what depends on the low gears it has waits, at
// most kPositionsWaitMs (a read that never comes leaves the truck with every position, as before).
static const unsigned long long kPositionsWaitMs = 5000;
inline bool PositionsWaiting(const PositionsState &st, uint64_t vehicle, unsigned long long now)
{
    return vehicle && st.vehicle == vehicle && !st.name[0] && now - st.since < kPositionsWaitMs;
}

// On the mod's thread, the job's result taken (true) when it is done: kept only when it is for this vehicle and the name
// the mod's own peek reads now (nameNow); else dropped, and the next peek posts again.
inline bool PositionsJobTake(PositionsJob &j, PositionsState &st, const char *nameNow, int &step, GearboxFlags &flags)
{
    if (j.state != kPosJobDone) return false;
    const bool ours = j.vehicle == st.vehicle && nameNow && nameNow[0] && !strcmp(j.name, nameNow);
    step = j.step;
    flags = j.flags;
    if (ours)
    {
        strncpy_s(st.name, j.name, _TRUNCATE);
        st.missing = step == kReadOk ? GearboxMissingFromHas(GearboxHasFromFlags(flags)) : 0;
    }
    InterlockedExchange(&j.state, kPosJobIdle);
    return ours;
}

// A new vehicle starts at zero (M2).
inline void PositionsNewVehicle(PositionsState &st, uint64_t vehicle)
{
    st = PositionsState{};
    st.vehicle = vehicle;
}
