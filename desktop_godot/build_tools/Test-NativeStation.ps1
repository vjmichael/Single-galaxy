[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stationExe=Join-Path $root 'native\galaxy_desktop_station_host.exe'
$pilotExe=Join-Path $root 'native\galaxy_desktop_player_host.exe'
$report=Join-Path $root 'build\native-station-acceptance'
New-Item -ItemType Directory -Path $report -Force | Out-Null
$save=Join-Path $report 'station-test-world.sqlite'
@($save,"$save-wal","$save-shm") | ForEach-Object {if(Test-Path -LiteralPath $_){Remove-Item -LiteralPath $_ -Force}}
$events=New-Object 'System.Collections.Generic.List[string]'
function Expect([bool]$condition,[string]$message){if(-not $condition){throw $message}}
function Launch([string]$exe,[string]$arguments){
  $cfg=New-Object System.Diagnostics.ProcessStartInfo
  $cfg.FileName=$exe;$cfg.Arguments=$arguments
  $cfg.UseShellExecute=$false;$cfg.CreateNoWindow=$true
  $cfg.RedirectStandardInput=$true;$cfg.RedirectStandardOutput=$true;$cfg.RedirectStandardError=$true
  return [System.Diagnostics.Process]::Start($cfg)
}
function Frame($p,[string]$stage){
  $line=$p.StandardOutput.ReadLine()
  Expect (-not [string]::IsNullOrWhiteSpace($line)) "$stage empty response"
  $events.Add("$stage $line")
  return @($line.Split('|'))
}
function Request($p,[string]$command,[string]$stage){
  Start-Sleep -Milliseconds 80
  $p.StandardInput.WriteLine($command);$p.StandardInput.Flush()
  return @(Frame $p $stage)
}
function End-Native($p){
  $p.StandardInput.WriteLine('QUIT');$p.StandardInput.Flush()
  Expect ($p.WaitForExit(5000) -and $p.ExitCode -eq 0) 'Native child failed to exit cleanly'
  $errors=$p.StandardError.ReadToEnd()
  Expect ([string]::IsNullOrWhiteSpace($errors)) "Native stderr: $errors"
  $p.Dispose()
}
try{
  Expect (Test-Path -LiteralPath $stationExe) "Missing station host $stationExe"
  Expect (Test-Path -LiteralPath $pilotExe) "Missing cockpit host $pilotExe"
  $stationProc=Launch $stationExe ('"'+$save+'" --explicit-test-station')
  $first=Frame $stationProc 'station-spawn'
  Expect (($first.Count -eq 13) -and ($first[0] -eq 'PERSON') -and ($first[1] -eq '2') -and ($first[6] -eq '4') -and ($first[9] -eq '1600') -and ($first[8] -eq '0')) 'Not initially inside station'
  $epoch=$first[2];$generation=[uint64]$first[5];$ship=[uint64]$first[11]
  Expect ($ship -gt 0) 'No actual owned docked ship available'
  $denied=Request $stationProc "BOARD|$epoch|1|$generation|$ship" 'boarding-too-far'
  Expect (($denied[0] -eq 'DENY') -and ($denied[3] -eq 'station-custody-or-distance')) 'Distant boarding incorrectly allowed'
  for($req=2;$req -le 11;$req++){
    $walk=Request $stationProc "WALK|$epoch|$req|$generation|-100|0" "walk-$req"
    Expect (($walk.Count -eq 13) -and ($walk[0] -eq 'PERSON') -and ($walk[6] -eq '4')) 'Original station walk failed'
    $generation=[uint64]$walk[5]
  }
  Expect ($walk[9] -eq '600') 'Player failed to reach native hatch threshold'
  $board=Request $stationProc "BOARD|$epoch|12|$generation|$ship" 'board-owned-ship'
  Expect (($board.Count -eq 13) -and ($board[6] -eq '0') -and ([uint64]$board[8] -eq $ship)) 'Original player ship custody not committed'
  End-Native $stationProc;$stationProc=$null
  $pilotProc=Launch $pilotExe ('"'+$save+'"')
  $pose=Frame $pilotProc 'cockpit-recovered-boarding'
  $telemetry=Frame $pilotProc 'original-ship-state'
  Expect (($pose.Count -eq 13) -and ([uint64]$pose[10] -eq $ship) -and ($telemetry.Count -eq 17) -and ($telemetry[0] -eq 'SHIP')) 'Original cockpit cannot recover boarding save'
  $sit=Request $pilotProc ("SEAT|"+$pose[2]+"|1|0|1") 'sit'
  Expect (($sit[0] -eq 'ACK') -and ($sit[11] -eq '1')) 'Native cockpit sit refused'
  $undock=Request $pilotProc ("FLIGHT|"+$pose[2]+"|2|1|UND") 'real-undock'
  Expect (($undock.Count -eq 17) -and ($undock[0] -eq 'SHIP') -and ($undock[5] -eq '0') -and ($undock[16] -eq '0')) 'Real original ship did not undock'
  End-Native $pilotProc;$pilotProc=$null
  $afterProc=Launch $stationExe ('"'+$save+'" --explicit-test-station')
  $after=Frame $afterProc 'station-restart'
  Expect (($after[6] -eq '0') -and ([uint64]$after[8] -eq $ship) -and ($after[7] -eq '0')) 'Boarding lost across restart'
  $wrong=Request $afterProc ("EXIT|"+$after[2]+"|1|"+$after[5]+"|"+$first[7]) 'deny-exit-in-space'
  Expect (($wrong[0] -eq 'DENY') -and ($wrong[3] -eq 'station-custody-or-distance')) 'Disembark from space incorrectly allowed'
  End-Native $afterProc;$afterProc=$null
  $events.Add('RESULT PASS: real Windows station spawn, ten walks, ownership boarding, cockpit sit, undock and restart')
  $events | Set-Content -LiteralPath (Join-Path $report 'summary.txt')
  Write-Host $events[-1] -ForegroundColor Green
}catch{
  $events.Add('RESULT FAIL: '+[string]$_)
  $events | Set-Content -LiteralPath (Join-Path $report 'summary.txt')
  Write-Host $events[-1] -ForegroundColor Red
  throw
}finally{
  foreach($name in @('stationProc','pilotProc','afterProc')){
    $var=Get-Variable -Name $name -ErrorAction SilentlyContinue
    if($var -and $var.Value){try{if(-not $var.Value.HasExited){$var.Value.Kill()};$var.Value.Dispose()}catch{}}
  }
  $zip=Join-Path $root 'SingleGalaxy_NativeStation_Windows_Evidence.zip'
  if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}
  Compress-Archive -Path (Join-Path $report '*') -DestinationPath $zip -Force
}
