# Removes only the disconnected KLQ development device instance; retains drivers.
$ErrorActionPreference = 'Stop'
$klqProject = Split-Path -Parent $PSScriptRoot
$klqOutput = Join-Path $klqProject 'build/usb_refresh.log'
New-Item -ItemType Directory -Path (Split-Path -Parent $klqOutput) -Force | Out-Null
$klqIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$klqPrincipal = New-Object Security.Principal.WindowsPrincipal($klqIdentity)
$klqElevated = $klqPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
"Time: $(Get-Date -Format o)`nElevated: $klqElevated" | Set-Content -LiteralPath $klqOutput -Encoding UTF8
if (-not $klqElevated) {
    'Administrator authorization is required; nothing was changed.' | Add-Content -LiteralPath $klqOutput
    exit 1
}
try {
    $klqId = 'USB\VID_314B&PID_0108\000000000000'
    $klqPresent = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -eq $klqId })
    if ($klqPresent.Count -eq 0) {
        & pnputil.exe /remove-device $klqId 2>&1 | Out-File -LiteralPath $klqOutput -Append -Encoding UTF8
        "Remove exit code: $LASTEXITCODE" | Add-Content -LiteralPath $klqOutput
    } else {
        'KLQ is connected; skipping removal.' | Add-Content -LiteralPath $klqOutput
    }
    & pnputil.exe /scan-devices 2>&1 | Out-File -LiteralPath $klqOutput -Append -Encoding UTF8
    "Scan exit code: $LASTEXITCODE" | Add-Content -LiteralPath $klqOutput
    'Completed.' | Add-Content -LiteralPath $klqOutput
} catch {
    $_ | Out-String | Add-Content -LiteralPath $klqOutput
    exit 1
}
