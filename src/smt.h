// smt.h: a manual transmission mod's state, as it publishes it for other mods (a build of it with the state export,
// version 1 of the layout below). About 60 times a second it writes a block of 64 bytes into the named shared memory
// "Local\SnowRunnerMT.State.<process id>": the truck it drives, the gear and the top gear, the range, the clutch
// (held, and its travel when it is bound to an axis), the power of its low gears, the stalls it caused for a shift
// without the clutch, and its options. The block is copied while its sequence is even and unchanged, as the mod
// makes it odd while it writes.
//
// The mod's file is never touched: the block is a mapping that lives on while either side holds it, so a reader is
// never left with a pointer into a file that was unloaded (the manual transmission mod can unload itself). Without
// the mod, or with a build that has no export, the mapping is not there and the gauges go on as before; it is looked
// for again every kSmtLookMs. A block whose time stands for kSmtStaleMs is taken as the mod gone.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const uint32_t kSmtMagic = 0x53544D53u; // "SMTS"
static const uint32_t kSmtVersion = 1;
static const unsigned long long kSmtLookMs = 5000; // how often the mapping is looked for while it is not there
static const unsigned long long kSmtStaleMs = 1000; // a block not written for this long is the mod gone

// Its flags, as the manual transmission mod sets them.
enum : uint32_t
{
    kSmtVehicle = 1u << 0,         // a truck is driven; the fields describe it
    kSmtAutoRange = 1u << 1,       // its A range (the numbered gears at the normal power), not L-, L or L+
    kSmtClutch = 1u << 2,          // the clutch is held
    kSmtRequireClutch = 1u << 3,   // a shift without the clutch stalls the engine
    kSmtRequireGearHeld = 1u << 4, // neutral when no gear is held, as with an H shifter
    kSmtImmersive = 1u << 5,
    kSmtGameShiftingOff = 1u << 6, // the game's own shifting is shut out
    kSmtClutchAxis = 1u << 7,      // the clutch is bound to one axis, and clutch is its travel
    kSmtFocused = 1u << 8,         // the game's window is in front, so the mod reads its bindings
};

// The block as the manual transmission mod lays it out, version 1. A later version only adds to its end.
struct SmtBlock
{
    uint32_t magic, version, size;
    volatile long sequence;  // odd while the mod writes
    uint64_t tick;           // GetTickCount64 at the write
    uint64_t vehicle;        // the truck it drives: the same object as TruckRefs::vehicle
    int32_t gear;            // -1 reverse, 0 neutral, 1 up; the top gear + 1 is the high gear
    int32_t maxGear;
    int32_t range;           // its range selector: -1 low, 0 none, 1 high
    uint32_t flags;
    float clutch;            // 0 released to 1 pressed
    float powerCoef;         // the power its low gears were last given
    uint32_t stalls, shifts; // counts since it loaded
};
static_assert(sizeof(SmtBlock) == 64 && offsetof(SmtBlock, tick) == 16 && offsetof(SmtBlock, clutch) == 48, "the manual transmission mod's layout, version 1");

// What the gauges take from it.
struct SmtView
{
    bool present;        // the mod publishes, and its block is fresh
    bool driving;        // it drives a truck
    uint64_t vehicle;
    int gear, maxGear, range;
    bool autoRange, clutchHeld, clutchAxis, requireClutch, gearHeld, focused;
    float clutch;        // 0 released to 1 pressed: the axis's travel, or 0 or 1 for a button
    float powerCoef;
    unsigned stalls, shifts;
};

// Copies the block while the mod is not writing it: the sequence even and the same before and after. False when it
// was written over all the tries, or is no block of a version this reads.
inline bool SmtCopy(const volatile SmtBlock *b, SmtBlock &out)
{
    for (int tries = 0; tries < 8; tries++)
    {
        const long before = b->sequence;
        if (before & 1) continue;
        MemoryBarrier();
        memcpy(&out, (const void *)b, sizeof out);
        MemoryBarrier();
        if (b->sequence == before)
            return out.magic == kSmtMagic && out.version >= kSmtVersion && out.size >= sizeof(SmtBlock);
    }
    return false;
}

// The view of a copied block at now (GetTickCount64): nothing while it is stale.
inline SmtView SmtViewOf(const SmtBlock &b, unsigned long long now)
{
    SmtView v = {};
    if (now > b.tick + kSmtStaleMs) return v;
    v.present = true;
    v.driving = (b.flags & kSmtVehicle) != 0 && b.vehicle != 0;
    v.vehicle = v.driving ? b.vehicle : 0;
    v.gear = b.gear;
    v.maxGear = b.maxGear;
    v.range = b.range;
    v.autoRange = (b.flags & kSmtAutoRange) != 0;
    v.clutchHeld = (b.flags & kSmtClutch) != 0;
    v.clutchAxis = (b.flags & kSmtClutchAxis) != 0;
    v.requireClutch = (b.flags & kSmtRequireClutch) != 0;
    v.gearHeld = (b.flags & kSmtRequireGearHeld) != 0;
    v.focused = (b.flags & kSmtFocused) != 0;
    v.clutch = b.clutch >= 0.0f && b.clutch <= 1.0f ? b.clutch : v.clutchHeld ? 1.0f : 0.0f;
    v.powerCoef = b.powerCoef;
    v.stalls = b.stalls;
    v.shifts = b.shifts;
    return v;
}

// The mapping, held open once it is found.
struct SmtReader
{
    HANDLE mapping;
    const volatile SmtBlock *block;
    unsigned long long lookAt; // when it is looked for next
};

// The manual transmission mod's state at now: the mapping is opened the first time it is there, looked for every
// kSmtLookMs until then, and held open after.
inline SmtView SmtRead(SmtReader &r, unsigned long long now)
{
    if (!r.block && now >= r.lookAt)
    {
        r.lookAt = now + kSmtLookMs;
        wchar_t name[64];
        swprintf(name, 64, L"Local\\SnowRunnerMT.State.%lu", GetCurrentProcessId());
        r.mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (r.mapping)
        {
            r.block = (const volatile SmtBlock *)MapViewOfFile(r.mapping, FILE_MAP_READ, 0, 0, sizeof(SmtBlock));
            if (!r.block)
            {
                CloseHandle(r.mapping);
                r.mapping = nullptr;
            }
        }
    }
    SmtBlock b;
    if (!r.block || !SmtCopy(r.block, b)) return SmtView{};
    return SmtViewOf(b, now);
}

inline void SmtClose(SmtReader &r)
{
    if (r.block) UnmapViewOfFile((const void *)r.block);
    if (r.mapping) CloseHandle(r.mapping);
    r = SmtReader{};
}

// The gear as the manual transmission mod names it: R, N, its number, H for the high gear, and L-, L or L+ for its
// low gears by the power it gave them.
inline const char *SmtGearName(const SmtView &v, char *out, size_t size)
{
    if (v.gear < 0) snprintf(out, size, "R");
    else if (v.gear == 0) snprintf(out, size, "N");
    else if (v.maxGear > 0 && v.gear > v.maxGear) snprintf(out, size, "H");
    else if (!v.autoRange && v.gear == 1) snprintf(out, size, "%s", v.powerCoef < 0.3f ? "L-" : v.powerCoef < 0.7f ? "L" : "L+");
    else snprintf(out, size, "%d", v.gear);
    return out;
}

// The manual transmission mod in a line of text for the probe log.
inline void SmtProbeText(const SmtView &v, bool sameTruck, char *out, size_t size)
{
    if (!v.present)
    {
        snprintf(out, size, "manual transmission mod: not publishing (not loaded, or a build without the state export)");
        return;
    }
    char gear[8];
    snprintf(out, size, "manual transmission mod: %s | gear %s of %d, range %d | clutch %s%.2f%s | power of the low gears %.2f | %u shifts, %u stalls for a shift without "
                        "the clutch | %s%s%s",
             !v.driving ? "no truck" : sameTruck ? "drives this truck" : "drives another truck", SmtGearName(v, gear, sizeof gear), v.maxGear, v.range,
             v.clutchHeld ? "held, " : "", v.clutch, v.clutchAxis ? " (an axis)" : "", v.powerCoef, v.shifts, v.stalls, v.requireClutch ? "clutch required, " : "",
             v.gearHeld ? "gear held, " : "", v.focused ? "reading its bindings" : "the game's window not in front");
}
