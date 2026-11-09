@echo off
setlocal
cd /d "%~dp0"
echo PLAY-STATION-02 - VISUAL STATION PREVIEW ONLY
echo Runs existing Godot 4.7.2; NO CMake, compiler, native ship host or Godot download.
echo This tests the Godot scene, not native boarding or mining.
echo.
PowerShell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_tools\Launch-VisualPreview.ps1"
set "rc=%ERRORLEVEL%"
echo.
if not "%rc%"=="0" (
  echo Godot failed. See godot-visual-logs and SingleGalaxy_Godot_Visual_Only_Evidence.zip
) else (
  echo Godot visual scene test completed. Full native gameplay requires the GitHub Windows build.
)
pause
exit /b %rc%
