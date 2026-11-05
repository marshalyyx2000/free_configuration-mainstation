# Build libiec61850.dll + mms_master_cli.exe (x64 Release) on Windows
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$LibSrc = Join-Path $Root "third_party\libiec61850"
$LibBuild = Join-Path $Root "build-win"
$CliBuild = Join-Path $Root "build-cli"
$Bin = Join-Path $Root "bin"

$ClientReportSrc = Join-Path $LibSrc "src\iec61850\client\client_report.c"
if ((Test-Path $ClientReportSrc) -and -not (Select-String -Path $ClientReportSrc -SimpleMatch "report data set size changed" -Quiet)) {
    Write-Warning "libiec61850 patch not applied: reports that reuse one RptID across data sets will be dropped. Apply native\patches\libiec61850-report-dataset-size.patch first."
}

function Resolve-Cmake {
    $candidates = @(
        (Join-Path ${env:ProgramFiles} "CMake\bin\cmake.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "CMake\bin\cmake.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    $pip = python -c "import pathlib,cmake; print(next(pathlib.Path(cmake.__file__).parent.rglob('cmake.exe')))" 2>$null
    if ($pip -and (Test-Path $pip.Trim())) { return $pip.Trim() }
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "cmake not found. Install Kitware.CMake or: python -m pip install cmake"
}

function Import-VsDevEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Visual Studio not found. Install VS 2022 Build Tools with C++ workload."
    }
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $install) {
        throw "MSVC toolset not found. Install VCTools (Desktop development with C++)."
    }
    $devCmd = Join-Path $install "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $devCmd)) {
        throw "VsDevCmd.bat not found under $install"
    }
    $envDump = cmd /c "`"$devCmd`" -no_logo -arch=amd64 -host_arch=amd64 && set" 2>&1
    foreach ($line in $envDump) {
        if ($line -match "^(.*?)=(.*)$") {
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }
    Write-Host "MSVC: $install"
}

if (-not (Test-Path $LibSrc)) {
    throw "Missing libiec61850 source at $LibSrc"
}

$cmake = Resolve-Cmake
Write-Host "CMake: $cmake"
& $cmake --version

Import-VsDevEnvironment

New-Item -ItemType Directory -Force -Path $Bin | Out-Null

$Generator = "Visual Studio 17 2022"
& $cmake -S $LibSrc -B $LibBuild -G $Generator -A x64 `
    -DBUILD_EXAMPLES=OFF `
    -DBUILD_PYTHON_BINDINGS=OFF `
    -DCONFIG_INCLUDE_GOOSE_SUPPORT=OFF

& $cmake --build $LibBuild --config Release --target iec61850

Remove-Item -Recurse -Force $CliBuild -ErrorAction SilentlyContinue

& $cmake -S (Join-Path $Root "native") -B $CliBuild -G $Generator -A x64

& $cmake --build $CliBuild --config Release

$Lib = Join-Path $LibBuild "src\Release\iec61850.lib"
$Exe = Join-Path $CliBuild "Release\mms_master_cli.exe"

if (-not (Test-Path $Lib)) { throw "iec61850.lib not found under $LibBuild" }
if (-not (Test-Path $Exe)) { throw "mms_master_cli.exe not found under $CliBuild" }

Copy-Item -Force $Exe (Join-Path $Bin "mms_master_cli.exe")

# Optional shared DLL if built separately
$Dll = Join-Path $LibBuild "src\Release\iec61850.dll"
if (Test-Path $Dll) {
    Copy-Item -Force $Dll (Join-Path $Bin "iec61850.dll")
    Write-Host "OK: $Bin\iec61850.dll"
}

Write-Host "OK: $Bin\mms_master_cli.exe"
