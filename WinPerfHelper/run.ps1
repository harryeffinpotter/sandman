#requires -Version 5.0
# run.ps1 — one-shot launcher. Order-corrected:
#   1. Elevate self
#   2. (opt) rebuild
#   3. Launch Sand_BE.exe if not running
#   4. Wait for sand.exe (the real game process, spawned by BE splash)
#   5. Small grace period
#   6. THEN run RTSSDriverSvc.exe — driver install + (optional) DLL inject
#   7. If -External, also launch PerfMonSvc.exe
#
# The previous version ran the launcher BEFORE the wait, which meant the
# injection picker opened before sand.exe existed and injection silently
# failed to find its target.

param(
    [switch]$SkipLauncher,
    [switch]$Rebuild,
    [switch]$External,
    [switch]$SkipGame,
    [int]$WaitSeconds = 180
)

$ErrorActionPreference = "Stop"

$root         = $PSScriptRoot
$launcherExe  = Join-Path $root "launcher\RTSSDriverSvc.exe"
$overlayPath  = Join-Path $root "external\PerfMonSvc.exe"
$gameExe      = "C:\Program Files (x86)\Steam\steamapps\common\Sand\Sand_BE.exe"

function Say($msg, $color = "Cyan") { Write-Host "[run] $msg" -ForegroundColor $color }
function Fail($msg) { Write-Host "[run] FATAL: $msg" -ForegroundColor Red; Read-Host "press enter"; exit 1 }

# --- rebuild ---
if ($Rebuild) {
    Say "rebuild..."
    & (Join-Path $root "build_all.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "rebuild failed" }
}

if (-not (Test-Path $launcherExe)) { Fail "launcher exe not found: $launcherExe" }
if (-not (Test-Path $overlayPath) -and $External) { Fail "overlay not found: $overlayPath" }

# --- elevation ---
function Test-IsAdmin {
    $current = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($current)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (-not (Test-IsAdmin)) {
    Say "elevating..." "Yellow"
    $argsForChild = @()
    if ($Rebuild)      { $argsForChild += "-Rebuild" }
    if ($SkipLauncher) { $argsForChild += "-SkipLauncher" }
    if ($External)     { $argsForChild += "-External" }
    if ($SkipGame)     { $argsForChild += "-SkipGame" }
    $argsForChild += @("-WaitSeconds", $WaitSeconds.ToString())
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "powershell.exe"
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`" " + ($argsForChild -join " ")
    $psi.Verb = "runas"
    [System.Diagnostics.Process]::Start($psi) | Out-Null
    exit 0
}

# --- game auto-launch ---
function Get-SandProc { Get-Process -Name "sand" -ErrorAction SilentlyContinue | Select-Object -First 1 }
function Get-BeProc   { Get-Process -Name "Sand_BE" -ErrorAction SilentlyContinue | Select-Object -First 1 }

if (-not $SkipGame -and -not (Get-SandProc) -and -not (Get-BeProc)) {
    if (Test-Path $gameExe) {
        Say "starting Sand_BE.exe..."
        Start-Process -FilePath $gameExe
    } else {
        Say "game not found at $gameExe — start it yourself." "Yellow"
    }
}

# --- WAIT for sand.exe (must exist BEFORE launcher runs, so injection has a target) ---
Say "waiting up to $WaitSeconds s for sand.exe to appear (past BE splash)..."
$deadline = (Get-Date).AddSeconds($WaitSeconds)
$lastReport = 0
$found = $false
while ((Get-Date) -lt $deadline) {
    if (Get-SandProc) { $found = $true; break }
    $secsLeft = [int]($deadline - (Get-Date)).TotalSeconds
    if (($WaitSeconds - $secsLeft) - $lastReport -ge 10) {
        $lastReport = $WaitSeconds - $secsLeft
        $be = Get-BeProc
        $beStatus = if ($be) { "Sand_BE.exe still up (BE splash)" } else { "no game process yet" }
        Say ("waiting... {0}s elapsed, {1}s left, {2}" -f $lastReport, $secsLeft, $beStatus)
    }
    Start-Sleep -Milliseconds 500
}
if (-not $found) {
    Fail "sand.exe never appeared. Get past BE's splash screen first, then re-run with -SkipGame."
}
Say "sand.exe is up." "Green"

# Grace period so BE's initial-scan window passes before we touch the process.
Say "5s grace before attaching..."
Start-Sleep -Seconds 5

# --- NOW run the launcher ---
if (-not $SkipLauncher) {
    if ($External) {
        Say "launcher (external mode — driver install only)..." "Cyan"
        $lp = Start-Process -FilePath $launcherExe -ArgumentList "--no-inject" -Wait -PassThru -NoNewWindow
    } else {
        Say "launcher (FULL DLL — will pop injection picker; click Inject)..." "Cyan"
        $lp = Start-Process -FilePath $launcherExe -Wait -PassThru -NoNewWindow
    }
    if ($lp.ExitCode -ne 0) {
        Say "launcher exit $($lp.ExitCode) — driver may already be loaded, continuing." "Yellow"
    } else {
        Say "launcher OK." "Green"
    }
} else {
    Say "skipping launcher."
}

# --- external overlay if requested ---
if ($External) {
    Say "launching overlay: $overlayPath"
    Start-Process -FilePath "explorer.exe" -ArgumentList "`"$overlayPath`""
    Say "done. External hotkeys: INSERT = click-through, HOME = menu toggle." "Green"
} else {
    Say "done. If injection landed, in-game DLL menu should appear. INSERT toggles it." "Green"
    Say "if nothing shows: check %APPDATA%\Microsoft\PerfCache\perf_events.dat for the ringlog"
}

Say "keeping window open — close manually when done."
Read-Host "press enter to exit"
