# The winch: feasibility of three changes (2026-10-14)

Three wishes on the board: the winch overheats after long use, it stays attached when the engine stalls, and it can reel out as a crane does. This note says what the game has for each and what each would take. Nothing is built. It comes from reading the game's files and its script bundle (`initial.pak`, `[media]/classes/winches/*.xml`, `[ssl_cache]/initial_release.sslbundle`), nothing changed and no game started.

## What the game has

- A winch is data: `winches_default.xml`, `winches_scouts.xml`, `winches_medium_trucks.xml`, `winches_heavy_trucks.xml` and the DLC ones give each winch `Length`, `StrengthMult` and `IsEngineIgnitionRequired`. A winch with `IsEngineIgnitionRequired="false"` is an "offline" one (the game's word, for example `w_actaeon_offline`): it works with the engine off. There is no heat, wear or duty time in the data.
- The winch's rules are a script (names from the bundle): `HandleWinchAction`, `OnStartPullWinch`, `OnStartPushWinch`, `OnStopPushWinch`, `SetWinchPush`, `StartPushWinch`, `isWinchPushing`, `IsWinchCrane`, `IsWinchAutonomus`, `CheckTruckWinchAutonomus`, and for the engine `RemoveWinchOnStall`, `ToggleWinchEngine`, `isWinchEngineTurnOn`, `TryEnableWinchTruckEngine`. The script runs in the game's script engine; its code is bytecode in the bundle, not in the exe.
- The exe carries the native side under `combine::TRUCK_WINCH_DESC` (seen among the truck's script functions while reading for the engine sound), but none of the names above: the script reaches the exe by index.

## The three, one by one

### 1. Overheating after sustained use

- The game has nothing to build on: no heat. The mod would keep the winch's heat itself, as it does the coolant's: up while the winch pulls, more under load, down while it rests; a lamp or a small dial.
- What the mod needs from the game: whether the winch pulls now (and ideally how hard). The script has a pull state and a push state (`isWinchPushing`, the pull status events), so the native winch object holds them somewhere. Finding it is a probe run as for the engine sound: the user winches, the reader looks for what flips with it.
- What "too hot" would do: the gentle way is the mod's lamp and log only. To make the winch weaker or stop, the mod would write into the winch's state in memory: lower its strength (the `StrengthMult` the truck's winch carries) or end the pull. Both are writes of a number, no code change, once the places are found.
- Feasible: yes for the display, likely for a weaker winch. Steps: a probe run with the user winching, then the model, then the effect.

### 2. Staying attached when the engine stalls

- The game lets go of the winch on a stall in its script, `RemoveWinchOnStall`. An offline winch (`IsEngineIgnitionRequired` false) keeps working without the engine, which the script checks (`IsWinchAutonomus`).
- Route a: make the driven truck's winch count as offline while it is attached, by writing that flag in the winch's description in memory. Side effect: it would also work with the engine switched off, not only through a stall. The mod could set the flag only while the engine is on or stalling and put it back after, if the game reads the flag at that moment and not once at the start.
- Route b: change the script's rule. That is an edit of the game's script bundle, a game file, which this mod does not do; or a patch in the script engine, which is far more than this wish is worth.
- Feasible: probably, by route a. Steps: find the flag in memory (a reading run: two trucks, an offline winch and a normal one, compared), then a trial with the user stalling an engine on the winch.

### 3. Reeling out as the crane does

- Pushing a winch out exists in the script (`SetWinchPush`, `StartPushWinch`, `isWinchPushing`), so the game knows how to pay the line out. Whether a normal truck's winch may push is decided in the script's input handling (`HandleWinchModeAction`, `IsWinchCrane`), where the crane gets the action and the winch does not.
- Routes: call the native behind `SetWinchPush` from the mod on a key (the mod already calls two game functions, the gear shift among them), which means finding that native by its index in the script's tables first; or make the winch count as a crane's, which brings the crane's whole mode with it and is not wanted.
- Feasible: unsure. Finding the native is the open part, and only reading the script engine's tables will tell. The most work of the three.

## What I would do

1. Item 2 first: a reading run to find the offline flag in a winch's description, then a trial. Small, and most likely to work.
2. Item 1 next, the display first: a probe run with the user winching for the pull state, then the heat model and a lamp; a weaker winch when hot as a later step.
3. Item 3 last, and only after the native behind `SetWinchPush` is found by reading; if it is not found, it stays a wish.

Each step's game run needs the user at the PC: the reading runs need someone to winch.

## Open questions for the user

- The order above: right, or another?
- Item 2's side effect: an attached winch would work with the engine off for as long as the mod holds the flag. Acceptable, or only through a stall?
- Item 1: is a lamp enough, or should a hot winch also pull weaker?
