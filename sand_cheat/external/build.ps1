$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$sdkInc = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$sdkLib = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"

$launcherSrc = Resolve-Path "$PWD\..\launcher\src"
$launcherCommon = Resolve-Path "$PWD\..\launcher\common"
$imguiDir = Resolve-Path "$PWD\..\imgui"

$env:PATH = "$vsPath\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "$vsPath\include;$sdkInc\ucrt;$sdkInc\um;$sdkInc\shared;$PWD\src;$launcherSrc;$launcherCommon;$imguiDir"
$env:LIB = "$vsPath\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"

Write-Host "`n=== sand_external Build ===" -ForegroundColor Cyan

$sources = @(
    "src\main.cpp"
    "src\overlay.cpp"
    "src\state.cpp"
    "src\ui.cpp"
    "$launcherSrc\cmdchannel.cpp"
    "$imguiDir\imgui.cpp"
    "$imguiDir\imgui_draw.cpp"
    "$imguiDir\imgui_tables.cpp"
    "$imguiDir\imgui_widgets.cpp"
    "$imguiDir\imgui_impl_win32.cpp"
    "$imguiDir\imgui_impl_dx11.cpp"
)

$libs = @(
    "kernel32.lib"
    "user32.lib"
    "gdi32.lib"
    "advapi32.lib"
    "ntdll.lib"
    "d3d11.lib"
    "dxgi.lib"
    "dwmapi.lib"
    "dcomp.lib"
    "dxguid.lib"
)

cl /EHsc /O2 /MT /std:c++17 /Isrc /I$launcherSrc /I$launcherCommon /I$imguiDir @sources /Fe:sand_external.exe /link @libs

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nBuild FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "`nBuild succeeded: sand_external.exe" -ForegroundColor Green

Remove-Item -Force *.obj -ErrorAction SilentlyContinue
