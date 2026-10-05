@echo off
rem Builds out\test\gauge_preview.exe: the gauge cluster and the Gauges tab drawn without the game (see
rem test\gauge_preview.cpp). Needs the full Dear ImGui sources (1.92; imgui.cpp and backends\imgui_impl_dx11.cpp) in
rem deps\imgui_src, a folder that is not in the repository, or in the folder IMGUI_SRC names. x64, Visual Studio 2022.
rem Exit code 2 = no sources, nothing built.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0.."
if not defined IMGUI_SRC set IMGUI_SRC=deps\imgui_src
rem no brackets around this check: a folder name with brackets in it would end them early
if not exist "%IMGUI_SRC%\imgui.cpp" goto nosources
if not exist out\obj\preview mkdir out\obj\preview
if not exist out\test mkdir out\test
cl /nologo /O2 /W3 /EHsc /std:c++17 /utf-8 /MT /I "%IMGUI_SRC%" /I "%IMGUI_SRC%\backends" /I deps\stb /I src /Fo:out\obj\preview\ /Fe:out\test\gauge_preview.exe test\gauge_preview.cpp "%IMGUI_SRC%\imgui.cpp" "%IMGUI_SRC%\imgui_draw.cpp" "%IMGUI_SRC%\imgui_tables.cpp" "%IMGUI_SRC%\imgui_widgets.cpp" "%IMGUI_SRC%\backends\imgui_impl_dx11.cpp" /link d3d11.lib d3dcompiler.lib windowscodecs.lib ole32.lib || (echo PREVIEW BUILD FAILED & exit /b 1)
echo BUILD OK: out\test\gauge_preview.exe
exit /b 0
:nosources
echo no Dear ImGui sources in "%IMGUI_SRC%": copy them there or set IMGUI_SRC. The preview is not built.
exit /b 2
