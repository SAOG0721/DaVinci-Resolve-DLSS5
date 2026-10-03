# Compatibility entry point; 0.4 uses explicit validation packaging.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DlssSdkRoot,
    [string]$ResolveSdkRoot = 'C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\Developer\OpenFX',
    [string]$CudaRoot = $env:CUDA_PATH,
    [string]$FidelityFxSdkRoot = $env:FIDELITYFX_SDK_ROOT,
    [string]$NvOfSdkRoot = $env:NVOF_SDK_ROOT,
    [string]$DxcPath = $env:DXC_PATH,
    [Parameter(Mandatory)][string]$RuntimeDll,
    [switch]$Package
)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot '..\src\scripts\Build-Local.ps1') @PSBoundParameters
