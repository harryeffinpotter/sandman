# RENAME_DIR.ps1 — one-shot: renames the project directory from sand_cheat
# to something innocuous. Run this AFTER closing Claude Code / any editor
# that has files open in this tree, because Windows locks the folder if
# any process has a handle on it.
#
# Usage:
#   cd C:\Users\ysg\projects
#   powershell -ExecutionPolicy Bypass -File .\sand_cheat\RENAME_DIR.ps1
#
# What it does:
#   1. Renames C:\Users\ysg\projects\sand_cheat -> C:\Users\ysg\projects\WinPerfHelper
#   2. Updates every hardcoded path in build scripts to point at the new location
#   3. Force-rebuilds everything from the new location
#   4. Leaves git in a clean state — one commit describing the rename

param(
    [string]$NewName = "WinPerfHelper"
)

$ErrorActionPreference = "Stop"
$root  = "C:\Users\ysg\projects"
$oldDir = Join-Path $root "sand_cheat"
$newDir = Join-Path $root $NewName

Write-Host "`n=== Project directory rename ===" -ForegroundColor Cyan
Write-Host "  $oldDir"
Write-Host "  -> $newDir"

if (-not (Test-Path $oldDir)) {
    Write-Host "sand_cheat not found — already renamed?" -ForegroundColor Yellow
    exit 0
}
if (Test-Path $newDir) {
    Write-Host "target already exists — aborting" -ForegroundColor Red
    exit 1
}

# Purge IDE workspace caches so nothing holds a handle
Remove-Item -Force -Recurse (Join-Path $oldDir ".vs") -ErrorAction SilentlyContinue

# Physical move
Rename-Item -Path $oldDir -NewName $NewName

# Update every hardcoded path (build scripts + fallback strings)
$oldPathBackslash = "C:\\Users\\ysg\\projects\\sand_cheat\\"
$newPathBackslash = "C:\\Users\\ysg\\projects\\$NewName\\"
$oldPathForward   = "C:/Users/ysg/projects/sand_cheat"
$newPathForward   = "C:/Users/ysg/projects/$NewName"

$targets = Get-ChildItem -Path $newDir -Recurse -Include *.ps1,*.cpp,*.h,*.md,*.txt,*.json,*.slnx,*.sln,*.rc,*.bat -File -ErrorAction SilentlyContinue
foreach ($t in $targets) {
    $content = Get-Content -Raw -LiteralPath $t.FullName -ErrorAction SilentlyContinue
    if ($null -eq $content) { continue }
    if ($content.Contains("sand_cheat")) {
        $content = $content.Replace($oldPathBackslash, $newPathBackslash)
        $content = $content.Replace($oldPathForward,   $newPathForward)
        # residual "sand_cheat" tokens in filenames/comments — turn them
        # into the new project token so nothing user-visible says "cheat".
        $content = $content.Replace("sand_cheat", $NewName)
        Set-Content -LiteralPath $t.FullName -Value $content -NoNewline
    }
}

Push-Location $newDir
try {
    Write-Host "`nRebuilding at new location..." -ForegroundColor Cyan
    & .\build_all.ps1
    if ($LASTEXITCODE -ne 0) { throw "rebuild failed after rename" }

    Write-Host "`nCommitting rename..." -ForegroundColor Cyan
    & git add -A
    & git commit -m "opsec: rename project directory sand_cheat -> $NewName"
} finally { Pop-Location }

Write-Host "`nDone. Project now lives at $newDir" -ForegroundColor Green
Write-Host "Anything you had open in an editor pointing at the old path — reopen from the new one."
