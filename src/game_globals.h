// game_globals.h: where the game keeps the objects the mod reads (truck.h), found in any build of the game by the
// code that uses them. Each such place is a static of the exe, and the game's code reaches it by an instruction whose
// 32-bit displacement counts from the instruction's end (x64's RIP-relative addressing): a build moves the place and
// the displacement with it, and the bytes around the instruction mostly stay. A signature is those bytes, "?" where
// a build may differ (the displacement itself, and the distance of a call, a jump or another such instruction near
// it), with where the displacement stands in it.
//
// The signatures are harvested from the Steam build of 22 July 2026, whose places are known (GlobalHarvest, run with
// Probe=1 on that build, writes them into Gauges.signatures.txt), and looked for in any other build (GlobalResolve).
// Some places the code never names: they are fields of a static object the code reaches by its start (a lea, as the
// small functions that hand the object out do), and loads from that by an offset. For those the signature is of a
// reference to the nearest object start before them, with how far before it stands. Nothing here reads the game's memory but its code, and nothing is written.
#pragma once
#include "game_code.h"

enum { kGlobControl, kGlobWheelClass, kGlobGame, kGlobMap, kGlobViewer, kGlobGarage, kGlobInput, kGlobEngineSets, kGlobClock, kGlobCount };
struct GameGlobal
{
    const char *name, *id; // id: its name in this file, for the harvest's lines
    uint64_t steam; // its place in the Steam build of 22 July 2026, from the exe's base
    bool needed;    // without it the mod cannot find the driven truck, and stands down
    int reach;      // bytes past its place the code may reach it at (a field of a static object, as the engine sets' list at + 8)
};
static const GameGlobal kGameGlobals[kGlobCount] = {
    { "control", "kGlobControl", 0x2a8eb78, true, 1 },         { "wheel class", "kGlobWheelClass", 0x2258918, true, 1 },
    { "game", "kGlobGame", 0x2a8eb90, true, 1 },               { "map", "kGlobMap", 0x2a8eb30, false, 1 },
    { "viewer", "kGlobViewer", 0x2a8eb38, false, 1 },          { "garage", "kGlobGarage", 0x2a90728, false, 1 },
    { "input", "kGlobInput", 0x2a8ebe0, false, 1 },            { "engine sets", "kGlobEngineSets", 0x2a9e8e0, false, 16 },
    { "clock", "kGlobClock", 0x2a8eb48, false, 1 } };
// The harvest's version: a new one is run again on the known build where Gauges.signatures.txt is of an older one.
static const int kHarvestVersion = 3;
// How far before a place the code never names the harvest looks for a static object's start, and how many it tries,
// nearest first.
static const int kGlobalBaseSpan = 0x400, kGlobalBaseTries = 8;

// A signature of a global: the pattern, the place of the displacement in it, how many bytes of the instruction
// follow the displacement (an immediate: the instruction ends that much later), and how far past the global's place
// the instruction reaches (a field of it; below 0 the start of a static object the global is a field of).
struct GlobalSig
{
    int global;
    const char *pattern;
    int at, after, into;
};
// Harvested from the Steam build of 22 July 2026 (harvest 3, each checked there; the end marked by a global of -1).
// Control, the game, the map, the viewer, the input and the clock are fields of static objects that small functions
// hand out by their start: control and the game 0x18 and 0x30 into the one at 0x2a8eb60, the map, the viewer and the
// clock 0x20, 0x28 and 0x38 into the one at 0x2a8eb10, the input 0x40 into the one at 0x2a8eba0.
static const char *const kSigControlObject = "? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 40 53 48 83 EC 60 48";
static const char *const kSigMapObject = "? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC "
                                         "CC 48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 40 53 48 83 EC 60 48";
static const GlobalSig kGlobalSigs[] = {
    { kGlobControl, kSigControlObject, 16, 0, -0x18 },
    { kGlobGame, kSigControlObject, 16, 0, -0x30 },
    { kGlobInput, "5B C3 CC CC CC CC CC 48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48", 10, 0, -0x40 },
    { kGlobMap, kSigMapObject, 32, 0, -0x20 },
    { kGlobViewer, kSigMapObject, 32, 0, -0x28 },
    { kGlobClock, kSigMapObject, 32, 0, -0x38 },
    { kGlobWheelClass, "01 00 00 00 87 43 08 48 8D 0D ? ? ? ? 48 89 0B 89 BB CC 00 00 00 40", 10, 0, 0 },
    { kGlobWheelClass, "FE FF FF FF 48 8B D9 48 8D 05 ? ? ? ? 48 89 01 48 8B 51 18 48 85 D2", 10, 0, 0 },
    { kGlobGarage, "? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B C4 4C 89 40 18", 16, 0, 0 },
    { kGlobEngineSets, "20 22 00 00 48 8D 0D ? ? ? ? E8 ? ? ? ? BA 08 00 00 00 48 8D 0D ? ? ? ? E8 ? ? ? ? 90 8B 15 ? ? ? ? 48 8B 0D ? ? ? ? E8 ? ? ? ?", 24, 0, 0 },
    { kGlobEngineSets, "44 24 70 48 8D 55 10 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 45 10 48", 10, 0, 0 },
    { kGlobEngineSets, "8D 41 10 48 8D 55 50 48 8D 0D ? ? ? ? E8 ? ? ? ? 4C 8B 74 24 50", 10, 0, 0 },
    { kGlobEngineSets, "29 BC 24 20 22 00 00 48 8D 0D ? ? ? ? E8 ? ? ? ? BA 08 00 00 00", 10, 0, 8 },
    { -1, "", 0, 0, 0 } };

// The bytes of an immediate that follow a RIP-relative displacement at p, by the instruction's opcode before its
// ModRM byte (code[p - 1]); -1 for an instruction that is no data access of the kinds the game uses.
inline int RipAfter(const uint8_t *code, size_t from, size_t p)
{
    if (p < from + 2) return -1;
    const uint8_t modrm = code[p - 1], op = code[p - 2];
    if ((modrm & 0xC7) != 0x05) return -1;
    const int reg = (modrm >> 3) & 7;
    const bool word = (p >= from + 3 && code[p - 3] == 0x66) || (p >= from + 4 && code[p - 4] == 0x66 && (code[p - 3] & 0xF0) == 0x40);
    switch (op)
    {
    case 0xC6: case 0x80: case 0x83: return 1;              // mov, an operation with a byte
    case 0xC7: case 0x81: return word ? 2 : 4;               // mov, an operation with a word or a dword
    case 0xF6: return reg == 0 ? 1 : 0;                      // test with a byte, or another of its group
    case 0xF7: return reg == 0 ? (word ? 2 : 4) : 0;
    case 0xBA: return p >= from + 3 && code[p - 3] == 0x0F ? 1 : -1; // bt and its kind
    default: return 0;
    }
}

// The places in code[from, to) where an instruction reaches image + rva (or up to reach bytes past it), up to most of
// them: the displacement's place, the bytes after it, and how far past rva it reaches. Their count; those that reach
// rva itself come first.
struct GlobalRef
{
    size_t at;
    int after, into;
};
inline int GlobalRefsFind(const uint8_t *code, size_t from, size_t to, uint64_t rva, int reach, GlobalRef *out, int most)
{
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (size_t p = from + 2; p + 4 <= to && n < most; p++)
        {
            if ((code[p - 1] & 0xC7) != 0x05) continue;
            int32_t d;
            memcpy(&d, code + p, 4);
            const int64_t end = (int64_t)p + 4 + d; // where it points, but for an immediate after it
            if (end + 4 < (int64_t)rva || end >= (int64_t)rva + reach) continue;
            const int after = RipAfter(code, from, p);
            const int64_t into = end + (after < 0 ? 0 : after) - (int64_t)rva;
            if (after < 0 || into < 0 || into >= reach || (pass == 0) != (into == 0)) continue;
            out[n++] = GlobalRef{ p, after, (int)into };
        }
    return n;
}

// A displacement or a distance at q in code: a call or a jump (E8, E9, 0F 80 to 0F 8F) or a RIP-relative operand,
// pointing into the image of size bytes. A build moves what it points at, so a signature does not count it.
inline bool GlobalMoving(const uint8_t *code, size_t from, size_t q, uint64_t size)
{
    if (q < from + 1) return false;
    const uint8_t b = code[q - 1];
    const bool jump = b == 0xE8 || b == 0xE9 || (q >= from + 2 && code[q - 2] == 0x0F && b >= 0x80 && b <= 0x8F);
    if (!jump && (b & 0xC7) != 0x05) return false;
    int32_t d;
    memcpy(&d, code + q, 4);
    const int64_t target = (int64_t)q + 4 + d;
    return target >= 0 && (uint64_t)target < size;
}

// A signature for the reference r: the bytes from before ahead of its displacement to behind past the instruction's
// end, the displacement and whatever else a build moves left out. In text ("48 8B 0D ? ? ? ? ..."), with where the
// displacement stands. False where the window leaves the code or is longer than kPatternMax.
inline bool GlobalSigOf(const uint8_t *code, size_t from, size_t to, uint64_t size, const GlobalRef &r, int before, int behind, char *text, size_t textSize, int &at)
{
    const size_t lo = r.at - (size_t)before, hi = r.at + 4 + (size_t)r.after + (size_t)behind;
    if (r.at < from + (size_t)before || hi > to || (int)(hi - lo) > kPatternMax) return false;
    uint8_t keep[kPatternMax];
    memset(keep, 1, sizeof keep);
    for (int i = 0; i < 4; i++) keep[before + i] = 0;
    // a displacement or a distance in the window, or one it cuts at either end
    for (size_t q = lo >= from + 3 ? lo - 3 : from; q < hi && q + 4 <= to; q++)
        if (q != r.at && GlobalMoving(code, from, q, size))
            for (size_t i = q; i < q + 4; i++)
                if (i >= lo && i < hi) keep[i - lo] = 0;
    size_t n = 0;
    text[0] = 0;
    for (size_t q = lo; q < hi && n + 4 < textSize; q++)
        n += (size_t)snprintf(text + n, textSize - n, keep[q - lo] ? "%s%02X" : "%s?", q > lo ? " " : "", code[q]);
    at = before;
    return n + 4 < textSize;
}

// Where a pattern stands in code[from, to): its offset and how often (up to 9). False where it does not.
inline bool GlobalPatternFind(const uint8_t *code, size_t from, size_t to, const char *pattern, size_t &at, int &count)
{
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    const int length = PatternParse(pattern, bytes, mask);
    return length > 0 && PatternFind(code, from, to, bytes, mask, length, at, count);
}

// The nearest object start before rva, at most kGlobalBaseSpan before it and before rva + below: a place a lea in
// code[from, to) takes the address of. How far before rva it stands (below 0), 0 where there is none.
inline int GlobalBaseBelow(const uint8_t *code, size_t from, size_t to, uint64_t rva, int below)
{
    int best = 0;
    for (size_t p = from + 2; p + 4 <= to; p++)
    {
        if ((code[p - 1] & 0xC7) != 0x05 || code[p - 2] != 0x8D) continue;
        int32_t d;
        memcpy(&d, code + p, 4);
        const int64_t k = (int64_t)p + 4 + d - (int64_t)rva;
        if (k >= below || k < -kGlobalBaseSpan || (best && k <= best)) continue;
        best = (int)k;
    }
    return best;
}

// Up to most signatures of a global at rva, each standing once in code[from, to): from its first references on, the
// shortest window that stands once (10 bytes on each side, then 16, 24, 32 and 40). Where none of them does, the same
// for the nearest places before it that the code reaches (GlobalBaseBelow), up to kGlobalBaseTries of them. Their
// count; note: what was found on the way, for the harvest's file.
struct GlobalHarvested
{
    char pattern[kPatternMax * 3 + 4];
    int at, after, into;
};
struct GlobalHarvestNote
{
    int direct;  // references to the global (up to 24)
    int base;    // where its signatures are of a place before it: how far before (below 0); else 0
    int baseRefs; // references to that place (up to 24)
};
inline int GlobalHarvestAt(const uint8_t *code, size_t from, size_t to, uint64_t size, const GlobalRef *refs, int found, int shift, GlobalHarvested *out, int most)
{
    int n = 0;
    static const int sides[5] = { 10, 16, 24, 32, 40 };
    for (int i = 0; i < found && n < most; i++)
        for (int side : sides)
        {
            GlobalHarvested h = {};
            if (!GlobalSigOf(code, from, to, size, refs[i], side, side, h.pattern, sizeof h.pattern, h.at)) continue;
            size_t at = 0;
            int count = 0;
            if (GlobalPatternFind(code, from, to, h.pattern, at, count) && count == 1)
            {
                h.after = refs[i].after;
                h.into = refs[i].into + shift;
                out[n++] = h;
                break;
            }
        }
    return n;
}
inline int GlobalHarvest(const uint8_t *code, size_t from, size_t to, uint64_t size, uint64_t rva, int reach, GlobalHarvested *out, int most, GlobalHarvestNote *note = nullptr)
{
    GlobalRef refs[24];
    GlobalHarvestNote seen = {};
    seen.direct = GlobalRefsFind(code, from, to, rva, reach, refs, 24);
    int n = GlobalHarvestAt(code, from, to, size, refs, seen.direct, 0, out, most);
    for (int tries = 0, below = 0; !n && tries < kGlobalBaseTries; tries++)
    {
        const int k = GlobalBaseBelow(code, from, to, rva, below);
        if (!k) break;
        const int found = GlobalRefsFind(code, from, to, (uint64_t)((int64_t)rva + k), 1, refs, 24);
        n = GlobalHarvestAt(code, from, to, size, refs, found, k, out, most);
        if (n)
        {
            seen.base = k;
            seen.baseRefs = found;
        }
        below = k;
    }
    if (note) *note = seen;
    return n;
}

// The place of global g in this build, from the exe's base: where its signatures that stand once in code[from, to)
// point. 0 where none stands once, or where two point at different places (conflict set). signatures: how many stood once.
inline uint64_t GlobalResolve(const uint8_t *code, size_t from, size_t to, const GlobalSig *sigs, int g, int &signatures, bool &conflict)
{
    uint64_t place = 0;
    signatures = 0;
    conflict = false;
    for (const GlobalSig *s = sigs; s->global >= 0; s++)
    {
        if (s->global != g) continue;
        size_t at = 0;
        int count = 0;
        if (!GlobalPatternFind(code, from, to, s->pattern, at, count) || count != 1) continue;
        int32_t d;
        memcpy(&d, code + at + s->at, 4);
        const int64_t where = (int64_t)(at + (size_t)s->at) + 4 + s->after + d - s->into;
        if (where <= 0) continue;
        signatures++;
        if (!place) place = (uint64_t)where;
        else if (place != (uint64_t)where) conflict = true;
    }
    return conflict ? 0 : place;
}
