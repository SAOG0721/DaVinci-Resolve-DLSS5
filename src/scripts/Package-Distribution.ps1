[CmdletBinding()]
param([switch]$Validation)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$packageRoot=Join-Path $projectRoot 'dist\0.4.0'
$manifest=Get-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Raw | ConvertFrom-Json
$plugin=Join-Path $packageRoot 'ResolveDlss5.ofx.bundle\Contents\Win64\ResolveDlss5.ofx'
$buildPlugin=Join-Path $projectRoot 'build\Release\ResolveDlss5.ofx'
if($manifest.Version -ne '0.4.0' -or $manifest.ReleaseReady -or $manifest.RuntimeIncluded){throw 'Unexpected development package manifest'}
if($manifest.DeploymentAllowed -ne $true -and !($Validation -and $manifest.ValidationAllowed -eq $true)){
    throw ('Distribution is blocked: '+$manifest.BlockedReason)
}
foreach($binaryPath in @($plugin,$buildPlugin)){
    if((Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash -ne $manifest.PluginSHA256){throw 'Package does not match the tested build binary'}
}
[xml]$testReport=Get-Content -LiteralPath (Join-Path $projectRoot 'tests\results\ctest-release.xml') -Raw
$expectedTests=@('ResolveDlss5.Feature18RuntimeSmoke','ResolveDlss5.Core','ResolveDlss5.TimelineRuntimeSmoke','ResolveDlss5.GpuContracts')
if([int]$manifest.PackageRevision -ge 4){$expectedTests+='ResolveDlss5.OpticalFlow'}
if([int]$testReport.testsuite.tests -ne $expectedTests.Count -or $testReport.testsuite.failures -ne '0' -or $testReport.testsuite.skipped -ne '0'){throw 'All successful production contract tests are required'}
foreach($testName in $expectedTests){
    if(!($testReport.testsuite.testcase | Where-Object {$_.name -eq $testName -and $_.status -eq 'run'})){throw "Missing passed test: $testName"}
}
$zipName='ResolveDLSS5-0.4.0-win64.zip'
$zipPath=Join-Path $projectRoot ('dist\'+$zipName)
if(Test-Path -LiteralPath $zipPath){throw 'Distribution ZIP already exists; preserve or move it before repackaging'}
foreach($scriptName in @('Install-Development.ps1','Restore-Development.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $scriptName) -Destination (Join-Path $packageRoot $scriptName) -Force
}
if($Validation){
    $reportPath=Join-Path $projectRoot 'tests\results\ctest-release.xml'
    $evidenceRoot=Join-Path $projectRoot ('tests\results\'+$manifest.EvidenceSubdirectory)
    $hostEvidence=Get-Content -LiteralPath (Join-Path $evidenceRoot 'host-evidence.json') -Raw | ConvertFrom-Json
    if($hostEvidence.PluginSHA256 -ne $manifest.PluginSHA256){throw 'Native validation evidence belongs to a different binary'}
    $parameterPath=Join-Path $evidenceRoot 'parameters.json'
    if((Get-FileHash -LiteralPath $parameterPath).Hash -ne $hostEvidence.ParameterReportSHA256){throw 'Native parameter report differs from validation evidence'}
    Copy-Item -LiteralPath $reportPath -Destination (Join-Path $packageRoot 'contract-tests.xml') -Force
    Copy-Item -LiteralPath $parameterPath -Destination (Join-Path $packageRoot 'parameter-check.json') -Force
    $manualPath=Join-Path $projectRoot 'docs\release\r5\MANUAL-ACCEPTANCE.md'
    if(!(Test-Path -LiteralPath $manualPath)){$manualPath=Join-Path $projectRoot 'tests\MANUAL-ACCEPTANCE.md'}
    Copy-Item -LiteralPath $manualPath -Destination (Join-Path $packageRoot 'MANUAL-ACCEPTANCE.md') -Force
    [ordered]@{ValidationOnly=$true;PluginSHA256=$manifest.PluginSHA256;
        ContractReportSHA256=(Get-FileHash -LiteralPath $reportPath).Hash;
        ParameterReportSHA256=$hostEvidence.ParameterReportSHA256;
        NativeParameterRoundTripPassed=$hostEvidence.NativeParameterRoundTripPassed;
        ParameterProofKind=$hostEvidence.ParameterProofKind;
        CompactLabelsSourceAndBinaryChecked=$hostEvidence.CompactLabelsSourceAndBinaryChecked;
        NativePreviewRenderObserved=$hostEvidence.NativePreviewRenderObserved;
        NativeTimelineNRExportAccepted=$hostEvidence.NativeTimelineNRExportAccepted;
        Pending='Manual timeline/export acceptance; sequential preparation cache and full 0.4 features remain incomplete'} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageRoot 'validation-evidence.json') -Encoding UTF8
}
$hashList=Join-Path $packageRoot 'SHA256SUMS.txt'
$files=@(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Where-Object {$_.FullName -ne $hashList} | Sort-Object FullName)
foreach($file in $files){
    if($file.Name -ne 'LICENSE' -and $file.Extension -notin @('.ofx','.md','.txt','.json','.ps1','.xml')){throw "Unexpected delivery file: $($file.Name)"}
    if($file.Name -eq 'nvngx_dlssnr.dll'){throw 'Community runtime is not redistributable in this package'}
}
$hashes=@($files | ForEach-Object {
    $relative=[IO.Path]::GetRelativePath($packageRoot,$_.FullName).Replace('\','/')
    [ordered]@{Path=$relative;SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash;Bytes=$_.Length}
})
$hashes | ForEach-Object {"$($_.SHA256)  $($_.Path)"} | Set-Content -LiteralPath $hashList -Encoding utf8
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($packageRoot,$zipPath,[IO.Compression.CompressionLevel]::Optimal,$true)
$archive=[IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $payloadEntries=@($archive.Entries | Where-Object {$_.Name})
    if($payloadEntries.Count -ne $files.Count+1){throw 'Unexpected ZIP payload count'}
    $verificationItems=@($hashes)+@([ordered]@{Path='SHA256SUMS.txt';SHA256=(Get-FileHash -LiteralPath $hashList -Algorithm SHA256).Hash;Bytes=(Get-Item -LiteralPath $hashList).Length})
    foreach($item in $verificationItems){
        $entry=$archive.GetEntry('0.4.0/'+$item.Path)
        if(!$entry -or $entry.Length -ne $item.Bytes){throw "Missing or truncated ZIP entry: $($item.Path)"}
        $stream=$entry.Open()
        $sha=[Security.Cryptography.SHA256]::Create()
        try {$actual=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}
        finally {$sha.Dispose();$stream.Dispose()}
        if($actual -ne $item.SHA256){throw "ZIP content hash mismatch: $($item.Path)"}
    }
} finally {$archive.Dispose()}
$zipHash=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
"$zipHash  $([IO.Path]::GetFileName($zipPath))" | Set-Content -LiteralPath ($zipPath+'.sha256.txt') -Encoding utf8
[ordered]@{
    Version=$manifest.Version;PackageRevision=$manifest.PackageRevision;ValidationOnly=[bool]$Validation;
    ReleaseReady=$false;RuntimeIncluded=$false;TestsPassed=$expectedTests.Count;
    Archive=[IO.Path]::GetFileName($zipPath);ArchiveSHA256=$zipHash;ArchiveBytes=(Get-Item -LiteralPath $zipPath).Length;
    PayloadCount=$files.Count+1;ZipContentVerified=$true;Payload=$hashes
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $projectRoot 'dist\distribution-audit.json') -Encoding utf8
Write-Output "Verified development distribution: $zipPath"
Write-Output "SHA256: $zipHash"
