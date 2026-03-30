$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
Set-Location $RootDir

$python = Get-Command py -ErrorAction SilentlyContinue
if ($null -eq $python) {
    Write-Error "py launcher not found. Install Python 3.11+ and rerun this script."
}

$msysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$bashExe = Join-Path $msysRoot "usr\bin\bash.exe"

if (-not (Test-Path $bashExe)) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        Write-Error "winget not found. Install MSYS2 manually or install winget and rerun this script."
    }

    winget install -e --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements
}

if (-not (Test-Path $bashExe)) {
    Write-Error "MSYS2 installation not found at $msysRoot after install attempt."
}

& $bashExe -lc "pacman -Sy --noconfirm --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2"

py -3 scripts/dev.py setup
