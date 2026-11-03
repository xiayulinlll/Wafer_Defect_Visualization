@echo off
REM ===================================================================
REM  ⚠️ 已弃用（DEPRECATED）—— 请改用 tools\build_cmake.bat
REM  本脚本基于 qmake（.pro），主构建系统已迁移到 CMake。
REM ===================================================================
setlocal
set "PATH=D:\qt6\Tools\mingw1120_64\bin;%PATH%"
cd /d d:\qt6.2.4_project\Wafer_Defect_Visualization\build\app
if exist build\obj rmdir /s /q build\obj
mkdir build\obj 2>nul
D:\qt6\Tools\mingw1120_64\bin\mingw32-make.exe -f Makefile.Release > ..\clean_final.log 2>&1
echo BUILD_EXIT=%ERRORLEVEL%
if exist bin\Wafer_Defect_Visualization.exe (
  echo EXE_OK
  for %%F in (bin\Wafer_Defect_Visualization.exe) do echo SIZE=%%~zF
) else (
  echo NO_EXE
)
endlocal
