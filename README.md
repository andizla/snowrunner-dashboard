# SnowRunner Gauges

A gauge cluster for the truck you drive in SnowRunner: analogue dials in the style of a truck dashboard, drawn over the game in the game's own font.

The cluster has a speedometer, a tachometer with the gear, fuel with the range, coolant temperature, boost pressure, brake air pressure and eight lamps: handbrake, headlights, all wheel drive, differential lock, low gear, engine damage, cold engine and tilt.

This is version 0.1.0, the foundation. The cluster is drawn, placed and set up in the game, but it shows demo values. Reading the truck's own values comes with the next versions.

## What you need

- SnowRunner for Windows, the Steam version (the game build of 22 July 2026). On any other build of the game the mod writes that into its log and does nothing.
- ReShade 6.8.0 or newer, the build "with full add-on support". The cluster and its settings tab are drawn through ReShade. Without ReShade nothing is drawn.
- An ASI loader in the game folder. The SnowRunner Tire Inflation System brings one (`version.dll`), and any other loader that loads `.asi` files from the game folder will do.

## Install

1. Close the game.
2. Copy `Gauges.asi` into the game's `Sources\Bin` folder, next to `SnowRunner.exe`.
3. Start the game. The Home key opens ReShade's overlay, which has a Gauges tab.

To update, copy the new file over the old one. Keep only one `.asi` file of this mod in the folder: a second copy under another name stands down and says so in the log.

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
| A key that hides and shows the cluster | `ToggleKey`, a virtual key code | none |
| Pad buttons that do the same | `PadToggle`, as `LB+DPadUp` | None |
| Demo values while no truck is driven | `Demo` | on |
| A log of the values read from the game | `Probe` | off |

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

- The mod changes no game file.
- If something does not work, `Gauges.log` in `Sources\Bin` says what the mod found and did.
- The game's own HUD stays as it is. Its "Immersive Mode" option hides it while it is not in use.

## Build

Windows with Visual Studio 2022 (C++ desktop tools).

```
build.bat
test.bat
```

`build.bat` makes `out\Gauges.asi`, the two offline test programs in `out\test`, and `out\version.dll`, the loader, from the sources in `tools`. `test.bat` runs the tests: every setting through the ini and back, and a preview that draws the cluster and the settings tab into PNG files in `out\preview` without the game. The preview needs the Dear ImGui sources, see `test\build_preview.bat`. Without them the rest is still built.

The headers the mod is built with are in `deps`, each with its licence: the ReShade add-on API, Dear ImGui, and stb_truetype with stb_rect_pack.

## How it works

A thread in the game process keeps the settings and the values to show. ReShade calls the mod on every frame, and the mod draws the cluster with Dear ImGui's drawing calls, which ReShade hands to its add-ons. The text is baked from the game's own font files when the game starts, at the size the cluster has on your screen.

## Credits

The cluster is drawn through ReShade by Patrick Mours (crosire) with Dear ImGui by Omar Cornut, and its text is baked with stb_truetype by Sean Barrett. The loader in `tools` is the one of the SnowRunner Tire Inflation System.

SnowRunner is a game by Saber Interactive. This project is not affiliated with Saber Interactive or Focus Entertainment and contains no game files.
