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
Write-Host "  DLL:       sand_cheat.dll (legacy, for --with-inject)"
Write-Host "  Launcher:  launcher\sand_launcher.exe"
Write-Host "  External:  external\sand_external.exe + PerfMonSvc.exe"
Write-Host "`nDeploy workflow (KWARE-style):"
Write-Host "  1. sand_launcher.exe --no-inject     # driver install only"
Write-Host "  2. PerfMonSvc.exe                    # attach as external"
