@echo off
rem Builds out\test\menu_preview.exe: Next Gen's overlay (src\ng_overlay.cpp) hooked onto a WARP swap chain without the
rem game, the NEXT GEN menu with the Gameplay tab drawn into PNG files (test\menu_preview.cpp). Needs build.bat first
rem (the overlay's and Dear ImGui's objects in out\obj). x64, Visual Studio 2022.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0.."
if not exist out\test mkdir out\test
cl /nologo /O2 /W3 /EHsc /std:c++17 /utf-8 /MT /I deps\imgui /I deps\stb /I src /Fo:out\obj\menu_preview.obj /Fe:out\test\menu_preview.exe test\menu_preview.cpp out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_dx11.obj out\obj\ng_overlay.obj out\obj\ng_reshade.obj /link d3d11.lib d3dcompiler.lib windowscodecs.lib ole32.lib user32.lib || (echo MENU PREVIEW BUILD FAILED & exit /b 1)
echo BUILD OK: out\test\menu_preview.exe
