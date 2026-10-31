@echo off
setlocal
cd /d "%~dp0.."
set "HOST=.build\Release\galaxy_interactive_host.exe"
if not exist "%HOST%" set "HOST=.build\galaxy_interactive_host.exe"
if not exist "%HOST%" (
  echo Build galaxy_interactive_host with MSVC and verified SQLite first.
  pause
  exit /b 2
)
where py >nul 2>&1
if errorlevel 1 (
  echo This local development relay requires Python 3.
  pause
  exit /b 3
)
start "" "http://127.0.0.1:8766/viewer/interactive.html"
py -3 live\interactive_relay.py --host-exe "%HOST%" --port 8766 --tick-ms 100 --max-ticks 600
if errorlevel 1 pause
