@echo off
rem Builds out\NextGenTuning.asi (the tuning tool: a tab "Tuning" in the NEXT GEN menu that draws the figures the Next
rem Gen mods publish, src\ng_tune.h) and out\test\tune_test.exe. x64, static CRT, Visual Studio 2022 Community.
rem test.bat runs the tests.
rem   build.bat [serial]
rem The jobs are the lines of tools\build.jobs. tools\run_jobs.py (python, the standard library) runs them side by side
rem and prints their output in the list's order. "build.bat serial" runs the same lines one at a time. Every job is run
rem each time. Each job's output is also in out\log\build, with the time each took in times.txt.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
set MODE=
if /i "%~1"=="serial" set MODE=--serial
if not exist out mkdir out
if not exist out\obj mkdir out\obj
if not exist out\test mkdir out\test
set OVERLAY_OBJ=out\obj\ng_overlay.obj out\obj\ng_dx11.obj out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_reshade.obj
python -I tools\run_jobs.py tools\build.jobs --log out\log\build --stop %MODE%
if errorlevel 1 goto fail
echo BUILD OK: out\NextGenTuning.asi, out\test\tune_test.exe
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
