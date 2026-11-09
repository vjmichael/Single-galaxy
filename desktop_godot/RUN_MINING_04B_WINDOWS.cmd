@echo off
setlocal
cd /d "%~dp0"
echo Single-Galaxy PLAY-STATION-04B - manufactured drill and one-tonne extraction
echo Requires existing Godot 4.7.2 editor; native hosts and seed are precompiled in this bundle.
echo The finite original SQLite world persists in playtest-data. No free restock.
PowerShell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Start-04BMiningPlaytest.ps1"
set "rc=%ERRORLEVEL%"
if not "%rc%"=="0" echo 04B PLAYTEST BLOCKED - inspect playtest-logs.
pause
exit /b %rc%
