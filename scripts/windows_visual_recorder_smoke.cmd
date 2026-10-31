@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
set "BIN=%CD%\build\Release"
if not exist "%BIN%\galaxy_visual_scenario_recorder.exe" (
  echo FAIL: build Release executable first
  exit /b 2
)
set "ROOT=%TEMP%\galaxy_b4_smoke_%RANDOM%_%RANDOM%"
mkdir "%ROOT%" || exit /b 2
for %%S in (ten hold motion) do (
  "%BIN%\galaxy_visual_scenario_recorder.exe" %%S "%ROOT%\%%S.sqlite" > "%ROOT%\%%S.sgview"
  if errorlevel 1 goto fail
  "%BIN%\galaxy_client_wire_consumer.exe" < "%ROOT%\%%S.sgview" > "%ROOT%\%%S.out"
  if errorlevel 1 goto fail
)
findstr /c:"cargo=100/100" "%ROOT%\ten.out" > nul
if errorlevel 1 goto fail
findstr /c:"MINING_STOPPED:CARGO_FULL" "%ROOT%\hold.out" > nul
if errorlevel 1 goto fail
findstr /c:"client tick=12" "%ROOT%\motion.out" > nul
if errorlevel 1 goto fail
rmdir /s /q "%ROOT%"
echo PASS: three B4 visual recorder smoke scenarios
exit /b 0
:fail
echo FAIL: visual recorder smoke scenarios
rmdir /s /q "%ROOT%"
exit /b 1
