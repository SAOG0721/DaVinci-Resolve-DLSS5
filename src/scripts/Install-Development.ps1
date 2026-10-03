[CmdletBinding()]
param(
    [string]$PackageRoot = '',
    [string]$RuntimeDll = '',
    [switch]$Apply,
    [switch]$Validation
)
$ErrorActionPreference='Stop'
# Portable packages keep installer metadata inside the bundle. Source-tree
# candidates retain manifest.json. Neither mode writes without -Apply.
if(!$PackageRoot){
    $PackageRoot=if((Test-Path -LiteralPath (Join-Path $PSScriptRoot 'manifest.json')) -or
        (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'ResolveDlss5.ofx.bundle\Contents\package.json'))){
        $PSScriptRoot
    }else{[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\dist\0.4.0'))}
}
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)
$targetRoot='C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle'
$backupRoot=Join-Path $env:LOCALAPPDATA 'ResolveDlss5\Backups'
if([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($backupRoot)) -ne [IO.Path]::GetPathRoot($targetRoot)){throw 'Atomic bundle replacement requires a backup directory on the target volume'}
$manifestPath=Join-Path $PackageRoot 'manifest.json'
if(!(Test-Path -LiteralPath $manifestPath)){
    $manifestPath=Join-Path $PackageRoot 'ResolveDlss5.ofx.bundle\Contents\package.json'
}
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$sourceBundle=Join-Path $PackageRoot 'ResolveDlss5.ofx.bundle'
if(!$RuntimeDll){
    $RuntimeDll=if($manifest.RuntimeIncluded){
        Join-Path $sourceBundle 'Contents\Win64\runtime\nvngx_dlssnr.dll'
    }else{Join-Path $targetRoot 'Contents\Win64\runtime\nvngx_dlssnr.dll'}
}
$sourcePlugin=Join-Path $sourceBundle 'Contents\Win64\ResolveDlss5.ofx'
if((Get-FileHash -LiteralPath $sourcePlugin -Algorithm SHA256).Hash -ne $manifest.PluginSHA256){throw 'Candidate plugin hash mismatch'}
if(!(Test-Path -LiteralPath $RuntimeDll -PathType Leaf)){throw 'Runtime DLL file was not found'}
# Use the selected runtime; its hash checks copying and the restore receipt only.
$runtimeHash=(Get-FileHash -LiteralPath $RuntimeDll -Algorithm SHA256).Hash
if($manifest.Version -notin @('0.4.0','0.4.0-dev-s0')){throw 'Unsupported development manifest'}
$compactPackage=($manifest.InstallerFormat -eq 1)
if($compactPackage){
    $summary=$manifest.ValidationSummary
    $requiredTests=@('ResolveDlss5.Feature18RuntimeSmoke','ResolveDlss5.Core',
        'ResolveDlss5.TimelineRuntimeSmoke','ResolveDlss5.GpuContracts')
    if([int]$manifest.PackageRevision -ge 4){$requiredTests+='ResolveDlss5.OpticalFlow'}
    if(!$summary -or $summary.PluginSHA256 -ne $manifest.PluginSHA256 -or
        $summary.ContractReportSHA256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        [int]$manifest.TestsPassed -ne $requiredTests.Count -or
        @($summary.Tests).Count -ne $requiredTests.Count){
        throw 'Invalid compact validation summary'
    }
    foreach($name in $requiredTests){
        if(@($summary.Tests | Where-Object {$_.Name -eq $name -and $_.Passed -eq $true}).Count -ne 1){
            throw ('Compact package requires a passed test: '+$name)
        }
    }
}
if(!$Apply){
    [ordered]@{Mode='PreviewOnly';Version=$manifest.Version;Target=$targetRoot;BackupRoot=$backupRoot;
        PluginSHA256=$manifest.PluginSHA256;RuntimeSource=$RuntimeDll;RuntimeSHA256=$runtimeHash;
        DeploymentAllowed=($manifest.DeploymentAllowed -eq $true);BlockedReason=$manifest.BlockedReason;
        ValidationOnly=[bool]$Validation;ValidationAllowed=($manifest.ValidationAllowed -eq $true);
        ResolveRunning=[bool](Get-Process Resolve -ErrorAction SilentlyContinue);WritesPerformed=$false} | ConvertTo-Json
    return
}
if($manifest.DeploymentAllowed -ne $true -and !($Validation -and $manifest.ValidationAllowed -eq $true)){
    throw ('Candidate deployment is blocked: '+$manifest.BlockedReason)
}
if($Validation -and !$compactPackage){
    $portableEvidence=Join-Path $PackageRoot 'validation-evidence.json'
    if(Test-Path -LiteralPath $portableEvidence){
        $evidence=Get-Content -LiteralPath $portableEvidence -Raw | ConvertFrom-Json
        $reportPath=Join-Path $PackageRoot 'contract-tests.xml'
        if($evidence.PluginSHA256 -ne $manifest.PluginSHA256 -or
            (Get-FileHash -LiteralPath $reportPath).Hash -ne $evidence.ContractReportSHA256){throw 'Validation evidence mismatch'}
    }else{
        $reportPath=Join-Path $PSScriptRoot '..\..\tests\results\ctest-release.xml'
        $testedBinary=Join-Path $PSScriptRoot '..\..\build\Release\ResolveDlss5.ofx'
        if((Get-FileHash -LiteralPath $testedBinary).Hash -ne $manifest.PluginSHA256){throw 'Validation candidate differs from tested build'}
    }
    [xml]$report=Get-Content -LiteralPath $reportPath -Raw
    if([int]$report.testsuite.tests -ne [int]$manifest.TestsPassed -or $report.testsuite.failures -ne '0' -or $report.testsuite.skipped -ne '0'){throw 'Validation installation requires all manifest contract tests'}
}
if(Get-Process Resolve -ErrorAction SilentlyContinue){throw 'Close Resolve before applying this private OFX bundle update'}
# Only this explicitly named bundle is moved. No database/cache/application
# directories are touched, and there are no recursive deletion operations.
if([IO.Path]::GetFullPath($targetRoot) -ne 'C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle'){throw 'Unexpected install target'}
$stamp=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N')
$transactionRoot=Join-Path $backupRoot $stamp
$stage=Join-Path $transactionRoot 'staged\ResolveDlss5.ofx.bundle'
$backup=Join-Path $transactionRoot 'original\ResolveDlss5.ofx.bundle'
$receipt=Join-Path $transactionRoot 'receipt.json'
New-Item -ItemType Directory -Path (Split-Path -Parent $stage),(Split-Path -Parent $backup) -Force | Out-Null
Copy-Item -LiteralPath $sourceBundle -Destination $stage -Recurse
$stageRuntime=Join-Path $stage 'Contents\Win64\runtime'
New-Item -ItemType Directory -Path $stageRuntime -Force | Out-Null
Copy-Item -LiteralPath $RuntimeDll -Destination (Join-Path $stageRuntime 'nvngx_dlssnr.dll')
if((Get-FileHash -LiteralPath (Join-Path $stage 'Contents\Win64\ResolveDlss5.ofx') -Algorithm SHA256).Hash -ne $manifest.PluginSHA256){throw 'Staged plugin verification failed'}
if((Get-FileHash -LiteralPath (Join-Path $stageRuntime 'nvngx_dlssnr.dll') -Algorithm SHA256).Hash -ne $runtimeHash){throw 'Staged runtime verification failed'}
$hadOriginal=Test-Path -LiteralPath $targetRoot
$originalMoved=$false
$installedMoved=$false
try {
    if($hadOriginal){Move-Item -LiteralPath $targetRoot -Destination $backup;$originalMoved=$true}
    Move-Item -LiteralPath $stage -Destination $targetRoot
    $installedMoved=$true
    if((Get-FileHash -LiteralPath (Join-Path $targetRoot 'Contents\Win64\ResolveDlss5.ofx') -Algorithm SHA256).Hash -ne $manifest.PluginSHA256){throw 'Installed plugin verification failed'}
    if((Get-FileHash -LiteralPath (Join-Path $targetRoot 'Contents\Win64\runtime\nvngx_dlssnr.dll') -Algorithm SHA256).Hash -ne $runtimeHash){throw 'Installed runtime verification failed'}
    [ordered]@{Version=$manifest.Version;Target=$targetRoot;Backup=$backup;HadOriginal=$hadOriginal;
        PluginSHA256=$manifest.PluginSHA256;RuntimeSHA256=$runtimeHash;
        InstalledAt=(Get-Date).ToString('o');ValidationOnly=[bool]$Validation} | ConvertTo-Json | Set-Content -LiteralPath $receipt -Encoding utf8
} catch {
    if($installedMoved -and (Test-Path -LiteralPath $targetRoot)){Move-Item -LiteralPath $targetRoot -Destination (Join-Path $transactionRoot 'failed-install.bundle')}
    if($originalMoved){Move-Item -LiteralPath $backup -Destination $targetRoot}
    throw
}
Write-Output "Installed private development bundle. Restore receipt: $receipt"
