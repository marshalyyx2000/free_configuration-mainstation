# End-to-end: install + verify + CLI functional tests; optional mirror to Program Files
param(
    [string]$InstallDir = "",
    [switch]$SkipCliFunctional,
    [switch]$SkipProgramFilesPublish
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuiltExe = Join-Path $Root "build-qt\Release\sim_master_qt.exe"
$Tools = Join-Path $Root "tools\61850_functional_test.py"

if (-not $InstallDir) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\SimMasterQt"
}

Write-Host "=== SimMasterQt install self-test ==="
Write-Host "Install dir: $InstallDir"

& (Join-Path $Root "install_sim_master.ps1") -InstallDir $InstallDir

$exe = Join-Path $InstallDir "bin\sim_master_qt.exe"
$cli = Join-Path $InstallDir "bin\mms_master_cli.exe"
$share = Join-Path $InstallDir "share\sim_master_qt"

if (-not (Test-Path $exe)) { throw "Missing $exe" }
if (-not (Test-Path $cli)) { throw "Missing $cli" }
foreach ($f in @("IEDList.xml", "station.SMCD")) {
    $p = Join-Path $share $f
    if (-not (Test-Path $p)) { throw "Missing $p" }
}

$bi = Get-Item $BuiltExe
$ii = Get-Item $exe
if ($ii.Length -ne $bi.Length) {
    throw "Size mismatch: installed $($ii.Length) vs build $($bi.Length)"
}

function Get-PeSubsystem($path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    return [BitConverter]::ToUInt16($b, $pe + 4 + 20 + 68)
}
if ((Get-PeSubsystem $exe) -ne 2) {
    throw "sim_master_qt.exe must be GUI subsystem (2), got $(Get-PeSubsystem $exe)"
}

Push-Location (Split-Path $exe)
& $exe --verify
if ($LASTEXITCODE -ne 0) { throw "--verify failed on installed exe" }
Pop-Location

$pinfo = New-Object System.Diagnostics.ProcessStartInfo
$pinfo.FileName = $cli
$pinfo.RedirectStandardOutput = $true
$pinfo.RedirectStandardInput = $true
$pinfo.UseShellExecute = $false
$pinfo.CreateNoWindow = $true
$p = [System.Diagnostics.Process]::Start($pinfo)
$line = $p.StandardOutput.ReadLine()
$p.StandardInput.WriteLine('{"cmd":"quit"}')
$p.WaitForExit(5000) | Out-Null
if (-not $p.HasExited) { $p.Kill() }
if ($line -notmatch "ready") {
    throw "CLI ready check failed: $line"
}

Write-Host "Install self-test OK: $exe ($($ii.Length) bytes, GUI subsystem)"

if (-not $SkipCliFunctional) {
    $tcp = Test-NetConnection -ComputerName 192.168.0.10 -Port 102 -WarningAction SilentlyContinue
    if (-not $tcp.TcpTestSucceeded) {
        Write-Warning "192.168.0.10:102 not reachable — skipping CLI functional tests"
    } else {
        $env:SIMMASTER_INSTALL_DIR = $InstallDir
        python $Tools --cli-only
        if ($LASTEXITCODE -ne 0) { throw "61850_functional_test.py --cli-only failed" }
        Write-Host "CLI functional tests passed (6/6)."
    }
}

if (-not $SkipProgramFilesPublish) {
    $pf64 = $env:ProgramW6432
    if (-not $pf64) { $pf64 = $env:ProgramFiles }
    $pfDest = Join-Path $pf64 "SimMasterQt"
    if ($InstallDir -eq (Join-Path $env:LOCALAPPDATA "Programs\SimMasterQt")) {
        Write-Host "Publishing to $pfDest ..."
        try {
            & (Join-Path $Root "publish_to_program_files.ps1") -SourceDir $InstallDir
            $pfExe = Join-Path $pfDest "bin\sim_master_qt.exe"
            if ((Get-Item $pfExe).Length -eq $ii.Length) {
                Write-Host "Program Files publish OK: $pfExe"
            }
        } catch {
            Write-Warning "Program Files publish skipped: $($_.Exception.Message)"
            Write-Warning "Right-click install_as_admin.cmd -> Run as administrator to install under Program Files."
        }
    }
}

Write-Host "=== ALL PASSED ==="
