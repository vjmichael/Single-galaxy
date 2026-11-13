# NO NSIS install, GPU workload, repo merge or automatic service provisioning.
# No SCM mutation unless creator explicitly supplies -ExecuteOneUse.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceSha,
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [switch]$ExecuteOneUse,
    [switch]$HostedSmoke,
    [string]$EvidenceRoot = (Join-Path $env:ProgramData 'SingleGalaxy\S6GKernelGate')
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$serviceName='SingleGalaxyS6GKernelGate'
if($SourceSha -cnotmatch '^[0-9a-f]{40}$') { throw 'An exact lowercase 40-digit source SHA is required' }
$gitSha=(& git rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $gitSha -cne $SourceSha){throw 'Checked-out source differs from authorized source SHA'}
if((& git status --porcelain | Out-String).Trim()) {throw 'Dirty source checkout is not eligible for physical SCM evidence'}
$base=[IO.Path]::GetFullPath($BuildDir)
$serviceExe=Join-Path $base 'Release\galaxy_stage6g_windows_scm_gate.exe'
$childExe=Join-Path $base 'Release\galaxy_server_dx_fault_child.exe'
foreach($file in @($serviceExe,$childExe)) {
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing compiled MSVC x64 binary: $file"}
}
if(-not $ExecuteOneUse){
    Write-Host 'PREFLIGHT ONLY: no service created, no physical test executed.'
    Write-Host "Commit: $SourceSha"
    Write-Host "Service: $serviceName"
    Write-Host 'To authorize ONE disposable physical SCM service acceptance, rerun with -ExecuteOneUse.'
    exit 0
}
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=[Security.Principal.WindowsPrincipal]::new($identity)
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    throw 'An elevated Windows console is required for one-use SCM acceptance'
}
if(Get-Service -Name $serviceName -ErrorAction SilentlyContinue){
    throw 'Existing service detected; never change or overwrite any pre-existing SCM service'
}
$folder=Join-Path $EvidenceRoot $SourceSha
if(Test-Path -LiteralPath $folder){
    throw 'One-use evidence directory already exists; refuse authorization-token replay'
}
New-Item -ItemType Directory -Path $folder -Force | Out-Null
# LocalService can write only these private original receipts; no Everyone/Users ACL.
& icacls.exe $folder /inheritance:r /grant:r `
  'NT AUTHORITY\LOCAL SERVICE:(OI)(CI)M' `
  'NT AUTHORITY\SYSTEM:(OI)(CI)F' `
  'BUILTIN\Administrators:(OI)(CI)F' | Out-Null
if($LASTEXITCODE -ne 0){throw 'Could not install restricted SCM evidence ACL'}
# A LocalService process cannot be assumed to traverse the owner's checkout.
# Stage EXACT compiled executables inside a private ACL-protected directory;
# the service user gets RX only for binaries, not mutation authority.
$binFolder=Join-Path $folder 'bin'
New-Item -ItemType Directory -Path $binFolder | Out-Null
& icacls.exe $binFolder /inheritance:r /grant:r `
  'NT AUTHORITY\LOCAL SERVICE:(OI)(CI)RX' `
  'NT AUTHORITY\SYSTEM:(OI)(CI)F' `
  'BUILTIN\Administrators:(OI)(CI)F' | Out-Null
if($LASTEXITCODE -ne 0){throw 'Could not protect staged SCM executables'}
$stagedService=Join-Path $binFolder (Split-Path $serviceExe -Leaf)
$stagedChild=Join-Path $binFolder (Split-Path $childExe -Leaf)
Copy-Item -LiteralPath $serviceExe -Destination $stagedService -ErrorAction Stop
Copy-Item -LiteralPath $childExe -Destination $stagedChild -ErrorAction Stop
foreach($pair in @(@($serviceExe,$stagedService),@($childExe,$stagedChild))){
    if((Get-FileHash -LiteralPath $pair[0] -Algorithm SHA256).Hash -cne
       (Get-FileHash -LiteralPath $pair[1] -Algorithm SHA256).Hash){
        throw 'Staged SCM executable differs from exact compiled source'
    }
}
$created=$false
$results=[System.Collections.Generic.List[object]]::new()
try {
    $binaryPath='"'+$stagedService+'" --service --fault-child "'+$stagedChild+'" --evidence "'+$folder+'" --sha '+$SourceSha
    New-Service -Name $serviceName -BinaryPathName $binaryPath -StartupType Manual | Out-Null
    $created=$true
    & sc.exe config $serviceName obj= 'NT AUTHORITY\LocalService' password= '' | Out-Null
    if($LASTEXITCODE -ne 0){throw 'SCM LocalService identity configuration refused'}
    for($cycle=1;$cycle -le 2;$cycle++){
        Start-Service -Name $serviceName
        $deadline=(Get-Date).AddSeconds(20)
        $startFile=Join-Path $folder 'job-start.json'
        $stopFile=Join-Path $folder 'job-stop.json'
        while(-not (Test-Path -LiteralPath $startFile) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 100}
        if(-not (Test-Path -LiteralPath $startFile)){throw "SCM cycle $cycle missing native Job start receipt"}
        $start=Get-Content -LiteralPath $startFile -Raw | ConvertFrom-Json
        if($start.source_sha -cne $SourceSha -or $start.scm_service -cne $serviceName -or
           $start.passed -ne $true -or $start.job_kill_on_close -ne $true -or
           $start.job_process_ids.Count -lt 2 -or $start.job_process_ids -notcontains $start.supervised_pid){
            throw "SCM cycle $cycle did not attest two genuine Job processes"
        }
        $running=Get-Service $serviceName
        if($running.Status -ne 'Running'){throw "SCM cycle $cycle did not reach RUNNING"}
        Stop-Service -Name $serviceName
        $deadline=(Get-Date).AddSeconds(20)
        while(-not (Test-Path -LiteralPath $stopFile) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 100}
        if(-not (Test-Path -LiteralPath $stopFile)){throw "SCM cycle $cycle missing Job termination receipt"}
        $stop=Get-Content -LiteralPath $stopFile -Raw | ConvertFrom-Json
        if($stop.source_sha -cne $SourceSha -or $stop.passed -ne $true -or
           $stop.verified_terminated -ne $true -or $stop.service_pid -ne $start.service_pid){
            throw "SCM cycle $cycle did not prove same-process Job termination"
        }
        foreach($childPid in $start.job_process_ids){
            if(Get-Process -Id ([int]$childPid) -ErrorAction SilentlyContinue){
                throw "SCM cycle $cycle retained worker/descendant PID $childPid"
            }
        }
        Copy-Item -LiteralPath $startFile -Destination (Join-Path $folder "job-start-$cycle.json")
        Copy-Item -LiteralPath $stopFile -Destination (Join-Path $folder "job-stop-$cycle.json")
        Remove-Item -LiteralPath @($startFile,$stopFile) -Force
        $results.Add([pscustomobject]@{cycle=$cycle;service_pid=$start.service_pid;
                    supervised_pid=$start.supervised_pid;job_process_ids=$start.job_process_ids;
                    kernel_job_closed=$true;all_original_pids_dead=$true})
    }
    if($results[0].service_pid -eq $results[1].service_pid){
        throw 'Service restart reused stale service process identity'
    }
    $serviceBinaryHash=(Get-FileHash -LiteralPath $serviceExe -Algorithm SHA256).Hash.ToLowerInvariant()
    $faultChildHash=(Get-FileHash -LiteralPath $childExe -Algorithm SHA256).Hash.ToLowerInvariant()
    $kind=if($HostedSmoke){'HOSTED'}else{'PHYSICAL'}
    $passedStatus=if($HostedSmoke){'hosted_SCM_Job_two_cycles_passed_only'}else{'physical_SCM_Job_two_cycles_passed'}
    $receipt=[pscustomobject]@{schema="SG-S6GK-$kind-WINDOWS-01";source_sha=$SourceSha;
              service_binary_sha256=$serviceBinaryHash;fault_child_sha256=$faultChildHash;
              os=[Environment]::OSVersion.VersionString;computer=$env:COMPUTERNAME;
              sc_name=$serviceName;status=$passedStatus;hosted_smoke_only=[bool]$HostedSmoke;
              physical_pc_verified=(-not [bool]$HostedSmoke);
              cycles=$results;installed_gameplay_tested=$false;radeon_tested=$false}
    $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $folder 'scm-acceptance.json') -Encoding UTF8
    Write-Host "PASS $passedStatus: two SCM lifetimes, native Job descendants and stop/restart; evidence: $folder"
}
finally {
    if($created){
        try {if((Get-Service $serviceName -ErrorAction SilentlyContinue).Status -ne 'Stopped'){
            Stop-Service -Name $serviceName -ErrorAction Stop}}catch{}
        & sc.exe delete $serviceName | Out-Null
        if($LASTEXITCODE -ne 0){Write-Warning "Could not delete disposable SCM test service $serviceName"}
    }
}
