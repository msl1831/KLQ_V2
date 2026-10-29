param([string]$Compiler = 'C:\Program Files (x86)\Microsoft Visual Studio\VC98\bin\CL.EXE', [string]$OutputDir = '')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=if ($OutputDir) { $OutputDir } else { Join-Path $PSScriptRoot 'build' }
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldLib=$env:LIB
$oldInclude=$env:INCLUDE
New-Item -ItemType Directory -Path $out -Force | Out-Null
$out=(Resolve-Path -LiteralPath $out).Path
Push-Location $out
try {
    $env:LIB=Join-Path $vc 'Lib'
    $env:INCLUDE=Join-Path $vc 'Include'
    foreach ($kind in @('UL','IR','MOTOR')) {
        $exe=Join-Path $out "$kind.exe"
        & $Compiler /nologo /W3 /O2 "/DSENSOR_$kind" "/I$PSScriptRoot\host" "/I$root\include" (Join-Path $PSScriptRoot 'test_update.c') (Join-Path $root 'src\update.c') "/Fe$exe"
        if ($LASTEXITCODE -ne 0) { throw "$kind update test build failed" }
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "$kind update test failed" }
        Write-Output "$kind update tests PASS"
        $exe=Join-Path $out "protocol_$kind.exe"
        & $Compiler /nologo /W3 /O2 "/DSENSOR_$kind" "/I$PSScriptRoot\host" "/I$root\include" (Join-Path $PSScriptRoot 'test_protocol.c') (Join-Path $root 'src\update.c') "/Fe$exe"
        if ($LASTEXITCODE -ne 0) { throw "$kind protocol test build failed" }
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "$kind protocol test failed" }
        $exe=Join-Path $out "sim_$($kind.ToLowerInvariant()).exe"
        & $Compiler /nologo /W3 /O2 /DBOOT_SIMULATOR "/DSENSOR_$kind" "/I$PSScriptRoot\host" "/I$root\include" (Join-Path $PSScriptRoot 'test_protocol.c') (Join-Path $root 'src\update.c') "/Fe$exe"
        if ($LASTEXITCODE -ne 0) { throw "$kind simulator build failed" }
    }
    $repo=Split-Path $root -Parent
    foreach ($kind in @('UL','IR')) {
        $fw=Join-Path $repo "$kind\firmware"
        $board=if ($kind -eq 'UL') { 'ul_v1' } else { 'ir_v1' }
        $exe=Join-Path $out "entry_$kind.exe"
        $argsList=@('/nologo','/W3','/O2','/DKLQ_BOOT_APP',"/DSENSOR_$kind","/I$PSScriptRoot\host","/I$fw\boards\$board","/I$fw\app\Inc","/I$fw\common","/I$fw\platform\py32f002b",(Join-Path $PSScriptRoot 'test_app_entry.c'),(Join-Path $fw 'app\Src\device_service.c'),(Join-Path $fw 'common\frame_codec.c'),"/Fe$exe")
        & $Compiler @argsList
        if ($LASTEXITCODE -ne 0) { throw "$kind app entry test build failed" }
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "$kind app entry test failed" }
    }
} finally { Pop-Location; $env:LIB=$oldLib; $env:INCLUDE=$oldInclude }
