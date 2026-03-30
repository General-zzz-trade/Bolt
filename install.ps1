# Bolt Agent Installer for Windows
# Usage: iwr -useb https://raw.githubusercontent.com/General-zzz-trade/Bolt/master/install.ps1 | iex
$ErrorActionPreference = "Stop"

$Repo = "General-zzz-trade/Bolt"
$BinaryName = "bolt-windows-x64.exe"
$InstallDir = "$env:LOCALAPPDATA\Bolt"
$BoltPath = "$InstallDir\bolt.exe"

# Auto-detect latest release version
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    $Release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -UseBasicParsing 2>$null
    $Version = $Release.tag_name -replace '^v', ''
    $ProgressPreference = 'Continue'
} catch {
    $Version = "0.6.0"  # Fallback
}

Write-Host ""
Write-Host "  ⚡ Bolt Agent Installer" -ForegroundColor Cyan -NoNewline
Write-Host " v$Version" -ForegroundColor DarkGray
Write-Host ""

# Create install directory
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# Download
$Url = "https://github.com/$Repo/releases/download/v$Version/$BinaryName"
Write-Host "  Platform:  Windows x64" -ForegroundColor White
Write-Host "  Install:   $BoltPath" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  Downloading..." -ForegroundColor Cyan

try {
    # Use TLS 1.2
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $Url -OutFile $BoltPath -UseBasicParsing
    $ProgressPreference = 'Continue'
} catch {
    Write-Host "  Download failed: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Alternative install methods:" -ForegroundColor Yellow
    Write-Host "    npm install -g bolt-agent"
    Write-Host "    Build from source: https://github.com/$Repo"
    exit 1
}

# Add to PATH if not already there
$CurrentPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($CurrentPath -notlike "*$InstallDir*") {
    Write-Host "  Adding to PATH..." -ForegroundColor DarkGray
    [Environment]::SetEnvironmentVariable("PATH", "$CurrentPath;$InstallDir", "User")
    $env:PATH = "$env:PATH;$InstallDir"
}

# Verify
if (Test-Path $BoltPath) {
    Write-Host ""
    Write-Host "  ✓ Installed successfully!" -ForegroundColor Green
    Write-Host ""

    # Try to run version check
    try {
        $VersionOutput = & $BoltPath -v 2>&1
        Write-Host "  $VersionOutput" -ForegroundColor DarkGray
    } catch {}

    Write-Host ""
    Write-Host "  Get started:" -ForegroundColor White
    Write-Host "    bolt                  Interactive mode (setup wizard on first run)" -ForegroundColor White
    Write-Host "    bolt doctor            Check environment" -ForegroundColor DarkGray
    Write-Host "    bolt --help            Show all commands" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "  Note: Restart your terminal for PATH changes to take effect." -ForegroundColor Yellow
    Write-Host "  Docs: https://github.com/$Repo" -ForegroundColor DarkGray
    Write-Host ""
} else {
    Write-Host "  Installation failed." -ForegroundColor Red
    exit 1
}
