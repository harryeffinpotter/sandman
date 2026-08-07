#requires -Version 5.0
# run.ps1 — one-shot launcher for the KWARE-style external overlay.
#
# Does the full sequence:
#   1. Elevate self if not admin (launcher needs it, external doesn't)
#   2. Run sand_launcher.exe --no-inject to install the kernel driver
#      (skipped if we can already reach the driver's heartbeat — rerunning
#      the launcher when driver is already up is safe but wastes seconds)
#   3. Wait for sand.exe to be running (with timeout)
#   4. Launch PerfMonSvc.exe (external overlay) as normal user
#
# Usage:
#   .\run.ps1                    # normal — installs driver if needed, then overlay
#   .\run.ps1 -SkipLauncher      # driver already up, jump straight to overlay
#   .\run.ps1 -NoWait            # don't wait for sand.exe, just launch external
#   .\run.ps1 -Rebuild           # build_all.ps1 first, then run

param(
    [switch]$SkipLauncher,
    [switch]$NoWait,
    [switch]$Rebuild,
    [int]$WaitSeconds = 60
)

$ErrorActionPreference = "Stop"

$root         = $PSScriptRoot
$launcherExe  = Join-Path $root "launcher\RTSSDriverSvc.exe"
if (-not (Test-Path $launcherExe)) {
    # legacy fallback for old checkouts
    $launcherExe = Join-Path $root "launcher\sand_launcher.exe"
}
$externalExe  = Join-Path $root "external\PerfMonSvc.exe"
$externalDev  = Join-Path $root "external\sand_external.exe"

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
if (-not (Test-Path $launcherExe)) { Fail "sand_launcher.exe not found. Run with -Rebuild first." }
if (-not (Test-Path $externalExe) -and -not (Test-Path $externalDev)) {
    Fail "PerfMonSvc.exe (and sand_external.exe) not found in external\. Run with -Rebuild first."
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
        $argsForChild += @("-WaitSeconds", $WaitSeconds.ToString())
        $scriptPath = $MyInvocation.MyCommand.Path
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "powershell.exe"
        $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`" " + ($argsForChild -join " ")
        $psi.Verb = "runas"
        [System.Diagnostics.Process]::Start($psi) | Out-Null
        exit 0
    }

    Say "Running launcher (driver install, no injection)..."
    $lp = Start-Process -FilePath $launcherExe -ArgumentList "--no-inject" -Wait -PassThru -NoNewWindow
    if ($lp.ExitCode -ne 0) {
        Say "launcher returned exit $($lp.ExitCode) — driver may already be loaded, continuing." "Yellow"
    } else {
        Say "launcher OK — driver installed + syscall hijack active." "Green"
    }
} else {
    Say "Skipping launcher (assuming driver already loaded)."
}

# --- Wait for sand.exe ---
if (-not $NoWait) {
    Say "Waiting up to $WaitSeconds s for sand.exe..."
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $found = $false
    while ((Get-Date) -lt $deadline) {
        if (Get-Process -Name "sand" -ErrorAction SilentlyContinue) {
            $found = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $found) {
        Say "sand.exe never appeared. Launch it manually then re-run with -SkipLauncher." "Yellow"
        exit 2
    }
    Say "sand.exe is running." "Green"

    # Small grace period so BE's initial-scan window can pass before we attach.
    Say "Giving 3 s grace before attaching..."
    Start-Sleep -Seconds 3
}

# --- Launch overlay (as CURRENT USER, not elevated — external doesn't need admin) ---
Say "Launching overlay: $overlayPath"
if (Test-IsAdmin) {
    # We were elevated for launcher — spawn overlay via explorer so it drops
    # back to the normal user token (avoids running our overlay as SYSTEM/admin,
    # which changes some window compositor behaviour).
    Start-Process -FilePath "explorer.exe" -ArgumentList "`"$overlayPath`""
} else {
    Start-Process -FilePath $overlayPath
}

Say "Done. Hotkeys: INSERT = click-through toggle, HOME = menu toggle." "Green"
Say "In the overlay: hit 'Auto-discover' once, then flip silent_mode ON for real play."
