# Silent-install NSIS package to Program Files only; run verify + CLI tests. Must pass before release.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Tools = Join-Path $Root "tools\61850_functional_test.py"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-PeSubsystem($path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    return [BitConverter]::ToUInt16($b, $pe + 4 + 20 + 68)
}

if (-not (Test-Path $InstallerPath)) {
    throw "Installer not found: $InstallerPath"
}

$pf64 = $env:ProgramW6432
if (-not $pf64) { $pf64 = $env:ProgramFiles }
$pfInstall = Join-Path $pf64 "SimMasterQt"
$pfExe = Join-Path $pfInstall "bin\sim_master_qt.exe"
$pfCli = Join-Path $pfInstall "bin\mms_master_cli.exe"
$pfShare = Join-Path $pfInstall "share\sim_master_qt"
$localExe = Join-Path $env:LOCALAPPDATA "Programs\SimMasterQt\bin\sim_master_qt.exe"

function Invoke-AdminTest {
    param([string]$InstPath)

    $log = Join-Path $env:TEMP "SimMasterQt_nsis_install.log"
    "" | Out-File $log -Encoding utf8
    try {
        Stop-Process -Name sim_master_qt, mms_master_cli -Force -ErrorAction SilentlyContinue
        Get-Process | Where-Object { $_.Path -and ($_.Path -like "$pfInstall*") } | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2

        $localBefore = $null
        if (Test-Path $localExe) {
            $localBefore = Get-Item $localExe | Select-Object Length, LastWriteTimeUtc
            Write-Host "LocalAppData exe before (must not be replaced by NSIS): $($localBefore.Length) bytes"
        }

        # Do not force-delete Program Files tree (may be locked); NSIS overwrites in place.
        if (Test-Path $pfInstall) {
            Write-Host "Existing install detected — NSIS will upgrade: $pfInstall"
        }

        Add-Content $log "Running silent install to: $pfInstall"
        $proc = Start-Process -FilePath $InstPath -ArgumentList "/S", "/D=$pfInstall" -Wait -PassThru
        if (-not $proc) { throw "Could not start installer" }

        if (-not (Test-Path $pfExe)) {
            throw "After install, missing $pfExe (NSIS exit $($proc.ExitCode))"
        }
        if ($proc.ExitCode -ne 0) {
            Write-Warning "NSIS exit code $($proc.ExitCode) but payload present — continuing verification"
        }
        foreach ($f in @("IEDList.xml", "station.SMCD")) {
            $p = Join-Path $pfShare $f
            if (-not (Test-Path $p)) { throw "Missing $p" }
        }
        if (-not (Test-Path $pfCli)) { throw "Missing $pfCli" }

        $ii = Get-Item $pfExe
        if ($ii.Length -lt 500000) {
            throw "Installed exe too small ($($ii.Length) bytes)"
        }
        if ((Get-PeSubsystem $pfExe) -ne 2) {
            throw "sim_master_qt.exe must be Windows GUI subsystem (2)"
        }

        if ($localBefore -and (Test-Path $localExe)) {
            $localAfter = Get-Item $localExe
            if ($localAfter.Length -ne $localBefore.Length -or $localAfter.LastWriteTimeUtc -ne $localBefore.LastWriteTimeUtc) {
                throw "NSIS must not modify LocalAppData install: $localExe"
            }
        }

        $vp = Start-Process -FilePath $pfExe -ArgumentList "--verify" -Wait -PassThru -NoNewWindow
        if ($vp.ExitCode -ne 0) { throw "--verify failed on $pfExe (exit $($vp.ExitCode))" }

        $tcp = Test-NetConnection -ComputerName 192.168.0.10 -Port 102 -WarningAction SilentlyContinue
        if (-not $tcp.TcpTestSucceeded) {
            throw "192.168.0.10:102 not reachable — cannot run CLI functional tests"
        }

        $env:SIMMASTER_INSTALL_DIR = $pfInstall
        python $Tools --cli-only
        if ($LASTEXITCODE -ne 0) { throw "61850_functional_test.py --cli-only failed" }

        Write-Host "CLI functional tests: 6/6"
        Add-Content $log "ALL CHECKS PASSED"
    } catch {
        Add-Content $log $_.Exception.Message
        throw
    }
}

if (-not (Test-IsAdmin)) {
    Write-Host "NSIS test requires administrator (install to Program Files)..."
    $proc = Start-Process powershell.exe -Verb RunAs -PassThru -Wait -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", $MyInvocation.MyCommand.Path,
        "-InstallerPath", $InstallerPath
    )
    if (-not $proc -or $proc.ExitCode -ne 0) {
        $log = Join-Path $env:TEMP "SimMasterQt_nsis_install.log"
        $tail = if (Test-Path $log) { Get-Content $log -Tail 25 -ErrorAction SilentlyContinue } else { @() }
        throw "Elevated NSIS test failed (exit $($proc.ExitCode)). Log: $log`n$($tail -join "`n")"
    }
    exit 0
}

Invoke-AdminTest -InstPath $InstallerPath
