param([string]$Compiler = 'C:\Program Files (x86)\Microsoft Visual Studio\VC98\bin\CL.EXE')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out=Join-Path $root 'tests\build'
$vcRoot=Split-Path (Split-Path $Compiler -Parent) -Parent
$previousLib=$env:LIB
$env:LIB=Join-Path $vcRoot 'Lib'
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location $out
try {
    foreach ($test in @('safety','update','app')) {
        $source=Join-Path $root "tests\test_$test.c"
        $impl=if ($test -eq 'update') { Join-Path $root 'firmware\boot\update.c' } else { Join-Path $root 'firmware\app\motor_safety.c' }
        $exe=Join-Path $out $(if ($test -eq 'update') { 'motor_core_tests.exe' } else { "motor_${test}_tests.exe" })
        $rsp=Join-Path $out "test_$test.rsp"
        $argsText=@('/nologo','/W3','/O2',('/I"'+$root+'\tests\host"'),('/I"'+$vcRoot+'\Include"'),('"'+$source+'"'),('"'+$impl+'"'),('/Fe"'+$exe+'"'))
        if ($test -eq 'app') { $argsText+='/DKLQ_BOOT_APP' }
        [IO.File]::WriteAllLines($rsp,$argsText,[Text.Encoding]::ASCII)
        & $Compiler "@$rsp"
        if ($LASTEXITCODE -ne 0) { throw "$test test build failed" }
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "$test test failed" }
        Write-Output "$test tests PASS"
    }
} finally { Pop-Location; $env:LIB=$previousLib }
