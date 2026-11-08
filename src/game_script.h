// game_script.h: the game's gear display at the bottom right made dark while the mod's own gearbox drives
// (BRIEF_shifter_no_gear.md part B, with the Reviewer's addendum and the Researcher's three). The display is the
// game's script view HudTransmissionUiView. Two of its functions light it: UpdateStatusText writes the mode's text,
// and UpdateRunnerAnimation animates the lit dot (the runner). The game's script engine runs a function from the code
// array its function object holds at + 0x60 and reads that pointer once at each call's start. So one aligned pointer
// write swaps a function from its next call on, and a call in flight ends on the old array.
//
// Each function gets two arrays of ours, made once a session with the engine's own allocator, so that the function
// object's destructor frees them at the runtime's teardown as its own:
// - dark: the text hidden (status.visible = false, then return), and the dot shown only while LB is held
//   (runner.visible = isActive, then the stock words);
// - light: visible = true, then the stock words. That is the game's display as it was, and it stays in place.
// The stock array stays the engine's and is never freed or put back by us.
//
// Everything is found by name, as the engine finds it: the class by the hash of its name, its member by the hash of
// the member's name, and each name read back. The functions' words are checked as numbers here before anything is
// written. Nothing of the game's code is in this file but the opcode numbers and the member indexes. The stock words
// are copied from the engine's array at run time.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "truck.h"
#include "game_globals.h"

// ---- the script engine's words (C:\Games\snowrunner-ssl\sslkit\ops.py, read from the interpreter in 6adb8627) ----
// A word is a uint32: the opcode in the low byte, the operand above it biased by 2^23.
enum { kSslPop = 2, kSslBool = 5, kSslLocal = 7, kSslGetField = 10, kSslCall = 13, kSslRet = 19, kSslSetField = 60 };
static const int kSslJumpFirst = 22, kSslJumpLast = 26; // jz, jnz, jz.keep, jnz.keep, jmp: the operand an absolute word
static const int kSslBias = 0x800000;
inline uint32_t SslWord(int op, int operand) { return (uint32_t)op | ((uint32_t)(operand + kSslBias) << 8); }
inline int SslOp(uint32_t w) { return (int)(w & 0xFF); }
inline int SslOperand(uint32_t w) { return (int)(w >> 8) - kSslBias; }
inline bool SslIsJump(uint32_t w) { return SslOp(w) >= kSslJumpFirst && SslOp(w) <= kSslJumpLast; }

// The engine's name hash: Jenkins one-at-a-time over the name's bytes (signed) and its NUL (sslkit/oat.py).
inline uint32_t SslOat(const char *s)
{
    uint32_t h = 0;
    for (const char *c = s;; c++)
    {
        h += (uint32_t)(int32_t)(signed char)*c;
        h *= 0x401;
        h ^= h >> 6;
        if (!*c) break;
    }
    h *= 9;
    h ^= h >> 11;
    h *= 0x8001;
    return h;
}

// ---- the engine's tables (6adb8627, review\shifter_no_gear.md and its addenda) ----
// The class registry (a pointer the code loads): at + 8 the module list, whose + 0x10 holds sorted (class hash, module
// name) pairs, count at + 0x18, and whose own array of 0x50-byte modules, sorted by name, is at + 0 with its count at
// + 8. A module's classes are sorted (hash, class) pairs of 16 bytes at + 0x18, count at + 0x20. A class whose module
// has no name is in the registry's own list at + 0x170, count at + 0x178. Names are offsets into the string pool (a
// pointer the code loads).
static const uint32_t kSslModules = 0x08, kSslPairs = 0x10, kSslPairCount = 0x18, kSslModuleArray = 0x00, kSslModuleCount = 0x08, kSslModuleSize = 0x50;
static const uint32_t kSslModuleClasses = 0x18, kSslModuleClassCount = 0x20, kSslGlobalClasses = 0x170, kSslGlobalClassCount = 0x178;
// A class: its base at + 0x20, sorted (member hash, index) pairs at + 0x48 with their count at + 0x50, the member
// table at + 0x58 (32 bytes an entry: the name at + 0, the kind at + 4, the object at + 8), its count at + 0x60.
static const uint32_t kSslClassBase = 0x20, kSslMemberPairs = 0x48, kSslMemberPairCount = 0x50, kSslMembers = 0x58, kSslMemberCount = 0x60, kSslEntrySize = 32;
static const int kSslKindScript = 2;
// A script function object: the code array at + 0x60, its word count at + 0x68 and capacity at + 0x6C.
static const uint32_t kSslCode = 0x60, kSslCodeCount = 0x68;
static const int kSslMostSearch = 64, kSslMostBases = 16, kSslMostName = 96;

// ---- the places, found by code in any build (game_globals.h's signatures, harvested on 6adb8627 and found the same
// in 6ab52f5d and 6a607c05) ----
enum { kSslPlaceClasses, kSslPlacePool, kSslPlaceCount };
static const GlobalSig kSslSigs[] = {
    { kSslPlaceClasses, "? ? ? 84 C0 75 2D 48 8B 05 ? ? ? ? 8B 98 68 04 00 00 E8 ? ? ?", 10, 0, 0 },
    { kSslPlaceClasses, "28 C6 E8 ? ? ? ? 48 8B 0D ? ? ? ? ? ? ? FF FF E8 ? ? ? ?", 10, 0, 0 },
    { kSslPlaceClasses, "8B F8 BA FF FF FF FF 48 8B 0D ? ? ? ? E8 ? ? ? ? E8 ? ? ? ?", 10, 0, 0 },
    { kSslPlacePool, "58 08 E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 C7 C0 FF FF FF FF 4C 03 C3", 10, 0, 0 },
    { kSslPlacePool, "C0 75 ED 85 D2 74 1C 48 8B 0D ? ? ? ? 41 FF C3 49 FF C2 49 83 C1 18", 10, 0, 0 },
    { kSslPlacePool, "89 6C 24 18 45 33 C9 48 8B 2D ? ? ? ? 8B F1 48 89 7C 24 20 49 63 78", 10, 0, 0 },
    { -1, "", 0, 0, 0 } };
// The engine's allocate (bytes, 0, 0, 0): its own first bytes, and the call to it in the code array's reserve, which
// must name the same place (the Reviewer's should 5).
static const char *const kSslAllocEntry = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 65 48 8B 04 25 58 00 00 00 48 8B D9 8B 15 ? ? ? ? "
                                          "41 BE 78 14 00 00 48 8B 34 D0 41 8B 04 36 85 C0 75 12 E8";
static const char *const kSslAllocCall = "45 33 C9 45 33 C0 33 D2 41 8B CA E8 ? ? ? ? 48 89 07 89 77 0C";

struct SslPlaces
{
    uint64_t classes, pool, alloc; // from the exe's base; 0 where not found
    char why[160];                 // what was not found, "" where all were
};
inline bool SslPlacesFind(const uint8_t *code, size_t from, size_t to, SslPlaces &p)
{
    memset(&p, 0, sizeof p);
    int signatures = 0;
    bool conflict = false;
    p.classes = GlobalResolve(code, from, to, kSslSigs, kSslPlaceClasses, signatures, conflict);
    p.pool = GlobalResolve(code, from, to, kSslSigs, kSslPlacePool, signatures, conflict);
    size_t entry = 0, call = 0;
    int entries = 0, calls = 0;
    const bool hasEntry = GlobalPatternFind(code, from, to, kSslAllocEntry, entry, entries) && entries == 1;
    const bool hasCall = GlobalPatternFind(code, from, to, kSslAllocCall, call, calls) && calls == 1;
    int32_t d = 0;
    if (hasCall) memcpy(&d, code + call + 12, 4);
    const int64_t target = hasCall ? (int64_t)call + 16 + d : -1;
    if (hasEntry && hasCall && target == (int64_t)entry) p.alloc = entry;
    if (!p.classes) strcat_s(p.why, "the class registry not found; ");
    if (!p.pool) strcat_s(p.why, "the string pool not found; ");
    if (!p.alloc)
        strcat_s(p.why, !hasEntry ? "the engine's allocate not found; " : !hasCall ? "the call to the engine's allocate not found; "
                                                                                    : "the engine's allocate and the call to it disagree; ");
    const size_t n = strlen(p.why);
    if (n >= 2) p.why[n - 2] = 0;
    return p.classes && p.pool && p.alloc;
}

// ---- reads by name ----
struct SslHost
{
    TruckMemory m;
    uint64_t classes, pool;                    // the places' addresses (base + the place)
    uint64_t (*alloc)(uint64_t bytes);         // the engine's allocate on this thread, 0 where it failed
    bool (*store64)(uint64_t at, uint64_t v);  // one aligned 8-byte store
};

// A name from the pool at offset off into out; false where it cannot be read or is longer than size - 1.
inline bool SslName(const SslHost &h, uint32_t off, char *out, size_t size)
{
    uint64_t base = 0;
    if (!h.m.get(h.pool, base) || !base) return false;
    for (size_t i = 0; i + 1 < size; i++)
    {
        char c = 0;
        if (!h.m.get(base + off + i, c)) return false;
        out[i] = c;
        if (!c) return true;
    }
    out[size - 1] = 0;
    return false;
}

// A sorted array of (hash, value) entries of stride bytes at array, count of them: the entry's address holding hash, 0
// where none does.
inline uint64_t SslSorted(const SslHost &h, uint64_t array, uint32_t count, uint32_t stride, uint32_t hash)
{
    if (!array || count > 1u << 20) return 0;
    uint32_t lo = 0, hi = count;
    for (int i = 0; lo < hi && i < kSslMostSearch; i++)
    {
        const uint32_t mid = (lo + hi) / 2;
        uint32_t at = 0;
        if (!h.m.get(array + (uint64_t)mid * stride, at)) return 0;
        if (at == hash) return array + (uint64_t)mid * stride;
        if (at < hash) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

// The class named name, as the engine's class lookup finds it: 0 where there is none.
inline uint64_t SslClassFind(const SslHost &h, const char *name)
{
    const uint32_t hash = SslOat(name);
    uint64_t reg = 0, list = 0, pairs = 0, modules = 0;
    uint32_t pairCount = 0, moduleCount = 0;
    if (!h.m.get(h.classes, reg) || !reg || !h.m.get(reg + kSslModules, list) || !list) return 0;
    h.m.get(list + kSslPairs, pairs);
    h.m.get(list + kSslPairCount, pairCount);
    h.m.get(list + kSslModuleArray, modules);
    h.m.get(list + kSslModuleCount, moduleCount);
    uint64_t module = 0;
    const uint64_t pair = SslSorted(h, pairs, pairCount, 8, hash);
    uint32_t moduleName = 0;
    char want[kSslMostName] = "";
    if (pair && h.m.get(pair + 4, moduleName) && SslName(h, moduleName, want, sizeof want) && *want && modules && moduleCount <= 1u << 16)
    {
        // the modules sorted by name, as strcmp sorts
        uint32_t lo = 0, hi = moduleCount;
        for (int i = 0; lo < hi && i < kSslMostSearch; i++)
        {
            const uint32_t mid = (lo + hi) / 2;
            uint32_t off = 0;
            char at[kSslMostName] = "";
            if (!h.m.get(modules + (uint64_t)mid * kSslModuleSize, off) || !SslName(h, off, at, sizeof at)) break;
            const int c = strcmp(at, want);
            if (!c)
            {
                module = modules + (uint64_t)mid * kSslModuleSize;
                break;
            }
            if (c < 0) lo = mid + 1;
            else hi = mid;
        }
    }
    uint64_t array = 0;
    uint32_t count = 0;
    if (module)
    {
        h.m.get(module + kSslModuleClasses, array);
        h.m.get(module + kSslModuleClassCount, count);
    }
    else
    {
        h.m.get(reg + kSslGlobalClasses, array);
        h.m.get(reg + kSslGlobalClassCount, count);
    }
    const uint64_t entry = SslSorted(h, array, count, 16, hash);
    uint64_t cls = 0;
    if (!entry || !h.m.get(entry + 8, cls)) return 0;
    return cls;
}

// The index of the member whose name hashes to hash, in cls or its bases; -1 where none.
inline int SslMemberIndex(const SslHost &h, uint64_t cls, uint32_t hash)
{
    for (int depth = 0; cls && depth < kSslMostBases; depth++)
    {
        uint64_t pairs = 0;
        uint32_t count = 0;
        h.m.get(cls + kSslMemberPairs, pairs);
        h.m.get(cls + kSslMemberPairCount, count);
        const uint64_t pair = SslSorted(h, pairs, count, 8, hash);
        int32_t index = -1;
        if (pair && h.m.get(pair + 4, index)) return index;
        if (!h.m.get(cls + kSslClassBase, cls)) return -1;
    }
    return -1;
}

// Member index of cls is named name (its lookup gives index and its entry's own name is name), of kind where kind >= 0:
// its object in obj. why: the first that is not so.
inline bool SslMemberIs(const SslHost &h, uint64_t cls, const char *clsName, const char *name, int index, int kind, uint64_t *obj, char *why, size_t whySize)
{
    const int found = SslMemberIndex(h, cls, SslOat(name));
    if (index < 0) index = found;
    int32_t count = 0;
    uint64_t table = 0;
    uint32_t nameOff = 0;
    uint8_t k = 0;
    char named[kSslMostName] = "";
    if (found < 0 || found != index)
    {
        snprintf(why, whySize, "%s.%s is %s, not member %d", clsName, name, found < 0 ? "not found" : "another member", index);
        return false;
    }
    if (!h.m.get(cls + kSslMemberCount, count) || index >= count || !h.m.get(cls + kSslMembers, table) || !table ||
        !h.m.get(table + (uint64_t)index * kSslEntrySize, nameOff) || !SslName(h, nameOff, named, sizeof named) || strcmp(named, name))
    {
        snprintf(why, whySize, "%s's member %d is not named %s", clsName, index, name);
        return false;
    }
    if (!h.m.get(table + (uint64_t)index * kSslEntrySize + 4, k) || (kind >= 0 && k != kind))
    {
        snprintf(why, whySize, "%s.%s is of kind %d, not %d", clsName, name, k, kind);
        return false;
    }
    if (obj && (!h.m.get(table + (uint64_t)index * kSslEntrySize + 8, *obj) || !*obj))
    {
        snprintf(why, whySize, "%s.%s has no function object", clsName, name);
        return false;
    }
    return true;
}

// ---- the two functions ----
// The view's container (its member 21), the status and the runner in the container's class (153 and 143), and the
// visibility every display object has from UiDisplayObjectBase (24).
static const int kSslContainer = 21, kSslStatus = 153, kSslRunner = 143, kSslVisible = 24;
static const int kSslMostWords = 64;
struct SslCheck
{
    int at, op, operand;
};
struct SslFunction
{
    const char *name;
    uint32_t count;      // the stock word count (+ 0x68)
    int field;           // the container's element it lights
    int retOperand;      // the last word's ret
    SslCheck checks[4];  // more words the stock function has where ours count on it
};
enum { kSslStatusText, kSslRunnerAnimation, kSslFunctions };
static const SslFunction kSslFunctionsOf[kSslFunctions] = {
    { "UpdateStatusText", 41, kSslStatus, 4, { { 33, kSslLocal, 0 }, { 34, kSslGetField, kSslContainer }, { 35, kSslGetField, kSslStatus }, { 38, kSslSetField, 97 } } },
    { "UpdateRunnerAnimation", 18, kSslRunner, 2, { { 1, kSslLocal, 1 }, { 11, kSslLocal, 0 }, { 13, kSslCall, 32 }, { 16, kSslPop, 0 } } } };

// Each stock word as the check wants it; why the first that is not.
inline bool SslStockCheck(const SslFunction &f, const uint32_t *words, uint32_t count, char *why, size_t whySize)
{
    if (count != f.count)
    {
        snprintf(why, whySize, "%s has %u words, not %u", f.name, count, f.count);
        return false;
    }
    for (const SslCheck &c : f.checks)
        if (words[c.at] != SslWord(c.op, c.operand))
        {
            snprintf(why, whySize, "%s's word %d is %d %d, not %d %d", f.name, c.at, SslOp(words[c.at]), SslOperand(words[c.at]), c.op, c.operand);
            return false;
        }
    if (words[count - 1] != SslWord(kSslRet, f.retOperand))
    {
        snprintf(why, whySize, "%s's last word is %d %d, not ret %d", f.name, SslOp(words[count - 1]), SslOperand(words[count - 1]), f.retOperand);
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
        if (SslIsJump(words[i]) && (SslOperand(words[i]) < 0 || (uint32_t)SslOperand(words[i]) >= count))
        {
            snprintf(why, whySize, "%s's word %u jumps out of it", f.name, i);
            return false;
        }
    return true;
}

// An array of ours into out (at most kSslMostWords): value (bool 0 or 1, or local 1) to the element's visible, then
// for stockToo the stock words with their jumps moved past ours, else ret. Its word count.
inline uint32_t SslBuild(const SslFunction &f, uint32_t value, bool stockToo, const uint32_t *stock, uint32_t count, uint32_t *out)
{
    const uint32_t prefix[6] = { value, SslWord(kSslLocal, 0), SslWord(kSslGetField, kSslContainer), SslWord(kSslGetField, f.field), SslWord(kSslSetField, kSslVisible),
                                 SslWord(kSslPop, 0) };
    memcpy(out, prefix, sizeof prefix);
    if (!stockToo)
    {
        out[6] = SslWord(kSslRet, f.retOperand);
        return 7;
    }
    for (uint32_t i = 0; i < count; i++) out[6 + i] = SslIsJump(stock[i]) ? SslWord(SslOp(stock[i]), SslOperand(stock[i]) + 6) : stock[i];
    return 6 + count;
}

// ---- the state and the step ----
enum { kGearDisplayGame, kGearDisplayDark, kGearDisplayLight }; // what is in place: the stock, ours dark, ours light
struct SslArray
{
    uint64_t at;
    uint32_t count;
};
struct SslFunctionState
{
    uint64_t obj, stock, stockSize;  // the function object, its stock array and the count and capacity it had
    uint32_t words[kSslMostWords];   // the stock words the arrays were made from
    uint32_t count;
    SslArray dark, light;
    int now;                         // kGearDisplay*: what is in place
};
struct GearDisplay
{
    SslFunctionState fn[kSslFunctions];
    int made;           // arrays made this session (at most 4 for the first stock, more only for a stock with other words)
    bool everDark;      // dark was in place this session: the elements may still be hidden
    char refused[200];  // the last refusal said, so it is said once
};
static const int kSslMostMade = 8;

// Two fields of a function object in the order that never pairs a count with a shorter array (the Reviewer's should
// 3): growing, the pointer first; shrinking, the count and capacity first (one 8-byte store).
inline bool SslSwap(const SslHost &h, uint64_t obj, uint32_t was, const SslArray &to)
{
    const uint64_t size = (uint64_t)to.count | (uint64_t)to.count << 32;
    if (to.count > was) return h.store64(obj + kSslCode, to.at) && h.store64(obj + kSslCodeCount, size);
    return h.store64(obj + kSslCodeCount, size) && h.store64(obj + kSslCode, to.at);
}

// One look (at each truck change and each change of the setting or the gearbox): want kGearDisplayDark or
// kGearDisplayLight. Light where nothing of ours was ever in place leaves the game's own. Every name and word is
// checked before any write, for both functions; nothing is written where one check fails. line: what happened, ""
// where nothing changed and nothing new was refused. Returns what is in place now (kGearDisplay*), or -1 where it
// was refused.
inline int GearDisplayStep(const SslHost &h, GearDisplay &g, int want, char *line, size_t size)
{
    line[0] = 0;
    char why[200] = "";
    auto refuse = [&](const char *text) {
        if (strcmp(g.refused, text)) snprintf(line, size, "gear display: left as it is, %s", text);
        strcpy_s(g.refused, text);
        return -1;
    };
    if (want == kGearDisplayLight && !g.everDark && g.fn[0].now == kGearDisplayGame && g.fn[1].now == kGearDisplayGame && !g.made) return kGearDisplayGame;
    const uint64_t view = SslClassFind(h, "HudTransmissionUiView"), lib = SslClassFind(h, "UiHudLibHudtransmission"), base = SslClassFind(h, "UiDisplayObjectBase");
    if (!view || !lib || !base)
        return refuse(!view ? "the class HudTransmissionUiView is not found (the scripts not loaded yet?)" : !lib ? "the class UiHudLibHudtransmission is not found"
                                                                                                              : "the class UiDisplayObjectBase is not found");
    uint64_t obj[kSslFunctions] = {};
    if (!SslMemberIs(h, view, "HudTransmissionUiView", "container", kSslContainer, -1, nullptr, why, sizeof why) ||
        !SslMemberIs(h, lib, "UiHudLibHudtransmission", "status", kSslStatus, -1, nullptr, why, sizeof why) ||
        !SslMemberIs(h, lib, "UiHudLibHudtransmission", "runner", kSslRunner, -1, nullptr, why, sizeof why) ||
        !SslMemberIs(h, base, "UiDisplayObjectBase", "visible", kSslVisible, -1, nullptr, why, sizeof why))
        return refuse(why);
    for (int i = 0; i < kSslFunctions; i++)
        if (!SslMemberIs(h, view, "HudTransmissionUiView", kSslFunctionsOf[i].name, -1, kSslKindScript, &obj[i], why, sizeof why)) return refuse(why);
    // what each object holds now: ours, its stock as seen, or new (a reload, or the first look)
    uint64_t at[kSslFunctions] = {};
    uint32_t count[kSslFunctions] = {};
    uint64_t sizeNow[kSslFunctions] = {};
    bool fresh[kSslFunctions] = {};
    uint32_t words[kSslFunctions][kSslMostWords] = {};
    for (int i = 0; i < kSslFunctions; i++)
    {
        SslFunctionState &f = g.fn[i];
        if (!h.m.get(obj[i] + kSslCode, at[i]) || !h.m.get(obj[i] + kSslCodeCount, sizeNow[i]) || !at[i]) return refuse("a function object's code is not read");
        count[i] = (uint32_t)sizeNow[i];
        const bool ours = obj[i] == f.obj && ((f.dark.at && at[i] == f.dark.at) || (f.light.at && at[i] == f.light.at));
        if (ours || (obj[i] == f.obj && at[i] == f.stock)) continue;
        // new: the stock words read and checked (the Reviewer's should 4: exactly count words)
        fresh[i] = true;
        if (count[i] > kSslMostWords || (uint32_t)(sizeNow[i] >> 32) < count[i])
        {
            snprintf(why, sizeof why, "%s has %u words, not %u", kSslFunctionsOf[i].name, count[i], kSslFunctionsOf[i].count);
            return refuse(why);
        }
        for (uint32_t k = 0; k < count[i]; k++)
            if (!h.m.get(at[i] + 4ull * k, words[i][k])) return refuse("a function's words are not read");
        if (!SslStockCheck(kSslFunctionsOf[i], words[i], count[i], why, sizeof why)) return refuse(why);
    }
    // a new stock: the arrays made from it, once a session, again only where its words differ
    for (int i = 0; i < kSslFunctions; i++)
    {
        if (!fresh[i]) continue;
        SslFunctionState &f = g.fn[i];
        const bool same = f.dark.at && f.count == count[i] && !memcmp(f.words, words[i], 4ull * count[i]);
        f.obj = obj[i];
        f.stock = at[i];
        f.stockSize = sizeNow[i];
        f.now = kGearDisplayGame;
        if (same) continue;
        f.dark = f.light = SslArray{};
        f.count = count[i];
        memcpy(f.words, words[i], 4ull * count[i]);
    }
    for (int i = 0; i < kSslFunctions; i++)
    {
        SslFunctionState &f = g.fn[i];
        if (at[i] == f.dark.at) f.now = kGearDisplayDark;
        else if (at[i] == f.light.at) f.now = kGearDisplayLight;
    }
    // where it already is
    if (g.fn[0].now == want && g.fn[1].now == want)
    {
        g.refused[0] = 0;
        return want;
    }
    if (want == kGearDisplayLight && !g.everDark && g.fn[0].now != kGearDisplayDark && g.fn[1].now != kGearDisplayDark) return kGearDisplayGame;
    // the arrays, on this thread (the mod's own: the engine's global heap, the Researcher's addendum 3)
    for (int i = 0; i < kSslFunctions; i++)
    {
        SslFunctionState &f = g.fn[i];
        if (f.dark.at) continue;
        uint32_t dark[kSslMostWords + 8], light[kSslMostWords + 8];
        const SslFunction &spec = kSslFunctionsOf[i];
        const uint32_t nDark = SslBuild(spec, i == kSslStatusText ? SslWord(kSslBool, 0) : SslWord(kSslLocal, 1), i != kSslStatusText, f.words, f.count, dark);
        const uint32_t nLight = SslBuild(spec, SslWord(kSslBool, 1), true, f.words, f.count, light);
        if (g.made + 2 > kSslMostMade) return refuse("the arrays were made too often this start");
        const uint64_t a = h.alloc(4ull * nDark), b = h.alloc(4ull * nLight);
        g.made += (a ? 1 : 0) + (b ? 1 : 0);
        if (!a || !b || !h.m.poke(a, dark, 4ull * nDark) || !h.m.poke(b, light, 4ull * nLight)) return refuse("the engine's allocate gave no room");
        f.dark = SslArray{ a, nDark };
        f.light = SslArray{ b, nLight };
    }
    // before any write, each object holds what was read (else nothing written, said once)
    for (int i = 0; i < kSslFunctions; i++)
    {
        uint64_t again = 0;
        if (!h.m.get(obj[i] + kSslCode, again) || again != at[i]) return refuse("a function's code changed while it was looked at");
    }
    for (int i = 0; i < kSslFunctions; i++)
    {
        SslFunctionState &f = g.fn[i];
        const SslArray &to = want == kGearDisplayDark ? f.dark : f.light;
        if (at[i] == to.at) continue;
        if (!SslSwap(h, obj[i], count[i], to)) return refuse("a function's code could not be written");
        f.now = want;
    }
    if (want == kGearDisplayDark) g.everDark = true;
    g.refused[0] = 0;
    if (want == kGearDisplayDark)
        snprintf(line, size, "gear display: dark, %s and %s swapped (%u and %u words checked; ours %u and %u words): the text goes and the dot shows only while LB is held, "
                             "from the view's next update",
                 kSslFunctionsOf[0].name, kSslFunctionsOf[1].name, g.fn[0].count, g.fn[1].count, g.fn[0].dark.count, g.fn[1].dark.count);
    else
        snprintf(line, size, "gear display: the game's again, %s and %s on the light arrays (visible set, then the stock words), from the view's next update",
                 kSslFunctionsOf[0].name, kSslFunctionsOf[1].name);
    return want;
}
