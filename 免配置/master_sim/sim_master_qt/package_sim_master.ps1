# Package NSIS installer; bump VERSION +0.01 on success
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$VersionFile = Join-Path $Root "VERSION"
$ver = (Get-Content $VersionFile -Raw).Trim()
$BuildDir = Join-Path $Root "build-qt"
$packOut = "C:/Temp/SimMasterQt_cpack"
$distDir = Join-Path $Root "dist"

function Bump-Version([string]$v) {
    $n = [double]$v + 0.01
    return "{0:F2}" -f $n
}

$env:SIMMASTER_CPACK_DIR = "C:/Temp/SimMasterQt_cpack"
Remove-Item -Recurse -Force "C:\Temp\SimMasterQt_cpack" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

& (Join-Path $Root "build_sim_master.ps1") -Configuration Release -AppVersion $ver

if (-not $env:QT6_DIR) { $env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64" }
$windeployqt = Join-Path $env:QT6_DIR "bin\windeployqt.exe"
$stage = Join-Path $BuildDir "install_staging"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$cmake = (Get-Command cmake).Source
& $cmake --install $BuildDir --config Release --prefix $stage
& $windeployqt --no-translations --no-compiler-runtime (Join-Path $stage "bin\sim_master_qt.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

$stagedExe = Join-Path $stage "bin\sim_master_qt.exe"
Push-Location (Split-Path $stagedExe)
& $stagedExe --verify
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { throw "staged exe verify failed with $code" }

Push-Location $BuildDir
& cpack -C Release
$cpackCode = $LASTEXITCODE
Pop-Location

$built = "C:\Temp\SimMasterQt_cpack\SimMasterQt-$ver-win64.exe"
if (-not (Test-Path $built)) {
    $built = Get-ChildItem "C:\Temp\SimMasterQt_cpack" -Recurse -Filter "SimMasterQt-$ver-win64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $built -or -not (Test-Path $built)) {
    if ($cpackCode -ne 0) { throw "cpack failed and installer not found" }
    throw "installer not found under C:\Temp\SimMasterQt_cpack"
}

$dest = Join-Path $distDir "SimMasterQt-$ver-win64.exe"
Remove-Item -Force $dest -ErrorAction SilentlyContinue
try {
    Copy-Item -Force $built $dest
} catch {
    $dest = $built
    Write-Warning "Could not copy to project dist (file in use). Using: $built"
}

$newVer = Bump-Version $ver
Set-Content -Path $VersionFile -Value $newVer -NoNewline
Write-Host "Package: $dest"
Write-Host "CPack artifact: $built"
Write-Host "Next VERSION: $newVer"
Write-Host "Staged verify OK"
