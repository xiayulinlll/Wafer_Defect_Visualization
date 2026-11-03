@echo off
REM 一键构建 cmake 版（使用与 Qt 6.2.4 mingw_64 匹配的 MinGW 11.2.0）
setlocal EnableDelayedExpansion
set "PROJ=d:\qt6.2.4_project\Wafer_Defect_Visualization"
set "CMAKEDIR=d:\CMake\bin"
set "CMAKE=%CMAKEDIR%\cmake.exe"
set "CTEST=%CMAKEDIR%\ctest.exe"
set "NINJA=d:\qt6\Tools\Ninja\ninja.exe"
set "QTDIR=d:\qt6\6.2.4\mingw_64"
set "MINGW=d:\qt6\Tools\mingw1120_64"

REM 关键：替换 PATH，避免系统里 32 位 gcc 9.2.0 污染 qmake/moc 的头路径探测
set "PATH=%MINGW%\bin;%QTDIR%\bin;%CMAKEDIR%;C:\Windows\System32;C:\Windows"

cd /d "%PROJ%"
if not exist build\cmake mkdir build\cmake

echo [1/3] Configure...
"%CMAKE%" -S . -B build\cmake -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_PREFIX_PATH="%QTDIR%" ^
    -DCMAKE_CXX_COMPILER="%MINGW%\bin\g++.exe" ^
    -DWAFER_BUILD_TESTS=ON ^
    -DWAFER_ENABLE_TRANSLATIONS=ON > build\configure.log 2>&1
echo   configure exit=!errorlevel!

echo [2/3] Build...
"%CMAKE%" --build build\cmake --parallel > build\build.log 2>&1
echo   build exit=!errorlevel!

echo [3/3] Test...
"%CTEST%" --test-dir build\cmake --output-on-failure > build\ctest.log 2>&1
echo   ctest exit=!errorlevel!

echo Done. Logs: build\configure.log build\build.log build\ctest.log
endlocal
