# Changelog

## [Unreleased]

- The overlay copies a swap chain's method table that another program has put at the start of its memory. Before, it drew nothing there (no cluster, no panel and no NEXT GEN menu on two players' PCs). When a table still cannot be copied, `Gauges.log` says in whose code the table and its Present lie, so the program can be named.
- After every load (the loading screen) every truck's engine starts as cold as the air outside, the coolant and the oil. The cold start of 0.16.1 went by the save, which names the new map minutes after the load; the known issue is closed.
- The torque factor's roof is 6 for every engine again. An engine profile's `FullPull` and the tab's "Full pull up to" are gone: at 98 percent the roof was 24, and a truck got a kick at the top of 2nd to 5th gear (a d110, 25 October). `RevCap` and "Rev cap 95%" of 0.16.1 are gone too: the cap went by the engine's modelled speed, the kick by the wheels' share of the gear, and it never stopped one. For an engine with several times a stock engine's torque, lower `Torque`.
- The mod finds its places on the Microsoft Store and Game Pass build the way Next Gen Tire Inflation does: each place by two more patterns when the first misses, and the optional ones left out with a line in the log.

## [0.16.1] 2026-10-25

- `RevCap`, the tab's "Rev cap 95%" under Engine to the game, off by default: with the mod's engine the pull ends at 95 percent of the way from idle to the red zone and the automatic shifts up there floored. For a truck that shoots from 40 to 80 km/h on its own, a game bug that RLM-like engines and this mod make worse.
- `Torque`, the tab's "Torque" slider under Engine, per engine in `Gauges\engines\<name>.ini`: how much of its torque the truck gets with the engine handed to the game, 10 to 100 percent (100 when not set). Lower it for an RLM-like engine with several times a stock engine's torque: at 25 an engine with four times the torque pulls like the stock one. The set-off gear follows it. The tab's torque plot is labelled Curve now.
- On another map every truck's engine is meant to start as cold as the air outside. Known: it does not take effect yet (a drive on 25 October kept the engine warm over a map change); the code stays in and a fix follows.

## [0.16.0] 2026-10-24

- The mod draws the cluster and its settings with its own overlay, Dear ImGui compiled in, on the game's DirectX 11 device, no longer through ReShade's overlay. It is now `Gauges.asi`, loaded by an ASI loader such as the SnowRunner Tire Inflation System's `version.dll`; a `Gauges.addon64` from before should be removed (one left beside it stands down). The release brings ReShade 6.8.0 without its start banner and with a `ReShade.ini` that loads no effects and keeps Home for ReShade's own window, and the mod takes the game's swap chains from it, drawing after ReShade and its add-ons. Without ReShade the mod draws on its own. The game can then crash with the Steam, Discord or RivaTuner overlay or with multiple monitors.
- When ReShade is missing, takes no add-ons, or another program's `dxgi.dll` is in its place, the mod says so at the start: in a line at the top for 15 seconds and in the NEXT GEN menu, with what to do, and in its log.
- The NEXT GEN menu opens on F2. A `NextGen.ini` from a test build that still holds that build's default key (Delete or Page Down) moves to F2 once; a fresh `NextGen.ini` is made so that a Delete or Page Down set in it by hand stays. The cluster's toggle key does nothing while the menu is open.
- With Next Gen Graphics installed the NEXT GEN menu has a Graphics tab with its switches and status lines. Its F keys are off while the tab is there.
- The settings are in the Gameplay tab of the NEXT GEN menu, which F2 opens and closes. The Next Gen Tire Inflation mod, where installed, has its tab in the same window. The key is set in `NextGen.ini` next to the game's exe, or in the menu: click the key's name at its top right, then press the new key. While the menu is open the game gets no mouse or keys.
- Works alongside other mods that draw over the game. A minimap mod with a ReShade add-on mode of its own needs that mode on next to ReShade: its DirectX probe at start otherwise ends the game while ReShade sets up its hooks.
- Should one Next Gen mod's overlay fail, its tab leaves the NEXT GEN menu and the other mod's tab shows; with no tab left the menu closes, so the game never loses its mouse and keys to a menu nobody draws. The log says where it failed.
- The loader in the zip, `version.dll`, loads the mods into the game only. SnowRunner's crash reporter and its other programs in `Bin` load it too: they get no `.asi` file, and `AsiLoader.log` keeps the game's lines, with one line added when the crash reporter starts.
- Works with the Steam overlay of Steam's client from 21 October 2026. Without ReShade it ended the game at its first frame on a tester's PC: Steam hooked both DirectX's Present and the mod's, and the two called each other until the game ran out of stack. The mod now hooks only the swap chains the game makes, each through a method table of its own, and leaves DirectX's as they are.
- Two Next Gen mods that start in the same moment take turns at the game's import table, so neither finds it locked again under its own write, which would have ended the game at start.
- A Next Gen mod or tester build from before 23 October next to this one is named in the log and keeps a menu of its own: its hook in DirectX's table can bring back the crash with the Steam overlay, so every Next Gen mod should be the same release.
- The torque factor and the automatic flag the mod writes go only to the drive train the truck still has. When the game rebuilt it under the truck (a gearbox fitted), the mod could write into the old one for up to half a second, memory the game had given back.
- The pull test gives a truck its torque factor back when another truck is taken or the truck is lost mid-test; it was left changed before.
- On a build of the game the mod has no numbers for, the search for the game's places reads a copy of the game's code only, so a page it cannot read no longer ends the game, and a place found outside the game's data is not taken.
- Season 19's Switzerland (Grüntal Hollow, Schneetal and Eiswandspitze, from the game's update of 20 October 2026) has its outside temperature: a `[Map:Switzerland]` section, -10 C to start, which an existing `Gauges.ini` gets once (`MapRegionsOffered` in `[Gauges]` remembers it, so a section taken out by hand stays out). Until now those maps had none and fell back to `AmbientC`. Grainwoods River has its name in the log and the tab, and the log writes names such as Grüntal Hollow as they are.
- The log names the mod Next Gen Gameplay.

## [0.15.11]

- A look for the game's places that misses one the mod needs is made again every 2 seconds, 31 looks in all, before the mod stands down. Loaded early, as through another mod's DirectX probe (a minimap mod, on the game's build of 20 October 2026), it looked before the game's code was ready, found nothing and stood down; the next start without that mod found every place. The look runs on the game's own code, so no offline test covers it.

## [0.15.10]

- Floored, with the engine handed over, the mod's automatic shifts up where the next gear pulls more, or on the limiter where none does. The cap at 0.95 of the way from idle to the red zone is gone: it came before the fade toward a gear's top was worked out, and with the torque factor's roof at 6 it sat just over where the next gear pulls more anyway (a tester's ZIL-131: 3024 to 3056 rpm against the cap's 3072), so shifts hardly move.
- An engine's own `FullPull`, the tab's "Full pull up to" in its Engine section: how far toward a gear's top it keeps all its torque floored before it fades out, 92 to 98 percent (92, the roof of 6, for every engine at first). The torque factor's roof follows (about 10 times the game's own at 95, 24 at 98), and floored the automatic shifts up later with it. A tester's ZIL-131 shifted at about 3000 rpm of its 3200 red zone; a petrol engine that makes its power up to its red zone can now keep its pull there.

## [0.15.9]

- Driven with the keyboard (`Input`, "Driven with" on the tab), a lift shifts up only once the key has been let go for 2 seconds, and a lift's shift up that the next press or the brake takes back within 6 seconds stops lifts shifting up for 10 seconds, twice as long each time again, up to a minute. A tester's keyboard drive on 0.15.8 had 19 of 23 lift upshifts go straight back down, most within 4 seconds: a key tapped to hold the speed (376 of 412 lifts were under a second) shifted up at a lift and down at the next tap, the fourth and the fifth of a ZIL-131 swapping. A wheel's pedals and a pad shift as in 0.15.8. A test taps the key the same way, and lifts a wheel's pedal.

## [0.15.8]

- The mod's automatic cruises low: a light foot held steady shifts up as soon as the next gear lands at 0.8 of the engine's peak torque speed or over (and clear of its point down), without waiting for the light point up. A car's nine-speed automatic, filmed on the road, cruised at 1200 to 1300 rpm against a peak at about 1600.
- Lifting at speed shifts up a gear at a time where the next lands clear of the coast point; before, with the foot off it never shifted up short of the red zone. Rolling downhill or slowing hard (mud, a climb) it keeps the gear, for the engine's braking.
- Coasting holds the gear down to 0.15 of the way from idle to the red zone (was 0.25); with the brake on it shifts down from 0.25, as before. The same car coasted in its gear to 1000 to 1300 rpm of a 4500 rpm red zone.
- A lift opens the converter's lockup for a second, the needle a little under the gear's speed meanwhile, as a road automatic's is at a tip-out.
- With `Powertrain=1` the torque factor goes up to six times the game's own (was three), so the engine's torque holds to about 0.92 of a gear's top speed (was 0.84) before falling to nothing at the top, as a diesel's governor does; with the wheels at a gear's top or past it, rolling downhill, the game's own factor is written. A tester's log had the factor at the old roof in 1133 readings, nearly all near the red zone. Floored, the automatic takes each gear nearer its top accordingly.
- Checked: floored shifts up with the engine handed over follow its curve, by its power. A diesel profile built from a peak keeps 0.8 of its torque at the red zone and still gains power up to it, so it revs near the top; a profile whose power falls before the red zone shifts up sooner. A test holds both.
- The Gauges tab's engine damage sliders go to 100 percent a minute for overheating and 60 for a cold engine (they stopped at 50 and 30), for a harder game. Gauges.ini takes up to 100 for both, as before.
- Under the mod's automatic the needle goes no further past the red zone than wheelspin takes it without (1.1 of the way from idle to the red zone). The torque converter's engine speed has no top of its own, and with the wheels spinning in a low gear it took the needle to the end of the dial.
- The brake air dial's red zone comes from the air model's own figure (5.5 bar, the same as before).
- The readme says where another build of the game stands down (only without the truck, its wheels or the game's state), how far a diesel comes down at a light foot (1.15 times idle), that the damage lamp also blinks for two seconds after a write, and that the air dial is hidden on scout scales. Comments that still said the engine and the automatic are not handed to the game say they are.

## [0.15.7] 2026-10-13

- A tire lamp, with a tire inflation mod by the same author from the version of it that publishes its state: a tire with the letter of the pressure mode chosen there (L, R, N, or + for Increased). Amber at Low and Reduced, green at Increased, dark at Normal, blinking while the pressure is on its way, red while the tires take damage or one is worn out. Without that mod the cluster has its nine lamps as before.

## [0.15.6] 2026-10-12

- A new setting says what you drive with, so the mod's automatic fits it: `Input`, on the tab "Driven with" under Gearbox. A wheel's pedals (0, the default): as before. A pad (1): the trigger has to come from under three quarters before a push to the end kicks down, as one held near its end wavers past the mark. The keyboard (2): no kickdown at all, and a key held shifts as the pedal floored, however the game ramps it.

## [0.15.5] 2026-10-12

- The automatic kicks down only on a push from part throttle: the pedal a fifth of the way down or more for 0.3 seconds, then past nine tenths. Until now any push after 0.3 seconds short of the floor kicked down, the foot off included, so on a keyboard every press of the key after a lift did. A press from the foot off now drives by the floored shift points, which still shift down where the truck is slow.
- `update.bat` follows `main` on GitHub. Commits made in the folder go up first, GitHub's come down, and only the two apart stop it.

## [0.15.4] 2026-10-12

- Every start's log is kept. `Gauges.log` is still the log of the game's last start; at each start the one before moves into `Gauges\logs`, named by the date and time that start began (`Gauges 2026-10-12 21-29-11.log`, the day before for a drive past midnight). The mod rewrote `Gauges.log` at each start, so a tester's log was lost unless copied out before the next one. The log's second line says where the last one went. The mod never empties the folder: with `Probe=1` a long drive's log runs to 20 or 30 MB.

## [0.15.3] 2026-10-12

- The mod publishes its state for other mods in shared memory, `Local\SnowRunnerGauges.State.<process id>`: the truck driven, the outside temperature in force (the map's by map and hour, or `AmbientC`), the coolant temperature, the engine speed and the boost, about 20 times a second. A tire mod by the same author reads the outside temperature from it. `Gauges.log` names the block at the start.

## [0.15.2] 2026-10-12

- The automatic no longer hunts up a grade or through mud. A tester's drives (0.15.0) had 37 of 119 shifts up come back down within six seconds with the foot on: a Tayga swapped its first and second 18 times in six minutes, another truck every second up a climb. Floored, the first shifted up near its top, the second could not hold the speed on the grade, fell under its point down in about a second, and the first, not held after a shift down, was at its point up again a second later. Now a shift up taken back down with the foot on within six seconds keeps the gear it left: no shift up past it for eight seconds, the limiter's neither, so the engine sits at its governed speed as a loaded truck's does on a hill, then one more try. Each try taken back doubles the wait, up to 30 seconds. A lift of the foot, a stop, a kickdown or a try that holds lets it go. The log says each time, and the probe's automatic line shows the gear kept and for how long. On the test's climb it shifts up 3 times in 40 seconds, where it shifted 8.
- A truck without scales of its own shifts by the class of its weight, read from the game's files: a scout up to 6 tonnes, a truck under 16, a heavy truck from there. That sets each gear's hold going up (a scout 1.5 seconds, the others 2.5) and the engine's torque curve where the engine has no profile of its own. The tester's plain scales were a scout's, so a 12 tonne Tayga and a 20 tonne Western Star shifted like scouts. A truck with scales of its own shifts by their class, as before, and where the weight is not read the plain scales' class holds. A scout heavier than 6 tonnes (some modded ones are) shifts as a truck until it has scales of its own. The log's truck line names the class the automatic goes by, and the tab says where it comes from.
- `update.bat` sets edits made in the repository's folder aside for the merge and puts them back after it. An edit of `MODULES.md` there stopped the update at the merge ("Your local changes ... would be overwritten"), and nothing was installed. A file the branch changed too is left as the branch has it, and the edits stay in git's stash; the window says which.
- Not changed: the "6 to 1" and "4 to 1" lines in the tester's logs were the game's own automatic picking a gear right after the mod took the gears (its A flag up again, likely the pad's gear selector), which the mod put back on the same pass.

## [0.15.1] 2026-10-12

- The outside temperature by map works on the Epic store's build. Its saves are `CompleteSave*.dat` in `Documents\My Games\SnowRunner\base\storage\<account>`, where Steam's are `CompleteSave*.cfg`; the mod looked in that folder for `.cfg` alone, found nothing, and kept `AmbientC` in force (a tester's two logs: "no save of the game was found"). It now takes both, the newest of them. Documents is looked for where the shell has it (OneDrive may have moved it) and in the Windows profile folder as well.
- Where no save is found, the log says which folders were looked in; a save found that names no map the mod can read is said once. At the start the log names the game's folder and the store its path suggests. The Windows profile folder stands in the log as `%USERPROFILE%`, so a log passed on does not carry the account's name.
- The tester's logs carry the Steam build's exe stamp, and the mod knew the game's places without the signatures. If the tester plays the Epic build, as arranged, that build runs the same exe; the next log's line naming the game's folder will say.

## [0.15.0] 2026-10-11

- Engine profiles. Each engine can have a profile of its own in `Gauges\engines\<name>.ini`, set on the tab's new Engine section while the truck is driven: the engine speed of its peak torque (`PeakTorqueRpm`), as its maker gives it, and its aspiration (`Aspiration`, turbo or naturally aspirated). The torque curve is built around the peak: a turbo diesel's broad plateau from 0.85 to 1.5 times its speed, a naturally aspirated engine's narrower hump. A curve can also be written whole (`TorqueCurve`). A naturally aspirated engine has no boost, no boost dial and no turbo sounds. A plot on the tab shows the curve in force. Without a profile an engine pulls by its class's curve, as before.
- With the engine handed to the game, a light foot shifts up about as far along as the pedal is down: a fifth of the pedal, a fifth of the way from idle to the floored point. A scout's diesel with its peak at 1950 rpm shifts at about 1560 rpm at a fifth of the pedal (it was about 2500), as a road car's automatic does, and never under 0.8 of the peak torque's speed. Floored is as before.
- It shifts down where the gear below would come in 15 percent under its own point up, gear by gear, so the point down follows the point up whatever the gearbox's steps; at a light foot a diesel lugs down to about 1000 rpm before it shifts down.
- The converter locks up from 1.3 times idle at a light foot (from half the red zone floored, as before), so a gentle shift up lands locked.

## [0.14.1] 2026-10-11

- The gearbox's name is read where the game keeps it, at the truck's vehicle + 0x9a0, which 0.14.0's search found on four trucks. A name there is taken whatever it begins with, so a modded gearbox not named with "g_" is known by its name too, where it went by its gears' speeds (two modded trucks in the drive of 16:48). The search stays behind it, and the gears behind that.

## [0.14.0] 2026-10-11

- Each truck's own scales and each gearbox's own hold are kept in a file of their own, in the folder `Gauges` beside `Gauges.ini`: `Gauges\trucks\<kind>.ini` and `Gauges\gearboxes\<name>.ini`. A setup can be passed to another player and dropped into their folder as it is. A modded truck's kind holds a stroke (`<name>/<mod id>`), which a file's name cannot, so its file is `<name>@<mod id>.ini`. The first start of this version moves the `[Truck:<kind>]` and `[Gearbox:<name>]` sections out of `Gauges.ini` into their files, keeping what a file already holds, and says so in the log.

## [0.13.9] 2026-10-11

- How long each gear is held going up can be set for each gearbox: the tab's slider "Each gear going up" (Gearbox section, with the mod's automatic and the engine handed over), from 1 to 3 seconds. It is kept in Gauges.ini under `[Gearbox:<name>]` as `GearHold`, and every truck with that gearbox fitted takes it; without it the truck's class has it (1.5 seconds for a scout, 2.5 for the others). The mod reads the gearbox's name from the truck as the game's files give it ("g_" and the rest); where it cannot, the gearbox goes by its gears' speeds. The log says which gearbox is fitted and where its name was found.

## [0.13.8] 2026-10-11

- A scout holds each gear one and a half seconds going up, where trucks and heavy trucks keep two and a half. The scout of the drives, a light 4x4, sat in its gears slightly too long for its kind; the big trucks were right. The class is the one set for the truck on the tab (its scales). The game's files put that scout at 9007 kg and the Fleetstar at 6314, so the mass could not tell them apart.

## [0.13.7] 2026-10-11

- With the engine handed to the game, going up through the gears, from a standstill or after a shift up, each gear is held at least two and a half seconds of moving, on the limiter if it comes to that. An empty Fleetstar, one of the first trucks of the game, went 1 to 5 in two seconds and pulled like a late one. A gear a kickdown brings is not held: the kickdown keeps its pace. 0.13.6's half second between shifts up floored is gone again, and the floor the gearbox keeps under the game's gear lets a gear held on the limiter stay.

## [0.13.6] 2026-10-11

- With the engine handed to the game, floored, a shift up may follow another after half a second, not a whole one. An empty Fleetstar went through its close gears (tops 8, 13, 17, 21 and 25 rad/s) faster than a shift a second, so 10 of its 21 floored shifts up came on the limiter, at 0.98 to 1.15 of the gear's top (1 to 5 in two seconds at 14:12:05, each on the limiter). The limiter is now also judged by the wheels as they turn, not only by their smoothed speed, which lags a quick truck by a fifth of a short gear.

## [0.13.5] 2026-10-11

- With the engine handed to the game, the automatic sets off in a lower gear where the engine is weak for the truck: its torque over the truck's mass, both now read from the game's files (the mass at the truck's model + 0x784, checked on five trucks). A scout of 9007 kg sets off in second with an engine of 195000 and in first with one of 100000. The mass leaves out addons, cargo and trailers; a truck that cannot pull away in its start gear still takes the first by the stuck rule.
- A kickdown lands only in a gear short of its own floored point up. At `EngineShare` 50 it could land at 0.86 of a gear's top, where the next gear already pulls harder, and shift straight back up; at 100 a scout kicked down and was back up within one to two seconds (5 to 4 and back at 12:17:19 and 12:17:25).
- A truck losing speed with the pedal under 0.4 is no longer taken for one climbing. At 0.15, the engine giving one to two hundredths of its power, a scout slowing on the road took the floored points and shifted 5 to 4 at 1580 rpm (12:16:41).
- Before the wheels' grip on the ground is learned, the engine's torque goes to the game by no more than half the gear's top. Wheels spun up from a standstill read as the gear's top, which handed them three times the game's torque: a scout put in first at a standstill went to 3.00 and spun its wheels to 1.23 of first's top (12:14:23).
- The signature for the input is the one harvest 3 found: 0x40 into the object it belongs to, not 0x80 into control's. All nine places stand once and are found where they are on the Steam build.

## [0.13.4] 2026-10-11

- With the engine handed to the game, floored, the mod's automatic shifts up where the next gear pulls harder, if that comes before the red zone. The game's pull falls away toward a gear's top whatever the engine, and at `EngineShare` 50 a scout floored in third, fourth and fifth hung at 0.86 to 0.87 of the gear's top for up to five seconds (11:51:00 to 11:51:05 in fourth), short of the shift point at 0.96, where the next gear would have pulled 13 percent more. On a scout's road gearbox it now shifts at 0.86 to 0.87 of a gear's top at 50, and at 0.90 to 0.92 at 100.
- Floored, it shifts down from 0.3 of the way from idle to the red zone (0.4 before), so a shift up at a lower `EngineShare` lands clear above it.
- The signatures for other builds now find all nine places the mod reads on the Steam build. Control and the map are fields of two static objects that small functions in the game's code hand out by their start; the game, the input, the viewer and the clock are fields of the same two, and are found by the same signatures. The harvest (run again on the Steam build with `Probe=1`) looks only for such object starts before a place the code never names, up to eight of them. Whether other builds lay the objects out alike is still to be seen on one.

## [0.13.3] 2026-10-11

- The signatures for other builds are harvested again (`Probe=1` on the Steam build, into `Gauges.signatures.txt`). The first harvest gave 7 signatures for 3 of the 9 places the mod reads, and none for the driven truck's control, the game, the map, the viewer, the input and the clock: those lie within 0xB0 bytes of each other, fields of one static object that the game's code reaches by its start, never by their own places. The harvest now takes the nearest place before such a field that the code does reach, with how far before it stands. Its windows go up to 40 bytes on each side, and a displacement a window cuts at either end is left out: the first harvest kept the first bytes of four of them, which another build moves. The first harvest's 7 signatures are in the mod, and the new harvest checks them against the known places.
- The engine's figures from its file are read from the game's record of it and logged for each truck: its torque, responsiveness, MaxDeltaAngVel and fuel consumption. The probe's dump of the record found them, against a scout's file: Torque 195000 at + 0x60. They are a first step toward shift points and a start gear by each truck's own figures.

## [0.13.2] 2026-10-10

- With the engine handed to the game, the mod's automatic shifts as a truck automatic does. Floored, it takes each gear to the red zone (0.95 of the way from idle to it); at a light foot it shifts up early, from 0.3 of the way; it shifts down at 0.1 to 0.4 of the way, well under where a shift up lands, so no shift is taken back. In the drive of 00:27 the points by the game's pull put a floored shift up at 0.86 of the gear's top and landed it at 0.56, where the gear above wanted to shift down: a shift up was taken back within five seconds 34 times in ten minutes. One gear up at a time, after a second in each, unless the engine is on the limiter; a kickdown goes to the lowest gear that keeps the engine under 0.85 of the way.
- A game's gearbox is set up for the game's own engine, with a first gear for crawling: a scout's road gearbox has tops of 8, 13, 19, 27, 43 and 65 rad/s. With the engine handed over, the automatic sets off in the lowest gear with at least a seventh of the top gear's speed, and takes the gears under it only when the truck is stuck: floored, under three tenths of that gear's speed and not gaining, for a second. It is back in the start gear once the foot comes off at a standstill.
- `EngineShare` (on the tab, under "Engine to the game": "How much of it") sets how much of the mod's engine the game gets, from 0 to 100 percent. At 50, the default, the truck pulls half way between the game's own engine and the mod's: at 100 a scout pulled too hard to hold.
- With `Probe=1` the log shows the engine's record (its first 0x100 bytes) and the floats of a mass's size in the truck, once for each truck: to find where the game keeps the engine's torque and the truck's mass, for shift points and a start gear by each truck's own figures.

## [0.13.1] 2026-10-10

- With the engine handed to the game, a light foot no longer leaves the truck weak. The missing boost and the cold oil cost torque on every pedal, so at 40 percent pedal the game got two thirds of the torque it asked for; now they cost only where the pedal asks more than they leave, as a turbo diesel gives a light foot what it asks without boost.
- Wheels spinning in mud no longer get three times the torque. The lift went by the wheels' speed, and wheels spinning at their gear's top put it at its most; it goes by the truck's speed over the ground now, so spinning wheels keep the game's own fall toward the top, which holds them back.
- The signatures for other builds of the game go into a file of their own, `Gauges.signatures.txt`, with `Probe=1` on the Steam build. In the log they were lost when the game started again before the log was sent; the harvest runs again wherever that file is missing.

## [0.13.0] 2026-10-10

- The truck can pull with the mod's engine. `Powertrain=1` (the tab's Gearbox section, "Engine to the game") hands the game the mod's torque in place of its own: the class's torque curve against the engine speed on the needle, less with cold oil and without boost, and with the mod's automatic doubled by the converter at its stall. The game's own gives all its torque at half a gear's top speed and falls to nothing at the top whatever the engine speed; the mod lifts that fall by the game's torque factor, written on every pass at up to three times the game's own share, so a gear revved high pulls as a real engine's would. The game's share comes back when the setting goes off, the engine stops, a pull test is armed or another truck is driven. Off by default.
- With the engine handed to the game, the mod's automatic shifts by it: floored it revs out to near the gear's top before it shifts up (0.88 to 0.94 of it, where the game's own engine wants 0.63 to 0.79), and shifts down sooner.

## [0.12.12] 2026-10-10

- Floored from a standstill, the mod's automatic leaves first gear near its point. A scout's first gear is through in about a second, and the gearbox went by the wheels' speed smoothed over a quarter second, which put the shift a quarter of the gear late (the Tacoma at 0.88 of first's top and 0.89 of second's). A shift up now goes by the wheels as they turn, held to the truck's speed over the ground.
- Slowing hard up a climb, it shifts down gears at a time to the gear that pulls the most, and a shift down may follow another after half a second. The Tacoma went 6, 5, 4 a second at a time, by then at 0.29 of the gear's top.

## [0.12.11] 2026-10-10

- The mod no longer stands down on every build but the Steam one it is made on. On another build (the Epic or Microsoft Store version, or the game after an update) it looks for the places it reads by the game's code that uses them, logs what it found, and runs where it finds the driven truck. The signatures it looks for come from the Steam build: with `Probe=1` on that build the mod harvests them once into `Gauges.log`, and the next version carries them.
- The Microsoft Store version's saves are read for the map, from the game's save containers.
- The README says what the mod writes into the game's memory: the engine damage, the gears with `Gearbox=1`, and the torque factor during the pull test.

## [0.12.10] 2026-10-10

- A tap of the shift modifier does nothing while the mod's automatic drives. The game's own automatic still picks a gear at the tap, and the mod sets its own gear straight back; a gear the game goes to right after its pick is put back too, not taken for one picked by hand. In 0.12.9 the mod and the game traded 5th and 4th ten times in six seconds over one burst of taps.
- A heavy truck's close gears no longer run to the top of each one. The Scania's ten gears went by faster than the mod's one shift a second, so it shifted at nine tenths of a gear's top and the floor under the gears pushed it up and back (4, 3, 4 within a second). Shifts up now skip gears where the next one would shift up at once too, a shift up may follow another after half a second, and a gear past what the engine can take is left at once.
- Wheels spinning in mud or on rocks no longer shift up. A shift up goes by the truck's speed over the ground; up a rocky climb in the sooty, wheels spinning a third faster than the ground shifted 2 to 3 and back four times in four seconds.

## [0.12.9] 2026-10-10

- Floored, the mod's automatic shifts where the next gear pulls more in the game. The game gives the engine's torque by how far the wheels are toward the gear's top speed, not by the engine speed: all of it at about half the top, falling in a line to nothing at the top (the value at action + 0xB4, the same on the Defender and the sooty), and a lower gear multiplies it. So a gear revved near its top pulls a fraction of the next one. Floored it now shifts up at 0.63 to 0.79 of the gear's top, by how big the step to the next gear is (it was 0.8 of the way from idle to the red zone, about 0.83 of the top), and down where the gear below pulls more. At a light foot the points are as they were.
- The kickdown goes to the gear that pulls the most, gears at a time, and no longer holds that gear to near the red zone. Up a climb in the sooty it kicked down into first at nine tenths of first's top, where first pulls under half of what second does, and went 1 to 2 and back six times in two seconds.
- A tap of the shift modifier no longer leaves the truck in a gear too tall. Floored up a climb the game's own automatic picked 5th at 0.4 of its top five times in four minutes, where 4th pulls a fifth more; the mod now takes the highest gear its own shifts would keep (4th there). At a light foot the tall gear stays.
- Wheels count as spinning from 1.2 times the ground's speed (was 1.4); floored, they turned within 1.13 times nine times in ten in your drives.
- With `Probe=1` the gearbox line shows how fast the truck gains and where the wheels are against the gear's top, with the game's pull there; each shift's line shows that for both gears.

## [0.12.8] 2026-10-10

- Flooring it from a standstill no longer sends the mod's automatic through the gears. One wheel spinning by itself (all wheel drive off) read as a fast truck to the floor 0.12.7 put under the gears, which set the third gear and back at a standstill, and the game's throttle, cut to nothing for a moment at each shift, read as lifts and pushes of the pedal, which shifted down and kicked down by turns. The floor goes by the truck's speed over the ground now, and only once the wheels' grip is learned; a drop of the pedal shorter than 0.3 s is no lift, and a kickdown needs the pedal short of the floor for 0.3 s before the push.
- The kickdown goes as far down as the engine can take (to nine tenths of the way from idle to the red zone, where it stopped at three quarters and often did not come at all) and holds the gear while the pedal stays floored until the engine is near the red zone. Floored, the gears go down as the truck slows at 0.4 of the way from idle to the red zone, where it was 0.3, so a climb lugs the engine less.
- The game's clock is read where it was found in this version of the game, `[exe+2a8eb48]+150`, without the search; the day lasts an hour of driving.

## [0.12.7] 2026-10-10

- The mod's automatic never sets a gear the engine cannot take at the speed the wheels turn, whatever gear its model holds. At the end of both power tests in the third gear it set the second at about 6500 rpm: the model had shifted down by itself while the test held the game's gear, and set its own once the test let go. The model now holds the test's gear too.
- The tab's power test is a pull test now: it changes the game's torque factor (the share of its torque the game leaves a damaged engine) in place of the power factor, halved, raised by half and at three quarters, in the fourth gear or higher. The power factor's answer is in: a write counts at once and is kept, but it moves the gear's top speed, the way the game makes its low gears, so halved the truck braked hard and doubled the gear pulled less. It cannot carry the engine's torque.

## [0.12.6] 2026-10-10

- The outside temperature follows the game's day: coldest just before sunrise, warmest in the afternoon. A map's section can give the day's warmest and the night's coldest, `DayC` and `NightC` (Scandinavia's asphalt has frost at night and none by day: `DayC=3`, `NightC=-12`); a section with only `AmbientC` swings 10 C around it. The tab shows both and the hour, and its slider moves them together.
- The mod finds the game's clock by itself: with `Probe=1` it looks through the game's main objects for a value from 0 to 24 that rises at a day's pace while you drive, and takes the one that jumps when the night is skipped on the map (or time passes in a garage), or the only one left after three minutes. `Gauges.log` tells the search; the place is kept in `Gauges.ini` under `[Clock]` for this version of the game and read from then on. Until it is found the day's mean is in force, as before.

## [0.12.5] 2026-10-10

- The outside temperature by the map is a setting of its own, on by default: `AmbientByMap` (on the tab, "Outside by map"). While the map's temperature is in force, the tab's slider shows it and sets it in the map's or its region's section of `Gauges.ini`; before, the slider showed and set `AmbientC`, which is only in force where the map is not known, so the tab looked as if the temperature were set by hand. Off, `AmbientC` is in force on every map.

## [0.12.4] 2026-10-10

- A power test on the tab (Gearbox, "Test the power factor"): floored in the third gear or higher on flat ground, the mod holds the gear for seven seconds and changes the game's power factor a second at a time (as it is, halved by a write alone, halved with the gear set again, doubled with the gear set again), measures the pull in each, and puts the factor back. `Gauges.log` gets the numbers: whether the factor changes the pull at once, only once the gear is set again, or not at all, which decides how the engine's torque reaches the game next. Lifting the foot, the brake, another gear or the game's automatic stops it.

## [0.12.3] 2026-10-10

- The mod's automatic goes by the pedal where your foot holds it. It went by the pedal after the powertrain's curve, under which a fifth of the pedal reads as a twentieth, so a light foot sat on the line between coasting and driving and it shifted 4 to 5 and back every few seconds.
- A tap of the shift modifier with the foot off no longer drops the truck into first at speed. The game's own automatic picks first then whatever the speed (at 23 km/h, about 8400 rpm), and the mod took that gear on; it now takes the lowest gear the engine can take at the speed the wheels turn.
- Floored, a shift up comes 0.15 s after it is called for, where it was 0.4 s: a short first gear went 300 to 800 rpm past its shift point in that time.

## [0.12.2] 2026-10-10

- The mod's automatic no longer shifts down into a gear that races the engine. In 0.12.1 it learned how fast the wheels turn for the truck's speed from any moment with the foot on, so a bump or the truck sliding back down a slope could teach it the truck went at half its speed: it then shifted down as far as first at up to 6800 rpm, and held first on the limiter for half a minute. It learns now only while the wheels turn steadily and alike, the truck going forwards with no brake or handbrake, and follows a lower reading over two seconds. It goes by the wheels again and weighs them against the ground only to tell wheelspin and locked wheels, and no shift down takes the engine past nine tenths of the way to the red zone at the speed the wheels turn.
- A climb is told by the truck losing speed for half a second with the foot on, where a moment of it was enough, so the shift points no longer jump to floored's on a bump.
- A tap of the shift modifier the game goes back on within two seconds (it picked 3rd from 1st and fell back to 1st) is taken on as well; before, it gave the gears back as if picked by hand.

## [0.12.1] 2026-10-10

- The mod's automatic shifts sooner. Floored it shifts up at about four fifths of the way from idle to the red zone, where it waited until nearly the red zone (on the Defender 4040 rpm, where it was about 4600 from 4th to 5th), and at a light foot lower still. The shift points are measured from idle, so a slow revving diesel and a fast revving petrol engine both shift in their band.
- With the foot off it no longer shifts up (only past the red zone, rolling downhill), and it shifts down early, at a quarter of the way from idle to the red zone, gears at a time when the truck slows hard, never so far that the engine races. Before, a lift shifted up and the shifts down came late, the last one as the truck stopped.
- Climbing (the truck slowing with the foot on) it shifts down where floored would and holds those points four seconds after, so it does not shift up and straight back down. Before, it barely shifted on a climb: wheels slipping held every shift.
- It goes by the truck's speed over the ground, learned against the wheels while they grip, so wheelspin in mud does not shift it up and wheels locked under the brake do not throw it into first; the shifts no longer wait while the wheels slip.
- A push of the pedal to the floor kicks down at once, once; held floored it steps down a gear at a time as the truck slows.
- Tapping the shift modifier while the mod drives lets the game's own automatic pick a gear, which the mod takes on and holds for two seconds. Before, the mod set its own gear straight back, and a tap for 5th went back to 4th.
- Another mod raising the power factor in A (a manual transmission mod sets 1.05) no longer reads as a low gear picked by hand, which gave the gears back a few seconds after they were taken.

## [0.12.0] 2026-10-10

- The mod's automatic drives the game's gears. `Gearbox=1` (the tab's new Gearbox section) hands the gears to the mod's automatic while the game's shifter is in A: a torque converter (the engine at its stall speed with the pedal floored against a standing truck, at most 2400 rpm, following the road speed with a little slip on the move, locked up from the second gear), shift points that rise with the pedal, a kickdown when floored, the gear held when the foot lifts and rolling downhill, no shift in wheelspin, and the first gear by the time the truck stops. The tachometer and everything that goes by it (the turbo, the heat) take the converter's engine speed, and the cluster shows A. The gears are set through the game's own shift function with the game's automatic put down; picking a gear by hand gives the gearbox back, A takes it again. The truck still pulls with the game's own torque. `Gearbox=0`, the default, leaves the game's automatic as it was.
- The converter's stall speed is at most 2400 rpm: a fast revving engine stops well short of its red zone floored against a standing truck.

## [0.11.3] 2026-10-09

- The outside temperature goes by the map. The mod reads which map you are on from the game's save (the map it loaded last), and the `[Map:<name>]` sections now carry the game's own names: one for each of its 21 regions (`[Map:Michigan]`, `[Map:Zurdania]`), and a map can have one apart from its region's (`[Map:Black River]`). A map of a mod goes by its id. `Gauges.log` says the map, its region and the section in force when it changes, and the Gauges tab names them under the outside temperature.
- The mod is a ReShade add-on now, `Gauges.addon64`: ReShade loads it, so no ASI loader is needed. Delete `Gauges.asi` when you install it; with both in the folder only one runs, and the log says which. `Gauges.asi` is the same file for a game set up with an ASI loader, and the log says which of the two loaded it.
- Once a truck is driven, the log says where the game's gearbox functions are in its code, or that another mod holds them. Nothing is written: this is the ground for the gearbox, the clutch and the engine that come with 0.12.0.
- The clutch's model in the log (`Probe=1`, while the manual transmission mod drives): a stalled engine stays dead until the clutch is pressed or the gearbox is in neutral, where it started over every two seconds before and stalled again at once while the truck stood in gear. Without a clutch pedal on an axis (a clutch button, or none) the clutch works by itself: it lets go at idle, takes hold as the engine speeds up, and never stalls the engine.
- The look for the game's gearbox functions tells a function behind another mod's hook also where the hook moved more than one instruction, and no longer looks for ShiftToNeutral where its neighbours are not both found.
- The automatic's model (`Probe=1`) runs beside the game's automatic also while the manual transmission mod is loaded; it stands down only while that mod drives a manual gear with the game's automatic off. Before, it stood down whenever the mod was loaded.
- The automatic's model is in the first gear as the truck all but stands, also after a hard stop that left no time to shift down gear by gear.
- The placeholder sections of 0.11.0 to 0.11.2 are renamed to the game's names with the temperatures they hold, and the four regions they lacked (Quebec, Washington, Chiapas, Zurdania) are added.

## [0.11.2] 2026-10-08

- The brake air dial shows the air in the brakes' tanks. The engine's compressor fills them, faster the higher it revs (about three minutes from empty at idle, one at the red zone); its governor stops it at 8.2 bar and starts it again at 6.9. Each press of the brake takes air by how far it goes down, letting the handbrake go takes the air that holds the spring brakes off, and the system leaks a little. Eight full presses with the engine off take a full tank into the red zone. Scouts brake by hydraulics and have no air dial. `ShowAir` is still off at first. With `Probe=1` the log has a line for it.
- With `Probe=1` the log has a line for the powertrain that comes with 0.12.0: the pedal as it will be shaped (a progressive curve, and a ramp so a keyboard's pedal comes in over 0.6 s), the torque from the truck class's torque curve at the engine speed, less with cold oil and more with boost, and the power the engine would deliver. Nothing of it reaches the game yet.
- With `Probe=1` the log has a line for an automatic gearbox with a torque converter as well, and a line at each of its shifts beside the game's own gear: the gear it would be in, the converter's slip and lock-up, and how much it multiplies the torque. It shifts up later the further the pedal is down, kicks down when floored, holds its gear rolling downhill, and does not shift up at a lift of the foot or in wheelspin. Nothing of it reaches the game yet.
- A manual transmission mod that publishes its state (a build with the state export) is followed: the log says when it comes and goes, and while it drives the truck the automatic's model stands down. With `Probe=1` the log has a line for it (its gear by its own names, the clutch held and how far, its stalls) and a line at each stall it causes for a shift without the clutch. Without such a build nothing changes.
- While that mod drives, the log with `Probe=1` also has a line for the engine behind its clutch pedal: free with the pedal pressed, slipping in the bite, turning with the gearbox let up, and a stall when the clutch comes up too fast for the engine's speed, said in the log as it would happen. Nothing of it reaches the game yet.
- `WarmUpMinutes` is measured on driving as it really is: the mean load and speed of a 25 minute drive (half of what the engine burns floored at the red zone, at 13 km/h), where it was a heavier foot before. On that drive the coolant was at 85 C after 4 minutes of driving with the setting at 3, where it took nearly 5 with 0.11.1.

## [0.11.1] 2026-10-08

- The heat comes from the fuel the engine burns: the pedal and the engine speed together, as in a real engine. Floored at the red zone makes the most heat, floored at low revs little, and coasting or rolling downhill with the foot off next to none. In 0.11.0 a truck coasting at high revs heated as if floored.
- The cold engine damage goes by the fuel burned too: only a cold engine burning more than half of what it burns floored at the red zone suffers. Pulling away and driving about at moderate revs cost nothing, also on a keyboard.
- The oil takes its own heat: the friction, which grows with the engine speed, and the pistons' cooling, which grows with the load. It runs about 7 degrees over the coolant at cruise and 14 floored at the red zone, and it is warm a minute after the coolant at the least, whatever `OilWarmUpMinutes` says.
- New defaults: `OilWarmUpMinutes` 5 (was 8) and `OilWarnC` 120 C (was 125), the warning one maker puts on its truck diesels. An ini that holds the old values keeps them.
- `models_test.exe <drive> <idle> <red zone>` replays a recorded drive on a truck's own scales.

## [0.11.0] 2026-10-08

- The coolant temperature follows the engine speed, not the pedal. An engine revved high heats whatever the foot does, one pulling at low revs heats little, and in neutral it makes a quarter of that heat.
- The cold engine damage goes by the engine speed too: below 60 C with the needle more than half way from idle to the red zone. A keyboard's pedal, which is either up or floored, costs no more than a gentle foot that revs as high. The damage from overheating comes from the temperature, as before.
- The oil temperature. The oil follows the coolant slowly: driven as usual it is at 85 C after about eight minutes, and a stopped engine's oil keeps its heat longer. It runs about 2 degrees over the coolant at idle and 14 at full load. Each kind of truck keeps its own.
- `OilGauge` shows it as a second, amber needle in the coolant's dial (the default), on a dial of its own left of the coolant's, or not at all.
- An oil lamp, the ninth. It blinks while the oil is too hot, from `OilWarnC` (125 C at first), and a warning of two short beeps sounds every four seconds until the oil is two degrees under that or the engine stops. `SoundVolume` sets how loud; `sounds\oil_beep_1.wav` plays in its place. The Gauges tab has a button to hear it.
- New settings `OilWarmUpMinutes` (8) and `OilWarnC` (125), on the tab under Oil temperature.
- `BoostProfile`: how the turbo builds its boost. Standard, small and quick (early, little lag), big and slow (late, long to come at low revs), or two stages (a small turbo early and a big one after it). It is on the tab under Scales, and each kind of truck can have its own, as its seventh scale.
- Sections `[Map:<name>]` with an outside temperature for each of the game's regions are written into the ini. Their names are placeholders: the mod does not read which map a truck is on yet, so `AmbientC` is in force everywhere until it does.
- With `Probe=1` the coolant's line has the oil, and the log says when the oil warning comes and goes.

## [0.10.0] 2026-10-07

- The turbo is heard. Take your foot off the pedal under boost and the turbo flutters: the air beats back through the compressor wheel, slower and fainter until it is gone.
- The more boost the lift had, the longer and louder the flutter: there is one for a lift at full boost, one at half and one at little, each in three recordings, of which one is played and never the same twice running. Each also plays a little faster or slower than the last.
- Put the pedal down again and a flutter that still sounds stops.
- `SoundVolume` (0 to 100, 50 at first) sets how loud, and 0 switches the sounds off. The Gauges tab has the slider and three buttons to hear the sounds.
- `SoundsOnly` plays the sounds and draws no cluster.
- WAV files `sounds\turbo_high_1.wav` to `_4.wav` next to `Gauges.asi` (and `turbo_mid_`, `turbo_low_`) are played in place of the mod's own recordings of that strength.
- The sounds go through XAudio2, the game's own copy or Windows'. The mod holds the audio device only while a sound plays.

## [0.9.0] 2026-10-07

- The cluster is drawn anew. Solid and tinted dials sit in a bevelled metal ring with a soft shadow, and the solid face is a little lighter towards its middle. In the open style the scale arcs have a soft and a sharp shadow.
- The needles are blades with a point, one half bright and one shaded, over a hub with a lit ring. Major ticks taper towards the middle, and the red and blue zones have a soft inner edge.
- Every lamp has a new symbol, drawn from curves: parking brake, headlights, engine, frost, the truck of the tilt lamp, and on the dials the fuel pump, the thermometer, the turbo and brake air. All wheel drive, the differential lock and the low range are symbols now, in place of the texts AWD, DIFF and LOW. A lit lamp glows.
- Still drawn at the screen's own resolution at any size, with no image files.
- With `Probe=1` the log says once a minute what the cluster takes to draw.

## [0.8.0] 2026-10-07

- The boost dial shows a boost pressure, worked out from the pedal and the engine speed. The turbo makes nothing near idle and all it can from about two thirds of the way to the red zone with the pedal floored. In neutral it makes a quarter of that at most.
- The needle lags behind the pedal, longer at low engine speed: floored at the red zone the boost is there in under a second, pulling away from a standstill it takes a second or more. It is gone within half a second after the foot comes off.
- New setting `BoostMax`: the boost in bar the turbo reaches, from 0.3 to 2. At 0 it is the truck class's own: trucks 1.8 bar, scouts 1, heavy trucks 2. It is on the tab under Scales, and each kind of truck can have its own, kept with its other scales in its section of `Gauges.ini`.
- With `Probe=1` the log has a line for the boost, and one for each quick lift of the pedal under boost: a blow-off or a flutter, with the boost it came at.

## [0.7.0] 2026-10-07

- Each kind of truck can have its own scales. Change the truck class, idle, the start of the red zone, the tachometer's top or the speedometer's top on the Gauges tab while you drive a truck, and all five are kept for that kind of truck. They are back whenever you drive one: after a recovery, out of the garage, after a change of trucks and after a new start of the game.
- A truck that was never set up shows the scales of `[Gauges]`, as before, and an ini from an earlier version works as it is. On the tab those plain scales are the ones shown while no truck is driven: in the game's main menu and in the garage.
- The Scales section of the tab names the truck it sets, says whether the scales are its own, and has a button that gives the truck the plain scales back.
- A truck's scales are saved in `Gauges.ini` in a section named after the truck as the game's files name it, such as `[Truck:chevrolet_ck1500]`. `Gauges.log` names the truck each time you take another one.
- The coolant temperature and the range go by the kind of truck as well. They went by the tank, the wheels and the gearbox before, so two different trucks with the same three shared a temperature. Two trucks of one kind still do.
- The Defaults button takes the scales of the truck you drive back to the defaults too. Other kinds of truck keep theirs.

## [0.6.1] 2026-10-07

- Water cools the engine. From about half a metre of water at the truck's front the coolant loses heat to it, and with the engine standing in the water (1.2 m and more) a hot engine is back at 100 C within half a minute, even with the pedal floored. A stopped engine cools five times as fast in deep water. Nothing cools below the outside temperature, and an idling engine in a ford stays warm enough to keep the cold lamp dark.
- `Damage` is on by default: an ini without the key now has the engine damage from the temperature. `Damage=0` switches it off.
- The dials wait while the game does not simulate the truck, so nothing heats, cools or wears on values the game has left standing.
- The write of engine damage checks more before it writes, and for an engine whose record it cannot find it writes the damage alone and leaves the loss of torque to the game.
- The tab, the ini help and the readme say that a keyboard's pedal counts as floored, so a cold start always costs there unless `ColdDamagePercentPerMinute` is 0, and that `Damage` has not been tried in co-op.

## [0.6.0] 2026-10-07

- The temperature can cost the engine. With `Damage` on, an engine in the red zone takes the game's own engine damage: 10 percent of what it can take every minute at 125 C, nothing at 118 C, less between the two and more above. Floored at a standstill with the wheels spinning, an engine is used up about five minutes after it enters the red zone.
- A cold engine suffers under a heavy pedal. Below 60 C, with the pedal more than half down, it loses up to 5 percent a minute (floored at 0 C), and less the warmer it is. A cold start driven with the pedal floored costs 2 to 3 percent of the engine. Half pedal or less costs nothing.
- The damage is the game's own: a garage repairs it, and past the engine's threshold the game takes torque away and burns more fuel. The mod stops one point short of what the engine takes, so it never stops the engine itself.
- The engine damage lamp blinks while the mod adds damage.
- `Damage` is off by default, and the temperature is then only shown, as before. The Gauges tab has a new Engine damage section with the switch and the two rates, `HotDamagePercentPerMinute` and `ColdDamagePercentPerMinute`.
- The radiator gets all its air from 30 km/h, where it took 50 km/h before. A truck in the low range at its own top speed now runs hot and settles just under the red zone, and the needle stays out of the red zone from about 7 km/h up. Slower crawls and a truck that spins its wheels without moving still overheat. The new setting `AirFullKmh` moves that speed: lower keeps a slow, hard pulling engine cooler.
- With `Probe=1` the log has a sixth line with the engine's damage, the engine's record and what the mod adds.

## [0.5.1] 2026-10-07

- A truck you take out of the garage again is as warm as you left it, less what it cooled while you drove another truck. In 0.5.0 it came back cold, as the game makes a new truck of it. The mod now knows a truck by its tank, its wheels and its gearbox.

## [0.5.0] 2026-10-06

- The coolant temperature dial works. The mod works the temperature out from how hard the engine works and how fast the truck drives. A cold engine starts at the outside temperature and is at 85 C after about three minutes of driving. The thermostat then holds it: about 86 C at idle, near 100 C when the truck works hard at speed.
- The needle goes into the red zone from 118 C only when the pedal stays floored at a crawl. At a standstill with the engine revving out that takes a minute and a half from 90 C. From about 15 km/h up the needle stays out of the red zone, and off the pedal the engine is back at 100 C within a minute. The temperature is only shown and does nothing to the truck.
- The cold lamp is lit while the coolant is below 60 C.
- A stopped engine cools from 90 C to 40 C in a quarter of an hour. Each truck keeps its own temperature while the game runs, and a truck left standing cools while you drive another.
- Four new settings on the Gauges tab and in `Gauges.ini`: `WarmUpMinutes`, `OverheatMinutes` and `CoolDownMinutes` set the three times, and `AmbientC` is the outside temperature.
- With `Probe=1` the log has a fifth line with the coolant temperature, the engine's load and the air through the radiator.

## [0.4.0] 2026-10-06

- The tilt lamp works. It is green while the truck stands level, amber when the truck leans 15 degrees to a side and red from 25 degrees, or when the truck points 35 degrees up or down a slope. A truck on its side or its roof is in the red.
- The small truck in the lamp leans the way your truck does, by the same angle.
- The lean is smoothed over a third of a second, so a bump does not light the lamp, and the lamp goes back two degrees below the angle it came on at, so it does not flicker on the edge.
- Three new settings on the Gauges tab and in `Gauges.ini` set the angles: `RollWarnDeg`, `RollDangerDeg` and `PitchDangerDeg`.
- With `Probe=1` the log has a fourth line with the slope, the lean and the lamp.

## [0.3.0] 2026-10-06

- The tachometer shows an engine speed, worked out from the driven wheels, the gear and the pedal. It climbs through each gear to the red zone and falls at the shift, it rises under the pedal when the truck is stuck, and the pedal revs the engine freely in neutral. Wheels that spin faster than the gear allows take the needle into the red zone.
- The needle rises from 0 while the engine starts, dips for a moment when a gear goes in, and runs down when the engine stalls or is switched off.
- The range readout shows how far the fuel in the tank goes, from the litres per kilometre the truck burned on the last three kilometres it drove. It shows `---` until the truck has driven half a kilometre, and a tenth of a kilometre below ten.
- Fuel burned while standing comes off the range with the litres it takes and does not change the litres per kilometre. A truck put back into the world keeps its figure.
- The speedometer and tachometer needles glide between the 20 readings a second.
- With `Probe=1` the log has a third line with the engine speed, the range and the fuel burn.

## [0.2.0] 2026-10-06

- The cluster shows the driven truck's own values: speed, the gear, fuel, and the lamps for the handbrake, the headlights, all wheel drive, the differential lock, the low range and engine damage. The tachometer rests at idle while the engine runs.
- The gear is written as its number with an A in automatic, H for the high gear, L, L+ and L- for the low range, R and N.
- The cluster is on screen only while a truck is driven in the world. It is hidden on loading screens, in the pause menu, on the map and in the garage.
- The dials and lamps that have no source yet rest: coolant temperature, boost, brake air, the range, the cold lamp and the tilt lamp.
- `Demo` is off by default. It is a switch for work on the mod and draws demo values whenever no truck is driven.
- `Probe=1` writes the values read from the game into `Gauges.log`, every two seconds and whenever one changes.

## [0.1.0] 2026-10-05

- `Gauges.asi` draws a gauge cluster over the game through ReShade's add-on overlay, in the game's own font: speedometer, tachometer with the gear, fuel with the range, coolant temperature, boost pressure, brake air pressure and eight lamps. The cluster shows demo values.
- Two layouts (a strip along the bottom centre, a block in the bottom right corner) and three styles (solid dial faces, tinted see-through faces, open scales without faces).
- A Gauges tab in ReShade's overlay edits every setting while the game runs and saves it to `Gauges.ini`: layout, style, position, size, opacity, units, which dials and lamps are shown, the scales by truck class, and a key and pad buttons that hide and show the cluster.
- The mod runs next to the SnowRunner Tire Inflation System, loaded by the same `version.dll`.
- On any build of the game but the Steam build of 22 July 2026 the mod stands down and says so in `Gauges.log`.
