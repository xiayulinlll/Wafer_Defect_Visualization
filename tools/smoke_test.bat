@echo off
REM =====================================================================
REM  CMake 版冒烟测试：拉起程序 -> 存活检查 -> 自动收尾
REM  依据 docs/BUILD_TROUBLESHOOTING.md 的判定标准：
REM    tasklist 能列出进程 => 窗口 + OpenGL 3.3 上下文创建成功
REM =====================================================================
setlocal EnableDelayedExpansion
set "PROJ=d:\qt6.2.4_project\Wafer_Defect_Visualization"
set "MINGW=d:\qt6\Tools\mingw1120_64"
set "QTDIR=d:\qt6\6.2.4\mingw_64"
set "BINDIR=%PROJ%\build\cmake\bin"

set "PATH=%MINGW%\bin;%QTDIR%\bin;C:\Windows\System32;C:\Windows"

set "EXE=%BINDIR%\Wafer_Defect_Visualization.exe"
if not exist "%EXE%" (
    echo [错误] 未找到可执行文件: %EXE%
    exit /b 1
)

echo [信息] 启动 %EXE%
cd /d "%BINDIR%"
start "" "%EXE%"

REM 用 ping 代替 sleep（无 tty 也可靠），等待约 10 秒
ping -n 11 127.0.0.1 >nul 2>&1

echo [信息] 进程状态：
tasklist /fi "imagename eq Wafer_Defect_Visualization.exe" > "%PROJ%\build\smoke_status.txt" 2>&1
type "%PROJ%\build\smoke_status.txt"

echo [信息] 在途日志文件：
dir /b "%BINDIR%\logs" > "%PROJ%\build\smoke_logs.txt" 2>&1
type "%PROJ%\build\smoke_logs.txt"

taskkill /f /im Wafer_Defect_Visualization.exe >> "%PROJ%\build\smoke_status.txt" 2>&1
echo [完成] 冒烟测试结束，结果见 build\smoke_status.txt
endlocal
