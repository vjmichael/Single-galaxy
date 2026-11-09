# PLAY-STATION-04B fixture entry: finite original test world and prebuilt native hosts.
# No overwrite or implicit mineral replenishment after the world is created.
[CmdletBinding()]
param([string]$GodotExe="")
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$seedExe=Join-Path $project 'native\galaxy_play_station04b_native_mining_seed.exe'
$saveDir=Join-Path $project 'playtest-data'
$save=Join-Path $saveDir '04b_original_world.sqlite'
if(-not(Test-Path -LiteralPath $seedExe -PathType Leaf)){
    throw "Compiled PLAY-STATION-04B test seed is missing: $seedExe"
}
New-Item -ItemType Directory -Force $saveDir | Out-Null
if(-not(Test-Path -LiteralPath $save -PathType Leaf)){
    & $seedExe $save
    if($LASTEXITCODE -ne 0 -or -not(Test-Path -LiteralPath $save)){
        throw 'Original 04B finite-world genesis failed; never launch a fallback world'
    }
}
$env:SG_04B_TEST_SAVE=$save
Write-Host "PLAY-STATION-04B: Original finite world at $save"
Write-Host 'Controls: P opens your original cockpit, E sits, G fabricates, F fits, K power, M extracts 1t, U unfit only when docked, V exterior'
Write-Host 'Do not delete playtest-data to respawn inventory: this save persists across launches.'
& (Join-Path $PSScriptRoot 'Launch-PrebuiltStation.ps1') -GodotExe $GodotExe
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
