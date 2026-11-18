# Build sim_master_qt (Qt6 + MSVC x64)
param(
    [string]$Configuration = "Release",
    [string]$AppVersion = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$MasterSim = Split-Path -Parent $Root
$BuildDir = Join-Path $Root "build-qt"
# 路径含中文时 CMake/MSBuild 易失败，可用 build_local.cmd（SUBST Z:）编译。
$BinMaster = Join-Path $MasterSim "bin"

function Resolve-Cmake {
    $candidates = @(
        (Join-Path ${env:ProgramFiles} "CMake\bin\cmake.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "CMake\bin\cmake.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "cmake not found"
}

function Import-VsDevEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $install) { throw "MSVC not found" }
    $devCmd = Join-Path $install "Common7\Tools\VsDevCmd.bat"
    $envDump = cmd /c "`"$devCmd`" -no_logo -arch=amd64 -host_arch=amd64 && set" 2>&1
    foreach ($line in $envDump) {
        if ($line -match "^(.*?)=(.*)$") {
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }
}

if (-not $env:QT6_DIR) {
    $defaultQt = "C:\Qt\6.7.3\msvc2019_64"
    if (Test-Path $defaultQt) { $env:QT6_DIR = $defaultQt }
}
if (-not $env:QT6_DIR -or -not (Test-Path (Join-Path $env:QT6_DIR "lib\cmake\Qt6"))) {
    throw "Set QT6_DIR to Qt6 MSVC kit (e.g. C:\Qt\6.7.3\msvc2019_64)"
}

$cli = Join-Path $BinMaster "mms_master_cli.exe"
if (-not (Test-Path $cli)) {
    Write-Host "Building mms_master_cli..."
    & (Join-Path $MasterSim "build_libiec61850.ps1")
}

$cmake = Resolve-Cmake
Import-VsDevEnvironment

$versionFile = Join-Path $Root "VERSION"
$ver = (Get-Content $versionFile -Raw).Trim()
if ($AppVersion) { $ver = $AppVersion }

$cmakeArgs = @(
    "-S", $Root,
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_PREFIX_PATH=$($env:QT6_DIR)",
    "-DAPP_VERSION=$ver"
)
if ($env:SIMMASTER_CPACK_DIR) {
    $cpackDir = ($env:SIMMASTER_CPACK_DIR -replace '\\', '/')
    $cmakeArgs += "-DSIMMASTER_CPACK_DIR=$cpackDir"
}

& $cmake @cmakeArgs
& $cmake --build $BuildDir --config $Configuration --target sim_master_qt

$OutExe = Join-Path $BuildDir "$Configuration\sim_master_qt.exe"
if (-not (Test-Path $OutExe)) { throw "Build failed: $OutExe" }

New-Item -ItemType Directory -Force -Path (Join-Path $Root "dist") | Out-Null
Copy-Item -Force $OutExe (Join-Path $Root "dist\sim_master_qt.exe")
Copy-Item -Force $cli (Join-Path $Root "dist\mms_master_cli.exe")
Copy-Item -Force $cli (Join-Path (Split-Path $OutExe) "mms_master_cli.exe")
Write-Host "OK: $OutExe"
Write-Host "CLI beside GUI: $(Join-Path (Split-Path $OutExe) 'mms_master_cli.exe')"
