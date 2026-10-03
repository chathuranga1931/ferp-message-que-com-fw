<#
.SYNOPSIS
  Restart the FERP Device Web service so it loads the code currently on disk.

.DESCRIPTION
  Use after pulling / editing code. Optionally rebuilds the UI and updates
  Python packages first, then restarts the service and waits until it answers.
  If your user lacks permission to restart the service (the installer grants it),
  the script re-launches itself elevated (one UAC prompt).

    powershell -ExecutionPolicy Bypass -File tools\ferp-device-web\deploy\windows\restart-service.ps1
    ... -Build      also rebuild the web UI (npm run build)
    ... -Deps       also pip install -r requirements.txt
#>
param(
    [switch]$Build,
    [switch]$Deps,
    [int]$Port = 8700
)

$ErrorActionPreference = "Stop"
$ServiceId  = "ferp-device-web"
$WebDir     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BackendDir = Join-Path $WebDir "backend"

if ($Build) {
    Write-Host "==> Building web UI" -ForegroundColor Cyan
    Push-Location (Join-Path $WebDir "frontend")
    try { npm run build; if ($LASTEXITCODE -ne 0) { throw "npm run build failed" } } finally { Pop-Location }
}
if ($Deps) {
    Write-Host "==> Updating Python packages" -ForegroundColor Cyan
    Push-Location $BackendDir
    try { & .\.venv\Scripts\python.exe -m pip install --quiet -r requirements.txt } finally { Pop-Location }
}

$before = $null
try { $before = (Invoke-RestMethod "http://127.0.0.1:$Port/api/health" -TimeoutSec 3).started } catch { }

Write-Host "==> Restarting service $ServiceId" -ForegroundColor Cyan
try {
    Restart-Service $ServiceId -ErrorAction Stop
} catch {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($isAdmin) { throw }
    Write-Host "No permission to restart the service as $env:USERNAME - asking for admin (UAC)..." -ForegroundColor Yellow
    Write-Host "(Re-run install-service.ps1 once as admin to allow restarts without this prompt.)"
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList @(
        "-NoProfile", "-Command", "Restart-Service $ServiceId")
    if ($p.ExitCode -ne 0) { throw "Elevated restart failed (exit $($p.ExitCode))" }
}

Write-Host "Waiting for the server..." -NoNewline
foreach ($i in 1..30) {
    Start-Sleep -Seconds 1
    try {
        $h = Invoke-RestMethod "http://127.0.0.1:$Port/api/health" -TimeoutSec 2
        if (-not $before -or $h.started -ne $before) {
            Write-Host " up." -ForegroundColor Green
            Write-Host "API version $($h.api_version) - http://127.0.0.1:$Port"
            exit 0
        }
    } catch { }
    Write-Host "." -NoNewline
}
Write-Host ""
Write-Warning "The server did not answer within 30 s - check deploy\windows\logs\ferp-device-web.err.log"
exit 1
