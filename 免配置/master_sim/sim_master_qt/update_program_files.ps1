#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"
$SrcDir = Join-Path $PSScriptRoot "build-qt\Release"
$Src = Join-Path $SrcDir "sim_master_qt.exe"
$SrcCli = Join-Path $SrcDir "mms_master_cli.exe"
$DstDir = Join-Path ${env:ProgramFiles} "SimMasterQt\bin"
$Dst = Join-Path $DstDir "sim_master_qt.exe"
if (-not (Test-Path $Src)) {
    throw "请先运行 build_local.cmd 编译: $Src"
}
Stop-Process -Name sim_master_qt,mms_master_cli -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Copy-Item -Force $Src $Dst
if (Test-Path $SrcCli) {
    Copy-Item -Force $SrcCli (Join-Path $DstDir "mms_master_cli.exe")
}
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Write-Host "Updated: $Dst"
Get-Item $Dst | Format-List Length, LastWriteTime
