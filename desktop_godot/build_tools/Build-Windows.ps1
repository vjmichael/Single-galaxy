# Single-Galaxy FP3D-02A-WINTEST: official, pinned Godot Windows export acceptance.
# Needs only Windows 10/11 PowerShell 5.1+, disk space and HTTPS to GitHub.
# It never downloads or executes plugins, scripts or build tools from strangers.
[CmdletBinding()]
param([switch]$LaunchAfterExport, [string]$ExistingGodotExe = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$tag = '4.7.2-stable'
$godotEngineVersion = '4.7.2.stable'
$editorName = 'Godot_v4.7.2-stable_win64.exe.zip'
$templatesName = 'Godot_v4.7.2-stable_export_templates.tpz'
$rootDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd([IO.Path]::DirectorySeparatorChar)
$cacheDir = Join-Path $rootDir '.sg-tool-cache'
$buildDir = Join-Path $rootDir 'build'
$logsDir = Join-Path $buildDir 'acceptance'
New-Item -ItemType Directory -Force -Path $cacheDir, $buildDir, $logsDir | Out-Null
$startedUtc = [DateTime]::UtcNow.ToString('o')
$results = New-Object 'System.Collections.Generic.List[object]'
$state = [ordered]@{stage='initialization'; status='FAILED'; release_tag=$tag; started_utc=$startedUtc; completed_utc=$null; editor_sha256=$null; template_sha256=$null; executable_sha256=$null; executable_bytes=$null; native_host_sha256=$null; native_host_bytes=$null; gate_results=$results; error=$null}
$exitCode = 1

function Record-Gate([string]$name, [string]$status, [string]$detail) {
    $results.Add([ordered]@{name=$name;status=$status;detail=$detail})
    Write-Host ('[{0}] {1}: {2}' -f $status, $name, $detail)
}
function Official-Release {
    $headers = @{ 'User-Agent'='SingleGalaxy-Godot-Release-Builder/1.0'; 'Accept'='application/vnd.github+json' }
    $uri = "https://api.github.com/repos/godotengine/godot-builds/releases/tags/$tag"
    $release = Invoke-RestMethod -Uri $uri -Headers $headers -TimeoutSec 45
    if ($release.tag_name -ne $tag -or $release.draft -or $release.prerelease) {throw 'Official release tag failed validation'}
    return $release
}
function Fetch-VerifiedAsset([object]$release, [string]$fileName) {
    $matches = @($release.assets | Where-Object { $_.name -ceq $fileName })
    if ($matches.Count -ne 1) {throw "Expected exactly one official asset $fileName"}
    $asset = $matches[0]
    $expected = [string]$asset.digest
    if ($expected -match '^sha256:([0-9a-fA-F]{64})$') {
        $hex = $Matches[1].ToLowerInvariant()
    } else {throw "Missing official SHA256 digest for $fileName; refusing unverified download"}
    if (-not ([string]$asset.browser_download_url).StartsWith("https://github.com/godotengine/godot-builds/releases/download/$tag/", [StringComparison]::Ordinal)) {
        throw "Unexpected download origin for $fileName"
    }
    $output = Join-Path $cacheDir $fileName
    if (Test-Path -LiteralPath $output) {
        $actual = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $hex) {Remove-Item -LiteralPath $output -Force; throw "Cached $fileName digest mismatch"}
        Record-Gate "verify-$fileName" 'PASS' 'Previously downloaded official asset verified'
    } else {
        # PowerShell 5.1's Invoke-WebRequest can fail partway through a >1 GB file.
        # Use Windows curl.exe (HTTPS only), preserving .part for continuation.
        $tmp = "$output.part"
        $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
        if ($null -eq $curl) {throw 'Windows curl.exe unavailable: cannot resume official asset downloads'}
        $success = $false
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            $have = 0L
            if (Test-Path -LiteralPath $tmp) {$have = (Get-Item -LiteralPath $tmp).Length}
            Write-Host "Downloading official $fileName (attempt $attempt/3; resumable cache $have bytes)..."
            & $curl.Source '--fail' '--location' '--show-error' '--retry' '5' '--retry-delay' '3' '--connect-timeout' '30' '--continue-at' '-' '--proto' '=https' '--proto-redir' '=https' '--output' $tmp ([string]$asset.browser_download_url)
            if ($LASTEXITCODE -eq 0) {$success = $true; break}
            Record-Gate "download-retry-$fileName" 'RETRY' "curl exit $LASTEXITCODE, partial file retained: $tmp"
            Start-Sleep -Seconds 3
        }
        if (-not $success) {throw "Official download interrupted after 3 retries; partial file kept at $tmp for later continuation"}
        if ((Get-Item -LiteralPath $tmp).Length -ne [int64]$asset.size) {
            throw "Partial official asset $fileName; bytes do not match release size; retaining $tmp for resume"
        }
        $actual = (Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $hex) {Remove-Item -LiteralPath $tmp -Force; throw "Downloaded $fileName SHA256 mismatch"}
        Move-Item -LiteralPath $tmp -Destination $output -Force
        Record-Gate "verify-$fileName" 'PASS' 'Official release SHA256 and size match verified resumable download'
    }
    if ((Get-Item -LiteralPath $output).Length -ne [int64]$asset.size) {throw "Official asset $fileName size mismatch"}
    return [pscustomobject]@{Path=$output;SHA256=$hex}
}
function Check-LocalEditor([string]$candidate) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {return $false}
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {throw "Local Godot editor not found: $candidate"}
    $version = (& $candidate '--version' 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -notmatch '^4\.7\.2\.stable') {
        throw "Local Godot editor must report 4.7.2.stable, got '$version'"
    }
    return $true
}
function Extract-ZipMember([string]$archivePath,[string]$fileName,[string]$outPath) {
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $found = @($zip.Entries | Where-Object { $_.Name -ceq $fileName })
        if ($found.Count -ne 1) {throw "Archive missing unique $fileName"}
        $dir = [IO.Path]::GetDirectoryName($outPath)
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        $source = $found[0].Open()
        $target = [IO.File]::Open($outPath, [IO.FileMode]::Create, [IO.FileAccess]::Write)
        try {$source.CopyTo($target)} finally {$target.Dispose();$source.Dispose()}
        if ((Get-Item -LiteralPath $outPath).Length -le 0) {throw "Empty extracted member $fileName"}
    } finally {$zip.Dispose()}
}
function Run-Godot([string]$label,[string[]]$commandArgs) {
    $output = Join-Path $logsDir "$label.stdout.log"
    $errors = Join-Path $logsDir "$label.stderr.log"
    # ArgumentList is a single command-line string on Windows PowerShell 5.1.
    $quotedArgs = @($commandArgs | ForEach-Object {if ($_ -match '\s') {'"' + ($_ -replace '"','\"') + '"'} else {$_}})
    $child = Start-Process -FilePath $script:editorExe -ArgumentList $quotedArgs -Wait -PassThru -RedirectStandardOutput $output -RedirectStandardError $errors -NoNewWindow
    $code = [int]$child.ExitCode
    Record-Gate $label $(if($code -eq 0){'PASS'}else{'FAIL'}) "Godot exit code $code; $output; $errors"
    if ($code -ne 0) {throw "Godot $label failed with exit code $code; see acceptance logs"}
}
function Verify-PE64([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {throw 'Windows executable not produced'}
    $stream = [IO.File]::OpenRead($path)
    try {
        if ($stream.Length -lt 1048576) {throw 'Export suspiciously small; missing embedded game content?'}
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5a4d) {throw 'Invalid Windows MZ signature'}
        $stream.Seek(0x3c, [IO.SeekOrigin]::Begin) | Out-Null
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt ($stream.Length-8)) {throw 'Invalid PE offset'}
        $stream.Seek($offset, [IO.SeekOrigin]::Begin) | Out-Null
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {throw 'Not PE32+ x86-64'}
    } finally {$stream.Dispose()}
}

try {
    $state.stage = 'download-official-release'
    # Explicit or PATH-selected installed 4.7.2 editor avoids a redundant download.
    # Its provenance is USER-INSTALLED, not falsely labeled official-SHA-verified.
    $editorExe = $null
    if (-not [string]::IsNullOrWhiteSpace($ExistingGodotExe)) {
        $editorExe = [IO.Path]::GetFullPath($ExistingGodotExe)
        [void](Check-LocalEditor $editorExe)
    } else {
        foreach ($name in @('godot.exe', 'Godot.exe')) {
            $installed = Get-Command $name -ErrorAction SilentlyContinue
            if ($null -ne $installed) {
                try {
                    if (Check-LocalEditor $installed.Source) {$editorExe = $installed.Source; break}
                } catch {Write-Host "Ignoring PATH Godot: $($_.Exception.Message)"}
            }
        }
    }
    $release = $null
    if ($editorExe) {
        $state.editor_provenance = 'user-installed-Godot-version-verified-no-official-asset-digest'
        Record-Gate 'existing-editor' 'PASS' "Using locally installed Godot 4.7.2; SHA256 release authenticity NOT independently verified: $editorExe"
    } else {
        $release = Official-Release
        $editor = Fetch-VerifiedAsset $release $editorName
        $state.editor_sha256 = $editor.SHA256
        $editorExe = Join-Path $cacheDir 'Godot_v4.7.2-stable_win64.exe'
        Extract-ZipMember $editor.Path 'Godot_v4.7.2-stable_win64.exe' $editorExe
        $state.editor_provenance = 'official-sha256-verified-release'
        Record-Gate 'editor-extract' 'PASS' 'Verified official x64 Godot editor extracted locally; no installation'
    }

    $state.stage = 'export-templates'
    $templateRoot = Join-Path $env:APPDATA "Godot\export_templates\$godotEngineVersion"
    $templateExe = Join-Path $templateRoot 'windows_release_x86_64.exe'
    # Existing templates may be user-installed; do not claim validated release hash in that case.
    if (-not (Test-Path -LiteralPath $templateExe)) {
        if ($null -eq $release) {$release = Official-Release}
        $templates = Fetch-VerifiedAsset $release $templatesName
        $state.template_sha256 = $templates.SHA256
        Extract-ZipMember $templates.Path 'windows_release_x86_64.exe' $templateExe
        Record-Gate 'windows-template-extract' 'PASS' 'Extracted only the Windows x64 release template from verified TPZ'
    } else {Record-Gate 'windows-template-present' 'PASS' 'Existing Godot release template found; content hash not independently authenticated'}

    $state.stage = 'godot-engine-test'
    Run-Godot 'engine-version' @('--version')
    Run-Godot 'project-import' @('--headless','--path',$rootDir,'--editor','--quit')
    Run-Godot 'runtime-smoke' @('--headless','--path',$rootDir,'--script','res://tests/godot_smoke.gd')
    Run-Godot 'native-projection-contract' @('--headless','--path',$rootDir,'--script','res://tests/native_projection_smoke.gd')
    Run-Godot 'native-player-protocol' @('--headless','--path',$rootDir,'--script','res://tests/native_player_protocol_smoke.gd')

    $state.stage = 'export-release'
    $gameExe = Join-Path $buildDir 'SingleGalaxy_Graybox.exe'
    if (Test-Path -LiteralPath $gameExe) {Remove-Item -LiteralPath $gameExe -Force}
    Run-Godot 'windows-release-export' @('--headless','--path',$rootDir,'--export-release','Windows Desktop',$gameExe)
    Verify-PE64 $gameExe
    $state.executable_sha256 = (Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash.ToLowerInvariant()
    $state.executable_bytes = (Get-Item -LiteralPath $gameExe).Length
    Record-Gate 'windows-x64-pe-inspection' 'PASS' "PE32+ AMD64, $($state.executable_bytes) bytes"
    # FP3D-02B: the desktop C++ observer is a separate native process next
    # to the exported game. Godot never imports or executes it from a PCK.
    $nativeExe = Join-Path $rootDir 'native\galaxy_desktop_readonly_host.exe'
    $stagedNative = Join-Path $buildDir 'native'
    $nativePackaged = $false
    if (Test-Path -LiteralPath $nativeExe) {
        Verify-PE64 $nativeExe
        New-Item -ItemType Directory -Force -Path $stagedNative | Out-Null
        Copy-Item -LiteralPath $nativeExe -Destination (Join-Path $stagedNative 'galaxy_desktop_readonly_host.exe') -Force
        $state.native_host_sha256 = (Get-FileHash -LiteralPath $nativeExe -Algorithm SHA256).Hash.ToLowerInvariant()
        $state.native_host_bytes = (Get-Item -LiteralPath $nativeExe).Length
        Record-Gate 'native-readonly-host' 'PASS' 'Native Windows x64 PE bundled adjacent to game; OS runtime test still required'
        $nativePackaged = $true
    } else {
        Record-Gate 'native-readonly-host' 'SKIP' 'No host binary; F7 reports native observer unavailable'
    }


    # FP3D-02F: the authoritative FPS writer is an OWNED process, not a PCK script.
    # Never claim native movement works if this PE has not been bundled.
    $playerExe = Join-Path $rootDir 'native\galaxy_desktop_player_host.exe'
    if (Test-Path -LiteralPath $playerExe) {
        Verify-PE64 $playerExe
        New-Item -ItemType Directory -Force -Path $stagedNative | Out-Null
        Copy-Item -LiteralPath $playerExe -Destination (Join-Path $stagedNative 'galaxy_desktop_player_host.exe') -Force
        $state.native_player_sha256 = (Get-FileHash -LiteralPath $playerExe -Algorithm SHA256).Hash.ToLowerInvariant()
        $state.native_player_bytes = (Get-Item -LiteralPath $playerExe).Length
        Record-Gate 'native-player-host' 'PASS' 'Authoritative C++ SQLite FPS child bundled; physical Windows F8 acceptance still required'
        $nativePackaged = $true
    } else {
        Record-Gate 'native-player-host' 'SKIP' 'Native FPS host absent; F8 clearly reports missing host and keeps local preview mode'
    }

    $state.stage = 'license'
    $licensePath = Join-Path $buildDir 'GODOT_LICENSE.txt'
    if (-not (Test-Path -LiteralPath $licensePath)) {
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/godotengine/godot/$tag/LICENSE.txt" -UseBasicParsing -TimeoutSec 45 -OutFile $licensePath
    }
    if ((Get-Item $licensePath).Length -lt 400) {throw 'Godot MIT license notice is missing/too short'}
    $readme = Join-Path $buildDir 'READ_FIRST.txt'
    @'
Single-Galaxy standalone graybox (FP3D-02A development preview)
Double-click SingleGalaxy_Graybox.exe. No Godot editor or Python install required.
WASD=walk/mouse look; F8=authoritative persistent native FPS inside original owned ship; F7=read-only contact diagnostic; Esc=release mouse.
F8 optionally sends bounded WASD moves to an adjacent native C++ host. Native SQLite atomically commits world STEP, player pose and movement receipt. F7 remains read-only. Native FPS interior collisions are a prototype and do not yet match all Godot scene walls.
Actual physical Windows GPU, collision and usability acceptance is still required.
Godot MIT terms: GODOT_LICENSE.txt
'@ | Set-Content -LiteralPath $readme -Encoding UTF8
    $state.status='BUILT_NOT_HARDWARE_ACCEPTED'
    $state.stage='complete'
    $exitCode=0
} catch {
    $state.error = [string]$_
    Write-Host "BUILD FAILED: $($state.error)" -ForegroundColor Red
} finally {
    $state.completed_utc = [DateTime]::UtcNow.ToString('o')
    $evidenceJson = Join-Path $logsDir 'build-result.json'
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $evidenceJson -Encoding UTF8
    $evidenceZip = Join-Path $rootDir 'SingleGalaxy_Windows_Build_Evidence.zip'
    if (Test-Path $evidenceZip) {Remove-Item $evidenceZip -Force}
    Compress-Archive -Path (Join-Path $logsDir '*') -DestinationPath $evidenceZip -Force
    Write-Host "Build evidence: $evidenceZip"
    if ($exitCode -eq 0) {
        $deliverable = Join-Path $rootDir 'SingleGalaxy_Graybox_Windows_x64.zip'
        if (Test-Path $deliverable) {Remove-Item $deliverable -Force}
        $packagePaths = @((Join-Path $buildDir 'SingleGalaxy_Graybox.exe'),(Join-Path $buildDir 'GODOT_LICENSE.txt'),(Join-Path $buildDir 'READ_FIRST.txt'))
        if ($nativePackaged) {$packagePaths += $stagedNative}
        Compress-Archive -LiteralPath $packagePaths -DestinationPath $deliverable -Force
        Write-Host "Standalone export: $deliverable"
        if ($LaunchAfterExport) {Start-Process -FilePath (Join-Path $buildDir 'SingleGalaxy_Graybox.exe')}
    }
}
exit $exitCode
