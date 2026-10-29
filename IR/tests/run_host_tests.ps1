param([string]$Compiler = 'C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE')
$ErrorActionPreference = 'Stop'
$testRoot = $PSScriptRoot
$irRoot = Split-Path -Parent $testRoot
$outDir = Join-Path $testRoot 'build'
if (-not (Test-Path -LiteralPath $Compiler)) { throw "Native C compiler not found: $Compiler" }
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$vcRoot = Split-Path -Parent (Split-Path -Parent $Compiler)
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
try {
    $env:INCLUDE = (Join-Path $vcRoot 'Include')
    $env:LIB = (Join-Path $vcRoot 'Lib')
    $includes = @('/I', (Join-Path $testRoot 'mocks'), '/I', (Join-Path $irRoot 'firmware\app\Inc'), '/I', (Join-Path $irRoot 'firmware\common'), '/I', (Join-Path $irRoot 'firmware\platform\py32f002b'))
    $groups = @(
        @{Name='sampling'; Sources=@((Join-Path $testRoot 'test_sampling.c'), (Join-Path $irRoot 'firmware\app\Src\ir_sensor.c'))},
        @{Name='protocol'; Sources=@((Join-Path $testRoot 'test_protocol.c'), (Join-Path $irRoot 'firmware\app\Src\device_service.c'), (Join-Path $irRoot 'firmware\common\frame_codec.c'))},
        @{Name='host_ports'; Sources=@((Join-Path $testRoot 'test_host_ports.c'), (Join-Path $irRoot 'firmware\common\frame_codec.c'))}
    )
    $hostSource = Join-Path (Split-Path -Parent $irRoot) 'bootloader\external_ports.c'
    Copy-Item -LiteralPath $hostSource -Destination (Join-Path $outDir 'external_ports_source.c') -Force
    foreach ($group in $groups) {
        $groupIncludes = $includes
        if ($group.Name -eq 'host_ports') {
            $groupIncludes = @('/I', (Join-Path $testRoot 'mocks\host')) + $includes + @('/I', $outDir, '/I', (Join-Path (Split-Path -Parent $irRoot) 'bootloader'))
        }
        $objects = @()
        foreach ($source in $group.Sources) {
            $object = Join-Path $outDir ($group.Name + '_' + [IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
            & $Compiler '/nologo' '/W3' '/TC' @groupIncludes '/c' $source "/Fo$object"
            if ($LASTEXITCODE -ne 0) { throw "Host compilation failed: $source" }
            $objects += $object
        }
        $exe = Join-Path $outDir ($group.Name + '.exe')
        & $Compiler '/nologo' @objects "/Fe$exe"
        if ($LASTEXITCODE -ne 0) { throw "Host linking failed: $($group.Name)" }
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "Host test failed: $($group.Name)" }
    }
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}
