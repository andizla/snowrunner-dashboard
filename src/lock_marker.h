// lock_marker.h: the X of a locked axle for the mod's own locks (BRIEF_drive_1115_findings.md, part 206, with the
// Reviewer's addendum and review\research_diff_lock_marker.md).
//
// The game's drive schematic draws one row a driven axle and picks each row's texture by one byte, the action's lock
// (action + 0x4A, read as [r13 + 0x12] with r13 the action + 0x38). The mod's front lock leaves that byte at 0, so no row
// showed it. Two places of the drawer are patched once at the start, found by pattern with their bytes checked:
//   the row's pick, 12 bytes: mov rbx, r14; cmp byte [r13+12h], 0; cmovne rbx, r15 (r15 the lock-on texture, r14 the
//   off one). It jumps to a stub that answers on for a row the mod locks (its table, one byte a row, at most 16 rows,
//   by esi, the drawer's row counter), else by the game's byte as before. The stub changes rbx alone, and the flags as
//   the game's own cmp leaves them: that cmp comes last before the jump back.
//   the fade's stamp, 37 bytes: the drawer compares the lock byte with its last-seen copy and stamps the time on its
//   own clock when they differ, so the marks fade in. The stub does the same with "the game's byte or any mod lock",
//   at both the compare and the store, calling the game's clock as the drawer did. Nothing else reads the last-seen
//   byte (read on all three builds), and the call stands where the drawer's own call stood, so the registers the
//   drawer leaves to a call are the only ones it touches.
// The line colour stays the game's (picked once by the game's byte). No DirectX hook and no draw of the mod's own.
//
// The rows are numbered as the drawer numbers them (MarkRowsOf): the wheels in the wheel list's order, those not
// driven left out, those driven with all wheel drive only while it is on, each one in the first row whose first wheel
// stands within 0.5 m of it along the truck, else a new row. The drawer measures along by the truck's own X axis; the
// mod by the main body's forward axis (rowAt of AxleRowsStep). Both lie along the truck, and only the differences
// count, so the rows come out alike wherever two axles stand more than 0.5 m apart.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <initializer_list>
#include "game_code.h"

static const int kMarkRowsMost = 16;   // the table's rows; a row past them answers by the game's byte
static const float kMarkRowGap = 0.5f; // metres along the truck within which two wheels share a row (the drawer's 0.5)

// The pick with what stands around it: test r12b, r12b; je +0x0E; the pick; jmp +7. kMarkPickAt: the pick in the match.
static const char *const kMarkPickPattern = "45 84 E4 74 0E 49 8B DE 41 80 7D 12 00 49 0F 45 DF EB 07";
static const int kMarkPickAt = 5, kMarkPickLength = 12;
// The fade's compare and store: movzx eax, byte [r13+12h]; cmp [last], al; je +0x12; call clock; movsd [time], xmm0;
// movzx eax, byte [r13+12h]; mov [last], al.
static const char *const kMarkFadePattern = "41 0F B6 45 12 38 05 ? ? ? ? 74 12 E8 ? ? ? ? F2 0F 11 05 ? ? ? ? 41 0F B6 45 12 88 05 ? ? ? ?";
static const int kMarkFadeLength = 37;
static const int kMarkPickStub = 51, kMarkFadeStub = 64; // the stubs' sizes
static const int kMarkFadeAt = 64;                          // the fade's stub on the page, after the pick's

struct MarkPlan
{
    size_t pick, fade;                 // from the exe's base
    size_t last, time, clock;          // the drawer's last-seen lock byte, its change time and its clock, from the base
    uint8_t pickBytes[kMarkPickLength], fadeBytes[kMarkFadeLength]; // the game's own, as the copy holds them
    const char *why;                   // what is missing, where the plan is not made
};

inline int32_t MarkRel(const uint8_t *p)
{
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

// The two places in a copy of the exe (code[from, to) at the exe's own offsets), each found once with its bytes.
inline bool MarkPlanMake(const uint8_t *code, size_t from, size_t to, MarkPlan &p)
{
    p = MarkPlan{};
    uint8_t bytes[64], mask[64];
    size_t at = 0;
    int count = 0, length = PatternParse(kMarkPickPattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        p.why = count > 1 ? "the axle mark's row pick stands more than once" : "the axle mark's row pick is not found";
        return false;
    }
    p.pick = at + kMarkPickAt;
    length = PatternParse(kMarkFadePattern, bytes, mask);
    if (!PatternFind(code, from, to, bytes, mask, length, at, count) || count != 1)
    {
        p.why = count > 1 ? "the axle mark's fade stamp stands more than once" : "the axle mark's fade stamp is not found";
        return false;
    }
    p.fade = at;
    p.last = at + 11 + MarkRel(code + at + 7);
    p.clock = at + 18 + MarkRel(code + at + 14);
    p.time = at + 26 + MarkRel(code + at + 22);
    if (p.last != at + 37 + MarkRel(code + at + 33))
    {
        p.why = "the axle mark's fade stamp compares one byte and stores another";
        return false;
    }
    if (p.clock < from || p.clock >= to)
    {
        p.why = "the axle mark's fade stamp calls outside the exe's code";
        return false;
    }
    memcpy(p.pickBytes, code + p.pick, kMarkPickLength);
    memcpy(p.fadeBytes, code + p.fade, kMarkFadeLength);
    return true;
}

inline void MarkPut64(uint8_t *at, uint64_t v) { memcpy(at, &v, 8); }
inline bool MarkPutRel(uint8_t *at, uint64_t from, uint64_t to)
{
    const int64_t d = (int64_t)(to - from);
    if (d < INT32_MIN || d > INT32_MAX) return false;
    const int32_t v = (int32_t)d;
    memcpy(at, &v, 4);
    return true;
}

// The pick's stub, kMarkPickStub bytes, to stand at stubAt; back: the byte after the 12 of the pick; rows: the mod's
// table (kMarkRowsMost bytes, 1 locked).
//   cmp esi, 16; jae game; mov rbx, rows; cmp byte [rbx+rsi], 0; jne on
//   game: mov rbx, r14; cmp byte [r13+12h], 0; cmovne rbx, r15; jmp back
//   on:   mov rbx, r15; cmp byte [r13+12h], 0; jmp back
inline bool MarkPickStub(uint8_t *s, uint64_t stubAt, uint64_t back, uint64_t rows)
{
    static const uint8_t head[] = { 0x83, 0xFE, (uint8_t)kMarkRowsMost, 0x73, 0x10, 0x48, 0xBB };
    static const uint8_t look[] = { 0x80, 0x3C, 0x33, 0x00, 0x75, 0x11 };
    static const uint8_t game[] = { 0x49, 0x8B, 0xDE, 0x41, 0x80, 0x7D, 0x12, 0x00, 0x49, 0x0F, 0x45, 0xDF, 0xE9 };
    static const uint8_t on[] = { 0x49, 0x8B, 0xDF, 0x41, 0x80, 0x7D, 0x12, 0x00, 0xE9 };
    int n = 0;
    memcpy(s + n, head, sizeof head), n += sizeof head;
    MarkPut64(s + n, rows), n += 8;
    memcpy(s + n, look, sizeof look), n += sizeof look;
    memcpy(s + n, game, sizeof game), n += sizeof game;
    if (!MarkPutRel(s + n, stubAt + n + 4, back)) return false;
    n += 4;
    memcpy(s + n, on, sizeof on), n += sizeof on;
    if (!MarkPutRel(s + n, stubAt + n + 4, back)) return false;
    n += 4;
    return n == kMarkPickStub;
}

// The fade's stub, kMarkFadeStub bytes; back: the byte after the 37 of the block; any: the mod's byte, 1 while any of
// its locks shows; last, time, clock: the drawer's own, as absolute addresses.
//   movzx eax, byte [r13+12h]; mov rcx, any; or al, [rcx]; mov rcx, last; cmp [rcx], al; je same
//   mov [rcx], al; mov rax, clock; call rax; mov rcx, time; movsd [rcx], xmm0
//   same: jmp back
inline bool MarkFadeStub(uint8_t *s, uint64_t stubAt, uint64_t back, uint64_t any, uint64_t last, uint64_t time, uint64_t clock)
{
    int n = 0;
    auto put = [&](std::initializer_list<uint8_t> b) {
        for (uint8_t c : b) s[n++] = c;
    };
    put({ 0x41, 0x0F, 0xB6, 0x45, 0x12, 0x48, 0xB9 });
    MarkPut64(s + n, any), n += 8;
    put({ 0x0A, 0x01, 0x48, 0xB9 });
    MarkPut64(s + n, last), n += 8;
    put({ 0x38, 0x01, 0x74, 0x1C, 0x88, 0x01, 0x48, 0xB8 });
    MarkPut64(s + n, clock), n += 8;
    put({ 0xFF, 0xD0, 0x48, 0xB9 });
    MarkPut64(s + n, time), n += 8;
    put({ 0xF2, 0x0F, 0x11, 0x01, 0xE9 });
    if (!MarkPutRel(s + n, stubAt + n + 4, back)) return false;
    n += 4;
    return n == kMarkFadeStub;
}

// The bytes a place is given: a jump to its stub, then int3 to the place's end (never run: the stub jumps back past it).
inline bool MarkJump(uint8_t *out, int length, uint64_t placeAt, uint64_t stubAt)
{
    if (length < 5) return false;
    out[0] = 0xE9;
    if (!MarkPutRel(out + 1, placeAt + 5, stubAt)) return false;
    for (int i = 5; i < length; i++) out[i] = 0xCC;
    return true;
}

// The drawer's rows for the wheels in the wheel list's order. drive: how each is driven (0 not, 2 with all wheel drive,
// anything else always); along: its place along the truck in metres; ok: both were read. rowOf: each wheel's row, -1
// none. Returns the rows, or -1 where a wheel that counts was not read (then nothing is numbered).
inline int MarkRowsOf(const int *drive, const float *along, const bool *ok, int n, bool awd, int *rowOf)
{
    float first[64];
    int rows = 0;
    for (int i = 0; i < n; i++) rowOf[i] = -1;
    for (int i = 0; i < n; i++)
    {
        if (drive[i] == 0 || (drive[i] == 2 && !awd)) continue;
        if (!ok[i] || !isfinite(along[i])) return -1;
        int r = 0;
        while (r < rows && !(fabsf(first[r] - along[i]) < kMarkRowGap)) r++;
        if (r == rows)
        {
            if (rows == 64) return -1;
            first[rows++] = along[i];
        }
        rowOf[i] = r;
    }
    return rows;
}

// The table: each row 1 where a wheel in it is locked by the mod (locked[i]), 0 else; rows past kMarkRowsMost are left
// to the game's byte. any: some row is locked.
inline bool MarkTable(const int *rowOf, const bool *locked, int n, uint8_t *table)
{
    bool any = false;
    for (int r = 0; r < kMarkRowsMost; r++) table[r] = 0;
    for (int i = 0; i < n; i++)
        if (locked[i] && rowOf[i] >= 0 && rowOf[i] < kMarkRowsMost) table[rowOf[i]] = 1, any = true;
    return any;
}
