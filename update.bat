@echo off
rem Brings this folder up to the branch, builds, runs the tests and installs out\Gauges.asi into the game, keeping
rem the one it replaces as out\rollback\Gauges_previous.asi. Close the game first. Nothing is installed when the
rem build or a test fails.
rem   update.bat [the game's Sources\Bin folder]
rem Without a folder the Steam default is used. Run it from a cmd window or by a double click.
setlocal
cd /d "%~dp0"
set "BIN=%~1"
if "%BIN%"=="" set "BIN=C:\Program Files (x86)\Steam\steamapps\common\Snowrunner\Sources\Bin"
set "BRANCH=claude/vibrant-darwin-anbhqw"

echo == the branch
git switch main || goto fail
git fetch origin || goto fail
git merge --ff-only origin/%BRANCH% || goto fail
git push origin main || goto fail

echo == the build
call build.bat || goto fail

echo == the tests
call test.bat || goto fail

echo == the install
tasklist /fi "imagename eq SnowRunner.exe" 2>nul | find /i "SnowRunner.exe" >nul && goto running
if not exist "%BIN%\SnowRunner.exe" goto nobin
if not exist out\rollback mkdir out\rollback
if exist "%BIN%\Gauges.asi" copy /y "%BIN%\Gauges.asi" out\rollback\Gauges_previous.asi >nul
copy /y out\Gauges.asi "%BIN%\Gauges.asi" >nul || goto nocopy
for /f "tokens=3" %%v in ('findstr /c:"#define GAUGES_VERSION" src\gauges.h') do set "VERSION=%%~v"
echo.
echo UPDATE OK: Gauges %VERSION% is installed. Gauges.log should start with "Gauges %VERSION%".
echo The one it replaced is out\rollback\Gauges_previous.asi.
pause
exit /b 0

:running
echo The game is running. Close it and run update.bat again: the build and the tests are done, only the install is left.
goto fail
:nobin
echo There is no SnowRunner.exe in "%BIN%".
echo Give the game's Sources\Bin folder: update.bat "D:\Games\Snowrunner\Sources\Bin"
goto fail
:nocopy
echo Out\Gauges.asi could not be copied into "%BIN%". Is the game still closing? Try again, or run the window as administrator.
goto fail
:fail
echo.
echo UPDATE FAILED: nothing was installed after the step above.
pause
exit /b 1
