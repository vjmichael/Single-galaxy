@echo off
setlocal
cd /d "%~dp0"
echo Single-Galaxy PLAY-STATION-02 - precompiled Windows native host game
echo No CMake, Visual Studio, Python, installed Godot editor, download or game export templates required.
echo Includes SHA-256-verified Godot 4.7.2 runtime in the precompiled Windows package.
echo.
PowerShell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Launch-PrebuiltStation.ps1"
set "rc=%ERRORLEVEL%"
echo.
if not "%rc%"=="0" (
  echo PLAYTEST BLOCKED - see playtest-logs and SingleGalaxy_Station_Playtest_Evidence.zip
  echo If native hosts are missing, use the exact-sha compiled Windows ZIP from the SGDX03 task-group Actions run for PR 125.
) else (
  echo Bundled Godot window exited; camera and human gameplay verification are separate.
)
pause
exit /b %rc%
