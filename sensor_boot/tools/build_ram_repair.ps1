param(
    [string]$ArmccBin=$(if($env:KLQ_ARMCC){$env:KLQ_ARMCC}else{'C:/Keil_v5/ARM/ARMCC/bin'}),
    [string]$SdkRoot=$(if($env:KLQ_PY32_SDK){$env:KLQ_PY32_SDK}else{Join-Path $PSScriptRoot '../../UL/firmware/vendor/PY32F002B_Firmware'}),
    [string]$OutputDir=''
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if(!$OutputDir){$OutputDir=Join-Path $repo 'build/ir_boot102_20261029/ram_repair'}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir=(Resolve-Path -LiteralPath $OutputDir).Path
$sdk=(Resolve-Path -LiteralPath $SdkRoot).Path
foreach($name in @('armcc','armlink','fromelf')){
    if(!(Test-Path (Join-Path $ArmccBin "$name.exe"))){throw "Missing $name"}
}
$scatter=Join-Path $OutputDir 'ram_repair.sct'
@'
LR_REPAIR 0x20000000 0x700 {
 ER_REPAIR 0x20000000 0x700 {
  boot_repair_ram.o (ENTRY, +First)
  .ANY (+RO)
 }
}
'@ | Set-Content -LiteralPath $scatter -Encoding Ascii
$object=Join-Path $OutputDir 'boot_repair_ram.o'
$axf=Join-Path $OutputDir 'boot_repair_ram.axf'
$bin=Join-Path $OutputDir 'boot_repair_ram.bin'
$map=Join-Path $OutputDir 'boot_repair_ram.map'
$includes=@('-I',(Join-Path $sdk 'Drivers/CMSIS/Include'),'-I',(Join-Path $sdk 'Drivers/CMSIS/Device/PY32F0xx/Include'))
& (Join-Path $ArmccBin 'armcc.exe') --cpu Cortex-M0plus --thumb --c99 --split_sections --diag_warning=Pe223 -O2 -Ospace -D PY32F002Bx5 @includes -c (Join-Path $PSScriptRoot 'boot_repair_ram.c') -o $object
if($LASTEXITCODE -ne 0){throw 'RAM repair compile failed'}
& (Join-Path $ArmccBin 'armlink.exe') $object --entry=boot_repair_ram --no_scanlib --scatter $scatter --map --symbols --callgraph --info sizes,totals,unused --list $map -o $axf
if($LASTEXITCODE -ne 0){throw 'RAM repair link failed'}
& (Join-Path $ArmccBin 'fromelf.exe') --bin --output $bin $axf
if($LASTEXITCODE -ne 0){throw 'RAM repair BIN failed'}
if((Get-Item $bin).Length -gt 0x700){throw 'RAM repair overlaps reserved memory'}
$text=Get-Content -LiteralPath $map -Raw
if($text -notmatch 'boot_repair_ram\s+0x20000001'){throw 'RAM entry is not fixed at 0x20000000 Thumb'}
if($text -notmatch 'Total RW\s+Size[^\r\n]*?\s0\s*\('){throw 'RAM helper has initialized/zero data'}
Write-Output "RAM repair ready: $bin"
Get-FileHash -LiteralPath $bin -Algorithm SHA256 | Format-List
