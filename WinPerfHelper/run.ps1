#requires -Version 5.0
# run.ps1 — one-shot launcher for the KWARE-style external overlay.
#
# Does the full sequence:
#   1. Elevate self if not admin (launcher needs it, external doesn't)
#   2. Run RTSSDriverSvc.exe --no-inject to install the kernel driver
#      (skipped if we can already reach the driver's heartbeat — rerunning
#      the launcher when driver is already up is safe but wastes seconds)
#   3. Wait for sand.exe to be running (with timeout)
#   4. Launch PerfMonSvc.exe (external overlay) as normal user
#
# Usage:
#   .\run.ps1                    # full DLL mode — installs driver + injects RTSSHelper64.dll
#   .\run.ps1 -External          # KWARE mode — driver install only + external overlay
#   .\run.ps1 -SkipLauncher      # driver already up, skip the install pass
#   .\run.ps1 -SkipGame          # game already running / launch it yourself
#   .\run.ps1 -NoWait            # don't wait for sand.exe
#   .\run.ps1 -Rebuild           # build_all.ps1 first, then run

param(
    [switch]$SkipLauncher,
    [switch]$NoWait,
    [switch]$Rebuild,
    [switch]$External,        # external-only mode (--no-inject + PerfMonSvc)
    [switch]$SkipGame,        # don't auto-launch game
    [int]$WaitSeconds = 90
)

$ErrorActionPreference = "Stop"

$root         = $PSScriptRoot
$launcherExe  = Join-Path $root "launcher\RTSSDriverSvc.exe"
if (-not (Test-Path $launcherExe)) {
    # legacy fallback for old checkouts
    $launcherExe = Join-Path $root "launcher\RTSSDriverSvc.exe"
}
$externalExe  = Join-Path $root "external\PerfMonSvc.exe"
$externalDev  = Join-Path $root "external\PerfMonSvc.exe"

# BattlEye launcher — this is the exe Steam runs; it spawns sand.exe.
$gameExe = "C:\Program Files (x86)\Steam\steamapps\common\Sand\Sand_BE.exe"

function Say($msg, $color = "Cyan") {
    Write-Host "[run] $msg" -ForegroundColor $color
}

function Fail($msg) {
    Write-Host "[run] FATAL: $msg" -ForegroundColor Red
    exit 1
}

# --- Rebuild ---
if ($Rebuild) {
    Say "Rebuilding all targets..."
    $buildScript = Join-Path $root "build_all.ps1"
    if (-not (Test-Path $buildScript)) { Fail "build_all.ps1 not found at $buildScript" }
    & $buildScript
    if ($LASTEXITCODE -ne 0) { Fail "build_all.ps1 exited with $LASTEXITCODE" }
}

# --- Sanity ---
if (-not (Test-Path $launcherExe)) { Fail "RTSSDriverSvc.exe not found. Run with -Rebuild first." }
if (-not (Test-Path $externalExe) -and -not (Test-Path $externalDev)) {
    Fail "PerfMonSvc.exe (and PerfMonSvc.exe) not found in external\. Run with -Rebuild first."
}
$overlayPath = if (Test-Path $externalExe) { $externalExe } else { $externalDev }

# --- Admin elevation for launcher stage ---
function Test-IsAdmin {
    $current = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($current)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# --- Driver install stage ---
if (-not $SkipLauncher) {
    if (-not (Test-IsAdmin)) {
        Say "Not elevated — relaunching this script as admin..." "Yellow"
        # Rebuild the argument list so the elevated child gets the same flags
        # minus this admin bounce.
        $argsForChild = @()
        if ($NoWait)      { $argsForChild += "-NoWait" }
        if ($Rebuild)     { $argsForChild += "-Rebuild" }
        if ($SkipLauncher){ $argsForChild += "-SkipLauncher" }
        if ($External)    { $argsForChild += "-External" }
        if ($SkipGame)    { $argsForChild += "-SkipGame" }
        $argsForChild += @("-WaitSeconds", $WaitSeconds.ToString())
        $scriptPath = $MyInvocation.MyCommand.Path
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "powershell.exe"
        $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`" " + ($argsForChild -join " ")
        $psi.Verb = "runas"
        [System.Diagnostics.Process]::Start($psi) | Out-Null
        exit 0
    }

    if ($External) {
        Say "Running launcher (external mode: driver install only, no DLL injection)..."
        $lp = Start-Process -FilePath $launcherExe -ArgumentList "--no-inject" -Wait -PassThru -NoNewWindow
    } else {
        Say "Running launcher (full DLL mode)..."
        $lp = Start-Process -FilePath $launcherExe -Wait -PassThru -NoNewWindow
    }
    if ($lp.ExitCode -ne 0) {
        Say "launcher returned exit $($lp.ExitCode) — driver may already be loaded, continuing." "Yellow"
    } else {
        Say "launcher OK." "Green"
    }
} else {
    Say "Skipping launcher (assuming driver already loaded)."
}

# --- Auto-launch game if not running ---
function Get-GameProc {
    Get-Process -Name "sand","Sand_BE" -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

if (-not $SkipGame -and -not (Get-GameProc)) {
    if (Test-Path $gameExe) {
        Say "Launching game: $gameExe"
        # Start-Process is fine here — Sand_BE.exe respawns sand.exe under BE.
        Start-Process -FilePath $gameExe
    } else {
        Say "Game exe not found at $gameExe — start it manually." "Yellow"
    }
}

# --- Wait for sand.exe (or Sand_BE.exe as intermediate) ---
if (-not $NoWait) {
    Say "Waiting up to $WaitSeconds s for sand.exe..."
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $found = $false
    while ((Get-Date) -lt $deadline) {
        # Prefer the un-BE-wrapped sand.exe once BE has spawned it. If
        # only Sand_BE.exe is present, keep waiting — sand.exe is what
        # we actually attach to.
        if (Get-Process -Name "sand" -ErrorAction SilentlyContinue) {
            $found = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $found) {
        Say "sand.exe never appeared. Get past BE's launch screen, then re-run with -SkipLauncher -SkipGame." "Yellow"
        exit 2
    }
    Say "sand.exe is running." "Green"

    # Small grace period so BE's initial-scan window can pass before we attach.
    Say "Giving 5 s grace before attaching..."
    Start-Sleep -Seconds 5
}

# --- Launch overlay (external mode only) ---
if ($External) {
    Say "Launching external overlay: $overlayPath"
    if (Test-IsAdmin) {
        # We were elevated for launcher — spawn overlay via explorer so it
        # drops back to the normal user token.
        Start-Process -FilePath "explorer.exe" -ArgumentList "`"$overlayPath`""
    } else {
        Start-Process -FilePath $overlayPath
    }
    Say "Done. External hotkeys: INSERT = click-through toggle, HOME = menu toggle." "Green"
} else {
    Say "Done. DLL is injected (or was already). In-game menu should be visible." "Green"
    Say "Full-DLL hotkeys: INSERT = menu toggle."
}
