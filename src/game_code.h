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
