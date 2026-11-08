@echo off
setlocal
cd /d "%~dp0"
echo Single-Galaxy Standalone Graybox - official Godot Windows build and smoke tests
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Build-Windows.ps1"
set "SG_RESULT=%ERRORLEVEL%"
if not "%SG_RESULT%"=="0" (
  echo.
  echo Build or acceptance failed. Upload SingleGalaxy_Windows_Build_Evidence.zip to the Single Galaxy Library.
) else (
  echo.
  echo Build succeeded. Standalone executable: SingleGalaxy_Graybox_Windows_x64.zip
  echo NOTE: interactive GPU and control acceptance is still required on this PC.
)
pause
exit /b %SG_RESULT%
