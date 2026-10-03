<#
.SYNOPSIS
  Install (or update) FERP Device Web as a Windows service that starts at boot.

.DESCRIPTION
  1. Creates backend\.venv and installs Python requirements (incl. ../ferp-core)
  2. Builds the React UI (frontend\dist) when Node.js is available
  3. Registers the service with WinSW (downloaded to deploy\windows\bin on first run):
       - starts automatically at boot, before anyone logs in
       - restarts on failure, rotating logs in deploy\windows\logs
       - starts after the Mosquitto service when it exists
  4. Adds a Windows Firewall rule so the port is reachable from Tailscale peers
     only (100.64.0.0/10): open http://<tailscale-ip>:<port> from anywhere

  Re-run any time to update after pulling new code. Run from an elevated PowerShell:
    powershell -ExecutionPolicy Bypass -File tools\ferp-device-web\deploy\windows\install-service.ps1

.PARAMETER Python
  Python 3.12+ used to create the venv. Must NOT be the Microsoft Store python
  (the service account cannot run it). Auto-detected via the py launcher.

.PARAMETER BindHost
  Listen address. 0.0.0.0 (default) = all interfaces, access limited by the
  firewall rule; 127.0.0.1 = this PC only (no firewall rule).

.PARAMETER AllowFrom
  Remote addresses allowed through the firewall. Default: Tailscale range only.
  Add LocalSubnet to also allow your LAN:  -AllowFrom 100.64.0.0/10,LocalSubnet
#>
param(
    [string]$Python = "",
    [int]$Port = 8700,
    [string]$BindHost = "0.0.0.0",
    [string[]]$AllowFrom = @("100.64.0.0/10"),
    [switch]$SkipBuild,
    [switch]$RunAsCurrentUser
)

$ErrorActionPreference = "Stop"
$ServiceId  = "ferp-device-web"
$WinSWUrl   = "https://github.com/winsw/winsw/releases/download/v2.12.0/WinSW-x64.exe"

$DeployDir  = $PSScriptRoot
$WebDir     = (Resolve-Path (Join-Path $DeployDir "..\..")).Path
$BackendDir = Join-Path $WebDir "backend"
$FrontDir   = Join-Path $WebDir "frontend"
$BinDir     = Join-Path $DeployDir "bin"
$LogDir     = Join-Path $DeployDir "logs"
$VenvPy     = Join-Path $BackendDir ".venv\Scripts\python.exe"
$ServiceExe = Join-Path $BinDir "$ServiceId.exe"
$ServiceXml = Join-Path $BinDir "$ServiceId.xml"

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw "Run this script from an elevated (Administrator) PowerShell." }

# ── 1. Python venv ───────────────────────────────────────────────────────────
Step "Python environment"
if (-not $Python) {
    foreach ($v in @("3.13", "3.14", "3.12")) {
        try {
            $p = (& py "-$v" -c "import sys; print(sys.executable)" 2>$null)
            if ($LASTEXITCODE -eq 0 -and $p -and (Test-Path $p) -and ($p -notmatch "WindowsApps")) { $Python = $p; break }
        } catch { }
    }
}
if (-not $Python -or -not (Test-Path $Python)) { throw "No usable Python 3.12+ found. Pass -Python C:\path\to\python.exe" }
if ($Python -match "WindowsApps") { throw "Microsoft Store Python cannot run as a service. Install Python from python.org or 'py install'." }
Write-Host "Using $Python"

if (-not (Test-Path $VenvPy)) { & $Python -m venv (Join-Path $BackendDir ".venv") }
Push-Location $BackendDir
try {
    & $VenvPy -m pip install --quiet --upgrade pip
    & $VenvPy -m pip install --quiet -r requirements.txt
    if ($LASTEXITCODE -ne 0) { throw "pip install failed" }
} finally { Pop-Location }

# ── 2. Frontend build ────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Step "Building web UI"
    if (Get-Command npm -ErrorAction SilentlyContinue) {
        Push-Location $FrontDir
        try {
            if (Test-Path "package-lock.json") { npm ci --no-fund --no-audit } else { npm install --no-fund --no-audit }
            npm run build
            if ($LASTEXITCODE -ne 0) { throw "npm run build failed" }
        } finally { Pop-Location }
    } else {
        Write-Warning "npm not found - skipping UI build (the API will still run)."
    }
}

# ── 3. Windows service (WinSW) ───────────────────────────────────────────────
Step "Windows service '$ServiceId'"
New-Item -ItemType Directory -Force -Path $BinDir, $LogDir | Out-Null
if (-not (Test-Path $ServiceExe)) {
    Write-Host "Downloading WinSW..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $WinSWUrl -OutFile $ServiceExe -UseBasicParsing
}

$existing = Get-Service -Name $ServiceId -ErrorAction SilentlyContinue
if ($existing) {
    if ($existing.Status -ne "Stopped") { & $ServiceExe stop | Out-Null; Start-Sleep -Seconds 2 }
    & $ServiceExe uninstall | Out-Null
    Start-Sleep -Seconds 1
}

$depend = ""
if (Get-Service -Name "mosquitto" -ErrorAction SilentlyContinue) { $depend = "  <depend>mosquitto</depend>" }

$account = ""
if ($RunAsCurrentUser) {
    $cred = Get-Credential -UserName "$env:USERDOMAIN\$env:USERNAME" -Message "Windows password for the service account"
    $account = @"
  <serviceaccount>
    <username>$([Security.SecurityElement]::Escape($cred.UserName))</username>
    <password>$([Security.SecurityElement]::Escape($cred.GetNetworkCredential().Password))</password>
    <allowservicelogon>true</allowservicelogon>
  </serviceaccount>
"@
}

@"
<service>
  <id>$ServiceId</id>
  <name>FERP Device Web</name>
  <description>FERP device web tool (FastAPI + React) - tools/ferp-device-web</description>
  <executable>$VenvPy</executable>
  <arguments>-m app</arguments>
  <workingdirectory>$BackendDir</workingdirectory>
  <env name="PYTHONUNBUFFERED" value="1"/>
  <env name="FERP_WEB_HOST" value="$BindHost"/>
  <env name="FERP_WEB_PORT" value="$Port"/>
  <startmode>Automatic</startmode>
  <delayedAutoStart>true</delayedAutoStart>
$depend
  <onfailure action="restart" delay="10 sec"/>
  <onfailure action="restart" delay="30 sec"/>
  <onfailure action="restart" delay="60 sec"/>
  <resetfailure>1 hour</resetfailure>
  <stoptimeout>15 sec</stoptimeout>
  <logpath>$LogDir</logpath>
  <log mode="roll-by-size">
    <sizeThreshold>10240</sizeThreshold>
    <keepFiles>8</keepFiles>
  </log>
$account
</service>
"@ | Set-Content -Path $ServiceXml -Encoding UTF8

& $ServiceExe install
if ($LASTEXITCODE -ne 0) { throw "WinSW install failed" }

# Let the installing (interactive) user start/stop/restart the service without
# elevation, e.g. `Restart-Service ferp-device-web` after pulling new code.
$sid  = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$sddl = ((& sc.exe sdshow $ServiceId) | Where-Object { $_ -match "\S" }) -join ""
$ace  = "(A;;RPWPDTLOCRRC;;;$sid)"
if ($sddl -and $sddl -notmatch [regex]::Escape($ace)) {
    $i = $sddl.IndexOf("S:")
    $new = if ($i -ge 0) { $sddl.Insert($i, $ace) } else { $sddl + $ace }
    & sc.exe sdset $ServiceId $new | Out-Null
    Write-Host "Granted $env:USERNAME permission to start/stop the service."
}

& $ServiceExe start
Start-Sleep -Seconds 3

$ok = $false
foreach ($i in 1..10) {
    try { Invoke-RestMethod "http://127.0.0.1:$Port/api/health" -TimeoutSec 2 | Out-Null; $ok = $true; break } catch { Start-Sleep -Seconds 1 }
}
if ($ok) { Write-Host "Service is running: http://127.0.0.1:$Port" -ForegroundColor Green }
else { Write-Warning "Service did not answer yet - check logs in $LogDir" }

# ── 4. Firewall ──────────────────────────────────────────────────────────────
$RuleName = "FERP Device Web ($Port)"
Get-NetFirewallRule -DisplayName "FERP Device Web (*)" -ErrorAction SilentlyContinue | Remove-NetFirewallRule   # incl. old ports
if ($BindHost -ne "127.0.0.1") {
    Step "Firewall: allow TCP $Port from $($AllowFrom -join ', ')"
    New-NetFirewallRule -DisplayName $RuleName -Direction Inbound -Action Allow -Protocol TCP `
        -LocalPort $Port -RemoteAddress $AllowFrom -Profile Any | Out-Null
    $tsIp = $null
    try { $tsIp = (& "C:\Program Files\Tailscale\tailscale.exe" ip -4 2>$null | Select-Object -First 1) } catch { }
    if ($tsIp) { Write-Host "From any device on your tailnet: http://${tsIp}:$Port" -ForegroundColor Green }
}

Write-Host "`nDone. Manage with:  Get-Service $ServiceId | Restart-Service   (logs: $LogDir)"
