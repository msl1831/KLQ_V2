param([string]$Compiler='C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE')
$ErrorActionPreference='Stop'
$testRoot=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $testRoot '..\..')).Path
$out=Join-Path $testRoot 'build\pipeline'
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldInclude=$env:INCLUDE
$oldLib=$env:LIB
New-Item -ItemType Directory -Path $out -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'bootloader\protocol.c') -Destination (Join-Path $out 'protocol_source.c') -Force
Copy-Item -LiteralPath (Join-Path $repo 'bootloader\protocol.h') -Destination (Join-Path $out 'protocol.h') -Force
Copy-Item -LiteralPath (Join-Path $repo 'bootloader\external_ports.c') -Destination (Join-Path $out 'external_ports_source.c') -Force
Push-Location $out
try {
    $env:INCLUDE=Join-Path $vc 'Include'
    $env:LIB=Join-Path $vc 'Lib'
    $exe=Join-Path $out 'bridge_pipeline.exe'
    & $Compiler '/nologo' '/W3' '/O2' '/DKLQ_DEMO_APP' "/I$testRoot\usb_host" "/I$out" "/I$repo\IR\tests\mocks\host" "/I$repo\bootloader" (Join-Path $testRoot 'test_bridge_pipeline.c') "/Fe$exe"
    if($LASTEXITCODE -ne 0){throw 'Full pipeline simulator build failed'}
} finally {Pop-Location;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
& (Join-Path $repo '.venv\Scripts\python.exe') -m unittest discover -s $testRoot -p test_bridge_pipeline.py -v
if($LASTEXITCODE -ne 0){throw 'Full pipeline integration tests failed'}
