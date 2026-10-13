// tires.h: a tire inflation mod's state, as it publishes it for other mods: a block of 64 bytes of shared memory,
// "Local\SnowRunnerTirePressure.State.<process id>", written about 20 times a second, version 1 of the layout below.
// It holds the truck the mod works on, the pressure mode chosen (Low, Reduced, Normal, Increased), where the pressure
// stands, and whether the tires take damage. The cluster shows the mode in a lamp of its own (TiresLamp).
//
// The block is copied while its sequence is even and unchanged, as the writer makes it odd while it writes. Without
// the mod, or with a build of it that publishes nothing, the mapping is not there and the lamp is not drawn; it is
// looked for again every kTiresLookMs. A block whose time stands for kTiresStaleMs is taken as the mod gone. The
// mapping lives on while either side holds it, so there is never a pointer into a module that went away.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const uint32_t kTiresMagic = 0x45524954u; // "TIRE"
static const unsigned long long kTiresLookMs = 5000, kTiresStaleMs = 2000;

// Its flags, as the tire mod sets them.
enum : uint32_t
{
    kTiresVehicle = 1u << 0,  // a truck is driven; vehicle names it and the rest describes its tires
    kTiresChanging = 1u << 1, // the pressure is on its way to the chosen mode's
    kTiresHeld = 1u << 2,     // the change waits for the truck to stand still
    kTiresTooFast = 1u << 3,  // the truck is too fast for its tire pressure: the tires take damage
    kTiresWorn = 1u << 4,     // a tire of the truck is worn out
};

// The block as the tire mod lays it out, version 1. A later version only adds to its end.
struct TiresBlock
{
    uint32_t magic, version, size;
    volatile long sequence; // odd while the writer writes
    uint64_t tick;          // GetTickCount64 at the write
    uint64_t vehicle;       // the truck driven, 0 none
    int32_t mode;           // the chosen mode: 0 Low, 1 Reduced, 2 Normal, 3 Increased
    int32_t modes;          // the modes in use: 3 (Low to Normal) or 4
    float pressure;         // the tires' pressure as a share of Normal's, where it stands
    float pressureAir;      // the same as the outside temperature leaves it
    float target;           // the chosen mode's pressure
    float outsideC;         // the outside temperature in force, degrees C
    uint32_t flags;
    int32_t wear;           // the most worn tire's damage in percent of what it takes, -1 not known
};
static_assert(sizeof(TiresBlock) == 64 && offsetof(TiresBlock, sequence) == 12 && offsetof(TiresBlock, tick) == 16 && offsetof(TiresBlock, vehicle) == 24 &&
              offsetof(TiresBlock, mode) == 32 && offsetof(TiresBlock, pressure) == 40 && offsetof(TiresBlock, outsideC) == 52 && offsetof(TiresBlock, flags) == 56 &&
              offsetof(TiresBlock, wear) == 60, "the tire mod's layout, version 1");

// A consistent copy of the block, or false while the writer is in the middle of a write or the block is not one of
// its.
inline bool TiresCopy(const volatile TiresBlock *from, TiresBlock &to)
{
    for (int tries = 0; tries < 8; tries++)
    {
        const long before = from->sequence;
        if (before & 1) { Sleep(0); continue; }
        memcpy(&to, (const void *)from, sizeof to);
        MemoryBarrier();
        if (from->sequence != before) continue;
        return to.magic == kTiresMagic && to.version >= 1 && to.size >= sizeof(TiresBlock);
    }
    return false;
}

// The mapping, held open once it is found.
struct TiresReader
{
    HANDLE mapping;
    const volatile TiresBlock *block;
    unsigned long long lookAt; // when it is looked for next
};

// What the cluster takes from the block. present: the mod publishes and its block is fresh.
struct TiresView
{
    bool present;
    bool driving;   // it has a truck: vehicle names it
    uint64_t vehicle;
    int mode;       // 0 Low, 1 Reduced, 2 Normal, 3 Increased
    float pressure; // where the pressure stands, as a share of Normal's
    bool changing, held, tooFast, worn;
};

// The tire mod's state now. The mapping is opened the first time it is there (looked for every kTiresLookMs until
// then) and held open after.
inline TiresView TiresRead(TiresReader &r, unsigned long long now)
{
    TiresView v = {};
    if (!r.block && now >= r.lookAt)
    {
        r.lookAt = now + kTiresLookMs;
        wchar_t name[64];
        swprintf(name, 64, L"Local\\SnowRunnerTirePressure.State.%lu", GetCurrentProcessId());
        r.mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (r.mapping)
        {
            r.block = (const volatile TiresBlock *)MapViewOfFile(r.mapping, FILE_MAP_READ, 0, 0, sizeof(TiresBlock));
            if (!r.block)
            {
                CloseHandle(r.mapping);
                r.mapping = nullptr;
            }
        }
    }
    TiresBlock b;
    if (!r.block || !TiresCopy(r.block, b) || now - b.tick > kTiresStaleMs) return v;
    v.present = true;
    v.driving = (b.flags & kTiresVehicle) != 0;
    v.vehicle = b.vehicle;
    v.mode = b.mode < 0 ? 0 : b.mode > 3 ? 3 : b.mode;
    v.pressure = b.pressure;
    v.changing = (b.flags & kTiresChanging) != 0;
    v.held = (b.flags & kTiresHeld) != 0;
    v.tooFast = (b.flags & kTiresTooFast) != 0;
    v.worn = (b.flags & kTiresWorn) != 0;
    return v;
}

// The cluster's tire lamp for the truck at vehicle. lamp: 0 no lamp (no tire mod, or it works on another truck), else
// 1 Low, 2 Reduced, 3 Normal, 4 Increased: the mode chosen. lit: steady away from Normal, blinking while the pressure
// is on its way (ms: the time now, for the blink), dark at Normal. warn: the tires take damage or one is worn out:
// the lamp is lit red.
inline void TiresLamp(const TiresView &v, uint64_t vehicle, unsigned long long ms, int &lamp, bool &lit, bool &warn)
{
    lamp = 0;
    lit = warn = false;
    if (!v.present || !v.driving || !vehicle || v.vehicle != vehicle) return;
    lamp = 1 + v.mode;
    warn = v.tooFast || v.worn;
    lit = warn || (v.changing || v.held ? (ms / 400) % 2 == 0 : v.mode != 2);
}
