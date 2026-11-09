# Precompiled original C++ station/cockpit, existing Godot 4.7.2 editor.
# No CMake, VS, Python, download, installer or export templates on user's PC.
[CmdletBinding()]
param([string]$GodotExe="",[switch]$SkipHeadlessSmoke)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$station=Join-Path $project 'native\galaxy_desktop_station_host.exe'
$pilot=Join-Path $project 'native\galaxy_desktop_player_host.exe'
$logDir=Join-Path $project 'playtest-logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$pass=$false
try {
  foreach($exe in @($station,$pilot)){
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){
      throw "PREBUILT NATIVE HOST MISSING: $exe. This is the source checkout, not the compiled Windows artifact. Download the PLAY-STATION-02 Windows artifact from PR #119 GitHub Actions. No CMake installation required."
    }
  }
  Write-Host 'Precompiled native hosts found. No C++ compiler needed.'
  if(-not [string]::IsNullOrWhiteSpace($GodotExe)){
    $GodotExe=[IO.Path]::GetFullPath($GodotExe)
  } elseif(-not [string]::IsNullOrWhiteSpace($env:SINGLE_GALAXY_GODOT_EXE)){
    $GodotExe=[IO.Path]::GetFullPath($env:SINGLE_GALAXY_GODOT_EXE)
  } else {
    foreach($name in @('Godot.exe','godot.exe','Godot_v4.7.2-stable_win64.exe')){
      $candidate=Get-Command $name -ErrorAction SilentlyContinue
      if($candidate){$GodotExe=$candidate.Source;break}
    }
    if([string]::IsNullOrWhiteSpace($GodotExe)){
      Add-Type -AssemblyName System.Windows.Forms
      $dialog=New-Object System.Windows.Forms.OpenFileDialog
      $dialog.Title='Select your EXISTING Godot 4.7.2 editor'
      $dialog.Filter='Godot editor (*.exe)|*.exe'
      if($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK){$GodotExe=$dialog.FileName}
    }
  }
  if(-not (Test-Path -LiteralPath $GodotExe -PathType Leaf)){
    throw 'Existing Godot editor not found; select the Godot 4.7.2 executable.'
  }
  $version=(& $GodotExe --version 2>&1 | Out-String).Trim()
  if($LASTEXITCODE -ne 0 -or $version -notmatch '^4\.7\.2\.stable'){
    throw "Expected already-installed Godot 4.7.2.stable; got $version"
  }
  Write-Host "Using installed Godot $version"
  if(-not $SkipHeadlessSmoke){
    $suites=@(
      @('--headless','--path',('"' + $project + '"'),'--editor','--quit'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/godot_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/station_visual_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_player_protocol_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_station_protocol_smoke.gd')
    )
    $index=0
    foreach($suite in $suites){
      $index++
      $stdout=Join-Path $logDir "godot-$index-stdout.log"
      $stderr=Join-Path $logDir "godot-$index-stderr.log"
      $p=Start-Process -FilePath $GodotExe -ArgumentList $suite -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
      $errorText=Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue | Out-String
      if($p.ExitCode -ne 0 -or $errorText -match '(?m)^(SCRIPT ERROR|ERROR:|Parser Error:)'){
        throw "Installed Godot parser or scene smoke #$index failed. See $stdout and $stderr. Game was not launched."
      }
    }
    Write-Host 'Actual installed-Godot parser/scene checks passed.'
  }
  Write-Host 'Game controls: H station, WASD walk, E board, P cockpit, E sit, U undock.'
  $game=Start-Process -FilePath $GodotExe -ArgumentList @('--path',('"' + $project + '"')) -Wait -PassThru
  Write-Host "Godot exited code $($game.ExitCode). This is not yet manual 3D-play acceptance."
  $pass=$game.ExitCode -eq 0
} catch {
  Write-Host "PLAYTEST BLOCKED: $_" -ForegroundColor Red
} finally {
  $manifest=[ordered]@{
    task='PLAY-STATION-02';godot=$GodotExe
    native_station_hash=if(Test-Path $station){(Get-FileHash $station -Algorithm SHA256).Hash}else{''}
    native_cockpit_hash=if(Test-Path $pilot){(Get-FileHash $pilot -Algorithm SHA256).Hash}else{''}
    result=if($pass){'WINDOW-CLOSED-NEEDS-MANUAL-OBSERVATION'}else{'BLOCKED'}
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logDir 'result.json') -Encoding UTF8
  $zip=Join-Path $project 'SingleGalaxy_Station_Playtest_Evidence.zip'
  if(Test-Path $zip){Remove-Item $zip -Force}
  Compress-Archive -Path (Join-Path $logDir '*') -DestinationPath $zip -Force
  Write-Host "Evidence: $zip"
  if(-not $pass){exit 1}
}
