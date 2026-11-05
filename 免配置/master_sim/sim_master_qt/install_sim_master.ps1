# Install sim_master_qt without NSIS (avoids Uninstall failed on upgrade)
param(
    [string]$InstallDir = $(Join-Path $env:ProgramFiles "SimMasterQt")
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build-qt"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$programFilesRoot = [System.IO.Path]::GetFullPath($env:ProgramFiles)
$installFull = [System.IO.Path]::GetFullPath($InstallDir)
if ($installFull.StartsWith($programFilesRoot, [StringComparison]::OrdinalIgnoreCase) -and -not (Test-IsAdmin)) {
    Write-Host "Installing under Program Files requires administrator — elevating..."
    $elevArgs = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", $MyInvocation.MyCommand.Path,
        "-InstallDir", $InstallDir
    )
    Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $elevArgs -Wait
    exit $LASTEXITCODE
}

if (-not $env:QT6_DIR) { $env:QT6_DIR = "C:\Qt\6.7.3\msvc2019_64" }

& (Join-Path $Root "build_sim_master.ps1") -Configuration Release | Out-Host

$windeployqt = Join-Path $env:QT6_DIR "bin\windeployqt.exe"
$cmake = (Get-Command cmake).Source

if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
}
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

& $cmake --install $BuildDir --config Release --prefix $InstallDir
& $windeployqt --no-translations --no-compiler-runtime (Join-Path $InstallDir "bin\sim_master_qt.exe")

$exe = Join-Path $InstallDir "bin\sim_master_qt.exe"
Push-Location (Split-Path $exe)
& $exe --verify
if ($LASTEXITCODE -ne 0) { throw "verify failed after install" }
Pop-Location

$startMenu = [Environment]::GetFolderPath("CommonPrograms")
$appName = -join ([char]0x514D, [char]0x914D, [char]0x7F6E, [char]0x4EFF, [char]0x771F, [char]0x4E3B, [char]0x7AD9)
$lnkDir = Join-Path $startMenu "SimMasterQt"
New-Item -ItemType Directory -Force -Path $lnkDir | Out-Null
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path $lnkDir ($appName + ".lnk")))
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = Split-Path $exe
$shortcut.Save()

Write-Host "Installed: $exe"
Write-Host "Start menu (all users): $lnkDir"
