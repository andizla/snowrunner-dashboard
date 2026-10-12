# SnowRunner Gauges: module board

The mod is built one module at a time, each in its own Claude session. The overseer session (the gauge design
session that wrote `docs\plan\GAUGES_PLAN.md`) keeps this board, writes each module's brief, checks the results
and hands out the next one. The user creates the sessions. Agents: none, or at most one or two at a time.

A module session: reads this file and `HANDOFF.md`, builds its module, tests it as its brief says, then writes its
results into `HANDOFF.md` (the running state of the mod) and sets its row below to done with a one-line result.
Questions for the overseer go into `HANDOFF.md` under "Questions"; the overseer reads it.

## Board

Other builds (the user, 2026-10-11): the user hands the next version to a player on the Epic build, whose log will
say what the signatures find there. The user's lead: "next gen's notes" may say how the other builds lay out the
game's memory (a lot to go through; with the user, another day).
The tester's two logs (2026-10-12, 0.15.0, Gauges.log of 21:29 and Gauges.1.log of 10:34): exe stamp 6a607c05, the
Steam build's, the places known without the signatures, the trucks found and driven (the tester on the Epic build, as
the user arranged, if so: the Epic build then runs the same exe; `LogGameFolder` will say); the outside temperature at
AmbientC throughout: "map: no save of the game was found". The Epic store saves as CompleteSave*.dat in
Documents\My Games\SnowRunner\base\storage\<account> (community guides; Steam's are .cfg), and `SaveNewest` looked
for .cfg alone. 0.15.1: `kSavePatterns` both, `SaveNewestIn` (tested on a folder of each), the profile's Documents
beside the shell's, the folders looked in logged when none is found (`PathForLog`: %USERPROFILE%), the game's folder
and its likely store at the start (`LogGameFolder`). Owed: the tester's next log, to see the save found and read (the
.dat taken for the same text as the .cfg, as the guides say the two are renamed into each other).
Pinned (the user, 2026-10-12): the engine sound by our rpm, route 1 of the note of 2026-10-11 (find the value the game's
engine sound goes by, a probe reading the vehicle, the action, the truck control and its three sound objects at
+ 0x70 to + 0x80, keeping what moves like revs in neutral; nothing written); later.

| # | Module | Effort | Status | Session | Result |
|---|---|---|---|---|---|
| 1 | Foundation: repo, build, loader, overlay, settings | high | done (the user confirmed the cluster and the tire panel in the game, 2026-10-06) | Gauges M1 (created 2026-10-05) | Gauges.asi 0.1.0 (0A9C3341) built clean, offline tests OK, installed; in the game both mods load, the font bakes and the cluster is drawn with demo values (logs of two starts by the user). Not yet shown: an overlay picture of the cluster, a tab edit saved in the game, the tire panel opening beside it (HANDOFF, "The game run") |
| 2 | Live data: truck reader, probe log, verification run | max | done (2026-10-06; checked in the game by two drives of the user; the H-shifter check of the gear letters is deferred: the wheel is a sim rig in another room, so it waits for a day the rig is connected, in whichever module is current then) | Module 2: Live data (started in the design folder, worked here) | Gauges.asi 0.2.0 (AC3AE066, commits 81a7c0e and eeaa48c) installed: the cluster shows the truck's speed, gear letters, fuel and six lamps and is on screen only while driving (off while loading, in the pause menu, on the map and in the garage, by the logs of the drives). The plan's first group confirmed against the HUD and the gearbox XML; start timer, stall counter and the automatic's power factor corrected in the plan. Chores done: licence, notices, changelog, preview build, wording, a saved tab edit. Open: the gear letters with a wheel's shifter, and the run protocol notice came late (HANDOFF, "Still to see" and "For the overseer") |
| 3 | Tachometer and fuel range models | high | done by the log of the user's drive of 2026-10-07 (A1 to A4, H, L, N, both reverses, start, stall, the range from 507 m, the burn rule in the game) and by the user's word of 2026-10-07: the needle is believable, L+ and L- work, the range held on longer stretches. The user finds the rpm high for a diesel and leaves that for another mod, if at all. Module 2's open item is closed too: the user confirms the cluster stays off where it should (loading screen, pause menu, map). Still open: the wheel's shifter, deferred (HANDOFF, "The user's drive of 2026-10-07") | Module 3: Tachometer and fuel range (created 2026-10-06) | Gauges.asi 0.3.0 (E30EC128, commit ace0bf8) installed: the engine speed from the driven wheels, the gear and the pedal (`src\models.h`), the range from litres per km over the last 3 km, shown from 500 m. Offline tests OK (28 new checks) and both of module 2's drives replayed through the models: no jump, range 5 to 8 km on the Defender. The game's burn rule confirmed in l/min from the logs. Not done: the drive through every gear (this session started no game); Probe=1 is left on for it. Three questions in HANDOFF |
| 4 | Tilt lamp | medium | done (2026-10-07): the drive of 09:37 shows the lean to both sides, amber and red at their angles and red on a steep descent, no blinking; the side was turned after the user saw it mirrored (b5ea083), and the user confirmed the turned build by eye ("Yes, tilt is correct") | Module 4: Tilt lamp preparation (created 2026-10-06) | Gauges.asi 0.4.0 (D6796A33, commit 05ea2c1) installed: the tilt lamp green, amber from 15 degrees to a side, red from 25 or from 35 up or down a slope, its truck leaning with the real roll; smoothed over 0.3 s with a 2 degree band; three settings on the tab and in the ini; a fourth probe line. Offline tests OK (models 39, truck 29, settings 16, preview). Pitch sign confirmed from the recorded drives; the roll's side is taken from the axis order and waits for the drive (HANDOFF, "The drive still owed") |
| 5 | Coolant temperature (display) and cold lamp | high | done (2026-10-07: the user drove 0.5.0 and found it good; 0.5.1 "a truck's memory by its build" installed AE24F274 and confirmed by the user's A, B, A swap; water cooling researched, not built, a proposal: the ford scan goes on module 6's drive) | Module 5: Coolant temperature display (created 2026-10-06) | Gauges.asi 0.5.0 (044BFEEC, commit 5b85b37) installed: the temperature dial and the cold lamp work from a model in `src\models.h` (the plan's heat, thermostat, airflow and fan; per truck, a recovery keeps the temperature); `WarmUpMinutes` 3, `OverheatMinutes` 1.5, `CoolDownMinutes` 15 and `AmbientC` 15 on the tab and in the ini; a fifth probe line. Offline tests OK (models 57, settings 19, truck 33, preview), the change read by one C++ reviewer, and both of module 2's drives replayed: 85 C after 2.7 and 3.0 minutes of driving, hottest 105 C, never in the red zone; into the red zone only floored below 12 km/h (1.5 to 5.6 minutes from 90 C). In the game: the user's drive of 00:08 to 00:27 by eye, and a setting saved from the tab's new section (the log). Four questions in HANDOFF; the first is open and matters before module 6: the low range floored at its own speed overheats after three and a half minutes |
| 6 | Engine damage write: verification, overheating, cold abuse | max | done (closed by the overseer 2026-10-07 10:40: the user's drive log verified the write, the repair and the save; 0.6.1 adds the water term from the scan, the game's own clock for the models, Damage on by default). Open for the user's next play, not blocking: did the function menu's engine number follow "Add 1", and one hit on something after an "Add 10" (the user, 2026-10-07: "Will test both next time") | Module 6 Engine damage write (created 2026-10-07) | Gauges.asi 0.6.0 (ECF11786, commits cec366d and c2a8887) installed: with `Damage` on (off by default) an engine in the red zone and a cold engine under a heavy pedal take the game's own engine damage (`src\models.h`), written one short of the engine's capacity at most, with the effect and the torque factor by the game's rule from the engine's record, which is found by reading the game's own lookup (`src\truck.h`); the damage lamp blinks while damage is added; `AirFullKmh` 30 as a setting (the low range at its own speed settles at 115 C and costs nothing); the switch and two rates on the tab and in the ini, two test buttons behind the probe switch, a sixth probe line. Offline tests OK (settings 21, truck 46, models 68, preview); the change read by one C++ reviewer (no way to a crash or a stray write found; its eight points taken in the second commit); the recorded drives replayed: never in the red zone, 3 to 4 points of 150 per cold start. Two unattended starts of the game (reads only): the build loads, and the record is found in the real game ("mod_suv_engine_2 takes 150", the block's own 150). The user's drive (the log in `out\user_drive_0937`): thirteen test writes went in and read back as written, up to the cap of 149 of 150 with the effect and torque by the rule; they stood until a repair, which cleared them; the game's own damage added up on the same block, and the save kept it. After the drive, 0.6.1 (EBDF6A8F, commit 5296ceb) installed: `Damage` on by default; water cools the engine (the depth at the wheels, wheel + 0x17C, confirmed by an unattended scan of the truck parked in water: one water level to 3 mm over four wheels); the models wait for the game's own clock (vehicle + 0x114). Offline tests OK (settings 21, truck 48, models 73, preview). Not seen in the game: a game damage event on a written number, and the function menu after a write (HANDOFF has the one line for the user) |
| 7 | Per-truck setups: the truck's identity, scales saved and restored per truck | high | built, committed and installed (2026-10-07 12:15); NOT yet seen with a truck in the game: the user's drive is owed and is the first game check (the checklist in HANDOFF) | the module 5 session, reused on the user's word (remote, no new session possible) | Gauges.asi 0.7.0 (98251685, commit c268bc6) installed: each kind of truck keeps its own five scales (`TachClass`, `IdleRpm`, `RedlineRpm`, `MaxRpm`, `SpeedMax`) in a `[Truck:<kind>]` section of `Gauges.ini`, written when one of them is changed on the tab while such a truck is driven; every other truck has the plain ones of `[Gauges]`, and an ini without sections behaves as before. A truck goes by its kind's name, which the game holds at model + 0x588 ("mod_suv/1000001", the save's `type`; found by two scans of the running game), and by its build where no name is read. The game's id for the one truck (vehicle + 0x48) was found too and is only logged: the save keeps none for trucks in storage, so it would not last through a swap. The tab names the truck and has the button "Back to the plain scales"; the coolant and the range go by the kind. Offline tests OK (settings 32, truck 58, models 73, preview with three tab pictures); read by one C++ reviewer, nothing serious, its points taken. In the game: six unattended boots after the install never left the main menu, with 0.6.1 back in the folder as well, so they say nothing about the build (HSR's script; HSR told). |
| 8 | Boost model and gauge | high | built, committed (bcee527) and installed (2026-10-07 14:44, F1AB83EE); the user's drive owed | the module 3 session, reused on the user's word (remote) | 0.8.0 (F1AB83EE): the boost dial live from the pedal and the tach needle's share (`BoostStep` in `src\models.h`), `BoostMax` by class (1.8 / 1.0 / 2.0 bar) and as the sixth per-truck key, blow-offs and flutters told on quick lifts and logged with the probe on, a probe line. Offline tests OK (8 new checks), the two recorded drives replayed: 1.2 to 1.4 s from a floored standstill to nine tenths of the boost. Owed: the user's drive (checklist and three questions in HANDOFF) |
| 9 | Cluster graphics: dials, needles, lamps and icons at real-cluster quality | high | done but for the user's eye in the game: approved from the previews ("install them"), 0.9.0 installed 2026-10-07 19:22 (EE4F2EE6, commits 1b255bb and e5ecdd0) | the module 3 session (also module 8) | The drawing header rewritten with curves, a lit bezel, shaded needles and every lamp symbol redrawn; close-up previews at 4x; the draw time logged with the probe. 0.21 ms a frame at 2160p in the open style, 0.35 ms in the solid one (the preview's timing). Owed: the user's look at it in the game and the probe's draw time there (HANDOFF) |
| 10 | Sounds: XAudio2, flutter and blow-off clips, credits | high | done for now (the user, 2026-10-08, after playing it in the game: the playback is perfect and the recordings are good; the module goes on later with more sounds). Built with recordings and installed 2026-10-07 22:17 (BCA05B9C, commits 030f1c7 and 92e8d18): the user found the made sounds unlike a turbo and supplied a clip (permission from its author, by the user's word), now nine flutter recordings in three strengths. Still owed before any release: the clip author's name and link for SOUND_CREDITS.md | the module 4 session, reused on the user's word (remote) | 0.10.0 (E1C05E2A): a blow-off after a full lift under boost and a flutter after a smaller one through XAudio2 (`src\sound.h`, after the tire mod's), louder with more boost, pitch within 6 percent, at most one per 0.8 s; SoundVolume and SoundsOnly on the tab and in the ini, a button to hear each. Both sounds are made by code: no clip was downloaded (one poor CC0 hit on freesound, and its files need a signed-in account); a WAV in `sounds\` or `assets\` takes a sound's place. Offline tests OK, XAudio2 played both here (HANDOFF, the top section) |
| 11 | Engine models: turbo spool profiles, water and oil temperatures, heat from the engine speed, the outside temperature by map | high | done but for part C's map read (it needs a scan of the running game on the user's PC; the prompt for that session is in the chat of 2026-10-08), built, all four parts, as 0.11.0 (2026-10-08); built and tested with MSVC on the user's PC, installed and seen in the game (the user's screenshot of 2026-10-08: the Defender on its own scout scales, the oil on a dial of its own at about 100 C over a coolant of about 95 C at idle, the oil lamp dark in its own row); the drive's checks are owed. Part C is the plumbing only: the map's name is not read from the game yet (a scan of the running game on the user's PC), and the map names and temperatures are placeholders until the user gives the game's names | this cloud session ("Next steps"), on the user's word | A (commit bef186c): the heat and both engine damages by the engine speed. B, on the user's picks of 2026-10-08 ("second needle for now, perhaps with the option for a separate one to the left of water", "an oil temp lamp that starts to blink when the beep goes off"): an oil temperature behind the coolant (its lag solved from `OilWarmUpMinutes` 8, 14 C over the coolant at full load), `OilGauge` 1 an amber needle in the temperature dial (default) or 2 a dial of its own left of it, a ninth lamp that blinks from `OilWarnC` 125 C, and two made beeps every 4 s (`sounds\oil_beep_1.wav` in their place, a tab button). C: `[Map:<name>]` sections with `AmbientC`, written once into an ini without any, 17 placeholder regions; the outside temperature in force is the map's while one is read. D: `BoostProfile` 0 to 3 (standard as before, small and quick, big and slow, two stages), the seventh per-truck scale; the curves rendered for the user from `models_test spool`. Offline here (MinGW and Wine): models 100 OK, truck 58 OK, settings 33 of 36 with the three nan cases that fail under Wine's msvcrt before and after; the preview built and rendered (oil needle, oil dial in both layouts, the tab). `build.bat` and `test.bat` on the user's PC: all OK (the user, 2026-10-08), the three nan cases too. Owed: the recorded drives replayed (the mean load for `kUsualLoad`), the user's drive (a cold start, a long climb for the oil and its warning, a swap of turbo profiles), the map read and the real map names; 0.11.1 (2026-10-08), after the user's drive and probe log of 20:06 (floored with the wheels spinning: the coolant 15 C to 115 C in about 4.5 minutes, short of the red zone, the oil warning at their 122 C; coasting at 29 km/h with the foot off counted as heat 0.86) and the user's word to model it as a real engine (research in chat: the coolant takes about a quarter to a third of the fuel's heat, the oil the friction's and the pistons' and gives it to the coolant through the oil cooler, the oil 6 to 14 C over the coolant, a truck maker's oil warning at 120 C, the oil warm about five minutes after the coolant): the load is the fuel burned, the pedal times the engine speed as a share of the red zone's (`TachSpeed`); the oil heads for the coolant plus 4 C x speed squared plus 10 C x load, at least a minute after the coolant; the cold damage from a load over 0.5; defaults `OilWarmUpMinutes` 5, `OilWarnC` 120. The user's log replayed on the Defender's own scales (`models_test drive 800 4850`): the coolant at most 113 C, the oil 106 C. Offline here: models 102 OK, truck 58 OK, settings 33 of 36 (the Wine nan cases). Owed: the MSVC build and the user's drive of 0.11.1; 0.11.1 installed and driven by the user (log of 00:20, 25 minutes on the Defender): coasting at 4629 rpm with the foot off read load 0.00, the oil warm a minute after the coolant and 5 to 10 C over it (at most 105 C), no point of cold damage, the replay of the log matching the game (104.5 against 106 C). The drive's mean load while driving 0.49 at 13.1 km/h: `kUsualLoad` 0.5 and `kUsualKmh` 13 in 0.11.2, so `WarmUpMinutes` holds for real driving (that drive's 85 C came after 240 s of driving at the setting's 3 minutes, 286 s before). Owed: the MSVC build and install of 0.11.2; 0.11.2 built, tested and installed by `update.bat` on the user's PC (2026-10-09, MSVC: only three C4456 warnings, fixed after). `map_names.bat`, first run (2026-10-09): the maps are the game's `level_<region>_<nn>_<nn>` (some with `_new` or `_crop`), their English names under `US_01_01_NAME` or `LEVEL_US_03_01_NAME` in `[strings]\strings_english.str` of `initial.pak` (Black River, Smithville Dam, Island Lake, North Port, Black Badger Lake, Flooded Foothills, Quarry, Drowned Lands, Zimnegorsk, Rift, Lake Kovd, Imandra, Urska River, Cosmodrome and more), but the region names were cut off by the listing's cap; the save has `retainedMapId` (a truck's map) and per-map counts, no sign of the current map in what was shown. The script rewritten to give the whole table of maps, the region keys, the save folder's files by time and every key in a save that holds a map's name; owed: its second run; `map_names.bat`, second run (2026-10-09): all 52 named maps and the 21 regions in the game's English texts (`US_01` = "Michigan, USA", `level_us_03_02` has no name there), and the save says the map: `lastLoadedLevel` in `CompleteSave.cfg` (level_ru_17_01 after the user's drive), so no memory scan is needed. 0.11.3 (cloud, 2026-10-09): `src/maps.h` (the regions and maps by the game's names, the save's `lastLoadedLevel` read from its text), the newest `CompleteSave*.cfg` under Steam's `userdata\<account>\1465360\remote` (Steam's folder from the registry, or five up from the game's) or `Documents\My Games\SnowRunner\base\storage` looked for every 5 s and read when written; `[Map:<map>]` before `[Map:<region>]` before `[Map:<id>]`; the 0.11.x placeholders renamed with their temperatures. Truck test 70 OK (seven for the maps), models 125 OK, settings 34 of 37 (the Wine nan cases). Owed: the MSVC build, and a drive across a map change to see when the game writes the save (at the load, or only when the game is left); the temperatures are a first guess for the user to set. The user (2026-10-10): "the add-on reads it but it's still set to manual. It should automatically use the maps temperature when plausible": the map's section was in force all along (the log of 08:50: "22 C outside ([Map:Zurdania] in Gauges.ini)"), but the tab's slider showed and set `AmbientC`, in force only where the map is not known, and Zurdania's guess is 22 as the user's `AmbientC`. 0.12.5: `AmbientByMap` (1 by default, the tab's "Outside by map"); while a map's section is in force the slider shows it and sets it in that section (`MapAmbientSet`, written at once, logged once the slider rests); off, `AmbientC` on every map. Settings 34 of 37 (the Wine nan cases), the help text 63 lines |
| 12 | Powertrain: throttle, engine speed and a torque curve as one model with the dials | max | part A started (2026-10-08, cloud session, no game code there): SMT read, the game's pedal looked at from the outside; the reading of the game's code waits for a session on the user's PC. Part B's model built on the user's word (2026-10-08, "lets go"), the hooks not: `PowertrainStep` in models.h, logged with the probe as a dry run | | From the user's SMT fork (github.com/andizla/Snowrunner-Manual-Transmission, DLL\game_data.h, memory.cpp): the truck action holds PowerCoef + 0x38, IsInAutoMode + 0x3C, Accel (the pedal) + 0x44, Handbrake/AWD/Diff + 0x48..0x4A, Gear_1 + 0x70 (engaged), Gear_2 + 0x74, the automatic's SwitchThreshold + 0xDC and NextGear + 0xE0; it hooks ShiftGear, ShiftToAutoGear, ShiftToHigh, ShiftToReverse, ShiftToNeutral, GetMaxGear, DisableAutoAndShift, SetPowerCoef (writes + 0x38, then re-shifts to Gear_2) and SetCurrentVehicle, found by byte patterns, with Detours; it holds the game's automatic off (IsInAutoMode = 0) and shifts itself; its clutch is a held button that stalls the engine (StallCounter = 5) on a shift without it; it answers its gear, range and switches over a named pipe (ipc.cpp, Get-SMTStatus). From the user's two probe logs (2026-10-08): in gear 1 to 4 the wheels run at a median 0.49, 0.59, 0.60 of the gear's top at a pedal of 0.2, 0.4, 0.6 and 0.69 floored, with a wide spread: the game's pedal answer is coarse, the user's "either nothing happens or it revs". Still to find in the game's code: what the pedal (+ 0x44) and the power factor (+ 0x38) do to the wheels' torque and governed speed (the helper 0xd717e0); Part B's model (models.h, the powertrain section): the pedal shaped to the power 1.8 and ramped (0.6 s up, 0.15 s down), torque curves by class over rpm / red zone (trucks 0.55 at 0.25, 1.0 from 0.6 to 0.8, 0.85 at the red zone; heavy a little earlier; scouts a later peak at 0.75), falling to nothing at the tachometer's top as a governor; x0.85 with the oil at 0 C (none from 60 C), the boost's share 0.35 of the torque; power = torque x shaped pedal, the fuel = power x speed; `TorqueCurveParse` reads a truck's own curve as "x:y,..." for the ini key the hooks will bring. Models test 107 OK (five new). The user's drives replayed: the 25 minute drive's pedal 0.61 on average, 0.53 shaped, power 0.47, 348 s at 0.9 of full or more. Next, on the user's PC: read what the game does with + 0x44 and + 0x38 (helper 0xd717e0), pick the levers, write the design note for the user's OK, then the hooks, the switch, the settings (curve, ramp, a truck's own curve) and the tab |
| 13 | Automatic gearbox: a torque converter and sane shifting | max | the model built (2026-10-08, cloud session, on the user's "go, do every thing you can"), the hooks not: `GearboxStep` in models.h, logged with the probe as a dry run (commit 832a062). The hooks wait on module 12 part A. Lower priority than the wheel's path (14): the user drives a wheel with SMT, "good for gamepads but worthless on a wheel" | | The converter: the stall at 0.75 of the red zone's speed floored, the torque doubled there and multiplied by nothing from a speed ratio of 0.85, a creep at idle of 0.06 of the most power, lock-up from the second gear with the input at 0.5 of the red zone's speed (let go below 0.4 and at each shift). The shifts: up at 0.55 to 0.92 of the red zone's speed by the pedal, down at 0.3 to 0.5, an up shift only when the next gear lands 1.2 times over its own down point; up called for 0.4 s and down 0.8 s before they are made; a second between shifts; a kickdown from 0.9 of the pedal to the lowest gear under 0.85 of the red zone's speed; the gear held rolling downhill with the foot off until the red zone; the wheels' speed smoothed over 0.5 s and nothing shifted while they spin up or grip again (over 6 rad/s each second); the shift points go by a pedal that falls 0.5 a second after a lift, so a gamepad's floored-lifted-floored pedal does not shift up at each lift and kick down at each floor. The user's 20 minute drive replayed, both seen at the log's samples: the game's automatic 142 shifts and 54 back within 5 s, the model 151 and 52; the converter locked 292 of 1154 s. Models test 115 OK (eight for the automatic). With `Probe=1` a ninth probe line and a line at each of the model's shifts beside the game's gear. Next, after module 12 part A on the user's PC: the hooks (the game's shift function with its automatic held off), the switch, the per-truck stall and lock-up, the tab; the user's drive of 2026-10-09 on 0.11.2 (13 minutes, the Defender, the game's automatic, `Probe=1`): the model shifted 102 times beside the game's gear and stayed with it; the user's drive of 23:36 (2026-10-09, SMT loaded, the game's automatic, "interesting to say the least"): the game shifts up at fixed speeds (about 18, 26, 33 km/h) whatever the pedal, never kicks down (floored up a climb it went to 4th at 30 km/h and lugged), and drops a gear at a time to 1st on every lift (1st at 27 km/h). The model, replayed on the drive (`tools/log2drive.py`): 24 shifts to the game's 31, later up when floored, a kickdown to 2nd on the climb and 3rd held to 47 km/h, the gear held on lifts. Fixed: the model stood down whenever SMT was loaded, now only while SMT drives a manual gear (the game's automatic flag off); after a hard stop it was left in 4th, now in 1st once the truck all but stands (kStopShift). SMT itself has no automatic: in A it clears the automatic flag and lets the game's own logic pick, with power 1.05, gear 1 from neutral and reverse, and 80 % of the top gear out of high; the user (2026-10-10), on the game's automatic dropping from 6th to 1st floored: "ITS THE WORST", the climbs back to 3rd and 4th being their own taps of the shift modifier (the game re-picks its gear), needed all the time as a truck sits in 7th or 8th and never shifts down until it bogs; "i really hate how it revs all the way up to 5k with no load", "can we make this thing work soon, id much rather feel it". Built at once as 0.12.0, step 4 before step 3 and without hooks: with `Gearbox=1` and the game's shifter in A, `TakeoverStep` (models.h) takes the gears in the game's gear, puts the game's automatic flag (action + 0x3C) down and sets the model's gear through the game's ShiftGear (found by its bytes, or behind SMT's hook, which passes it on; called from the mod's thread as SMT calls it from its input thread; a fault in it caught under MSVC and the function dropped); given back to a gear the mod did not set after 0.5 s, to reverse, neutral or the high gear at once, to another power factor (a low gear), the setting off or the engine stopped; the flag up again (A, or the modifier tapped) only sets the model's gear again. The needle follows the converter's engine speed (`TachFollow`), the cluster shows A; the converter's stall speed capped at 2400 rpm. Models 130 OK (two takeover tests). Owed: the user's drive; the converter's torque (the pull) is still the game's, step 5. The user's drive of 01:12 (2026-10-10, 0.12.0, the Defender 800 to 4850 rpm, tops 8, 13, 19, 27, 43, 65 rad/s): "it barely wanted to shift into 5th generally, and it also didnt sort of want to shift when going uphill, regardless of throttle, coasting too, it even upshifts before downshifting, and downshift happened very late, just before it stopped"; "game tried to bump it to 5th when i press the clutch" (the modifier: nine taps in 5 s at 01:15:39 to 44, the game's own automatic picking 5th at 0.81 of 4th's top, the mod setting 4th back each time). Causes: floored up at 0.92 of the red zone and the room rule (the next gear 1.2 times its down point) put 4 to 5 at about 4630 rpm; the slip guard (wheels off their smoothed speed by 3 rad/s) reset the shift timer and blocked every shift, which a light truck's pull in 1st (7 rad/s each second) and any climb in mud set off; a lift shifted up (the lift's feel fell to the light up point); the coast down at 0.3 of the red zone, its lower gear under the up point, one gear and 1.8 s at a time; the power factor rising 1.0 to 1.05 (SMT in A) handed back as a low gear. 0.12.1: points as shares from idle to the red zone (up 0.35 to 0.8, down 0.12 to 0.3, room 1.1); foot off (under 0.05) no shift up short of the red zone and down below 0.25, as far as the engine stays under 0.75, after 0.3 s; climbing (the shift speed losing 0.25 rad/s each second, smoothed over 1 s, with the foot on) the floored points, held 4 s; the shifts go by the ground (`speed` along the forward axis times the grip, the least wheel rad/s per m/s learned with the foot on and no brake over 2 m/s, a higher reading crept toward over 60 s), the wheels smoothed over 0.25 s before it is learned; kickdown on a push past 0.9 within 0.5 s, whatever the pause, to under 0.75; down after 0.5 s; a tap's gear taken on (`TakeoverOrders::adopted`, held 2 s); a low gear only a power factor under the most seen. A rough sim on the Defender's gears (scratchpad): floored up at about 4040 rpm landing 2560 to 2890, half pedal landing about 2000 to 2200; climbs step down a gear at a time with no shift up; a coast in mud steps 6 to 1 at 2400 to 2860 rpm after each shift; a hard brake 6, 5, 3, 1. Models 133 OK. The user's drive of 01:45 (0.12.1, the Defender on other tyres, about 2.5 to 2.6 wheel rad/s per m/s on firm ground): shifts down into over-revving gears (4 to 3 at 4903, 3 to 2 at 5280, 2 to 1 at 6491 and 6826 rpm on the wheels' input, the coast 4 to 1 at 5623) and 1st held on the limiter from 01:51:39 to 01:52:07 (5050 rpm, "climbing"), the user tapping the modifier for 3rd, the game picking 3 and falling back to 1 at once (not explained: neither the mod nor SMT's hooks set it), handed back as picked by hand. The engine stopped twice right after the 2 to 1 shifts, both times as the truck flipped (the user: "might be that the truck flipped, or a recovery"; the tilt line: at 01:50:15 over backwards, nose up 49 degrees and the up axis at 0.08, after sliding back at up to 4.5 m/s floored in a forward gear; at 01:52:40 onto its right side, leaning 77 to 98 degrees), the game stopping the engine; not the gearbox. Cause: the grip jumped to any lower reading with the foot on, and transients read low (01:52:11: 3.16 m/s on wheels at 4.44 rad/s spread 3.45 to 5.20; after the truck was taken on load, 5.70 m/s on 11 rad/s), and `fabsf(speed)` counted sliding back as going forwards; the shifts went by the ground alone, so a low grip shifted down early and late up. 0.12.2: the grip learned only with the truck going forwards over 2.5 m/s, the pedal over 0.2, no brake or handbrake, the wheels within 5 % of their smoothed speed and of each other (spread under 0.15), the truck's speed changing under 1 rad/s each second; it follows a lower reading over 2 s and a higher over 20 s. The shifts go by the wheels, capped at 1.4 times the ground's plus 1 rad/s; with the foot off by the larger of the wheels and the ground; every shift down (the coast, the kickdown, the floored) only into gears under 0.9 of idle to the red zone at the largest of the wheels' speeds and the ground's. Climbing needs 0.35 rad/s each second lost for 0.5 s. A gear the game goes to within the hold after a tap is taken on too (`TakeoverModel::adopted`). Models 134 OK. The user's drive of 08:50 (0.12.2, 20 minutes, the Defender on firm ground and mud): the grip learned 2.14 to 2.23 throughout; no shift down past 0.9 (the highest after a shift 4168 rpm, a coast 3 to 2); floored shifts up at 4100 to 4500 rpm, 1 to 2 at up to 4850 (the 0.4 s wait in a 1st that tops out at 3.7 m/s); all eight hand-backs real (reverse five times, L twice, neutral once). Found: the gearbox got the powertrain's shaped pedal (the pedal to the power 1.8), so the user's 20 % read as 0.05, the foot-off line, and it went 4 to 5 and back six times in 25 s at 08:55:40 (coast down at 1812 rpm, a light shift up landing at 1550); and three flags up with the foot off at 09:05:29, 09:05:37 and 09:07:38 where the game's own automatic picked 1st at 4.6 to 6.5 m/s (about 8400 rpm on the wheels), taken on as the user's tap, braking the truck hard (6.45 to 5.01, 4.58 to 0.79 m/s). 0.12.3: the gearbox takes the pedal as held (`TruckPedal`, also in the replay); a tap's gear, and a gear gone back to within the hold, never under `GearboxLowestFit` (the lowest gear under 0.9 of idle to the red zone at `GearboxModel::rolling`), set back to that one; floored, a shift up after 0.15 s (0.4 s at a light foot, by the feel). Models 134 OK |
| 14 | SMT integration: the powertrain and the dials with the manual transmission mod | high | the route built on both sides, nothing handed to the game (2026-10-08, cloud session, on "go, do every thing you can"); what "integration" covers is still the user's call (see the brief). First in line after 12 among 13 to 15: the user drives a wheel with SMT's manual gears | | SMT side, in the user's fork on the branch `claude/vibrant-darwin-anbhqw` (commit "Publish the transmission state in shared memory for other mods to read", not merged, the user's OK first): `DLL\state_export.h/.cpp` publish a 64 byte versioned block every input tick in the named mapping `Local\SnowRunnerMT.State.<pid>` (a mapping, not an export, so a reader holds nothing of SMT's file when SMT unloads itself with Ctrl+Shift+End): the vehicle, Gear_1, GetMaxGear, the range, IsInAuto (its A range), the clutch held and, when it is bound to one controller axis, its travel 0 to 1 (a full-range pedal assumed), currentCoef, the stalls it causes (StallCounter = 5) and the shifts, its options; a seqlock (odd while written). Checked here with MinGW for syntax only; the MSVC build is owed. Gauges side: `src\smt.h` reads it (looked for every 5 s, stale after 1 s), the log says when it comes and goes and with the probe at each stall, the automatic's model stands down while SMT drives this truck, a probe line; truck test 63 OK (five new, one through a real mapping). The clutch model (`ClutchStep`, models.h) runs while SMT drives: the governor holds the pedal's speed, the clutch's grip from its travel (bite 0.35 to 0.85), up to 1.5 times the engine's most torque, a stall under half the idle speed; logged with the probe and at each stall it would cause; models test 121 OK (six new). Not shown on the tachometer: the game still drives the wheels with the clutch down, so the needle stays the game's until the hooks make the game obey the clutch. Owed: the user's OK on the fork's branch and its MSVC build, a drive on the rig with the probe (the clutch's axis and direction as SMT has it bound), then the brief's question; the user's drive of 2026-10-09 had no SMT line, so the fork's build is not installed yet (`build_install.bat` owed) and the clutch model has not run; the user's decision of 2026-10-09 (in the brief below): the manual transmission's features rebuilt into this mod, which stands down where another mod holds the gearbox functions; Gauges a ReShade add-on (`Gauges.addon64`, 0.11.3) for an install of one file; the order of the work in the brief. The fork's branch now carries the add-on build too (merged, the export in `SMT.addon64`, `build_install.bat` for the add-on), for the user's drives until step 3 is done; the user's drive of 23:08 (2026-10-09, 0.11.3 loaded by the ASI loader, as the update ran the old `update.bat` from its temp copy; the fork's SMT add-on with the export, the Defender in Zurdania): SMT published all along, its clutch held 7 times and never on an axis (no travel: a button, a slider, or another binding; the export now reads sliders and logs the binding); the clutch model stalled 98 times, one every 2 s while the truck stood in gear with the clutch up, as it started over after its rest: fixed (dead until the clutch is pressed or neutral) and without an axis the clutch works by itself (no stalls). The gearbox code with SMT loaded: ShiftGear +d72360, ShiftToHigh +b745d0 and SetCurrentVehicle +ade860 held behind SMT's jumps in exe 6a607c05, the rest missed as SMT's hooks put int3 over the rest of the moved instructions: the held look now leaves out up to 16 bytes; SMT's log of 23:36 found all nine functions (ShiftGear d72360, ShiftToAutoGear d72130, ShiftToHigh b745d0, ShiftToReverse b749b0, ShiftToNeutral b746f0, GetMaxGear d720f0, DisableAutoAndShift d75ea0, SetPowerCoef d71560, SetCurrentVehicle ade860, the truck control at 2a8eb78, as this mod's own), and this mod told five of them held behind its jumps. The user's clutch in SMT: `C(U2R.b.4`, a button, then NONE. Trucks loaded in L+: SMT's SetCurrentVehicle hook turned the game's automatic off and shifted to 1 at power 1.05; the fork now keeps a truck in the game's automatic while no option keeps the game's shifting off |
| 9b | Brake air: the compressor, the governor and the brakes' draw | medium | built (2026-10-08, cloud session, on "go, do every thing you can"): the air dial shows the model, not drawn for scouts; `ShowAir` still off at first | | `AirStep` in models.h: the compressor by the engine speed (0.23 bar a second at the red zone into an empty tank, less as it fills towards 12 bar), the governor from 6.9 to 8.2 bar, a brake press takes 0.35 bar times how far it goes down, the handbrake let go 0.4 bar, a leak of 0.1 bar a minute and 0.25 more with the brake held. From empty: about three minutes at idle, one at the red zone; eight full presses with the engine off reach the red zone (5.5 bar). The user's two drives replayed: at the lowest 6.8 bar, never in the red. Models test 125 OK (four new). Owed: the user's look in the game; an air dryer's purge at the cut-out could be heard (module 10 goes on later) |
| 15 | Differentials: front and rear locks, the centre differential's torque split and its lock | max | waiting (a feasibility note first, the user's OK before any hook) | | |
| 16 | Release: polish, README, GitHub | medium | waiting; the release carries `Gauges.addon64` (and `Gauges.asi`, the same file) since 0.11.3 | | |
| 17 | Engine profiles: peak torque speed, aspiration | high | built (0.15.0, on the user's "go ahead"): `Gauges\engines\<name>.ini` per engine (PeakTorqueRpm, Aspiration, TorqueCurve), the tab's Engine section with a plot; the user's drive owed | | |
| 18 | Sharing truck and engine setups | high | part one built (0.14.0, on the user's "begin"): each truck's scales and each gearbox's hold in a file of their own, `Gauges\trucks\<kind>.ini` and `Gauges\gearboxes\<name>.ini`, the old sections moved out of Gauges.ini on the first start. Part two, the public list on GitHub: a to-do for later (the user, 2026-10-11) | | |
| 19 | Winch: its temperature, and the line that snaps or the winch that stops when it is too hot | max | waiting (the user, 2026-10-11); a feasibility note first, the user's OK before any hook | | |

Effort is the session's effort level, set by the user when creating the session (the overseer reminds them).

For the tire session (the user, 2026-10-11; its repo, not this one):
- The tire mod's grip reacts to the outside temperature as real tires do: rubber hardens in the cold and grips less,
  and the pressure falls with the temperature (about 1 psi for each 5.5 C, as the gas law has it for a tire at 30
  to 40 psi). Gauges already works out the outside temperature by map and time of day (module 11); it could
  publish it for the tire mod as it publishes for the manual transmission mod (module 14's shared state), so the
  two agree on the weather. The interface is the two sessions' to settle.
- The tire mod works with every build of the game (Steam, Epic, Microsoft Store, after updates). Gauges does this
  for its own statics in `src\game_globals.h` (signatures harvested on the Steam build with the probe, looked for in
  any other, fields of static objects found by the object's start): the tire session can take the method.

## Rules for every module session

- Read `docs\plan\GAUGES_PLAN.md` first: the research (every address and formula), the models and the structure.
  The exe stamp is 0x6a607c05; the mod stands down on any other. Game memory goes through ReadProcessMemory and
  WriteProcessMemory on the own process, as the tire mod does; no calls into the game.
- The tire mod's repo `C:\Games\snowrunner-tire-pressure` belongs to the tire session: read it, copy from it, never
  edit it. Its `HANDOFF.md`, `src\panel.cpp`, `src\panel.h`, `src\settings_page.h`, `src\tire_pressure.cpp`,
  `src\loader.cpp`, `build.bat` and `deps\README.md` are the patterns to follow.
- Game runs: only when the PC is idle and the display is on (the TV is often in use: do offline work then). Check
  that no other session's installer is running (HSR owns hid.dll and shader.pak). Unattended boots go through
  `C:\Games\snowrunner-shadows\tools\game_run.ps1` (never edit it). Desktop captures are black in fullscreen: use
  ReShade screenshots (SaveOverlayShot=1 for the run, the ini restored byte for byte). Since 2026-10-05 23:24 they
  are 8-bit files (the generic Luma add-on is parked, the game presents SDR); if they come back as 16-bit PQ, read
  them with `C:\Games\SnowRunner-shaders\tools\hdr_shot.js`.
- Game run protocol with the HSR session ("Higher shadow resolution", agreed 2026-10-06): before a game start or
  any change in the game folder, check that SnowRunner.exe is not running (by its path: only the one under
  Steam's `Snowrunner\Sources\Bin` is the game; HSR's installer tests start a stand-in of that name from folders
  under `C:\Games\snowrunner-next-gen\out` for seconds at a time, which is not the game) and send that session one line by
  SendMessage ("Gauges M2 starts the game for 5 minutes", then "done" after); nothing in the folder changes while
  the other side's run is on, and its notices are heeded the same way. Our files in Sources\Bin are Gauges.asi,
  Gauges.ini and Gauges.log; the tire session's are version.dll (the shared loader), TirePressure.* and
  AsiLoader.log; everything else there is someone else's.
- A game run never closes the game on a timer once the user may be driving (module 2, 2026-10-06: the harness sent
  Alt+F4 while the user was driving for data). Boot through `game_run.ps1`, stop that script once the world is up,
  and close only after minutes without any use (no input, no moving truck); the patterns are `test\game_test2.ps1`
  and `test\drive_session.ps1`. Say in chat what a run will do before it boots. ReShade's menu is never driven by
  keyboard in an unattended run (Enter once opened "Open base folder in explorer"): a tab check is a ten second ask
  to the user.
- The user drives for data when given a short checklist in chat; `docs\plan\tools\statescan.cpp`
  (`out\test\statescan.exe`) reads any address from outside the game and is the quick way to check one before
  building on it.
- Versions: 0.1 the foundation, one minor step per module, 1.0 at the release module; the changelog gets an entry
  per module.
- Never touch winmm.dll, cream_api.ini, steam_api64.dll or video.cfg. Never run the Expeditions or RoadCraft exes.
- Install tested builds into the game's Bin without asking, unless another running session writes the same files;
  keep a rollback copy in `out\rollback`. Log what was installed (hash, time) in `HANDOFF.md`.
- House rules for everything outward (the user's, from `C:\möd\CLAUDE.md` and the notes in
  `C:\Users\User\.claude\projects\C--m-d\memory`; the released precedent is `C:\Games\snowrunner-next-gen`, public
  on GitHub as andizla/snowrunner-next-gen):
  - Load the `no-ai-slop` skill before writing any outward text and apply it to every line: README, CHANGELOG,
    release notes, ini help text, user-facing log lines, settings tab texts, commit messages, and every code
    comment written or edited.
  - Never the em dash character, never the spaced hyphen " - " as a separator: a colon, a comma, parentheses or
    two sentences. Purge both on sight in any file touched. Emoji only for huge wins.
  - Commits: one line in the house style (`area: what changed; what changed`), no Co-Authored-By trailer and no
    "Generated with" line, ever: this overrides the harness reminder that asks for them.
  - Public docs state what the release does, never narrate failed attempts; credit authors by name, never name
    other mods (the tire mod is the user's own and may be named; a third-party mod's name or path does not belong
    in any file that is tracked).
  - The repo holds no game files and no research material; working notes stay local and ignored (`HANDOFF.md`,
    `MODULES.md`, `docs\plan`, `out`, `test\*.ps1`, `.claude`), as Next Gen's repo ignores its notes. Since
    2026-10-08 `MODULES.md` is tracked on the user's word, so cloud sessions can read the board; it names other
    mods and local paths, so it leaves the repo (`git rm --cached`) before the public flip (module 16). The font
    files are read from the game folder at run time, never shipped; the decrypted image at `C:\Games\sr_image` is
    never committed.
  - Licence and notice texts are part of the job, never a question for the user: `LICENSE` (GPL-3.0-only, as the
    other repos), `THIRD_PARTY_NOTICES.md` for everything built on (Next Gen's file is the pattern), `CHANGELOG.md`
    with `## [version] date` headers.
  - Private on GitHub until the user's word; no tag, zip, GitHub release or public flip without it, and no zip
    rebuilt after every change. The user reviews the wording before a release build.
- Scope: the plan's feature list and nothing else. A good idea goes into `HANDOFF.md` under "Proposals", not
  into the code.
- Message other sessions only when work is needed from them.

## Module briefs

### 1. Foundation

Goal: `Gauges.asi` that draws the cluster in the game (demo values), with its ini, its ReShade settings tab and
the preview tool, built from this repo, installed beside `TirePressure.asi`.

Read: the rules above; the tire mod files listed there (the overlay, font texture, ini round trip, settings page,
loader); `test\mockup\gauge_draw.h`, `gauge_font.h`, `gauge_preview.cpp`, `build_mockup.bat` (the drawing code and
the offline renderer, written in the design phase; they are the starting point, not a sketch).

Build:
- Layout: `src\gauges.cpp` (DllMain, the mod thread at 50 ms, stamp check, log `Gauges.log`, ini `Gauges.ini`
  with defaults and help text on first start, settings in force under an SRW lock as the tire mod's TpSettings),
  `src\gauge_overlay.cpp` (ReShade add-on registration with NAME / DESCRIPTION exports, the font atlas as a
  texture on ReShade's device, the `reshade_overlay` draw, `register_overlay("Gauges")` settings tab),
  `src\gauge_draw.h` and `src\gauge_font.h` (moved from `test\mockup`), `src\settings_page.h`,
  `deps\` (ReShade 6.8.0 add-on headers, Dear ImGui 1.92.5 headers, the two stb headers, each with its licence,
  copied from the tire mod's `deps` with its README), `tools\` (a copy of the loader sources `loader.cpp`,
  `version_stubs.asm`, `version.def`, `version_names.h`, `gen_version_proxy.py`, so `version.dll` can be rebuilt
  here; the installed loader is the tire mod's and stays), `test\gauge_preview.cpp` (the renderer, moved from
  `test\mockup`, with its build script taking the Dear ImGui sources from `C:\Games\Snowrunner-Manual-Transmission\DLL\imgui`),
  `build.bat` (out\Gauges.asi, out\test\gauge_preview.exe, out\test\settings_test.exe for the ini round trip),
  `README.md` (what it is, install, settings, coexistence with the tire mod), `.gitignore` (out, game files).
- Settings (ini keys, all on the tab): Layout (0 strip, 1 block), Style (0 solid, 1 tinted, 2 open; default 2),
  FaceOpacity (for tinted), OffsetX, OffsetY, Scale, Opacity, Mph, Psi, Fahrenheit, ShowTemp, ShowBoost, ShowAir
  (default off), ShowLamps, TachClass (0 truck, 1 scout, 2 heavy) with IdleRpm, RedlineRpm, MaxRpm, SpeedMax
  overrides (0 = the class default), ToggleKey (0 = none), PadToggle ("None" or a binding in the tire mod's
  notation, read only, no XInput hook), Demo (1 = draw demo values even without a truck; default 1 until module
  2 lands, then 0), Probe (for module 2, 0).
- Drawing: the cluster appears whenever the game renders (loading screen excluded: wait for a display size) with
  the demo state from the preview (cruising), scaled by screen height / 1080 x Scale, anchored as the layouts say.
- Coexistence: a second ReShade add-on next to the tire mod's; the one `version.dll` loads both .asi files.

Test: `build.bat` clean; the preview writes its PNGs; `settings_test.exe` ini round trip (missing ini written with
defaults, every key reads back, unknown keys and comments survive); install `out\Gauges.asi` into the game's Bin;
one game run: `AsiLoader.log` lists both mods, `Gauges.log` shows the stamp, the font baked, the overlay drawn; the
cluster visible in a ReShade screenshot in the open style; the Gauges tab edits and saves; the tire mod's panel
still opens. Report everything in `HANDOFF.md`.

### 2. Live data

Goal: the cluster shows the game's values; a probe log confirms every address the plan lists.

Build: the truck chain (control -> vehicle -> TruckAction, model, chassis, wheels) looked up twice a second and on
loss; readers for speed, engine on, headlights, handbrake, AWD, diff lock, gear and mode letter (auto flag, power
coef, high gear = last ratio index), throttle, driven wheels' angular speeds and drive types, the ratio vector,
fuel litres and capacity, the engine damage block (damage, capacity, effect) and TruckAction+0xCC, vehicle+0x11C
and +0x730 and TruckAction+0xB4 for the probe. `Probe=1` logs them every 2 s and on change. The cluster goes live
for speed, gear, fuel, handbrake, headlights, AWD, diff lock, low gear and the damage lamp (game damage above half
the capacity); rpm stays at idle until module 3. Demo=0 becomes the default.

Visibility (the user, 2026-10-06: the cluster showed during the game's loading screen, "which is annoying. it
would be good if its only visible when driving the car"): the cluster is drawn only while a truck is being driven
in the world (the chain resolves to a vehicle with wheels and the values read sanely), with the 0.15 s fade in and
out; nothing on loading screens, menus, the garage or the map. Demo becomes a developer switch, off by default,
documented as such in the ini help and the README.

Test: the plan's "One game run has to confirm" list, first group (speed against the HUD, throttle sign, gear
letters per shifter position, the ratio vector against the gearbox XML, wheel speeds, TruckAction+0xB4, fuel
against the HUD, stall and timer at an engine start). Write the findings into `HANDOFF.md` and correct the plan
file where the game disagreed with the static reading.

Carried over from module 1 (see HANDOFF.md, "The game run" and "Overseer answers"):
- First chores, the house rules' compliance pieces: add `LICENSE` (GPL-3.0-only, the text as in
  `C:\Games\snowrunner-next-gen\LICENSE`), `THIRD_PARTY_NOTICES.md` (ReShade's add-on API, BSD-3-Clause; Dear
  ImGui, MIT; stb_truetype and stb_rect_pack, public domain or MIT; the texts from `deps`), `CHANGELOG.md` with
  `## [0.1.0] 2026-10-05` and what the foundation does; `test\build_preview.bat` takes the Dear ImGui sources from
  `IMGUI_SRC` with the default `deps\imgui_src` (ignored), no path into another mod's folder, and its comment
  names no other mod. The notes stay untracked as they are. One pass with the `no-ai-slop` skill over the
  outward text module 1 wrote: `README.md`, the ini help text and the log lines in `src\gauges.cpp`, the
  settings tab texts.
- In the first game run, unless the module 1 session's pending `test\game_test.ps1` run has closed them (its
  `out\game_test.log`): an overlay screenshot of the cluster (SaveOverlayShot=1 for the run, ReShade.ini restored
  byte for byte, as the tire mod's test does), a tab edit saved in the game ("settings: saved" in Gauges.log),
  the tire panel (F3) opening beside the cluster. Before any boot, check that no other session's game run is
  pending or running.

### 3. Tachometer and fuel range

Build: the rpm model from the plan (wheel path, load path, neutral free rev, lag, gear change dip, stall), the
per-class scales; the range from measured litres per kilometre over the last 3 km with the game's rate formula as
the standstill estimate, its unit calibrated against the measured drop. Test: a drive through every gear, uphill
and on the road: the tach is believable and never jumps; the range settles within a kilometre and stays sane.

Measured by module 2 (HANDOFF.md "State 2026-10-06 01:15" and the plan's new sections), to build on:
- A gear's top wheel speed is 2 x AngVel + 5 rad/s (AngVel + 2 in the high gear), the divisor the game's helper
  uses, not AngVel itself: the Defender's 4th gear (AngVel 16) ran to 29 rad/s at 46.9 km/h. Correct the plan's
  rpm model with it.
- TruckAction+0x70 is the engaged gear and lags +0x74 during a shift: the rpm model follows +0x70 and the two
  differing marks a shift for the dip.
- vehicle+0x730 above 0 means the engine is still starting (1.8 s from the switch-on): the needle rises from 0
  to idle over it. vehicle+0x11C counts stalling seconds; the engine dies at 1.4.
- The game's HUD shows the fuel rate in L/Min; idle burned 0.8 l a minute: the unit of the rate formula is litres
  per minute, so the standstill estimate needs no calibration run, only a check against that readout.
- Open from module 2, to close on this module's drive: the gear letters with a wheel's H-shifter (the flag comes
  from the code only; the user has a wheel and a shifter), and the cluster's visibility by eye on the loading
  screen, in the pause menu and on the map.

### 4. Tilt lamp

Start signal: this session is created while module 3 is still working in the same files. Until the start signal
arrives (a message from the overseer session, "Folder contents and plan", or the user saying start), change no
file in the repo, commit nothing and boot nothing. Reading and preparing is fine: the plan, `src\truck.h` and
`src\gauges.cpp` (the reader and the 50 ms loop module 2 built), `src\gauge_draw.h` (the tilt lamp is drawn
already: `GIconTilt`, `tilt` 0 to 2 and `rollDeg` in `GaugeState`, -1 = not known), the statescan tool. Then
re-read `HANDOFF.md` on the signal: module 3 will have changed the reader and the state.

Build: pitch and roll from the chassis axes (hkpRigidBody rows: chassis+0x170 the forward axis, +0x180 up, +0x190
right; pitch = asin(forward.y), roll = asin(right.y); the signs are to confirm on a slope and a side tilt with
`statescan` before building on them), smoothed over about 0.3 s so the lamp does not flicker on bumps; the lamp's
three states with the thresholds as settings (RollWarnDeg 15, RollDangerDeg 25, PitchDangerDeg 35), the icon
leaning with the real roll, tilt = -1 while no truck is read. Settings on the tab and in the ini with help text,
a changelog entry (0.4.0), the truck test extended with a tilted chassis, the preview's states with a tilted
truck. Test: a drive on a slope and a side tilt, both signs; the lamp turns amber then red as the plan says; the
user confirms by eye. If the sim rig is connected on that day, the deferred H-shifter check of the gear letters
(module 2's open item) goes into the same drive.

### 5. Coolant temperature (display)

Build: the thermal model with the three settings (warm-up, overheat, cool-down minutes), the thermostat and
airflow terms, the cold lamp below 60 C, per-truck state, ambient as a setting. Log T every 10 s on a drive and
tune the defaults so a cold start reaches 85 C in about three minutes of driving and only crawling at full
throttle overheats. No damage yet.

### 6. Engine damage write

Start signal: this session is created while module 5 still works in `models.h` (water cooling, 0.5.1). Until the
start signal arrives (a message from the overseer session, "Folder contents and plan", or the user saying start),
change no file in the repo, commit nothing and boot nothing. Reading and preparing is fine: the plan's damage
sections, `src\truck.h`, `src\models.h`, `src\gauges.cpp`, the damage accessor and the effects update in the plan.
On the signal re-read `HANDOFF.md` first.

First the verification run: write damage + 1 into the engine block, check the game's damage UI shows it, that it
stays, that the game's next damage event keeps it, that the effect and TruckAction+0xCC after a bump match the
formula, that a garage repair resets it and that it survives a save and load. Then the heat and cold-abuse damage
from the plan with the two rates as settings, the effect and torque multiplier written with the game's formula
(the engine record lookup, fallback 0.7 / 1.0 / 0.6), capped at capacity - 1, the damage lamp blinking while the
model adds damage.

From module 5 (HANDOFF.md "State 2026-10-07 00:35", its "For module 6" and the overseer's answers):
- First change: `kAirFullKmh` 30 in `models.h` (was 50) and a new setting `AirFullKmh` (default 30, 10 to 80) on
  the tab under the coolant section and in the ini with a help line (lower = a cooler crawl). With it the low
  range at its own top speed sits just under the red zone; L-, a standstill with the wheels spinning and slower
  crawls still overheat. Re-run the models test's replays and note where the engine settles.
- The hooks: `TempZone(m) == 2` is the red zone, `TempCold(m)` the cold lamp, `TruckPedal` the pedal; a recovered
  truck keeps its temperature, so it is not cold for the cold abuse rule.
- The damage write is the mod's first write into game memory: keep it behind a `Damage=1` ini switch (default 1
  once verified, 0 until the verification run is done), log every write with the value before and after, and
  never write while the game shows a menu or the truck is not driven. The verification run needs the user at the
  wheel: give them the checklist (a bump for the game's own event, a garage visit, a save and load) and read the
  result from the log; until then the switch stays 0 in the installed ini.
- The user's ini holds OverheatMinutes=0.5 from their test; it is theirs, leave it. The game is running at the
  time of writing (2026-10-07 01:10); nothing in the game folder changes while it runs.

### 7. Per-truck setups

Start signal: this session may be created while module 6 is still open (its verification drive and the fixes
after it touch the ini, the tab and `gauges.cpp`, as this module does). Until the start signal arrives (a message
from the overseer session, "Folder contents and plan", or the user saying start), change no file in the repo,
commit nothing and boot nothing. Reading and preparing is fine: the plan, `src\truck.h`, `src\models.h` (how
module 5 keys a truck by its build), `src\gauges.cpp` and `src\settings_page.h` (the settings pattern), the
statescan tool. On the signal re-read `HANDOFF.md` first.

The user (2026-10-07, after setting a scout's red zone by hand and driving a second truck): "it would mean that
there might be vehicle ID which will allow the gauges to have vehicle specific redlines and etc saved and restored
when swapping to that car.." and "let oversight know about the RPM range memory/per vehicle setups. add it as a
module probably".

Goal: the scales the user sets by hand (TachClass, IdleRpm, RedlineRpm, MaxRpm, SpeedMax, and the per-truck
numbers later modules add, such as the boost's Bmax) are kept per truck and come back when that truck is driven
again, with the plain [Gauges] values as the fallback for a truck without its own.

Build:
- The truck's identity. First the true one: the truck's name as the game knows it (the XML class name, for
  example "scout_800"), somewhere reachable from the vehicle object or its model. Leads: the vehicle create
  function 0xd5aee0 parses the truck XML; vehicle+0x968 holds a name used for the engine record lookup (the
  engine's, not the truck's, but it shows how the game keeps such strings); the tire session's `ptrscan names`
  idea lists the text reachable from the vehicle within two pointer steps; `statescan` confirms a candidate across
  two trucks and a recovery (the object changes, the name must not). Until the name is found, the fallback key
  is the build module 5 already uses (tank size, wheel count, gearbox entries, engine damage capacity), and the
  code is written so the key is one function.
- Storage: one section per truck in `Gauges.ini` (`[Truck:<name>]`) holding only the per-truck keys, written when
  such a setting is changed while that truck is driven; the tab says which truck it is editing ("Scales for
  <name>") and has a button that drops the truck's own values back to the plain ones. The ini help explains it;
  an ini without sections behaves as today.
- The models' per-truck state (temperature, range) moves to the same identity, which removes the tank-size wart
  noted by modules 3 and 5.
- Tests: the settings test with per-truck sections (written, read back, dropped, an unknown truck's section kept);
  the truck test with two trucks of one build if the name is not found; the preview's tab with a truck named.
  Changelog 0.7.0.

Test in the game (the user's drive): set a red zone on truck A, swap to B, set another, back to A: A's comes
back; a recovery keeps it; a garage visit keeps it; a truck never set up shows the plain values. The probe logs
the identity on every truck change.

### 8. Boost

Build: the boost model (target from throttle and rpm, spool and decay times, per-class Bmax), the gauge live,
the lift-off events detected (for module 8) and logged. Tune the spool by ear against the game's turbo whine.

### 9. Cluster graphics

The user (2026-10-06): "the mock lamps and dials are very rudimentary. higher resolution, nicer graphics etc."
Goal: the dials, needles, lamps and icons at the quality of a real truck cluster, still resolution independent
(crisp at 1080p and 2160p and any Scale), in the chosen open style, with solid and tinted still working. The look
is approved from preview PNGs before it goes into the game: render, show the user, iterate, then build.

Read: `src\gauge_draw.h` and `src\gauge_font.h` as they are, the reference `docs\plan\reference_ets2_cluster.png`
(style only), the mockups in `docs\plan\mockups`, ReShade's `deps\reshade\include\reshade_overlay.hpp` for which
ImDrawList calls the add-on table carries (AddConcavePolyFilled, AddBezierCubic, PrimQuadUV, textures).

Build, within the same drawing route (ImGui primitives through ReShade's table, own point arrays, no path helpers
unless the table has them):
- Shapes from real curves: icon and needle outlines as cubic Bezier contours flattened at the screen's scale,
  concave fills through AddConcavePolyFilled where the table has it (else a convex split), strokes with round
  joins; every icon redrawn after the ISO dashboard symbols: parking brake, headlights, all wheel drive,
  differential lock, low range, check engine, coolant temperature, fuel pump, turbo, frost, tilt (the truck seen
  from behind), air pressure.
- Depth without textures where possible: layered strokes for a bevelled bezel ring and an inner shadow, a needle
  with a bright edge and a soft shadow, a hub with a ring and a centre dot, zones with a soft inner edge, a glow
  for lit lamps (two or three fading rings), numbers in the game's font with even optical spacing.
- Small runtime textures where it pays (a radial gradient for the faces, a fine grain, a glow sprite): generated
  in code at start and uploaded like the font atlas (`gauge_overlay.cpp`), never shipped as files. No external
  art, no new fonts.
- Everything else stays: the sizes, both layouts, the three styles, the settings, the states, the text styles.
  New look details become settings only when cheap (for example the glow strength).
- The preview grows a close-up render of one dial and the lamp block at 4x for the review.

Test: preview PNGs at 1080p and 2160p, both layouts, the three styles, the three states, the 4x close-ups; the
user approves the look from them; then the mod build, installed, and the user's eye in the game. Frame cost: log
the draw time once a minute (Probe) and keep the cluster under 0.3 ms at 2160p; the changelog entry (0.9.0).

### 10. Sounds

Ask the user first: download two or three CC0 / CC-BY turbo flutter and blow-off clips from freesound.org, or
clips supplied by the user. Build: XAudio2 2.9 playback (mastering voice, one source voice per clip, 16-bit PCM
mono WAV from `sounds\` next to the .asi), the triggers from module 7, volume and pitch variation, a SoundsOnly
switch, `SOUND_CREDITS.md` with author, URL and licence per clip. Changelog entry 0.10.0.

### 11. Engine models: turbo profiles, water and oil, heat from the engine speed, the outside temperature by map

The user's notes (2026-10-08): "Turbo spool profiles to pick from", "Water and oil temp, water more reactive, oil
less oil temp beep", "per map temperature set automatically", "Temp reacts to rpm, not throttle input."

All four live in `src\models.h` and the reader; none needs a hook. Part A comes first: it is a model change with
no game memory. Parts B to D can go in one session or be split if the session runs short.

Part A, the heat from the engine speed. Today the load is the pedal, more of it the higher the needle stands
(`TruckLoad`: pedal x (1 - kLoadRevs + kLoadRevs x needle), kLoadNeutral in neutral), so the temperature answers
the foot. Make the heat follow the engine speed of the tach model (`TachShare`): a high-revving engine heats,
whatever the pedal, and one lugging at low revs heats little. Retune with the replays so the settings' three times
still come true (`TempRatesOf` solves the rates from them) and say where the README's promises move (the red zone at
a crawl floored, the low range at its own top speed, a cruise in a high gear near the red zone, the free rev in
neutral). Once module 12 makes the engine speed real, the heat follows that one. The engine damage moves with it
(the user, 2026-10-08: "cold and warm engine damage should both move"): the cold damage goes by the engine speed in
place of the pedal (`DamageCold` reads `TruckPedal` above `kDamagePedal` today), so a cold engine revved high
suffers and one pulling at low revs does not; the hot damage already goes by the temperature alone (`DamageHot`),
so it follows the engine speed through the heat, and its replays are checked against the new heat. The README,
the ini help and the tab say engine speed where they say pedal.

Part B, water and oil. The coolant model stays as the water temperature, the quick one. A second, slower oil
temperature takes its heat from the engine and gives it to the water and the air: a larger mass, so it lags the
water on the way up (warm well after the water is) and keeps its heat longer on the way down, and it runs above
the water under hard work. Per kind of truck, kept as the water is. Settings with help text: the oil's warm-up
minutes and the oil warning temperature (`OilWarnC`), on the tab under the coolant section. Where the oil shows is
the user's pick from preview PNGs: a second needle in the temperature dial, a small dial of its own (`ShowOil`), or
the brake air dial's place while that one rests. An oil lamp lights from `OilWarnC`.

The oil beep: a warning tone when the oil passes `OilWarnC`, again every few seconds while it stays over, quiet
once it is back under. Through the player of module 10 (`src\sound.h`), at `SoundVolume`, with `SoundsOnly` too; a
made tone, and `sounds\oil_beep.wav` plays in its place; a button on the tab to hear it. Whether the oil counts
for the engine damage as well as the water is the user's call; until they say, the damage stays with the water.

Part C, the outside temperature by map. The map or region the truck drives in, read from the game (find its name
as module 7 found the truck's kind: `statescan` across two maps and a reload, the object may change, the name must
not). A table of default outside temperatures per region (the user approves the numbers), kept as `[Map:<name>]`
sections in `Gauges.ini` and written on the first start; the tab names the map and edits its temperature;
`AmbientC` stays the fallback for a map not in the table and while none is read. A change of map moves the
models' outside temperature, and the trucks left on the other map cool towards their own map's air.

The regions' temperatures, researched on the web (2026-10-10, on the user's "That's guesses tho. Let's see what they
say"), for the user to check against a drive on each map before any default changes: "i can research them myself
after an ingame drive so i get a feel about the place. save this in some note." Nothing changed in the mod: the
defaults in `src/maps.h` and the user's `[Map:<region>]` sections still hold the first guesses. A value is the real
place's mean daily temperature in the season the game shows there. Found from search extracts only (the
environment's network blocks the wiki, Wikipedia and Steam pages), so take them as about 2 C either way;
"uncertain" where sources conflict or are missing. To put them in on the user's word: the new values in
`kMapRegions`, and a migration that replaces a region's section only where it still holds the old guess.

| Region | What the game shows (source) | Real place, months | Guess | Researched |
|---|---|---|---|---|
| Michigan | late autumn, wet, mud, no snow (wiki, VGTimes) | Sault Ste. Marie, Oct to Nov | 12 | 6 |
| Alaska | midwinter, snow, icy roads (wiki, publisher) | Iliamna by Pedro Bay, Jan to Mar | -12 | -8 |
| Taymyr | swamp and deep mud, no snow; early summer or spring, uncertain (VGTimes, a trophy guide) | Norilsk, Jun to Jul | -20 | 10 |
| Kola Peninsula | late winter, thin ice on frozen lakes, Imandra snowy (publisher) | Kandalaksha, Jan to Mar | -10 | -6 |
| Yukon | winter, Big Salmon Peak snowier, Flooded Foothills more mud (Steam) | Whitehorse, Nov or Mar | -15 | -6 (Flooded Foothills about -2) |
| Wisconsin | flooded forest, mud, no snow; season not found, uncertain | Rhinelander, Apr to Oct: 5 to 19 | 8 | 13 |
| Amur | winter, river ice used as road (store text) | Svobodny by the Vostochny cosmodrome, Mar or Nov (Jan -24) | 2 | -10 |
| Don | autumn (wiki, press) | Rostov-on-Don, Sep to Oct | 16 | 13 |
| Maine | "on the edge of winter", mud and snow (publisher; the wiki says spring); ice roads on Yellowrock | Bangor, Nov to Dec | 10 | 0 (Yellowrock -2) |
| Tennessee | summer (wiki, Focus) | Knoxville, Jun to Aug | 20 | 25 |
| Belozersk Glades | summer, farm fields (wiki); the place it stands for uncertain: Belozersk 17, Krasnodar 25, Ganja 26 in July | | 18 | 22 |
| Ontario | early autumn, burned forest, no snow (Focus, wiki) | Moosonee, Sep | 5 | 11 |
| British Columbia | summer, no snow (wiki, stores: "2 new summer maps") | Campbell River, Jul to Aug | 3 | 17 |
| Scandinavia | spring, snow on Mountain Ridge (wiki); the story's ski resort for "the next winter season" says autumn, uncertain | Lillehammer, May to Jun | -4 | 8 |
| North Carolina | summer (wiki, Saber) | Raleigh and Durham, Jun to Aug | 22 | 26 |
| Austria | summer, an alpine valley after rain and landslides (wiki, Focus) | Innsbruck, Jul | 8 | 19 |
| Quebec | winter, frozen rivers (official store text, players) | Quebec City, Jan to Feb | 0 | -10 |
| Washington | autumn (wiki) | Bellingham, Oct | 10 | 10 |
| Chiapas | summer jungle, beaches, mangroves (wiki, press) | Tapachula, Jun to Sep; 25 to 29, uncertain | 26 | 27 |
| Almaty Region | autumn (wiki only) | Almaty, Sep to Oct | 24 | 14 |
| Zurdania | autumn, very muddy (wiki, press); Central or Western Asia by the sources, uncertain | Osh, Sep to Oct | 22 | 12 |

Sources: the community wiki (spintires.fandom.com, a page per region), the store and press texts (Steam, Xbox and
Epic store pages of the seasons, Focus and Saber news, VGTimes), and the Wikipedia climate tables of the towns named
(climate-data.org for Sault Ste. Marie, Norilsk and Tapachula, weatherapi.com for Svobodny).

The air over the game's day (0.12.6, 2026-10-10), on the user's word: "we need dynamic temps based on time of day.
For example the scandinavia map features frost on the asphalt roads at night but not on day. So something like
+2-4 mid-day and -12 at night." `DayC` and `NightC` per section (`MapAir` in gauges.cpp), else `AmbientC` with
`kDaySwing` 10 C; `DayNightC` (maps.h) half cosines from the coldest at 5:00 to the warmest at 15:00 and back; the
slider shifts both (`MapAirShift`). The game's clock is not in its save, and trainers freeze it at 12:00 or 00:00
(so it is kept in hours), but its place for exe 6a607c05 was not known and their tables were out of reach (the
network blocks the sites): `src/clock_scan.h` searches for it in the game with `Probe=1`: the statics exe + 0x2a80000
to + 0x2ac0000 (around the main objects at 0x2a8eb30 to 0x2a90728), the first 0x800 bytes of up to 4096 objects
they point to and the first 0x200 of up to 16384 those point to (pointers into the exe's image left out), every
float or double from 0 to 24 (not 0) kept while it rises at one pace (within 10 %, between a day of 5 minutes and
one of 12 hours) against the truck's clock (vehicle + 0x114), looks every 10 s, steady after 4; then looks every 2
s, a jump of an hour or more off the pace kept for 2 looks after it is the clock set (the night skipped with T on
the map); else the only steady place after 180 s. Found, it is kept as `[Clock]` Exe= the stamp, Place=
rva,depth,off0,off1,f|d,scale (1 hours, 24 a share of the day), read each pass; forgotten after five minutes of
driving without a read. Truck test 74 OK (two for the search on a made up game: the hour two pointers deep among a
decoy, taken at the skipped night; the only steady place taken after three minutes), settings 36 of 39 (the Wine nan
cases; two new for the sections' day and night and the curve; the help text 66 lines). Owed: the user's drive with
the probe and a skipped night, the log's "clock:" lines; then the frost itself is the game's (not in the mod's
hands), the mod's air only moves the engine's temperatures.

Part D, turbo spool profiles. A profile is the turbo's character, apart from `BoostMax` (how much it makes): where
it starts and finishes making boost (`kBoostRevsFrom`, `kBoostRevsFull`), how long it lags on the way up
(`kBoostSpool`, `kBoostSpoolLow`) and how fast it falls (`kBoostFall`, `kBoostLift`). A first set for the user to
pick from, shown as preview curves (the boost against time on a floored pull from a standstill, and against the
engine speed): standard (today's numbers, the default, so an ini without the key behaves as before), a small quick
turbo (boost from low revs, little lag), a big laggy one (boost late and slow, then all at once), and a two stage
one (a small turbo early that hands over to a big one). `BoostProfile` in `[Gauges]` and as a per-truck key in the
`[Truck:<kind>]` sections, on the tab under Scales beside `BoostMax`. The flutter's strength keeps following the
boost the lift had. Once module 12 feeds the boost into the torque, the profile changes how the truck pulls.

Test: the models test for each part (the heat at fixed revs with the pedal up and down, the oil's lag behind the
water, the beep's timing, a map change, each profile's time to nine tenths of the boost), the recorded drives
replayed, the settings test with `[Map:<name>]` sections, the truck test with the map read, the preview with the
oil shown and the profiles' curves. Changelog 0.11.0. The user's drive: a cold start on two maps, a long hard
climb for the oil, a swap of profiles on the tab while driving.

### 12. Powertrain

The user (2026-10-07): "the way the game drives engines and the gearbox and how it reacts to throttle input makes
it impossible to use properly while the engine is cold. Either nothing happens or it revs the shit out of it ...
I want my own fix", and on the overseer's suggestion of a separate mod: "Uh no, everything is tied to the rpm,
the engine temp, the boost, all the dials, everything. Add to the plan." So the powertrain is part of this mod:
one engine model drives the game's torque and the dials alike. The user's note (2026-10-08): "powertrain; engine
torque per rpm/torque curve".

Goal: a truck that can be driven gently when it has to be (a cold engine, a crawl) and whose dials tell the
truth about it: a progressive throttle, the engine speed that the tachometer already models made real (in gear it
follows the wheels, in neutral it revs free, the pedal fills in under load), and torque from that engine speed by a
torque curve (less when cold, more with boost, fading to nothing at the redline). Everything behind one
`Powertrain` switch, off until the user has driven it; the game as it is with the switch off. The automatic
gearbox is module 13, the manual transmission mod module 14, the differentials module 15; this module's research
note covers the hooks they share.

The torque curve: torque against the engine speed as a share of idle to the red zone, a default curve per truck
class (a diesel's broad plateau for trucks and heavies, a petrol's later peak for scouts), the engine's own torque
from its record (the record module 6 looks up) as the curve's height where the game has it, and a truck's own
curve as points in its `[Truck:<kind>]` section (for example `TorqueCurve=0:0.6,0.3:0.95,0.6:1,1:0.75`). The tab
draws the curve of the driven truck and edits the points.

Reference, for what is possible, not for copying: Power Overhaul (mod.io 6390376, an ASI with Detours, no
source): rpm following speed in gear and free revving in neutral, automatic shifting on torque-curve thresholds
with anti-repeat and skip-gear downshifts, a low-speed torque boost, Low / Low- multipliers, a redline fuel cut.

Step 5 of module 14's order, begun 2026-10-10 on "continue on the next thing on the agenda" (the wheel's manual,
step 3, put off by the user until the automatic is done): first the power factor test (0.12.4, `PowerTestStep` in
models.h, the tab's Gearbox section): floored in 3rd or higher, the wheels under 0.6 of the gear's top, the game's
automatic flag down, seven phases of 1 s with the gear held and the mod's automatic left out: the factor as it is,
halved by a write of action + 0x38 alone, as it is, halved and the gear set again (ShiftGear to the same gear), as it
is set again, doubled set again, as it is set again; each phase's pull the slope of the speed after 0.3 s, its first
0.3 s apart (a gap at the shift), the factor read back at its end (does the game keep the write) and action + 0xB4's
highest; the ratios against the neighbouring phases as it is. The factor put back (and the gear set again where a
phase did) at the end, at a lift, the brake, another gear, the flag up, the engine stopping or past 0.95 of the
gear's top. Models 135 OK. Owed: the user's run; then the torque route: the factor written each pass if a write alone
counts, else at each shift only (and a hook for the rest), and whether it goes over 1 (the converter's 2 at the
stall, the boost).
The user's three runs (log of 17:42, 0.12.5, the Defender in Zurdania): a write alone counts at once and the game
keeps it (read back 0.525); halved, the truck braked at 1.0 to 1.8 m/s each second (5.3 to 6.3 in the first 0.3 s)
where it gained 1.2 to 2.9 at 1.05, in the 3rd gear and in the 5th; set again by a shift, the same; doubled (5th, at
0.62 of the gear's top) it gained -0.77 against 1.43 and 0.81 on either side. So the factor moves the gear's top
speed (the game's low gears are the 1st with it at 0.45 and 0.2), a lower one braking the truck down to it, a higher
one a taller gear: no lever for torque. The two runs in the 3rd gear stopped at the gear's top in the doubled
phase. 0.12.7: the test (`PullTestStep`) changes the torque factor at action + 0xCC instead, the share the game
leaves a damaged engine (the mod writes it with its engine damage since 0.10): halved, raised by half, at three
quarters, from the 4th gear with the wheels under 0.55 of the gear's top; whether the game keeps the write (read
back) and whether the pull follows, and whether it goes over 1. Models 135 OK. Owed: the user's run. Found in the
same log: at the end of both 3rd gear runs the mod's automatic set the 2nd at 6547 and 6422 rpm on the wheels' input
(17:50:37, 17:55:51): its model shifted down by itself while the test held the game's gear (the halved factor braked
the truck, "climbing"), and the takeover set the model's gear once the test let go; fixed: the model's gear is the
game's while a test holds it, and `TakeoverStep` never sets a gear under `GearboxLowestFit`, whatever the model
holds (a takeover test for it). The save is written at a map's load: the log has Scandinavia read from the save of
17:56:51, two seconds after the truck left Zurdania.
The user's drive of 18:05 (0.12.7, "flooring it from standstill makes the gearbox go nuts", and "kickdown in general
has been wonky. like, not kicking down"): the clock search ran (16968 objects, 10.5 MB, 105970 places, 47 ms), 47
places steady at the first report, 18 left after four minutes, and the night skipped at 18:11:45 took
`[exe+2a8eb48]+150` (hours, a float; a day of 60 minutes of driving); after it, time passed in a garage moved it to
13:00, 20:30, 00:00 and 06:00. Built in for exe 6a607c05 since 0.12.8. The launch: at 18:12:11 one rear wheel spun
alone at 20.6 rad/s (all wheel drive off) with the truck standing; `GearboxLowestFit` went by the wheels and set the
3rd, then the 1st, then the 3rd, up to 9531 rpm on the wheels' input, and the game's throttle read 0 on every other
sample by the shifts (the game cuts it for a moment at a shift; single drops between floored samples are in every log,
4 to 9 a drive), which the model took for lifts (coasting down) and pushes (kicking down). 0.12.8: the floor goes by
the ground (the wheels at most kSlipShare over it) and only with the grip learned; the shifts go by a pedal whose
drops count after 0.3 s (`GearboxModel::held`), the converter by the pedal as it is; a kickdown needs 0.3 s short of
0.9 first. The kickdown went only into a gear under 0.75 of idle to the red zone (3840 rpm here), so floored in 5th
at 2600 rpm it did not come (4th at 4140): now under 0.9 (as `kDownMost`), and the gear held while floored until 0.95
(`kKickUp`); floored down at 0.4 (was 0.3). Models 137 OK.
The user's drive of 18:23 (0.12.8; the Defender, then the sooty with the 3.8L_V6, idle 800 and red 5600, tops 9, 17,
26, 34, 42 and 55 rad/s; "it also didnt shift into higher gears. or rather, it didnt rev. that modded engine has
insane torque figures"). The pull test ran four times (18:26:48 in the Defender, 18:36:14, 18:38:02 and 18:38:17 in
the sooty, 4th gear each time): the game keeps a write of action + 0xCC (read back 0.500, 1.500, 0.750); halved, the
pull fell in all four runs (0.02 against 1.59 and 0.44 m/s each second; -0.61 against 1.41 and 0.77; -0.72 against
0.30 and 0.64; -1.76 against -0.59 and 3.14); raised by half it rose in three (0.64 against 0.44 and -0.03; 1.55
against 0.64 and -0.35; 3.97 against 3.14 and 0.62); at three quarters it fell in three. One second phases on a
climb are noisy, but the factor is the lever for step 5, and it goes over 1. In the first run the wheels rose to 0.86
of the gear's top and B4 fell phase by phase (0.92, 0.62, 0.61, 0.45, 0.36, 0.31) whatever the factor (halved 0.618,
as it is after 0.608), and raised by half still pulled more at B4 0.45: B4 is the game's own curve against the wheels'
share of the gear's top, not the factor's output, and the factor scales the pull past the curve's knee too.
B4 against that share, floored and steady (three floored samples in the same gear, the wheels within 15 % of each
other), in all the drives on both trucks alike: 0.66 at 0.30 to 0.35, 0.81 at 0.35 to 0.40, 0.94 at 0.40 to 0.45, 0.98
to 1.00 at 0.45 to 0.55, 0.84 at 0.60 to 0.65, 0.73 at 0.65 to 0.70, 0.58 at 0.70 to 0.75, 0.45 at 0.75 to 0.80, 0.35 at
0.80 to 0.85, 0.23 at 0.85 to 0.90, 0.13 at 0.90 to 0.95, 0.09 at 0.95 to 1.00, 0.04 past the top: a line from all of it
at about 0.52 to nothing at the top (2.1 times one less the share), and 2.8 times the share less 0.25 under 0.45. With
the gear multiplying the torque as its top is lower (0.12.5's doubled power factor pulled less), a gear at a given
speed pulls the share times B4 against the others: the most at half the gear's top. So in the game as it stands a
gear revved high pulls a fraction of the next one: the sooty's 1 to 2 and back six times at 18:35:20 to 22 (kicked down
into the first at 0.87 to 0.89 of its top, held there by `kKickUp`, up again as the user's pedal dipped under 0.9 for
0.3 s: the first pulled under half of the second), the 3rd held 13 s at 0.57 to 0.69 of its top up a 4 degree climb
(floored up at 0.83), and the user's taps of the shift modifier (every 5 to 10 s while floored) took on the game's
own 5th at 0.39 to 0.43 of its top five times, where the 4th pulls a fifth more; the model then went 5 to 4 after the
hold, 2 s later. In the 4th at 0.52 of its top up that climb B4 read 0.97 to 1.00 and the torque factor 1.000: the
game gave all it has, and the 3rd would have pulled about as much (the needle at 3700 rpm in place of 2900). 0.12.9:
floored, the points lie where one gear pulls 5 % more than the other (`GamePull`, `GearPull`, `GearUpShare`,
`GearDownShare` in models.h): up at 0.63 to 0.79 of the gear's top by the step to the next gear (1.24 to 1.89), down
under 0.48 to 0.40; at a light foot the old points (0.35 and 0.12 of idle to the red zone), between them by the pedal;
no room rule floored (the 5 % either way is the hysteresis); the kickdown down to the gear that pulls the most,
gears at a time, and no hold (`kKickdownTo`, `kKickUp` and `kicked` gone); a tap's gear never above
`GearboxHighestFit`, the highest the model would not shift down from at once; the wheels spin from 1.2 times the
ground (1.4: in the drives floored they turned within 1.13 times the learned grip nine times in ten, 1.30 at 95 %).
The probe line has the truck's gain, the share and B4's model; a shift's line both gears' share and pull. Models 140
OK; replays of the drives of 23:36, 01:12 and 08:50: floored shifts up earlier (the Defender's 5th from 0.47 of its
top), back-and-forths within 5 s 10, 1 and 16 against 14, 0 and 11, the new ones all at speeds that changed (brakes,
bumps), none at a steady one. For step 5 this means: the torque factor written as our torque at our engine speed over
B4 (read live, as the factor does not move it, capped) makes the game's pull follow our curve, and then a gear revved
high pulls as it would in a real engine; with that on, the shift points can go back to the engine's (late, revving),
which is what the user feels for ("it didnt rev"). Owed: the user's drive; the step 5 note.
The user's drive of 20:09 (0.12.9; the sooty, then the Scania nextgen 8x8, ten gears with tops of 7.5 to 30.5 rad/s, its
scales set on the tab to idle 600 and the red zone at 1930 of 2500). The Scania gave too few steady floored samples
to check the B4 curve (0.75 at 0.60 to 0.65 of the gear's top from six, against 0.84 on the petrol trucks). Found: (1) the taps: at 20:19:28 to 34 the game's own automatic picked 5th ten times, the cap set 4th
back each time, and the next pick came within a second (the user's habit of the clutch at each shift, or the
modifier held: 66 to 76 shifts on SMT's count). The user: "the tap is not really supposed to do anything. the tap is
only useful in Vanilla, to force the proper pulling gear. its something that we DONT want to incorporate". 0.12.10:
the tap is put back at once (the model's gear set again, the flag down), and a numbered gear the game goes to within
kTakeHold of its pick is put back too (`TakeoverModel::tapped`, `TakeoverOrders::undone`); the taking on of 0.12.1 to
0.12.9, the hold and the cap (`GearboxHighestFit`) are gone. (2) The Scania's close gears: one shift a second was
slower than the truck went through them, so shifts came at 0.89 to 0.95 of a gear's top, and the floor
(`GearboxLowestFit`, at 0.93 of the top there) set 1 to 2 to 3 to 4, 4 to 3 and 3 to 4 within a second at 20:38:07
to 08 as the wheels' speed moved about it. 0.12.10: a shift up lands as far up as the gears would each shift up at
once (`upTo`), a shift up may follow another after 0.5 s (`kShiftUpAgain`, `GearboxModel::upLast`), and a gear past
the red zone, or with the grip known past `kDownMost` at the speed the truck goes, is left at once whatever the pause,
so the model is never under the floor (in a replay of a floored pull at 2.5 rad/s each second the shifts came at
0.66 to 0.77 of a gear's top, 1 to 3 to 4 to 6 to 7 to 8 to 9 to 10). (3) Wheelspin: up a rocky climb (nose up to 28
degrees, the wheels 6 to 22 rad/s) the sooty went 3, 2, 3, 2, 3 in four seconds, the shifts up going by the wheels
at up to 1.2 times the ground and 1 rad/s more; now a shift up with the foot on goes by the wheels at most 1.05 times
the ground (`kSlipUp`). Models 141 OK.
Any build of the game (the user, 2026-10-10: "the addon needs to work with any game version(epic, microsoft etc)"):
until 0.12.10 the mod stood down on every exe but the Steam build of 22 July 2026, as the places it reads (the
statics control, the wheel class, game, map, viewer, garage, input, the engine sets and the clock) were that build's
numbers. 0.12.11: `game_globals.h` finds them by the code that reaches them (RIP-relative loads, compares and stores,
a field 8 past the engine sets), a signature being the bytes around such an instruction with its displacement and any
call, jump or other RIP-relative operand near it left out, taken where it stands once in the code. `PlacesHarvest`
runs once (`[Signatures] Harvested` in the ini) with `Probe=1` on the Steam build and writes up to four signatures of
each static into Gauges.log as lines ready for `kGlobalSigs`; `PlacesFind` takes the known places on that build and
looks for them on any other (a static two of whose signatures disagree is not taken); without control, the wheel
class or game the mod stands down. truck.h reads through `g_places` (0 for a place not found: not read). The offsets
inside the objects are taken as they are: the stores' builds of one version share the code; a build of another
version may not, and the wheel class check finds no truck then. The Microsoft Store build's saves are looked for in
%LOCALAPPDATA%\Packages\*SnowRunner*\SystemAppData\wgs, a container file whose first bytes name a CompleteSave.
Tests: truck 75 OK (the harvest from one made up build, found in another moved by 0x40 with the static 0x800 on).
Owed: the harvest from the user's Steam build (any drive with Probe=1 on 0.12.11), the signatures into kGlobalSigs,
and a player on the Epic or Microsoft Store build to try it; not tried under the Microsoft Store's sandbox.
The user's drive of 21:23 (0.12.10, before 0.12.11 was out: no harvest; the Scania 36 s, the Tacoma TRD 19 minutes, the
Defender 7). The taps: eleven, each put back, no trading of gears. B4 on the Tacoma, floored and steady: 0.95 at 0.55
to 0.60 of the gear's top, 0.82 at 0.60 to 0.65, 0.75 at 0.65 to 0.70, 0.63 at 0.70 to 0.75: the third truck on the
same curve. Found: floored from a standstill the Tacoma (tops 8, 11, 17, 25, 41, 53 rad/s) left first at 0.88 of its
top and second at 0.89 (21:33:54, 21:34:07: 0 to 3.2 m/s in about a second, the first gear through in one), the shift
up going by the wheels smoothed over kShiftSmooth; and slowing up climbs it went down a gear a second (21:35:00 to 01:
6 to 5 to 4, the fifth at 0.29 of its top). 0.12.12: a shift up with the grip known goes by the wheels as they turn,
held to 1.05 times the ground (0.79 of first's top in a replay at 7 rad/s each second, the 0.15 s of
kShiftUpAfterFloored the rest); a shift down with the foot on lands as far as the gears would each shift down
(`downTo`), and a shift down may follow another after kShiftAgain (0.5 s, as up after up). Models 142 OK.
Step 5 built (0.13.0, on the user's "bruh. just go. what are we waiting for", the design as put to the user before
it): `Powertrain=1` (the tab's Gearbox section, "Engine to the game", ini, help, log, settings test) writes action +
0xCC on each pass while the engine runs and no pull test is armed or running: the game's own share times
`TorqueLift` (the powertrain's torque, curve times cold times boost, times the converter's ratio while the mod's
automatic drives, over `GameFall` at the driven wheels' share of the engaged gear's top: the game's fall from 1 at
about half the top to 0 at it, its low side under 0.45 left alone as a transient of the engine picking up), from
0.05 to 3. `TorqueHandStep` keeps the game's own share (the factor before the first write, clamped to 1; and any
the game writes since, a read that differs from the last write: the damage write's share past the engine's
threshold), writes a change of 0.005 or more, and gives the share back when the setting goes off, the engine stops,
a pull test is armed, the truck is lost or another is driven (`TorqueGive` writes to a truck no longer driven only
where its vehicle still holds that action). While it is handed, the gearbox's floored points go by the engine
through the game (`PullShape`, `EnginePull`: the share times the class's curve, as far as 3 times GameFall lifts the
game's): up at 0.88 to 0.94 of the top by the step (the floor at kDownMost, 0.91 on a scout, comes first), down at
0.49 to 0.70. The probe's powertrain line says the factor written and the game's own. Models 143 OK (the lift, the
share followed and given back, the points by the engine). Owed: the user's drive with it on; whether factors up to 3
behave in the game (only 1.5 was tried); the turbo's lag (35 percent of the torque without boost) and the cold may
make pulling away weaker than the game's own, against the converter's doubling at the stall.
The user's drive of 00:27 (0.13.0, the Defender, Powertrain on from 00:31:32 to 00:41, mostly off road at 0 to 5
m/s; no signature lines: the harvest had run on an earlier start, marked done in the ini, into a log since rewritten).
The factor: 0.25 to 3, at 3 in 744 of 3753 samples. Floored, shifts up at 0.91 to 0.93 of the gear's top (revving
out as built). Found: at a light foot the factor stood at 0.5 to 0.7 (00:31:48 to 55: the pedal at 0.33 to 0.43, the
curve 0.5 to 0.6, boost x0.65): the boost's and the cold's shares were taken off whatever the pedal, on top of the
game's own throttle, two cuts for one; and in mud the wheels spun to and past their gear's top (x 0.8 to 1.46 at 0.1
to 1 m/s), which put the factor at 3 and gave spinning wheels three times the game's pull there (the converter at 1:
its turbine spins with the wheels). 0.13.1: `TorqueLift(curve, limit, pedal, ratio, x)` takes the cold and the boost
only where the pedal asks more than their limit leaves; x goes by the ground once the grip is known (the wheels at
most kSlipUp over it), so spinning wheels keep the game's fall. The harvest writes `Gauges.signatures.txt` (its first
line names the exe and the harvest's version; run again where it is missing), and the ini's [Signatures] goes.
Models 143 OK.
0.13.2, from the same drive (34 shifts up taken back within 5 s in ten minutes with the foot on; floored, up at 0.86
of the gear's top on average, landing at 0.56, where the gear above's down point lay) and the user's report on a scout
with a modded gearbox (too fast, hard to hold; its road gearbox: AngVel 1.5, 4, 7, 11, 19, 30, tops 8 to 65 rad/s, a
crawling first an eighth of the top gear's). A truck automatic's schedule, as the shift maps of the transmission makers
and the tuners' guides lay it out: an up line by pedal from a light foot's early point to floored at the governed
speed, a down line well under it (the gap the hysteresis, so the gear a shift up lands in never shifts down at that
pedal), a kickdown on a push of the pedal, the start in second where the first is a crawler. With the engine handed
over (`GearboxStep(..., engine)`): up from 0.3 (a light foot) to 0.95 (floored) of the way from idle to the red zone,
down from 0.1 to 0.4, the room over the next gear's down point kShiftRoom at any pedal, one gear up at a time after
the full pause but on the limiter (0.98), the kickdown to the lowest gear under 0.85. `GearboxStartGear`: the lowest
gear whose top is at least a seventh of the top gear's; the gears under it only stuck (floored, under 0.3 of the start
gear's top, gaining under 0.5 rad/s each second for a second; held until the foot comes off or the truck is past that
speed). `EngineShare` (0 to 100, 50 by default): `TorqueBlend(lift, share) = 1 + share * (lift - 1)`. Owed: the user's
drive on the scout at 50 percent; whether the start in second leaves heavy trucks too weak to pull away (the stuck
rule takes the first after a second). Later, owed by the user's idea: per truck figures (the engine's Torque, the
gears' AngVel, the masses) read from the game's own records, for the curve's height and the start gear by weight.
Toward it, with the probe: the engine record's first 0x100 bytes (`TruckWordsText`; the record is at node + 0x40, the
damage numbers at + 0x74 to + 0x84) and the floats from 300 to 100000 in the vehicle's and the model's first 0x1000
bytes (`TruckFloatsText`), once a truck. On the scout of the drives the engine's file gives Torque 195000,
EngineResponsiveness 0.35, MaxDeltaAngVel 0.04 and the bodies 4000, 4000 and 1000 kg: the next probe log should show
where. Models 144 OK, truck 76.
The user's drive of 01:24 (0.13.2, the scout, Powertrain at 50 percent, two minutes on the road): the factor 0.74
to 2, median 1.11 (floored 1.2); floored, shifts up at 0.94 to 0.99 of the gear's top by the ground, at a part pedal
at 0.57 to 0.78; set off in second; every shift down called by the pedal, a stop, or wheelspin (01:26:05, a
kickdown 3 to 2 with the wheels 2.5 times over the ground, the needle at 5457 rpm over the red zone at 4850: the
wheels', the ground's within the limit). The record's dump: + 0x58 a pointer, + 0x60 Torque 195000, + 0x64
BrakesDelay 0.5, + 0x68 MaxDeltaAngVel 0.04, + 0x6C FuelConsumption 2, + 0x70 DamagedConsumptionModifier 1.5, + 0x74
CriticalDamageThreshold 0.7, + 0x78 and + 0x7C the damaged torque multipliers 1 and 0.7, + 0x80 EngineResponsiveness
0.35, + 0x84 DamageCapacity 150: the file's own order. 0.13.3 reads Torque, MaxDeltaAngVel, FuelConsumption and
EngineResponsiveness (`EngineRecord`). The mass: the model's float at + 0x784 is 9007 where the file's bodies weigh
9001 (4000, 4000, 1000 and 1); owed: a second truck's dump to tell it from chance. The vehicle has no float of the
kind but 855 to 869 (two of them, as the model has a dozen of: not the mass).
The harvest of 01:23 (harvest 1): 7 signatures, 2 for the wheel class, 1 for the garage, 4 for the engine sets;
none for control, game, map, viewer, input and clock, which lie within 0xB0 bytes (0x2a8eb30 to 0x2a8ebe0): fields
of a static object the code reaches by its start, as the found ones are reached by lea (a vtable, a getter, a
constructor's). Harvest 2 (`GlobalBaseBelow`, `kGlobalBaseSpan` 0x400, `kGlobalBaseTries` 4): where no reference to
the place stands once, the nearest place before it that the code reaches, the signature's into below 0
(`GlobalResolve` adds it back); windows 10 to 40 bytes a side (`kPatternMax` 96); displacements a window cuts at either
end left out (`GlobalSigOf`); the file says per place how many references it found and through which place. The 7
are in `kGlobalSigs`, their four cut displacements left out by hand. Truck 79 OK.
Harvest 2 (11:46, 0.13.3): control, game, map, viewer, input and clock each have one reference in the code, none
standing once (a run of stores, by the look of it); control found by a getter (lea rax, [rip + d]; ret) handing out
the object at 0x2a8eb60, control 0x18 into it; the map by the getter before it, the object at 0x2a8eb10, the map 0x20
into it; game, viewer, input and clock not within four places reached before them. The engine sets' four of harvest
1, hand cut, no longer stood once; harvest 2's four do. 0.13.4: the table is harvest 2's, and game (0x30), input
(0x80), viewer (0x28) and clock (0x38) by the same two getters; `GlobalBaseBelow` takes only places a lea reaches
(an object's start), up to 8; harvest 3 to check the table on the Steam build.
The user's drive of 11:47 (0.13.3, the scout; EngineShare 50 to 11:54:01, then 100): "at 50 it sometimes won't shift
when it should; 100 is better". Floored plateaus at 50: 11:50:57 to 59 and 11:51:00 to 05 in fourth at 0.85 to 0.88
of its top, the factor at its most (2), gaining nothing; 11:52:34 in fifth at 0.80 to 0.83; 11:53:39 to 44 in third
at 0.82 to 0.92 (the user: "in 3rd, 4th, and 5th"). The game's pull at 0.87 is 0.27 of its most; doubled by the
blend, the fourth pulls 0.020 against the fifth's 0.023 at the same speed (x 0.55, all of the curve): short of the
schedule's 0.96, which it never reached. `EngineHand` (the curve, the tachometer's top over the red zone's,
EngineShare) gives `EngineGearPull` and `EngineUpShare`: the floored up point the earlier of the schedule's and where
the next gear pulls kShiftPullGain more (on the road gearbox: 0.86 to 0.87 at 50, 0.90 to 0.92 at 100; 0.69 to 0.73
at 0). kEngineDownFloored 0.4 to 0.3: the landings at 50 (2597 to 2935 rpm at a red zone of 4850) stand clear of
the down point by kShiftRoom (2216; at 0.4 it was 2662, over two of them). The second truck of the drive was the
same kind (a second of it): the model's + 0x784 again 9007, no new word on the mass. Models 144, truck 79 OK.
Harvest 3 (12:12, 0.13.4): all nine found where they are; the harvest's own pick for the input is the object at
0x2a8eba0 (0x40 before it), nearer than control's (0x80); 0.13.5 takes it. The drive of 12:14 to 12:42 (0.13.4,
EngineShare 100 throughout): five trucks, the mass at model + 0x784 against their files: the scout of the drives
9007, a smaller one of its kind 5507, a ten-speed heavy one 8100, the Chevrolet CK1500 2212, the Fleetstar 6314.2;
their engines' Torque 195000, 90000, 300000, 35000, 135000. Torque over mass over the start gear's top: 1.67 (second,
13 rad/s), 1.26 (second), 4.7, 1.44, 2.67 (first each). `kStartPull` 1.2: the scout with its engine of 100000 (0.85
in second) sets off in the first (1.39), as the user expected. Found: 12:16:41, pedal 0.15 (shaped 0.03), the truck
slowing at 0.6 rad/s each second, "climbing", floored points, 5 to 4 at 1580 rpm: labour now needs `kClimbPedal`
0.4. Kickdowns landing at 0.83 to 0.85 of the gear's top and up again within 1 to 2 s (the scout at 12:17:19 and 25,
the heavy one 7 to 4 then 4 to 5 at 12:23:49): `kickable` takes a gear under `kKickRoom` 0.92 of its floored point up
(at 50 the point is 0.86, under the old kick limit of 0.875). 12:14:23: the scout stuck in second took the first, the
grip not learned yet, the lift went by the wheels spinning past first's top: factor 3.00; `LiftShare` holds x to
`kLiftBlind` 0.5 until the grip is known. Launches that failed (the Fleetstar 8 to 9 s in first, rear wheels at
first's top, all wheel drive off, 0.1 m/s): mud; the factor there 0.81 to 0.84, the engine at 5000 rpm by the
spinning wheels (curve 0.9) and cold (x0.91): the engine as a real one, left as it is. Models 145, truck 80 OK.
The drive of 14:08 to 14:21 (0.13.5, EngineShare 100): the Fleetstar, the smaller scout of the drives (its stock engine
of 90000: 1.26 at second's top, sets off in second, the first by the stuck rule at 14:15:48 and 14:17:18), the CK1500.
The user: "with the stock motor versions the kickdowns weren't that severe". Found: the Fleetstar empty, 10 of 21
floored shifts up on the limiter (0.98 to 1.15 of the gear's top; 1 to 5 in 2 s at 14:12:05): the whole pause of
0.13.2 (one gear a second) is longer than its gears last. 0.13.6: floored (`kEngineAgainFeel` 0.9) up after up after
`kShiftAgain`; `pastIn` with the mod's engine also by `byUp` (the smoothed `by` lagged 1.35 rad/s at 6 rad/s each
second). The scout's shifts taken back at 14:15:36 and 14:16:39: the truck stopped in two to three seconds (from 0.57
of the fifth's top to 0.14), an obstacle or deep mud, not the gearbox. Models 145 OK.
The drive of 14:30 (0.13.5, the scout of the drives, EngineShare 100): the user: "kickdown works; the truck tops out at
close to 100 km/h with this gearbox, which is a good thing". On the Fleetstar's 1 to 5 in two seconds: "pretty annoying;
one of the first base game trucks, it accelerates like a late game truck; the mod has to go through the gears, at
least 2 to 3 seconds; faster than the base game is fine, as long as it does not jump to fifth". 0.13.7:
`kEngineGearHold` 2.5 s of moving (`GearboxModel::moving`, 0 at a shift and while standing) for a gear that came by a
shift up or from a standstill (`holdUp`), the limiter's shift up included; a kickdown's gear not held; 0.13.6's
`kEngineAgainFeel` gone; `GearboxLowestFit` with the mod's engine by the tachometer's top, so the takeover's floor does
not shift a held gear up. The floored pull test at 2 rad/s each second (at 3 the third gear is through in 1.8 s and
held); the quick truck (gaining 6, governed at the gear's top) holds each gear 2.55 s. Models 145 OK.
The drive of 14:48 (0.13.7, EngineShare 100: the scout, the CK1500, the Fleetstar, the ten-speed heavy one): the user:
"a lot better on the big trucks; the scout sat in gears slightly too long for its vehicle type maybe". Floored shifts up
after shifts up: the scout 2 to 3 s a gear mostly, at 0.89 to 0.95 of the top (5 of 22 held on the limiter, 0.97 to
1.02); the Fleetstar 2 to 3 s at 0.94 to 0.98; the heavy one 2 to 3 s at 0.86 to 1.10. 0.13.8: `EngineHand::hold` by the
tachometer's class, `kEngineGearHoldScout` 1.5 s for scouts, 2.5 for the others (the mass cannot tell them: 9007 kg
for the scout, 6314 for the Fleetstar). Models 145 OK.
The user (2026-10-11), for later: at a light foot the automatic shifts up too late (the scout at about 2500 rpm at
a fifth of the pedal: `kEngineUpLight` 0.3 plus 0.6 of the pedal, of the way from idle to the red zone); the user's
own car (a nine-speed automatic behind a 2-litre diesel) shifts at about 1500 rpm at a light foot. Wanted: the point
up about as far along as the pedal is down (a fifth of the pedal, a fifth of the way), in steps if need be. Then the
point down has to follow: with the scout's wide steps (1.4 to 1.6) a shift up at 1500 rpm lands near 1000, and the
point down and `kShiftRoom` must leave that landing above it, or the shift up never comes (GearboxStep's room check).
The user: "it's a diesel so 1000 rpm is fine; peak torque is like 2k rpm". 0.15.0: the point up `idle + (floored - idle)
x feel`, not under `EngineHand::least`; the point down per gear, the gear below's point up over the step over
`kEngineDownRoom` 1.15, not under `kEngineDownIdle` 1.15 times idle (the landing clears it by the room check's 1.1 by
construction); the converter's lock-up from `kLockIdle` 1.3 times idle up to `kLockLight` 0.3 of the pedal, to half
the red zone floored. The test: the scout's gearbox at a fifth of the pedal shifts up at 1579 to 1639 rpm, none taken
back, locked after each.
The user: "a slider for it, 1 s to 3 s, per gearbox". 0.13.9: `[Gearbox:<key>] GearHold` (`kGearHoldRange` 1 to 3), the
tab's slider "Each gear going up" (`GearHoldSet`, as the map's temperature slider), the class's default without it.
The key: the gearbox's name in the game's files ("g_" and the rest, as the scout's pak names its four), looked for among the texts of
the vehicle from + 0x900 to + 0xC00 (the engine's is at + 0x968), the first that begins with "g_" and is a name
(`TruckGearboxName`); its place unknown till the user's log says it ("its name at vehicle + 0x..."), then it can be
read there alone. Without a name, by the gears' speeds ("gears 1.5 4 7 11 19 30 high 20"). Looked up for each truck
and when its gears change (another gearbox fitted in the garage). Truck 81, settings 36 (71 help lines) OK.

Part A, research and design note (no hook before the user's OK on the note):
- Where the game turns the pedal into torque and how the automatic decides a shift: the truck action's per-frame
  update, the helper 0xd717e0 (the plan has it: the wheel path, the power factor at TruckAction+0x38, the pedal at
  +0x44, the gear at +0x74, the engaged gear at +0x70), the readers of +0x38 and +0x44, the shift functions the
  manual transmission mod hooks (ShiftGear, SetPowerCoef, ShiftToAutoGear, its byte patterns in
  `C:\Games\Snowrunner-Manual-Transmission\DLL\memory.cpp`; its PowerCoef values 0.45 Low, 1.0 Low+, 0.2 Low-, 1.05
  automatic), the gearbox XML (AngVel and FuelModifier per gear, the gear tops as module 2 measured them), and
  where the drive torque is split between the driven wheels (for module 15).
- Which levers do the job with the least code: writing the power factor (+0x38) per frame from our torque model,
  shaping the pedal (+0x44) before the physics reads it, calling the game's own shift function from our automatic
  while the game's automatic is held off (as SMT does), a hook on the game's thread with Microsoft Detours (MIT,
  into deps with its notice; the pattern is SMT's) where a write from our thread would race.
- The design note in HANDOFF.md: the hook points, the model's formulas (the torque curve against the tach share,
  the cold factor from the oil temperature of module 11, the boost term from modules 8 and 11, the redline fade, the
  throttle curve and ramp), the settings, the per-truck pieces through module 7's sections, what stays the game's,
  the risks (co-op untested: the switch off there; a game update: the stamp check stands down; the manual
  transmission mod hooks the same functions: until module 14, detect SMT.asi or SMT.addon64 and stand down with a
  log line and a tab notice), and the checklist of the user's drive. The user says yes or changes it; then part B.

Part B, build: the model in `models.h` (no game memory in it, tests and replays as the other models), the hooks
and writes in a new `src\powertrain.h` behind the switch, the dials fed from the model (the tach shows the engine
speed that makes the torque, the coolant's heat and the boost's share come from it), the tab's Powertrain section
and the ini keys with help text, the probe line, the changelog (0.12.0), the plan file updated as built. Install
with the switch off; the user's drive decides the defaults and switches it on.

Effort max. A fresh session if the user can create one; else the closed session with the most room.

### 13. Automatic gearbox

The user's note (2026-10-08): "Auto gearbox more sane (torque converter simulation)". And later the same day: "good
for gamepads but worthless on a wheel": the automatic is for the gamepad players; the user drives a wheel with SMT's
manual gears, so module 14's path comes first, and the automatic's tuning goes by a gamepad's pedal (floored and
lifted by turns more than held part way).

Goal: the game's automatic (the A gears) as a truck's automatic drives: a torque converter between the engine and
the gearbox, and shifts that follow the engine speed and the load with hysteresis and never hunt. Behind the
`Powertrain` switch of module 12 (or its own switch, if the note says so); the manual gears, the high gear and the
low range stay the game's.

- The torque converter: below the lock-up the engine speed is free of the wheels. With the truck held, the pedal
  takes the engine to the converter's stall speed and no further; the converter multiplies the torque, most at
  the stall (about two times) and none from the coupling point; the needle shows the slip. At idle in drive the
  truck creeps. A lock-up clutch couples the engine to the wheels from a gear and a speed on, and the needle drops
  onto the wheel speed. Settings: the stall speed and the lock-up's gear and speed, per truck through module 7.
- The shifting: up at a share of the red zone that grows with the pedal, down with a band below it so a gear
  holds; a floored pedal kicks down, skipping gears where the engine speed allows; no shift while the wheels spin
  or the truck is in the air; on a long descent the gear holds for engine braking; a minimum time between shifts.
  Our automatic calls the game's own shift function while the game's automatic is held off, as part A of module 12
  found.
- Part A, research note in HANDOFF.md first (what the game's automatic does now, measured on a drive with the
  probe: where it hunts and where it over-revs; the hook points from module 12's note; the converter's formulas
  and the shift rule with its thresholds and timers); the user's OK, then the build.

Test: the models test with the converter (the stall speed held, the creep, the lock-up, a shift sequence on a climb
without hunting) and the recorded drives replayed; the user's drive on a road, a climb, a descent and in mud.
Changelog 0.13.0.

### 14. SMT integration

The user's note (2026-10-08): "SMT integration". Until now the plan had the powertrain stand down when the manual
transmission mod is loaded (module 12, the risks). This module makes the two work together.

What "integration" covers is the user's call, asked first, with the options laid out:
- The dials follow SMT: the gear and its letter, the clutch and the engine speed read from SMT's state, so the
  cluster is right with its H-shifter and clutch pedal. Module 2's deferred check of the gear letters with the
  wheel's shifter closes here, on a day the sim rig is connected.
- The powertrain with SMT: SMT picks the gear and owns the shift calls; module 12's torque model owns the torque
  and module 13's converter steps aside; the clutch pedal slips the engine against the wheels (a stall when it
  comes up too fast at too low an engine speed, a free rev with it down). The two mods must never both write the
  power factor or call a shift in one frame.
- How the state crosses over: SMT is the user's own fork (github.com/andizla/Snowrunner-Manual-Transmission,
  locally `C:\Games\Snowrunner-Manual-Transmission`), so the route is an exported state: a small versioned struct
  or function in its DLL (the gear, the clutch, its mode, whether it holds the game's automatic off) that this mod
  finds with GetModuleHandle and GetProcAddress and reads once a frame, and, if the powertrain needs it, a call
  back the other way so only one of the two writes the power factor or calls a shift. The change to SMT is made in
  its own repo on a branch, as a separate commit the user approves; this mod works on with an SMT build that has
  no export by standing the powertrain down, as module 12 does.

Research note in HANDOFF.md first (SMT's hooks and state, the order the two run in each frame, the chosen route),
the user's OK, then the build. Test: the user's drive on the rig with the shifter and the clutch, and with SMT off
to show nothing changed there. Changelog 0.14.0.

The user's decision (2026-10-09), on the clash of two mods hooking the same game functions: "we could just rebuild
SMT like features into our mod. or we simply dehook it on my version of SMT for compatibility. whichever makes more
sense for a user. also, installation wise, whatever is the least complicated." Chosen, for one file to install and
no clash to manage: this mod takes over the gearbox, the clutch and the engine itself, the manual transmission
mod's features rebuilt here (not copied: its game function patterns are the facts the work stands on, its own code
stays its own). SMT needs no change.
- Install: Gauges is a ReShade add-on, `Gauges.addon64` (0.11.3): ReShade, which draws the cluster anyway, loads it,
  so no ASI loader. It registers in DllMain and pins itself through ReShade's unload passes at start-up, as the
  user's SMT add-on build does; `Gauges.asi` is the same file for an ASI loader, and a mutex lets one copy run.
- Coexistence: the mod takes its hooks only after the first truck is found, minutes after a manual transmission mod
  hooked at start-up. A gearbox function whose first bytes are no longer the pattern (a jump put there) is held by
  another mod: the gearbox is left to it, the mod follows it as now (the state export where there is one), and the
  log and the tab say so. Its hooks are written by hand, a jump over whole instructions the patterns pin (no RIP
  relative ones among them), so no hooking library comes in.
- The order: (1) the add-on build, done in 0.11.3; (2) the game control layer: the gearbox functions found by
  pattern (shift to a gear, neutral, reverse, high, the game's automatic step, the automatic switched off with a
  shift, the power factor, the truck put in the seat), hooked, standing down when taken; with a probe of the power
  factor (does it scale the drive in every gear, at once or only at the next shift, as SMT's shift after setting it
  hints) to learn whether it can carry the engine's torque; (3) the manual for the wheel: DirectInput devices (the
  wheel, the H-shifter, the pedals), bindings on the tab and in the ini, H-pattern and sequential, the ranges by the
  power factor, the clutch pedal through ClutchStep, which decides the stalls (through the stall timer, vehicle +
  0x11C) and the slip; (4) the automatic (GearboxStep handed to the game) for pads; (5) the engine's torque
  (module 12 part A) through the power factor, or a hook into the game's input path found in a session on the
  user's PC if the factor cannot carry it. Steps 2 to 5 behind a `Gearbox` setting that is the game's own by
  default.
- The user's rig and the device merger (andizla/dimerge, the user's own, 2026-10-09 read): a SIMAGIC Alpha wheelbase
  (0483:0522), SIMSONN pedals (DDFD:6011) merged onto it as X->Y throttle, Y->Z brake, Z->Rz clutch, a GX100
  H-shifter (04B0:5750, buttons from 64, a pull collar layer from 80) and a PXN-CB1 button box, all handed to the
  game as one DirectInput device by a dinput8.dll proxy in Bin (HideMerged=1). For step 3: this mod reads the merged
  wheel through that proxy like SMT does, opened DISCL_NONEXCLUSIVE | DISCL_BACKGROUND (the proxy downgrades
  exclusive only for the modules it lists, smt.asi and smt.addon64), by GetDeviceState; the clutch is the merged
  wheel's Rz, the axis slot the game's wheel presets give the clutch (Y, Z, Rz are throttle, brake, clutch); a pedal
  may read full scale at rest, so each axis binding learns its two ends (rest and pressed) rather than assume a
  direction; the shifter's gates are buttons, a layer's gates other buttons. Gamepads through XInput, as the pad
  toggle already reads them: no fork of the manual transmission mod on GitHub reads XInput (the original and its
  forks read DirectInput through their input library, where a pad's triggers share one axis). Step 2's read-only half is in 0.11.3 (`src/game_code.h`): once a truck is found, the nine functions are looked
  for in the exe's code sections and logged (offset, more than once, or held behind a jump); 0.7 s over 100 MB here.

### 15. Differentials

The user's note (2026-10-08): "Front/rear Diff locks, centerdiff torque split locks". And later the same day:
"remember that trucks can have more than 4 axles. separate locking diffs etc will have to be expandable".

Any number of axles: nothing in this module may assume two axles or four driven wheels. The axles come from the
truck's wheels as read (up to `kMaxWheels`, 32, so 16 axles), grouped by where they sit along the chassis; the
locks, the splits, the keys, the lamps, the tab rows and the per-truck settings are lists with one entry per axle
or per pair of neighbouring axle groups, sized from the truck, never fixed fields. A 6x6 or 8x8 has a lock per axle
(or per tandem, if that is how the truck is built) and a split between its front and rear groups and within a
tandem; the lamps show the locks as a row the length of the truck's axles. The note in part A says how the game
groups them (tandems, a lifted axle, trailers left out) and what a 3, 4 and 5 axle truck gets.

Goal: in place of the game's single differential lock and all wheel drive switch, a front lock, a rear lock and a
centre differential with a torque split between the axles (for example 40 to 60 front to rear, per truck) and a
centre lock that makes it 50 to 50 rigid. Each lock on its own key and pad binding (in the notation of
`ToggleKey` and `PadToggle`), with a lamp each on the cluster (the ISO differential lock symbol with F, R and C).

This module stands or falls on the game: part A is a feasibility note before anything else.
- How the game splits the drive between the driven wheels: the drive types per wheel and their angular speeds that
  module 2 reads, the all wheel drive and lock flags, the per-frame update that module 12's note maps, and what the
  diff lock does to the wheels (equal speeds per axle, or across all driven wheels).
- Which lever gives a per-axle lock and a torque split: the wheels' drive torque written per frame, the lock
  applied per axle in a hook, or the drive types switched per axle. What it costs and what can go wrong (a truck
  that shakes or jumps, co-op).
- What a truck may lock: only what the truck has in the game (a truck without a diff lock gets none), or free for
  every truck. The user's call, asked in the note.

The user's OK on the note, then the build: the model in `models.h`, the writes or hooks behind a switch (off until
the user's drive), the keys, the lamps, the tab section, the per-truck split through module 7's sections, the probe
line. Test: the models test, and the user's drive in mud and on a cross-axle climb with each lock. Changelog
0.15.0.

### 16. Release

Build: the tach and speed scales by truck class from the truck's name (module 7 finds the name; here the class
defaults per name, if wanted), README complete, rollback notes, the repo on GitHub (private until the user's word).
A last pass over every outward text for the house rules. `MODULES.md` leaves the repo (`git rm --cached`, back
into `.gitignore`'s effect) before the public flip: it names other mods and local paths. `update.bat` (the work
branch brought in, built, tested and installed) and `map_names.bat` are the user's development tools: they name the
work branch and the Steam path, so they leave with it or are made general. The sound recordings
(module 10, cut from a clip the user supplied with its author's permission) are not under the GPL: SOUND_CREDITS.md
names the author and the link (the user supplies both) and states the terms (used with the author's permission),
and THIRD_PARTY_NOTICES.md says the recordings are excluded from the GPL licence of the code.
Release flow as the other repos: `CHANGELOG.md` dated, release notes and the Discord post written by hand (the
template is the note `release-doc-style.md` in `C:\Users\User\.claude\projects\C--m-d\memory`), the wording shown
to the user before any build; the zip, tag and GitHub release only on the user's word.

### 17. Naturally aspirated engines

The models take every engine for a turbo diesel: the boost dial, the flutter and blow-off sounds, and 35 percent of
the torque that comes only with boost (`kBoostTorque`, the cold and the boost's limit in `TorqueLift`). A naturally
aspirated engine has none of that: no boost dial (or one that stays at 0, hidden), no turbo sounds, all of its
torque at once with the pedal, and a curve of its own (torque rising with the engine speed to a later peak, less
low down than a turbo diesel's). Build: `BoostProfile` gains "none" (or a separate `Aspiration` setting), per truck
in `[Truck:<kind>]` as the scales are; the powertrain's boost term 1 for it; the cluster drops the boost dial; the
sounds stay quiet. Whether the game's files say which engines are turbocharged: the engine's record holds no such
flag (module 12's dump of a scout's record, + 0x58 to + 0x84); its UI text sometimes names "TurboDiesel", which is
a hint for a default at most. The user (2026-10-11): a setting per truck, and per engine if that can be done. It
can: the engine fitted is known by its name in the game's files (`EngineRecord::name`, "<truck>_engine_<n>"), so
a section of its own per engine (`[Engine:<name>]`, or the engine's file under 18's folder) overrides the truck's.
Tests: the curve, the torque with no boost term, the settings round trip, the engine's section over the truck's.
0.15.0, with the user's design ("the plan" of 2026-10-11): per engine, not per truck (the scales stay the truck's: an
engine file over the truck's scales would make the tab's scale edits seem to do nothing). `EngineProfile`,
`EngineCurveOf` (its own `TorqueCurve`, else `EngineCurveFromPeak`, else the class's), `EngineProfileRead`/`Write` in
gauges.cpp under `Gauges\engines`, loaded in `EngineLook` per engine name; the curve in force goes to the powertrain,
the clutch and the automatic (`EngineHand::curve`); `natural`: no `BoostStep`, the boost share 1 to the powertrain,
`GaugeState::natural` drops the boost dial. The light foot's floor `EngineHand::least` = `kLightPeak` 0.8 times the
curve's peak (`CurvePeak`) in rpm. Models 146, settings 40 (72 help lines) OK.
The user (2026-10-11), for the engine profiles: a peak torque rpm per engine, as most engines have their peak torque
figures published. The profile's curve then built around it (the peak at that speed over the red zone's, say a
plateau around it for a turbo diesel, a narrower peak for an atmospheric engine), in the engine's own file
(`Gauges\engines\<name>.ini`, beside 18's trucks and gearboxes), the class's curve where none is set.

### 18. Sharing truck and engine setups

What would be shared: a truck kind's section of Gauges.ini (the scales, the class, the boost profile, the torque
curve and aspiration once 17 has them). Ways, from least to most: (a) export and import as text: a button copies the
section to the clipboard, another reads one pasted in; nothing to run anywhere, shared on Discord or a forum. (b) A
public list of setups by truck kind, a file in a GitHub repository, read over HTTPS when the user presses a button
and offered for the trucks that have none of their own (never over the user's own without asking); contributions as
pull requests or through a form. (c) A database with uploads: a server, accounts, moderation and its upkeep, for
little more than (b) gives. The likely path: (a) first, (b) after. To settle with the user before (b): it is a
public repository (the house rule: nothing public without the user's word), and setups for modded trucks carry their
kinds, which name the mods (`<name>/<mod.io id>`): whether such entries may stand in a public list at all. The add-on
fetches only on the button, never on its own, and says in the log what it fetched.
The user (2026-10-11): the public list on GitHub is the way; each truck's setup a file of its own, in a folder of its
own in the game's directory (beside `Gauges.ini`, say `Gauges\trucks\`), so a file from the list drops in as it is.
The file name from the truck's kind: a modded truck's kind holds a stroke (`<name>/<mod.io id>`), which no file name
may, so it is written another way (`<name>@<id>.ini`). Gauges.ini's `[Truck:<kind>]` sections move into the folder on
the first start of the version that has it, and are read from there after. Later, on the user's word.
0.14.0 (the user: "begin; keep the public GitHub a to-do for later"): `SetupFile(kind, key)` under `Gauges\trucks` and
`Gauges\gearboxes`, one section each (`[Truck]` with `Kind=`, `[Gearbox]` with `Name=`), `SetupFileName` (a stroke as
"@", what no file's name holds as "_"); `ReadTruckIni`, `WriteTruckIni`, `DropTruckIni` (the file goes when nothing else
is in it), `GearHoldRead`, `GearHoldWrite` on the files; `SetupsMove` in `ReadIni` (each start and each reload) moves
`[Truck:...]` and `[Gearbox:...]` out of Gauges.ini, a key the file holds already kept. Settings 39 OK (three new).

### 19. Winch

Wanted: the winch heats as it pulls and cools at rest; too hot, it stops (or the line snaps). First a feasibility note
from the game's memory and code: where the game keeps whether the winch is attached and pulling, what it pulls with
(the line's tension or the load, the winch's strength from the truck's files), and the game's own function that
lets the line go (as the player does). The model then: heat from the work done (tension times the line's speed) and
from time under load, cooling with the air (module 11's outside temperature); a gauge or a lamp for it; at the limit
the mod calls the game's release, or cuts the winch's pull until it has cooled. Which of the two (snap or stop) is the
user's to choose, perhaps as a setting. No write and no hook before the user's OK on the note.

