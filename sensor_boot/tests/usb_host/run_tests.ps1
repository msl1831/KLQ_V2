param([string]$Compiler = 'C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE')
$ErrorActionPreference='Stop'
$testRoot=Split-Path $PSScriptRoot -Parent
$repo=(Resolve-Path (Join-Path $testRoot '..\..')).Path
$out=Join-Path $testRoot 'build\usb_bridge'
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldInclude=$env:INCLUDE
$oldLib=$env:LIB
New-Item -ItemType Directory -Path $out -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'bootloader\protocol.c') -Destination (Join-Path $out 'protocol_source.c') -Force
Push-Location $out
try {
    $env:INCLUDE=Join-Path $vc 'Include'
    $env:LIB=Join-Path $vc 'Lib'
    $exe=Join-Path $out 'test_usb_bridge.exe'
    & $Compiler '/nologo' '/W3' '/O2' '/DKLQ_DEMO_APP' "/I$PSScriptRoot" "/I$out" "/I$repo\bootloader" (Join-Path $testRoot 'test_usb_bridge.c') "/Fe$exe"
    if ($LASTEXITCODE -ne 0) { throw 'USB bridge integration test build failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'USB bridge integration tests failed' }
} finally { Pop-Location;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib }
