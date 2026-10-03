[CmdletBinding()]
param([Parameter(Mandatory)][string]$Receipt,[switch]$Apply)
$ErrorActionPreference='Stop'
$data=Get-Content -LiteralPath $Receipt -Raw | ConvertFrom-Json
$expectedTarget='C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle'
if([IO.Path]::GetFullPath($data.Target) -ne $expectedTarget){throw 'Receipt target is not the permitted private OFX bundle'}
$backupRoot=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'ResolveDlss5\Backups'))
$backup=[IO.Path]::GetFullPath($data.Backup)
if(!$backup.StartsWith($backupRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Receipt backup is outside the permitted backup directory'}
if($data.HadOriginal -and !(Test-Path -LiteralPath $backup -PathType Container)){throw 'Original backup is missing'}
$plugin=Join-Path $expectedTarget 'Contents\Win64\ResolveDlss5.ofx'
$runtime=Join-Path $expectedTarget 'Contents\Win64\runtime\nvngx_dlssnr.dll'
if((Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash -ne $data.PluginSHA256 -or
    (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne $data.RuntimeSHA256){throw 'Installed bundle changed since this receipt; preserve it and review manually'}
if(!$Apply){[ordered]@{Mode='PreviewOnly';Target=$expectedTarget;OriginalBackup=$backup;HadOriginal=$data.HadOriginal;WritesPerformed=$false}|ConvertTo-Json;return}
if(Get-Process Resolve -ErrorAction SilentlyContinue){throw 'Close Resolve before restoring the OFX bundle'}
$recovery=Join-Path (Split-Path -Parent (Split-Path -Parent $backup)) ('replaced-'+[guid]::NewGuid().ToString('N')+'.bundle')
if(![IO.Path]::GetFullPath($recovery).StartsWith($backupRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid recovery path'}
Move-Item -LiteralPath $expectedTarget -Destination $recovery
try {if($data.HadOriginal){Move-Item -LiteralPath $backup -Destination $expectedTarget}}
catch {Move-Item -LiteralPath $recovery -Destination $expectedTarget;throw}
Write-Output "Previous bundle restored; replaced development files preserved at $recovery"
