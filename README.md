# SnowRunner Gauges

A gauge cluster for the truck you drive in SnowRunner: analogue dials in the style of a truck dashboard, drawn over the game in the game's own font.

The cluster has a speedometer, a tachometer with the gear, fuel with the range, coolant temperature, boost pressure, brake air pressure and eight lamps: handbrake, headlights, all wheel drive, differential lock, low gear, engine damage, cold engine and tilt.

This is version 0.10.0. It reads the truck's own values for the speedometer, the gear, the fuel gauge and seven of the lamps: handbrake, headlights, all wheel drive, differential lock, low gear, engine damage and tilt. The game has no engine speed, no range, no coolant temperature and no boost pressure, so the mod works those out from what the truck does, and lights the cold lamp by the temperature. The temperature also costs the engine: overheating it, or flooring it cold, adds to the game's own engine damage. `Damage=0` switches that off. The scales of the tachometer and the speedometer and the turbo's boost can be set for each kind of truck. Brake air pressure comes with a later version and rests until then.

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
- Lamps for the handbrake, the headlights, all wheel drive, the differential lock and the low range. The engine damage lamp lights when the game's engine damage is above half of what the engine takes, and it blinks while the mod adds damage.
- The tilt lamp: a small truck seen from behind that leans as yours does. It is green while the truck stands level, amber from 15 degrees of lean to a side and red from 25, or from 35 degrees up or down a slope. A bump does not light it.
- The coolant temperature. The game has none, so the mod works it out from how hard the engine works and how fast the truck drives. A cold engine starts at the outside temperature and is at 85 C after about three minutes of driving. The thermostat then holds it: about 86 C at idle, near 100 C when the truck works hard at speed. The needle goes into the red zone from 118 C only when the pedal stays floored at a crawl. At a standstill with the engine revving out that takes a minute and a half from 90 C, and longer when the engine pulls at lower revs or the truck still moves. From about 7 km/h up the needle stays out of the red zone: a truck in the low range at its own top speed runs hot and settles just under it. Off the pedal the engine is back at 100 C within a minute. Water cools it too: from about half a metre of water at the truck's front, and with the engine standing in it (1.2 m and more) a hot engine is back at 100 C within half a minute, pedal floored or not. A stopped engine cools from 90 C to 40 C in a quarter of an hour, and five times as fast in deep water. Each kind of truck keeps its own temperature while the game runs: one you left cools while you drive another, and is as warm as that when you take it again, out of the garage too. The mod knows a truck by its kind, so two trucks of the same kind share one temperature.
- The cold lamp is lit while the coolant is below 60 C.
- Boost pressure. The game has none, so the mod works it out from the pedal and the engine speed. The turbo makes nothing near idle and all it can from about two thirds of the way to the red zone, with the pedal floored. The needle lags behind the pedal: floored at the red zone the boost is there in under a second, and pulling away from a standstill it takes a second or more. It falls away within half a second when the foot comes off, and in neutral the turbo makes a quarter of its boost at most. How much a truck's turbo reaches is the setting `BoostMax`: trucks 1.8 bar, scouts 1, heavy trucks 2.
- The turbo's flutter. Take your foot off the pedal under boost and the air beats back through the compressor wheel, slower and fainter until it is gone. The more boost the lift had, the longer and louder, and no two in a row are the same. It stops when the pedal goes down again.
- Engine damage from the temperature. An engine in the red zone loses 10 percent of what it can take every minute at 125 C. It loses nothing at 118 C, less than that between the two and more above: floored at a standstill with the wheels spinning, an engine is used up about five minutes after it enters the red zone. A cold engine suffers under a heavy pedal: below 60 C, with the pedal more than half down, it loses up to 5 percent a minute (floored at 0 C), and less the warmer it is. A cold start driven with the pedal floored costs 2 to 3 percent of the engine, and one driven at half pedal costs nothing. On a keyboard the pedal is either up or floored, so every cold start counts as floored there, and `ColdDamagePercentPerMinute` set to 0 switches that part off. All of it goes into the game's own engine damage, so a garage repairs it, and past the engine's own threshold the game takes torque away and burns more fuel, as with any engine damage. The mod stops one point short of what the engine takes and never stops the engine itself. With `Damage` off the temperature is only shown.

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
| Truck class, which sets the scales. Each kind of truck can have its own, see below | `TachClass` 0 truck, 1 scout, 2 heavy | truck |
| Own numbers for idle, the start of the red zone, the tachometer's top and the speedometer's top. Each kind of truck can have its own | `IdleRpm`, `RedlineRpm`, `MaxRpm`, `SpeedMax` | 0: the class's own |
| The boost in bar the turbo reaches with the pedal floored at high engine speed, from 0.3 to 2. Each kind of truck can have its own | `BoostMax` | 0: the class's own |
| The tilt lamp: amber and red from this many degrees of lean to a side, red from this many up or down a slope | `RollWarnDeg`, `RollDangerDeg`, `PitchDangerDeg` | 15, 25, 35 |
| The coolant: minutes a cold engine takes from 15 C to 85 C when it is driven as usual, minutes from 90 C into the red zone with the pedal floored and the wheels spinning at a standstill, minutes a stopped engine takes from 90 C to 40 C | `WarmUpMinutes`, `OverheatMinutes`, `CoolDownMinutes` | 3, 1.5, 15 |
| The outside temperature in degrees C, from 40 below to 50 | `AmbientC` | 15 |
| The speed in km/h from which the radiator gets all the air that driving gives it, from 10 to 80 | `AirFullKmh` | 30 |
| The temperature costs the engine | `Damage` 0 or 1 | on |
| Percent of the engine lost a minute at 125 C, and with the pedal floored on a cold engine at 0 C, each from 0 to 100 | `HotDamagePercentPerMinute`, `ColdDamagePercentPerMinute` | 10, 5 |
| How loud the turbo's flutter is, from 0 to 100. 0 = no sound | `SoundVolume` | 50 |
| The sounds without the cluster | `SoundsOnly` 0 or 1 | off |
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
| Boost | 1.8 bar | 1 bar | 2 bar |

Each kind of truck can have its own scales: the class and the five numbers. Change one of the six on the tab while you drive a truck, and all six are kept for that kind of truck. They are back whenever you drive one: after a recovery, out of the garage, after a change of trucks and after a new start of the game. The Scales section of the tab names the truck it sets and says whether the scales are its own. Its button "Back to the plain scales" lets the truck give them up.

The plain scales are the ones in the ini's `[Gauges]` section. Every truck without scales of its own shows them, and the tab sets them while no truck is driven: in the game's main menu and in the garage. A truck's own scales stand in a section of their own, named after the truck as the game's files name it:

```
[Truck:chevrolet_ck1500]
TachClass=1
IdleRpm=0
RedlineRpm=4500
MaxRpm=0
SpeedMax=0
```

`Gauges.log` names the truck each time you take another one. A section can be written by hand as well, and a key it leaves out is taken from `[Gauges]`. Every other setting is the same for all trucks.

Each of the three coolant times changes one thing: how fast a cold engine warms up, how fast a hot one overheats, how fast a stopped one cools. The temperatures the engine settles at stay as they are. The outside temperature moves those: in frost the engine takes longer to warm up and cannot be overheated, and on a hot day it overheats sooner. `AirFullKmh` moves them at a crawl: lower keeps a slow, hard pulling engine cooler, and higher makes it overheat at speeds where it now stays out of the red zone.

The cluster keeps its share of the screen: it is twice as large in pixels on a 2160p screen as on a 1080p one.

The flutter comes in three strengths, for a lift at full boost, at half and at little, with three recordings of each. To hear recordings of your own instead, put them next to `Gauges.asi` as `sounds\turbo_high_1.wav` to `turbo_high_4.wav`, and `turbo_mid_` and `turbo_low_` likewise: WAV files, PCM or float, up to six seconds. A strength with at least one file of yours plays only yours. The mod brings every sound to the same loudness, and `Gauges.log` says which ones it plays. The Gauges tab has a button for each strength.

The pad is only read. The game sees the toggle buttons too, so pick buttons it does not use while driving.

## With the SnowRunner Tire Inflation System

The two mods are made to run side by side. Each is its own `.asi` file and its own ReShade add-on with its own tab, ini and log. One `version.dll` loads both. This mod does not touch the pad's buttons, so that mod's panel keeps working as before.

## Remove

Delete `Gauges.asi`, `Gauges.ini` and `Gauges.log` from `Sources\Bin`.

## Notes

- The mod changes no game file. With `Damage` off it writes nothing into the game's memory. With `Damage` on it writes one thing there: the engine damage of the truck you drive.
- `Damage` has not been tried in co-op. Leave it off there.
- If something does not work, `Gauges.log` in `Sources\Bin` says what the mod found and did.
- The game's own HUD stays as it is. Its "Immersive Mode" option hides it while it is not in use.

## Build

Windows with Visual Studio 2022 (C++ desktop tools).

```
build.bat
test.bat
```

`build.bat` makes `out\Gauges.asi`, the four offline test programs in `out\test`, and `out\version.dll`, the loader, from the sources in `tools`. `test.bat` runs the tests: every setting through the ini and back, the trucks' own scales, the reader of the game's memory and the write of engine damage against a made up game, the engine speed, the range, the tilt, the coolant temperature and the engine damage on made up drives, and a preview that draws the cluster and the settings tab into PNG files in `out\preview` without the game. The preview needs the full Dear ImGui sources in `deps\imgui_src`, see `test\build_preview.bat`. Without them the rest is still built.

The headers the mod is built with are in `deps`, each with its licence: the ReShade add-on API, Dear ImGui, and stb_truetype with stb_rect_pack.

## How it works

A thread in the game process reads the driven truck's values from the game's memory 20 times a second, works out the engine speed, the range, the tilt and the coolant temperature from them, and keeps the settings. With `Damage` on, the same thread writes the engine damage that the temperature caused into the game, at most once a second and only while a truck is driven. ReShade calls the mod on every frame, and the mod draws the cluster with Dear ImGui's drawing calls, which ReShade hands to its add-ons. The text is baked from the game's own font files when the game starts, at the size the cluster has on your screen.

The sounds are played through XAudio2 2.9, the copy the game brings or Windows' own, on a thread of their own. The mod takes the audio device when a sound is due and lets go of it a few seconds after the last one.

## Licence and credits

GNU General Public License v3.0 (GPL-3.0-only), see `LICENSE`.

The cluster is drawn through ReShade by Patrick Mours (crosire) with Dear ImGui by Omar Cornut, and its text is baked with stb_truetype by Sean Barrett. Their notices are in `THIRD_PARTY_NOTICES.md`. The loader in `tools` is the one of the SnowRunner Tire Inflation System.

`SOUND_CREDITS.md` says where each sound comes from.

SnowRunner is a game by Saber Interactive. This project is not affiliated with Saber Interactive or Focus Entertainment and contains no game files.
