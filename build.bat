@echo off
rem Builds out\Gauges.asi (the mod), out\test\settings_test.exe (the ini round trip and the scales, offline),
rem out\test\truck_test.exe (the reader of the game's memory and the write of engine damage against a made up game, offline),
rem out\test\models_test.exe (the engine speed, the range, the tilt, the coolant temperature and what it costs the engine
rem on made up drives, offline),
rem out\test\gauge_preview.exe (the cluster and the Gauges tab drawn into PNG files, offline; test\build_preview.bat)
rem and out\version.dll (the .asi loader, from the sources in tools; the game keeps the loader it has).
rem x64, static CRT, Visual Studio 2022 Community. test.bat runs the tests.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\obj mkdir out\obj
if not exist out\test mkdir out\test
rem the mod: gauge_overlay.cpp draws through ReShade's add-on overlay (headers only, deps\reshade and deps\imgui), its
rem text in the game's font through stb_truetype (deps\stb); gauges.cpp is the mod's thread
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I deps\imgui /I deps\reshade\include /I deps\stb /Fo:out\obj\gauge_overlay.obj src\gauge_overlay.cpp || goto fail
rem the recordings the mod's file carries (src\sounds.rc: assets\turbo_high_1.wav and eight more). Without them the
rem mod is built all the same and makes its sounds by code (src\sound.h)
set SOUNDS=
if exist assets\turbo_high_1.wav (rc /nologo /fo out\obj\sounds.res src\sounds.rc || goto fail) & set SOUNDS=out\obj\sounds.res
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /LD /Fo:out\obj\gauges.obj /Fe:out\Gauges.asi src\gauges.cpp out\obj\gauge_overlay.obj %SOUNDS% /link user32.lib ole32.lib || goto fail
rem the settings test: the same source with a main in place of the mod's thread
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /D GAUGES_SETTINGS_TEST /Fo:out\obj\settings_test.obj /Fe:out\test\settings_test.exe src\gauges.cpp /link user32.lib || goto fail
rem the truck test: the reader of the game's memory and the write of engine damage (src\truck.h) against a made up game
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I src /Fo:out\obj\truck_test.obj /Fe:out\test\truck_test.exe test\truck_test.cpp || goto fail
rem the models test: the engine speed, the range, the tilt, the coolant temperature and what it costs the engine
rem (src\models.h) on made up drives, and the sounds of a lifted pedal (src\sound.h)
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I src /Fo:out\obj\models_test.obj /Fe:out\test\models_test.exe test\models_test.cpp /link ole32.lib || goto fail
rem the loader
ml64 /nologo /c /Fo out\obj\version_stubs.obj tools\version_stubs.asm || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /Fo:out\obj\loader.obj tools\loader.cpp || goto fail
link /nologo /DLL /DEF:tools\version.def /OUT:out\version.dll /IMPLIB:out\obj\version_proxy.lib out\obj\version_stubs.obj out\obj\loader.obj || goto fail
rem the preview: left out, with a note, where there are no Dear ImGui sources
call test\build_preview.bat
if errorlevel 2 (echo BUILD OK without the preview: out\Gauges.asi, out\test\settings_test.exe, out\test\truck_test.exe, out\test\models_test.exe, out\version.dll & exit /b 0)
if errorlevel 1 goto fail
echo BUILD OK: out\Gauges.asi, out\test\settings_test.exe, out\test\truck_test.exe, out\test\models_test.exe, out\test\gauge_preview.exe, out\version.dll
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
