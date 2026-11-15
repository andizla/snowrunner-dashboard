@echo off
rem Runs the offline tests build.bat made: the tuning block (the layout, the soft version rule, the hand-over) and the
rem player text check. The tests are the lines of tools\test.jobs; "test.bat serial" runs them one at a time.
cd /d "%~dp0"
set MODE=
if /i "%~1"=="serial" set MODE=--serial
python -I tools\run_jobs.py tools\test.jobs --log out\test\log --timeout 300 %MODE%
if errorlevel 1 goto failed
echo TESTS OK
exit /b 0
:failed
echo TESTS FAILED
exit /b 1
