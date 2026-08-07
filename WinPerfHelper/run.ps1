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
    [switch]$Manual,        # opt IN to the "press ENTER when ready" gate — default now auto-waits
    [int]$WaitSeconds = 300,
    [int]$AutoGraceSec = 40, # sleep after sand.exe appears (game should be rendering by then)
    [int]$RetryCount = 4,    # retry launcher this many times if injection fails
    [int]$RetryDelaySec = 15
)

$ErrorActionPreference = "Stop"

# Trap any unhandled error and (a) dump to disk (b) keep window open.
# Dumping to disk means even if the window somehow closes, we can grab
# the trace after the fact.
trap {
    $errFile = Join-Path ([Environment]::GetFolderPath('ApplicationData')) "Microsoft\PerfCache\run_error.log"
    try {
        $errDir = Split-Path $errFile
        if (-not (Test-Path $errDir)) { New-Item -ItemType Directory -Force -Path $errDir | Out-Null }
        $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        $dump = @"

===== $stamp =====
ERROR: $_
InvocationInfo: $($_.InvocationInfo | Out-String)
Stack trace:
$($_.ScriptStackTrace)
PSVersion: $($PSVersionTable.PSVersion)
Line: $($_.InvocationInfo.ScriptLineNumber)  Column: $($_.InvocationInfo.OffsetInLine)
ScriptName: $($_.InvocationInfo.ScriptName)
CommandLine: $($_.InvocationInfo.Line)
"@
        Add-Content -LiteralPath $errFile -Value $dump
    } catch {}

    Write-Host ""
    Write-Host "==============================================================" -ForegroundColor Red
    Write-Host " run.ps1 FATAL ERROR" -ForegroundColor Red
    Write-Host "==============================================================" -ForegroundColor Red
    Write-Host "Error:   $_" -ForegroundColor Red
    Write-Host "At line: $($_.InvocationInfo.ScriptLineNumber)  col: $($_.InvocationInfo.OffsetInLine)" -ForegroundColor Red
    Write-Host "Command: $($_.InvocationInfo.Line.Trim())" -ForegroundColor Red
    Write-Host ""
    Write-Host "Stack trace:" -ForegroundColor DarkYellow
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkYellow
    Write-Host ""
    Write-Host "Full trace saved to: $errFile" -ForegroundColor Yellow
    Write-Host "If this window closes, cat that file." -ForegroundColor Yellow
    Read-Host "press ENTER to close"
    exit 1
}

$root         = $PSScriptRoot
$launcherExe  = Join-Path $root "launcher\RTSSDriverSvc.exe"
$overlayPath  = Join-Path $root "external\PerfMonSvc.exe"
$gameExe      = "C:\Program Files (x86)\Steam\steamapps\common\Sand\Sand_BE.exe"
$rtssExe      = "C:\Program Files (x86)\RivaTuner Statistics Server\RTSS.exe"

function Say($msg, $color = "Cyan") { Write-Host "[run] $msg" -ForegroundColor $color }
function Fail($msg) {
    Write-Host ""
    Write-Host "==============================================================" -ForegroundColor Red
    Write-Host " [run] FATAL: $msg" -ForegroundColor Red
    Write-Host "==============================================================" -ForegroundColor Red
    Read-Host "press ENTER to close (screenshot this window first)"
    exit 1
}

# --- rebuild ---
if ($Rebuild) {
    Say "rebuild..."
    & (Join-Path $root "build_all.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "rebuild failed" }
}

if (-not (Test-Path $launcherExe)) { Fail "launcher exe not found: $launcherExe" }
if (-not (Test-Path $overlayPath) -and $External) { Fail "overlay not found: $overlayPath" }

# --- preflight report (BEFORE anything spawns / elevates) ---
Say "=== Preflight ===" "Cyan"
$issues = @()

$osBuild = (Get-CimInstance Win32_OperatingSystem).BuildNumber
Say "  Windows build:        $osBuild"
if ($osBuild -ne "26200") { $issues += "Windows build $osBuild (target: 26200) — kernel offsets may not match" }

$dotnet = try { [System.Reflection.Assembly]::LoadWithPartialName('System') -ne $null } catch { $false }
Say "  .NET assemblies:      $(if ($dotnet) { 'OK' } else { 'MISSING' })"
if (-not $dotnet) { $issues += ".NET Framework unusable" }

if (Test-Path $gameExe) {
    Say "  Sand_BE.exe:          $gameExe" "Green"
} else {
    Say "  Sand_BE.exe:          NOT FOUND at $gameExe" "Red"
    $issues += "Sand game not at expected path — edit `$gameExe in run.ps1 if it's elsewhere"
}

if (Test-Path $rtssExe) {
    Say "  RTSS.exe:             $rtssExe" "Green"
} else {
    if (-not $External) {
        Say "  RTSS.exe:             NOT FOUND (DLL mode needs it)" "Red"
        $issues += "RTSS not installed at $rtssExe — download from guru3d.com/download/rtss/"
    } else {
        Say "  RTSS.exe:             not installed (fine for -External mode)" "Yellow"
    }
}

$wdFilter = Get-Service -Name "WdFilter" -ErrorAction SilentlyContinue
if ($wdFilter -and $wdFilter.Status -eq "Running") {
    Say "  Defender WdFilter:    RUNNING (launcher will refuse to install driver)" "Red"
    $issues += "Windows Defender is active — need to disable real-time protection OR use Sordum's Defender Control"
} else {
    Say "  Defender WdFilter:    off" "Green"
}

if ($issues.Count -gt 0) {
    Write-Host ""
    Say "==============================================================" "Yellow"
    Say " Preflight found $($issues.Count) issue(s):" "Yellow"
    foreach ($i in $issues) { Say "   * $i" "Yellow" }
    Say "==============================================================" "Yellow"
    Write-Host ""
    $go = Read-Host "Continue anyway? (y = try, anything else = quit)"
    if ($go -ne "y" -and $go -ne "Y") {
        Say "aborted at preflight." "Yellow"
        Read-Host "press ENTER to close"
        exit 3
    }
} else {
    Say "  All checks passed." "Green"
}

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
    if ($Manual)       { $argsForChild += "-Manual" }
    $argsForChild += @("-WaitSeconds", $WaitSeconds.ToString())
    $argsForChild += @("-AutoGraceSec", $AutoGraceSec.ToString())
    $argsForChild += @("-RetryCount", $RetryCount.ToString())
    $argsForChild += @("-RetryDelaySec", $RetryDelaySec.ToString())
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "powershell.exe"
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`" " + ($argsForChild -join " ")
    $psi.Verb = "runas"
    [System.Diagnostics.Process]::Start($psi) | Out-Null
    exit 0
}

# --- RTSS check (injection vector — MUST be running before game starts) ---
function Get-RtssProc { Get-Process -Name "RTSS" -ErrorAction SilentlyContinue | Select-Object -First 1 }

if (-not $External) {
    # RTSS only needed for DLL-mode injection. External mode bypasses it.
    if (-not (Get-RtssProc)) {
        if (Test-Path $rtssExe) {
            Say "RTSS not running — starting it..." "Yellow"
            Start-Process -FilePath $rtssExe
            Start-Sleep -Seconds 3
            if (-not (Get-RtssProc)) {
                Fail "Started RTSS.exe but it didn't stay running. Launch it manually first."
            }
        } else {
            Fail @"
RTSS is NOT installed at $rtssExe.
DLL injection requires RTSS (Rivatuner Statistics Server) as the vector.
Install it from: https://www.guru3d.com/download/rtss-rivatuner-statistics-server-download/
After install: run RTSS, add sand.exe to Application Detection with Level = HIGH.
Then re-run this script.
Alternatively: use -External mode which doesn't need RTSS.
"@
        }
    }
    Say "RTSS is running." "Green"
    Say "Reminder: sand.exe must be in RTSS's App Detection with Level=HIGH." "Yellow"
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
$sandProc = Get-SandProc
Say "sand.exe process detected. PID=$($sandProc.Id) StartTime=$($sandProc.StartTime)" "Green"
Say "(BUT the game itself may still be loading — WAIT.)" "Yellow"

if ($Manual) {
    Write-Host ""
    Say "manual mode: waiting for you to press ENTER once the game is rendering" "Yellow"
    Read-Host "press ENTER when the game is VISIBLY RENDERING (title/menu/in-match)"
} else {
    # Default: automagic. Sleep AutoGraceSec then start retry loop.
    Say "auto mode: sleeping ${AutoGraceSec}s while game finishes loading..." "Cyan"
    for ($i = $AutoGraceSec; $i -gt 0; $i--) {
        Write-Host -NoNewline "`r  T-minus $i s (get past BE splash / login)   "
        Start-Sleep -Seconds 1
    }
    Write-Host ""
}

# Re-verify sand.exe survived — BE sometimes kills the splash sand.exe
# and respawns a NEW one for actual gameplay. If we injected against
# the dead PID, it silently failed. Poll for a healthy sand.exe.
Say "verifying sand.exe is still alive..."
$verifyDeadline = (Get-Date).AddSeconds(60)
$goodSand = $null
while ((Get-Date) -lt $verifyDeadline) {
    $sp = Get-SandProc
    if ($sp -and -not $sp.HasExited) {
        # Extra sanity: main thread should exist + process should have a HWND
        try {
            if ($sp.MainWindowHandle -ne [IntPtr]::Zero) {
                $goodSand = $sp
                break
            }
        } catch {}
        # No HWND yet — game still on splash. Keep waiting.
    }
    Start-Sleep -Milliseconds 500
}
if (-not $goodSand) {
    Fail "sand.exe is not in a good state (no main window). Get INTO the game (past BE splash + past login) then re-run with -SkipGame -SkipLauncher."
}
Say "OK — sand.exe PID=$($goodSand.Id) has main window (hwnd=$($goodSand.MainWindowHandle)). Proceeding." "Green"

# --- NOW run the launcher (with retry if it fails) ---
if (-not $SkipLauncher) {
    $attempt = 0
    $success = $false
    while ($attempt -lt $RetryCount) {
        $attempt++
        if ($External) {
            Say "launcher attempt $attempt/$RetryCount (external mode — driver install only)..." "Cyan"
            $lp = Start-Process -FilePath $launcherExe -ArgumentList "--no-inject" -Wait -PassThru -NoNewWindow
        } else {
            Say "launcher attempt $attempt/$RetryCount (FULL DLL — auto-clicking Inject on picker if it pops)..." "Cyan"
            $lp = Start-Process -FilePath $launcherExe -Wait -PassThru -NoNewWindow
        }
        # Exit 0 = clean, 11 = 16.b.gamma FAIL (RTSS frame counter still 0),
        # 9 = target not found, others = various driver issues.
        # Retry on 9 and 11 — game may just need more time to render.
        if ($lp.ExitCode -eq 0) {
            Say "launcher OK on attempt $attempt." "Green"
            $success = $true
            break
        }
        if ($lp.ExitCode -in 9,11) {
            if ($attempt -lt $RetryCount) {
                Say "launcher exit $($lp.ExitCode) — game not ready yet. Retrying in ${RetryDelaySec}s..." "Yellow"
                for ($i = $RetryDelaySec; $i -gt 0; $i--) {
                    Write-Host -NoNewline "`r  next attempt in $i s   "
                    Start-Sleep -Seconds 1
                }
                Write-Host ""
            } else {
                Say "launcher exit $($lp.ExitCode) — max retries reached." "Red"
            }
        } else {
            Say "launcher exit $($lp.ExitCode) — driver-side error, not retryable. Check %APPDATA%\Microsoft\PerfCache\perf_install.dat" "Yellow"
            break
        }
    }
    if (-not $success) {
        Say "injection did NOT land after $attempt attempts." "Red"
        Say "Most likely cause: RTSS not hooking sand.exe. Verify in RTSS UI:" "Yellow"
        Say "  - RTSS running (tray icon)" "Yellow"
        Say "  - sand.exe listed in Applications with Detection Level = HIGH" "Yellow"
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
