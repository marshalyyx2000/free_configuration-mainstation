#Requires -RunAsAdministrator
# 将 build-qt\Release 安装到 %ProgramFiles%\SimMasterQt（含 windeployqt、CLI、share）
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$MasterSim = Split-Path -Parent $Root
$BuiltExe = Join-Path $Root "build-qt\Release\sim_master_qt.exe"
$Cli = Join-Path $MasterSim "bin\mms_master_cli.exe"
$Qt = if ($env:QT6_DIR) { $env:QT6_DIR } else { "C:\Qt\6.7.3\msvc2019_64" }
$Windeploy = Join-Path $Qt "bin\windeployqt.exe"

if (-not (Test-Path $BuiltExe)) {
    Write-Host "ERROR: run build_local.cmd first."
    exit 1
}
if (-not (Test-Path $Windeploy)) {
    Write-Host "ERROR: windeployqt missing. Install Qt 6.7.3 msvc2019_64 or set QT6_DIR."
    exit 1
}

$pf = ${env:ProgramW6432}
if (-not $pf) { $pf = $env:ProgramFiles }
$Install = Join-Path $pf "SimMasterQt"
$Bin = Join-Path $Install "bin"
$Share = Join-Path $Install "share\sim_master_qt"

Stop-Process -Name sim_master_qt, mms_master_cli -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

New-Item -ItemType Directory -Force -Path $Bin, $Share | Out-Null
Copy-Item -Force $BuiltExe (Join-Path $Bin "sim_master_qt.exe")
if (Test-Path $Cli) {
    Copy-Item -Force $Cli (Join-Path $Bin "mms_master_cli.exe")
}

& $Windeploy --no-translations --no-compiler-runtime (Join-Path $Bin "sim_master_qt.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

foreach ($name in @("IEDList.xml")) {
    foreach ($src in @(
            (Join-Path $MasterSim "out\$name"),
            (Join-Path $Share $name),
            (Join-Path $MasterSim "..\ops\_debug\$name")
        )) {
        if (Test-Path $src) {
            Copy-Item -Force $src (Join-Path $Share $name)
            break
        }
    }
}

$smcd = Join-Path $MasterSim "out\station.SMCD"
if ((Test-Path $smcd) -and ((Get-Item $smcd).Length -lt 24MB)) {
    Copy-Item -Force $smcd (Join-Path $Share "station.SMCD")
}

$menu = [Environment]::GetFolderPath("Programs")
$lnk = Join-Path $menu "SimMasterQt.lnk"
if (-not (Test-Path $lnk)) {
    $wsh = New-Object -ComObject WScript.Shell
    $sc = $wsh.CreateShortcut($lnk)
    $sc.TargetPath = Join-Path $Bin "sim_master_qt.exe"
    $sc.WorkingDirectory = $Bin
    $sc.Description = "SimMasterQt"
    $sc.Save()
}

$exe = Join-Path $Bin "sim_master_qt.exe"
Get-Item $exe | Format-List FullName, Length, LastWriteTime
Write-Host "OK: Installed to $Install"
Write-Host "Start: $exe"
