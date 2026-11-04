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

$MasterSim = Split-Path -Parent $Root
$Scd = Join-Path (Split-Path $MasterSim -Parent) "子站scd\26579.SCD"
if (Test-Path $Scd) {
    Push-Location (Split-Path $Exe)
    & $Exe --generate-config $Scd
    if ($LASTEXITCODE -ne 0) { throw "generate-config failed" }
    Pop-Location
    $zip = Join-Path $MasterSim "out\station.zip"
    python -c "import zipfile; z=zipfile.ZipFile(r'$zip'); i=z.infolist()[0]; assert i.filename=='station.SMCD', i.filename; assert i.compress_type==8, i.compress_type"
    if ($LASTEXITCODE -ne 0) { throw "station.zip is not DEFLATED (method 8)" }
    Write-Host "zip DEFLATED OK"
}

Write-Host "verify OK"
