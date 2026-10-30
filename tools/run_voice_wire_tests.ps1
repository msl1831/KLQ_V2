param([string]$Compiler='C:\Program Files (x86)\Microsoft Visual Studio\VC98\bin\CL.EXE')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out=Join-Path $repo 'build/voice_wire_20261030'
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldLib=$env:LIB;$oldInclude=$env:INCLUDE
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location $out
try {
 $env:LIB=Join-Path $vc 'Lib';$env:INCLUDE=Join-Path $vc 'Include'
 $exe=Join-Path $out 'voice_wire.exe'
 & $Compiler /nologo /TC /W3 /O2 /DKLQ_VOICE_TEST "/I$repo/IR/tests/mocks/host" "/I$repo/sensor_boot/tests/host" (Join-Path $repo 'tools/test_voice_wire.c') "/Fe$exe"
 if($LASTEXITCODE -ne 0){throw 'Voice wire compile failed'}
 & $exe
 if($LASTEXITCODE -ne 0){throw 'Voice wire tests failed'}
} finally {Pop-Location;$env:LIB=$oldLib;$env:INCLUDE=$oldInclude}
