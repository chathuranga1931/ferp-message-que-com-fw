<#
.SYNOPSIS
  Development mode: backend on :8701 (separate data dir, so the installed
  service on :8700 keeps running) + Vite dev server with hot reload.

  Open the URL Vite prints (http://localhost:5173). Ctrl+C stops both.
#>
$ErrorActionPreference = "Stop"
$WebDir     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BackendDir = Join-Path $WebDir "backend"
$FrontDir   = Join-Path $WebDir "frontend"

$env:FERP_WEB_PORT = "8701"
$env:FERP_DATA_DIR = Join-Path $BackendDir "data-dev"
$backend = Start-Process -PassThru -NoNewWindow -WorkingDirectory $BackendDir `
    -FilePath (Join-Path $BackendDir ".venv\Scripts\python.exe") -ArgumentList "-m", "app"
try {
    Push-Location $FrontDir
    $env:VITE_BACKEND = "127.0.0.1:8701"
    npm run dev
} finally {
    Pop-Location
    Stop-Process -Id $backend.Id -ErrorAction SilentlyContinue
}
