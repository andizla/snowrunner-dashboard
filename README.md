# SnowRunner Gauges

A gauge cluster for the truck you drive in SnowRunner: analogue dials in the style of a truck dashboard, drawn over the game in the game's own font.

The cluster has a speedometer, a tachometer with the gear, fuel with the range, coolant temperature, boost pressure, brake air pressure and eight lamps: handbrake, headlights, all wheel drive, differential lock, low gear, engine damage, cold engine and tilt.

This is version 0.4.0. It reads the truck's own values for the speedometer, the gear, the fuel gauge and seven of the lamps: handbrake, headlights, all wheel drive, differential lock, low gear, engine damage and tilt. The game has no engine speed and no range, so the mod works both out from what the truck does. The coolant temperature, boost and brake air pressure and the cold lamp come with later versions and rest until then.

## What you need

- SnowRunner for Windows, the Steam version (the game build of 22 July 2026). On any other build of the game the mod writes that into its log and does nothing.
- ReShade 6.8.0 or newer, the build "with full add-on support". The cluster and its settings tab are drawn through ReShade. Without ReShade nothing is drawn.
- An ASI loader in the game folder. The SnowRunner Tire Inflation System brings one (`version.dll`), and any other loader that loads `.asi` files from the game folder will do.

## Install

1. Close the game.
2. Copy `Gauges.asi` into the game's `Sources\Bin` folder, next to `SnowRunner.exe`.
3. Start the game. The Home key opens ReShade's overlay, which has a Gauges tab.

To update, copy the new file over the old one. Keep only one `.asi` file of this mod in the folder: a second copy under another name stands down and says so in the log.

## What it shows

- Speed, forwards and backwards.
- The gear: its number with an A in automatic, H for the high gear, L, L+ and L- for the low range, R and N.
- The engine speed. The game has none, so the mod works it out from the driven wheels, the gear and the pedal. The needle climbs through each gear to the red zone and falls at the shift. It rises under the pedal when the truck is stuck and revs freely in neutral, and spinning wheels take it into the red zone. It rises from 0 while the engine starts and runs down when the engine stalls or stops.
- Fuel as a share of the tank.
- The range: how far the fuel in the tank goes at the litres per kilometre the truck burned on the last three kilometres it drove. It shows `---` until the truck has driven half a kilometre. Fuel burned while standing comes off the range and leaves the litres per kilometre alone. The numbers are small, as SnowRunner's trucks are thirsty: a scout that burns 10 litres a kilometre off road gets about 7 km out of its 75 litre tank.
- Lamps for the handbrake, the headlights, all wheel drive, the differential lock and the low range. The engine damage lamp lights when the game's engine damage is above half of what the engine takes.
- The tilt lamp: a small truck seen from behind that leans as yours does. It is green while the truck stands level, amber from 15 degrees of lean to a side and red from 25, or from 35 degrees up or down a slope. A bump does not light it.

The cluster is on screen while you drive a truck in the world, and it fades in and out. It is hidden on loading screens, in the pause menu, on the map and in the garage.

## Settings

The Gauges tab in ReShade's overlay edits every setting while you play. Changes count at once and are saved to `Gauges.ini` in the same folder. The game writes that file with the defaults and a short guide on its first start, so it can be edited by hand too.

| Setting | In the ini | Default |
|---|---|---|
| Layout: a strip along the bottom centre, or a block in the bottom right corner | `Layout` 0 or 1 | strip |
| Style: solid dial faces, tinted see-through faces, or open (no faces, only scales, numbers and needles) | `Style` 0, 1 or 2 | open |
| How dark the tinted faces are | `FaceOpacity` 0 to 0.85 | 0.35 |
| Position, in pixels of a 1080p screen from the layout's own place | `OffsetX`, `OffsetY` | 0, 0 |
| Size | `Scale` | 1 |
| Opacity of the whole cluster | `Opacity` 0.1 to 1 | 1 |
| Miles per hour, psi, Fahrenheit | `Mph`, `Psi`, `Fahrenheit` | off: km/h, bar, Celsius |
| The temperature, boost and air dials and the lamps | `ShowTemp`, `ShowBoost`, `ShowAir`, `ShowLamps` | on, on, off, on |
| Truck class, which sets the scales | `TachClass` 0 truck, 1 scout, 2 heavy | truck |
| Own numbers for idle, the start of the red zone, the tachometer's top and the speedometer's top | `IdleRpm`, `RedlineRpm`, `MaxRpm`, `SpeedMax` | 0: the class's own |
| The tilt lamp: amber and red from this many degrees of lean to a side, red from this many up or down a slope | `RollWarnDeg`, `RollDangerDeg`, `PitchDangerDeg` | 15, 25, 35 |
| A key that hides and shows the cluster | `ToggleKey`, a virtual key code | none |
| Pad buttons that do the same | `PadToggle`, as `LB+DPadUp` | None |
| For work on the mod: demo values whenever no truck is driven, in menus and on loading screens too | `Demo` | off |
| For work on the mod: a log of the values read from the game | `Probe` | off |

The scales by class:

| | Truck | Scout | Heavy |
|---|---|---|---|
| Idle | 650 rpm | 800 rpm | 600 rpm |
| Red zone from | 2200 rpm | 5500 rpm | 1900 rpm |
| Tachometer top | 3000 rpm | 7000 rpm | 2500 rpm |
| Speedometer top | 100 km/h | 120 km/h | 80 km/h |

The cluster keeps its share of the screen: it is twice as large in pixels on a 2160p screen as on a 1080p one.

The pad is only read. The game sees the toggle buttons too, so pick buttons it does not use while driving.

## With the SnowRunner Tire Inflation System

The two mods are made to run side by side. Each is its own `.asi` file and its own ReShade add-on with its own tab, ini and log. One `version.dll` loads both. This mod does not touch the pad's buttons, so that mod's panel keeps working as before.

## Remove

Delete `Gauges.asi`, `Gauges.ini` and `Gauges.log` from `Sources\Bin`.

## Notes

- The mod changes no game file and writes nothing into the game's memory.
- If something does not work, `Gauges.log` in `Sources\Bin` says what the mod found and did.
- The game's own HUD stays as it is. Its "Immersive Mode" option hides it while it is not in use.

## Build

Windows with Visual Studio 2022 (C++ desktop tools).

```
build.bat
test.bat
```

`build.bat` makes `out\Gauges.asi`, the four offline test programs in `out\test`, and `out\version.dll`, the loader, from the sources in `tools`. `test.bat` runs the tests: every setting through the ini and back, the reader of the game's memory against a made up game, the engine speed, the range and the tilt on made up drives, and a preview that draws the cluster and the settings tab into PNG files in `out\preview` without the game. The preview needs the full Dear ImGui sources in `deps\imgui_src`, see `test\build_preview.bat`. Without them the rest is still built.

The headers the mod is built with are in `deps`, each with its licence: the ReShade add-on API, Dear ImGui, and stb_truetype with stb_rect_pack.

## How it works

A thread in the game process reads the driven truck's values from the game's memory 20 times a second, works out the engine speed, the range and the tilt from them, and keeps the settings. ReShade calls the mod on every frame, and the mod draws the cluster with Dear ImGui's drawing calls, which ReShade hands to its add-ons. The text is baked from the game's own font files when the game starts, at the size the cluster has on your screen.

## Licence and credits

GNU General Public License v3.0 (GPL-3.0-only), see `LICENSE`.

The cluster is drawn through ReShade by Patrick Mours (crosire) with Dear ImGui by Omar Cornut, and its text is baked with stb_truetype by Sean Barrett. Their notices are in `THIRD_PARTY_NOTICES.md`. The loader in `tools` is the one of the SnowRunner Tire Inflation System.

SnowRunner is a game by Saber Interactive. This project is not affiliated with Saber Interactive or Focus Entertainment and contains no game files.
