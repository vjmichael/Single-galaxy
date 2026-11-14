# FP3D-02B independent Windows host process and SQLite restart check.
# Runs a narrow *read-only* native simulation child with its own temporary save.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$hostExe = Join-Path $root 'native\galaxy_desktop_readonly_host.exe'
$logDir = Join-Path $root 'native-evidence'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$save = Join-Path $logDir ("test-world-{0}.sqlite" -f [Guid]::NewGuid().ToString('N'))
$status = [ordered]@{status='FAILED'; reason='not run'; first_ticks=$null; restart_ticks=$null; native_sha256=$null; native_bytes=$null; time_utc=[DateTime]::UtcNow.ToString('o')}
function Run-Native([string]$label,[string]$numberOfTicks) {
    $out = Join-Path $logDir "$label.sgdesk"
    $err = Join-Path $logDir "$label.stderr.txt"
    & $hostExe $save $numberOfTicks 1> $out 2> $err
    if ($LASTEXITCODE -ne 0) {throw "Native child $label exited $LASTEXITCODE; $err"}
    return @(Get-Content -LiteralPath $out | Where-Object {$_ -match '^SGDESK\|1\|'})
}
function Header-Ticks([object[]]$lines) {
    return @($lines | ForEach-Object {
        $p=([string]$_).Split('|')
        if($p.Length -ne 13 -or $p[7] -ne '7' -or [int64]$p[12] -gt 64 -or [int64]$p[12] -lt 1){throw 'Invalid SGDESK header or unauthorized ship'}
        [int64]$p[4]
    })
}
try {
    if(-not (Test-Path -LiteralPath $hostExe)) {throw 'No native Windows host adjacent to Godot project'}
    $bytes=(Get-Item -LiteralPath $hostExe).Length
    if($bytes -lt 1000000){throw 'Native host binary unexpectedly small'}
    $status.native_sha256=(Get-FileHash -LiteralPath $hostExe -Algorithm SHA256).Hash.ToLowerInvariant()
    $status.native_bytes=$bytes
    $a = @(Run-Native 'first' '3')
    $b = @(Run-Native 'restart' '2')
    $ta = @(Header-Ticks $a); $tb = @(Header-Ticks $b)
    if($a.Count -ne 4 -or $b.Count -ne 3 -or (($ta -join ',') -ne '0,1,2,3') -or (($tb -join ',') -ne '3,4,5')){
        throw "SQLite restart continuity mismatch: $($ta -join ',') | $($tb -join ',')"
    }
    $streamA = $a[0].Split('|')[2]; $streamB = $b[0].Split('|')[2]
    if($streamA -eq $streamB){throw 'New native child must have fresh stream epoch'}
    $status.status='PASS'
    $status.reason='Real Windows native process and SQLite restart verified, Godot F7 still separate'
    $status.first_ticks=$ta
    $status.restart_ticks=$tb
    Write-Host 'Native Windows process + SQLite restart PASS. Test Godot F7 separately.' -ForegroundColor Green
} catch {
    $status.reason=[string]$_
    Write-Host "Native Windows test FAILED: $($status.reason)" -ForegroundColor Red
} finally {
    $status | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logDir 'summary.json') -Encoding UTF8
    $evidence=Join-Path $root 'SingleGalaxy_NativeBridge_Windows_Evidence.zip'
    if(Test-Path $evidence){Remove-Item $evidence -Force}
    Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $logDir -File | Select-Object -ExpandProperty FullName) -DestinationPath $evidence -Force
    Write-Host "Evidence: $evidence"
}
if($status.status -ne 'PASS'){exit 1}
exit 0
