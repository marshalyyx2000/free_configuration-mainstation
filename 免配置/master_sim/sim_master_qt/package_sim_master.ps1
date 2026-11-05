# Package single-file NSIS installer; test Program Files install; bump VERSION only on pass
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$VersionFile = Join-Path $Root "VERSION"
$ver = (Get-Content $VersionFile -Raw).Trim()
$BuildDir = Join-Path $Root "build-qt"
$distDir = Join-Path $Root "dist"
$MasterSim = Split-Path -Parent $Root
$OutDev = Join-Path $MasterSim "out"

function Bump-Version([string]$v) {
    $n = [double]$v + 0.01
    return "{0:F2}" -f $n
}

function Stop-SimMasterProcesses {
    Stop-Process -Name sim_master_qt, mms_master_cli -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 800
}

$env:SIMMASTER_CPACK_DIR = "C:/Temp/SimMasterQt_cpack"
Remove-Item -Recurse -Force "C:\Temp\SimMasterQt_cpack" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

if (-not $env:QT6_DIR) { $env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64" }

$buildLib = Join-Path $MasterSim "build_libiec61850.ps1"
if (Test-Path $buildLib) { & $buildLib | Out-Host }

& (Join-Path $Root "build_sim_master.ps1") -Configuration Release -AppVersion $ver

$builtExe = Join-Path $BuildDir "Release\sim_master_qt.exe"
$scdDefault = Join-Path (Split-Path $MasterSim -Parent) "子站scd\26579.SCD"
if (-not (Test-Path (Join-Path $OutDev "station.SMCD"))) {
    if (-not (Test-Path $scdDefault)) { throw "Need $scdDefault or out/station.SMCD to package" }
    New-Item -ItemType Directory -Force -Path $OutDev | Out-Null
    Push-Location (Split-Path $builtExe)
    & $builtExe --generate-config $scdDefault
    if ($LASTEXITCODE -ne 0) { throw "generate-config failed" }
    Pop-Location
}

$pf64 = $env:ProgramW6432
if (-not $pf64) { $pf64 = $env:ProgramFiles }
$nsisRoot = ($pf64 -replace '\\', '/')

$cmake = (Get-Command cmake).Source
& $cmake -S $Root -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=$($env:QT6_DIR)" "-DAPP_VERSION=$ver" `
    "-DSIMMASTER_CPACK_DIR=C:/Temp/SimMasterQt_cpack"
& $cmake --build $BuildDir --config Release --target sim_master_qt

$windeployqt = Join-Path $env:QT6_DIR "bin\windeployqt.exe"
$stage = Join-Path $BuildDir "install_staging"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

& $cmake --install $BuildDir --config Release --prefix $stage
$shareStage = Join-Path $stage "share\sim_master_qt"
New-Item -ItemType Directory -Force -Path $shareStage | Out-Null
foreach ($name in @("IEDList.xml", "station.SMCD")) {
    Copy-Item -Force (Join-Path $OutDev $name) (Join-Path $shareStage $name)
}
$cliBuilt = Join-Path $MasterSim "bin\mms_master_cli.exe"
Copy-Item -Force $cliBuilt (Join-Path $stage "bin\mms_master_cli.exe")
Copy-Item -Force $builtExe (Join-Path $stage "bin\sim_master_qt.exe")

& $windeployqt --no-translations --no-compiler-runtime (Join-Path $stage "bin\sim_master_qt.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

$stagedExe = Join-Path $stage "bin\sim_master_qt.exe"
Push-Location (Split-Path $stagedExe)
& $stagedExe --verify
if ($LASTEXITCODE -ne 0) { throw "staged verify failed" }
Pop-Location

$nsisPackDir = Join-Path $env:SIMMASTER_CPACK_DIR "_CPack_Packages/win64/NSIS"
$nsisIncludeSrc = Join-Path $Root "cmake\cpack_nsis_include.nsh"
New-Item -ItemType Directory -Force -Path $nsisPackDir | Out-Null

Push-Location $BuildDir
& cpack -C Release
$cpackExit = $LASTEXITCODE
Pop-Location
if ($cpackExit -ne 0) {
    # CPack wipes NSIS/ before writing project.nsi; include must sit beside project.nsi for makensis.
    $projectNsi = Join-Path $nsisPackDir "project.nsi"
    if (-not (Test-Path $projectNsi)) { throw "cpack failed and project.nsi missing" }
    Copy-Item -Force $nsisIncludeSrc (Join-Path $nsisPackDir "cpack_nsis_include.nsh")
    $makensis = "C:/Program Files (x86)/NSIS/makensis.exe"
    & $makensis $projectNsi
    if ($LASTEXITCODE -ne 0) { throw "cpack/makensis failed" }
}

$nsiFiles = Get-ChildItem "C:\Temp\SimMasterQt_cpack" -Recurse -Filter "*.nsi" -ErrorAction SilentlyContinue
if ($nsiFiles) {
    $nsiText = Get-Content $nsiFiles[0].FullName -Raw
    if ($nsiText -notmatch 'MUI_DIRECTORYPAGE_VERIFYONLEAVE') {
        throw "Generated NSIS missing MUI_DIRECTORYPAGE_VERIFYONLEAVE"
    }
    $iVerify = $nsiText.IndexOf('MUI_DIRECTORYPAGE_VERIFYONLEAVE')
    $iPage = $nsiText.IndexOf('MUI_PAGE_DIRECTORY')
    if ($iPage -ge 0 -and $iVerify -gt $iPage) {
        throw "MUI_DIRECTORYPAGE_VERIFYONLEAVE must appear before MUI_PAGE_DIRECTORY in NSIS script"
    }
    Write-Host "NSIS directory-page fix present in generated script."
}

$built = "C:\Temp\SimMasterQt_cpack\SimMasterQt-$ver-win64.exe"
if (-not (Test-Path $built)) {
    $built = Get-ChildItem "C:\Temp\SimMasterQt_cpack" -Recurse -Filter "SimMasterQt-$ver-win64.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $built -or -not (Test-Path $built)) {
    throw "installer not found: SimMasterQt-$ver-win64.exe"
}

$dest = Join-Path $distDir "SimMasterQt-$ver-win64.exe"
Stop-SimMasterProcesses
Copy-Item -Force $built $dest

Write-Host "Installer built: $dest"
Write-Host "NSIS target root: $pf64\SimMasterQt"

& (Join-Path $Root "test_nsis_package.ps1") -InstallerPath $dest

$newVer = Bump-Version $ver
Set-Content -Path $VersionFile -Value $newVer -NoNewline
Write-Host "Package verified. Next VERSION: $newVer"
Write-Host "Ship this single file: $dest"
