# Genuine non-headless Windows engine + original C++/SQLite acceptance.
# Runs ONLY after compiling native x64 MSVC hosts and the headless native gate.
# This automated viewport test is NOT a replacement for human physical GPU play.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$GodotExe,
  [Parameter(Mandatory=$true)][string]$Project,
  [Parameter(Mandatory=$true)][string]$BuildRoot,
  [Parameter(Mandatory=$true)][string]$SeedExe
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$exe=[IO.Path]::GetFullPath($GodotExe)
$projectPath=[IO.Path]::GetFullPath($Project)
$build=[IO.Path]::GetFullPath($BuildRoot)
$seed=[IO.Path]::GetFullPath($SeedExe)
foreach($file in @($exe,$seed)){
  if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Windows acceptance executable missing: $file"}
}
$env:SG_FP3D_E2E_SAVE=Join-Path $build 'sgdx-windowed-world.sqlite'
$env:SG_FP3D_VISUAL_CAPTURE_DIR=Join-Path $build 'godot-visual'
New-Item -ItemType Directory -Force $env:SG_FP3D_VISUAL_CAPTURE_DIR | Out-Null
if(Test-Path -LiteralPath $env:SG_FP3D_E2E_SAVE){
  throw 'Windowed source save must be a new original SQLite world; will not overwrite'
}
& $seed $env:SG_FP3D_E2E_SAVE
if($LASTEXITCODE -ne 0){throw 'Genuine original C++ finite-rock world seed failed'}
$stdout=Join-Path $build 'godot-logs/windowed-stdout.log'
$stderr=Join-Path $build 'godot-logs/windowed-stderr.log'
$proc=$null
try {
  # NO --headless. The GDScript itself rejects a headless display backend.
  # Windows runner may lack an interactive display: if so, fail honestly.
  $proc=Start-Process -FilePath $exe -ArgumentList @(
    '--path',('"' + $projectPath + '"'),
    '--rendering-method','gl_compatibility',
    '--script','res://tests/fp3d_env01_live_scene_acceptance.gd'
  ) -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  try {Wait-Process -Id $proc.Id -Timeout 240 -ErrorAction Stop}
  catch {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    throw 'Windowed Godot/native test timed out: inspect captured stderr/stdout'
  }
  $proc.Refresh()
  if($proc.ExitCode -ne 0){throw "Native windowed scene failed, Godot exit $($proc.ExitCode)"}
  $manifest=Join-Path $env:SG_FP3D_VISUAL_CAPTURE_DIR 'windowed-evidence.json'
  if(-not(Test-Path -LiteralPath $manifest -PathType Leaf)){
    throw 'No authentic rendered screenshot manifest generated'
  }
  $proof=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
  if($proof.schema -ne 'SG-FP3D-WINDOW-01' -or
     $proof.human_gpu_visual_approval -ne $false -or $proof.frames.Count -ne 4){
    throw 'Windowed screenshots failed original-source manifest schema/count'
  }
  $required=@('01-docked-cabin','02-exterior-flight','03-native-rock-impact','04-restarted-world')
  $seen=@{}
  $hashes=@{}
  foreach($frame in $proof.frames){
    $stage=[string]$frame.stage
    if($stage -notin $required -or $seen.ContainsKey($stage)){
      throw "Unexpected or repeated original-world stage $stage"
    }
    $seen[$stage]=$frame
    if($frame.display_backend -ieq 'headless' -or
       $frame.image_width -lt 640 -or $frame.image_height -lt 360 -or
       $frame.sampled_color_count -lt 4){
      throw "Missing or blank real rendered image $stage"
    }
    $file=Join-Path $env:SG_FP3D_VISUAL_CAPTURE_DIR ([string]$frame.png)
    if(-not(Test-Path -LiteralPath $file -PathType Leaf)){
      throw "Actual screenshot missing: $stage"
    }
    if((Get-Item -LiteralPath $file).Length -lt 5000){
      throw "Actual viewport screenshot trivially empty: $stage"
    }
    $sha=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    if($sha -ine $frame.sha256){throw "Viewport screenshot SHA mismatch: $stage"}
    $hashes[$sha]=$true
  }
  if($hashes.Count -lt 3){throw 'Unchanged repeated screenshot pixels do not prove 3D transitions'}
  # The capture stage keeps its historical name for artifact compatibility,
  # but its contents MUST show the native pilot's exterior camera, never
  # an explorable cabin or first-person camera, even before undock.
  if($seen['01-docked-cabin'].ship.dock -ne 1 -or
     $seen['01-docked-cabin'].camera_current -ne $true -or
     $seen['01-docked-cabin'].player_camera_current -ne $false){
    throw 'Authentic docked native pilot must remain third-person, not cabin-first-person'
  }
  if($seen['02-exterior-flight'].ship.dock -ne 0 -or
     $seen['02-exterior-flight'].camera_current -ne $true){
    throw 'Authentic undocked 3rd-person exterior camera not recorded'
  }
  $impact=$seen['03-native-rock-impact']
  $after=$seen['04-restarted-world']
  if($impact.rock.impact_rock -le 0 -or
     $impact.rock.impact_tick -ne $impact.ship.tick -or
     $impact.ship.vx -ne 0 -or $impact.ship.vy -ne 0 -or
     $impact.ship.vz -ne 0 -or $impact.camera_current -ne $true){
    throw 'Original C++ collision/contact not visible and committed at this tick'
  }
  if($after.rock.impact_rock -ne 0 -or
     $after.ship.tick -ne $impact.ship.tick -or
     $after.ship.gen -ne $impact.ship.gen -or
     $after.ship.x_mm -ne $impact.ship.x_mm -or
     $after.ship.y_mm -ne $impact.ship.y_mm -or
     $after.ship.z_mm -ne $impact.ship.z_mm -or
     $after.rock.ore_tonnes -ne $impact.rock.ore_tonnes -or
     $after.camera_current -ne $true){
    throw 'Same original SQLite source pose/finite-ore/camera did not survive SGDX restart'
  }
  Write-Host 'SGDX03-WINDOWED PASS: real Godot viewport, native dock, exterior camera, physical rock contact and SQLite reopen'
} finally {
  Remove-Item Env:SG_FP3D_VISUAL_CAPTURE_DIR -ErrorAction SilentlyContinue
  Remove-Item Env:SG_FP3D_E2E_SAVE -ErrorAction SilentlyContinue
}
