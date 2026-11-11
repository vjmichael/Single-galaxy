# Self-contained native station/cockpit with bundled pinned Godot 4.7.2 runtime.
# No compiler, installed Godot editor, Python, download or export templates.
# -GodotExe is a deliberate developer override, not a default fallback.
[CmdletBinding()]
param([string]$GodotExe="",[switch]$SkipHeadlessSmoke,[switch]$VerifyOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"

# Mixed pwsh 7 -> Windows PowerShell 5.1 test invocation can inherit a
# PSModulePath without the OS's ordinary built-in modules. Restore the
# Windows-installed module directory; no new package or network access.
# Built-in ConvertTo-Json and Compress-Archive remain required to create
# original-source, fail-closed local evidence.
if($PSVersionTable.PSEdition -eq 'Desktop'){
  $winModules=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\Modules'
  if(Test-Path -LiteralPath $winModules -PathType Container){
    $paths=@($env:PSModulePath -split ';')
    if($paths -notcontains $winModules){
      $env:PSModulePath=$winModules+';'+$env:PSModulePath
    }
  }
}

# The single-click player path must work under Windows PowerShell 5.1 even
# when Microsoft.PowerShell.Utility/Get-FileHash is missing. The .NET SHA-256
# engine is built into Windows and is not a new dependency or authority.
function Get-LocalSha256([string]$FullPath){
  if(-not(Test-Path -LiteralPath $FullPath -PathType Leaf)){
    throw "Required bundled file absent for SHA-256 identity check: $FullPath"
  }
  $stream=[System.IO.File]::OpenRead($FullPath)
  try {
    $hasher=[System.Security.Cryptography.SHA256]::Create()
    try {
      $hash=$hasher.ComputeHash($stream)
      return ([System.BitConverter]::ToString($hash)).Replace('-','').ToLowerInvariant()
    } finally {
      if($hasher -ne $null){$hasher.Dispose()}
    }
  } finally {
    if($stream -ne $null){$stream.Dispose()}
  }
}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$station=Join-Path $project 'native\galaxy_desktop_station_host.exe'
$pilot=Join-Path $project 'native\galaxy_desktop_player_host.exe'
$supervisor=Join-Path $project 'native\galaxy_server_dx_supervisor.exe'
$shaManifest=Join-Path $project 'native\SGDX_BUILD_SHA.txt'
$logDir=Join-Path $project 'playtest-logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
# Never confuse prior F11 captures with this launch's physical evidence.
$operatorSessionName='operator-'+[DateTimeOffset]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$operatorLogDir=Join-Path $logDir $operatorSessionName
$pass=$false
if($VerifyOnly -and $SkipHeadlessSmoke){
  throw 'Package verification cannot skip bundled-engine headless smoke tests.'
}
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
  $CliGodotExe=$GodotExe
  if([string]::IsNullOrWhiteSpace($GodotExe)){
    # The player receives the entire engine with the exact native ZIP.
    # No PATH/editor picker, web download, installer or silent substitute.
    $GodotExe=Join-Path $project 'runtime\Godot_v4.7.2-stable_win64.exe'
    $engineManifest=Join-Path $project 'runtime\GODOT_RUNTIME_SHA256.txt'
    $CliGodotExe=Join-Path $project 'runtime\Godot_v4.7.2-stable_win64_console.exe'
    $cliManifest=Join-Path $project 'runtime\GODOT_CONSOLE_SHA256.txt'
    $engineLicense=Join-Path $project 'runtime\LICENSE.txt'
    foreach($file in @($GodotExe,$engineManifest,$CliGodotExe,$cliManifest,$engineLicense)){
      if(-not(Test-Path -LiteralPath $file -PathType Leaf)){
        throw "Precompiled ZIP missing verified bundled Godot runtime/licensing: $file"
      }
    }
    $expected=(Get-Content -LiteralPath $engineManifest -Raw).Trim().ToLowerInvariant()
    if($expected -notmatch '^[0-9a-f]{64}$'){
      throw "Bundled Godot SHA-256 manifest malformed"
    }
    $actual=(Get-LocalSha256 $GodotExe)
    if($actual -cne $expected){
      throw "Bundled Godot SHA-256 does not match release-derived package manifest"
    }
    # Official Windows Godot console wrapper owns the real child process and
    # propagates its exit code under Windows PowerShell 5.1.
    $cliExpected=(Get-Content -LiteralPath $cliManifest -Raw).Trim().ToLowerInvariant()
    if($cliExpected -notmatch '^[0-9a-f]{64}$'){
      throw "Bundled Godot console wrapper SHA-256 manifest malformed"
    }
    $cliActual=Get-LocalSha256 $CliGodotExe
    if($cliActual -cne $cliExpected){
      throw "Bundled Godot console wrapper SHA-256 does not match release-derived package manifest"
    }
    Write-Host "Verified bundled Godot engine SHA-256 $actual; CLI wrapper SHA-256 $cliActual"
  } else {
    # Explicit diagnostic override only; never silently fallback on a
    # user's installed editor or substitute a different major engine.
    $GodotExe=[IO.Path]::GetFullPath($GodotExe)
    $CliGodotExe=$GodotExe
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
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/colony_dock_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/interaction_prompt_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/boarding_action_pipe_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_player_protocol_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_station_protocol_smoke.gd'),
      @('--headless','--path',('"' + $project + '"'),'--script','res://tests/native_station_refinery_protocol_smoke.gd')
    )
    $index=0
    foreach($suite in $suites){
      $index++
      $stdout=Join-Path $logDir "godot-$index-stdout.log"
      $stderr=Join-Path $logDir "godot-$index-stderr.log"
      # The exact extracted ZIP's engine must execute each real parser/scene
      # check under a bounded lifetime. Godot may return exit 0 despite
      # reporting GDScript parser errors, so inspect BOTH real output streams.
      # WinPS5 Start-Process returns a Process wrapper that we observed
      # report HasExited=True yet expose a NULL ExitCode, even with an
      # explicit WaitForExit. Own a FRESH System.Diagnostics.Process
      # directly, never infer success from stdout or an absent status.
      $info=New-Object System.Diagnostics.ProcessStartInfo
      $info.FileName=$CliGodotExe
      $info.Arguments=[string]::Join(' ',$suite)
      $info.WorkingDirectory=[IO.Path]::GetDirectoryName($CliGodotExe)
      $info.UseShellExecute=$false
      $info.CreateNoWindow=$true
      $info.RedirectStandardOutput=$true
      $info.RedirectStandardError=$true
      $child=New-Object System.Diagnostics.Process
      $child.StartInfo=$info
      try {
        if(-not $child.Start()){throw "Godot CLI process did not start"}
        # Drain both pipes concurrently to prevent Windows stdout/stderr
        # deadlocks before waiting for bounded process completion.
        $outTask=$child.StandardOutput.ReadToEndAsync()
        $errTask=$child.StandardError.ReadToEndAsync()
        if(-not [bool]$child.WaitForExit(90000)){
          try {$child.Kill()} catch {}
          throw "Godot headless smoke #$index exceeded 90-second limit"
        }
        $child.WaitForExit()
        $outputText=$outTask.GetAwaiter().GetResult()
        $errorText=$errTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($stdout,$outputText)
        [IO.File]::WriteAllText($stderr,$errorText)
        if(-not $child.HasExited){throw "Godot process still live after wait"}
        $exitValue=$child.ExitCode
        if($null -eq $exitValue){throw "Actual .NET Process.ExitCode unexpectedly absent"}
        $code=[int]$exitValue
      } catch {
        throw "Bundled Godot headless smoke #$index process execution failed: $_. See $stdout and $stderr. Game was not launched."
      } finally {
        $child.Dispose()
      }
      $output=$outputText+$errorText
      $errors=($output -match '(?im)SCRIPT ERROR:|ERROR: Failed to load script|Parse Error:|Error at \(')
      if($code -ne 0 -or $errors){
        throw "Bundled Godot console smoke #$index failed: code=$code parser_error=$errors executable=$CliGodotExe. See $stdout and $stderr. Game was not launched."
      }
    }
    Write-Host 'Bundled Godot 4.7.2 parser/scene checks passed.'
  }
  if($VerifyOnly){
    # Run ONLY the actual extracted ZIP's bundled engine and smoke suites.
    # No Godot window / user input, and no physical-GPU PASS asserted.
    Write-Host 'Verified packaged native host, bundled Godot SHA/version and actual Godot headless scenes. Physical 3D still unapproved.'
    $pass=$true
  } else {
    # Screenshot key is opt-in; captures ONLY the game viewport plus read-only
    # verified original C++ frames. No Windows desktop/screen recording.
    New-Item -ItemType Directory -Force -Path $operatorLogDir | Out-Null
    $env:SG_FP3D_OPERATOR_CAPTURE_DIR=$operatorLogDir
    Write-Host 'Original saved colony starts automatically: WASD walk to cyan hatch, E board (auto native pilot seat and permanent third-person camera), mouse/A,D steer, U undock, B brake, G dock near colony, ONE E disembark after return docking, F11 capture. No cabin exploration or V toggle. H retries a failed station connection only.'
    Write-Host 'For physical playtest evidence, press F11 at native station, docked cockpit, exterior flight, asteroid contact and after restart. F12 marks a stuck-input or stutter event in the native command/frame-time trace.'
    $game=Start-Process -FilePath $GodotExe -ArgumentList @('--path',('"' + $project + '"')) -Wait -PassThru
    Write-Host "Godot exited code $($game.ExitCode). Window close is NOT proof of physical gameplay acceptance."
    $pass=$game.ExitCode -eq 0
  }
} catch {
  Write-Host "PLAYTEST BLOCKED: $_" -ForegroundColor Red
} finally {
  Remove-Item Env:SG_FP3D_OPERATOR_CAPTURE_DIR -ErrorAction SilentlyContinue
  $nativeReceipts=Join-Path $operatorLogDir 'operator-viewport-receipts.jsonl'
  $captureCount=0
  $verifiedCaptureCount=0
  if(Test-Path -LiteralPath $nativeReceipts -PathType Leaf){
    foreach($line in (Get-Content -LiteralPath $nativeReceipts -ErrorAction SilentlyContinue)){
      try {
        $record=$line | ConvertFrom-Json -ErrorAction Stop
        $picture=Join-Path $operatorLogDir $record.image
        if($record.schema -eq 'SG-FP3D-OPERATOR-01' -and
           (Test-Path -LiteralPath $picture -PathType Leaf) -and
           (Get-LocalSha256 $picture) -ieq $record.image_sha256){
          $captureCount++
          if($record.native_flight_joined -or $record.station_person.state -eq 4){$verifiedCaptureCount++}
        }
      } catch { Write-Warning 'Invalid operator screenshot receipt; not counted as evidence.' }
    }
  }
  # Only adapter name/driver version are retained locally; neither the
  # Windows desktop nor private original-world SQLite is uploaded.
  $physicalVideo=@()
  if(-not $VerifyOnly){
    try {
      $physicalVideo=@(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop | ForEach-Object {
        [ordered]@{name=[string]$_.Name;driver_version=[string]$_.DriverVersion}
      })
    } catch {
      Write-Warning 'Windows GPU adapter metadata unavailable; physical operator review still required.'
    }
  }
  $seenR9700=@($physicalVideo | Where-Object { $_.name -match 'R9700' }).Count -gt 0
  $manifest=[ordered]@{
    task='FP3D-WIN02-BUNDLED-RUNTIME';godot=$GodotExe
    engine_mode=if($GodotExe -eq (Join-Path $project 'runtime\Godot_v4.7.2-stable_win64.exe')){'BUNDLED-VERIFIED'}else{'EXPLICIT-DEVELOPER-OVERRIDE'}
    engine_sha256=if(Test-Path -LiteralPath $GodotExe -PathType Leaf){(Get-LocalSha256 $GodotExe)}else{''}
    godot_console_sha256=if($CliGodotExe -and (Test-Path -LiteralPath $CliGodotExe -PathType Leaf)){(Get-LocalSha256 $CliGodotExe)}else{''}
    native_station_hash=if(Test-Path $station){(Get-LocalSha256 $station).ToUpperInvariant()}else{''}
    native_cockpit_hash=if(Test-Path $pilot){(Get-LocalSha256 $pilot).ToUpperInvariant()}else{''}
    sgdx_supervisor_hash=if(Test-Path $supervisor){(Get-LocalSha256 $supervisor).ToUpperInvariant()}else{''}
    build_sha=if(Test-Path $shaManifest){(Get-Content -LiteralPath $shaManifest -Raw).Trim()}else{''}
    operator_capture_session=if($VerifyOnly){''}else{$operatorSessionName}
    windows_gpu_adapters=$physicalVideo
    windows_r9700_adapter_name_seen=$seenR9700
    windows_gpu_data_is_not_acceptance=$true
    verified_game_viewport_screenshots=$captureCount
    verified_native_frame_screenshots=$verifiedCaptureCount
    operator_visual_acceptance='NOT ATTESTED: review actual PNGs and original SQLite receipts'
    result=if(-not $pass){'BLOCKED'}elseif($VerifyOnly){'BUNDLED-PACKAGE-HEADLESS-PREFLIGHT-VERIFIED-NO-GPU-CLAIM'}else{'WINDOW-CLOSED-NEEDS-MANUAL-OBSERVATION'}
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logDir 'result.json') -Encoding UTF8
  $zip=Join-Path $project 'SingleGalaxy_Station_Playtest_Evidence.zip'
  if(Test-Path $zip){Remove-Item $zip -Force}
  if(-not $VerifyOnly -and (Test-Path -LiteralPath $operatorLogDir -PathType Container)){
    # Current session only; do not let older screenshots satisfy a new run.
    Compress-Archive -Path @((Join-Path $logDir 'result.json'),$operatorLogDir) -DestinationPath $zip -Force
  } else {
    Compress-Archive -Path (Join-Path $logDir '*') -DestinationPath $zip -Force
  }
  Write-Host "Evidence: $zip"
  if(-not $pass){exit 1}
}
