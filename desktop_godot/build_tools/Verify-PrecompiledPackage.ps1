# Verify the actual packaged Windows x64 native game, not just build targets.
# Uses the archive that will be delivered to the player. No Godot downloads,
# CLI installation, signature requirement or unverified source substitution.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Archive,
  [Parameter(Mandatory=$true)][string]$ExpectedSourceSha,
  [string]$EvidenceFile=""
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if($ExpectedSourceSha -notmatch '^[0-9a-f]{40}$'){throw 'Expected exact lowercase 40-digit source SHA'}
$archivePath=[IO.Path]::GetFullPath($Archive)
if(-not(Test-Path -LiteralPath $archivePath -PathType Leaf)){throw "Windows game archive missing: $archivePath"}
if([string]::IsNullOrWhiteSpace($EvidenceFile)){
  $EvidenceFile=Join-Path ([IO.Path]::GetDirectoryName($archivePath)) 'SGDX03-Windows-Package-Verification.json'
}
$evidencePath=[IO.Path]::GetFullPath($EvidenceFile)
$staging=Join-Path ([IO.Path]::GetTempPath()) ('sgdx03-package-'+[guid]::NewGuid().ToString('N'))
$checks=[ordered]@{}
$state=[ordered]@{
  schema='SGDX03-WINDOWS-PACKAGE-01'
  source_sha=$ExpectedSourceSha
  archive_sha256=(Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
  result='BLOCKED'
  checks=$checks
  signed_binaries_not_required=$true
}
function Verify-Pe64([string]$FullPath){
  $data=[IO.File]::ReadAllBytes($FullPath)
  if($data.Length -lt 512 -or $data[0] -ne 0x4d -or $data[1] -ne 0x5a){
    throw "No genuine DOS/PE header: $FullPath"
  }
  $offset=[BitConverter]::ToInt32($data,0x3c)
  if($offset -lt 64 -or $offset -gt ($data.Length - 32) -or
     $data[$offset] -ne 0x50 -or $data[$offset+1] -ne 0x45 -or
     $data[$offset+2] -ne 0 -or $data[$offset+3] -ne 0){
    throw "Invalid Windows PE header: $FullPath"
  }
  if([BitConverter]::ToUInt16($data,$offset+4) -ne 0x8664 -or
     [BitConverter]::ToUInt16($data,$offset+24) -ne 0x20b){
    throw "Not a Windows x64 PE32+ image: $FullPath"
  }
  return (Get-FileHash -LiteralPath $FullPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
try {
  New-Item -ItemType Directory -Force -Path $staging | Out-Null
  Expand-Archive -LiteralPath $archivePath -DestinationPath $staging -Force
  $game=Join-Path $staging 'desktop_godot'
  $entry=Join-Path $staging 'START_SINGLE_GALAXY_WINDOWS.cmd'
  if(-not(Test-Path -LiteralPath $entry -PathType Leaf)){
    throw 'Self-contained Windows ZIP missing root-level double-click launcher'
  }
  $entryText=Get-Content -LiteralPath $entry -Raw
  if($entryText -notmatch 'RUN_STATION_PLAYTEST_WINDOWS' -or
     $entryText -match '(?i)(curl|powershell\s+-command\s+invoke-webrequest|download)'){
    throw 'Root Windows playtest launcher does not delegate to the bundled original game'
  }
  $game=Join-Path $staging 'desktop_godot'
  if(-not(Test-Path -LiteralPath $game -PathType Container)){
    throw 'ZIP does not contain the original desktop_godot project folder'
  }
  $shaText=(Get-Content -LiteralPath (Join-Path $staging 'SOURCE_SHA.txt') -Raw).Trim()
  if($shaText -cne "Exact source SHA: $ExpectedSourceSha"){
    throw "Archive source SHA mismatch: $shaText"
  }
  $native=Join-Path $game 'native'
  $manifest=(Get-Content -LiteralPath (Join-Path $native 'SGDX_BUILD_SHA.txt') -Raw).Trim()
  if($manifest -cne $ExpectedSourceSha){throw "Native SGDX build SHA mismatch: $manifest"}
  $checks['source_and_native_manifest']='PASS'
  foreach($file in @(
    'project.godot','RUN_STATION_PLAYTEST_WINDOWS.cmd',
    'build_tools/Launch-PrebuiltStation.ps1',
    'world/main.gd','world/native_player_session.gd',
    'world/native_station_session.gd','world/sgdx_process_launch.gd',
    'world/refinery_terminal_ui.gd',
    'tests/fp3d_env01_live_scene_acceptance.gd'
  )){
    $full=Join-Path $game $file
    if(-not(Test-Path -LiteralPath $full -PathType Leaf)){
      throw "Packaged Godot/control resource missing: $file"
    }
  }
  $checks['godot_project_and_double_click_launcher']='PASS'
  $pe=@{}
  foreach($file in @(
    'galaxy_desktop_station_host.exe',
    'galaxy_desktop_player_host.exe',
    'galaxy_desktop_readonly_host.exe',
    'galaxy_server_dx_supervisor.exe',
    'sqlite3.dll'
  )){
    $full=Join-Path $native $file
    if(-not(Test-Path -LiteralPath $full -PathType Leaf)){
      throw "Precompiled Windows native component absent: $file"
    }
    $pe[$file]=Verify-Pe64 $full
  }
  $checks['verified_windows_pe64']=$pe
  # Self-contained playtest: the official 4.7.2 engine is inside this ZIP.
  # The engine archive's published release SHA-256 is pinned in CI before
  # extraction; independently hash the shipped executable against the
  # exact-archive manifest after re-extracting the DELIVERED package.
  $runtime=Join-Path $game 'runtime'
  $engine=Join-Path $runtime 'Godot_v4.7.2-stable_win64.exe'
  $engineHashPath=Join-Path $runtime 'GODOT_RUNTIME_SHA256.txt'
  $engineLicense=Join-Path $runtime 'LICENSE.txt'
  foreach($file in @($engine,$engineHashPath,$engineLicense)){
    if(-not(Test-Path -LiteralPath $file -PathType Leaf)){
      throw "Bundled Godot 4.7.2 Windows engine/hash/license absent: $file"
    }
  }
  $engineHash=(Get-Content -LiteralPath $engineHashPath -Raw).Trim().ToLowerInvariant()
  if($engineHash -notmatch '^[0-9a-f]{64}$'){
    throw 'Bundled Godot runtime hash manifest malformed'
  }
  $actualHash=Verify-Pe64 $engine
  if($actualHash -cne $engineHash){
    throw 'Bundled Godot runtime hash fails after re-extracting the delivered ZIP'
  }
  $licenseText=Get-Content -LiteralPath $engineLicense -Raw
  if($licenseText -notmatch 'Godot Engine contributors' -or
     $licenseText -notmatch 'Permission is hereby granted, free of charge' -or
     $licenseText -notmatch 'copyright notice'){
    throw 'Bundled Godot upstream MIT license text missing or incomplete'
  }
  $checks['bundled_official_godot_pe64_sha256_license']=$actualHash
  # No private machine data or crash dumps may enter the end-user archive.
  $forbidden=Get-ChildItem -LiteralPath $staging -File -Recurse |
    Where-Object { $_.Name -match '(?i)\.(dmp|mdmp|sqlite|sqlite-wal|sqlite-shm)$' }
  if($forbidden){throw "Archive contains private crash data or SQLite save: $($forbidden[0].FullName)"}
  $checks['no_private_world_or_crash_dumps']='PASS'
  $launcher=Get-Content -LiteralPath (Join-Path $game 'build_tools/Launch-PrebuiltStation.ps1') -Raw
  if($launcher -notmatch 'SGDX_BUILD_SHA' -or $launcher -notmatch 'galaxy_server_dx_supervisor' -or
     $launcher -notmatch 'GODOT_RUNTIME_SHA256' -or
     $launcher -notmatch '4\.7\.2' -or $launcher -notmatch 'Start-Process'){
    throw 'Package is missing supervised native/Godot version/windowed launch enforcement'
  }
  $checks['supervised_bundled_windowed_entry']='PASS'
  $state['result']='BUNDLED_GODOT_AND_NATIVE_WINDOWS_PE64_VERIFIED'
} catch {
  $state['error']=$_.Exception.Message
  throw
} finally {
  $parent=[IO.Path]::GetDirectoryName($evidencePath)
  if(-not(Test-Path -LiteralPath $parent)){New-Item -ItemType Directory -Force -Path $parent | Out-Null}
  $state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
  if(Test-Path -LiteralPath $staging){Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue}
  Write-Host "Package verification evidence: $evidencePath"
}
Write-Host "Self-contained Windows x64 native+bundled-Godot package PASS: $ExpectedSourceSha"
