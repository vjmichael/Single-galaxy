@echo off
setlocal
cd /d "%~dp0"
echo Single-Galaxy PLAY-STATION-02: station to cockpit original C++ test
echo Uses your EXISTING Godot 4.7.2 editor; no export templates or editor download.
echo A supported Windows C++ compiler, CMake and SQLite headers/library are required.
echo.
PowerShell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Test-StationGodot.ps1"
set "rc=%ERRORLEVEL%"
echo.
if not "%rc%"=="0" (
  echo ACCEPTANCE NOT PASSED - see build\station-visual-acceptance\logs and evidence ZIP.
) else (
  echo Native and headless Godot tests passed; human camera/visual gameplay still needs review.
)
echo Evidence: SingleGalaxy_Station_Visual_Windows_Evidence.zip
pause
exit /b %rc%
