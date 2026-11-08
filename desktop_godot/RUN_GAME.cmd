@echo off
setlocal
cd /d "%~dp0"
if not exist "build\SingleGalaxy_Graybox.exe" (
 echo Game not exported. Double-click BUILD_WINDOWS.cmd first.
 pause
 exit /b 1
)
start "" "build\SingleGalaxy_Graybox.exe"
