@echo off
REM =====================================================================
REM  CMake 构建脚本（跨机器可用，零硬编码路径）
REM
REM  用法:
REM      build.bat                 配置 + 构建（Release，Ninja）
REM      build.bat clean           清理构建目录后重新构建
REM      build.bat test            构建并运行单元测试
REM      build.bat debug           以 Debug 配置构建
REM      build.bat vs              使用 Visual Studio 2022 生成器
REM
REM  Qt 定位优先级:
REM      1) 环境变量 Qt6_DIR / CMAKE_PREFIX_PATH
REM      2) 环境变量 QT_ROOT  ->  %QT_ROOT%\lib\cmake
REM      3) 常见默认安装路径自动探测
REM =====================================================================
setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "MODE=%~1"
set "BUILD_TYPE=Release"
set "GENERATOR=Ninja"
set "BUILD_DIR=%SCRIPT_DIR%build\ninja"
set "CLEAN=0"
set "RUN_TESTS=0"

if /i "%MODE%"=="clean" (
    set "CLEAN=1"
) else if /i "%MODE%"=="test" (
    set "RUN_TESTS=1"
) else if /i "%MODE%"=="debug" (
    set "BUILD_TYPE=Debug"
    set "BUILD_DIR=%SCRIPT_DIR%build\debug"
) else if /i "%MODE%"=="vs" (
    set "GENERATOR=Visual Studio 17 2022"
    set "BUILD_DIR=%SCRIPT_DIR%build\vs2022"
)

REM ---- 定位 Qt ----
set "QT_CMAKE_PREFIX="
if not "%Qt6_DIR%"=="" set "QT_CMAKE_PREFIX=%Qt6_DIR%"
if "%QT_CMAKE_PREFIX%"=="" if not "%CMAKE_PREFIX_PATH%"=="" set "QT_CMAKE_PREFIX=%CMAKE_PREFIX_PATH%"
if "%QT_CMAKE_PREFIX%"=="" if not "%QT_ROOT%"=="" set "QT_CMAKE_PREFIX=%QT_ROOT%"

if "%QT_CMAKE_PREFIX%"=="" (
    for %%P in (
        "D:\qt6\6.2.4\mingw_64"
        "D:\Qt\Qt6.2.4"
        "C:\Qt\Qt6.2.4\msvc2019_64"
        "C:\Qt\6.2.4\msvc2019_64"
        "%USERPROFILE%\Qt\6.2.4\msvc2019_64"
    ) do (
        if "!QT_CMAKE_PREFIX!"=="" if exist "%%~P\lib\cmake\Qt6\Qt6Config.cmake" (
            set "QT_CMAKE_PREFIX=%%~P"
        )
    )
)

if "%QT_CMAKE_PREFIX%"=="" (
    echo [错误] 未找到 Qt6 安装目录。
    echo        请设置环境变量 Qt6_DIR 指向 Qt 的 lib\cmake 目录，例如:
    echo            set Qt6_DIR=D:\Qt\Qt6.2.4\lib\cmake
    echo        或设置 QT_ROOT 指向 Qt 根目录:
    echo            set QT_ROOT=D:\Qt\Qt6.2.4
    exit /b 1
)
echo [信息] Qt 前缀: %QT_CMAKE_PREFIX%

REM ---- 定位 CMake / Ninja ----
where cmake >nul 2>nul
if errorlevel 1 (
    echo [错误] 未在 PATH 中找到 cmake。请安装 CMake 3.16+ 并加入 PATH。
    exit /b 1
)
if /i "%GENERATOR%"=="Ninja" (
    where ninja >nul 2>nul
    if errorlevel 1 (
        echo [警告] 未找到 ninja，回退到 Visual Studio 生成器。
        set "GENERATOR=Visual Studio 17 2022"
        set "BUILD_DIR=%SCRIPT_DIR%build\vs2022"
    )
)

REM ---- 清理 ----
if "%CLEAN%"=="1" (
    echo [信息] 清理构建目录: %BUILD_DIR%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

REM ---- 配置 ----
echo [信息] 配置项目 (生成器: %GENERATOR%, 类型: %BUILD_TYPE%)
cmake -S "%SCRIPT_DIR%." -B "%BUILD_DIR%" -G "%GENERATOR%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_PREFIX_PATH="%QT_CMAKE_PREFIX%" ^
    -DWAFER_BUILD_TESTS=ON ^
    -DWAFER_ENABLE_TRANSLATIONS=ON
if errorlevel 1 (
    echo [错误] CMake 配置失败。
    exit /b 1
)

REM ---- 构建 ----
echo [信息] 开始构建...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo [错误] 构建失败。
    exit /b 1
)

echo.
echo [完成] 可执行文件: %BUILD_DIR%\bin\Wafer_Defect_Visualization.exe

REM ---- 测试 ----
if "%RUN_TESTS%"=="1" (
    echo.
    echo [信息] 运行单元测试...
    ctest --test-dir "%BUILD_DIR%" -C %BUILD_TYPE% --output-on-failure
    if errorlevel 1 (
        echo [错误] 单元测试失败。
        exit /b 1
    )
    echo [完成] 单元测试全部通过。
)

endlocal
