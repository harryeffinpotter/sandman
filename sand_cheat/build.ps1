$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$sdkInc = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$sdkLib = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"

$env:PATH = "$vsPath\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "$vsPath\include;$sdkInc\ucrt;$sdkInc\um;$sdkInc\shared;$PWD\src;$PWD\imgui"
$env:LIB = "$vsPath\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"

$sources = @(
    "src\cheat.cpp"
    "src\overlay.cpp"
    "src\main.cpp"
    "imgui\imgui.cpp"
    "imgui\imgui_draw.cpp"
    "imgui\imgui_tables.cpp"
    "imgui\imgui_widgets.cpp"
    "imgui\imgui_impl_dx11.cpp"
    "imgui\imgui_impl_win32.cpp"
)

cl /EHa /O2 /MT /LD /std:c++17 /Zi @sources /Fe:sand_cheat.dll /link /DEBUG /OPT:REF /OPT:ICF user32.lib d3d11.lib dxgi.lib dwmapi.lib dxguid.lib dbghelp.lib

if ($LASTEXITCODE -eq 0) {
    Copy-Item -Force "sand_cheat.dll" "launcher\sand_cheat.dll"
    Copy-Item -Force "sand_cheat.pdb" "launcher\sand_cheat.pdb"
    Write-Host "Build succeeded: sand_cheat.dll (copied to launcher/)" -ForegroundColor Green
} else {
    Write-Host "Build FAILED" -ForegroundColor Red
    exit 1
}
