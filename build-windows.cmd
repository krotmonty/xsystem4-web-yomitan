@echo off
rem Builds the engine and web bundle into dist\ using MSYS2 + emsdk in C:\dev\tools.
setlocal
set MSYSTEM=UCRT64
set CHERE_INVOKING=1
cd /d "%~dp0"
C:\dev\tools\msys64\usr\bin\bash.exe -l ./build-windows.sh
