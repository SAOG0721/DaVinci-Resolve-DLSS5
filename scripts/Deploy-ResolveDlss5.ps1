# Compatibility entry point; preview by default, no implicit installation.
[CmdletBinding()]
param(
    [string]$PackageRoot = '',
    [Parameter(Mandatory)][string]$RuntimeDll,
    [switch]$Apply,
    [switch]$Validation
)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot '..\src\scripts\Install-Development.ps1') @PSBoundParameters
