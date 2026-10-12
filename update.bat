@echo off
rem Brings this folder up to the branch, builds, runs the tests and installs out\Gauges.addon64 into the game (ReShade
rem loads it), keeping the one it replaces as out\rollback\Gauges_previous.addon64. A Gauges.asi from before is moved
rem out of the game to out\rollback\Gauges_previous.asi, so one copy runs. Close the game first. Nothing is installed when the
rem build or a test fails. All the build and the tests print goes to out\build.log and out\test.log; the window
rem shows the errors and the failed tests.
rem   update.bat [the game's Sources\Bin folder]
rem Without a folder the Steam default is used. Run it from a cmd window or by a double click.
rem It runs from a copy in %TEMP%: the merge may bring a new update.bat, and cmd reads a running batch file from disk
rem as it goes. Edits made in this folder are set aside for the merge and put back after it; a file the branch changed
rem too is left as the branch has it, and the edits are kept in git's stash (git stash show -p shows them).
if "%~1"=="--copy" goto start
del "%TEMP%\snowrunner_gauges_update.bat" 2>nul
copy /y "%~f0" "%TEMP%\snowrunner_gauges_update.bat" >nul
if not exist "%TEMP%\snowrunner_gauges_update.bat" (echo Could not copy update.bat to the temp folder. & pause & exit /b 1)
"%TEMP%\snowrunner_gauges_update.bat" --copy "%~dp0" %*
:start
setlocal
cd /d "%~2"
set "BIN=%~3"
if "%BIN%"=="" set "BIN=C:\Program Files (x86)\Steam\steamapps\common\Snowrunner\Sources\Bin"
set "BRANCH=claude/vibrant-darwin-anbhqw"

echo == the branch
git switch main || goto fail
git fetch origin || goto fail
git merge-base --is-ancestor main origin/%BRANCH% || goto diverged
git merge --ff-only --autostash origin/%BRANCH% || goto fail
set "CLASHED="
for /f "delims=" %%f in ('git diff --name-only --diff-filter U') do (
    set "CLASHED=1"
    echo Your edit of %%f clashed with the branch: the branch's is in place, yours is kept in git's stash.
    git checkout HEAD -- "%%f"
)
git push origin main || goto fail

if not exist out mkdir out
echo == the build: a minute or two, all it prints goes to out\build.log
call build.bat > out\build.log 2>&1 || goto buildfail

echo == the tests: all they print goes to out\test.log
call test.bat > out\test.log 2>&1 || goto testfail
findstr /c:"TEST OK" /c:"TESTS OK" out\test.log

echo == the install
tasklist /fi "imagename eq SnowRunner.exe" 2>nul | find /i "SnowRunner.exe" >nul && goto running
if not exist "%BIN%\SnowRunner.exe" goto nobin
if not exist out\rollback mkdir out\rollback
if exist "%BIN%\Gauges.addon64" copy /y "%BIN%\Gauges.addon64" out\rollback\Gauges_previous.addon64 >nul
copy /y out\Gauges.addon64 "%BIN%\Gauges.addon64" >nul || goto nocopy
if exist "%BIN%\Gauges.asi" copy /y "%BIN%\Gauges.asi" out\rollback\Gauges_previous.asi >nul
if exist "%BIN%\Gauges.asi" del "%BIN%\Gauges.asi"
if exist "%BIN%\Gauges.asi" goto asileft
for /f "tokens=3" %%v in ('findstr /c:"#define GAUGES_VERSION" src\gauges.h') do set "VERSION=%%~v"
echo.
echo UPDATE OK: Gauges %VERSION% is installed. Gauges.log should start with "Gauges %VERSION%".
echo The one it replaced is out\rollback\Gauges_previous.addon64, and a Gauges.asi from before is in out\rollback too.
if defined CLASHED echo Your edits that clashed are in git's stash: git stash show -p shows them, git stash pop puts them back.
pause
exit /b 0

:diverged
echo.
echo This folder's main has commits the branch does not have, made here by hand or by a session working in this folder:
git log --oneline origin/%BRANCH%..main
git push origin main || goto fail
echo They are now on GitHub's main. Ask Claude to bring them into the branch, then run update.bat again.
goto fail
:buildfail
echo The errors:
findstr /i /c:" error " /c:"error C" /c:"error L" /c:"fatal error" /c:"BUILD FAILED" out\build.log
echo The whole build output is in out\build.log: send me that file.
goto fail
:testfail
echo The tests that failed:
findstr /c:"FAIL" out\test.log
echo The whole test output is in out\test.log: send me that file.
goto fail
:running
echo The game is running. Close it and run update.bat again: the build and the tests are done, only the install is left.
goto fail
:nobin
echo There is no SnowRunner.exe in "%BIN%".
echo Give the game's Sources\Bin folder: update.bat "D:\Games\Snowrunner\Sources\Bin"
goto fail
:nocopy
echo Out\Gauges.addon64 could not be copied into "%BIN%". Is the game still closing? Try again, or run the window as administrator.
goto fail
:asileft
echo Gauges.addon64 is installed, but the old Gauges.asi could not be deleted from "%BIN%". Delete it by hand:
echo with both in the folder only one runs, and the log says which.
pause
exit /b 1
:fail
echo.
echo UPDATE FAILED: nothing was installed after the step above.
pause
exit /b 1
