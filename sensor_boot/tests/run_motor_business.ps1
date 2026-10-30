param([string]$Compiler='C:\Program Files (x86)\Microsoft Visual Studio\VC98\bin\CL.EXE',[string]$OutDirectory='')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out=Join-Path $PSScriptRoot 'build\motor_business'
if($OutDirectory){$out=[IO.Path]::GetFullPath($OutDirectory)}
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldLib=$env:LIB;$oldInclude=$env:INCLUDE
New-Item -ItemType Directory -Path $out -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'bootloader\external_ports.c') -Destination (Join-Path $out 'external_ports_source.c') -Force
Push-Location $out
try {
    $env:LIB=Join-Path $vc 'Lib';$env:INCLUDE=Join-Path $vc 'Include'
    $exe=Join-Path $out 'test_motor_business.exe'
    & $Compiler /nologo /W3 /O2 "/I$repo\IR\tests\mocks\host" "/I$repo\sensor_boot\tests\host" "/I$repo\bootloader" "/I$out" (Join-Path $PSScriptRoot 'test_motor_business.c') "/Fe$exe"
    if($LASTEXITCODE -ne 0){throw 'Motor business compile failed'}
    & $exe
    if($LASTEXITCODE -ne 0){throw 'Motor business tests failed'}
} finally {Pop-Location;$env:LIB=$oldLib;$env:INCLUDE=$oldInclude}
