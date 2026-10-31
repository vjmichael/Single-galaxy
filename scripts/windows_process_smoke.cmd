@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
set "BIN=%CD%\build\Release\galaxy_persistence_demo.exe"
if not exist "%BIN%" (
  echo FAIL: Release galaxy_persistence_demo.exe not built
  exit /b 2
)
set "ROOT=%TEMP%\galaxy_b2_smoke_%RANDOM%_%RANDOM%"
mkdir "%ROOT%" || exit /b 2
"%BIN%" "%ROOT%\normal.sqlite" init
if errorlevel 1 goto fail
"%BIN%" "%ROOT%\normal.sqlite" resume
if errorlevel 1 goto fail
"%BIN%" "%ROOT%\abrupt.sqlite" crash-init
if not errorlevel 77 goto fail
if errorlevel 78 goto fail
"%BIN%" "%ROOT%\abrupt.sqlite" resume
if errorlevel 1 goto fail
rmdir /s /q "%ROOT%"
echo PASS Windows two-process restart and abrupt-exit smoke
exit /b 0
:fail
echo FAIL Windows two-process restart smoke
rmdir /s /q "%ROOT%"
exit /b 1
