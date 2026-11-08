@echo off
setlocal
cd /d "%~dp0"
PowerShell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Test-NativeFPS.ps1"
echo.
echo Upload SingleGalaxy_NativeFPS_Windows_Evidence.zip if the test reports a failure.
pause
