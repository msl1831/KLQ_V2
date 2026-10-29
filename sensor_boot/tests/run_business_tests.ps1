param([string]$Compiler='C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out=Join-Path $PSScriptRoot 'build\business'
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
New-Item -ItemType Directory -Path $out -Force | Out-Null
$sources=@('protocol','external_ports','user_program','mini_python','klq_runtime','power_key','robot_ui')
$files=@()
foreach($name in $sources){$dst=Join-Path $out ($name+'_source.c');Copy-Item -LiteralPath (Join-Path $repo "bootloader\$name.c") -Destination $dst -Force;$files+=$dst}
Copy-Item -LiteralPath (Join-Path $repo 'bootloader\protocol.h') -Destination (Join-Path $out 'protocol.h') -Force
Push-Location $out
try {
    $env:INCLUDE=Join-Path $vc 'Include';$env:LIB=Join-Path $vc 'Lib'
    $exe=Join-Path $out 'business.exe'
    & $Compiler /nologo /TC /W3 /O2 "/I$PSScriptRoot\business_host" "/I$out" "/I$PSScriptRoot\host" "/I$repo\IR\tests\mocks\host" "/I$repo\bootloader" /DKLQ_DEMO_APP @files (Join-Path $PSScriptRoot 'test_business_bridge.c') "/Fe$exe"
    if($LASTEXITCODE -ne 0){throw 'Business/bridge build failed'}
    & $exe
    if($LASTEXITCODE -ne 0){throw 'Business/bridge tests failed'}
}finally{Pop-Location;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
