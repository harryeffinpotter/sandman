$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$sdkInc = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$sdkLib = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"

$env:PATH = "$vsPath\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "$vsPath\include;$sdkInc\ucrt;$sdkInc\um;$sdkInc\shared;$PWD\src;$PWD\imgui"
$env:LIB = "$vsPath\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"

$sources = @(
    "src\win.cpp"
    "src\overlay.cpp"
    "src\main.cpp"
    "src\pe_resolve.cpp"
    "src\debug_log.cpp"
    "imgui\imgui.cpp"
    "imgui\imgui_draw.cpp"
    "imgui\imgui_tables.cpp"
    "imgui\imgui_widgets.cpp"
    "imgui\imgui_impl_dx11.cpp"
    "imgui\imgui_impl_win32.cpp"
)

cl /EHa /O2 /MT /LD /std:c++17 @sources /Fe:RTSSHelper64.dll /link /OPT:REF /OPT:ICF user32.lib d3d11.lib dxgi.lib dwmapi.lib dxguid.lib dbghelp.lib dcomp.lib delayimp.lib /DELAYLOAD:dbghelp.dll /DELAYLOAD:dcomp.dll

if ($LASTEXITCODE -eq 0) {
    Copy-Item -Force "RTSSHelper64.dll" "launcher\RTSSHelper64.dll"
    Write-Host "Build succeeded: RTSSHelper64.dll (copied to launcher/)" -ForegroundColor Green
} else {
    Write-Host "Build FAILED" -ForegroundColor Red
    exit 1
}
