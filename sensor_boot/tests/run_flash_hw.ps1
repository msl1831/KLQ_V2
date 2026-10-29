param(
    [string]$Compiler='C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE',
    [string]$OutputDir='',
    [string]$LegacyRef=''
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo=Split-Path $root -Parent
if(!$OutputDir){$OutputDir=Join-Path $repo 'build\ir_boot102_20261029\flash_test'}
$vc=Split-Path (Split-Path $Compiler -Parent) -Parent
$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir=(Resolve-Path $OutputDir).Path
$current=Get-Content -LiteralPath (Join-Path $root 'src\boot_hw.c') -Raw
if($LegacyRef){
    $legacy=(& git -C $repo show "${LegacyRef}:sensor_boot/src/boot_hw.c") -join "`n"
    if($LASTEXITCODE -ne 0){throw 'Cannot load legacy driver from Git'}
}else{
    $legacy=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'flash_host\legacy_boot_hw_101.c.txt') -Raw
}
foreach($case in @(@('current',$current),@('legacy',$legacy))){
    $name=$case[0];$text=$case[1]
    $pattern='(?s)__asm static void jump\(uint32_t sp,uint32_t pc\)\s*\{.*?\}'
    if([regex]::Matches($text,$pattern).Count -ne 1){throw 'Expected exactly one ARM-only jump routine'}
    $text=[regex]::Replace($text,$pattern,'static void jump(uint32_t sp,uint32_t pc) { (void)sp;(void)pc; }')
    $folder=Join-Path $OutputDir $name
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $folder 'boot_hw_source.c') -Value $text -Encoding Ascii
    Push-Location $folder
    try{
        $env:INCLUDE=Join-Path $vc 'Include';$env:LIB=Join-Path $vc 'Lib'
        $extra=if($name -eq 'legacy'){'/DMOCK_OLD_DRIVER'}else{'/DMOCK_CURRENT_DRIVER'}
        $exe=Join-Path $folder 'flash_hw.exe'
        & $Compiler /nologo /TP /W3 /O2 /DSENSOR_IR $extra "/I$folder" "/I$PSScriptRoot\flash_host" "/I$PSScriptRoot\host" "/I$root\include" (Join-Path $PSScriptRoot 'test_flash_hw.c') (Join-Path $root 'src\update.c') "/Fe$exe"
        if($LASTEXITCODE -ne 0){throw "$name actual Flash-driver build failed"}
        & $exe
        if($LASTEXITCODE -ne 0){throw "$name actual Flash-driver test failed"}
    }finally{Pop-Location;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
}
