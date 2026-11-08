[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $root 'native\galaxy_desktop_player_host.exe'
$report=Join-Path $root 'build\native-fps-acceptance'
New-Item -ItemType Directory -Path $report -Force | Out-Null
$save=Join-Path $report 'test-authoritative.sqlite'
@($save, "$save-wal", "$save-shm") | ForEach-Object {if(Test-Path -LiteralPath $_){Remove-Item -LiteralPath $_ -Force}}
$events = New-Object 'System.Collections.Generic.List[string]'
function Expect([bool]$ok,[string]$message){if(-not $ok){throw $message}}
function Start-Native {
  $cfg=New-Object System.Diagnostics.ProcessStartInfo
  $cfg.FileName=$exe
  $cfg.Arguments='"'+$save+'"'
  $cfg.UseShellExecute=$false
  $cfg.CreateNoWindow=$true
  $cfg.RedirectStandardInput=$true
  $cfg.RedirectStandardOutput=$true
  $cfg.RedirectStandardError=$true
  return [System.Diagnostics.Process]::Start($cfg)
}
function Read-Frame($p,[string]$stage){
  # Execute and stdout are owned by the child. On a hanging process, the
  # parent should be stopped by the user and the case remains unaccepted.
  $line=$p.StandardOutput.ReadLine()
  Expect (-not [string]::IsNullOrWhiteSpace($line)) "$stage empty native reply"
  $events.Add("$stage $line")
  return @($line.Split('|'))
}
try {
  Expect (Test-Path -LiteralPath $exe) "Native Windows PE missing: $exe"
  $p=Start-Native
  $first=Read-Frame $p 'initial'
  Expect (($first.Count -eq 13) -and ($first[0] -eq 'POSE') -and ($first[1] -eq '1') -and ($first[4] -eq '0')) 'Invalid initial native pose'
  $epoch=$first[2]
  Start-Sleep -Milliseconds 75
  $p.StandardInput.WriteLine("MOVE|$epoch|1|0|80|0")
  $p.StandardInput.Flush()
  $ack=Read-Frame $p 'move'
  Expect (($ack.Count -eq 11) -and ($ack[0] -eq 'ACK') -and ($ack[4] -eq '1') -and ($ack[5] -eq '80')) 'First move not committed'
  Start-Sleep -Milliseconds 75
  $p.StandardInput.WriteLine("SEAT|$epoch|2|0|1")
  $p.StandardInput.Flush()
  $sit=Read-Frame $p 'seat'
  Expect (($sit.Count -eq 13) -and ($sit[0] -eq 'ACK') -and ($sit[4] -eq '1') -and ($sit[11] -eq '1') -and ($sit[12] -eq '1')) 'Native cockpit sit did not commit'
  Start-Sleep -Milliseconds 75
  $p.StandardInput.WriteLine("SEAT|$epoch|3|1|0")
  $p.StandardInput.Flush()
  $stand=Read-Frame $p 'stand'
  Expect (($stand.Count -eq 13) -and ($stand[0] -eq 'ACK') -and ($stand[11] -eq '0') -and ($stand[12] -eq '2')) 'Native cockpit stand did not commit'
  $p.StandardInput.WriteLine('QUIT')
  $p.StandardInput.Flush()
  Expect ($p.WaitForExit(5000) -and $p.ExitCode -eq 0) 'First native host did not exit cleanly'
  $errors=$p.StandardError.ReadToEnd()
  Expect ([string]::IsNullOrWhiteSpace($errors)) "First native stderr: $errors"
  $p.Dispose()
  $q=Start-Native
  $reopened=Read-Frame $q 'restarted'
  Expect (($reopened.Count -eq 11) -and ($reopened[0] -eq 'POSE') -and ($reopened[4] -eq '1') -and ($reopened[5] -eq '80') -and ($reopened[2] -ne $epoch) -and ($reopened[11] -eq '0') -and ($reopened[12] -eq '2')) 'Restart did not recover committed pose with a fresh session'
  $q.StandardInput.WriteLine('QUIT')
  $q.StandardInput.Flush()
  Expect ($q.WaitForExit(5000) -and $q.ExitCode -eq 0) 'Second native host did not exit cleanly'
  $q.Dispose()
  $events.Add('RESULT PASS: Windows x64 native movement, cockpit transitions and SQLite restart')
  $events | Set-Content -LiteralPath (Join-Path $report 'summary.txt')
  Write-Host ($events[-1]) -ForegroundColor Green
} catch {
  $events.Add('RESULT FAIL: '+[string]$_)
  $events | Set-Content -LiteralPath (Join-Path $report 'summary.txt')
  Write-Host ($events[-1]) -ForegroundColor Red
  throw
} finally {
  foreach($obj in @('p','q')) {
    $v=Get-Variable -Name $obj -Scope Local -ErrorAction SilentlyContinue
    if($v -and $v.Value) {
      try {if(-not $v.Value.HasExited){$v.Value.Kill()};$v.Value.Dispose()} catch {}
    }
  }
  $zip=Join-Path $root 'SingleGalaxy_NativeFPS_Windows_Evidence.zip'
  if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}
  Compress-Archive -Path (Join-Path $report '*') -DestinationPath $zip -Force
  Write-Host "Evidence saved to: $zip"
}
