# Reading Next Gen Gameplay's state from another mod

This guide is for the author of a SnowRunner mod (or the coding agent working for them) who wants to show what Next Gen Gameplay knows. The first case in mind is Spotter's speedo builder. The layout is in `SnowRunnerGauges_State.h`, next to this file. That header is the one Next Gen Gameplay itself builds on, so it is always right.

## What it is

Next Gen Gameplay (`Gauges.asi`) models the engine, gearbox, turbo, temperatures, brake air and fuel burn itself. Its values are not the game's. With the mod's own gearbox the game's gear is not the gear the player is in either. A speedo that reads the game alone will disagree with the mod's own cluster.

So the mod publishes everything its cluster shows, as the cluster shows it, in a block of shared memory inside the game's process:

- name: `Local\SnowRunnerGauges.State.<process id>` (the game's own id, `GetCurrentProcessId()` from inside the game)
- size: 256 bytes (version 2). Version 1 was the first 64 bytes.
- written about 20 times a second by the mod's own thread
- read only: a reader never writes it

If the block is not there, Next Gen Gameplay is not installed or not running. Fall back to your own reads of the game.

## How to read it

The header has a complete example reader (`SrgRead`). In short:

1. Open the mapping by name with `FILE_MAP_READ`. Map it with size 0, so the view is the whole block whatever its version. Never map `sizeof(SrgState)`: an older Gauges has a 64-byte block, and a bigger view fails. Keep the view open for the game's life.
2. The `sequence` field is odd while the mod writes. Read `sequence`, copy the block while it is even, then read `sequence` again. Keep the copy only if it did not change. Two tries are enough. On a miss, keep using your last good copy.
3. Check `magic` is `SRG_STATE_MAGIC`. Copy `min(size, sizeof(SrgState))` bytes into a zeroed `SrgState`.
4. A field exists only if `size` covers it: `SRG_STATE_HAS(copy, field)`. Use this for every version 2 field. Do not go by `version` alone.
5. If `GetTickCount64() - tick` is over 2000 ms, the mod is gone or the game sits in its menu. Treat that like no block.
6. If `flags & SRG_FLAG_VEHICLE` is off, no truck is driven. Every truck value is 0 then. Only `ambientC` is written without a truck.

Read it once per frame you draw, or at any rate you like. It costs a 256-byte copy.

## The fields

All values are metric, whatever the player reads. `options` says which units the player picked, so your speedo can show the same.

| Field | Meaning |
|---|---|
| `tick` | `GetTickCount64()` at the write |
| `vehicle` | the game's object for the truck driven, 0 none. Use it to notice a truck change. |
| `ambientC` | outside temperature in force, °C |
| `coolantC` | coolant temperature, °C (the mod's model) |
| `rpm` | engine speed (the mod's model) |
| `boostBar` | boost in bar above the air. Below 0 is a petrol engine's vacuum, down to about -0.7. |
| `flags` | `SRG_FLAG_VEHICLE`, `SRG_FLAG_ENGINE_ON`, `SRG_FLAG_BY_MAP` (the outside temperature is the map's) |
| `speedKmh` | speed in km/h. Below 0 is backwards. Most speedos show its absolute value. |
| `speedMax` | the top of the mod's speedometer, km/h (trucks 100, scouts 120, heavy 80, or the player's setting) |
| `rpmRedline`, `rpmMax` | the tachometer: the red zone from `rpmRedline` to the dial's end at `rpmMax` |
| `gear` | below 0 R, 0 N, 1 and up the forward gears |
| `gearMode` | the letter: `SRG_GEAR_NUMBER` (show the number, or N, or R), `SRG_GEAR_A`, `SRG_GEAR_H`, `SRG_GEAR_L_MINUS`, `SRG_GEAR_L`, `SRG_GEAR_L_PLUS` |
| `top` | how many forward gears the gearbox has, 0 not known. For "A 3/12". |
| `gearboxLevel` | which gearbox drives: `SRG_GEARBOX_GAME`, `SRG_GEARBOX_AUTO` (the mod's automatic), `SRG_GEARBOX_OWN` (the mod's own gearbox) |
| `tireLamp` | Next Gen Tire Inflation's mode: `SRG_TIRE_NONE` (no lamp), `LOW`, `REDUCED`, `NORMAL`, `INCREASED` |
| `oilC`, `oilWarnC` | oil temperature, and where the oil dial's red zone starts, °C |
| `airBar` | brake air, 0 to 10 bar |
| `boostTop` | the turbo's most, bar. The mod's boost dial ends at 2 bar when this is up to 2, else at 3. |
| `fuelFrac` | fuel left, 0 to 1 |
| `fuelL`, `tankL` | fuel left and the tank, litres |
| `burnPerMin` | fuel burnt, litres a minute, smoothed. Below 0 not known yet. |
| `rangeKm` | range left, km. Below 0 not known yet. |
| `lamps` | the lamps lit (`SRG_LAMP_*` bits), and the tilt lamp in bits 16 and 17 |
| `blinks` | the lamps that blink now: the same bits |
| `options` | `SRG_OPT_MPH`, `SRG_OPT_PSI`, `SRG_OPT_FAHRENHEIT`, `SRG_OPT_CLUSTER`, `SRG_OPT_VACUUM`, `SRG_OPT_NO_BOOST` |

No value is ever NaN or infinite. Where one can be unknown, the table says how that shows.

## Drawing it right

**The gear.** Show the letter for A, H and the low range. For "A", show the number next to it, as in "A 3" or "A 3/12" with `top`. For `SRG_GEAR_NUMBER`, show `gear` as a number, or "N" at 0, or "R" below 0. This is the gear the player is really in. It can differ from the game's own gear, so prefer it over your own read whenever the block is there.

**The engine and the temperatures.** `rpm`, `coolantC`, `oilC`, `boostBar` and `airBar` are the mod's models. The game has no such values, or different ones. Use the block's values when it is there, so your dials match the mod's cluster.

**The dial scales.** Use `speedMax`, `rpmRedline` and `rpmMax` for your dials' ends and red zone. Then a player who switches between the two clusters sees the same scale.

**The lamps.** A lit lamp is steady. When its bit is also set in `blinks`, blink it at your own rate (the mod's cluster blinks 400 ms lit and 400 ms dark, the damage lamp 250 ms). Only `SRG_LAMP_DAMAGE`, `SRG_LAMP_OIL` and `SRG_LAMP_TIRE` ever blink.

- `SRG_LAMP_TIRE_WARN` means the tires take damage or one is worn out. Show the tire lamp red then.
- The tilt lamp: `SRG_LAMP_TILT(lamps)` is 0 level (dark), 1 warning, 2 danger, 3 not known (dark).
- Handbrake, diff lock, AWD and low gear are in `lamps` too. Skip them if your HUD already shows the game's switches.

**The units.** Convert for display only, by `options`: km/h to mph is × 0.621371, bar to psi is × 14.5038, °C to °F is × 1.8 + 32.

**Two clusters.** `SRG_OPT_CLUSTER` is set while the mod's own cluster is on screen. If yours is on too, the player sees two. Show a hint, or offer the player a choice. Do not try to switch the mod's cluster off through the block: it is read only. If you want the mod to know your speedo is on, publish a block of your own and tell the mod's author its name.

## Versions

The layout only ever grows. Version 1's 64 bytes and version 2's fields keep their places for good. `reserved2` is for the mod's own later fields; never read meaning into it. A reader built on version 2 works with every later version.

## Checking it works

1. Start the game with Next Gen Gameplay installed and drive any truck.
2. In your mod, log one line a second: `size`, `gear`, `gearMode`, `rpm`, `speedKmh` and the age of `tick`.
3. Expect size 256, an age under 100 ms, and the same gear and rpm as the mod's cluster.
4. Close the mod's cluster (its toggle key) and watch `SRG_OPT_CLUSTER` go off.
5. Leave the truck for the menu: the age passes 2000 ms and your speedo falls back.

`Gauges.log`, next to the game's exe, has the line "state published for other mods in Local\SnowRunnerGauges.State.<id>" when the block is made.
