# The engine sound by the mod's engine speed: feasibility (2026-10-13)

The wish: the game's engine sound should follow the engine speed the mod works out (the tachometer's), so the truck sounds as the needle shows. This note says what is known, what the probe ruled out, which routes are left and what each costs. Nothing here is built.

## What is known

- The game holds no engine speed. Two helpers work one out when asked, from the pedal, a load factor and the damage:
  - `exe + 0xd79170 (vehicle)`: the value the in-cab tachometers show, 0 to 1 (0.19 at idle, 0.69 floored). `TruckGameRevs` in `src\models.h` is the same rule, and the game burns fuel by it.
  - `exe + 0xd717e0 (vehicle, useWheels, lowGearFlag)`: the helper under it, with a second path by the driven wheels' speed over the gear's top.
- Who calls them (the Steam build of 22 July 2026, from the decrypted image):
  - `0xd79170`: three places, in the functions at `0xb834a0`, `0xc7e7b0` and `0xc7ecf0`.
  - `0xd717e0`: eleven places, among them the frame's driving branch (`0xa56970`, `0xa5f730`), the fuel burn (`0xd67149`, `0xd67c70`), `0xd79170` itself, and `0x911f00`, `0xa5a440`, `0xb22700`, `0xc2c871` and `0xc75867` (twice).
- The sound logic is a script, `truck_effects_processor`, in the game's script bundle, not in the exe: its variable names (`engineTension`, `accelRevving`, `pedalAccelerate`, `velocityFactor`, `torqueFactor`, `gear`) and sound names (`ENGINE_REV_IN`, `ENGINE_TURBO` and so on) are not in the exe's image. The exe hands the script its inputs each frame.
- The truck control (`exe + 0x2a8eb78`) points at three objects of one small class (`exe + 0x227d028`, made at `0xdfcf76` and `0xdfde44`) at `+ 0x70`, `+ 0x78` and `+ 0x80`. They are handles, not the sound's numbers.

## What the probe ruled out

`tools\revscan.cpp`, three runs on the user's play (reports in `out\probe`, summary in `HANDOFF.md`). With the engine revved in neutral at a standstill, no float followed the pedal in: the first 0x2000 bytes of the vehicle, the drive train, the first 0x800 of the truck control, the first 0x1000 of each of the three sound objects, or 0x100 bytes behind each of their first 64 pointers. The pedal itself and the torque factor did, as they must. `vehicle + 0x744` followed loosely and slowly (0.058 to 0.109, 0.8 s behind): not revs.

So there is no stored number to write. The engine speed the sound hears is worked out on the way to the script, most likely by one of the callers above, or inside the script from the pedal and the speed.

## The routes left

| Route | What it takes | What it changes | Risk |
|---|---|---|---|
| A. Replace the helper's answer for the sound's caller | Find which caller feeds the script (read the three callers of `0xd79170` and the five unnamed callers of `0xd717e0` in a disassembler). Then a hook on the helper that returns the mod's engine speed, as a share of its scale, when called from that place | The sound follows the needle, if the script's pitch goes by that input | The first hook in this mod (it has only called game functions so far). The same helper feeds the fuel burn and the in-cab needle: the hook must tell the callers apart, or those change too. A game update moves the addresses: the signature search the mod already has would have to cover it |
| B. Feed the script's inputs | Find where the exe hands `engineTension` and the rest to the script, and write the mod's values there | The most direct, if the script takes them as plain numbers | The names are not in the exe, so the binding goes by index or hash: finding it is the hard part, with no promise it is found |
| C. A sound of the mod's own | Mute or leave the game's engine sound and play an engine loop through the mod's XAudio2 path, its pitch by the mod's engine speed | Full control, no hook | Needs engine recordings per kind of engine (again someone else's sound, or the user's), and the game's own sound either stays under it or has to be muted, which is route A's problem again. A lot of work to sound better than the game does |
| D. Leave it | Nothing | Nothing | None |

## What I would do

Route A, in two steps, each stopping for the user's word:

1. Reading only: disassemble the eight candidate callers and say which one feeds the sound, what it passes on and whether a changed answer there would move the pitch. No game start, no change to the mod. About one session.
2. Only if step 1 names the place: the hook, off by default behind a setting, tried in the game with the user listening.

If step 1 finds that the script works the pitch out itself from the pedal and the speed, route A is dead and the choice is C or D.

## Open questions for the user

- Step 1 of route A: go, or leave the engine sound (D)?
- A hook in the mod at all: so far it only reads memory, writes a few numbers and calls two game functions. A hook is a step further, in what it risks on a game update.
