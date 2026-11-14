@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Test-NativeHost.ps1"
set "exitcode=%ERRORLEVEL%"
echo.
if not "%exitcode%"=="0" echo Native bridge acceptance failed; review evidence ZIP.
if "%exitcode%"=="0" echo Native bridge acceptance passed; still needs Godot F7 verification.
pause
exit /b %exitcode%
