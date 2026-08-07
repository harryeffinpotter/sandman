#requires -Version 5.0
# launch.ps1 - click-through launcher UI. Double-click this from Explorer;
# picks between DLL mode and external mode with a proper Windows dialog.

$ErrorActionPreference = "Stop"
trap {
    Write-Host "[launch.ps1] FATAL: $_" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkYellow
    Read-Host "press ENTER to close"
    exit 1
}

try { Add-Type -AssemblyName System.Windows.Forms } catch {
    Write-Host "[launch.ps1] System.Windows.Forms unavailable - need .NET Framework 4.x installed." -ForegroundColor Red
    Read-Host "press ENTER to close"
    exit 2
}
Add-Type -AssemblyName System.Drawing

$root = $PSScriptRoot
if (-not $root) {
    Write-Host "[launch.ps1] Can't determine script root (probably launched via cmd pipe). Passing explicit path." -ForegroundColor Red
    Read-Host "press ENTER to close"
    exit 3
}
$runScript = Join-Path $root "run.ps1"
if (-not (Test-Path $runScript)) {
    Write-Host "[launch.ps1] run.ps1 not found at: $runScript" -ForegroundColor Red
    Write-Host "You need the full project directory - LAUNCH.bat can't run standalone." -ForegroundColor Yellow
    Read-Host "press ENTER to close"
    exit 4
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "sand launcher"
$form.Size = New-Object System.Drawing.Size(640, 520)
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.BackColor = [System.Drawing.Color]::FromArgb(30, 30, 34)
$form.ForeColor = [System.Drawing.Color]::White
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

# --- Title
$title = New-Object System.Windows.Forms.Label
$title.Text = "Pick a launch mode"
$title.AutoSize = $true
$title.Location = New-Object System.Drawing.Point(20, 15)
$title.Font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$form.Controls.Add($title)

# ==========================================
# DLL MODE - big button + description
# ==========================================
$dllBox = New-Object System.Windows.Forms.GroupBox
$dllBox.Text = "  Full DLL mode  "
$dllBox.Location = New-Object System.Drawing.Point(20, 55)
$dllBox.Size = New-Object System.Drawing.Size(280, 340)
$dllBox.ForeColor = [System.Drawing.Color]::FromArgb(120, 220, 120)
$form.Controls.Add($dllBox)

$dllDesc = New-Object System.Windows.Forms.Label
$dllDesc.Text = @"
Injects RTSSHelper64.dll into sand.exe.
In-game ImGui menu, all features work.

+  ALL toggles: aim, ESP, sticky/dupe,
   heavy trick, weapon mods, player mods
   (no fall damage, jump/speed multipliers,
   walker fly, always day, etc.)
+  Full stream-proof overlay option
+  HWBP-based interact / IsTooFar bypass
+  Sticky lock, force target, permalock

-  Bigger footprint in sand.exe:
   * HWBP debug registers set
   * Manually-mapped DLL in game process
   * ImGui rendering hooks
-  Higher ban risk if BE flags us
-  Use this on your MAIN account only if
   you're OK gambling
"@
$dllDesc.Location = New-Object System.Drawing.Point(15, 25)
$dllDesc.Size = New-Object System.Drawing.Size(250, 260)
$dllDesc.ForeColor = [System.Drawing.Color]::White
$dllDesc.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$dllBox.Controls.Add($dllDesc)

$dllBtn = New-Object System.Windows.Forms.Button
$dllBtn.Text = "LAUNCH FULL DLL"
$dllBtn.Location = New-Object System.Drawing.Point(15, 290)
$dllBtn.Size = New-Object System.Drawing.Size(250, 40)
$dllBtn.BackColor = [System.Drawing.Color]::FromArgb(50, 100, 50)
$dllBtn.ForeColor = [System.Drawing.Color]::White
$dllBtn.FlatStyle = "Flat"
$dllBtn.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$dllBox.Controls.Add($dllBtn)

# ==========================================
# EXTERNAL MODE - big button + description
# ==========================================
$extBox = New-Object System.Windows.Forms.GroupBox
$extBox.Text = "  External / KWARE mode  "
$extBox.Location = New-Object System.Drawing.Point(320, 55)
$extBox.Size = New-Object System.Drawing.Size(280, 340)
$extBox.ForeColor = [System.Drawing.Color]::FromArgb(120, 180, 255)
$form.Controls.Add($extBox)

$extDesc = New-Object System.Windows.Forms.Label
$extDesc.Text = @"
Installs kernel driver only. Overlay runs
in its own process (PerfMonSvc.exe).
Nothing is injected into sand.exe.

+  ZERO footprint in game process:
   no HWBP, no DLL, no vtable patches,
   no RWX pages
+  Stream-proof by architecture
+  Blends in: PerfMonSvc.exe process +
   %APPDATA%\Microsoft\PerfCache files
+  Lowest ban risk

-  NO duping, NO heavy trick, NO sticky
   (needs in-process hooks)
-  NO aim assist that FEELS good
   (only cursor-nudge via SendInput)
-  ESP + radar + name/pos + basic write
   ops only
"@
$extDesc.Location = New-Object System.Drawing.Point(15, 25)
$extDesc.Size = New-Object System.Drawing.Size(250, 260)
$extDesc.ForeColor = [System.Drawing.Color]::White
$extDesc.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$extBox.Controls.Add($extDesc)

$extBtn = New-Object System.Windows.Forms.Button
$extBtn.Text = "LAUNCH EXTERNAL"
$extBtn.Location = New-Object System.Drawing.Point(15, 290)
$extBtn.Size = New-Object System.Drawing.Size(250, 40)
$extBtn.BackColor = [System.Drawing.Color]::FromArgb(50, 80, 120)
$extBtn.ForeColor = [System.Drawing.Color]::White
$extBtn.FlatStyle = "Flat"
$extBtn.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$extBox.Controls.Add($extBtn)

# ==========================================
# Common option checkboxes
# ==========================================
$optPanel = New-Object System.Windows.Forms.Panel
$optPanel.Location = New-Object System.Drawing.Point(20, 405)
$optPanel.Size = New-Object System.Drawing.Size(580, 65)
$form.Controls.Add($optPanel)

$rebuildCB = New-Object System.Windows.Forms.CheckBox
$rebuildCB.Text = "Rebuild everything first  (slower launch, catches source edits)"
$rebuildCB.Location = New-Object System.Drawing.Point(0, 0)
$rebuildCB.AutoSize = $true
$rebuildCB.ForeColor = [System.Drawing.Color]::LightGray
$optPanel.Controls.Add($rebuildCB)

$skipDrvCB = New-Object System.Windows.Forms.CheckBox
$skipDrvCB.Text = "Skip driver install  (already loaded this boot)"
$skipDrvCB.Location = New-Object System.Drawing.Point(0, 22)
$skipDrvCB.AutoSize = $true
$skipDrvCB.ForeColor = [System.Drawing.Color]::LightGray
$optPanel.Controls.Add($skipDrvCB)

$skipGameCB = New-Object System.Windows.Forms.CheckBox
$skipGameCB.Text = "Skip auto-launching Sand_BE.exe  (game already running)"
$skipGameCB.Location = New-Object System.Drawing.Point(0, 44)
$skipGameCB.AutoSize = $true
$skipGameCB.ForeColor = [System.Drawing.Color]::LightGray
$optPanel.Controls.Add($skipGameCB)

# ==========================================
# Button handlers - build args + fire run.ps1
# ==========================================
function Invoke-Run([bool]$external) {
    $args = @()
    if ($external)      { $args += "-External" }
    if ($rebuildCB.Checked)  { $args += "-Rebuild" }
    if ($skipDrvCB.Checked)  { $args += "-SkipLauncher" }
    if ($skipGameCB.Checked) { $args += "-SkipGame" }

    $form.Close()

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "powershell.exe"
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$runScript`" " + ($args -join " ")
    # Not elevated here - run.ps1 self-elevates. Show its window so LO can
    # watch driver install output.
    $psi.WindowStyle = "Normal"
    [System.Diagnostics.Process]::Start($psi) | Out-Null
}

$dllBtn.Add_Click({ Invoke-Run $false })
$extBtn.Add_Click({ Invoke-Run $true })

[void]$form.ShowDialog()
