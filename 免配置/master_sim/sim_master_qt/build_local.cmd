@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
set QT6_DIR=C:\Qt\6.7.3\msvc2019_64
subst Z: "%~dp0" >nul 2>&1
Z:
cd /d Z:\
if exist build-qt rmdir /s /q build-qt
cmake -S . -B build-qt -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=%QT6_DIR% || exit /b 1
cmake --build build-qt --config Release --target sim_master_qt || exit /b 1
if not exist dist mkdir dist
copy /Y build-qt\Release\sim_master_qt.exe dist\sim_master_qt.exe
echo OK build-qt\Release\sim_master_qt.exe
