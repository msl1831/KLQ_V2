param(
    [string]$ProbeUid = "07000001002700594d00000c4e54514aa5a5a5a597969908",
    [string]$Image = "UL/firmware/app/build/ul_sensor_app.hex"
)

$ErrorActionPreference = "Stop"
$Workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$PyOcd = Join-Path $Workspace "UL/.venv/Scripts/pyocd.exe"
$Pack = Join-Path $Workspace "UL/firmware/vendor/PY32F002B_Firmware/Packs/MDK/Puya.PY32F0xx_DFP.1.2.6.pack"
$ImagePath = Join-Path $Workspace $Image

if (-not (Test-Path -LiteralPath $PyOcd)) {
    throw "pyOCD not found: $PyOcd"
}
if (-not (Test-Path -LiteralPath $Pack)) {
    throw "PY32 device pack not found: $Pack"
}
if (-not (Test-Path -LiteralPath $ImagePath)) {
    throw "Sensor image not found: $ImagePath"
}

Write-Host "Target : PY32F002Bx5 UL sensor (not KLQ host)"
Write-Host "Probe  : $ProbeUid"
Write-Host "Image  : $ImagePath"
& $PyOcd flash -t py32f002bx5 --pack $Pack -u $ProbeUid -f 1000000 -M halt -e sector $ImagePath
if ($LASTEXITCODE -ne 0) {
    throw "Sensor flashing failed with exit code $LASTEXITCODE"
}
