param(
    [string]$SdkRoot = 'F:\HK32\HK32F030Mxx系列MCU开发资料（2022-09-16更新）\3、片外设库、工程模板、参考例程\1、片外设库\01 HK32F030MF4P6_Fwlib-Template',
    [string]$ArmccBin = $(if ($env:KLQ_ARMCC) { $env:KLQ_ARMCC } else { 'C:\Keil_v5\ARM\ARMCC\bin' })
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\firmware')).Path
$sdk = (Resolve-Path -LiteralPath $SdkRoot).Path
$cmsis = Join-Path $sdk 'Source\Libraries\CMSIS'
$device = Join-Path $cmsis 'HK32F030M'
$out = Join-Path $root 'boot\build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$armcc = Join-Path $ArmccBin 'armcc.exe'
$armasm = Join-Path $ArmccBin 'armasm.exe'
$armlink = Join-Path $ArmccBin 'armlink.exe'
$fromelf = Join-Path $ArmccBin 'fromelf.exe'
foreach ($tool in @($armcc,$armasm,$armlink,$fromelf)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing compiler tool: $tool" }
}
$includes = @('-I',(Join-Path $cmsis 'CM0\Core'),'-I',(Join-Path $device 'Include'),'-I',(Join-Path $sdk 'Source\Libraries\HK32F030M_Lib\inc'),'-I',(Join-Path $sdk 'Source\User'),'-I',(Join-Path $root 'boot'))
$sources = @(
    (Join-Path $root 'boot\main.c'),
    (Join-Path $root 'boot\boot_hw.c'),
    (Join-Path $root 'boot\update.c'),
    (Join-Path $device 'Source\system_hk32f030m.c')
)
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $out (([System.IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
    & $armcc --cpu Cortex-M0 --thumb --c99 --split_sections -O2 -Ospace -D HK32F030MF4U6 @includes -c $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}
$startup = Join-Path $device 'Source\KEIL_Startup_hk32f030m.s'
$startupObject = Join-Path $out 'startup.o'
& $armasm --cpu Cortex-M0 $startup -o $startupObject
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$axf = Join-Path $out 'motor_boot.axf'
$map = Join-Path $out 'motor_boot.map'
$scatter = Join-Path $root 'linker\boot.sct'
& $armlink @($startupObject) @objects --scatter $scatter --map --info sizes,totals,unused --list $map -o $axf
if ($LASTEXITCODE -ne 0) { throw 'Link failed or exceeds 4.5 KiB boot / 2 KiB SRAM region' }
& $fromelf --bin --output (Join-Path $out 'motor_boot.bin') $axf
if ($LASTEXITCODE -ne 0) { throw 'BIN generation failed' }
& $fromelf --i32combined --output (Join-Path $out 'motor_boot.hex') $axf
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
& $fromelf --text -z $axf
if ($LASTEXITCODE -ne 0) { throw 'Size report failed' }
