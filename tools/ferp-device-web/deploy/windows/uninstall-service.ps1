<#
.SYNOPSIS
  Stop and remove the FERP Device Web Windows service and its firewall rule
  (data in backend\data is kept).
#>

$ErrorActionPreference = "Stop"
$ServiceId  = "ferp-device-web"
$ServiceExe = Join-Path $PSScriptRoot "bin\$ServiceId.exe"

if (Get-Service -Name $ServiceId -ErrorAction SilentlyContinue) {
    if (Test-Path $ServiceExe) {
        & $ServiceExe stop | Out-Null
        & $ServiceExe uninstall
    } else {
        Stop-Service $ServiceId -ErrorAction SilentlyContinue
        sc.exe delete $ServiceId | Out-Null
    }
    Write-Host "Service removed."
} else {
    Write-Host "Service is not installed."
}

Get-NetFirewallRule -DisplayName "FERP Device Web (*)" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
