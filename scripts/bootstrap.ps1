$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
Set-Location $RootDir

$python = Get-Command py -ErrorAction SilentlyContinue
if ($null -eq $python) {
    Write-Error "py launcher not found. Install Python 3.11+ and rerun this script."
}

py -3 scripts/dev.py setup
