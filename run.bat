@echo off
REM =====================================================================
REM  运行程序（自动从构建目录定位可执行文件，零硬编码路径）
REM  用法: run.bat [构建目录]
REM =====================================================================
setlocal

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%SCRIPT_DIR%build\ninja"

set "APP_EXE=%BUILD_DIR%\bin\Wafer_Defect_Visualization.exe"
if not exist "%APP_EXE%" (
    if exist "%SCRIPT_DIR%build\vs2022\bin\Release\Wafer_Defect_Visualization.exe" (
        set "APP_EXE=%SCRIPT_DIR%build\vs2022\bin\Release\Wafer_Defect_Visualization.exe"
    )
)

if not exist "%APP_EXE%" (
    echo [错误] 未找到可执行文件，请先执行: build.bat
    exit /b 1
)

echo [信息] 启动: %APP_EXE%
start "" "%APP_EXE%"
endlocal
