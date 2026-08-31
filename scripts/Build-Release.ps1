param(
    [string]$Version = '0.3.1',
    [string]$PackageName = 'Resolve-DLSS5-Experimental-x64',
    [string]$RuntimeDll = $env:DLSSNR_RUNTIME_DLL,
    [string]$DlssSdkRoot = $env:DLSS_SDK_ROOT,
    [string]$BuildPreset = 'release',
    [switch]$SkipBuild,
    [switch]$AllowDirtySource
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$distRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$versionMatch = [regex]::Match($Version, '^(?:v)?(\d+)\.(\d+)\.(\d+)$')
if (!$versionMatch.Success) {
    throw "Version must be major.minor.patch: $Version"
}
$normalizedVersion = '{0}.{1}.{2}' -f $versionMatch.Groups[1].Value,
    $versionMatch.Groups[2].Value, $versionMatch.Groups[3].Value
$tag = "v$normalizedVersion-experimental"
$releaseRoot = [System.IO.Path]::GetFullPath((Join-Path $distRoot $tag))
$stagingRoot = [System.IO.Path]::GetFullPath((Join-Path $releaseRoot $PackageName))
$zipPath = [System.IO.Path]::GetFullPath((Join-Path $releaseRoot "$PackageName.zip"))
$checksumPath = "$zipPath.sha256"
$bundleSource = Join-Path $projectRoot 'build\vs2022-x64\bundle\ResolveDlss5.ofx.bundle'
$bundleDestination = Join-Path $stagingRoot 'ResolveDlss5.ofx.bundle'
$expectedRuntimeHash =
    '984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014'

function Assert-ChildPath([string]$Candidate, [string]$Parent) {
    $prefix = $Parent.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (!$Candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escaped the expected parent: $Candidate"
    }
}

Assert-ChildPath $releaseRoot $distRoot
Assert-ChildPath $stagingRoot $releaseRoot
Assert-ChildPath $zipPath $releaseRoot

function Invoke-ProjectGit([string[]]$Arguments) {
    $output = & git -C $projectRoot @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($output | Out-String).Trim()
}

$commit = Invoke-ProjectGit @('rev-parse', 'HEAD')
$commitTime = Invoke-ProjectGit @('show', '-s', '--format=%cI', 'HEAD')
$sourceDirty = [bool](Invoke-ProjectGit @(
    'status', '--porcelain=v1', '--untracked-files=all'))
if ($sourceDirty -and !$AllowDirtySource) {
    throw 'Commit all source changes before packaging, or use -AllowDirtySource for a local test.'
}

if (!$SkipBuild) {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
    if (!$cmakePath) {
        $candidate =
            'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $candidate) {
            $cmakePath = $candidate
        }
    }
    if (!$cmakePath) { throw 'CMake was not found.' }
    & $cmakePath --preset vs2022-x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmakePath --build --preset $BuildPreset
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
}

$pluginSource = Join-Path $bundleSource 'Contents\Win64\ResolveDlss5.ofx'
if (!(Test-Path -LiteralPath $pluginSource -PathType Leaf)) {
    throw "Built OFX plugin was not found: $pluginSource"
}
if (!$RuntimeDll -or !(Test-Path -LiteralPath $RuntimeDll -PathType Leaf)) {
    throw 'Set -RuntimeDll or DLSSNR_RUNTIME_DLL to the authorized community runtime.'
}
if (!$DlssSdkRoot) {
    $cachePath = Join-Path $projectRoot 'build\vs2022-x64\CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath) {
        $cacheEntry = Get-Content -LiteralPath $cachePath |
            Where-Object { $_ -like 'DLSS_SDK_ROOT:PATH=*' } |
            Select-Object -First 1
        if ($cacheEntry) {
            $DlssSdkRoot = ($cacheEntry -split '=', 2)[1]
        }
    }
}
$nvidiaSdkLicense = if ($DlssSdkRoot) {
    Join-Path $DlssSdkRoot 'LICENSE.txt'
} else {
    $null
}
if (!$nvidiaSdkLicense -or !(Test-Path -LiteralPath $nvidiaSdkLicense -PathType Leaf)) {
    throw 'NVIDIA RTX SDK LICENSE.txt was not found. Set -DlssSdkRoot or DLSS_SDK_ROOT.'
}
$runtimeHash = (Get-FileHash -LiteralPath $RuntimeDll -Algorithm SHA256).Hash
if ($runtimeHash -ne $expectedRuntimeHash) {
    throw "Unexpected nvngx_dlssnr.dll SHA-256: $runtimeHash"
}

New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingRoot | Out-Null
Copy-Item -LiteralPath $bundleSource -Destination $bundleDestination -Recurse
$privateRuntimeDirectory = Join-Path $bundleDestination 'Contents\Win64\runtime'
New-Item -ItemType Directory -Path $privateRuntimeDirectory -Force | Out-Null
Copy-Item -LiteralPath $RuntimeDll -Destination (
    Join-Path $privateRuntimeDirectory 'nvngx_dlssnr.dll') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\RELEASE_INSTALL.md') -Destination (
    Join-Path $stagingRoot 'README.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (
    Join-Path $stagingRoot 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination (
    Join-Path $stagingRoot 'THIRD-PARTY-NOTICES.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\licenses\OFX-Support-Library-BSD-3-Clause.txt') -Destination (
    Join-Path $stagingRoot 'OFX-Support-Library-LICENSE.txt')
Copy-Item -LiteralPath $nvidiaSdkLicense -Destination (
    Join-Path $stagingRoot 'NVIDIA-RTX-SDK-LICENSE.txt')

$unexpectedDlls = @(Get-ChildItem -LiteralPath $stagingRoot -Recurse -File -Filter '*.dll' |
    Where-Object { $_.Name -ne 'nvngx_dlssnr.dll' })
if ($unexpectedDlls.Count -ne 0) {
    throw "Unexpected DLL in release staging: $($unexpectedDlls.FullName -join ', ')"
}
$packagedRuntime = Join-Path $privateRuntimeDirectory 'nvngx_dlssnr.dll'
$packagedRuntimeHash =
    (Get-FileHash -LiteralPath $packagedRuntime -Algorithm SHA256).Hash
if ($packagedRuntimeHash -ne $expectedRuntimeHash) {
    throw 'Packaged runtime hash mismatch.'
}

$sourceDate = if ($commitTime) {
    [DateTimeOffset]::Parse($commitTime).UtcDateTime
} else {
    [DateTime]::UtcNow
}
$fileRecords = @(Get-ChildItem -LiteralPath $stagingRoot -Recurse -File |
    Sort-Object FullName | ForEach-Object {
        $relativePath = $_.FullName.Substring($stagingRoot.Length).TrimStart('\', '/')
        [ordered]@{
            path = $relativePath.Replace('\', '/')
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            fileVersion = $_.VersionInfo.FileVersion
        }
    })
$manifest = [ordered]@{
    schemaVersion = 1
    package = $PackageName
    version = $normalizedVersion
    tag = $tag
    commit = $commit
    sourceDirty = $sourceDirty
    sourceDateUtc = $sourceDate.ToString('o')
    platform = 'Windows-x64'
    runtime = [ordered]@{
        name = 'nvngx_dlssnr.dll'
        version = '310.8.0.0'
        sha256 = $packagedRuntimeHash
        distribution = 'separate community-modified release runtime'
    }
    files = $fileRecords
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (
    Join-Path $stagingRoot 'build-manifest.json') -Encoding UTF8

Get-ChildItem -LiteralPath $stagingRoot -Recurse -Force | ForEach-Object {
    $_.LastWriteTimeUtc = $sourceDate
}
(Get-Item -LiteralPath $stagingRoot).LastWriteTimeUtc = $sourceDate

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
if (Test-Path -LiteralPath $checksumPath) {
    Remove-Item -LiteralPath $checksumPath -Force
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open(
    $zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem -LiteralPath $stagingRoot -Recurse -File |
        Sort-Object FullName | ForEach-Object {
            $relativePath = $_.FullName.Substring($stagingRoot.Length).TrimStart(
                '\', '/').Replace('\', '/')
            $entry = $archive.CreateEntry(
                "$PackageName/$relativePath",
                [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]$sourceDate
            $inputStream = [System.IO.File]::OpenRead($_.FullName)
            $outputStream = $entry.Open()
            try {
                $inputStream.CopyTo($outputStream)
            } finally {
                $outputStream.Dispose()
                $inputStream.Dispose()
            }
        }
} finally {
    $archive.Dispose()
}

$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
"$zipHash  $([System.IO.Path]::GetFileName($zipPath))" |
    Set-Content -LiteralPath $checksumPath -Encoding ASCII
Write-Host "Tag:        $tag"
Write-Host "Commit:     $commit"
Write-Host "Release:    $zipPath"
Write-Host "ZIP bytes:  $((Get-Item -LiteralPath $zipPath).Length)"
Write-Host "ZIP SHA256: $zipHash"
