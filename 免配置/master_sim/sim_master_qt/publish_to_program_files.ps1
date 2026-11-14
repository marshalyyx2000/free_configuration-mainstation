# Copy a verified SimMasterQt tree into Program Files (64-bit). Run as Administrator.
param(
    [string]$SourceDir = "$env:LOCALAPPDATA\Programs\SimMasterQt"
)

$ErrorActionPreference = "Stop"
$pf64 = $env:ProgramW6432
if (-not $pf64) { $pf64 = $env:ProgramFiles }
$DestDir = Join-Path $pf64 "SimMasterQt"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdmin)) {
    Write-Host "Publishing to Program Files requires administrator — elevating..."
    $proc = Start-Process powershell.exe -Verb RunAs -PassThru -Wait -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", $MyInvocation.MyCommand.Path,
        "-SourceDir", $SourceDir
    )
    if (-not $proc -or $proc.ExitCode -ne 0) {
        throw "publish_to_program_files failed (exit $($proc.ExitCode))"
    }
    exit 0
}

if (-not (Test-Path (Join-Path $SourceDir "bin\sim_master_qt.exe"))) {
    throw "Source not installed: $SourceDir — run install_sim_master.ps1 first"
}

Stop-Process -Name sim_master_qt -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

if (Test-Path $DestDir) {
    Remove-Item -Recurse -Force $DestDir
}
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

robocopy $SourceDir $DestDir /MIR /R:2 /W:2 /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with code $LASTEXITCODE" }

$exe = Join-Path $DestDir "bin\sim_master_qt.exe"
$srcExe = Join-Path $SourceDir "bin\sim_master_qt.exe"
if ((Get-Item $exe).Length -ne (Get-Item $srcExe).Length) {
    throw "Publish verification failed: size mismatch"
}

Push-Location (Split-Path $exe)
& $exe --verify
if ($LASTEXITCODE -ne 0) { throw "--verify failed in Program Files" }
Pop-Location

Write-Host "Published to: $DestDir"
Write-Host "  sim_master_qt.exe $((Get-Item $exe).Length) bytes"
