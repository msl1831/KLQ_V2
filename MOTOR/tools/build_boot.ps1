param(
    [string]$ArmccBin = $(if ($env:KLQ_ARMCC) { $env:KLQ_ARMCC } else { 'C:\Keil_v5\ARM\ARMCC\bin' }),
    [string]$SdkRoot = ''
)
& (Join-Path $PSScriptRoot 'build_motor.ps1') -Target boot -ArmccBin $ArmccBin -SdkRoot $SdkRoot
