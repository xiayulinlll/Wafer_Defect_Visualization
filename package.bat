@echo off
REM =====================================================================
REM  打包发布：windeployqt + 运行期资源拷贝
REM  用法: package.bat [构建目录] [Qt bin 目录]
REM        所有参数均可留空，脚本会依据环境变量 QT_ROOT / 默认路径自动探测
REM  输出: dist\Wafer_Defect_Visualization\  (可直接拷贝到其他机器运行)
REM =====================================================================
setlocal EnableDelayedExpansion
set "TRACE=%TEMP%\wafer_package_trace.log"
echo [TRACE] start > "%TRACE%"

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%~1"
set "QT_BIN=%~2"
echo [TRACE] SCRIPT_DIR=%SCRIPT_DIR% >> "%TRACE%"
echo [TRACE] BUILD_DIR=%BUILD_DIR% >> "%TRACE%"
echo [TRACE] QT_BIN=%QT_BIN% >> "%TRACE%"

if "%BUILD_DIR%"=="" set "BUILD_DIR=%SCRIPT_DIR%build\cmake"
echo [TRACE] BUILD_DIR(resolved)=%BUILD_DIR% >> "%TRACE%"

REM ---- 定位 Qt 安装（优先命令行参数 > QT_ROOT > 常见默认路径） ----
if "%QT_BIN%"=="" if not "%QT_ROOT%"=="" set "QT_BIN=%QT_ROOT%\bin"
if "%QT_BIN%"=="" if exist "D:\qt6\6.2.4\mingw_64\bin\windeployqt.exe" set "QT_BIN=D:\qt6\6.2.4\mingw_64\bin"
if "%QT_BIN%"=="" if exist "D:\Qt\Qt6.2.4\bin\windeployqt.exe" set "QT_BIN=D:\Qt\Qt6.2.4\bin"
if "%QT_BIN%"=="" if exist "C:\Qt\6.2.4\msvc2019_64\bin\windeployqt.exe" set "QT_BIN=C:\Qt\6.2.4\msvc2019_64\bin"
echo [TRACE] QT_BIN(final)=%QT_BIN% >> "%TRACE%"
if "%QT_BIN%"=="" (
    echo [错误] 未找到 Qt 安装目录。
    echo        请设置环境变量 QT_ROOT，例如:  set QT_ROOT=D:\qt6\6.2.4\mingw_64
    echo        或传入第二个参数:  package.bat build\cmake D:\qt6\6.2.4\mingw_64\bin
    exit /b 1
)

set "WINDEPLOYQT=%QT_BIN%\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    echo [错误] 未找到 windeployqt: %WINDEPLOYQT%
    exit /b 1
)

set "APP_EXE=%BUILD_DIR%\bin\Wafer_Defect_Visualization.exe"
echo [TRACE] APP_EXE=%APP_EXE% >> "%TRACE%"
if not exist "%APP_EXE%" (
    echo [错误] 未找到可执行文件: %APP_EXE%
    echo        请先执行 build.bat 完成构建。
    echo [TRACE]   ^^ 可执行文件缺失 >> "%TRACE%"
    exit /b 1
)

set "DIST=%SCRIPT_DIR%dist\Wafer_Defect_Visualization"
echo [信息] 清理并创建发布目录: %DIST%
echo [TRACE] DIST=%DIST% >> "%TRACE%"
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"
echo [TRACE] mkdir done, exists= [%DIST%] >> "%TRACE%"
if not exist "%DIST%" (
    echo [错误] 无法创建发布目录: %DIST%
    exit /b 1
)

echo [信息] 拷贝可执行文件...
copy /y "%APP_EXE%" "%DIST%\" >nul
if errorlevel 1 (
    echo [错误] 拷贝可执行文件失败。
    exit /b 1
)

echo [信息] 拷贝示例数据集...
if not exist "%DIST%\data" mkdir "%DIST%\data"
copy /y "%SCRIPT_DIR%data\wafer_defects.csv" "%DIST%\data\" >nul

echo [信息] 执行 windeployqt（收集 Qt DLL 与插件）...
"%WINDEPLOYQT%" --release --no-translations --no-system-d3d-compiler --no-opengl-sw ^
    --compiler-runtime "%DIST%\Wafer_Defect_Visualization.exe"
if errorlevel 1 (
    echo [错误] windeployqt 执行失败。
    exit /b 1
)

echo.
echo [完成] 发布包已生成: %DIST%
echo        该目录可整包拷贝到未安装 Qt 的机器直接运行。
echo [TRACE] success >> "%TRACE%"
endlocal
