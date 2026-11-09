@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0desktop_godot\RUN_STATION_PLAYTEST_WINDOWS.cmd" (
  echo BLOCKED: self-contained Single-Galaxy package missing original game launcher.
  pause
  exit /b 1
)
call "%~dp0desktop_godot\RUN_STATION_PLAYTEST_WINDOWS.cmd"
exit /b %ERRORLEVEL%
