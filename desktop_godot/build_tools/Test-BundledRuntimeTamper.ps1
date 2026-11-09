# Negative acceptance: a self-contained Windows alpha must reject a
# mismatched bundled Godot engine digest rather than silently finding an
# external editor. Mutates ONLY an extracted test copy, never the source ZIP.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ExtractedGame,
      [Parameter(Mandatory=$true)][string]$EvidenceDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$game=[IO.Path]::GetFullPath($ExtractedGame)
$logDir=[IO.Path]::GetFullPath($EvidenceDirectory)
$launcher=Join-Path $game 'build_tools\Launch-PrebuiltStation.ps1'
$engine=Join-Path $game 'runtime\Godot_v4.7.2-stable_win64.exe'
$manifest=Join-Path $game 'runtime\GODOT_RUNTIME_SHA256.txt'
foreach($f in @($launcher,$engine,$manifest)){
  if(-not(Test-Path -LiteralPath $f -PathType Leaf)){
    throw "Cannot run fail-closed tamper test; extracted file absent: $f"
  }
}
New-Item -ItemType Directory -Force $logDir | Out-Null
$original=(Get-Content -LiteralPath $manifest -Raw).Trim()
if($original -notmatch '^[0-9a-f]{64}$'){throw 'Original source engine digest malformed'}
$actual=(Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()
if($actual -cne $original){throw 'Extracted engine already corrupt before negative test'}

# The actual double-click .cmd launches Windows PowerShell 5.1, not pwsh 7.
# Prove a clean packaged game can complete its real bundled engine smoke in
# that exact interpreter BEFORE deliberately corrupting the digest.
$windowsPowershell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
if(-not(Test-Path -LiteralPath $windowsPowershell -PathType Leaf)){
  throw 'Actual Windows PowerShell 5.1 unavailable'
}
$positiveOut=Join-Path $logDir 'powershell-5_1-clean-stdout.log'
$positiveErr=Join-Path $logDir 'powershell-5_1-clean-stderr.log'
$clean=Start-Process -FilePath $windowsPowershell -ArgumentList @(
  '-NoProfile','-ExecutionPolicy','Bypass','-File',('"' + $launcher + '"'),
  '-VerifyOnly'
) -Wait -PassThru -RedirectStandardOutput $positiveOut -RedirectStandardError $positiveErr
if($clean.ExitCode -ne 0){
  throw 'Real Windows PowerShell 5.1 could not run the unmodified bundled native/Godot ZIP; inspect clean smoke logs'
}
$cleanReport=Join-Path $game 'playtest-logs\result.json'
if(-not(Test-Path -LiteralPath $cleanReport -PathType Leaf)){
  throw 'Real Windows PowerShell 5.1 did not write a verified package receipt'
}
$cleanResult=Get-Content -LiteralPath $cleanReport -Raw | ConvertFrom-Json
if($cleanResult.result -cne 'BUNDLED-PACKAGE-HEADLESS-PREFLIGHT-VERIFIED-NO-GPU-CLAIM' -or
   $cleanResult.engine_mode -cne 'BUNDLED-VERIFIED' -or
   $cleanResult.engine_sha256 -cne $original){
  throw 'Real Windows PowerShell 5.1 package smoke did not attest the original bundled engine'
}
Copy-Item -LiteralPath $cleanReport -Destination (Join-Path $logDir 'powershell-5_1-clean-preflight.json') -Force

$record=[ordered]@{
  schema='SG-FP3D-BUNDLED-TAMPER-01';type='manifest-mismatch'
  original_engine_sha256=$actual;clean_powershell5_preflight='PASS';result='BLOCKED';exit_code=$null
}
try {
  # This is a syntactically valid digest, but not the true engine hash.
  # Avoid expensive copy/patch of the 100MB PE executable itself.
  $wrong=if($original[0] -eq '0'){'1'+$original.Substring(1)}else{'0'+$original.Substring(1)}
  Set-Content -LiteralPath $manifest -Value $wrong -NoNewline
  $stdout=Join-Path $logDir 'tampered-launch-stdout.log'
  $stderr=Join-Path $logDir 'tampered-launch-stderr.log'
  # Match the real player's double-click Windows PowerShell 5.1 runtime.
  $p=Start-Process -FilePath $windowsPowershell -ArgumentList @(
    '-NoProfile','-ExecutionPolicy','Bypass','-File',('"' + $launcher + '"'),
    '-VerifyOnly'
  ) -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $record.exit_code=$p.ExitCode
  if($p.ExitCode -eq 0){throw 'CORRUPTED BUNDLED ENGINE SHA WAS ACCEPTED'}
  $output=Get-Content -LiteralPath $stdout -Raw
  if($output -notmatch 'Bundled Godot SHA-256 does not match release-derived package manifest'){
    throw 'The negative test failed for an unrelated reason; no hash-refusal receipt'
  }
  $resultFile=Join-Path $game 'playtest-logs\result.json'
  if(-not(Test-Path -LiteralPath $resultFile -PathType Leaf)){
    throw 'No fail-closed launcher evidence record was written'
  }
  $result=Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
  if($result.result -cne 'BLOCKED'){
    throw 'Corrupt bundled runtime did not create a blocked playtest receipt'
  }
  $record.result='EXPECTED_HASH_REFUSAL_VERIFIED'
} finally {
  # Always restore the extracted test copy, even if the negative check fails.
  Set-Content -LiteralPath $manifest -Value $original -NoNewline
  if((Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant() -cne
     (Get-Content -LiteralPath $manifest -Raw).Trim().ToLowerInvariant()){
    throw 'Failed to restore exact packaged runtime source after negative test'
  }
  $record | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $logDir 'bundled-tamper-result.json') -Encoding UTF8
}
if($record.result -cne 'EXPECTED_HASH_REFUSAL_VERIFIED'){
  throw 'Bundled engine negative test did not prove fail-closed admission'
}
Write-Host 'PASS: tampered Godot runtime SHA was denied by real Windows PowerShell launcher; extracted engine restored.'
