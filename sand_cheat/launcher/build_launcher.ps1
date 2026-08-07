$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$sdkInc = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$sdkLib = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"
$rcExe  = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe"

$env:PATH = "$vsPath\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "$vsPath\include;$sdkInc\ucrt;$sdkInc\um;$sdkInc\shared;$PWD\src;$PWD\common"
$env:LIB = "$vsPath\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"

Write-Host "`n=== Sand Launcher Build ===" -ForegroundColor Cyan

# --- Encrypt WinIo64.sys -> winio64_enc.bin ---

Write-Host "`n[1/3] Encrypting WinIo64.sys..." -ForegroundColor Cyan

function Encrypt-RollingXor([byte[]]$data) {
    [byte[]]$key = @(0x46, 0x28, 0x64)
    [uint32]$v7  = 16
    [uint32]$v9  = 329
    [uint32]$v10 = 16
    for ($i = 0; $i -lt $data.Length; $i++) {
        $v7  = ($v10 * ($key[$i % 3] + $v9 + 8) + ($v7 -shr 10)) -band 0xFFFFFFFF
        $v10 = $v7 -band 0xFF
        $v9  = $v7 -band 0xFF
        $data[$i] = $data[$i] -bxor [byte]($v7 -band 0xFF)
    }
    return $data
}

$winioPath  = "C:\Users\ysg\projects\Vulnerable Drivers\C__Users_user_Desktop_WinIo64_Sys\C__Users_user_Desktop_WinIo64.Sys"
$kdrvPath   = "$PWD\kerneldriver.sys"

foreach ($pair in @(
    @($winioPath, "$PWD\winio64_enc.bin", "WinIo64.sys"),
    @($kdrvPath,  "$PWD\kerneldriver_enc.bin", "kerneldriver.sys")
)) {
    $src, $dst, $label = $pair
    if (-not (Test-Path $src)) {
        Write-Host "ERROR: $label not found at: $src" -ForegroundColor Red
        exit 1
    }
    [byte[]]$raw = [System.IO.File]::ReadAllBytes($src)
    $enc = Encrypt-RollingXor $raw
    [System.IO.File]::WriteAllBytes($dst, $enc)
    Write-Host "  -> $([System.IO.Path]::GetFileName($dst)) ($($raw.Length) bytes)" -ForegroundColor Green
}

# --- Compile RC ---

Write-Host "`n[2/3] Compiling launcher.rc..." -ForegroundColor Cyan

& $rcExe /fo launcher.res src\launcher.rc

if ($LASTEXITCODE -ne 0) {
    Write-Host "RC compilation FAILED" -ForegroundColor Red
    exit 1
}

# --- Compile + Link ---

Write-Host "`n[3/3] Compiling and linking..." -ForegroundColor Cyan

$sources = @(
    "src\byovd.cpp"
    "src\cmdchannel.cpp"
    "src\crypto.cpp"
    "src\forensic_cleanup.cpp"
    "src\invoke_stage2.cpp"
    "src\ioctl.cpp"
    "src\kern_map.cpp"
    "src\kern_scan.cpp"
    "src\main.cpp"
    "src\map_stage2.cpp"
    "src\ntapi.cpp"
    "src\pagewalk.cpp"
    "src\parse_stage2.cpp"
    "src\resolve_imports.cpp"
    "src\rtss_inject.cpp"
    "src\syscall_hijack.cpp"
    "src\ui.cpp"
)

$libs = @(
    "kernel32.lib"
    "user32.lib"
    "advapi32.lib"
    "shell32.lib"
    "ole32.lib"
    "comdlg32.lib"
    "ntdll.lib"
    "gdi32.lib"
)

cl /EHsc /O2 /MT /std:c++17 /Isrc /Icommon @sources launcher.res /Fe:RTSSDriverSvc.exe /link @libs

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nBuild FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "`nBuild succeeded: RTSSDriverSvc.exe" -ForegroundColor Green

# --- Cleanup intermediate files ---

Remove-Item -Force *.obj, launcher.res -ErrorAction SilentlyContinue
