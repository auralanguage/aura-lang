param(
    [string]$Version = "0.1.0-prealpha",
    [switch]$BuildInstaller
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$distRoot = Join-Path $repoRoot "dist"
$stageRoot = Join-Path $distRoot "aura-windows-x64"
$zipPath = Join-Path $distRoot "aura-windows-x64.zip"
$exePath = Join-Path $repoRoot "build\aura.exe"
$installerScript = Join-Path $repoRoot "packaging\AuraSetup.iss"

if (-not (Test-Path $exePath)) {
    throw "Missing build\aura.exe. Run .\build.bat first."
}

if (Test-Path $stageRoot) {
    Remove-Item $stageRoot -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stageRoot "examples") | Out-Null

Copy-Item $exePath (Join-Path $stageRoot "aura.exe")
Copy-Item (Join-Path $repoRoot "README.md") (Join-Path $stageRoot "README.md")
Copy-Item (Join-Path $repoRoot "LICENSE") (Join-Path $stageRoot "LICENSE")
Copy-Item (Join-Path $repoRoot "examples\*.aura") (Join-Path $stageRoot "examples")

Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $zipPath -Force

Write-Host "Created portable zip: $zipPath"

if (-not $BuildInstaller) {
    return
}

$iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($null -eq $iscc) {
    Write-Warning "Inno Setup compiler (ISCC.exe) was not found on PATH. Skipping installer build."
    return
}

& $iscc.Source /DMyAppVersion="$Version" $installerScript
if ($LASTEXITCODE -ne 0) {
    throw "Installer build failed with exit code $LASTEXITCODE"
}

Write-Host "Created installer in $distRoot"
