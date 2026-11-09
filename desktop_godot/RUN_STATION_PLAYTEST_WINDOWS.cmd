@echo off
setlocal
cd /d "%~dp0"
echo Single-Galaxy PLAY-STATION-02 - precompiled Windows native host game
echo No CMake, Visual Studio, Python, Godot download, or game export templates required.
echo Uses your EXISTING Godot 4.7.2 editor.
echo.
PowerShell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Launch-PrebuiltStation.ps1"
set "rc=%ERRORLEVEL%"
echo.
if not "%rc%"=="0" (
  echo PLAYTEST BLOCKED - see playtest-logs and SingleGalaxy_Station_Playtest_Evidence.zip
  echo If native hosts are missing, download the compiled Windows artifact from PR 119 Actions.
) else (
  echo Godot window exited; camera and human gameplay verification are separate.
)
pause
exit /b %rc%
