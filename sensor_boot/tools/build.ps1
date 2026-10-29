param(
    [ValidateSet('all','ul','ir','motor')][string]$Device = 'all',
    [string]$ArmccBin = $(if ($env:KLQ_ARMCC) { $env:KLQ_ARMCC } else { 'C:\Keil_v5\ARM\ARMCC\bin' }),
    [string]$SdkRoot = $(if ($env:KLQ_PY32_SDK) { $env:KLQ_PY32_SDK } else { Join-Path $PSScriptRoot '..\..\UL\firmware\vendor\PY32F002B_Firmware' }),
    [string]$Python = (Join-Path $PSScriptRoot '..\..\.venv\Scripts\python.exe')
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo=Split-Path $root -Parent
$sdk=(Resolve-Path -LiteralPath $SdkRoot).Path
$cmsis=Join-Path $sdk 'Drivers\CMSIS'
foreach ($name in @('armcc','armasm','armlink','fromelf')) {
    if (!(Test-Path -LiteralPath (Join-Path $ArmccBin "$name.exe"))) { throw "Missing ARMCC tool: $name" }
}
if (!(Test-Path -LiteralPath $Python)) { throw 'Pass -Python with a Python 3.12 executable.' }
$devices=if ($Device -eq 'all') { @('ul','ir','motor') } else { @($Device) }
foreach ($kind in $devices) {
    $out=Join-Path $root "build\$kind"
    $bootOut=Join-Path $out 'boot'
    $appOut=Join-Path $out 'app'
    New-Item -ItemType Directory -Path $bootOut,$appOut -Force | Out-Null
    $includes=@('-I',(Join-Path $root 'include'),'-I',(Join-Path $cmsis 'Include'),'-I',(Join-Path $cmsis 'Device\PY32F0xx\Include'))
    $sources=@('main','boot_hw','update','sensor_hw') | ForEach-Object { Join-Path $root "src\$_.c" }
    $sources+=Join-Path $sdk 'Templates\PY32F002Bxx_Templates_LL\Src\system_py32f002b.c'
    $objects=@()
    foreach ($source in $sources) {
        $obj=Join-Path $bootOut (([IO.Path]::GetFileNameWithoutExtension($source))+'.o')
        & (Join-Path $ArmccBin 'armcc.exe') --cpu Cortex-M0plus --thumb --c99 --split_sections --diag_warning=Pe223 -O2 -Ospace -D PY32F002Bx5 -D SWD_DELAY -D "SENSOR_$($kind.ToUpperInvariant())" @includes -c $source -o $obj
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
        $objects+=$obj
    }
    $startup=Join-Path $sdk 'Templates\PY32F002Bxx_Templates_LL\MDK-ARM\startup_py32f002bxx.s'
    $startupObj=Join-Path $bootOut 'startup.o'
    & (Join-Path $ArmccBin 'armasm.exe') --cpu Cortex-M0plus $startup -o $startupObj
    if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
    $axf=Join-Path $bootOut 'sensor_boot.axf'
    & (Join-Path $ArmccBin 'armlink.exe') @($startupObj) @objects --scatter (Join-Path $root 'linker\boot.sct') --map --symbols --callgraph --info sizes,totals,unused --list (Join-Path $bootOut 'sensor_boot.map') -o $axf
    if ($LASTEXITCODE -ne 0) { throw 'Boot link failed / memory region exceeded' }
    & (Join-Path $ArmccBin 'fromelf.exe') --bin --output (Join-Path $bootOut 'sensor_boot.bin') $axf
    if ($LASTEXITCODE -ne 0) { throw 'Boot BIN failed' }
    & (Join-Path $ArmccBin 'fromelf.exe') --i32combined --output (Join-Path $bootOut 'sensor_boot.hex') $axf
    if ($LASTEXITCODE -ne 0) { throw 'Boot HEX failed' }
    if ($kind -eq 'motor') {
        & (Join-Path $repo 'MOTOR\tools\build_motor.ps1') -Target app -Bootloader -OutputDir $appOut -ArmccBin $ArmccBin -SdkRoot $sdk
    } elseif ($kind -eq 'ir') {
        & (Join-Path $repo 'IR\tools\build_sensor.ps1') -Bootloader -Configuration Release -OutputDir $appOut -ArmccBin $ArmccBin -VendorRoot $sdk
    } else {
        & (Join-Path $repo 'UL\tools\build_sensor.ps1') -Bootloader -Configuration Release -OutputDir $appOut -ArmccBin $ArmccBin -VendorRoot $sdk
    }
    if (!$?) { throw "$kind app build failed" }
    & $Python (Join-Path $PSScriptRoot 'pack.py') --device $kind
    if ($LASTEXITCODE -ne 0) { throw "$kind packaging failed" }
}
