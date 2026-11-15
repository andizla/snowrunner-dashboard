# Next Gen Tuning

A tool for tuning the Next Gen mods while the game runs. This is version 1.0.0.

## Install

`NextGenTuning.asi` comes in the Next Gen Gameplay zip. Copy it into the game's `Sources\Bin` folder, next to `SnowRunner.exe`, beside the mods' asi files. It needs nothing else: the ASI loader of the mods loads it. To remove it, delete `NextGenTuning.asi`, `NextGenTuning.ini` and `NextGenTuning.log` from that folder.

## What it does

`NextGenTuning.asi` adds a tab "Tuning" to the NEXT GEN menu. Each Next Gen mod that has figures to tune publishes them in a block of named shared memory, and the tab draws what it finds: sliders, switches, buttons and lines to read, under the mod's name and the mod's own group headings, with one Reset a group. A moved slider is asked of the mod, which holds it to its range, puts it in force at its next pass and saves it in its own ini. The tool never writes a mod's memory or ini, hooks nothing of the game's and reads none of the game's memory.

A mod that is not in the game, or one that publishes no block, has no group on the tab.

## The key

F1 opens the menu on the Tuning tab and closes it. With the menu open on another tab, F1 goes to the Tuning tab. The dials of the Gameplay mod stay drawn under the Tuning tab, where both are of this version or later. Click the key's name on the tab and press another key to change it. It is saved in `NextGenTuning.ini` beside the game's exe (`[Tuning] Key=`, a virtual key code). The first start that finds no `NextGenTuning.ini` writes it with the defaults and a comment over each key, so the key can be seen and changed by hand. A file that is there is never written over. While the menu is open the game gets no keys and no mouse, as with every tab: drive with a pad or a wheel.

## The block

`src\ng_tune.h` is the block's layout and both sides of it. The same file goes into every mod that publishes. A mod lists its figures (`NgTuneItem`), opens its block (`NgTuneOpen`), and each pass writes the values in force (`NgTuneBegin`, `NgTuneValue`, `NgTuneEnd`) and takes what the tool asked (`NgTuneTake`). The layout is only ever added to at its end, and a reader steps by the sizes the block gives, so a newer tool beside an older mod and an older tool beside a newer mod both work.

## Build

Windows with Visual Studio 2022 (C++ desktop tools) and python. The source is in the `tuning` folder of the Next Gen Gameplay repo.

```
tuning\build.bat
tuning\test.bat
```

`build.bat` makes `tuning\out\NextGenTuning.asi` and `tuning\out\test\tune_test.exe`. `test.bat` runs the block's test and the check that no text drawn for the player carries an apostrophe. `tuning\src` holds the tool's own files. `ng_overlay.cpp`, `ng_overlay.h`, `ng_dx11.cpp`, `ng_dx11.h`, `ng_reshade.cpp` and `ng_reshade.h` and `ng_tune.h` are the Gameplay repo's own in its `src`, compiled as they are. What it is built with is in the Gameplay repo's `deps`, each with its licence (`deps\README.md`).

## Licence

GPLv3, as Next Gen Gameplay (`LICENSE` in the repo's root).

## Log

`NextGenTuning.log` beside the game's exe, made new at each start: the blocks found, a block that gives no group and why, the key. And each change made on the tab: "tuning: changed: ..." with the mod, the figure, its new value and the value before. The line comes when the figure has stood still for a second, so a drag gives one line. A Reset gives one line for its block. A button gives a line a press. Figures the mod loads by itself give none.
