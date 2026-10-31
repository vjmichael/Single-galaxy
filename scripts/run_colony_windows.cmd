@echo off
setlocal
cd /d "%~dp0.."
set "HOST=build\Release\galaxy_colony_live_host.exe"
if not exist "%HOST%" set "HOST=build\galaxy_colony_live_host.exe"
if not exist "%HOST%" (
 echo Build galaxy_colony_live_host with MSVC and SQLite first.
 pause
 exit /b 2
)
where py >nul 2>&1
if errorlevel 1 (
 echo Python 3 required by the local testing relay.
 pause
 exit /b 3
)
set "SAVE=%LOCALAPPDATA%\SingleGalaxy\colony.sqlite"
if not exist "%LOCALAPPDATA%\SingleGalaxy" mkdir "%LOCALAPPDATA%\SingleGalaxy"
start "" "http://127.0.0.1:8766/viewer/interactive.html"
py -3 live\interactive_relay.py --host-exe "%HOST%" --colony --save "%SAVE%" --port 8766 --tick-ms 100 --max-ticks 1200
if errorlevel 1 pause
