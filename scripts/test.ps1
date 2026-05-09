$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    & cmd /c build.bat
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }

    $exePath = Join-Path $repoRoot "build\aura.exe"
    & $exePath test --manifest tests/test_cases.json --executable build/aura.exe
    if ($LASTEXITCODE -ne 0) {
        throw "Test run failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
