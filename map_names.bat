@echo off
rem Reads the game's map names from its own files into out\map_names.txt (tools\map_names.ps1). It only reads.
rem   map_names.bat [the game's folder]
rem Without a folder the Steam default is used. Run it from a cmd window or by a double click.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\map_names.ps1 %*
pause
