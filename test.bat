@echo off
rem Runs the offline tests build.bat made: the settings (the ini round trip, the scales, the names of keys and pad
rem buttons), the truck reader and the write of engine damage against a made up game, the mod's own gearbox and the
rem hooks' policy, the engine speed, the range,
rem the tilt, the coolant temperature and what it costs the engine on made up drives, and the preview, which draws
rem the cluster and the Gauges tab into out\preview\*.png. With SR_IMAGE set to a copy of the game exe's image from a
rem running game (as the tire mod's test takes one), the truck test checks the place search on it too.
rem   test.bat [serial] [game Bin folder] [game screenshot.png]
rem The game's font files are read from its Bin folder (the Steam default when none is given; without them the
rem preview draws with the ImGui font). With a screenshot the cluster is drawn over it.
rem The tests are the lines of tools\test.jobs. tools\run_jobs.py runs those that share nothing side by side and the
rem others in a row, and prints their output in the list's order; every test runs, also after one failed, and one that
rem does not end in 300 s is killed and fails. "test.bat serial" runs the same lines one at a time. Each test's output
rem is also in out\test\log, with the time each took in times.txt.
cd /d "%~dp0"
set MODE=
if /i "%~1"=="serial" set MODE=--serial
if /i "%~1"=="serial" shift
set FONTS=%~1
if "%FONTS%"=="" set FONTS=C:\Program Files (x86)\Steam\steamapps\common\Snowrunner\Sources\Bin
set SHOT=%2
python -I tools\run_jobs.py tools\test.jobs --log out\test\log --timeout 300 %MODE%
if errorlevel 1 goto failed
echo TESTS OK
exit /b 0
:failed
echo TESTS FAILED
exit /b 1
