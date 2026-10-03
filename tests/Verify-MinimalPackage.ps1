[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [string]$RuntimeDll='C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle\Contents\Win64\runtime\nvngx_dlssnr.dll',
    [string]$Receipt=''
)
$ErrorActionPreference='Stop'
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)
$expected=@('INSTALL.md','Install.ps1','ResolveDlss5.ofx.bundle','Restore.ps1') | Sort-Object
$roots=@(Get-ChildItem -LiteralPath $PackageRoot | Select-Object -ExpandProperty Name | Sort-Object)
if(($roots -join '|') -ne ($expected -join '|')){throw 'Unexpected top-level package content'}
$metadataRelative='ResolveDlss5.ofx.bundle\Contents\package.json'
$metadataPath=Join-Path $PackageRoot $metadataRelative
$metadata=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
if($metadata.InstallerFormat -ne 1 -or $metadata.ReleaseReady -ne $false -or
    $metadata.DeploymentAllowed -ne $false -or $metadata.ValidationAllowed -ne $true){throw 'Unexpected installation gates'}
$target='C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle'
function Snapshot {
    $plugin=Join-Path $target 'Contents\Win64\ResolveDlss5.ofx'
    $runtime=Join-Path $target 'Contents\Win64\runtime\nvngx_dlssnr.dll'
    $backup=Join-Path $env:LOCALAPPDATA 'ResolveDlss5\Backups'
    [ordered]@{
        Plugin=if(Test-Path -LiteralPath $plugin){(Get-FileHash -LiteralPath $plugin).Hash}else{''};
        Runtime=if(Test-Path -LiteralPath $runtime){(Get-FileHash -LiteralPath $runtime).Hash}else{''};
        BackupCount=if(Test-Path -LiteralPath $backup){@(Get-ChildItem -LiteralPath $backup -Directory).Count}else{0}
    } | ConvertTo-Json -Compress
}
$before=Snapshot
$preview=(& (Join-Path $PackageRoot 'Install.ps1') -Validation -RuntimeDll $RuntimeDll | Out-String) | ConvertFrom-Json
if($preview.WritesPerformed -ne $false -or $preview.PluginSHA256 -ne $metadata.PluginSHA256){throw 'Installation preview differs'}
$temporaryRoot=Join-Path ([IO.Path]::GetTempPath()) ('ResolveDlss5-package-check-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $PackageRoot 'Install.ps1') -Destination $temporaryRoot
Copy-Item -LiteralPath (Join-Path $PackageRoot 'ResolveDlss5.ofx.bundle') -Destination $temporaryRoot -Recurse
$cases=@(
    @{Name='Failed test';Error='Compact package requires';Change={param($m) $m.ValidationSummary.Tests[0].Passed=$false}},
    @{Name='Missing test';Error='Invalid compact validation summary';Change={param($m) $m.ValidationSummary.Tests=@($m.ValidationSummary.Tests | Select-Object -Skip 1)}},
    @{Name='Duplicate test';Error='Compact package requires';Change={param($m) $m.ValidationSummary.Tests[1].Name=$m.ValidationSummary.Tests[0].Name}},
    @{Name='Wrong binary evidence';Error='Invalid compact validation summary';Change={param($m) $m.ValidationSummary.PluginSHA256=('0'*64)}},
    @{Name='Invalid report hash';Error='Invalid compact validation summary';Change={param($m) $m.ValidationSummary.ContractReportSHA256='invalid'}}
)
$rejected=@()
foreach($case in $cases){
    $copy=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    & $case.Change $copy
    $copy | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $temporaryRoot $metadataRelative) -Encoding utf8
    $caught=$false
    try { & (Join-Path $temporaryRoot 'Install.ps1') -Validation -RuntimeDll $RuntimeDll | Out-Null }
    catch {
        if($_.Exception.Message -notlike ($case.Error+'*')){throw}
        $caught=$true
    }
    if(!$caught){throw ('Invalid compact proof accepted: '+$case.Name)}
    $rejected+=$case.Name
}
$restorePreviewPassed=$false
if($Receipt){
    $restore=(& (Join-Path $PackageRoot 'Restore.ps1') -Receipt $Receipt | Out-String) | ConvertFrom-Json
    if($restore.WritesPerformed -ne $false -or !$restore.HadOriginal){throw 'Rollback preview differs'}
    $restorePreviewPassed=$true
}
$after=Snapshot
if($before -ne $after){throw 'Installed files or backup count changed during previews'}
[ordered]@{
    PowerShell=$PSVersionTable.PSVersion.ToString();TopLevelEntries=$roots;
    InstallationPreviewPassed=$true;RollbackPreviewPassed=$restorePreviewPassed;
    InvalidSummariesRejected=$rejected;InstalledAndBackupsUnchanged=$true;
    PluginSHA256=$metadata.PluginSHA256
} | ConvertTo-Json -Depth 4
