@echo off
setlocal
cd /d "%~dp0.."
set "HOST=.build-live\Release\galaxy_live_scenario_host.exe"
if not exist "%HOST%" set "HOST=.build-live\galaxy_live_scenario_host.exe"
if not exist "%HOST%" (
  echo B4 live host not found. Compile on Windows with CMake first, following README.md.
  pause
  exit /b 2
)
where py >nul 2>&1
if errorlevel 1 (
  echo B4 development relay needs Python 3. Install Python or run the native host directly.
  pause
  exit /b 3
)
start "" "http://127.0.0.1:8765/viewer/live.html"
py -3 live\relay.py --host-exe "%HOST%" --port 8765 --tick-ms 100
if errorlevel 1 pause
