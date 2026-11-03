@echo off
REM ===================================================================
REM  ⚠️ 已弃用（DEPRECATED）—— 请改用 build.bat / tools\build_cmake.bat
REM
REM  本脚本基于 qmake（.pro），主构建系统已迁移到 CMake。
REM  保留仅供历史参考，不再维护。
REM ===================================================================
REM  build_all.bat - clean rebuild of the app with a *sanitized* PATH
REM
REM  Key point: qmake derives moc's -I include paths from the g++ it
REM  finds on PATH. If the system PATH leaks d:\opengl\mingw (32-bit
REM  gcc 9.2.0), those bogus paths get baked into the Makefile and moc
REM  then emits "QtCore/qnamespace.:56: Parse error at __attribute__".
REM  So we wipe PATH down to the correct toolchain only, then qmake.
REM ===================================================================
setlocal
set PATH=C:\Windows\System32;C:\Windows

set ROOT=%~dp0
set BLD=%ROOT%build\app
set LOG=%ROOT%build\build_all.log

echo === build_all.bat === >  "%LOG%"

REM --- minimal, correct PATH: Qt first, then the right MinGW ---
set QTDIR=D:\qt6\6.2.4\mingw_64
set MINGWDIR=D:\qt6\Tools\mingw1120_64
set PATH=%MINGWDIR%\bin;%QTDIR%\bin;C:\Windows\System32;C:\Windows

echo --- PATH --- >> "%LOG%"
echo %PATH% >> "%LOG%"
echo --- g++ --- >> "%LOG%"
where g++ >> "%LOG%" 2>&1
g++ --version >> "%LOG%" 2>&1
echo --- moc --- >> "%LOG%"
where moc >> "%LOG%" 2>&1
echo --- qmake --- >> "%LOG%"
where qmake >> "%LOG%" 2>&1

if not exist "%BLD%" mkdir "%BLD%"
cd /d "%BLD%"

REM --- full clean so nothing stale survives ---
if exist build rmdir /s /q build
del /q Makefile Makefile.Debug Makefile.Release .qmake.stash 2>nul
if exist bin rmdir /s /q bin

echo. >> "%LOG%"
echo --- qmake --- >> "%LOG%"
qmake "%ROOT%Wafer_Defect_Visualization.pro" >> "%LOG%" 2>&1
echo qmake exit=%ERRORLEVEL% >> "%LOG%"

echo. >> "%LOG%"
echo --- check Makefile for foreign include paths --- >> "%LOG%"
findstr /c:"opengl/mingw" /c:"gcc/mingw32" Makefile.Release >> "%LOG%" 2>&1
echo foreign-refs-findstr-exit=%ERRORLEVEL% >> "%LOG%"

echo. >> "%LOG%"
echo --- make --- >> "%LOG%"
mingw32-make -f Makefile >> "%LOG%" 2>&1
echo make exit=%ERRORLEVEL% >> "%LOG%"

echo. >> "%LOG%"
echo --- result --- >> "%LOG%"
dir /b "%BLD%\bin" >> "%LOG%" 2>&1

endlocal
exit /b 0
