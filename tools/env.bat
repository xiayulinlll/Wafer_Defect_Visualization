@echo off
REM ===================================================================
REM  Qt6 + MinGW build environment
REM
REM  Qt 6.2.4 (mingw_64) was built with MinGW 11.2.0.
REM  See: mkspecs\qconfig.pri  ->  QT_GCC_MAJOR_VERSION=11 MINOR=2
REM
REM  You MUST use mingw1120_64. Using mingw810_64 breaks header/ABI
REM  compatibility and its <filesystem> does not even compile.
REM
REM  IMPORTANT: PATH is *replaced*, not prepended. The machine's system
REM  PATH contains other MinGW installs (e.g. d:\opengl\mingw, a 32-bit
REM  gcc 9.2.0). Tools like moc.exe inherit PATH and would pick up those
REM  foreign -I include paths, producing bogus errors such as
REM  "QtCore/qnamespace.:56: Parse error at __attribute__".
REM ===================================================================
set QTDIR=D:\qt6\6.2.4\mingw_64
set MINGWDIR=D:\qt6\Tools\mingw1120_64
set PATH=%MINGWDIR%\bin;%QTDIR%\bin;%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem
%*



