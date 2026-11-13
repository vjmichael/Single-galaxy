; S6-G test-only Windows NSIS acceptance. Never sign/promote from CI.
!ifndef PAYLOAD
  !error "Missing /DPAYLOAD=<reviewed generated original game directory>"
!endif
!ifndef OUTFILE
  !error "Missing /DOUTFILE=<compiled installer path>"
!endif
Unicode true
Name "Single Galaxy Stage 6 Native Acceptance"
OutFile "\${OUTFILE}"
InstallDir "$LOCALAPPDATA\SingleGalaxyStage6Acceptance"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
Section "Install native original game and bundled Godot" SecGame
  SetOutPath "$INSTDIR"
  File /r "\${PAYLOAD}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SingleGalaxyStage6Acceptance" "DisplayName" "Single Galaxy Stage 6 Acceptance"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SingleGalaxyStage6Acceptance" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SingleGalaxyStage6Acceptance" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SingleGalaxyStage6Acceptance" "NoModify" 1
SectionEnd
Section "Uninstall"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SingleGalaxyStage6Acceptance"
  RMDir /r "$INSTDIR\desktop_godot"
  Delete "$INSTDIR\SOURCE_SHA.txt"
  Delete "$INSTDIR\START_SINGLE_GALAXY_WINDOWS.cmd"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
