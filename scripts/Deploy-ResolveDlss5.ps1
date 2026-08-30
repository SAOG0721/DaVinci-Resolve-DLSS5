$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $projectRoot 'build\vs2022-x64\bundle\ResolveDlss5.ofx.bundle\Contents\Win64'))
$targetRoot = [System.IO.Path]::GetFullPath(
    'C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle\Contents\Win64')
$expectedTarget =
    'C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle\Contents\Win64'

if ($targetRoot -ne $expectedTarget) {
    throw "Unexpected deployment target: $targetRoot"
}
if (Get-Process Resolve -ErrorAction SilentlyContinue) {
    throw 'DaVinci Resolve is running. Save and close it before deployment.'
}

$requiredFiles = @(
    'ResolveDlss5.ofx',
    'manifest.sha256',
    'runtime\nvngx_dlssnr.dll'
)
foreach ($relativePath in $requiredFiles) {
    $sourceFile = Join-Path $sourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Required build artifact is missing: $sourceFile"
    }
}

New-Item -ItemType Directory -Path (Join-Path $targetRoot 'runtime') -Force |
    Out-Null
foreach ($relativePath in $requiredFiles) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $relativePath) `
        -Destination (Join-Path $targetRoot $relativePath) -Force
}

$results = foreach ($relativePath in $requiredFiles) {
    $sourceFile = Join-Path $sourceRoot $relativePath
    $targetFile = Join-Path $targetRoot $relativePath
    $sourceHash = (Get-FileHash $sourceFile -Algorithm SHA256).Hash
    $targetHash = (Get-FileHash $targetFile -Algorithm SHA256).Hash
    [pscustomobject]@{
        File = $relativePath
        SourceSHA256 = $sourceHash
        InstalledSHA256 = $targetHash
        Match = $sourceHash -eq $targetHash
        InstalledBytes = (Get-Item -LiteralPath $targetFile).Length
    }
}
if ($results.Match -contains $false) {
    throw 'Deployment completed, but at least one SHA-256 comparison failed.'
}

$logDirectory = Join-Path $env:LOCALAPPDATA 'ResolveDlss5'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$logPath = Join-Path $logDirectory 'deploy.log'
$results | Format-List | Out-String | Set-Content -LiteralPath $logPath -Encoding UTF8
"Deployment succeeded at $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" |
    Add-Content -LiteralPath $logPath -Encoding UTF8

$results | Format-Table -AutoSize
Write-Host "Deployment succeeded. Verification log: $logPath" -ForegroundColor Green
