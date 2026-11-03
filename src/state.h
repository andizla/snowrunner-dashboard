// state.h: the mod's state for other mods, in a block of 256 bytes of named shared memory,
// "Local\SnowRunnerGauges.State.<process id>": the truck driven, the outside temperature in force, and from version 2
// everything the cluster shows, as it shows it (speed, gear, the scales, the temperatures, air, fuel, the lamps, the
// units), so another mod's speedo and the mod's own never disagree. The mod's thread writes it on every pass, about
// 20 times a second. It makes the sequence odd before it writes and even after, so a reader copies the block while
// the sequence is even and unchanged, and takes a block whose tick stands for two seconds as the mod gone.
//
// The layout is docs\SnowRunnerGauges_State.h, the header other mods' authors are given, so their copy can never drift
// from the mod's. Version 1 was the first 64 bytes; a later version only adds to the end. The tire mod reads version 1
// (its outside.h copies 64 bytes and checks version >= 1 and size >= 64).
//
// The block is a mapping, not a place in the mod's file: it lives on while either side holds it, so a reader is never
// left with a pointer into a module that went away.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include "../docs/SnowRunnerGauges_State.h"

typedef SrgState StateBlock;
static const uint32_t kStateMagic = SRG_STATE_MAGIC;
static const uint32_t kStateVersion = SRG_STATE_VERSION;

enum : uint32_t
{
    kStateVehicle = SRG_FLAG_VEHICLE,    // a truck is driven; vehicle names it
    kStateByMap = SRG_FLAG_BY_MAP,       // the outside temperature is the map's, by map and hour, not the AmbientC setting
    kStateEngineOn = SRG_FLAG_ENGINE_ON, // the truck's engine runs
};

// What one pass publishes. The version 2 part is zero while no truck is driven.
struct StateValues
{
    uint64_t vehicle;
    float ambientC, coolantC, rpm, boostBar;
    uint32_t flags;
    // version 2
    float speedKmh, speedMax, rpmRedline, rpmMax;
    int32_t gear;
    uint8_t gearMode, top, gearboxLevel, tireLamp;
    float oilC, oilWarnC, airBar, boostTop, fuelFrac, fuelL, tankL, burnPerMin, rangeKm;
    uint32_t lamps, blinks, options;
};

// The cluster's gear letter as the block's code (SRG_GEAR_): "A", "H", "L-", "L", "L+", else a numbered gear.
inline uint8_t StateGearMode(const char *mode)
{
    if (!mode || !mode[0]) return SRG_GEAR_NUMBER;
    if (mode[0] == 'A') return SRG_GEAR_A;
    if (mode[0] == 'H') return SRG_GEAR_H;
    if (mode[0] == 'L') return mode[1] == '-' ? SRG_GEAR_L_MINUS : mode[1] == '+' ? SRG_GEAR_L_PLUS : SRG_GEAR_L;
    return SRG_GEAR_NUMBER;
}

// A value for the block: never NaN or infinite (none for a value that has no meaning, else 0).
inline float StateNumber(float v, float none = 0.0f) { return isfinite(v) ? v : none; }

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
    StateBlock *b = w.block;
    InterlockedIncrement(&b->sequence);
    MemoryBarrier();
    b->tick = tick;
    b->vehicle = v.vehicle;
    b->ambientC = StateNumber(v.ambientC);
    b->coolantC = StateNumber(v.coolantC);
    b->rpm = StateNumber(v.rpm);
    b->boostBar = StateNumber(v.boostBar);
    b->flags = v.flags;
    b->speedKmh = StateNumber(v.speedKmh);
    b->speedMax = StateNumber(v.speedMax);
    b->rpmRedline = StateNumber(v.rpmRedline);
    b->rpmMax = StateNumber(v.rpmMax);
    b->gear = v.gear;
    b->gearMode = v.gearMode;
    b->top = v.top;
    b->gearboxLevel = v.gearboxLevel;
    b->tireLamp = v.tireLamp;
    b->oilC = StateNumber(v.oilC);
    b->oilWarnC = StateNumber(v.oilWarnC);
    b->airBar = StateNumber(v.airBar);
    b->boostTop = StateNumber(v.boostTop);
    b->fuelFrac = StateNumber(v.fuelFrac);
    b->fuelL = StateNumber(v.fuelL);
    b->tankL = StateNumber(v.tankL);
    b->burnPerMin = StateNumber(v.burnPerMin, -1.0f);
    b->rangeKm = StateNumber(v.rangeKm, -1.0f);
    b->lamps = v.lamps;
    b->blinks = v.blinks;
    b->options = v.options;
    MemoryBarrier();
    InterlockedIncrement(&b->sequence);
}
