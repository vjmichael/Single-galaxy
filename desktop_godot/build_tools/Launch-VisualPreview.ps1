# PLAY-STATION-02: isolated Godot-only scene/parser check for Windows.
# This DOES NOT simulate original ship boarding or access SQLite.
# It needs ONLY the already-installed Godot 4.7.2 editor; no compiler.
[CmdletBinding()]
param([string]$GodotExe="")
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logs=Join-Path $project 'godot-visual-logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$passed=$false
try {
  if([string]::IsNullOrWhiteSpace($GodotExe)){
    if($env:SINGLE_GALAXY_GODOT_EXE){
      $GodotExe=$env:SINGLE_GALAXY_GODOT_EXE
    } else {
      foreach($name in @('godot.exe','Godot.exe','Godot_v4.7.2-stable_win64.exe')){
        $found=Get-Command $name -ErrorAction SilentlyContinue
        if($found){$GodotExe=$found.Source;break}
      }
      if([string]::IsNullOrWhiteSpace($GodotExe)){
        Add-Type -AssemblyName System.Windows.Forms
        $picker=New-Object System.Windows.Forms.OpenFileDialog
        $picker.Title='Choose your EXISTING Godot 4.7.2 editor'
        $picker.Filter='Godot executable (*.exe)|*.exe'
        if($picker.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK){$GodotExe=$picker.FileName}
      }
    }
  }
  if(-not (Test-Path -LiteralPath $GodotExe -PathType Leaf)){
    throw 'Choose your existing Godot 4.7.2 editor executable. No download will be attempted.'
  }
  $ver=(& $GodotExe --version 2>&1 | Out-String).Trim()
  if($LASTEXITCODE -ne 0 -or $ver -notmatch '^4\.7\.2\.stable'){
    throw "Wrong Godot editor; expected 4.7.2.stable, received: $ver"
  }
  Write-Host "Using official Godot $ver"
  $commands=@(
    @('--headless','--path',('"' + $project + '"'),'--editor','--quit'),
    @('--headless','--path',('"' + $project + '"'),'--script','res://tests/godot_smoke.gd'),
    @('--headless','--path',('"' + $project + '"'),'--script','res://tests/station_visual_smoke.gd')
  )
  $idx=0
  foreach($arguments in $commands){
    $idx++
    $out=Join-Path $logs "smoke-$idx-stdout.txt"
    $err=Join-Path $logs "smoke-$idx-stderr.txt"
    $proc=Start-Process -FilePath $GodotExe -ArgumentList $arguments -NoNewWindow -PassThru -Wait -RedirectStandardOutput $out -RedirectStandardError $err
    $msg=Get-Content $err -Raw -ErrorAction SilentlyContinue | Out-String
    if($proc.ExitCode -ne 0 -or $msg -match '(?m)^(SCRIPT ERROR|ERROR:|Parser Error:)'){
      throw "Godot scene/parser smoke $idx failed: see $out and $err"
    }
  }
  Write-Host 'Godot scene/parser smoke passed. Starting VISUAL preview only.'
  Write-Host 'Only local geometry/camera are available. Real native boarding requires precompiled Windows hosts.'
  $game=Start-Process -FilePath $GodotExe -ArgumentList @('--path',('"' + $project + '"')) -PassThru -Wait
  Write-Host "Editor game closed: exit $($game.ExitCode)."
  $passed=($game.ExitCode -eq 0)
} catch {
  Write-Host "GODOT PREVIEW ERROR: $_" -ForegroundColor Red
} finally {
  $evidence=Join-Path $project 'SingleGalaxy_Godot_Visual_Only_Evidence.zip'
  if(Test-Path $evidence){Remove-Item -LiteralPath $evidence -Force}
  Compress-Archive -Path (Join-Path $logs '*') -DestinationPath $evidence -Force
  Write-Host "Visual smoke evidence: $evidence"
  if(-not $passed){exit 1}
}
