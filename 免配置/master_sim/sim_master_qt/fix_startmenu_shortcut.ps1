# Fix Start Menu shortcut (Chinese title via Unicode code points)
$ErrorActionPreference = "Stop"

$appName = -join ([char]0x514D, [char]0x914D, [char]0x7F6E, [char]0x4EFF, [char]0x771F, [char]0x4E3B, [char]0x7AD9)
$exe = Join-Path $env:LOCALAPPDATA "Programs\SimMasterQt\bin\sim_master_qt.exe"
if (-not (Test-Path $exe)) {
    Write-Host "Not installed: $exe"
    exit 1
}

$programs = [Environment]::GetFolderPath("Programs")
Get-ChildItem $programs -Recurse -Filter "*.lnk" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.Name -match "61850|SimMaster|sim_master") {
        Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
    }
}

$folder = Join-Path $programs "SimMasterQt"
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$lnkPath = Join-Path $folder ($appName + ".lnk")
$shell = New-Object -ComObject WScript.Shell
$sc = $shell.CreateShortcut($lnkPath)
$sc.TargetPath = $exe
$sc.WorkingDirectory = Split-Path $exe
$sc.Description = $appName
$sc.Save()

Write-Host "OK: $lnkPath"
