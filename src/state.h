// state.h: the mod's state for other mods, in a block of 64 bytes of named shared memory,
// "Local\SnowRunnerGauges.State.<process id>": the truck driven, the outside temperature in force, and the coolant
// temperature, the engine speed and the boost as the mod models them. The mod's thread writes it on every pass,
// about 20 times a second. It makes the sequence odd before it writes and even after, so a reader copies the block
// while the sequence is even and unchanged, and takes a block whose tick stands for two seconds as the mod gone.
// This is version 1 of the layout; a later version only adds to its end.
//
// The block is a mapping, not a place in the mod's file: it lives on while either side holds it, so a reader is never
// left with a pointer into a module that went away.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static const uint32_t kStateMagic = 0x47554147u; // "GAUG"
static const uint32_t kStateVersion = 1;

enum : uint32_t
{
    kStateVehicle = 1u << 0,  // a truck is driven; vehicle names it
    kStateByMap = 1u << 1,    // the outside temperature is the map's, by map and hour, not the AmbientC setting
    kStateEngineOn = 1u << 2, // the truck's engine runs
};

struct StateBlock
{
    uint32_t magic, version, size;
    volatile long sequence; // odd while the mod writes
    uint64_t tick;          // GetTickCount64 at the write
    uint64_t vehicle;       // the game's object for the truck driven, 0 none
    float ambientC;         // the outside temperature in force, degrees C
    float coolantC;         // the coolant temperature, degrees C
    float rpm;              // the engine speed
    float boostBar;         // the boost pressure in bar, gauge: under 0 a petrol engine's vacuum (down to about -0.7)
    uint32_t flags;
    uint32_t reserved[3];
};
static_assert(sizeof(StateBlock) == 64 && offsetof(StateBlock, sequence) == 12 && offsetof(StateBlock, tick) == 16 && offsetof(StateBlock, vehicle) == 24 &&
              offsetof(StateBlock, ambientC) == 32 && offsetof(StateBlock, coolantC) == 36 && offsetof(StateBlock, rpm) == 40 && offsetof(StateBlock, boostBar) == 44 &&
              offsetof(StateBlock, flags) == 48, "version 1 of the layout, as its readers have it");

// What one pass publishes.
struct StateValues
{
    uint64_t vehicle;
    float ambientC, coolantC, rpm, boostBar;
    uint32_t flags;
};

// The mapping and the block in it, held for the life of the process.
struct StateWriter
{
    HANDLE mapping;
    StateBlock *block;
};

// The block's name in this process.
inline void StateName(wchar_t *name, size_t size) { swprintf(name, size, L"Local\\SnowRunnerGauges.State.%lu", GetCurrentProcessId()); }

// Creates the block and fills in what never changes. False when Windows gives no mapping: nothing is published then,
// and StateWrite does nothing.
inline bool StateOpen(StateWriter &w)
{
    wchar_t name[64];
    StateName(name, 64);
    w = StateWriter{};
    w.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(StateBlock), name);
    if (!w.mapping) return false;
    w.block = (StateBlock *)MapViewOfFile(w.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(StateBlock));
    if (!w.block)
    {
        CloseHandle(w.mapping);
        w.mapping = nullptr;
        return false;
    }
    InterlockedIncrement(&w.block->sequence);
    MemoryBarrier();
    w.block->magic = kStateMagic;
    w.block->version = kStateVersion;
    w.block->size = sizeof(StateBlock);
    MemoryBarrier();
    InterlockedIncrement(&w.block->sequence);
    return true;
}

// One pass's values into the block. tick: GetTickCount64 now.
inline void StateWrite(StateWriter &w, const StateValues &v, uint64_t tick)
{
    if (!w.block) return;
    InterlockedIncrement(&w.block->sequence);
    MemoryBarrier();
    w.block->tick = tick;
    w.block->vehicle = v.vehicle;
    w.block->ambientC = v.ambientC;
    w.block->coolantC = v.coolantC;
    w.block->rpm = v.rpm;
    w.block->boostBar = v.boostBar;
    w.block->flags = v.flags;
    MemoryBarrier();
    InterlockedIncrement(&w.block->sequence);
}
