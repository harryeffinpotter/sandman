$vsPath  = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$sdkInc  = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$sdkLib  = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"

$env:PATH = "$vsPath\bin\Hostx64\x64;$env:PATH"

Write-Host "`n=== Kernel Driver Build ===" -ForegroundColor Cyan

# --- Compile driver.c ---

Write-Host "`n[1/2] Compiling driver.c..." -ForegroundColor Cyan

cl /kernel /GS- /W3 /Ox `
    /D _AMD64_ /D _WIN64 /D NTDDI_VERSION=0x0A000000 /D _KERNEL_MODE `
    /I "kerneldriver\src" `
    /I "common" `
    /I "$sdkInc\km" `
    /I "$sdkInc\shared" `
    /I "$vsPath\include" `
    /I "$sdkInc\ucrt" `
    /c kerneldriver\src\driver.c `
    /Fo:driver.obj

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nCompilation FAILED" -ForegroundColor Red
    exit 1
}

# --- Link kerneldriver.sys ---

Write-Host "`n[2/2] Linking kerneldriver.sys..." -ForegroundColor Cyan

link /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /OUT:kerneldriver.sys `
    /LIBPATH:"$sdkLib\km\x64" `
    /LIBPATH:"$vsPath\lib\x64" `
    ntoskrnl.lib hal.lib `
    driver.obj

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nLink FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "`nBuild succeeded: kerneldriver.sys" -ForegroundColor Green

# --- Cleanup intermediate files ---

Remove-Item -Force driver.obj -ErrorAction SilentlyContinue
