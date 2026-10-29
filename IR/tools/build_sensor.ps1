param(
    [switch]$Bootloader,
    [string]$OutputDir = '',
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Release',
    [string]$ArmccBin = $(if ($env:KLQ_ARMCC) { $env:KLQ_ARMCC } else { 'C:\Keil_v5\ARM\ARMCC\bin' }),
    [string]$VendorRoot = $(if ($env:KLQ_PY32_SDK) { $env:KLQ_PY32_SDK } else {
        Join-Path $PSScriptRoot '..\..\UL\firmware\vendor\PY32F002B_Firmware'
    })
)

$ErrorActionPreference = 'Stop'
$firmwareRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\firmware')).Path
if (-not (Test-Path -LiteralPath $VendorRoot -PathType Container)) {
    throw 'Missing PY32F002B SDK. Restore UL/firmware/vendor/PY32F002B_Firmware at 394627d99085c5eae72142503b0405b84428bb20 or pass -VendorRoot / KLQ_PY32_SDK.'
}
$sdkRoot = (Resolve-Path -LiteralPath $VendorRoot).Path
$outputDir = if ($OutputDir) { [IO.Path]::GetFullPath($OutputDir) } else { Join-Path $firmwareRoot 'app\build' }

$compiler = Join-Path $ArmccBin 'armcc.exe'
$assembler = Join-Path $ArmccBin 'armasm.exe'
$linker = Join-Path $ArmccBin 'armlink.exe'
$fromelf = Join-Path $ArmccBin 'fromelf.exe'
foreach ($tool in @($compiler, $assembler, $linker, $fromelf)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing Keil tool: $tool" }
}

$includeDirs = @(
    (Join-Path $firmwareRoot 'app\Inc'),
    (Join-Path $firmwareRoot 'common'),
    (Join-Path $firmwareRoot 'boards\ir_v1'),
    (Join-Path $firmwareRoot 'platform\py32f002b'),
    (Join-Path $sdkRoot 'Drivers\CMSIS\Include'),
    (Join-Path $sdkRoot 'Drivers\CMSIS\Device\PY32F0xx\Include'),
    (Join-Path $sdkRoot 'Drivers\PY32F002B_HAL_Driver\Inc')
)
$sources = @(
    (Join-Path $firmwareRoot 'app\Src\main.c'),
    (Join-Path $firmwareRoot 'app\Src\py32f002b_it.c'),
    (Join-Path $firmwareRoot 'app\Src\ir_sensor.c'),
    (Join-Path $firmwareRoot 'app\Src\device_service.c'),
    (Join-Path $firmwareRoot 'common\frame_codec.c'),
    (Join-Path $firmwareRoot 'boards\ir_v1\board.c'),
    (Join-Path $firmwareRoot 'platform\py32f002b\uart.c'),
    (Join-Path $sdkRoot 'Templates\PY32F002Bxx_Templates_LL\Src\system_py32f002b.c'),
    (Join-Path $sdkRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_gpio.c'),
    (Join-Path $sdkRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_rcc.c'),
    (Join-Path $sdkRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_usart.c'),
    (Join-Path $sdkRoot 'Drivers\PY32F002B_HAL_Driver\Src\py32f002b_ll_utils.c')
)
$startup = Join-Path $sdkRoot 'Templates\PY32F002Bxx_Templates_LL\MDK-ARM\startup_py32f002bxx.s'
$scatter = Join-Path $firmwareRoot 'linker\application.sct'
if ($Bootloader) { $scatter = Join-Path $firmwareRoot '..\..\sensor_boot\linker\app.sct' }
foreach ($file in ($sources + @($startup, $scatter))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing source / PY32F002B SDK file: $file" }
}
foreach ($dir in $includeDirs) {
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) { throw "Missing include directory: $dir" }
}

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
foreach ($name in @('ir_sensor_app.axf','ir_sensor_app.map','ir_sensor_app.hex','ir_sensor_app.bin','ir_sensor_app.htm')) {
    $oldOutput = Join-Path $outputDir $name
    if (Test-Path -LiteralPath $oldOutput -PathType Leaf) { Remove-Item -LiteralPath $oldOutput -Force }
}
if ($Bootloader) { $includeDirs += Join-Path $firmwareRoot '..\..\sensor_boot\include' }
$includeArgs = @()
foreach ($dir in $includeDirs) { $includeArgs += @('-I', $dir) }
$flags = @('--cpu','Cortex-M0plus','--c99','--split_sections','--diag_warning=Pe223','-D','PY32F002Bx5','-D','USE_FULL_LL_DRIVER')
if ($Configuration -eq 'Release') { $flags += '-O2' } else { $flags += @('-O1','-g') }
if ($Bootloader) {
    $flags += @('-D','KLQ_BOOT_APP','-D','SWD_DELAY','-D','SENSOR_IR')
    $sources += Join-Path $firmwareRoot '..\..\sensor_boot\src\app_identity.c'
}
$objects = @()
foreach ($source in $sources) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($source)
    if ($source.StartsWith($sdkRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) { $name = "vendor_$name" }
    $object = Join-Path $outputDir "$name.o"
    & $compiler @flags @includeArgs '-c' $source '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}
$startupObject = Join-Path $outputDir 'startup_py32f002bxx.o'
& $assembler '--cpu' 'Cortex-M0plus' $startup '-o' $startupObject
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$objects = @($startupObject) + $objects

$axf = Join-Path $outputDir 'ir_sensor_app.axf'
$map = Join-Path $outputDir 'ir_sensor_app.map'
& $linker @objects '--scatter' $scatter '--map' '--xref' '--callgraph' '--symbols' '--info' 'sizes,totals,unused,veneers' '--list' $map '-o' $axf
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
& $fromelf '--i32combined' '--output' (Join-Path $outputDir 'ir_sensor_app.hex') $axf
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
& $fromelf '--bin' '--output' (Join-Path $outputDir 'ir_sensor_app.bin') $axf
if ($LASTEXITCODE -ne 0) { throw 'BIN generation failed' }
& $fromelf '--text' '-z' $axf
if ($LASTEXITCODE -ne 0) { throw 'Size report failed' }
