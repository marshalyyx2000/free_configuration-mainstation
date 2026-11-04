# Remove broken NSIS uninstall entries (Uninstall failed on old 61850 SimMaster packages)
$ErrorActionPreference = "Stop"

$patterns = @("61850", "SimMaster")
$roots = @(
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
)

foreach ($base in $roots) {
    if (-not (Test-Path $base)) { continue }
    Get-ChildItem $base -ErrorAction SilentlyContinue | ForEach-Object {
        $p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
        if (-not $p.DisplayName) { return }
        $hit = $false
        foreach ($pat in $patterns) {
            if ($p.DisplayName -like "*$pat*") { $hit = $true; break }
        }
        if (-not $hit) { return }
        Write-Host "Removing uninstall key: $($p.DisplayName)"
        Remove-Item -Recurse -Force $_.PSPath -ErrorAction SilentlyContinue
    }
}

$dirs = @(
    "$env:LOCALAPPDATA\Programs\SimMasterQt",
    "$env:ProgramFiles\SimMasterQt",
    "${env:ProgramFiles(x86)}\SimMasterQt"
)
foreach ($d in $dirs) {
    if (Test-Path $d) {
        Write-Host "Removing install dir: $d"
        Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
    }
}

Write-Host "Legacy NSIS cleanup done."
