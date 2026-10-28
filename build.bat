@echo off
rem Builds out\Gauges.asi (the mod, Next Gen Gameplay, for an ASI loader such as out\version.dll; it draws by itself),
rem out\test\settings_test.exe (the ini round trip and the scales, offline),
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
rem the mod: Dear ImGui (deps\imgui, 1.92.1) and its DirectX 11 renderer (src\ng_dx11.cpp) compiled in; ng_overlay.cpp
rem is Next Gen's overlay (the Present hook, the NEXT GEN menu), gauge_overlay.cpp draws the cluster and the Gameplay
rem tab with it, its text in the game's font through stb_truetype (deps\stb); gauges.cpp is the mod's thread
cl /nologo /c /O2 /W3 /EHsc /std:c++17 /utf-8 /MT /I deps\imgui /I deps\stb /Fo:out\obj\ deps\imgui\imgui.cpp deps\imgui\imgui_draw.cpp deps\imgui\imgui_tables.cpp deps\imgui\imgui_widgets.cpp src\ng_dx11.cpp || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I deps\imgui /I deps\stb /Fo:out\obj\ src\ng_overlay.cpp src\gauge_overlay.cpp || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I deps\reshade\include /Fo:out\obj\ng_reshade.obj src\ng_reshade.cpp || goto fail
set IMGUI_OBJ=out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_dx11.obj out\obj\ng_overlay.obj out\obj\ng_reshade.obj
rem the game's gearbox functions hooked (src\gearbox_hooks.cpp) through Microsoft Detours (src\detours, MIT, built from
rem its source with the static CRT; its own warnings are not ours), in one library the mod and the tests link. It
rem must be built /O2: the hook on the game's truck update relies on its call through being a tail jump (review 10, D2)
if not exist out\obj\detours mkdir out\obj\detours
cl /nologo /c /O2 /w /MT /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fo:out\obj\detours\ src\detours\detours.cpp src\detours\disasm.cpp src\detours\modules.cpp src\detours\creatwth.cpp src\detours\image.cpp || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /Fo:out\obj\gearbox_hooks.obj src\gearbox_hooks.cpp || goto fail
rem the creep's stub on the game's wheels' drive (src\creep_stub.asm), in the same library
ml64 /nologo /c /Fo out\obj\creep_stub.obj src\creep_stub.asm || goto fail
lib /nologo /OUT:out\obj\gearbox.lib out\obj\gearbox_hooks.obj out\obj\creep_stub.obj out\obj\detours\detours.obj out\obj\detours\disasm.obj out\obj\detours\modules.obj out\obj\detours\creatwth.obj out\obj\detours\image.obj || goto fail
rem the recordings the mod's file carries (src\sounds.rc: assets\turbo_high_1.wav and eight more). Without them the
rem mod is built all the same and makes its sounds by code (src\sound.h)
set SOUNDS=
if exist assets\turbo_high_1.wav (rc /nologo /fo out\obj\sounds.res src\sounds.rc || goto fail) & set SOUNDS=out\obj\sounds.res
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /Fo:out\obj\input_devices.obj src\input_devices.cpp || goto fail
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /LD /Fo:out\obj\gauges.obj /Fe:out\Gauges.asi src\gauges.cpp out\obj\input_devices.obj out\obj\gauge_overlay.obj %IMGUI_OBJ% %SOUNDS% out\obj\gearbox.lib /link user32.lib ole32.lib d3d11.lib d3dcompiler.lib dinput8.lib dxguid.lib || goto fail
rem the settings test: the same source with a main in place of the mod's thread
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /D GAUGES_SETTINGS_TEST /Fo:out\obj\settings_test.obj /Fe:out\test\settings_test.exe src\gauges.cpp out\obj\gearbox.lib /link user32.lib || goto fail
rem the truck test: the reader of the game's memory and the write of engine damage (src\truck.h) against a made up game
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I src /Fo:out\obj\truck_test.obj /Fe:out\test\truck_test.exe test\truck_test.cpp out\obj\gearbox.lib || goto fail
rem the gearbox test: the mod's own gearbox (src\gearbox_model.h) and the hooks' policy, on functions of its own
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I src /Fo:out\obj\gearbox_test.obj /Fe:out\test\gearbox_test.exe test\gearbox_test.cpp out\obj\gearbox.lib || goto fail
rem the models test: the engine speed, the range, the tilt, the coolant temperature and what it costs the engine
rem (src\models.h) on made up drives, and the sounds of a lifted pedal (src\sound.h)
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I src /Fo:out\obj\models_test.obj /Fe:out\test\models_test.exe test\models_test.cpp /link ole32.lib || goto fail
rem the input test: the gearbox's bindings (src\input_bindings.h) on made up snapshots, and the wheels and shifters
rem this machine has through DirectInput (src\input_devices.cpp; DirectInput comes with the Windows SDK)
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I src /Fo:out\obj\ /Fe:out\test\input_test.exe test\input_test.cpp src\input_devices.cpp /link user32.lib dinput8.lib dxguid.lib || goto fail
rem the loader
ml64 /nologo /c /Fo out\obj\version_stubs.obj tools\version_stubs.asm || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /Fo:out\obj\loader.obj tools\loader.cpp || goto fail
link /nologo /DLL /DEF:tools\version.def /OUT:out\version.dll /IMPLIB:out\obj\version_proxy.lib out\obj\version_stubs.obj out\obj\loader.obj || goto fail
rem the preview: left out, with a note, where there are no Dear ImGui sources
call test\build_preview.bat
if errorlevel 2 (echo BUILD OK without the preview: out\Gauges.asi, out\test\settings_test.exe, out\test\truck_test.exe, out\test\models_test.exe, out\test\input_test.exe, out\version.dll & exit /b 0)
if errorlevel 1 goto fail
echo BUILD OK: out\Gauges.asi, out\test\settings_test.exe, out\test\truck_test.exe, out\test\models_test.exe, out\test\input_test.exe, out\test\gauge_preview.exe, out\version.dll
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
