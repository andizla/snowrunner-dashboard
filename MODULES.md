# SnowRunner Gauges: module board

The mod is built one module at a time, each in its own Claude session. The overseer session (the gauge design
session that wrote `docs\plan\GAUGES_PLAN.md`) keeps this board, writes each module's brief, checks the results
and hands out the next one. The user creates the sessions. Agents: none, or at most one or two at a time.

A module session: reads this file and `HANDOFF.md`, builds its module, tests it as its brief says, then writes its
results into `HANDOFF.md` (the running state of the mod) and sets its row below to done with a one-line result.
Questions for the overseer go into `HANDOFF.md` under "Questions"; the overseer reads it.

## Board

| # | Module | Effort | Status | Session | Result |
|---|---|---|---|---|---|
| 1 | Foundation: repo, build, loader, overlay, settings | high | done (the user confirmed the cluster and the tire panel in the game, 2026-10-06) | Gauges M1 (created 2026-10-05) | Gauges.asi 0.1.0 (0A9C3341) built clean, offline tests OK, installed; in the game both mods load, the font bakes and the cluster is drawn with demo values (logs of two starts by the user). Not yet shown: an overlay picture of the cluster, a tab edit saved in the game, the tire panel opening beside it (HANDOFF, "The game run") |
| 2 | Live data: truck reader, probe log, verification run | max | done (2026-10-06; checked in the game by two drives of the user; the H-shifter check of the gear letters is deferred: the wheel is a sim rig in another room, so it waits for a day the rig is connected, in whichever module is current then) | Module 2: Live data (started in the design folder, worked here) | Gauges.asi 0.2.0 (AC3AE066, commits 81a7c0e and eeaa48c) installed: the cluster shows the truck's speed, gear letters, fuel and six lamps and is on screen only while driving (off while loading, in the pause menu, on the map and in the garage, by the logs of the drives). The plan's first group confirmed against the HUD and the gearbox XML; start timer, stall counter and the automatic's power factor corrected in the plan. Chores done: licence, notices, changelog, preview build, wording, a saved tab edit. Open: the gear letters with a wheel's shifter, and the run protocol notice came late (HANDOFF, "Still to see" and "For the overseer") |
| 3 | Tachometer and fuel range models | high | done by the log of the user's drive of 2026-10-07 (A1 to A4, H, L, N, both reverses, start, stall, the range from 507 m, the burn rule in the game) and by the user's word of 2026-10-07: the needle is believable, L+ and L- work, the range held on longer stretches. The user finds the rpm high for a diesel and leaves that for another mod, if at all. Module 2's open item is closed too: the user confirms the cluster stays off where it should (loading screen, pause menu, map). Still open: the wheel's shifter, deferred (HANDOFF, "The user's drive of 2026-10-07") | Module 3: Tachometer and fuel range (created 2026-10-06) | Gauges.asi 0.3.0 (E30EC128, commit ace0bf8) installed: the engine speed from the driven wheels, the gear and the pedal (`src\models.h`), the range from litres per km over the last 3 km, shown from 500 m. Offline tests OK (28 new checks) and both of module 2's drives replayed through the models: no jump, range 5 to 8 km on the Defender. The game's burn rule confirmed in l/min from the logs. Not done: the drive through every gear (this session started no game); Probe=1 is left on for it. Three questions in HANDOFF |
| 4 | Tilt lamp | medium | done (2026-10-07): the drive of 09:37 shows the lean to both sides, amber and red at their angles and red on a steep descent, no blinking; the side was turned after the user saw it mirrored (b5ea083), and the user confirmed the turned build by eye ("Yes, tilt is correct") | Module 4: Tilt lamp preparation (created 2026-10-06) | Gauges.asi 0.4.0 (D6796A33, commit 05ea2c1) installed: the tilt lamp green, amber from 15 degrees to a side, red from 25 or from 35 up or down a slope, its truck leaning with the real roll; smoothed over 0.3 s with a 2 degree band; three settings on the tab and in the ini; a fourth probe line. Offline tests OK (models 39, truck 29, settings 16, preview). Pitch sign confirmed from the recorded drives; the roll's side is taken from the axis order and waits for the drive (HANDOFF, "The drive still owed") |
| 5 | Coolant temperature (display) and cold lamp | high | done (2026-10-07: the user drove 0.5.0 and found it good; 0.5.1 "a truck's memory by its build" installed AE24F274 and confirmed by the user's A, B, A swap; water cooling researched, not built, a proposal: the ford scan goes on module 6's drive) | Module 5: Coolant temperature display (created 2026-10-06) | Gauges.asi 0.5.0 (044BFEEC, commit 5b85b37) installed: the temperature dial and the cold lamp work from a model in `src\models.h` (the plan's heat, thermostat, airflow and fan; per truck, a recovery keeps the temperature); `WarmUpMinutes` 3, `OverheatMinutes` 1.5, `CoolDownMinutes` 15 and `AmbientC` 15 on the tab and in the ini; a fifth probe line. Offline tests OK (models 57, settings 19, truck 33, preview), the change read by one C++ reviewer, and both of module 2's drives replayed: 85 C after 2.7 and 3.0 minutes of driving, hottest 105 C, never in the red zone; into the red zone only floored below 12 km/h (1.5 to 5.6 minutes from 90 C). In the game: the user's drive of 00:08 to 00:27 by eye, and a setting saved from the tab's new section (the log). Four questions in HANDOFF; the first is open and matters before module 6: the low range floored at its own speed overheats after three and a half minutes |
| 6 | Engine damage write: verification, overheating, cold abuse | max | done (closed by the overseer 2026-10-07 10:40: the user's drive log verified the write, the repair and the save; 0.6.1 adds the water term from the scan, the game's own clock for the models, Damage on by default). Open for the user's next play, not blocking: did the function menu's engine number follow "Add 1", and one hit on something after an "Add 10" (the user, 2026-10-07: "Will test both next time") | Module 6 Engine damage write (created 2026-10-07) | Gauges.asi 0.6.0 (ECF11786, commits cec366d and c2a8887) installed: with `Damage` on (off by default) an engine in the red zone and a cold engine under a heavy pedal take the game's own engine damage (`src\models.h`), written one short of the engine's capacity at most, with the effect and the torque factor by the game's rule from the engine's record, which is found by reading the game's own lookup (`src\truck.h`); the damage lamp blinks while damage is added; `AirFullKmh` 30 as a setting (the low range at its own speed settles at 115 C and costs nothing); the switch and two rates on the tab and in the ini, two test buttons behind the probe switch, a sixth probe line. Offline tests OK (settings 21, truck 46, models 68, preview); the change read by one C++ reviewer (no way to a crash or a stray write found; its eight points taken in the second commit); the recorded drives replayed: never in the red zone, 3 to 4 points of 150 per cold start. Two unattended starts of the game (reads only): the build loads, and the record is found in the real game ("lrd_110_engine_2 takes 150", the block's own 150). The user's drive (the log in `out\user_drive_0937`): thirteen test writes went in and read back as written, up to the cap of 149 of 150 with the effect and torque by the rule; they stood until a repair, which cleared them; the game's own damage added up on the same block, and the save kept it. After the drive, 0.6.1 (EBDF6A8F, commit 5296ceb) installed: `Damage` on by default; water cools the engine (the depth at the wheels, wheel + 0x17C, confirmed by an unattended scan of the truck parked in water: one water level to 3 mm over four wheels); the models wait for the game's own clock (vehicle + 0x114). Offline tests OK (settings 21, truck 48, models 73, preview). Not seen in the game: a game damage event on a written number, and the function menu after a write (HANDOFF has the one line for the user) |
| 7 | Per-truck setups: the truck's identity, scales saved and restored per truck | high | built, committed and installed (2026-10-07 12:15); NOT yet seen with a truck in the game: the user's drive is owed and is the first game check (the checklist in HANDOFF) | the module 5 session, reused on the user's word (remote, no new session possible) | Gauges.asi 0.7.0 (98251685, commit c268bc6) installed: each kind of truck keeps its own five scales (`TachClass`, `IdleRpm`, `RedlineRpm`, `MaxRpm`, `SpeedMax`) in a `[Truck:<kind>]` section of `Gauges.ini`, written when one of them is changed on the tab while such a truck is driven; every other truck has the plain ones of `[Gauges]`, and an ini without sections behaves as before. A truck goes by its kind's name, which the game holds at model + 0x588 ("lrd_110/1914924", the save's `type`; found by two scans of the running game), and by its build where no name is read. The game's id for the one truck (vehicle + 0x48) was found too and is only logged: the save keeps none for trucks in storage, so it would not last through a swap. The tab names the truck and has the button "Back to the plain scales"; the coolant and the range go by the kind. Offline tests OK (settings 32, truck 58, models 73, preview with three tab pictures); read by one C++ reviewer, nothing serious, its points taken. In the game: six unattended boots after the install never left the main menu, with 0.6.1 back in the folder as well, so they say nothing about the build (HSR's script; HSR told). |
| 8 | Boost model and gauge | high | built, committed (bcee527) and installed (2026-10-07 14:44, F1AB83EE); the user's drive owed | the module 3 session, reused on the user's word (remote) | 0.8.0 (F1AB83EE): the boost dial live from the pedal and the tach needle's share (`BoostStep` in `src\models.h`), `BoostMax` by class (1.8 / 1.0 / 2.0 bar) and as the sixth per-truck key, blow-offs and flutters told on quick lifts and logged with the probe on, a probe line. Offline tests OK (8 new checks), the two recorded drives replayed: 1.2 to 1.4 s from a floored standstill to nine tenths of the boost. Owed: the user's drive (checklist and three questions in HANDOFF) |
| 9 | Cluster graphics: dials, needles, lamps and icons at real-cluster quality | high | done but for the user's eye in the game: approved from the previews ("install them"), 0.9.0 installed 2026-10-07 19:22 (EE4F2EE6, commits 1b255bb and e5ecdd0) | the module 3 session (also module 8) | The drawing header rewritten with curves, a lit bezel, shaded needles and every lamp symbol redrawn; close-up previews at 4x; the draw time logged with the probe. 0.21 ms a frame at 2160p in the open style, 0.35 ms in the solid one (the preview's timing). Owed: the user's look at it in the game and the probe's draw time there (HANDOFF) |
| 10 | Sounds: XAudio2, flutter and blow-off clips, credits | high | done for now (the user, 2026-10-08, after playing it in the game: the playback is perfect and the recordings are good; the module goes on later with more sounds). Built with recordings and installed 2026-10-07 22:17 (BCA05B9C, commits 030f1c7 and 92e8d18): the user found the made sounds unlike a turbo and supplied a clip (permission from its author, by the user's word), now nine flutter recordings in three strengths. Still owed before any release: the clip author's name and link for SOUND_CREDITS.md | the module 4 session, reused on the user's word (remote) | 0.10.0 (E1C05E2A): a blow-off after a full lift under boost and a flutter after a smaller one through XAudio2 (`src\sound.h`, after the tire mod's), louder with more boost, pitch within 6 percent, at most one per 0.8 s; SoundVolume and SoundsOnly on the tab and in the ini, a button to hear each. Both sounds are made by code: no clip was downloaded (one poor CC0 hit on freesound, and its files need a signed-in account); a WAV in `sounds\` or `assets\` takes a sound's place. Offline tests OK, XAudio2 played both here (HANDOFF, the top section) |
| 11 | Engine models: turbo spool profiles, water and oil temperatures, heat from the engine speed, the outside temperature by map | high | waiting | | |
| 12 | Powertrain: throttle, engine speed and a torque curve as one model with the dials | max | waiting (research and design note first, the user's OK before the first hook) | | |
| 13 | Automatic gearbox: a torque converter and sane shifting | max | waiting (after 12; its own research note and the user's OK first) | | |
| 14 | SMT integration: the powertrain and the dials with the manual transmission mod | high | waiting (after 12; what "integration" covers is the user's call, see the brief) | | |
| 15 | Differentials: front and rear locks, the centre differential's torque split and its lock | max | waiting (a feasibility note first, the user's OK before any hook) | | |
| 16 | Release: polish, README, GitHub | medium | waiting | | |

Effort is the session's effort level, set by the user when creating the session (the overseer reminds them).

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

The user's note (2026-10-08): "Auto gearbox more sane (torque converter simulation)".

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

### 15. Differentials

The user's note (2026-10-08): "Front/rear Diff locks, centerdiff torque split locks".

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
into `.gitignore`'s effect) before the public flip: it names other mods and local paths. The sound recordings
(module 10, cut from a clip the user supplied with its author's permission) are not under the GPL: SOUND_CREDITS.md
names the author and the link (the user supplies both) and states the terms (used with the author's permission),
and THIRD_PARTY_NOTICES.md says the recordings are excluded from the GPL licence of the code.
Release flow as the other repos: `CHANGELOG.md` dated, release notes and the Discord post written by hand (the
template is the note `release-doc-style.md` in `C:\Users\User\.claude\projects\C--m-d\memory`), the wording shown
to the user before any build; the zip, tag and GitHub release only on the user's word.
