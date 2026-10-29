param(
    [switch]$Bootloader,
    [string]$OutputDir = '',
    [string]$VendorRoot = $(if ($env:KLQ_PY32_SDK) { $env:KLQ_PY32_SDK } else { Join-Path $PSScriptRoot '..\firmware\vendor\PY32F002B_Firmware' }),
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Debug',
    [string]$ArmccBin = $(if ($env:KLQ_ARMCC) { $env:KLQ_ARMCC } else { 'C:\Keil_v5\ARM\ARMCC\bin' })
)

$ErrorActionPreference = 'Stop'
$firmwareRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\firmware')).Path
$vendorRoot = (Resolve-Path -LiteralPath $VendorRoot).Path
$outputDir = if ($OutputDir) { [IO.Path]::GetFullPath($OutputDir) } else { Join-Path $firmwareRoot 'app\build' }
$toolRoot = $ArmccBin
$compiler = Join-Path $toolRoot 'armcc.exe'
$assembler = Join-Path $toolRoot 'armasm.exe'
$linker = Join-Path $toolRoot 'armlink.exe'
$fromelf = Join-Path $toolRoot 'fromelf.exe'

foreach ($tool in @($compiler, $assembler, $linker, $fromelf)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Missing Keil tool: $tool"
    }
}

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
Get-ChildItem -LiteralPath $outputDir -File -ErrorAction SilentlyContinue |
    Where-Object {
        ($_.Name -eq 'ul_sensor_app.axf') -or
        ($_.Name -eq 'ul_sensor_app.hex') -or
        ($_.Name -eq 'ul_sensor_app.bin') -or
        ($_.Name -eq 'ul_sensor_app.map') -or
        ($_.Name -eq 'ul_sensor_app.htm') -or
        ($_.Extension -in @('.o','.d'))
    } |
    Remove-Item -Force

$includeDirs = @(
    (Join-Path $firmwareRoot 'app\Inc'),
    (Join-Path $firmwareRoot 'common'),
    (Join-Path $firmwareRoot 'boards\ul_v1'),
    (Join-Path $firmwareRoot 'platform\py32f002b'),
    (Join-Path $vendorRoot 'Drivers\CMSIS\Include'),
    (Join-Path $vendorRoot 'Drivers\CMSIS\Device\PY32F0xx\Include'),
    (Join-Path $vendorRoot 'Drivers\PY32F002B_HAL_Driver\Inc')
)
if ($Bootloader) { $includeDirs += Join-Path $firmwareRoot '..\..\sensor_boot\include' }
$includeArgs = @()
foreach ($dir in $includeDirs) {
    $includeArgs += '-I'
    $includeArgs += $dir
}

$sources = @(
    (Join-Path $firmwareRoot 'app\Src\main.c'),
    (Join-Path $firmwareRoot 'app\Src\py32f002b_it.c'),
    (Join-Path $firmwareRoot 'app\Src\cs100a.c'),
    (Join-Path $firmwareRoot 'app\Src\device_service.c'),
    (Join-Path $firmwareRoot 'common\frame_codec.c'),
    (Join-Path $firmwareRoot 'boards\ul_v1\board.c'),
    (Join-Path $firmwareRoot 'platform\py32f002b\uart.c'),
    (Join-Path $vendorRoot 'Templates\PY32F002Bxx_Templates_LL\Src\system_py32f002b.c'),
    (Join-Path $vendorRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_gpio.c'),
    (Join-Path $vendorRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_rcc.c'),
    (Join-Path $vendorRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_tim.c'),
    (Join-Path $vendorRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_usart.c'),
    (Join-Path $vendorRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_utils.c')
)

$commonFlags = @('--cpu','Cortex-M0plus','--c99','--split_sections','--diag_warning=Pe223','-D','PY32F002Bx5','-D','USE_FULL_LL_DRIVER')
if ($Configuration -eq 'Release') {
    $commonFlags += '-O2'
} else {
    $commonFlags += @('-O1','-g')
}

if ($Bootloader) {
    $commonFlags += @('-D','KLQ_BOOT_APP','-D','SWD_DELAY','-D','SENSOR_UL')
    $sources += Join-Path $firmwareRoot '..\..\sensor_boot\src\app_identity.c'
}
$objects = @()
foreach ($source in $sources) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($source)
    if ($source -like '*Drivers*') { $name = "vendor_$name" }
    if ($source -like '*Templates*') { $name = "vendor_$name" }
    $object = Join-Path $outputDir "$name.o"
    & $compiler @commonFlags @includeArgs '-c' $source '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}

$startup = Join-Path $vendorRoot 'Templates\PY32F002Bxx_Templates_LL\MDK-ARM\startup_py32f002bxx.s'
$startupObject = Join-Path $outputDir 'startup_py32f002bxx.o'
& $assembler '--cpu' 'Cortex-M0plus' $startup '-o' $startupObject
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$objects = @($startupObject) + $objects

$axf = Join-Path $outputDir 'ul_sensor_app.axf'
$map = Join-Path $outputDir 'ul_sensor_app.map'
$scatter = Join-Path $firmwareRoot 'linker\application.sct'
if ($Bootloader) { $scatter = Join-Path $firmwareRoot '..\..\sensor_boot\linker\app.sct' }
& $linker @objects '--scatter' $scatter '--map' '--xref' '--callgraph' '--symbols' '--info' 'sizes,totals,unused,veneers' '--list' $map '-o' $axf
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }

& $fromelf '--i32combined' '--output' (Join-Path $outputDir 'ul_sensor_app.hex') $axf
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
& $fromelf '--bin' '--output' (Join-Path $outputDir 'ul_sensor_app.bin') $axf
if ($LASTEXITCODE -ne 0) { throw 'BIN generation failed' }
& $fromelf '--text' '-z' $axf
if ($LASTEXITCODE -ne 0) { throw 'Size report failed' }
