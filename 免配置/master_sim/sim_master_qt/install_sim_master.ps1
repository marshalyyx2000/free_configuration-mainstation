# Install sim_master_qt without NSIS (avoids Uninstall failed on upgrade)
param(
    [string]$InstallDir = "",
    [string]$InstallOkFlag = ""
)

$ErrorActionPreference = "Stop"
if (-not $InstallDir) {
    $pf64 = $env:ProgramW6432
    if (-not $pf64) { $pf64 = ${env:ProgramFiles(x86)} }
    if (-not $pf64) { $pf64 = $env:ProgramFiles }
    $InstallDir = Join-Path $pf64 "SimMasterQt"
}

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build-qt"
$MasterSim = Split-Path -Parent $Root
$OutDev = Join-Path $MasterSim "out"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Stop-SimMasterProcesses {
    Stop-Process -Name sim_master_qt -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 800
}

function Assert-InstalledExe([string]$InstalledExe, [string]$BuiltExe) {
    if (-not (Test-Path $InstalledExe)) {
        throw "Install failed: missing $InstalledExe"
    }
    if (-not (Test-Path $BuiltExe)) {
        throw "Build missing: $BuiltExe"
    }
    $i = Get-Item $InstalledExe
    $b = Get-Item $BuiltExe
    if ($i.Length -ne $b.Length) {
        throw @"
Install verification FAILED.
  Installed: $($i.FullName)  $($i.Length) bytes  $($i.LastWriteTime)
  Expected:  $($b.FullName)  $($b.Length) bytes  $($b.LastWriteTime)
Close all SimMasterQt windows and run PowerShell **as Administrator**, then:
  cd `"$Root`"
  .\install_sim_master.ps1
"@
    }
    Write-Host "Verified: $($i.FullName) ($($i.Length) bytes, $($i.LastWriteTime))"
}

$programFilesRoot = [System.IO.Path]::GetFullPath($(if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }))
$installFull = [System.IO.Path]::GetFullPath($InstallDir)
$needsAdmin = $installFull.StartsWith($programFilesRoot, [StringComparison]::OrdinalIgnoreCase)

if ($needsAdmin -and -not (Test-IsAdmin)) {
    $flag = Join-Path $env:TEMP "SimMasterQt_install_ok_$([Guid]::NewGuid().ToString('N')).txt"
    Remove-Item $flag -Force -ErrorAction SilentlyContinue
    Write-Host "Installing under Program Files requires administrator."
    Write-Host "Please click **Yes** on the UAC prompt..."
    try {
        $proc = Start-Process -FilePath "powershell.exe" -Verb RunAs -PassThru -Wait -ArgumentList @(
            "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", $MyInvocation.MyCommand.Path,
            "-InstallDir", $InstallDir,
            "-InstallOkFlag", $flag
        )
    } catch {
        throw "UAC elevation cancelled or failed: $($_.Exception.Message)"
    }
    if (-not $proc -or $proc.ExitCode -ne 0) {
        $code = if ($proc) { $proc.ExitCode } else { "?" }
        throw "Elevated install exited with code $code (did you click Yes on UAC?)"
    }
    if (-not (Test-Path $flag)) {
        throw "Elevated install did not complete (marker missing). Run PowerShell as Administrator and retry."
    }
    Remove-Item $flag -Force -ErrorAction SilentlyContinue
    $installedExe = Join-Path $InstallDir "bin\sim_master_qt.exe"
    $builtExe = Join-Path $BuildDir "Release\sim_master_qt.exe"
    Assert-InstalledExe $installedExe $builtExe
    Write-Host "Install OK (elevated)."
    exit 0
}

Stop-SimMasterProcesses

if (-not $env:QT6_DIR) { $env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64" }

& (Join-Path $Root "build_sim_master.ps1") -Configuration Release | Out-Host
$buildLib = Join-Path $MasterSim "build_libiec61850.ps1"
if (Test-Path $buildLib) {
    & $buildLib | Out-Host
}

$builtExe = Join-Path $BuildDir "Release\sim_master_qt.exe"
$scdDefault = Join-Path (Split-Path $MasterSim -Parent) "子站scd\26579.SCD"
if (-not (Test-Path (Join-Path $OutDev "station.SMCD"))) {
    if (Test-Path $scdDefault) {
        Write-Host "Generating out/station.SMCD from $scdDefault ..."
        New-Item -ItemType Directory -Force -Path $OutDev | Out-Null
        Push-Location (Split-Path $builtExe)
        & $builtExe --generate-config $scdDefault
        if ($LASTEXITCODE -ne 0) { throw "generate-config failed" }
        Pop-Location
    } else {
        Write-Warning "Missing $OutDev\station.SMCD and no $scdDefault"
    }
}

$windeployqt = Join-Path $env:QT6_DIR "bin\windeployqt.exe"
$cmake = (Get-Command cmake).Source

Stop-SimMasterProcesses
if (Test-Path $InstallDir) {
    try {
        Remove-Item -Recurse -Force $InstallDir
    } catch {
        Write-Warning "Could not remove entire $InstallDir (files in use?). Overwriting bin..."
        Stop-SimMasterProcesses
        Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue
    }
}
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

& $cmake --install $BuildDir --config Release --prefix $InstallDir
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

$shareDest = Join-Path $InstallDir "share\sim_master_qt"
New-Item -ItemType Directory -Force -Path $shareDest | Out-Null
foreach ($name in @("IEDList.xml", "station.SMCD")) {
    $src = Join-Path $OutDev $name
    if (Test-Path $src) {
        Copy-Item -Force $src (Join-Path $shareDest $name)
    }
}

& $windeployqt --no-translations --no-compiler-runtime (Join-Path $InstallDir "bin\sim_master_qt.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

$exe = Join-Path $InstallDir "bin\sim_master_qt.exe"
$cli = Join-Path $InstallDir "bin\mms_master_cli.exe"
$cliSrc = Join-Path $MasterSim "bin\mms_master_cli.exe"
if (-not (Test-Path $cli) -and (Test-Path $cliSrc)) {
    Copy-Item -Force $cliSrc $cli
}
if (-not (Test-Path $cli)) {
    throw "mms_master_cli.exe missing after install"
}

# Ensure GUI build is what we ship (cmake install should match; force if mismatch)
Copy-Item -Force $builtExe $exe
$cliBuilt = Join-Path $MasterSim "bin\mms_master_cli.exe"
if (Test-Path $cliBuilt) {
    Copy-Item -Force $cliBuilt $cli
}

Push-Location (Split-Path $exe)
& $exe --verify
if ($LASTEXITCODE -ne 0) { throw "verify failed after install" }
Pop-Location

Assert-InstalledExe $exe $builtExe

foreach ($name in @("IEDList.xml", "station.SMCD")) {
    $p = Join-Path $shareDest $name
    if (-not (Test-Path $p)) {
        Write-Warning "Install incomplete: missing $p"
    }
}

if (Test-IsAdmin) {
    $startMenu = [Environment]::GetFolderPath("CommonPrograms")
} else {
    $startMenu = [Environment]::GetFolderPath("Programs")
}
$appName = -join ([char]0x514D, [char]0x914D, [char]0x7F6E, [char]0x4EFF, [char]0x771F, [char]0x4E3B, [char]0x7AD9)
$lnkDir = Join-Path $startMenu "SimMasterQt"
New-Item -ItemType Directory -Force -Path $lnkDir | Out-Null
try {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path $lnkDir ($appName + ".lnk")))
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = Split-Path $exe
    $shortcut.Save()
    Write-Host "Start menu: $lnkDir"
} catch {
    Write-Warning "Could not create start menu shortcut: $($_.Exception.Message)"
}

if ($InstallOkFlag) {
    Set-Content -Path $InstallOkFlag -Value "ok" -Encoding ASCII
}

Write-Host "Installed: $exe"
Write-Host "MMS CLI: $cli ($((Get-Item $cli).Length) bytes)"
Write-Host "Share: $shareDest"
