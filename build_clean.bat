@echo off
REM ===================================================================
REM  ⚠️ 已弃用（DEPRECATED）—— 请改用 build.bat clean
REM  本脚本基于 qmake（.pro），主构建系统已迁移到 CMake。
REM ===================================================================
REM  Clean rebuild using the CORRECT 64-bit Qt MinGW toolchain.
REM  This script prepends the Qt mingw1120_64 bin to PATH so that the
REM  Makefile's bare `g++` / `ld` resolve to the 64-bit compiler
REM  instead of D:\OpenGL\MinGW\MinGW\bin (32-bit) which breaks linking.
REM ===================================================================
setlocal

set "QTBIN=D:\qt6\Tools\mingw1120_64\bin"
set "MAKE=%QTBIN%\mingw32-make.exe"
set "APPDIR=%~dp0build\app"
set "LOGDIR=%~dp0build"

set "PATH=%QTBIN%;%PATH%"

echo [1/3] Cleaning stale objects (they may be 32-bit)...
if exist "%APPDIR%\build\obj" rmdir /s /q "%APPDIR%\build\obj"
mkdir "%APPDIR%\build\obj" 2>nul

echo [2/3] Building with Qt MinGW 11.2.0 64-bit...
cd /d "%APPDIR%"
"%MAKE%" -f Makefile.Release > "%LOGDIR%\clean_build.log" 2>&1

echo [3/3] Checking result...
if exist "%APPDIR%\bin\Wafer_Defect_Visualization.exe" (
    echo SUCCESS: exe built.
    dir "%APPDIR%\bin\Wafer_Defect_Visualization.exe"
) else (
    echo FAILED. See "%LOGDIR%\clean_build.log"
)

endlocal
