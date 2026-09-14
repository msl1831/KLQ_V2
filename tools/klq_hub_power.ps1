# One-machine diagnostic for the observed KLQ hub, not a global USB power change.
param([switch]$Restore)
$ErrorActionPreference = 'Stop'
$klqProject = Split-Path -Parent $PSScriptRoot
$klqLog = Join-Path $klqProject 'build/usb_hub_power.log'
$klqBackup = Join-Path $klqProject 'build/usb_hub_power_original.json'
$klqHub = 'USB\VID_05E3&PID_0610\5&521a615&0&5'
$klqPowerInstance = $klqHub + '_0'
New-Item -ItemType Directory -Path (Split-Path -Parent $klqLog) -Force | Out-Null
try {
    $klqPower = @(Get-CimInstance -Namespace root/wmi -ClassName MSPower_DeviceEnable | Where-Object { $_.InstanceName -ieq $klqPowerInstance })
    if ($klqPower.Count -ne 1) { throw 'The observed KLQ hub power instance was not found uniquely.' }
    if ($Restore) {
        $klqSaved = Get-Content -LiteralPath $klqBackup -Raw | ConvertFrom-Json
        if ($klqSaved.InstanceName -ine $klqPowerInstance) { throw 'Saved hub instance differs.' }
        $klqEnable = [bool]$klqSaved.Enable
    } else {
        if (-not (Test-Path -LiteralPath $klqBackup)) {
            @{ InstanceName=$klqPowerInstance; Enable=[bool]$klqPower[0].Enable } | ConvertTo-Json | Set-Content -LiteralPath $klqBackup -Encoding UTF8
        }
        $klqEnable = $false
    }
    "Time: $(Get-Date -Format o)`nHub: $klqHub`nPrevious Enable: $($klqPower[0].Enable)`nRequested Enable: $klqEnable" | Set-Content -LiteralPath $klqLog -Encoding UTF8
    $klqPower[0] | Set-CimInstance -Property @{Enable=$klqEnable}
    & pnputil.exe /restart-device $klqHub 2>&1 | Out-File -LiteralPath $klqLog -Append -Encoding UTF8
    "Restart exit code: $LASTEXITCODE" | Add-Content -LiteralPath $klqLog
    Get-CimInstance -Namespace root/wmi -ClassName MSPower_DeviceEnable | Where-Object { $_.InstanceName -ieq $klqPowerInstance } | Select-Object InstanceName,Enable | Format-List | Out-File -LiteralPath $klqLog -Append -Encoding UTF8
    'Completed.' | Add-Content -LiteralPath $klqLog
} catch {
    $_ | Out-String | Add-Content -LiteralPath $klqLog
    exit 1
}
