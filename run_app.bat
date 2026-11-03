@echo off
REM ===================================================================
REM  ⚠️ 已弃用（DEPRECATED）—— 请改用 run.bat 或 tools\smoke_test.bat
REM  本脚本指向 qmake 版构建目录（build\app），CMake 版产物在 build\cmake\bin。
REM ===================================================================
REM  run_app.bat - 正常启动程序
REM
REM  作用：把 Qt6 / MinGW 运行时 DLL 所在目录加进 PATH，然后启动 exe。
REM  程序会一直运行，直到你自己关闭窗口 —— 本脚本不做任何自动退出。
REM ===================================================================
setlocal
set ROOT=%~dp0
set BIN=%ROOT%build\app\bin

REM 运行时 DLL 必须能找到（Qt6 + MinGW 11.2.0）
set PATH=D:\qt6\Tools\mingw1120_64\bin;D:\qt6\6.2.4\mingw_64\bin;C:\Windows\System32;C:\Windows

cd /d "%BIN%"
if not exist "Wafer_Defect_Visualization.exe" (
    echo ERROR: 未找到可执行文件：
    echo        %BIN%\Wafer_Defect_Visualization.exe
    echo 请先运行 build_all.bat 进行编译。
    echo.
    pause
    endlocal
    exit /b 1
)

echo 启动目录: %CD%
echo 数据目录: %CD%\data
echo.

REM 独立进程启动。脚本窗口随即关闭，主程序继续运行。
start "Wafer_Defect_Visualization" "Wafer_Defect_Visualization.exe"

endlocal
exit /b 0

