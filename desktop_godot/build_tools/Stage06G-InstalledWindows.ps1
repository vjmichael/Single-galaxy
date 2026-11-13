# Stage 6 G — genuine Windows test-only NSIS install / supervised planet / uninstall.
# Hosted Windows runner evidence is NOT physical Radeon or human acceptance.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BuildDir,
      [Parameter(Mandatory=$true)][string]$SourceSha,
      [string]$EvidenceDir='build/stage06g-installed-evidence')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$BuildDir=[IO.Path]::GetFullPath($BuildDir)
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
$head=(git -C $root rev-parse HEAD | Out-String).Trim()
if($head -cne $SourceSha -or $head -notmatch '^[0-9a-f]{40}$'){
    throw "Expected checked-out exact SHA $SourceSha; got $head"
}
$checks=New-Object 'System.Collections.Generic.List[object]'
function Gate([string]$name,[string]$details){
    $checks.Add([ordered]@{name=$name;result='PASS';details=$details})
    Write-Host "[PASS] $name : $details"
}
$result=[ordered]@{schema='SG-S6G-HOSTED-INSTALLED-01';status='blocked';
                  source_sha=$SourceSha;physical_acceptance='OPEN';
                  installer_sha256=$null;checks=$checks;error=$null}
$payload=Join-Path $EvidenceDir 'original-game-payload'
$game=Join-Path $payload 'desktop_godot'
$native=Join-Path $game 'native'
$runtime=Join-Path $game 'runtime'
$installer=Join-Path $EvidenceDir 'SingleGalaxy_Stage06G_Windows_Unsigned_Test.exe'
$installed=Join-Path $env:RUNNER_TEMP 'SingleGalaxyStage6Acceptance'
$uninstalled=$false
try {
    if(Test-Path -LiteralPath $installed){throw 'Dedicated test installation already exists'}
    New-Item -ItemType Directory -Force -Path $native,$runtime | Out-Null
    foreach($file in @('project.godot','RUN_STATION_PLAYTEST_WINDOWS.cmd')){
        Copy-Item -LiteralPath (Join-Path $root "desktop_godot/$file") -Destination $game -Force
    }
    foreach($dir in @('world','tests','build_tools')){
        Copy-Item -LiteralPath (Join-Path $root "desktop_godot/$dir") -Destination $game -Recurse -Force
    }
    Copy-Item -LiteralPath (Join-Path $root 'desktop_godot/START_SINGLE_GALAXY_WINDOWS.cmd') -Destination (Join-Path $payload 'START_SINGLE_GALAXY_WINDOWS.cmd')
    $SourceSha | Set-Content -LiteralPath (Join-Path $payload 'SOURCE_SHA.txt') -NoNewline
    $SourceSha | Set-Content -LiteralPath (Join-Path $native 'SGDX_BUILD_SHA.txt') -NoNewline
    foreach($name in @('galaxy_desktop_player_host.exe','galaxy_desktop_station_host.exe',
                        'galaxy_desktop_readonly_host.exe','galaxy_server_dx_supervisor.exe')){
        $source=Join-Path $BuildDir "Release/$name"
        if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw "Original MSVC host missing: $name"}
        Copy-Item -LiteralPath $source -Destination $native
    }
    $vcpkg=$env:VCPKG_INSTALLATION_ROOT
    if(-not $vcpkg){$vcpkg='C:\vcpkg'}
    $sqlite=Join-Path $vcpkg 'installed/x64-windows/bin/sqlite3.dll'
    if(-not(Test-Path -LiteralPath $sqlite -PathType Leaf)){throw 'Compiled Windows SQLite DLL missing'}
    Copy-Item -LiteralPath $sqlite -Destination $native
    Gate 'original-game-native-bytes' 'Four native hosts + real SQLite DLL + exact source SHA'

    $archive=Join-Path $EvidenceDir 'Godot_v4.7.2-stable_win64.exe.zip'
    & curl.exe '--fail' '--location' '--retry' '3' '--connect-timeout' '25' '--max-time' '240' '--proto' '=https' '--proto-redir' '=https' '--output' $archive 'https://github.com/godotengine/godot-builds/releases/download/4.7.2-stable/Godot_v4.7.2-stable_win64.exe.zip'
    if($LASTEXITCODE -ne 0){throw 'Official upstream Windows Godot download failed'}
    $archiveHash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if($archiveHash -cne '731980f9608d61333e5baf54a2ef17210acc7a538446c0cb9969f002aca1e953'){
        throw 'Official Windows Godot 4.7.2 hash mismatch'
    }
    $extract=Join-Path $EvidenceDir 'godot-release'
    Expand-Archive -LiteralPath $archive -DestinationPath $extract -Force
    foreach($name in @('Godot_v4.7.2-stable_win64.exe',
                        'Godot_v4.7.2-stable_win64_console.exe')){
        $source=Join-Path $extract $name
        if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing Godot runtime $name"}
        Copy-Item -LiteralPath $source -Destination $runtime
    }
    $godot=Join-Path $runtime 'Godot_v4.7.2-stable_win64.exe'
    $godotConsole=Join-Path $runtime 'Godot_v4.7.2-stable_win64_console.exe'
    (Get-FileHash -LiteralPath $godot -Algorithm SHA256).Hash.ToLowerInvariant() |
        Set-Content -LiteralPath (Join-Path $runtime 'GODOT_RUNTIME_SHA256.txt') -NoNewline
    (Get-FileHash -LiteralPath $godotConsole -Algorithm SHA256).Hash.ToLowerInvariant() |
        Set-Content -LiteralPath (Join-Path $runtime 'GODOT_CONSOLE_SHA256.txt') -NoNewline
    Copy-Item -LiteralPath (Join-Path $root 'desktop_godot/third_party/GODOT_LICENSE.txt') -Destination (Join-Path $runtime 'LICENSE.txt')
    Gate 'verified-Godot' 'Original official Windows runtime digest and licensing'

    $files=@(Get-ChildItem -LiteralPath $payload -File -Recurse | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($payload.Length+1).Replace('\','/');
                   sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    } | Sort-Object { $_.path })
    $files | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'payload-hashes.json') -Encoding UTF8
    $available=Get-Command makensis.exe -ErrorAction SilentlyContinue
    $makensis=if($available){$available.Source}else{'C:\Program Files (x86)\NSIS\makensis.exe'}
    if(-not(Test-Path -LiteralPath $makensis -PathType Leaf)){
        & choco.exe install nsis '--yes' '--no-progress' '--limit-output'
        if($LASTEXITCODE -ne 0){throw 'NSIS tool unavailable for genuine installer'}
    }
    if(-not(Test-Path -LiteralPath $makensis -PathType Leaf)){
        $makensis=(Get-Command makensis.exe -ErrorAction Stop).Source
    }
    $nsisLog=Join-Path $EvidenceDir 'nsis-build.log'
    & $makensis "/DPAYLOAD=$payload" "/DOUTFILE=$installer" (Join-Path $root 'desktop_godot/build_tools/Stage06G-Installed.nsi') *> $nsisLog
    if($LASTEXITCODE -ne 0 -or -not(Test-Path -LiteralPath $installer -PathType Leaf)){
        throw 'Actual NSIS installer compile failed'
    }
    $result.installer_sha256=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
    Gate 'actual-NSIS-test-installer' 'Unsigned per-user Windows executable and SHA256'

    & $installer '/S' "/D=$installed"
    if($LASTEXITCODE -ne 0){throw 'Actual silent Windows install failed'}
    $key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SingleGalaxyStage6Acceptance'
    if(-not(Test-Path -LiteralPath $key) -or
       -not(Test-Path -LiteralPath (Join-Path $installed 'Uninstall.exe'))){
        throw 'No actual Windows installed-app HKCU registration or uninstaller'
    }
    if((Get-ItemProperty -LiteralPath $key).InstallLocation -cne $installed){
        throw 'Windows installation registry path mismatch'
    }
    foreach($record in $files){
        $path=Join-Path $installed ($record.path -replace '/', '\')
        if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or
           (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.sha256){
            throw "Installed original game file hash mismatch: $($record.path)"
        }
    }
    Gate 'actual-OS-installed-original-payload' 'HKCU registration and every installed byte SHA256 validated'
    $installedGame=Join-Path $installed 'desktop_godot'
    $launchLog=Join-Path $EvidenceDir 'installed-bundled-runtime.log'
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $installedGame 'build_tools/Launch-PrebuiltStation.ps1') -VerifyOnly *> $launchLog
    if($LASTEXITCODE -ne 0){throw 'Actually installed original game headless launcher failed'}
    $launch=Get-Content -LiteralPath (Join-Path $installedGame 'playtest-logs/result.json') -Raw | ConvertFrom-Json
    if($launch.result -cne 'BUNDLED-PACKAGE-HEADLESS-PREFLIGHT-VERIFIED-NO-GPU-CLAIM' -or
       $launch.build_sha -cne $SourceSha){throw 'Installed original-game launcher source mismatch'}
    Gate 'installed-original-Godot-launcher' 'Bundled Godot and native host parser/smoke pass'

    $packet=Join-Path $EvidenceDir 'native-planet-source.json'
    $planetLog=Join-Path $EvidenceDir 'installed-planet-custody.log'
    python (Join-Path $root 'tests/Stage06GInstalledSupervisor.py') --supervisor (Join-Path $installedGame 'native/galaxy_server_dx_supervisor.exe') --fixture (Join-Path $BuildDir 'Release/galaxy_stage6f5e_planet_host_fixture.exe') --host (Join-Path $installedGame 'native/galaxy_desktop_player_host.exe') --sha $SourceSha --packet $packet *> $planetLog
    if($LASTEXITCODE -ne 0){throw 'Actual installed native planetary SGDX/restart gate failed'}
    Gate 'installed-real-planet-process-custody' 'Original 16+ ticks, guardian kill, frozen journal, re-admission and stale denial'
    $env:SG_STAGE6G_PACKET=$packet
    $terrainLog=Join-Path $EvidenceDir 'installed-real-planet-terrain.log'
    try {
        & (Join-Path $installedGame 'runtime/Godot_v4.7.2-stable_win64_console.exe') --headless --path $installedGame --script 'res://tests/stage6g_native_terrain_installed.gd' *> $terrainLog
        if($LASTEXITCODE -ne 0){throw 'Actually installed source-owned Godot terrain test failed'}
    } finally {Remove-Item Env:SG_STAGE6G_PACKET -ErrorAction SilentlyContinue}
    $godotLog=Get-Content -LiteralPath $terrainLog -Raw
    if($godotLog -notmatch 'S6-G installed original C\+\+ 3x3 terrain atlas.*PASS' -or
       $godotLog -match 'SCRIPT ERROR:|ERROR: Failed to load script|Parse Error:'){
        throw 'Installed renderer rejected real original 3x3 atlas'
    }
    Gate 'installed-original-terrain' 'Native atlas rendered by installed Godot; mixed-session refused'
    $result.status='hosted_installed_pass_only'
} catch {
    $result.error=[string]$_
    Write-Warning "Hosted S6-G installed gate failed: $_"
} finally {
    if(Test-Path -LiteralPath (Join-Path $installed 'Uninstall.exe')){
        & (Join-Path $installed 'Uninstall.exe') '/S'
        if($LASTEXITCODE -eq 0){$uninstalled=$true}
    }
    Start-Sleep -Seconds 1
    if($uninstalled -and -not(Test-Path -LiteralPath (Join-Path $installed 'desktop_godot')) -and
       -not(Test-Path -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SingleGalaxyStage6Acceptance')){
        Gate 'actual-Windows-uninstall' 'Installed files and original registry entry removed'
    } elseif($result.status -eq 'hosted_installed_pass_only'){
        $result.status='blocked'
        $result.error='Actual NSIS Windows uninstall failed'
    }
    $result | ConvertTo-Json -Depth 7 |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'stage06g-installed-acceptance.json') -Encoding UTF8
}
if($result.status -cne 'hosted_installed_pass_only'){exit 1}
