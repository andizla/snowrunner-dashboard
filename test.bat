@echo off
rem Runs the offline tests build.bat made: the settings (the ini round trip, the scales, the names of keys and pad
rem buttons), the truck reader and the write of engine damage against a made up game, the mod's own gearbox and the
rem hooks' policy, the engine speed, the range,
rem the tilt, the coolant temperature and what it costs the engine on made up drives, and the preview, which draws
rem the cluster and the Gauges tab into out\preview\*.png. With SR_IMAGE set to a copy of the game exe's image from a
rem running game (as the tire mod's test takes one), the truck test checks the place search on it too.
rem   test.bat [game Bin folder] [game screenshot.png]
rem The game's font files are read from its Bin folder (the Steam default when none is given; without them the
rem preview draws with the ImGui font). With a screenshot the cluster is drawn over it.
cd /d "%~dp0"
set FONTS=%~1
if "%FONTS%"=="" set FONTS=C:\Program Files (x86)\Steam\steamapps\common\Snowrunner\Sources\Bin
set failed=0
out\test\settings_test.exe || set failed=1
out\test\truck_test.exe || set failed=1
out\test\gearbox_test.exe || set failed=1
out\test\models_test.exe || set failed=1
if not exist out\test\gauge_preview.exe goto nopreview
out\test\gauge_preview.exe out\preview "%FONTS%" %2 || set failed=1
goto done
:nopreview
echo no out\test\gauge_preview.exe: the preview was not built
:done
if %failed%==1 (echo TESTS FAILED & exit /b 1)
echo TESTS OK
