param(
    [string]$ArmccBin = $(if ($env:KLQ_ARMCC) { $env:KLQ_ARMCC } else { 'C:\Keil_v5\ARM\ARMCC\bin' })
)

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'ram_unprotect.c'
$out = Join-Path $PSScriptRoot '..\firmware\boot\build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$object = Join-Path $out 'ram_unprotect.o'
$axf = Join-Path $out 'ram_unprotect.axf'
$bin = Join-Path $out 'ram_unprotect.bin'
& (Join-Path $ArmccBin 'armcc.exe') --cpu Cortex-M0 --thumb --c99 -O2 -Ospace -c $source -o $object
if ($LASTEXITCODE -ne 0) { throw 'SRAM routine compile failed' }
& (Join-Path $ArmccBin 'armlink.exe') $object --ro-base 0x20000000 --entry unlock_entry --no_scanlib --map --list (Join-Path $out 'ram_unprotect.map') -o $axf
if ($LASTEXITCODE -ne 0) { throw 'SRAM routine link failed' }
& (Join-Path $ArmccBin 'fromelf.exe') --bin --output $bin $axf
if ($LASTEXITCODE -ne 0) { throw 'SRAM routine BIN generation failed' }
Write-Output "SRAM routine: $bin ($((Get-Item -LiteralPath $bin).Length) bytes)"
