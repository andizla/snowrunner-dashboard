@echo off
rem Builds out\test\menu_preview_fault.exe: menu_preview with the overlay compiled with NG_FAULT_TEST, a fault forced
rem in one frame (test\menu_preview.cpp). Run it twice: menu_preview_fault.exe <out dir> <font dir> 1, then with 2.
rem Needs build.bat first (Dear ImGui's objects in out\obj). x64, Visual Studio 2022.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0.."
if not exist out\test mkdir out\test
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /DNG_FAULT_TEST /I deps\imgui /I deps\stb /Fo:out\obj\ng_overlay_fault.obj src\ng_overlay.cpp || (echo MENU PREVIEW FAULT BUILD FAILED & exit /b 1)
cl /nologo /O2 /W3 /EHsc /std:c++17 /utf-8 /MT /DNG_FAULT_TEST /I deps\imgui /I deps\stb /I src /Fo:out\obj\menu_preview_fault.obj /Fe:out\test\menu_preview_fault.exe test\menu_preview.cpp out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_dx11.obj out\obj\ng_overlay_fault.obj out\obj\ng_reshade.obj /link d3d11.lib dxgi.lib d3dcompiler.lib windowscodecs.lib ole32.lib user32.lib || (echo MENU PREVIEW FAULT BUILD FAILED & exit /b 1)
echo BUILD OK: out\test\menu_preview_fault.exe
