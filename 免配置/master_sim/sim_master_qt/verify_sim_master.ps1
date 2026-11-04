# Verify Release build (--verify self-test)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $Root "build-qt\Release\sim_master_qt.exe"
if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $Root "dist\sim_master_qt.exe"
}
if (-not (Test-Path $Exe)) {
    & (Join-Path $Root "build_sim_master.ps1") -Configuration Release
    $Exe = Join-Path $Root "build-qt\Release\sim_master_qt.exe"
}
Push-Location (Split-Path $Exe)
& $Exe --verify
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { throw "verify failed with exit $code" }
Write-Host "verify OK"
