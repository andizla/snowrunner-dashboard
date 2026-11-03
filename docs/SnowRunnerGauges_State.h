// SnowRunnerGauges_State.h: how another mod reads Next Gen Gameplay's state (Gauges.asi) in SnowRunner.
//
// Gauges writes a block of 256 bytes of named shared memory, "Local\SnowRunnerGauges.State.<process id>", in the
// game's own process, about 20 times a second. It holds the truck driven and everything its cluster shows, as the
// cluster shows it, so a speedo of another mod and Gauges' own never disagree. This file needs only windows.h and
// stdint.h. Gauges builds its writer on this same file.
//
// The read rule:
// - Open the mapping by its name with FILE_MAP_READ and map the whole of it (size 0). Never map sizeof(SrgState): an
//   older Gauges' block is 64 bytes, and asking for more fails.
// - The sequence is odd while Gauges writes. Copy the block while it is even, and take the copy only if the sequence is
//   still the same after it. Two tries are enough; on a miss, keep the last copy.
// - Check magic, then size: copy min(size, sizeof(SrgState)) bytes and treat the rest as zero. A field is there only
//   if size covers it (SRG_STATE_HAS below). Version 1 is 64 bytes, version 2 is 256.
// - A tick older than 2 seconds by GetTickCount64 means Gauges is gone, or the game is paused in its menu.
// - Read only. A reader never writes this block. A mod that wants to tell Gauges something publishes its own block.
//
// All values are metric (km/h, bar, degrees C, litres). The options say which units the player reads. No value is
// ever NaN: where one can be unknown, its comment says how that shows.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert // C11, for a reader written in C
#endif

#define SRG_STATE_MAGIC 0x47554147u // "GAUG"
#define SRG_STATE_VERSION 2u

// flags (version 1)
#define SRG_FLAG_VEHICLE 0x1u    // a truck is driven; vehicle names it. Off: every truck value below is 0
#define SRG_FLAG_BY_MAP 0x2u     // the outside temperature is the map's, by map and hour, not the AmbientC setting
#define SRG_FLAG_ENGINE_ON 0x4u  // the truck's engine runs

// gearMode (version 2): the letter the cluster shows. N and R go by gear
#define SRG_GEAR_NUMBER 0 // a numbered gear, or N, or R
#define SRG_GEAR_A 1      // automatic: "A" beside the gear's number
#define SRG_GEAR_H 2      // the high gear
#define SRG_GEAR_L_MINUS 3
#define SRG_GEAR_L 4
#define SRG_GEAR_L_PLUS 5

// gearboxLevel (version 2): the Gearbox level in force (a wheel without a reverse binding runs 2 as 1)
#define SRG_GEARBOX_GAME 0  // the game's own
#define SRG_GEARBOX_AUTO 1  // the mod's automatic in A
#define SRG_GEARBOX_OWN 2   // the mod's own gearbox

// tireLamp (version 2): the tire mode the tire lamp shows
#define SRG_TIRE_NONE 0 // no tire mod for this truck: no lamp
#define SRG_TIRE_LOW 1
#define SRG_TIRE_REDUCED 2
#define SRG_TIRE_NORMAL 3
#define SRG_TIRE_INCREASED 4

// lamps (version 2): a bit is set while the lamp is lit. blinks: the same bit is set while that lamp blinks, so the
// reader blinks it at its own rate. Only DAMAGE, OIL and TIRE ever blink
#define SRG_LAMP_HANDBRAKE 0x1u
#define SRG_LAMP_DIFF_LOCK 0x2u
#define SRG_LAMP_AWD 0x4u
#define SRG_LAMP_LOW_GEAR 0x8u
#define SRG_LAMP_LIGHTS 0x10u
#define SRG_LAMP_DAMAGE 0x20u    // the engine is damaged; blinks while the mod adds damage
#define SRG_LAMP_COLD 0x40u      // the engine is cold
#define SRG_LAMP_OIL 0x80u       // the oil is too hot (it blinks then)
#define SRG_LAMP_TIRE 0x100u     // the tire lamp: lit away from Normal, blinks while the pressure changes
#define SRG_LAMP_TIRE_WARN 0x200u // the tires take damage or one is worn out: the tire lamp is red
#define SRG_LAMP_TILT_SHIFT 16   // bits 16 and 17: the tilt lamp, 0 level, 1 warning, 2 danger, 3 not known (dark)
#define SRG_LAMP_TILT(lamps) (((lamps) >> SRG_LAMP_TILT_SHIFT) & 3u)

// options (version 2)
#define SRG_OPT_MPH 0x1u        // the player reads mph
#define SRG_OPT_PSI 0x2u        // psi
#define SRG_OPT_FAHRENHEIT 0x4u // degrees F
#define SRG_OPT_CLUSTER 0x8u    // Gauges' own cluster is on screen, so the player may see two clusters
#define SRG_OPT_VACUUM 0x10u    // the boost dial starts at -1 bar (a petrol engine)
#define SRG_OPT_NO_BOOST 0x20u  // a naturally aspirated engine: no boost dial
// 0x40 is kept for the mod's gearbox returning to A by itself (BackToA), still to come

typedef struct SrgState
{
    // version 1, 64 bytes
    uint32_t magic, version, size;
    volatile long sequence; // odd while Gauges writes
    uint64_t tick;          // GetTickCount64 at the write
    uint64_t vehicle;       // the game's object for the truck driven, 0 none
    float ambientC;         // the outside temperature in force, C (written with or without a truck)
    float coolantC;         // the coolant temperature, C
    float rpm;              // the engine speed
    float boostBar;         // the boost in bar, gauge: under 0 a petrol engine's vacuum (down to about -0.7)
    uint32_t flags;         // SRG_FLAG_
    uint32_t reserved1[3];
    // version 2, from offset 64
    float speedKmh;         // below 0 backwards
    float speedMax;         // the speedometer's top
    float rpmRedline, rpmMax; // the tachometer's red zone, from rpmRedline to its end at rpmMax
    int32_t gear;           // below 0 R, 0 N, 1 and up forward
    uint8_t gearMode;       // SRG_GEAR_
    uint8_t top;            // the gearbox's numbered gears, for "A 3/12"; 0 not known
    uint8_t gearboxLevel;   // SRG_GEARBOX_
    uint8_t tireLamp;       // SRG_TIRE_
    float oilC;             // the oil temperature, C
    float oilWarnC;         // where the oil dial's red zone starts, C
    float airBar;           // brake air, 0 to 10 bar
    float boostTop;         // the turbo's most in bar: the boost dial ends at 2 bar up to 2, else at 3
    float fuelFrac;         // 0 to 1
    float fuelL, tankL;     // the fuel and the tank in litres
    float burnPerMin;       // the fuel burnt in litres a minute, smoothed; below 0 not known
    float rangeKm;          // the range left; below 0 not known
    uint32_t lamps;         // SRG_LAMP_: lit
    uint32_t blinks;        // SRG_LAMP_: blinking
    uint32_t options;       // SRG_OPT_
    uint32_t reserved2[30]; // Gauges' later fields; zero for now
} SrgState;

#define SRG_STATE_HAS(state, field) ((state).size >= offsetof(SrgState, field) + sizeof((state).field))

static_assert(sizeof(SrgState) == 256, "version 2 is 256 bytes");
static_assert(offsetof(SrgState, sequence) == 12 && offsetof(SrgState, tick) == 16 && offsetof(SrgState, vehicle) == 24 &&
              offsetof(SrgState, ambientC) == 32 && offsetof(SrgState, coolantC) == 36 && offsetof(SrgState, rpm) == 40 &&
              offsetof(SrgState, boostBar) == 44 && offsetof(SrgState, flags) == 48, "version 1's places");
static_assert(offsetof(SrgState, speedKmh) == 64 && offsetof(SrgState, speedMax) == 68 && offsetof(SrgState, rpmRedline) == 72 &&
              offsetof(SrgState, rpmMax) == 76 && offsetof(SrgState, gear) == 80 && offsetof(SrgState, gearMode) == 84 &&
              offsetof(SrgState, top) == 85 && offsetof(SrgState, gearboxLevel) == 86 && offsetof(SrgState, tireLamp) == 87 &&
              offsetof(SrgState, oilC) == 88 && offsetof(SrgState, oilWarnC) == 92 && offsetof(SrgState, airBar) == 96 &&
              offsetof(SrgState, boostTop) == 100 && offsetof(SrgState, fuelFrac) == 104 && offsetof(SrgState, fuelL) == 108 &&
              offsetof(SrgState, tankL) == 112 && offsetof(SrgState, burnPerMin) == 116 && offsetof(SrgState, rangeKm) == 120 &&
              offsetof(SrgState, lamps) == 124 && offsetof(SrgState, blinks) == 128 && offsetof(SrgState, options) == 132 &&
              offsetof(SrgState, reserved2) == 136, "version 2's places");

// An example reader. Call it about as often as you draw; it returns false while there is no fresh block.
// Keep the mapping and the view open for the game's life rather than opening them each time.
//
// static const SrgState *srgView;
// bool SrgRead(SrgState *out)
// {
//     if (!srgView) {
//         wchar_t name[64];
//         wsprintfW(name, L"Local\\SnowRunnerGauges.State.%lu", GetCurrentProcessId());
//         HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
//         if (!m) return false;
//         srgView = (const SrgState *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0); // size 0: the whole block
//         CloseHandle(m);                                                        // the view keeps it
//         if (!srgView) return false;
//     }
//     for (int tries = 0; tries < 2; tries++) {
//         long before = srgView->sequence;
//         if (before & 1) continue;
//         MemoryBarrier();
//         uint32_t size = srgView->size;
//         if (srgView->magic != SRG_STATE_MAGIC || size < 64) return false;
//         SrgState copy = {0};                                                    // memcpy: string.h
//         memcpy(&copy, (const void *)srgView, size < sizeof copy ? size : sizeof copy);
//         MemoryBarrier();
//         if (srgView->sequence != before) continue;
//         if (GetTickCount64() - copy.tick > 2000) return false;
//         *out = copy;
//         return true;
//     }
//     return false;
// }
