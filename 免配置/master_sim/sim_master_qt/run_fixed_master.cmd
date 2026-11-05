@echo off
rem Run the fixed master simulator from the build tree.
rem The installed copy under Program Files still has the old mms_master_cli.exe
rem (no admin rights to replace it). This launcher uses the patched CLI in
rem build-qt\Release and the Qt runtime from the installed package.
setlocal
set "PATH=C:\Program Files\SimMasterQt\bin;%PATH%"
set "QT_PLUGIN_PATH=C:\Program Files\SimMasterQt\bin"
start "" "%~dp0build-qt\Release\sim_master_qt.exe"
