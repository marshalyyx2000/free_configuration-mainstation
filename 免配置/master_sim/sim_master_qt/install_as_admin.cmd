@echo off
cd /d "%~dp0"
echo Step 1: build + install to %%LOCALAPPDATA%%\Programs\SimMasterQt and run CLI tests...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_install_selftest.ps1" -SkipProgramFilesPublish
if errorlevel 1 goto fail
echo.
echo Step 2: mirror to Program Files (UAC - click Yes)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish_to_program_files.ps1"
if errorlevel 1 goto fail
echo.
echo Done. Launch: "%ProgramFiles%\SimMasterQt\bin\sim_master_qt.exe"
pause
exit /b 0
:fail
echo FAILED.
pause
exit /b 1
