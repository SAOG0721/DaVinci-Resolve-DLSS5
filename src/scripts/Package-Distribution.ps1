[CmdletBinding()]
param(
    [switch]$Validation,
    [string]$RuntimeDll='C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle\Contents\Win64\runtime\nvngx_dlssnr.dll'
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$candidateRoot=Join-Path $projectRoot 'dist\0.4.0'
$manifest=Get-Content -LiteralPath (Join-Path $candidateRoot 'manifest.json') -Raw | ConvertFrom-Json
$bundleName='ResolveDlss5.ofx.bundle'
$pluginRelative='Contents\Win64\ResolveDlss5.ofx'
$plugin=Join-Path (Join-Path $candidateRoot $bundleName) $pluginRelative
$buildPlugin=Join-Path $projectRoot 'build\Release\ResolveDlss5.ofx'
if($manifest.Version -ne '0.4.0' -or $manifest.ReleaseReady -or $manifest.RuntimeIncluded){throw 'Unexpected development package manifest'}
if($manifest.DeploymentAllowed -ne $true -and !($Validation -and $manifest.ValidationAllowed -eq $true)){
    throw ('Distribution is blocked: '+$manifest.BlockedReason)
}
foreach($binaryPath in @($plugin,$buildPlugin)){
    if((Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash -ne $manifest.PluginSHA256){throw 'Package does not match the tested build binary'}
}
$reportPath=Join-Path $projectRoot 'tests\results\ctest-release.xml'
[xml]$testReport=Get-Content -LiteralPath $reportPath -Raw
$expectedTests=@('ResolveDlss5.Feature18RuntimeSmoke','ResolveDlss5.Core','ResolveDlss5.TimelineRuntimeSmoke','ResolveDlss5.GpuContracts')
if([int]$manifest.PackageRevision -ge 4){$expectedTests+='ResolveDlss5.OpticalFlow'}
if([int]$testReport.testsuite.tests -ne $expectedTests.Count -or $testReport.testsuite.failures -ne '0' -or $testReport.testsuite.skipped -ne '0'){
    throw 'All successful production contract tests are required'
}
foreach($name in $expectedTests){
    if(@($testReport.testsuite.testcase | Where-Object {$_.name -eq $name -and $_.status -eq 'run'}).Count -ne 1){
        throw ('Missing passed test: '+$name)
    }
}
$evidenceRoot=Join-Path $projectRoot ('tests\results\'+$manifest.EvidenceSubdirectory)
$evidence=Get-Content -LiteralPath (Join-Path $evidenceRoot 'host-evidence.json') -Raw | ConvertFrom-Json
$parameterPath=Join-Path $evidenceRoot 'parameters.json'
if($evidence.PluginSHA256 -ne $manifest.PluginSHA256 -or
   (Get-FileHash -LiteralPath $parameterPath).Hash -ne $evidence.ParameterReportSHA256){throw 'Validation evidence differs from this binary'}
$zipName='ResolveDLSS5-0.4.0-win64.zip'
$zipPath=Join-Path $projectRoot ('dist\'+$zipName)
if(Test-Path -LiteralPath $zipPath){throw 'Distribution ZIP already exists; preserve it before repackaging'}
if(!(Test-Path -LiteralPath $RuntimeDll -PathType Leaf)){throw 'Provide the authorized community runtime DLL to include in the binary package'}
# Always use a fresh staging directory: candidate/debug documents stay outside the ZIP.
$stageBase=Join-Path $projectRoot ('build\minimal-distribution-'+[guid]::NewGuid().ToString('N'))
$packageRoot=Join-Path $stageBase '0.4.0'
New-Item -ItemType Directory -Path $packageRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $candidateRoot $bundleName) -Destination $packageRoot -Recurse
$runtimeDirectory=Join-Path $packageRoot ($bundleName+'\Contents\Win64\runtime')
New-Item -ItemType Directory -Path $runtimeDirectory -Force | Out-Null
Copy-Item -LiteralPath $RuntimeDll -Destination (Join-Path $runtimeDirectory 'nvngx_dlssnr.dll') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\THIRD_PARTY_NOTICES.md') -Destination (Join-Path $packageRoot ($bundleName+'\Contents\THIRD_PARTY_NOTICES.md')) -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-Development.ps1') -Destination (Join-Path $packageRoot 'Install.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Restore-Development.ps1') -Destination (Join-Path $packageRoot 'Restore.ps1')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\RELEASE_INSTALL.md') -Destination (Join-Path $packageRoot 'INSTALL.md')
$summary=[ordered]@{
    PluginSHA256=$manifest.PluginSHA256;
    ContractReportSHA256=(Get-FileHash -LiteralPath $reportPath).Hash;
    Tests=@($expectedTests | ForEach-Object {[ordered]@{Name=$_;Passed=$true}});
    ParameterReportSHA256=$evidence.ParameterReportSHA256;
    ParameterProofKind=$evidence.ParameterProofKind;
    NativeParameterRoundTripPassed=[bool]$evidence.NativeParameterRoundTripPassed;
    NativePreviewRenderObserved=[bool]$evidence.NativePreviewRenderObserved;
    NativeTimelineNRExportAccepted=[bool]$evidence.NativeTimelineNRExportAccepted
}
$metadata=[ordered]@{
    InstallerFormat=1;Version=$manifest.Version;PackageRevision=$manifest.PackageRevision;
    ReleaseReady=$false;DeploymentAllowed=[bool]$manifest.DeploymentAllowed;
    ValidationAllowed=[bool]$manifest.ValidationAllowed;RuntimeIncluded=$true;
    PluginRelativePath=$pluginRelative;PluginSHA256=$manifest.PluginSHA256;
    TestsPassed=$expectedTests.Count;
    BlockedReason=$manifest.BlockedReason;ValidationSummary=$summary
}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $packageRoot ($bundleName+'\Contents\package.json')) -Encoding utf8
$roots=@(Get-ChildItem -LiteralPath $packageRoot | Select-Object -ExpandProperty Name | Sort-Object)
$expectedRoots=@('INSTALL.md','Install.ps1',$bundleName,'Restore.ps1') | Sort-Object
if(($roots -join '|') -ne ($expectedRoots -join '|')){throw 'Unexpected top-level package content'}
$files=@(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName)
$hashes=@($files | ForEach-Object {
    $relativePath=[IO.Path]::GetRelativePath($packageRoot,$_.FullName).Replace('\','/')
    if($_.Extension -eq '.dll'){
        if($relativePath -ne ($bundleName+'/Contents/Win64/runtime/nvngx_dlssnr.dll')){throw ('Unexpected DLL: '+$relativePath)}
    }elseif($_.Name -ne 'LICENSE' -and $_.Extension -notin @('.ofx','.md','.txt','.json','.ps1')){throw ('Unexpected delivery file: '+$_.Name)}
    [ordered]@{Path=$relativePath;
        SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash;Bytes=$_.Length}
})
if(@($files | Where-Object {$_.Extension -eq '.ofx'}).Count -ne 1){throw 'The package must contain exactly one plugin'}
if(@($files | Where-Object {$_.Extension -eq '.dll'}).Count -ne 1){throw 'The package must contain exactly one DLSSNR runtime DLL'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($packageRoot,$zipPath,[IO.Compression.CompressionLevel]::Optimal,$true)
$archive=[IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if(@($archive.Entries | Where-Object {$_.Name}).Count -ne $hashes.Count){throw 'Unexpected ZIP payload count'}
    foreach($item in $hashes){
        $entry=$archive.GetEntry('0.4.0/'+$item.Path)
        if(!$entry -or $entry.Length -ne $item.Bytes){throw ('Missing ZIP entry: '+$item.Path)}
        $stream=$entry.Open();$algorithm=[Security.Cryptography.SHA256]::Create()
        try {$actual=[BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','')}
        finally {$stream.Dispose();$algorithm.Dispose()}
        if($actual -ne $item.SHA256){throw ('ZIP hash differs: '+$item.Path)}
    }
} finally {$archive.Dispose()}
$zipHash=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
"$zipHash  $zipName" | Set-Content -LiteralPath ($zipPath+'.sha256.txt') -Encoding utf8
[ordered]@{
    Version=$manifest.Version;PackageRevision=$manifest.PackageRevision;Layout='Minimal';
    ValidationOnly=[bool]$Validation;ReleaseReady=$false;RuntimeIncluded=$true;TestsPassed=$expectedTests.Count;
    Archive=$zipName;ArchiveSHA256=$zipHash;ArchiveBytes=(Get-Item -LiteralPath $zipPath).Length;
    TopLevelEntries=$roots;PayloadCount=$hashes.Count;ZipContentVerified=$true;Payload=$hashes
} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $projectRoot 'dist\distribution-audit.json') -Encoding utf8
Write-Output ('Verified minimal distribution: '+$zipPath)
Write-Output ('SHA256: '+$zipHash)
