param(
    [string]$projDir = $args[0]
)

if (-not $projDir) {
    Write-Host "ERROR: Project directory not specified." -ForegroundColor Red
    exit 1
}

$projDir = $projDir.TrimEnd('\')

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

$drivers = @(
    @{
        Source = "C:\Users\ysg\projects\Vulnerable Drivers\iQVW64_SYS\iQVW64.SYS"
        Output = "$projDir\iqvw64_enc.bin"
        Label  = "iQVW64.SYS"
    },
    @{
        Source = "$projDir\kerneldriver.sys"
        Output = "$projDir\kerneldriver_enc.bin"
        Label  = "kerneldriver.sys"
    }
)

foreach ($drv in $drivers) {
    $src = $drv.Source
    $dst = $drv.Output
    $label = $drv.Label

    if (-not (Test-Path $src)) {
        Write-Host "WARNING: $label not found at: $src - skipping" -ForegroundColor Yellow
        continue
    }

    if ((Test-Path $dst) -and ((Get-Item $dst).LastWriteTime -gt (Get-Item $src).LastWriteTime)) {
        Write-Host "$label -> $(Split-Path $dst -Leaf) is up to date, skipping." -ForegroundColor DarkGray
        continue
    }

    [byte[]]$raw = [System.IO.File]::ReadAllBytes($src)
    $enc = Encrypt-RollingXor $raw
    [System.IO.File]::WriteAllBytes($dst, $enc)
    Write-Host "$label -> $(Split-Path $dst -Leaf) ($($raw.Length) bytes encrypted)" -ForegroundColor Green
}
