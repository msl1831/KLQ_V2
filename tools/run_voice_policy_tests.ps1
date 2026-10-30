param([string]$Compiler='C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out=Join-Path $repo 'build\voice_policy_20261030'
$tests=Join-Path $PSScriptRoot 'voice_policy_tests'
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location $out
try {
 $env:INCLUDE=Join-Path $vc 'Include';$env:LIB=Join-Path $vc 'Lib'
 $exe=Join-Path $out 'voice_policy.exe'
 & $Compiler /nologo /TC /W3 /O2 "/I$repo\sensor_boot\tests\host" "/I$repo\bootloader" (Join-Path $repo 'bootloader\voice_control.c') (Join-Path $tests 'test_voice_policy.c') "/Fe$exe"
 if($LASTEXITCODE -ne 0){throw 'Voice policy compile failed'}
 & $exe
 if($LASTEXITCODE -ne 0){throw 'Voice policy tests failed'}
}finally{Pop-Location;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
