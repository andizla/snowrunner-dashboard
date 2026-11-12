@echo off
rem Builds out\Gauges.asi (the mod, Next Gen Gameplay, for an ASI loader such as out\version.dll; it draws by itself),
rem out\test\settings_test.exe (the ini round trip and the scales, offline),
rem out\test\truck_test.exe (the reader of the game's memory and the write of engine damage against a made up game, offline),
rem out\test\models_test.exe (the engine speed, the range, the tilt, the coolant temperature and what it costs the engine
rem on made up drives, offline),
rem out\test\gauge_preview.exe (the cluster and the Gauges tab drawn into PNG files, offline; test\build_preview.bat)
rem and out\version.dll (the .asi loader, from the sources in tools; the game keeps the loader it has).
rem x64, static CRT, Visual Studio 2022 Community. test.bat runs the tests.
rem   build.bat [serial]
rem The jobs are the lines of tools\build.jobs. tools\run_jobs.py (python, the standard library) runs them side by side,
rem as many as the machine has cores less one (set JOBS=n for another count), and prints their output in the list's
rem order. "build.bat serial" runs the same lines one at a time. Every job is run each time: nothing is skipped by a
rem file's date. Each job's output is also in out\log\build, with the time each took in times.txt.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
set MODE=
if /i "%~1"=="serial" set MODE=--serial
if not exist out mkdir out
if not exist out\obj mkdir out\obj
if not exist out\obj\detours mkdir out\obj\detours
if not exist out\test mkdir out\test
set IMGUI_OBJ=out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_dx11.obj out\obj\ng_overlay.obj out\obj\ng_reshade.obj
set SOUNDS=
if exist assets\turbo_high_1.wav set SOUNDS=out\obj\sounds.res
python -I tools\run_jobs.py tools\build.jobs --log out\log\build --stop %MODE%
if errorlevel 3 goto fail
if errorlevel 2 (echo BUILD OK without the preview: out\Gauges.asi, out\test\settings_test.exe, out\test\truck_test.exe, out\test\models_test.exe, out\test\input_test.exe, out\test\crt_test.exe, out\version.dll & exit /b 0)
if errorlevel 1 goto fail
echo BUILD OK: out\Gauges.asi, out\test\settings_test.exe, out\test\truck_test.exe, out\test\models_test.exe, out\test\input_test.exe, out\test\crt_test.exe, out\test\gauge_preview.exe, out\version.dll
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
