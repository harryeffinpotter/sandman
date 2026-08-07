$ErrorActionPreference = "Stop"

Write-Host "`n==============================================" -ForegroundColor Cyan
Write-Host "  Sand Cheat — full build (DLL + launcher + external)" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

$root = $PSScriptRoot
if (-not $root) { $root = (Get-Location).Path }

Push-Location "$root"
try {
    .\build.ps1
    if ($LASTEXITCODE -ne 0) { throw "DLL build failed" }
} finally { Pop-Location }

Push-Location "$root\launcher"
try {
    .\build_launcher.ps1
    if ($LASTEXITCODE -ne 0) { throw "Launcher build failed" }
} finally { Pop-Location }

Push-Location "$root\external"
try {
    .\build.ps1
    if ($LASTEXITCODE -ne 0) { throw "External build failed" }
} finally { Pop-Location }

Write-Host "`n==============================================" -ForegroundColor Green
Write-Host "  ALL BUILDS PASSED" -ForegroundColor Green
Write-Host "==============================================" -ForegroundColor Green
Write-Host "  DLL:       RTSSHelper64.dll         (renamed from sand_cheat.dll)"
Write-Host "  Launcher:  launcher\RTSSDriverSvc.exe (renamed from sand_launcher.exe)"
Write-Host "  External:  external\PerfMonSvc.exe   (renamed from sand_external.exe)"
Write-Host "`nDeploy workflow (KWARE-style):"
Write-Host "  1. RTSSDriverSvc.exe --no-inject   # driver install only"
Write-Host "  2. PerfMonSvc.exe                  # attach as external"
Write-Host "`nOR full DLL mode:"
Write-Host "  1. RTSSDriverSvc.exe               # driver install + DLL inject"
Write-Host "`nAll runtime logs now live in %APPDATA%\Microsoft\PerfCache\perf_*.dat"
