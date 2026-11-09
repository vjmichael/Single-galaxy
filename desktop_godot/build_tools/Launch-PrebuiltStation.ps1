# Self-contained native station/cockpit with bundled pinned Godot 4.7.2 runtime.
# No compiler, installed Godot editor, Python, download or export templates.
# -GodotExe is a deliberate developer override, not a default fallback.
[CmdletBinding()]
param([string]$GodotExe="",[switch]$SkipHeadlessSmoke)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$station=Join-Path $project 'native\galaxy_desktop_station_host.exe'
$pilot=Join-Path $project 'native\galaxy_desktop_player_host.exe'
$supervisor=Join-Path $project 'native\galaxy_server_dx_supervisor.exe'
$shaManifest=Join-Path $project 'native\SGDX_BUILD_SHA.txt'
$logDir=Join-Path $project 'playtest-logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$pass=$false
try {
  foreach($exe in @($station,$pilot,$supervisor)){
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){
      throw "SGDX-03 package incomplete: $exe. Use verified PR #125 Windows build; unsupervised native execution forbidden."
    }
  }
  if(-not(Test-Path -LiteralPath $shaManifest -PathType Leaf)){
    throw "SGDX build SHA manifest missing; no unverified process."
  }
  $buildSha=(Get-Content -LiteralPath $shaManifest -Raw).Trim()
  if($buildSha -notmatch '^[0-9a-fA-F]{40}$'){
    throw "SGDX build SHA malformed; expected 40 hexadecimal digits."
  }
  Write-Host "Supervised native hosts found. Build: $buildSha. No compiler needed."
  if($env:SGDX_WINDOWS_MINIDUMP -eq '1'){
    Write-Warning 'Opt-in crash minidumps may capture private process memory.'
  }
  if([string]::IsNullOrWhiteSpace($GodotExe)){
    # The player receives the entire engine with the exact native ZIP.
    # No PATH/editor picker, web download, installer or silent substitute.
    $GodotExe=Join-Path $project 'runtime\Godot_v4.7.2-stable_win64.exe'
    $engineManifest=Join-Path $project 'runtime\GODOT_RUNTIME_SHA256.txt'
    $engineLicense=Join-Path $project 'runtime\LICENSE.txt'
    foreach($file in @($GodotExe,$engineManifest,$engineLicense)){
      if(-not(Test-Path -LiteralPath $file -PathType Leaf)){
        throw "Precompiled ZIP missing verified bundled Godot runtime/licensing: $file"
      }
    }
    $expected=(Get-Content -LiteralPath $engineManifest -Raw).Trim().ToLowerInvariant()
    if($expected -notmatch '^[0-9a-f]{64}$'){
      throw "Bundled Godot SHA-256 manifest malformed"
    }
    $actual=(Get-FileHash -LiteralPath $GodotExe -Algorithm SHA256).Hash.ToLowerInvariant()
    if($actual -cne $expected){
      throw "Bundled Godot SHA-256 does not match release-derived package manifest"
    }
    Write-Host "Verified bundled Godot engine SHA-256 $actual"
  } else {
    # Explicit diagnostic override only; never silently fallback on a
    # user's installed editor or substitute a different major engine.
    $GodotExe=[IO.Path]::GetFullPath($GodotExe)
    Write-Warning "Developer -GodotExe override selected; not self-contained package acceptance."
  }
  if(-not(Test-Path -LiteralPath $GodotExe -PathType Leaf)){
    throw "Bundled Godot 4.7.2 engine missing: $GodotExe"
  }
  $version=(& $GodotExe --version 2>&1 | Out-String).Trim()
  if($LASTEXITCODE -ne 0 -or $version -notmatch '^4\.7\.2\.stable'){
    throw "Expected already-installed Godot 4.7.2.stable; got $version"
  }
  Write-Host "Using Windows Godot $version from $GodotExe"
  if(-not $SkipHeadlessSmoke){
    $suites=@(
      @('--headless','--path',('"' + $project + '"'),'--editor','--quit'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/godot_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/station_visual_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_player_protocol_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_station_protocol_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_station_refinery_protocol_smoke.gd')
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
    Write-Host 'Bundled Godot 4.7.2 parser/scene checks passed.'
  }
  # Screenshot key is opt-in; captures ONLY the game viewport plus read-only
  # verified original C++ frames. No Windows desktop/screen recording.
  $env:SG_FP3D_OPERATOR_CAPTURE_DIR=$logDir
  Write-Host 'Game controls: H station, WASD walk, E board/exit, P cockpit, E sit, V exterior, G dock, F11 capture game viewport.'
  Write-Host 'For physical playtest evidence, press F11 at native station, docked cockpit, exterior flight, asteroid contact and after restart.'
  $game=Start-Process -FilePath $GodotExe -ArgumentList @('--path',('"' + $project + '"')) -Wait -PassThru
  Write-Host "Godot exited code $($game.ExitCode). Window close is NOT proof of physical gameplay acceptance."
  $pass=$game.ExitCode -eq 0
} catch {
  Write-Host "PLAYTEST BLOCKED: $_" -ForegroundColor Red
} finally {
  Remove-Item Env:SG_FP3D_OPERATOR_CAPTURE_DIR -ErrorAction SilentlyContinue
  $nativeReceipts=Join-Path $logDir 'operator-viewport-receipts.jsonl'
  $captureCount=0
  $verifiedCaptureCount=0
  if(Test-Path -LiteralPath $nativeReceipts -PathType Leaf){
    foreach($line in (Get-Content -LiteralPath $nativeReceipts -ErrorAction SilentlyContinue)){
      try {
        $record=$line | ConvertFrom-Json -ErrorAction Stop
        $picture=Join-Path $logDir $record.image
        if($record.schema -eq 'SG-FP3D-OPERATOR-01' -and
           (Test-Path -LiteralPath $picture -PathType Leaf) -and
           (Get-FileHash -LiteralPath $picture -Algorithm SHA256).Hash -ieq $record.image_sha256){
          $captureCount++
          if($record.native_flight_joined -or $record.station_person.state -eq 4){$verifiedCaptureCount++}
        }
      } catch { Write-Warning 'Invalid operator screenshot receipt; not counted as evidence.' }
    }
  }
  $manifest=[ordered]@{
    task='FP3D-WIN02-BUNDLED-RUNTIME';godot=$GodotExe
    engine_mode=if($GodotExe -eq (Join-Path $project 'runtime\Godot_v4.7.2-stable_win64.exe')){'BUNDLED-VERIFIED'}else{'EXPLICIT-DEVELOPER-OVERRIDE'}
    engine_sha256=if(Test-Path -LiteralPath $GodotExe -PathType Leaf){(Get-FileHash -LiteralPath $GodotExe -Algorithm SHA256).Hash.ToLowerInvariant()}else{''}
    native_station_hash=if(Test-Path $station){(Get-FileHash $station -Algorithm SHA256).Hash}else{''}
    native_cockpit_hash=if(Test-Path $pilot){(Get-FileHash $pilot -Algorithm SHA256).Hash}else{''}
    sgdx_supervisor_hash=if(Test-Path $supervisor){(Get-FileHash $supervisor -Algorithm SHA256).Hash}else{''}
    build_sha=if(Test-Path $shaManifest){(Get-Content -LiteralPath $shaManifest -Raw).Trim()}else{''}
    verified_game_viewport_screenshots=$captureCount
    verified_native_frame_screenshots=$verifiedCaptureCount
    operator_visual_acceptance='NOT ATTESTED: review actual PNGs and original SQLite receipts'
    result=if($pass){'WINDOW-CLOSED-NEEDS-MANUAL-OBSERVATION'}else{'BLOCKED'}
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logDir 'result.json') -Encoding UTF8
  $zip=Join-Path $project 'SingleGalaxy_Station_Playtest_Evidence.zip'
  if(Test-Path $zip){Remove-Item $zip -Force}
  Compress-Archive -Path (Join-Path $logDir '*') -DestinationPath $zip -Force
  Write-Host "Evidence: $zip"
  if(-not $pass){exit 1}
}
