# PLAY-STATION-02 — current C++ source + Godot 4.7.2 Windows visual acceptance.
# Uses the owner's existing editor. Does not download an engine or export templates.
[CmdletBinding()]
param([string]$GodotExe="",[string]$Generator="Visual Studio 17 2022",[switch]$HeadlessOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repo=[IO.Path]::GetFullPath((Join-Path $root '..'))
$out=Join-Path $root 'build\station-visual-acceptance'
$nativeBuild=Join-Path $out 'native-source-build'
$logs=Join-Path $out 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$checks=New-Object 'System.Collections.Generic.List[object]'
$status='FAILED'
$started=[DateTime]::UtcNow.ToString('o')
function Gate([string]$name,[string]$status,[string]$details){
  $checks.Add([ordered]@{name=$name;status=$status;detail=$details})
  Write-Host "[$status] $name : $details"
}
function Run([string]$label,[string]$exe,[string[]]$arguments){
  $stdout=Join-Path $logs "$label.stdout.log"
  $stderr=Join-Path $logs "$label.stderr.log"
  $p=Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  if($p.ExitCode -ne 0){throw "$label exited $($p.ExitCode); see $stdout and $stderr"}
  $errorText=(Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue | Out-String)
  if($label -like 'godot-*' -and $errorText -match '(?m)^(SCRIPT ERROR|ERROR:|Parser Error:)'){
    throw "$label had Godot errors despite a zero process exit code; see $stderr"
  }
  Gate $label 'PASS' "exit 0; $stdout"
}
try {
  if(-not [string]::IsNullOrWhiteSpace($GodotExe)){
    $GodotExe=[IO.Path]::GetFullPath($GodotExe)
  } elseif(-not [string]::IsNullOrWhiteSpace($env:SINGLE_GALAXY_GODOT_EXE)){
    $GodotExe=[IO.Path]::GetFullPath($env:SINGLE_GALAXY_GODOT_EXE)
  } else {
    foreach($exeName in @('Godot.exe','godot.exe','Godot_v4.7.2-stable_win64.exe')){
      $found=Get-Command $exeName -ErrorAction SilentlyContinue
      if($found){$GodotExe=$found.Source;break}
    }
    if([string]::IsNullOrWhiteSpace($GodotExe)){
      Add-Type -AssemblyName System.Windows.Forms
      $picker=New-Object System.Windows.Forms.OpenFileDialog
      $picker.Title='Select your already-installed Godot 4.7.2 editor executable'
      $picker.Filter='Godot editor (*.exe)|*.exe'
      if($picker.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK){$GodotExe=$picker.FileName}
    }
  }
  if(-not (Test-Path -LiteralPath $GodotExe -PathType Leaf)){
    throw 'An existing Godot 4.7.2 executable is required. No editor is downloaded.'
  }
  $version=(& $GodotExe --version 2>&1 | Out-String).Trim()
  if($LASTEXITCODE -ne 0 -or $version -notmatch '^4\.7\.2\.stable'){
    throw "Existing editor is not Godot 4.7.2.stable: $version"
  }
  Gate 'installed-editor' 'PASS' "$version at $GodotExe; version checked, publisher digest not certified"
  if(-not (Test-Path -LiteralPath (Join-Path $repo 'CMakeLists.txt'))){
    throw 'Original C++ source checkout missing alongside desktop_godot'
  }
  $cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
  $configArgs=@('-S',('"'+$repo+'"'),'-B',('"'+$nativeBuild+'"'),'-G',('"'+$Generator+'"'),'-DBUILD_TESTING=ON')
  if($Generator -like 'Visual Studio*'){$configArgs+=@('-A','x64')}
  else {$configArgs+='-DCMAKE_BUILD_TYPE=Release'}
  Run 'native-configure' $cmake $configArgs
  Run 'native-build' $cmake @('--build',('"'+$nativeBuild+'"'),'--config','Release','--target',
      'galaxy_desktop_station_host','galaxy_desktop_player_host','galaxy_play_station_boarding_tests','--parallel','4')
  $station=Join-Path $nativeBuild 'Release\galaxy_desktop_station_host.exe'
  $pilot=Join-Path $nativeBuild 'Release\galaxy_desktop_player_host.exe'
  if(-not (Test-Path -LiteralPath $station)){
    $station=Join-Path $nativeBuild 'galaxy_desktop_station_host.exe'
    $pilot=Join-Path $nativeBuild 'galaxy_desktop_player_host.exe'
  }
  if(-not (Test-Path -LiteralPath $station) -or -not (Test-Path -LiteralPath $pilot)){
    throw 'Fresh source-compiled native Windows station/cockpit hosts not found'
  }
  $nativeDir=Join-Path $root 'native'
  New-Item -ItemType Directory -Force -Path $nativeDir | Out-Null
  Copy-Item -LiteralPath $station -Destination (Join-Path $nativeDir 'galaxy_desktop_station_host.exe') -Force
  Copy-Item -LiteralPath $pilot -Destination (Join-Path $nativeDir 'galaxy_desktop_player_host.exe') -Force
  Gate 'rebuilt-native-executables' 'PASS' "station SHA256 $((Get-FileHash $station -Algorithm SHA256).Hash); cockpit SHA256 $((Get-FileHash $pilot -Algorithm SHA256).Hash)"
  # Executes authentic station-spawn, walk, BOARD, handoff, UND and restart,
  # plus competing-writer and forced-crash tests against two real Windows PEs.
  $powershell=Join-Path $PSHOME 'powershell.exe'
  if(-not (Test-Path -LiteralPath $powershell)){$powershell='powershell.exe'}
  $nativeSmoke=Join-Path $PSScriptRoot 'Test-NativeStation.ps1'
  Run 'native-original-station-pipe' $powershell @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$nativeSmoke+'"'))
  Run 'godot-project-import' $GodotExe @('--headless','--path',('"'+$root+'"'),'--editor','--quit')
  foreach($test in @('godot_smoke.gd','station_visual_smoke.gd',
                     'native_station_protocol_smoke.gd','native_player_protocol_smoke.gd',
                     'native_mining_04b_smoke.gd')){
    Run ('godot-'+$test) $GodotExe @('--headless','--path',('"'+$root+'"'),'--script',('res://tests/'+$test))
  }
  if(-not $HeadlessOnly){
    Gate 'human-playtest' 'NEEDS-OBSERVATION' 'H native station, WASD approach hatch, E board, P cockpit, E sit, U undock, A/D turn, arrows thrust, B brake; close game to collect logs'
    $game=Start-Process -FilePath $GodotExe -ArgumentList @('--path',('"'+$root+'"')) -PassThru -Wait
    Gate 'godot-game-window' 'INFO' "game exited $($game.ExitCode); exit alone does NOT certify actual visual gameplay"
  }
  $status='NATIVE-AND-GODOT-HEADLESS-PASS-MANUAL-OPEN'
} catch {
  Gate 'acceptance-failure' 'FAIL' ([string]$_)
} finally {
  # Godot's user:// path lives beneath the installed editor's per-project
  # app_userdata directory. Copy only a file updated during THIS test run.
  $userData=Join-Path $env:APPDATA 'Godot\app_userdata'
  if(Test-Path -LiteralPath $userData){
    $journal=Get-ChildItem -LiteralPath $userData -Filter 'sg_station_visual_acceptance.jsonl' -File -Recurse -ErrorAction SilentlyContinue |
      Where-Object { $_.LastWriteTimeUtc -ge [DateTime]::Parse($started).ToUniversalTime() } |
      Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if($journal){
      Copy-Item -LiteralPath $journal.FullName -Destination (Join-Path $logs 'godot-observed-native-frames.jsonl') -Force
      $rows=@()
      foreach($line in (Get-Content -LiteralPath $journal.FullName)){
        try {
          $record=ConvertFrom-Json -InputObject $line
          if($record.wall_unix -ge ([DateTimeOffset]::Parse($started)).ToUnixTimeSeconds()){
            $rows+=,$record
          }
        } catch { }
      }
      $stationSeen=@($rows | Where-Object {$_.event -eq 'PERSON' -and $_.frame.state -eq 4}).Count -gt 0
      $boardSeen=@($rows | Where-Object {$_.event -eq 'PERSON' -and $_.frame.state -eq 0}).Count -gt 0
      $cockpitSeen=@($rows | Where-Object {$_.event -eq 'POSE'}).Count -gt 0
      $undockSeen=@($rows | Where-Object {$_.event -eq 'SHIP' -and $_.frame.dock -eq 0}).Count -gt 0
      Gate 'observed-client-native-sequence' 'INFO' "station=$stationSeen boarded=$boardSeen cockpit=$cockpitSeen undocked=$undockSeen; client observation, not 3D acceptance"
    }
  }
  $manifest=[ordered]@{test='PLAY-STATION-02';source_branch='sg/play-station02-native-spawn-board-20261108';started_utc=$started;completed_utc=[DateTime]::UtcNow.ToString('o');status=$status;gates=$checks}
  $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'acceptance.json') -Encoding UTF8
  $zip=Join-Path $root 'SingleGalaxy_Station_Visual_Windows_Evidence.zip'
  if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}
  Compress-Archive -Path (Join-Path $out 'acceptance.json'),(Join-Path $out 'logs') -DestinationPath $zip -Force
  Write-Host "Evidence saved: $zip"
  if($status -ne 'NATIVE-AND-GODOT-HEADLESS-PASS-MANUAL-OPEN'){exit 1}
}
