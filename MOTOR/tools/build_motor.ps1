param(
    [string]$OutputDir = '',
    [switch]$Bootloader,
    [ValidateSet('boot','app')][string]$Target = 'app',
    [string]$ArmccBin = $(if ($env:KLQ_ARMCC) { $env:KLQ_ARMCC } else { 'C:\Keil_v5\ARM\ARMCC\bin' }),
    [string]$SdkRoot = ''
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\firmware')).Path
if (!$SdkRoot) { $SdkRoot=Join-Path $root '..\..\UL\firmware\vendor\PY32F002B_Firmware' }
$sdk=(Resolve-Path -LiteralPath $SdkRoot).Path
$cmsis=Join-Path $sdk 'Drivers\CMSIS'
$device=Join-Path $cmsis 'Device\PY32F0xx\Include'
$out=if ($OutputDir) { [IO.Path]::GetFullPath($OutputDir) } else { Join-Path $root "$Target\build" }
New-Item -ItemType Directory -Path $out -Force | Out-Null
$armcc=Join-Path $ArmccBin 'armcc.exe'
$armasm=Join-Path $ArmccBin 'armasm.exe'
$armlink=Join-Path $ArmccBin 'armlink.exe'
$fromelf=Join-Path $ArmccBin 'fromelf.exe'
foreach ($tool in @($armcc,$armasm,$armlink,$fromelf)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Missing compiler tool: $tool" }
}
$includes=@('-I',(Join-Path $cmsis 'Include'),'-I',$device,'-I',(Join-Path $root 'platform'),'-I',(Join-Path $root 'boot'),'-I',(Join-Path $root 'app'))
$sources=@((Join-Path $root "$Target\main.c"),(Join-Path $root 'platform\motor_hw.c'),(Join-Path $sdk 'Templates\PY32F002Bxx_Templates_LL\Src\system_py32f002b.c'))
if ($Target -eq 'boot') {
    $sources+=@((Join-Path $root 'boot\boot_hw.c'),(Join-Path $root 'boot\update.c'))
} else { $sources+=Join-Path $root 'app\motor_safety.c' }
$objects=@()
if ($Bootloader) {
    if ($Target -ne 'app') { throw '-Bootloader selects the shared-boot application; use sensor_boot/tools/build.ps1 to build boot.' }
    $includes += @('-I',(Join-Path $root '..\..\sensor_boot\include'))
    $sources += Join-Path $root '..\..\sensor_boot\src\app_identity.c'
}
foreach ($source in $sources) {
    $obj=Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source))+'.o')
    $feature=if ($Bootloader) { @('-D','KLQ_BOOT_APP','-D','SENSOR_MOTOR') } else { @() }
    & $armcc --cpu Cortex-M0plus --thumb --c99 --split_sections --diag_warning=Pe223 -O2 -Ospace -D PY32F002Bx5 -D SWD_DELAY @feature @includes -c $source -o $obj
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects+=$obj
}
$startup=Join-Path $sdk 'Templates\PY32F002Bxx_Templates_LL\MDK-ARM\startup_py32f002bxx.s'
$startupObj=Join-Path $out 'startup.o'
& $armasm --cpu Cortex-M0plus $startup -o $startupObj
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$axf=Join-Path $out "motor_$Target.axf"
$scatter=if ($Bootloader) { Join-Path $root '..\..\sensor_boot\linker\app.sct' } else { Join-Path $root "linker\$Target.sct" }
& $armlink @($startupObj) @objects --scatter $scatter --map --xref --callgraph --symbols --info sizes,totals,unused --list (Join-Path $out "motor_$Target.map") -o $axf
if ($LASTEXITCODE -ne 0) { throw 'Link failed or exceeds Flash / 3 KiB SRAM region' }
& $fromelf --bin --output (Join-Path $out "motor_$Target.bin") $axf
if ($LASTEXITCODE -ne 0) { throw 'BIN generation failed' }
& $fromelf --i32combined --output (Join-Path $out "motor_$Target.hex") $axf
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
& $fromelf --text -z $axf
if ($LASTEXITCODE -ne 0) { throw 'Size report failed' }
