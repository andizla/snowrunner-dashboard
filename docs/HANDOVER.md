# Handover from the cloud session (2026-10-12)

The cloud session that built Gauges from 0.11 to 0.15.4 ends here, on the user's word. A session on the PC takes the work over. This note is what that session needs and only the cloud session knew. `MODULES.md` (the board) holds the history and the reasons behind each version; `CHANGELOG.md` what each version changed.

## Where things stand

- The branch `claude/vibrant-darwin-anbhqw` and `main` are both at the commit that brought this note. Gauges is at 0.15.4.
- In the game's `Sources\Bin` on the PC: Gauges 0.15.3 (the overseer's build of module 20) until `update.bat` runs again, and TirePressure.asi 1.2.0 (the overseer's, not driven yet).
- For the tester, in `C:\Games\tester`: Gauges.addon64 0.15.4 (MSVC, 4B033E44), TirePressure.asi 1.2.0 and `fallback_1.1.1\TirePressure.asi`. The user picks which tire mod goes out.
- The user's own drive on 0.15.3 (lrd_90, a wheel, the manual transmission mod loaded): clean. The hunt guard fired 7 times: 3 for a gear that could not hold, 4 when the truck lost most of its speed in a second or two (harmless). The other quick back and forth all came after a lift and a fresh floor, which kicks down by the rule.

## The branch and update.bat

`update.bat` on the PC: switches to `main`, fetches, and stops if `main` holds commits the branch lacks (it names them and pushes `main`, so the cloud session could take them in). Otherwise it fast-forwards `main` to `origin/claude/vibrant-darwin-anbhqw` with `--autostash` (a file the branch also changed is left as the branch has it, the local edit kept in git's stash), pushes `main`, runs `build.bat` into `out\build.log` and `test.bat` into `out\test.log`, and installs `out\Gauges.addon64` into the game's `Sources\Bin` with the one it replaces in `out\rollback`. It refuses to install while the game runs. It runs from a copy in `%TEMP%`, so a change to it takes effect from the next run.

With the cloud session gone nobody moves the branch. Either push each commit to it as well (`git push origin main:claude/vibrant-darwin-anbhqw`), or set `BRANCH=main` in `update.bat` in one commit: from the next run it brings `main` up to `origin/main`, and commits made here are pushed before it runs.

The git flow agreed between the sessions (in the board's rules): before a commit here, bring `main` up first, commit on it, push, and tell the user. One writer at a time in this repo; the overseer takes it for the PC's sessions.

## Versions and house rules

- A version is `GAUGES_VERSION` in `src\gauges.h`, the README's "This is version" line and a `CHANGELOG.md` entry, newest on top. The next free one is 0.15.5.
- Commits: one line, `area: what changed; what changed`, author `thereal10 <96357947+andizla@users.noreply.github.com>`, no Co-Authored-By and no "Generated with" line.
- Never the em dash, and never a spaced hyphen as a separator, in prose or comments.
- The repo stays private; no tag, zip, GitHub release or public flip without the user's word. `MODULES.md` leaves the repo (`git rm --cached`) before any public flip.
- Never touch `winmm.dll`, `cream_api.ini`, `steam_api64.dll` or `video.cfg`; never run the Expeditions or RoadCraft exes.
- Mod names in files: the earlier rule kept third-party mods' names out of tracked files; the user said on 2026-10-12 that it is "completely irrelevant".

## Building and testing

On the PC: `build.bat` and `test.bat` (MSVC). In the cloud: MinGW-w64 and Wine, with the commands and shims in `tools\cloud\README.md`. At 0.15.4: models 149 OK, truck OK, settings 42 OK plus three fails that are Wine's alone (the ini given "nan" or "inf"; MSVC passes them).

For a test of a change against the behaviour before it, the cloud session built the models test a second time against a copy of `src` with the change undone (for the hunt guard, `kHuntKeep` set to 0: 8 shifts up in 40 s on the test's climb, 3 with the guard).

## Waiting on

- The tester (plays on the keyboard; their two logs carried the Steam exe's stamp, so their build is not known for sure). They get Gauges 0.15.4 and a tire mod. Expected back: `Gauges.log` and `Gauges\logs\*`, `TirePressure.log`, a zip of `Gauges.ini`, the `Gauges` folder and `TirePressure.ini`, and the times of anything odd. Look for:
  - the `map:` lines: whether a save was found, in which folder, and whether it was a `.dat`;
  - "a shift up taken back" lines, and `tools\analysis\hunt.py` for any hunting left;
  - kickdowns right after a fresh press of W (`kick.py`, `ctx.py`);
  - the truck lines' "the automatic goes by the ... class, its weight's";
  - in the tire log, "outside temperature: ... C from another mod's block".
  
  Their earlier logs are assets of the private repo's release `d110` (download through the API with `Accept: application/octet-stream`; the browser link gives 404 on a private repo).
- The overseer's queued tire run: whether TirePressure 1.2.0 reads Gauges' block.

## Not built yet

- A kickdown only from part throttle (proposed, waiting on the tester's logs). Now a push past `kKickdown` after `kKickArm` short of it kicks down, so on a keyboard every press after a lift does. Change: the pedal before the push has to have been above a part-throttle floor (0.2, say), not off; a press from foot-off drives on the floored points, which still shift down where the truck is slow. Test: a press from 0 to 1 in fifth at speed does not kick down; a push from 0.5 to 1 does.
- An input setting (wheel, pad, keyboard): the user's note on the board, for later.
- The engine sound by the mod's rpm: pinned on the board (route 1, a probe first).
- A public setup list on GitHub; winch heat (module 19); the manual transmission mod's features rebuilt here (module 14); the signatures confirmed on a build other than Steam's.

## Things to know

- In a `gearbox: N to M` line, "the wheels at X of the gear's top" goes by the speed over the ground (the model's `by`), "at N rpm on the gearbox's input" by the wheels' smoothed spin. With wheelspin they disagree.
- "the game's own automatic picked gear N ... the mod puts its gear M back" is the game's pick undone, not the mod's shift. They come 0 to 3 s after a takeover (the game's A flag up again, likely a pad's gear selector).
- The hunt guard (`kHuntWindow` 6 s, `kHuntKeep` 8 s doubling to `kHuntKeepMost` 30 s) lets go at a lift of the foot, a stop, a kickdown, a try that holds and a takeover; kept, the limiter is blocked but halfway into the red zone. The class by weight (`ClassByMass`): a scout up to 6 t, a truck under 16 t, a heavy truck from 16 t; it counts only where a truck has no scales of its own. A scout over 6 t needs scales of its own.
- A start's log is kept as `Gauges\logs\Gauges <date> <time>.log`: the time from its first line, the date from its last write, a day back for a drive past midnight. Nothing prunes the folder.
- The shared block (`src\state.h`, version 1, 64 bytes) only grows at its end.
- The signature harvest (`Gauges.signatures.txt`) runs on the Steam build with `Probe=1` when `kHarvestVersion` changes.
- The user's manual transmission fork carries the state export on its branch `claude/vibrant-darwin-anbhqw`, not merged into the fork's main (the user's OK owed).

## Uploaded from the cloud server

- `tools\cloud\`: the MinGW shims, the update.bat test harness, the save and map fixtures and the probe programs (`tools\cloud\README.md`).
- `tools\analysis\`: the log analysis scripts (`tools\analysis\README.md`). `tools\log2drive.py` was already in the repo.

Left on the server, and why:

- The user's and the tester's logs (8 to 30 MB each): the user has them, and the tester's are on the release `d110`.
- A modded truck's game file (`d110.pak`): another author's content.
- Clones of four public manual transmission repos, with no local commits.
- Debug copies of the tests (`mt_*.cpp`, `tt_dbg.cpp`, 100 to 160 KB each), a copy of `src` with the hunt guard undone, replay and test outputs, preview pictures, the sounds as heard, the Wine prefix and PowerShell: copies of tracked files or made again from them.
- Builds: `.exe`, `.o`, `.addon64`, `.asi`.
- The home folder outside the repo was not looked through: the session's permission check stopped it.
