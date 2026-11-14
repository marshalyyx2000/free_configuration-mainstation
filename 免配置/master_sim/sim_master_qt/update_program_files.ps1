#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"
$Src = Join-Path $PSScriptRoot "build-qt\Release\sim_master_qt.exe"
$Dst = Join-Path ${env:ProgramFiles} "SimMasterQt\bin\sim_master_qt.exe"
if (-not (Test-Path $Src)) {
    throw "请先运行 build_local.cmd 编译: $Src"
}
Stop-Process -Name sim_master_qt -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Copy-Item -Force $Src $Dst
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Write-Host "Updated: $Dst"
Get-Item $Dst | Format-List Length, LastWriteTime
