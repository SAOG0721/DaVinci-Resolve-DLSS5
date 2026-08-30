$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$forbiddenExtensions =
    @('.dll', '.exe', '.exp', '.lib', '.obj', '.ofx', '.pdb', '.zip', '.7z')
$forbiddenRoots = @('build/', 'dist/', 'out/')

$tracked = @(& git -C $projectRoot ls-files)
if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }
$violations = @($tracked | Where-Object {
    $path = $_.Replace('\', '/')
    $extension = [System.IO.Path]::GetExtension($path).ToLowerInvariant()
    ($forbiddenExtensions -contains $extension) -or
        @($forbiddenRoots | Where-Object { $path.StartsWith($_) }).Count -ne 0
})
if ($violations.Count -ne 0) {
    throw "Forbidden tracked source files: $($violations -join ', ')"
}

$historyObjects = @(& git -C $projectRoot rev-list --objects --all)
if ($LASTEXITCODE -ne 0) { throw 'git rev-list failed.' }
$historyViolations = @($historyObjects | ForEach-Object {
    $parts = $_ -split ' ', 2
    if ($parts.Count -eq 2) { $parts[1] }
} | Where-Object {
    $extension = [System.IO.Path]::GetExtension($_).ToLowerInvariant()
    $forbiddenExtensions -contains $extension
})
if ($historyViolations.Count -ne 0) {
    throw "Forbidden binary files exist in Git history: $($historyViolations -join ', ')"
}

Write-Host "Source tree verified: $($tracked.Count) tracked files; no binary artifacts."
