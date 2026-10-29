param([string]$Compiler = 'C:\Program Files (x86)\Microsoft Visual Studio\VC98\bin\CL.EXE')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$fw = Join-Path $repo 'UL\firmware'
$out = Join-Path $PSScriptRoot 'build'
$vc = Split-Path (Split-Path $Compiler -Parent) -Parent
$oldLib = $env:LIB
$oldInclude = $env:INCLUDE
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location $out
try {
    $env:LIB = Join-Path $vc 'Lib'
    $env:INCLUDE = Join-Path $vc 'Include'
    $exe = Join-Path $out 'boot_entry.exe'
    $argsList = @('/nologo','/W3','/O2','/DKLQ_BOOT_APP','/DSENSOR_UL',
        "/I$repo\sensor_boot\tests\host","/I$fw\boards\ul_v1","/I$fw\app\Inc",
        "/I$fw\common","/I$fw\platform\py32f002b",
        (Join-Path $PSScriptRoot 'test_boot_entry.c'),
        (Join-Path $fw 'app\Src\device_service.c'),(Join-Path $fw 'common\frame_codec.c'),"/Fe$exe")
    & $Compiler @argsList
    if ($LASTEXITCODE -ne 0) { throw 'UL boot entry test build failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'UL boot entry test failed' }
} finally { Pop-Location; $env:LIB = $oldLib; $env:INCLUDE = $oldInclude }
