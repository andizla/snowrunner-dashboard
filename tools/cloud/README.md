# Cloud build and test kit

The cloud sessions built and tested Gauges on Linux with MinGW-w64 and ran the results under Wine. The add-on that goes into the game is always the MSVC build from `build.bat` on the PC; the MinGW build is a check. This folder holds what that took.

Setup on Ubuntu: `apt-get install g++-mingw-w64-x86-64-posix wine python3` (the sessions had Wine 9.0 and MinGW GCC 13-posix). `WINEDEBUG=-all` keeps Wine quiet.

## What is here

- `msvc_shim.h`, `winmacros.h`: forced includes (`-include`), so the sources compile with MinGW as they do with MSVC. The first puts the std headers before `windows.h` and adds MSVC's `min` and `max`. The second defines what the Windows SDK defines and MinGW leaves out (`small`, `hyper`, `near`, `far`, `interface`), so a name clash shows here as it would with MSVC: a lambda named `near` broke an MSVC build once.
- `inc/Windows.h`: a capitalised `windows.h` for sources that include it so (the file system is case sensitive).
- `smtinc/`: the same for building the manual transmission fork. `detours.h` points at the fork's Detours headers by an absolute path: change it to your clone.
- `updtest/`: a stand-in project for running `update.bat` under Wine. `build.bat` and `test.bat` only echo, `src/gauges.h` has a version line, and `run.bat` calls `update.bat` with a fake game folder. Copy `update.bat` beside them; `probes/gitstub.c` builds a `git.exe` that only prints its arguments.
- `fixtures/`: fake game folders for the save and map reading (a Steam `userdata` tree with `CompleteSave*.cfg`). Their `.pak` files and the empty `SnowRunner.exe` stand-ins are left out by the repo's `*.pak` and `*.exe` rules: such a `.pak` is a small zip holding a level's name, made by hand.
- `probes/`: one-off programs over the models and the game code.
  - `sim.cpp`: a rough truck on a gearbox, to see where the model shifts.
  - `halt.cpp`, `ptest.cpp`, `pull.cpp`, `ups.cpp`: shift points and the pull at given settings.
  - `bench.cpp`: the speed of the code pattern search.
  - `load.cpp`: loads DLLs as a loader would and says what came of it.
  - `nan.cpp`: what Wine's C runtime makes of "nan" and "inf" (the three settings test fails below).
  - `layout.cpp`: the manual transmission fork's state block against `src/smt.h`, field by field.
  - `dbg*.cpp`, `gdbg*.cpp`: debug runs of parts of the tests; they include the tests and the fork by absolute paths of the cloud container (`/home/user/...`).

## Commands

From the repository's root, with `T=tools/cloud` and `C="x86_64-w64-mingw32-g++-posix -O2 -std=c++17 -static -include $T/msvc_shim.h -include $T/winmacros.h"`:

```
mkdir -p out/test
# the models and the truck tests
$C -Wall -Wshadow -Wno-unknown-pragmas -I src -o out/test/models_test.exe test/models_test.cpp -lole32 -luuid && wine out/test/models_test.exe
$C -Wall -Wshadow -Wno-unknown-pragmas -I src -o out/test/truck_test.exe test/truck_test.cpp -lole32 -luuid && wine out/test/truck_test.exe
# the settings test (gauges.cpp built with GAUGES_SETTINGS_TEST)
$C -Wall -Wno-unknown-pragmas -municode -D GAUGES_SETTINGS_TEST -I src -o out/test/settings_test.exe src/gauges.cpp -lole32 -luuid -luser32 -lshell32 -ladvapi32 && wine out/test/settings_test.exe
# the add-on
$C -Wall -Wno-unknown-pragmas -municode -I src -c -o out/gauges.o src/gauges.cpp
$C -Wall -Wno-unknown-pragmas -I $T/inc -I deps/imgui -I deps/imgui_src -I deps/stb -I deps/reshade/include -I src -c -o out/overlay.o src/gauge_overlay.cpp
$C -shared -static -o out/Gauges.addon64 out/gauges.o out/overlay.o -lole32 -luuid -luser32 -lshell32 -ladvapi32 -lxinput1_4 -lwinmm
```

At 0.15.4: the models test 149 OK, the truck test OK, the settings test 42 OK and three fails that are Wine's alone ("nan" and "inf" give the defaults; the coolant's three times held to their ranges; the radiator's speed and the damage rates held to theirs). All three hand the ini "nan" or "inf"; `test.bat` with MSVC on Windows passes them. The overlay's object warns about ReShade's `novtable` and the exported `NAME` and `DESCRIPTION`: harmless with MinGW.
