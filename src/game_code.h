// game_code.h: the game's own gearbox functions, found in the exe's code by their bytes. The mod calls ShiftGear with
// Gearbox=1 (TakeoverStep) and only looks for the others, once a truck is driven, and says in the log where each one is, whether its bytes are there more than once, and whether another
// mod holds it: a hook puts a jump over a function's first bytes, so its pattern no longer matches there, and the
// rest of it still does. Nothing here writes to the game.
//
// Each pattern is the function's first bytes as the game's build of 22 July 2026 has them, "?" for a byte that
// changes from build to build (a call's or a jump's distance). ShiftToNeutral is looked for between ShiftToHigh and
// ShiftToReverse, where it stands in the exe, as its few bytes are found in many places; without both of their places
// it is not looked for.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

struct GameFunction { const char *name, *pattern; };
enum { kFnShiftGear, kFnShiftToAuto, kFnShiftToHigh, kFnShiftToReverse, kFnShiftToNeutral, kFnMaxGear, kFnAutoOffAndShift, kFnSetPower, kFnSetVehicle,
       kFnCount };
static const GameFunction kGameFunctions[kFnCount] = {
    { "ShiftGear", "48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 48 8B 81 48 01 00 00 48 8B F1 48 B9 FF FF FF FF FF FF 00 FF 8B FA 48 23 C1 74" },
    { "ShiftToAutoGear", "40 57 48 81 EC 80 00 00 00 48 8B 41 68 48 8B F9 48 89 9C 24 90 00 00 00 0F 29 7C 24 60 C6 40 3C 01 48 8B 41 60 48 8B 90 30 02 00 00 "
                         "0F 10 8A 30 02 00 00 0F 59 8A 70 01 00 00" },
    { "ShiftToHigh", "40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B CB 8D 50 01 48 83 C4 20 5B" },
    { "ShiftToReverse", "BA FF FF FF FF E9 ? ? ? ? CC" },
    { "ShiftToNeutral", "33 D2 E9 ? ? ? ? CC" },
    { "GetMaxGear", "48 8B 41 68 48 8B 50 58 48 8B 48 60 48 3B D1 75 ? 33 C0 C3 48 2B CA 48 C1 F9 02 8D 41 FE" },
    { "AutoOffAndShift", "48 8B 41 68 C6 40 3C 00 E9" },
    { "SetPowerFactor", "48 8B 41 68 F3 0F 11 48 38 C3" },
    { "SetCurrentVehicle", "48 8B C4 53 57 48 81 EC 98 00 00 00 48 8B FA 48 8B D9 48 39 51 08 0F 84 ? ? ? ? 48 89 68 E8 48 83 C1 70 48 89 70 E0 4C 89 70 D8 4C 89 78 D0" } };

static const int kPatternMax = 96;
// What a hook may put over a function's start: a jump (E9 and its distance, or FF 25 and an address) and int3 (CC)
// over the rest of the instructions it moved away. A pattern needs kHookLeft bytes after that to be told by.
static const int kHookSpan = 16, kHookLeft = 8;

// A pattern as bytes and a mask (1 where the byte counts). Its length, or 0 when it is not a pattern.
inline int PatternParse(const char *text, uint8_t *bytes, uint8_t *mask)
{
    int n = 0;
    for (const char *p = text; *p;)
    {
        if (*p == ' ') { p++; continue; }
        if (n >= kPatternMax) return 0;
        if (*p == '?')
        {
            bytes[n] = 0;
            mask[n++] = 0;
            p++;
            continue;
        }
        auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        const int high = hex(p[0]), low = p[1] ? hex(p[1]) : -1;
        if (high < 0 || low < 0) return 0;
        bytes[n] = (uint8_t)(high * 16 + low);
        mask[n++] = 1;
        p += 2;
    }
    return n;
}

inline bool PatternAt(const uint8_t *at, const uint8_t *bytes, const uint8_t *mask, int length)
{
    for (int i = 0; i < length; i++)
        if (mask[i] && at[i] != bytes[i]) return false;
    return true;
}

// The byte of a pattern to look for first: one that counts and is not among the most common bytes of x64 code, so
// memchr skips most of the code; the first that counts where all are common. -1 when none counts.
inline int PatternAnchor(const uint8_t *bytes, const uint8_t *mask, int length)
{
    static const uint8_t common[] = { 0x00, 0xFF, 0x48, 0x8B, 0x89, 0x24, 0x83, 0x0F, 0xCC, 0x4C, 0x8D, 0xE8, 0x44, 0xC3, 0x74, 0x75, 0x90 };
    int first = -1;
    for (int i = 0; i < length; i++)
    {
        if (!mask[i]) continue;
        if (first < 0) first = i;
        if (!memchr(common, bytes[i], sizeof common)) return i;
    }
    return first;
}

// Where in code[from, to) the pattern stands first, and how often it stands there (counted up to 9). False when it
// does not.
inline bool PatternFind(const uint8_t *code, size_t from, size_t to, const uint8_t *bytes, const uint8_t *mask, int length, size_t &at, int &count)
{
    count = 0;
    const int k = PatternAnchor(bytes, mask, length);
    if (length <= 0 || k < 0 || to < from + (size_t)length) return false;
    const uint8_t *p = code + from + k, *end = code + (to - length) + k + 1;
    while (p < end && count < 9)
    {
        p = (const uint8_t *)memchr(p, bytes[k], (size_t)(end - p));
        if (!p) break;
        const size_t i = (size_t)(p - code) - k;
        if (PatternAt(code + i, bytes, mask, length))
        {
            if (!count) at = i;
            count++;
        }
        p++;
    }
    return count > 0;
}

// The sections of an image (headers: how many of its first bytes are there to read), or nullptr.
inline const IMAGE_SECTION_HEADER *ImageSections(const uint8_t *image, size_t headers, int &count)
{
    count = 0;
    if (headers < 0x40) return nullptr;
    uint32_t lfanew = 0;
    memcpy(&lfanew, image + 0x3C, 4);
    if ((size_t)lfanew + sizeof(IMAGE_NT_HEADERS64) > headers) return nullptr;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(image + lfanew);
    const IMAGE_SECTION_HEADER *first = IMAGE_FIRST_SECTION(nt);
    if ((const uint8_t *)(first + nt->FileHeader.NumberOfSections) > image + headers) return nullptr;
    count = nt->FileHeader.NumberOfSections;
    return first;
}

// The exe's code to look in, by RVA: from the first byte of its first code section to the end of its last. Steam's DRM
// stub (the section .bind, code as well) is left out: none of the game's code is in it, and with it the span took in
// the exe's data too (the code review of 2026-10-23). False where there is no code.
inline bool CodeRangeOf(const uint8_t *image, size_t headers, size_t &from, size_t &to)
{
    from = SIZE_MAX;
    to = 0;
    int count = 0;
    const IMAGE_SECTION_HEADER *section = ImageSections(image, headers, count);
    for (int i = 0; section && i < count; i++, section++)
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) && memcmp(section->Name, ".bind", 5) != 0)
        {
            from = section->VirtualAddress < from ? section->VirtualAddress : from;
            const size_t end = (size_t)section->VirtualAddress + section->Misc.VirtualSize;
            to = end > to ? end : to;
        }
    return from < to;
}

// Whether an RVA lies in a section of the image that is not code (its data or its read-only data), where a static of
// the game's or a class's method table has to be: a single signature that stands once in another build could point
// anywhere.
inline bool RvaInDataOf(const uint8_t *image, size_t headers, uint64_t rva)
{
    int count = 0;
    const IMAGE_SECTION_HEADER *section = ImageSections(image, headers, count);
    for (int i = 0; section && i < count; i++, section++)
        if (rva >= section->VirtualAddress && rva < (uint64_t)section->VirtualAddress + section->Misc.VirtualSize)
            return !(section->Characteristics & IMAGE_SCN_MEM_EXECUTE);
    return false;
}

// What became of a function in the code.
enum { kFnNotFound, kFnFound, kFnHeld, kFnUnplaced };
struct FunctionFind
{
    int state;
    size_t at;     // its offset in the code (the exe's address of it, less the exe's base)
    int count;     // how often its pattern stands there
    bool tooShort; // its pattern is too short to tell it behind a hook's jump
};

// Looks for every function in code, the exe's image of size bytes, between from and to (its code). A function not
// found is looked for again with its first bytes left out, up to kHookSpan of them: found so, behind a jump (E9, or
// FF 25), another mod holds it.
inline void GameFunctionsFind(const uint8_t *code, size_t from, size_t to, FunctionFind out[kFnCount])
{
    for (int f = 0; f < kFnCount; f++)
    {
        uint8_t bytes[kPatternMax], mask[kPatternMax];
        const int length = PatternParse(kGameFunctions[f].pattern, bytes, mask);
        size_t lo = from, hi = to;
        FunctionFind &r = out[f];
        r = FunctionFind{};
        if (f == kFnShiftToNeutral)
        {
            const FunctionFind &high = out[kFnShiftToHigh], &reverse = out[kFnShiftToReverse];
            if (high.state == kFnNotFound || reverse.state == kFnNotFound || high.at >= reverse.at)
            {
                r.state = kFnUnplaced;
                continue;
            }
            lo = high.at;
            hi = reverse.at;
        }
        if (PatternFind(code, lo, hi, bytes, mask, length, r.at, r.count))
        {
            r.state = kFnFound;
            continue;
        }
        // behind a jump: the rest of the pattern, when enough of it is left to tell the function by
        const int span = length - kHookLeft < kHookSpan ? length - kHookLeft : kHookSpan;
        r.tooShort = span < 5;
        if (r.tooShort) continue;
        uint8_t rest[kPatternMax];
        memcpy(rest, mask, sizeof rest);
        memset(rest, 0, (size_t)span);
        size_t at = 0;
        int count = 0;
        if (PatternFind(code, lo, hi, bytes, rest, length, at, count) && (code[at] == 0xE9 || (code[at] == 0xFF && code[at + 1] == 0x25)))
        {
            r.state = kFnHeld;
            r.at = at;
            r.count = count;
        }
    }
}

// ---- the game's damage path: its truck update and its damage update ----
// As the tire mod deals tire damage (its build_find.h, the same patterns): the damage update (one argument, the
// vehicle) sums the parts' damage at vehicle + 0x108, sets their effects and tells the truck's script, which brings up
// the game's truck card (RVA 0xd6f690 in the Steam build 6a607c05); the truck update runs on the game's thread every
// frame for the driven truck, its first argument an object with the vehicle at + 0x20 (RVA 0xa56970 there). It is
// found by its call of GetForegroundWindow, then its first bytes, the nearest ones before that call that follow int3
// padding.
static const char *const kDamageUpdatePattern = "48 8B C4 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 C7 40 98 FE FF FF FF 48 89 58 18 "
                                                "0F 29 70 B8 0F 29 78 A8 4C 8B E9 48 8D B9 50 01 00 00 48 8B CF E8";
static const char *const kTruckUpdateCallPattern = "40 B7 01 40 88 7C 24 50 44 0F B6 FF FF 15 ? ? ? ? 48 8B D8 E8 ? ? ? ? 48 3B C3 74";
static const char *const kTruckUpdateHeadPattern = "CC 48 8B C4 F3 0F 11 58 20 F3 0F 11 50 18 48 89 50 10 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57";
static const size_t kTruckUpdateMost = 0x4000; // how far before the call its first bytes may stand

struct DamagePath
{
    size_t truckUpdate, damageUpdate; // offsets in the code, 0 = not found
    const char *why;                  // what was not found (a string literal), "" when both were
};

inline bool DamagePathFind(const uint8_t *code, size_t from, size_t to, DamagePath &out)
{
    out = DamagePath{};
    out.why = "";
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    size_t at = 0;
    int count = 0;
    int length = PatternParse(kDamageUpdatePattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        out.why = count > 1 ? "the damage update stands more than once" : "the damage update was not found";
        return false;
    }
    out.damageUpdate = at;
    length = PatternParse(kTruckUpdateCallPattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        out.why = count > 1 ? "the truck update's call of GetForegroundWindow stands more than once" : "the truck update's call of GetForegroundWindow was not found";
        out.damageUpdate = 0;
        return false;
    }
    const size_t call = at;
    length = PatternParse(kTruckUpdateHeadPattern, bytes, mask);
    for (size_t head = call; head > from && call - head < kTruckUpdateMost; head--)
        if (PatternAt(code + head - 1, bytes, mask, length))
        {
            out.truckUpdate = head;
            return true;
        }
    out.why = "the truck update's first bytes were not found before its call";
    out.damageUpdate = 0;
    return false;
}

// ---- the game's wheels' drive, for the creep (models.h, CreepShare) ----
// Once a physics step the game hands the drive throttle it made from the pedal to its wheels' drive: movss xmm2,[rbp+8]
// (the step's seconds); mov rcx,rbx (the action); movss xmm1,[rsp+150h] (the throttle); call. In the 6ab52f5d image the
// call is at c41d40 and the drive at c41da0, in 6a607c05 at c40270 and c402d0; it is the drive's only call there. The
// call's target has to be a function's start (int3 padding before it) with the drive's first bytes, and the bytes
// before the call have to stand once in the code.
static const char *const kDriveCallPattern = "F3 0F 10 55 08 48 8B CB F3 0F 10 8C 24 50 01 00 00 E8 ? ? ? ?";
static const char *const kDriveHeadPattern = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 48 C7 45 ? FE FF FF FF";

struct DrivePlace
{
    size_t call, drive; // offsets in the code, 0 = not found
    const char *why;    // what was not found (a string literal), "" when both were
};

inline bool DrivePlaceFind(const uint8_t *code, size_t from, size_t to, DrivePlace &out)
{
    out = DrivePlace{};
    out.why = "";
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    size_t at = 0;
    int count = 0;
    int length = PatternParse(kDriveCallPattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        out.why = count > 1 ? "the call of the wheels' drive stands more than once" : "the call of the wheels' drive was not found";
        return false;
    }
    const size_t call = at + (size_t)length - 5;
    int32_t distance = 0;
    memcpy(&distance, code + call + 1, sizeof distance);
    const int64_t target = (int64_t)call + 5 + distance;
    length = PatternParse(kDriveHeadPattern, bytes, mask);
    if (target <= (int64_t)from || target + length > (int64_t)to)
    {
        out.why = "the call of the wheels' drive goes outside the game's code";
        return false;
    }
    const size_t drive = (size_t)target;
    if (code[drive - 1] != 0xCC)
    {
        out.why = "the call of the wheels' drive does not go to a function's start";
        return false;
    }
    if (!PatternAt(code + drive, bytes, mask, length))
    {
        out.why = code[drive] == 0xE9 || (code[drive] == 0xFF && code[drive + 1] == 0x25) ? "another mod holds the wheels' drive"
                                                                                       : "the wheels' drive does not start as it should";
        return false;
    }
    out.call = call;
    out.drive = drive;
    return true;
}

// ---- the game's engine speed, for the mod's own (BRIEF_drivetrain_1_engine_speed.md) ----
// float(vehicle, bool withWheels, bool withMinThrottle): the engine speed 0 to 1 the game works out on each call from
// the pedal, its pull (action + 0xB4) and the driven wheels, stored nowhere (review\own_drivetrain_research.md B; RVA
// d73170 in 6adb8627, d73200 in 6ab52f5d). Eleven callers read it. Two of them keep the game's own value (the Reviewer's
// addendum, M1 and M2): the fuel burn, where it is the engine's load, and the stall's near-dead check. The hook tells
// them by the address their call returns to, from the bytes around each call; each call has to land on the function.
static const char *const kEngineSpeedPattern = "48 89 6C 24 18 56 48 81 EC 80 00 00 00 F6 81 68 07 00 00 01 0F B6 F2 48 8B E9 75 14 0F 57 C0 "
                                               "48 8B AC 24 A0 00 00 00 48 81 C4 80 00 00 00 5E C3";
static const char *const kEngineFuelCallPattern = "41 B0 01 F3 44 0F 10 9B 1C 01 00 00 33 D2 F3 44 0F 59 1D ? ? ? ? 48 8B CB 44 0F 29 64 24 20 E8 ? ? ? ? 48 8D 8B 50 01 00 00";
static const char *const kEngineStallCallPattern = "45 33 C0 B2 01 48 8B 4E 20 E8 ? ? ? ? 0F 28 F8 0F 57 F6 48 8B CF E8";
static const size_t kEngineFuelCallAt = 0x20, kEngineStallCallAt = 9; // the call's place in each pattern

struct EngineSpeedPlace
{
    size_t speed;              // the function's offset in the code, 0 = not found
    size_t fuelBack, stallBack; // where the fuel's call and the stall's call return to (the byte after each call)
    const char *why;           // what was not found (a string literal), "" when all three were
};

// The place a call at code[call] (E8 and its distance) goes to, or SIZE_MAX outside from to to.
inline size_t CallTarget(const uint8_t *code, size_t from, size_t to, size_t call)
{
    if (code[call] != 0xE8) return SIZE_MAX;
    int32_t distance = 0;
    memcpy(&distance, code + call + 1, sizeof distance);
    const int64_t target = (int64_t)call + 5 + distance;
    return target > (int64_t)from && target < (int64_t)to ? (size_t)target : SIZE_MAX;
}

inline bool EngineSpeedFind(const uint8_t *code, size_t from, size_t to, EngineSpeedPlace &out)
{
    out = EngineSpeedPlace{};
    out.why = "";
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    size_t at = 0;
    int count = 0;
    int length = PatternParse(kEngineSpeedPattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        if (count > 1)
        {
            out.why = "the engine speed's bytes stand more than once";
            return false;
        }
        // behind a jump: the rest of its bytes, after the span a hook covers
        uint8_t rest[kPatternMax];
        memcpy(rest, mask, sizeof rest);
        memset(rest, 0, (size_t)kHookSpan);
        out.why = PatternFind(code, from, to, bytes, rest, length, at, count) && count == 1 && (code[at] == 0xE9 || (code[at] == 0xFF && code[at + 1] == 0x25))
                      ? "another mod holds the engine speed" : "the engine speed was not found";
        return false;
    }
    const size_t speed = at;
    struct Site { const char *pattern; size_t callAt; size_t *back; const char *missing, *twice, *elsewhere; };
    const Site sites[2] = {
        { kEngineFuelCallPattern, kEngineFuelCallAt, &out.fuelBack, "the fuel's call of the engine speed was not found", "the fuel's call of the engine speed stands more than once",
          "the fuel's call does not go to the engine speed" },
        { kEngineStallCallPattern, kEngineStallCallAt, &out.stallBack, "the stall's call of the engine speed was not found", "the stall's call of the engine speed stands more than once",
          "the stall's call does not go to the engine speed" } };
    for (const Site &site : sites)
    {
        length = PatternParse(site.pattern, bytes, mask);
        if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
        {
            out.why = count > 1 ? site.twice : site.missing;
            out.fuelBack = out.stallBack = 0;
            return false;
        }
        if (CallTarget(code, from, to, at + site.callAt) != speed)
        {
            out.why = site.elsewhere;
            out.fuelBack = out.stallBack = 0;
            return false;
        }
        *site.back = at + site.callAt + 5;
    }
    out.speed = speed;
    return true;
}

// ---- the game's truck input, for the engine sound (BRIEF_drivetrain_1b_sound.md) ----
// Handle *(vehicle rcx, Handle *out rdx): the C++ behind Truck.GetTruckInput (b23fa0 in 6adb8627, b24030 in 6ab52f5d),
// found once by its first bytes. The helper that resolves a handle to its data block (b62290 and b62320) stands in five
// copies of one template, so its place is the target of the function's call at + 0x8F2, the same on both images.
static const char *const kTruckInputPattern = "48 8B C4 48 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 98 48 81 EC 68 01 00 00 48 C7 45 E8 FE FF FF FF "
                                              "0F 29 70 A8 0F 29 78 98 44 0F 29 40 88 4C 8B E2 48 8B F9 33 F6 89 74 24 48";
static const size_t kTruckInputHelperAt = 0x8F2;
struct TruckInputPlace
{
    size_t input;    // the function's offset in the code, 0 = not found
    size_t helper;   // the handle's helper, the target of the call at input + kTruckInputHelperAt
    const char *why; // what was not found (a string literal), "" when both were
};
inline bool TruckInputFind(const uint8_t *code, size_t from, size_t to, TruckInputPlace &out)
{
    out = TruckInputPlace{};
    out.why = "";
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    size_t at = 0;
    int count = 0;
    const int length = PatternParse(kTruckInputPattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        if (count > 1)
        {
            out.why = "the truck input's bytes stand more than once";
            return false;
        }
        // behind a jump: the rest of its bytes, after the span a hook covers
        uint8_t rest[kPatternMax];
        memcpy(rest, mask, sizeof rest);
        memset(rest, 0, (size_t)kHookSpan);
        out.why = PatternFind(code, from, to, bytes, rest, length, at, count) && count == 1 && (code[at] == 0xE9 || (code[at] == 0xFF && code[at + 1] == 0x25))
                      ? "another mod holds the truck input" : "the truck input was not found";
        return false;
    }
    const size_t call = at + kTruckInputHelperAt;
    const size_t helper = call + 5 <= to ? CallTarget(code, from, to, call) : SIZE_MAX;
    if (helper == SIZE_MAX)
    {
        out.why = "the truck input's call of its handle helper was not found";
        return false;
    }
    out.input = at;
    out.helper = helper;
    return true;
}

// ---- the diff lock's setter, for the bite (BRIEF_diff_lock_bite.md, the lead's addendum of 14:09) ----
// The script's isDiffLockEnabled setter (review\research_diff_lock_kind.md; d7d470 on 6adb8627, d7d500 on 6ab52f5d,
// d7bb70 on 6a607c05): it writes action + 0x4A only while vehicle + 0xE8 is 0. Its bytes hold both offsets and the 0
// test, so found once they say the lock kind sits at vehicle + 0xE8 on this build. Never called, never hooked.
static const char *const kLockSetterPattern = "83 B9 E8 00 00 00 00 75 0C 48 8B 41 68 38 50 4A 74 03 88 50 4A C3";
inline bool LockSetterFind(const uint8_t *code, size_t from, size_t to, size_t &out, const char *&why)
{
    out = 0;
    why = "";
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    size_t at = 0;
    int count = 0;
    const int length = PatternParse(kLockSetterPattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        why = count > 1 ? "the diff lock's setter's bytes stand more than once" : "the diff lock's setter was not found";
        return false;
    }
    out = at;
    return true;
}

// ---- the fitted gearbox's desc, for the positions by gearbox (BRIEF_positions_by_gearbox.md) ----
// const void *(vehicle, const std::string *name): the gearbox desc of that name among the vehicle's socket types, the
// lookup Truck.SetGearbox makes (review\research_low_range_fitted.md; d7d550 on 6adb8627, d7d5e0 on 6ab52f5d, d7bc50
// on 6a607c05), found once by its first bytes, which hold vehicle + 0x988. Only called, never hooked.
static const char *const kGearboxDescPattern = "40 57 48 83 EC 50 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 60 48 89 6C 24 68 48 89 74 24 70 48 8B EA "
                                               "48 8D 05 ? ? ? ? 48 89 44 24 28 48 8D 05 ? ? ? ? 48 89 44 24 40 33 FF 48 8B 99 88 09 00 00";
inline bool GearboxDescFind(const uint8_t *code, size_t from, size_t to, size_t &out, const char *&why)
{
    out = 0;
    why = "";
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    size_t at = 0;
    int count = 0;
    const int length = PatternParse(kGearboxDescPattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        why = count > 1 ? "the gearbox lookup's bytes stand more than once" : "the gearbox lookup was not found";
        return false;
    }
    out = at;
    return true;
}

// ---- the game's gear torque, for the torque cut (BRIEF_drivetrain_2_torque_cut.md) ----
// bool(vehicle, int gear, float *torque, float *start, float *a, float *top, float *gearSpeed): the gear's torque and
// its curve (review\own_drivetrain_research.md A; RVA d73dc0 in 6adb8627, d73e50 in 6ab52f5d), found by its first
// bytes, once on both images. Of its seven calls the cut answers only the drive's: found by walking the wheels' drive
// (DrivePlaceFind) from its head for the first call that lands on the gear torque (on 6adb8627 it returns to the drive's
// start + 0xCA). The stall's call passes as the others do (the Reviewer's M1), so it is not looked for.
static const char *const kGearTorquePattern = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 45 33 D2 48 63 FA 48 8B E9 4D 85 C0 74 03 45 89 10 "
                                              "4D 85 C9 74 03 45 89 11 48 8B 5C 24 28";
static const size_t kDriveCallSpan = 0x400; // how far into the drive its call of the gear torque may stand

struct GearTorquePlace
{
    size_t torque;    // the function's offset in the code, 0 = not found
    size_t driveBack; // where the drive's call returns to (the byte after it)
    const char *why;  // what was not found (a string literal), "" when both were
};

// drive: the wheels' drive's offset (DrivePlace::drive), 0 when it was not found.
inline bool GearTorqueFind(const uint8_t *code, size_t from, size_t to, size_t drive, GearTorquePlace &out)
{
    out = GearTorquePlace{};
    out.why = "";
    uint8_t bytes[kPatternMax], mask[kPatternMax];
    size_t at = 0;
    int count = 0;
    const int length = PatternParse(kGearTorquePattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        if (count > 1)
        {
            out.why = "the gear torque's bytes stand more than once";
            return false;
        }
        uint8_t rest[kPatternMax];
        memcpy(rest, mask, sizeof rest);
        memset(rest, 0, (size_t)kHookSpan);
        out.why = PatternFind(code, from, to, bytes, rest, length, at, count) && count == 1 && (code[at] == 0xE9 || (code[at] == 0xFF && code[at + 1] == 0x25))
                      ? "another mod holds the gear torque" : "the gear torque was not found";
        return false;
    }
    const size_t torque = at;
    if (!drive || drive < from || drive >= to)
    {
        out.why = "the wheels' drive was not found, so its call of the gear torque was not looked for";
        return false;
    }
    const size_t end = drive + kDriveCallSpan < to - 5 ? drive + kDriveCallSpan : to - 5;
    for (size_t i = drive; i < end; i++)
        if (code[i] == 0xE8 && CallTarget(code, from, to, i) == torque)
        {
            out.torque = torque;
            out.driveBack = i + 5;
            return true;
        }
    out.why = "the drive's call of the gear torque was not found";
    return false;
}

// The finds in a line of text for the log.
inline void GameFunctionsText(const FunctionFind finds[kFnCount], char *out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    for (int f = 0; f < kFnCount && n < size; f++)
    {
        const FunctionFind &r = finds[f];
        int w;
        if (r.state == kFnFound) w = snprintf(out + n, size - n, "%s%s at +%zx%s", f ? ", " : "", kGameFunctions[f].name, r.at, r.count > 1 ? " (more than once)" : "");
        else if (r.state == kFnHeld) w = snprintf(out + n, size - n, "%s%s at +%zx held by another mod", f ? ", " : "", kGameFunctions[f].name, r.at);
        else if (r.state == kFnUnplaced) w = snprintf(out + n, size - n, "%s%s not looked for (its place is between ShiftToHigh and ShiftToReverse)", f ? ", " : "", kGameFunctions[f].name);
        else w = snprintf(out + n, size - n, "%s%s not found%s", f ? ", " : "", kGameFunctions[f].name, r.tooShort ? " (too short to tell it behind a hook)" : "");
        if (w < 0) break;
        n += (size_t)w;
    }
}
