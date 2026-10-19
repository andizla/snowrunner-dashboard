# SnowRunner Gauges

A gauge cluster for the truck you drive in SnowRunner: analogue dials in the style of a truck dashboard, drawn over the game in the game's own font.

The cluster has a speedometer, a tachometer with the gear, fuel with the range, coolant and oil temperature, boost pressure, brake air pressure and nine lamps: handbrake, headlights, all wheel drive, differential lock, low gear, engine damage, cold engine, tilt and oil temperature.

This is version 0.15.8. It reads the truck's own values for the speedometer, the gear, the fuel gauge and seven of the lamps: handbrake, headlights, all wheel drive, differential lock, low gear, engine damage and tilt. The game has no engine speed, no range, no coolant or oil temperature, no boost pressure and no brake air, so the mod works those out from what the truck does, and lights the cold and oil lamps by the temperatures. The temperature also costs the engine: overheating it, or working it hard while cold, adds to the game's own engine damage. `Damage=0` switches that off. The scales of the tachometer and the speedometer and the turbo's boost and character can be set for each kind of truck. The brake air comes from the engine's compressor, which fills the tanks up to 8.2 bar and starts again at 6.9, and each press of the brake and letting the handbrake go draw from them; scouts brake by hydraulics, so the air dial is hidden on scout scales (`TachClass=1`, or a truck file that sets it). The outside temperature goes by the map you are on, which the mod reads from the game's save. With `Gearbox=1` an automatic of the mod's own, with a torque converter, picks the gears while the game's shifter is in A, and with `Powertrain=1` the truck pulls with the mod's engine and converter in place of the game's own.

## What you need

- SnowRunner for Windows: the Steam, Epic Games Store or Microsoft Store (Xbox app) version. The mod is made on the Steam build of 22 July 2026 and knows where that build keeps what it reads. On any other build (another store's, or after a game update) it finds those places by the game's code that uses them, and says in its log what it found; where it cannot find the truck it stands down and changes nothing. The signatures it finds them by were taken from the Steam build; another build stands down only where the truck, its wheels or the game's state cannot be found, and its clock is looked for with `Probe=1` (see the outside temperature).
- ReShade 6.8.0 or newer, the build "with full add-on support", installed for SnowRunner. It loads the mod and draws the cluster and its settings tab. Nothing else is needed.

## Install

1. Close the game.
2. Copy `Gauges.addon64` into the game's `Sources\Bin` folder, next to `SnowRunner.exe`, where ReShade's `dxgi.dll` is. (A `ReShade.ini` that sets `AddonPath` loads add-ons from that folder instead.)
3. Start the game. The Home key opens ReShade's overlay, which has a Gauges tab, and its Add-ons tab lists Gauges.

To update, copy the new file over the old one. Before 0.11.3 the mod was `Gauges.asi`, loaded by an ASI loader: delete that file when you install `Gauges.addon64`. With two files of the mod in the folder only one runs, and the log says which. `Gauges.asi` is still in the release for a game set up with an ASI loader, and works the same.

## What it shows

- Speed, forwards and backwards.
- The gear: its number with an A in automatic, H for the high gear, L, L+ and L- for the low range, R and N.
- The engine speed. The game has none, so the mod works it out from the driven wheels, the gear and the pedal. The needle climbs through each gear to the red zone and falls at the shift. It rises under the pedal when the truck is stuck and revs freely in neutral, and spinning wheels take it into the red zone. It rises from 0 while the engine starts and runs down when the engine stalls or stops.
- Fuel as a share of the tank.
- The range: how far the fuel in the tank goes at the litres per kilometre the truck burned on the last three kilometres it drove. It shows `---` until the truck has driven half a kilometre. Fuel burned while standing comes off the range and leaves the litres per kilometre alone. The numbers are small, as SnowRunner's trucks are thirsty: a scout that burns 10 litres a kilometre off road gets about 7 km out of its 75 litre tank.
- Lamps for the handbrake, the headlights, all wheel drive, the differential lock and the low range. The engine damage lamp lights when the game's engine damage is above half of what the engine takes, and it blinks while the mod adds damage and for two seconds after each write.
- The tilt lamp: a small truck seen from behind that leans as yours does. It is green while the truck stands level, amber from 15 degrees of lean to a side and red from 25, or from 35 degrees up or down a slope. A bump does not light it.
- The coolant temperature. The game has none, so the mod works it out from the fuel the engine burns and how fast the truck drives. A real engine's coolant takes about a quarter to a third of the fuel's heat, and the fuel goes with the pedal and the engine speed together: floored at the red zone makes the most heat, floored at low revs little, and coasting with the foot off next to none, as a diesel burns next to nothing then. In neutral nothing pulls against the engine, and it makes a quarter of the heat. A cold engine starts at the outside temperature and is at 85 C after about three minutes of driving. The thermostat then holds it: about 86 C at idle, near 100 C when the engine works hard at speed. The needle goes into the red zone from 118 C only when the engine works hard at a crawl. Floored at a standstill with the wheels spinning and the engine at the red zone that takes a minute and a half from 90 C, and longer at lower revs, with the pedal not quite floored, or with the truck still moving. From about 7 km/h up the needle stays out of the red zone: a truck in the low range at its own top speed runs hot and settles just under it. Back at idle the engine is at 100 C again within a minute. Water cools it too: from about half a metre of water at the truck's front, and with the engine standing in it (1.2 m and more) a hot engine is back at 100 C within half a minute, at any engine speed. A stopped engine cools from 90 C to 40 C in a quarter of an hour, and five times as fast in deep water. Each kind of truck keeps its own temperature while the game runs: one you left cools while you drive another, and is as warm as that when you take it again, out of the garage too. The mod knows a truck by its kind, so two trucks of the same kind share one temperature.
- The cold lamp is lit while the coolant is below 60 C.
- The oil temperature, as a second, amber needle in the coolant's dial or on a dial of its own left of it. In a real engine the coolant takes its heat from the cylinders, and the oil takes the heat of the friction in the bearings and the pistons and of the jets that cool the pistons, and gives it to the coolant through the oil cooler. So the oil follows the coolant slowly and runs a few degrees over it: about 7 degrees over at cruise and 14 floored at the red zone, more the faster the engine turns and the harder it works. Driven as usual the oil is at 85 C after about five minutes, two after the coolant, and never before the coolant. A stopped engine's oil keeps its heat longer. Each kind of truck keeps its own, as with the coolant.
- The oil lamp blinks while the oil is too hot, from 120 C, and a warning sounds every four seconds until the oil is two degrees under that or the engine stops.
- Boost pressure. The game has none, so the mod works it out from the pedal and the engine speed. The turbo makes nothing near idle and all it can from about two thirds of the way to the red zone, with the pedal floored. The needle lags behind the pedal: floored at the red zone the boost is there in under a second, and pulling away from a standstill it takes a second or more. It falls away within half a second when the foot comes off, and in neutral the turbo makes a quarter of its boost at most. How much a truck's turbo reaches is the setting `BoostMax`: trucks 1.8 bar, scouts 1, heavy trucks 2. How it gets there is `BoostProfile`: the standard turbo, a small quick one that comes in early with little lag, a big slow one that comes in late and takes long at low revs, or two stages, a small turbo early and a big one after it.
- The turbo's flutter. Take your foot off the pedal under boost and the air beats back through the compressor wheel, slower and fainter until it is gone. The more boost the lift had, the longer and louder, and no two in a row are the same. It stops when the pedal goes down again.
- Engine damage from the temperature. An engine in the red zone loses 10 percent of what it can take every minute at 125 C. It loses nothing at 118 C, less than that between the two and more above: floored at a standstill with the wheels spinning, an engine is used up about five minutes after it enters the red zone. A cold engine suffers when it works hard: below 60 C, burning more than half the fuel it burns floored at the red zone, it loses up to 5 percent a minute (floored at the red zone at 0 C), and less the warmer it is and the lighter the work. A cold start floored at the red zone costs 2 to 3 percent of the engine; driving about at moderate revs costs nothing, floored or not, as the engine burns little fuel there. `ColdDamagePercentPerMinute` set to 0 switches that part off. All of it goes into the game's own engine damage, so a garage repairs it, and past the engine's own threshold the game takes torque away and burns more fuel, as with any engine damage. The mod stops one point short of what the engine takes and never stops the engine itself. With `Damage` off the temperature is only shown.

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
| The oil temperature: not shown, a second needle in the temperature dial, or a dial of its own | `OilGauge` 0, 1 or 2 | second needle |
| Truck class, which sets the scales. Each kind of truck can have its own, see below | `TachClass` 0 truck, 1 scout, 2 heavy | truck |
| Own numbers for idle, the start of the red zone, the tachometer's top and the speedometer's top. Each kind of truck can have its own | `IdleRpm`, `RedlineRpm`, `MaxRpm`, `SpeedMax` | 0: the class's own |
| The boost in bar the turbo reaches with the pedal floored at high engine speed, from 0.3 to 2. Each kind of truck can have its own | `BoostMax` | 0: the class's own |
| How the turbo builds its boost: standard, small and quick, big and slow, or two stages. Each kind of truck can have its own | `BoostProfile` 0, 1, 2 or 3 | standard |
| The tilt lamp: amber and red from this many degrees of lean to a side, red from this many up or down a slope | `RollWarnDeg`, `RollDangerDeg`, `PitchDangerDeg` | 15, 25, 35 |
| The coolant: minutes a cold engine takes from 15 C to 85 C when it is driven as usual, minutes from 90 C into the red zone floored at the red zone with the wheels spinning at a standstill, minutes a stopped engine takes from 90 C to 40 C | `WarmUpMinutes`, `OverheatMinutes`, `CoolDownMinutes` | 3, 1.5, 15 |
| The outside temperature in degrees C, from 40 below to 50, where the map's is not known | `AmbientC` | 15 |
| The outside temperature by the map you drive on (0: `AmbientC` on every map) | `AmbientByMap` | 1 |
| The speed in km/h from which the radiator gets all the air that driving gives it, from 10 to 80 | `AirFullKmh` | 30 |
| The oil: minutes it takes from 15 C to 85 C when the engine is driven as usual, from 1 to 60 and at least a minute after the coolant, and the temperature in C from which it is too hot, from 100 to 150 | `OilWarmUpMinutes`, `OilWarnC` | 5, 120 |
| The temperature costs the engine | `Damage` 0 or 1 | on |
| The mod's automatic picks the gears in A (see below) | `Gearbox` 0 or 1 | off |
| What you drive with, for the mod's automatic: a wheel's pedals, a pad or the keyboard (see below) | `Input` 0, 1 or 2 | a wheel's pedals |
| The truck pulls with the mod's engine and converter (see below) | `Powertrain` 0 or 1 | off |
| How much of the mod's engine the game gets with `Powertrain=1`, from 0 to 100 percent: 0 leaves the game's own pull | `EngineShare` | 50 |
| Percent of the engine lost a minute at 125 C, and by a cold engine floored at the red zone at 0 C, each from 0 to 100 | `HotDamagePercentPerMinute`, `ColdDamagePercentPerMinute` | 10, 5 |
| How loud the turbo's flutter and the oil warning are, from 0 to 100. 0 = no sound | `SoundVolume` | 50 |
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

Each kind of truck can have its own scales: the class, the five numbers and the turbo. Change one of the seven on the tab while you drive a truck, and all seven are kept for that kind of truck. They are back whenever you drive one: after a recovery, out of the garage, after a change of trucks and after a new start of the game. The Scales section of the tab names the truck it sets and says whether the scales are its own. Its button "Back to the plain scales" lets the truck give them up.

The plain scales are the ones in the ini's `[Gauges]` section. Every truck without scales of its own shows them, and the tab sets them while no truck is driven: in the game's main menu and in the garage. A truck's own scales stand in a file of their own in the folder `Gauges\trucks` beside `Gauges.ini`, named after the truck as the game's files name it (a modded truck's kind holds a stroke, `<name>/<mod id>`, which a file's name cannot: it is written `<name>@<mod id>.ini`). A file can be passed to another player and dropped into their folder as it is. `Gauges\trucks\chevrolet_ck1500.ini`:

```
[Truck]
Kind=chevrolet_ck1500
TachClass=1
IdleRpm=0
RedlineRpm=4500
MaxRpm=0
SpeedMax=0
BoostMax=0
BoostProfile=2
```

`Gauges.log` names the truck each time you take another one. A file can be written by hand as well, and a key it leaves out is taken from `[Gauges]`. Every other setting is the same for all trucks. Up to 0.13.9 the trucks' scales stood in `Gauges.ini` as sections such as `[Truck:chevrolet_ck1500]`: the first start of 0.14.0 moves them into their files.

A region can have an outside temperature of its own, in a section named after it, and a map can have one apart from its region's:

```
[Map:Alaska]
AmbientC=-12

[Map:Black River]
AmbientC=5
```

The names are the game's own English names. The mod writes a section for each of the game's regions into an ini that has none, with a first guess at its temperature, and reads which map you are on from the game's save, as the map it loaded last: Steam's `CompleteSave*.cfg` in Steam's `userdata\<account>\1465360\remote`, the Epic store's `CompleteSave*.dat` in `Documents\My Games\SnowRunner\base\storage\<account>`, or the Microsoft Store's save containers. Where it finds none, `Gauges.log` says which folders it looked in. `Gauges.log` and the Gauges tab name the map, its region and the section in force. A map of a mod goes by its id, such as `[Map:level_my_map]`. Where neither the map nor its region has a section, or before the mod has read the save, `AmbientC` of `[Gauges]` is in force; `AmbientByMap=0` (on the tab, "Outside by map" off) puts it in force on every map. While the map's temperature is in force, the tab's "Outside temperature" slider shows it and sets it in the section it comes from.

The air follows the game's day: coldest just before sunrise, warmest in the afternoon. A section can give both ends, `DayC` and `NightC`:

```ini
[Map:Scandinavia]
DayC=3
NightC=-12
```

A section with only `AmbientC` swings 10 C around it. The mod reads the game's clock where it was found for this version of the game. After a game update that moves it, the mod looks for it again while you drive with `Probe=1`: drive a few minutes, then skip the night on the map (T) or let time pass in a garage once, and `Gauges.log` says "the game's time of day is at ...". The mod keeps the place in `Gauges.ini` under `[Clock]`; until it is found the day's mean is in force. The tab shows the day's and the night's temperature and the hour of the game's day.

Each of the three coolant times changes one thing: how fast a cold engine warms up, how fast a hot one overheats, how fast a stopped one cools. The temperatures the engine settles at stay as they are. The outside temperature moves those: in frost the engine takes longer to warm up and cannot be overheated, and on a hot day it overheats sooner. `AirFullKmh` moves them at a crawl: lower keeps a slow, hard pulling engine cooler, and higher makes it overheat at speeds where it now stays out of the red zone.

The cluster keeps its share of the screen: it is twice as large in pixels on a 2160p screen as on a 1080p one.

The flutter comes in three strengths, for a lift at full boost, at half and at little, with three recordings of each. To hear recordings of your own instead, put them in the game's `Sources\Bin` folder as `sounds\turbo_high_1.wav` to `turbo_high_4.wav`, and `turbo_mid_` and `turbo_low_` likewise: WAV files, PCM or float, up to six seconds. A strength with at least one file of yours plays only yours. The oil warning is two short beeps, and `sounds\oil_beep_1.wav` plays in their place. The mod brings every sound to the same loudness, and `Gauges.log` says which ones it plays. The Gauges tab has a button for each strength and one for the oil warning.

The pad is only read. The game sees the toggle buttons too, so pick buttons it does not use while driving.

## The mod's automatic

The game's automatic shifts up at fixed speeds whatever the pedal, never shifts down under load until the engine bogs, and then drops to first. `Gearbox=1` (on the tab: Gearbox, Automatic, "The mod's automatic") hands the gears to an automatic of the mod's own while the game's shifter is in A:

- a torque converter: standing with the pedal floored the engine stops at the converter's stall speed (at most 2400 rpm), and on the move it follows the road speed with a little slip and locks up from the second gear;
- shifts up later the further the pedal is down. A light foot held steady cruises: it shifts up as soon as the next gear lands at 0.8 of the engine's peak torque speed or over, so the engine settles low at a steady speed, as a road car's automatic does. Lifting at speed shifts up a gear at a time where the next lands clear of the coast point; rolling downhill or slowing hard (mud, a climb) it keeps the gear, for the engine's braking. Coasting it holds the gear down to 0.15 of the way from idle to the red zone (0.25 with the brake on, so the engine brakes), then shifts down, gears at a time when the truck slows hard. Floored it takes the gear that pulls the most in the game: the game gives the engine's torque by how far the wheels are toward the gear's top speed, all of it at about half and nothing at the top, so floored it shifts up at about two thirds to four fifths of the gear's top and down where the gear below pulls more;
- on a climb (the truck slowing with your foot on) it shifts down where floored would and holds the gear a few seconds after, so it does not shift up and straight back down;
- a kickdown is a push from part throttle: the pedal a fifth of the way down or more for a moment, then to the floor. A press from the foot off, as every press of a key is, kicks nothing down and drives by the floored shift points, which shift down where the truck is slow;
- such a push of the pedal to the floor kicks down at once to the gear that pulls the most, as far as the engine can take; wheelspin in mud and wheels locked under the brake do not shift it, as it weighs the wheels against the truck's speed over the ground; the first gear by the time the truck stops;
- `Input` (on the tab: "Driven with") fits the kickdown to what you drive with. A pad's trigger held near its end wavers, so on a pad the push has to come from under three quarters. The keyboard has no part throttle: no kickdown, and a key held shifts as the pedal floored;
- no shift down ever takes the engine near the red zone at the speed the wheels turn;
- the tachometer shows the converter's engine speed, and the cluster shows A. A lift opens the lockup for a second, so the needle sits a little under the gear's speed, as a road automatic's does.

The mod sets its gears through the game's own shift function and puts the game's automatic down while it drives, so the game's shifter shows the gear's number. Picking a gear by hand (a number, L, reverse, neutral or the high gear) hands the gearbox back to you; A takes it again. Tapping the shift modifier does nothing: in the game's own automatic a tap forces a pulling gear, and with the mod's the game's pick is put straight back to the mod's gear. The pull is still the game's: the truck drives with the game's torque in the gear the mod picks. `Gauges.log` says when the mod takes and gives back the gears, and with `Probe=1` each shift.

It works with a manual transmission mod installed, as long as you drive in A.

## The mod's engine

The game's engine gives all of its torque with the wheels at about half of the gear's top speed, and less and less from there to nothing at the top, whatever the engine speed. `Powertrain=1` (on the tab: Gearbox, "Engine to the game") hands the game the mod's engine in its place: the torque curve of the truck's class against the engine speed the needle shows, and with the mod's automatic doubled by the converter at its stall, fading out as the truck catches up with the engine. Cold oil (up to 15 percent at 0 C) and a turbo without boost (35 percent of the torque comes from it) only cost torque where the pedal asks for more than they leave: at a light foot the engine gives what the pedal asks. Wheels spinning in mud keep the game's own fall toward the gear's top, which holds them back, as the lift goes by the truck's speed over the ground. So a gear revved high pulls as a real engine's would, to about 0.92 of the gear's top speed and from there less, to nothing at the top, as a diesel's governor cuts the fuel; pulling away and a gear too tall on a climb get the converter's push. A gear's top speed stays the game's: at it and past it the game gets its own factor back.

`EngineShare` sets how much of the mod's engine the game gets: at 50, the default, the truck pulls half way between the game's own engine and the mod's, and at 100 all of the mod's. Lower it where a truck pulls too hard.

With the engine handed over, the mod's automatic shifts as a truck automatic does:

- floored, it takes each gear to the red zone, or shifts up sooner where the next gear already pulls harder, by the engine's power (a profile that still gains power up to the red zone, as one built from a peak does, is revved near the top): the game's pull falls away toward a gear's top whatever the engine, so at a lower `EngineShare` the truck would otherwise hang short of the red zone; at a lighter foot it shifts up about as far along as the pedal is down: a fifth of the pedal, a fifth of the way from idle to there (a diesel at about 1500 rpm, as a road car's automatic shifts), but never under 0.8 of the engine's peak torque speed, where it begins to pull (see the engine profiles below);
- it shifts down where the gear below would come in clear of its own point up (15 percent under it), whatever the gearbox's steps, so it does not shift up and straight back down; at a light foot a diesel comes down to 1.15 times its idle before it shifts down (750 rpm on a truck's scales);
- the converter locks up from 1.3 times idle at a light foot, so a gentle shift up lands locked, and from half the red zone floored;
- one gear up at a time; going up through the gears, from a standstill or after a shift up, each gear is held at least two and a half seconds of moving (a scout one and a half, by the truck's class: the class of its own scales, and for a truck without them the class of its weight, a scout up to 6 tonnes, a truck under 16, a heavy truck from there; the tab's slider "Each gear going up", 1 to 3 seconds, sets it for the gearbox fitted, kept in its file `Gauges\gearboxes\<name>.ini` as `GearHold`), on the limiter if it comes to that (the game governs the wheels at the gear's top), so a light truck does not run through its gears in a moment; a gear a kickdown brings is not held; a kickdown goes to the lowest gear that keeps the engine under the red zone and short of where it would shift straight back up;
- a game's gearbox is set up for the game's own engine, with a first gear for crawling, so the automatic sets off in the lowest gear with at least a seventh of the top gear's speed, and lower where the engine is weak for the truck: its torque over the truck's mass, both read from the game's files (a scout of 9 tonnes sets off in second with an engine of 195000, in first with one of 100000). It takes the gears under that only when the truck is stuck: floored, slow and not gaining for a second. The mass is the truck's own, without addons, cargo or a trailer, so a loaded truck that cannot pull away in its start gear takes the first by the stuck rule;
- a truck losing speed with the pedal under 0.4 is not taken for one climbing, so a light foot does not bring the floored points and an early shift down;
- a shift up the next gear cannot hold, up a grade or through mud, that comes back down within six seconds with the foot on keeps the gear below: no shift up past it for eight seconds, on the limiter if it comes to that, as a loaded truck sits at its governed speed on a hill, then one more try; each try taken back doubles the wait, up to 30 seconds. Lifting the foot, a stop or a kickdown lets it go, and so does a try that holds. The log says each time.

Each engine can have a profile of its own, in its own file `Gauges\engines\<name>.ini` (the name the game's files give it, which the tab and `Gauges.log` show). The tab's Engine section sets it while the truck is driven, and a plot there shows the torque curve in force:

- `PeakTorqueRpm`: the engine speed of its peak torque, as its maker gives it (most engines' figures are published). The torque curve is built around it: a turbo diesel's broad plateau, from 0.85 to 1.5 times that speed, with three fifths of the torque at half of it and four fifths left at the red zone; a naturally aspirated engine's narrower hump around the peak. Without it the engine pulls by its class's curve;
- `Aspiration`: 0 turbo, 1 naturally aspirated. Naturally aspirated has no boost, so no boost dial and no turbo sounds, and all of its torque at once with the pedal;
- `TorqueCurve`: a whole curve of its own, written by hand as `x:y` points (`0.3:0.7,0.5:1,1:0.75`, x the engine speed over the red zone's, y the torque over its most), in place of the one from the peak.

```
[Engine]
Name=us_truck_old_engine_0
PeakTorqueRpm=1400
Aspiration=0
```

The scales (idle, the red zone, the tachometer's top) stay the truck's. Each engine's profile goes with the engine, so a truck fitted with another engine in the garage pulls by that one's. A file written or changed by hand counts from the tab's reload button, or the next time the truck is driven.

The mod does it through the game's torque factor, the share of its torque the game leaves a damaged engine: it writes the factor on every pass, at most six times the game's own (the game's own with the wheels at a gear's top or past it), and gives the game its own back when the setting goes off, the engine stops, a pull test is armed or another truck is driven. The game's own share stays in it: a damaged engine pulls less, as before. The probe line "powertrain" says what is written.

The Gearbox section of the tab also has a pull test, for the engine's torque the mod will hand to the game next. Press "Test the engine's pull", then on a long flat road drive in the fourth gear or higher and floor the pedal for seven seconds. The mod holds the gear, changes the game's torque factor (what it leaves a damaged engine) for a second at a time (halved, raised by half, at three quarters) and measures how hard the truck pulls, then puts the factor back. Lifting the foot, braking or another gear stops it. The numbers go into `Gauges.log`.

## With the SnowRunner Tire Inflation System

The two mods are made to run side by side, each with its own tab, ini and log. That mod's `version.dll` loads its `.asi` file; ReShade loads this one. This mod does not touch the pad's buttons, so that mod's panel keeps working as before.

The mod publishes a little of its state for other mods to read: a block of 64 bytes of shared memory named `Local\SnowRunnerGauges.State.<process id>`, written about 20 times a second. It holds the truck driven, the outside temperature in force, the coolant temperature, the engine speed and the boost. A tire mod by the same author reads the outside temperature from it. `src\state.h` has the layout.

From the version of that mod that publishes its own state, the cluster has a tenth lamp: a tire with the letter of the pressure mode you chose there, L for Low, R for Reduced, N for Normal, + for Increased. It is lit amber at Low and Reduced and green at Increased, dark at Normal, it blinks while the pressure is on its way, and it is red while the tires take damage or one is worn out. Without that mod, or with an older version of it, there are nine lamps as before.

## Remove

Delete `Gauges.addon64` (or `Gauges.asi`), `Gauges.ini`, `Gauges.log` and the folder `Gauges` (the setups of trucks, gearboxes and engines, and the older logs) from `Sources\Bin`.

## Notes

- The mod changes no game file. What it writes into the game's memory: with `Damage` on, the engine damage of the truck you drive; with `Gearbox=1`, the game's automatic flag and the gears, through the game's own shift function; with `Powertrain=1`, the game's torque factor of the truck you drive, on every pass; during the pull test, the torque factor, for seven seconds. With all of them off it writes nothing.
- `Damage` has not been tried in co-op. Leave it off there.
- If something does not work, `Gauges.log` in `Sources\Bin` says what the mod found and did. It is the log of the game's last start; at each start the one before moves into `Gauges\logs`, named by the date and time that start began (`Gauges 2026-10-12 21-29-11.log`), so none is lost. With `Probe=1` a long drive's log runs to 20 or 30 MB, and the folder is never emptied by the mod.
- The game's own HUD stays as it is. Its "Immersive Mode" option hides it while it is not in use.

## Build

Windows with Visual Studio 2022 (C++ desktop tools).

```
build.bat
test.bat
```

`build.bat` makes `out\Gauges.addon64` (and the same file as `out\Gauges.asi`), the four offline test programs in `out\test`, and `out\version.dll`, the loader, from the sources in `tools`. `test.bat` runs the tests: every setting through the ini and back, the trucks' own scales, the reader of the game's memory and the write of engine damage against a made up game, the engine speed, the range, the tilt, the coolant and oil temperatures, the oil warning, the engine damage and the turbo's profiles on made up drives, and a preview that draws the cluster and the settings tab into PNG files in `out\preview` without the game. The preview needs the full Dear ImGui sources in `deps\imgui_src`, see `test\build_preview.bat`. Without them the rest is still built.

The headers the mod is built with are in `deps`, each with its licence: the ReShade add-on API, Dear ImGui, and stb_truetype with stb_rect_pack.

## How it works

A thread in the game process reads the driven truck's values from the game's memory 20 times a second, works out the engine speed, the range, the tilt, the boost and the coolant and oil temperatures from them, and keeps the settings. With `Damage` on, the same thread writes the engine damage that the temperature caused into the game, at most once a second and only while a truck is driven. ReShade calls the mod on every frame, and the mod draws the cluster with Dear ImGui's drawing calls, which ReShade hands to its add-ons. The text is baked from the game's own font files when the game starts, at the size the cluster has on your screen.

The sounds are played through XAudio2 2.9, the copy the game brings or Windows' own, on a thread of their own. The mod takes the audio device when a sound is due and lets go of it a few seconds after the last one.

## Licence and credits

GNU General Public License v3.0 (GPL-3.0-only), see `LICENSE`.

The cluster is drawn through ReShade by Patrick Mours (crosire) with Dear ImGui by Omar Cornut, and its text is baked with stb_truetype by Sean Barrett. Their notices are in `THIRD_PARTY_NOTICES.md`. The loader in `tools` is the one of the SnowRunner Tire Inflation System.

`SOUND_CREDITS.md` says where each sound comes from.

SnowRunner is a game by Saber Interactive. This project is not affiliated with Saber Interactive or Focus Entertainment and contains no game files.
