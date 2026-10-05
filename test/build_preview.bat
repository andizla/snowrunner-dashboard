@echo off
rem Builds out\test\gauge_preview.exe: the gauge cluster and the Gauges tab drawn without the game (see
rem test\gauge_preview.cpp). Needs the full Dear ImGui sources (1.92; imgui.cpp and backends\imgui_impl_dx11.cpp): set
rem IMGUI_SRC to their folder. Without it the SnowRunner Manual Transmission project's copy is taken, where this mod
rem is made. x64, Visual Studio 2022. Exit code 2 = no sources, nothing built.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0.."
if not defined IMGUI_SRC set IMGUI_SRC=C:\Games\Snowrunner-Manual-Transmission\DLL\imgui
rem no brackets around this check: a folder name with brackets in it would end them early
if not exist "%IMGUI_SRC%\imgui.cpp" goto nosources
if not exist out\obj\preview\shim mkdir out\obj\preview\shim
if not exist out\test mkdir out\test
rem a source tree that includes a precompiled header named shared.h (that project's does) gets an empty one
if not exist out\obj\preview\shim\shared.h echo // empty> out\obj\preview\shim\shared.h
cl /nologo /O2 /W3 /EHsc /std:c++17 /utf-8 /MT /I out\obj\preview\shim /I "%IMGUI_SRC%" /I "%IMGUI_SRC%\backends" /I deps\stb /I src /Fo:out\obj\preview\ /Fe:out\test\gauge_preview.exe test\gauge_preview.cpp "%IMGUI_SRC%\imgui.cpp" "%IMGUI_SRC%\imgui_draw.cpp" "%IMGUI_SRC%\imgui_tables.cpp" "%IMGUI_SRC%\imgui_widgets.cpp" "%IMGUI_SRC%\backends\imgui_impl_dx11.cpp" /link d3d11.lib d3dcompiler.lib windowscodecs.lib ole32.lib || (echo PREVIEW BUILD FAILED & exit /b 1)
echo BUILD OK: out\test\gauge_preview.exe
exit /b 0
:nosources
echo no Dear ImGui sources in "%IMGUI_SRC%": set IMGUI_SRC. The preview is not built.
exit /b 2
