[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DlssSdkRoot,
    [string]$ResolveSdkRoot = 'C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\Developer\OpenFX',
    [string]$CudaRoot = $env:CUDA_PATH,
    [string]$FidelityFxSdkRoot = $env:FIDELITYFX_SDK_ROOT,
    [string]$NvOfSdkRoot = $env:NVOF_SDK_ROOT,
    [string]$DxcPath = $env:DXC_PATH,
    [string]$RuntimeDll = 'C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle\Contents\Win64\runtime\nvngx_dlssnr.dll',
    [switch]$Package
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildRoot=Join-Path $projectRoot 'build'
$sourceRoot=Join-Path $projectRoot 'src'
$versionMatch=[regex]::Match((Get-Content -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt') -Raw),'project\(ResolveDlss5 VERSION (\d+\.\d+\.\d+) ')
if(!$versionMatch.Success){throw 'Project version was not found'}
$version=$versionMatch.Groups[1].Value
$cmakeCommand=Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmakePath=if($cmakeCommand){$cmakeCommand.Source}else{
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(!(Test-Path -LiteralPath $vswhere -PathType Leaf)){throw 'CMake executable was not found; add cmake.exe to PATH'}
    $vsRoot=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if(!$vsRoot){throw 'Visual Studio C++ tools were not found'}
    Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if(!(Test-Path -LiteralPath $cmakePath -PathType Leaf)){throw 'CMake executable was not found'}
if(!$DxcPath){
    $dxcCommand=Get-Command dxc.exe -ErrorAction SilentlyContinue
    if($dxcCommand){$DxcPath=$dxcCommand.Source}
}
if(!$FidelityFxSdkRoot -or !$NvOfSdkRoot -or !$DxcPath){
    throw 'Provide -FidelityFxSdkRoot, -NvOfSdkRoot and -DxcPath, or the corresponding FIDELITYFX_SDK_ROOT/NVOF_SDK_ROOT/DXC_PATH environment variables'
}
if(!(Test-Path -LiteralPath $RuntimeDll -PathType Leaf)){throw 'Provide the locally authorized runtime DLL for real NGX tests'}
$runtimeHash=(Get-FileHash -LiteralPath $RuntimeDll -Algorithm SHA256).Hash
$configureArgs=@('-S',$sourceRoot,'-B',$buildRoot,'-G','Visual Studio 17 2022','-A','x64',
    '-DRESOLVE_DLSS5_BUILD_SMOKE_TEST=ON',"-DDLSS_SDK_ROOT=$DlssSdkRoot", "-DRESOLVE_OFX_SDK_ROOT=$ResolveSdkRoot",
    "-DFIDELITYFX_SDK_ROOT=$FidelityFxSdkRoot", "-DNVOF_SDK_ROOT=$NvOfSdkRoot", "-DDXC_PATH=$DxcPath", "-DDLSSNR_RUNTIME_DLL=$RuntimeDll")
if($CudaRoot){$configureArgs+="-DCUDAToolkit_ROOT=$CudaRoot"}
& $cmakePath @configureArgs
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
& $cmakePath --build $buildRoot --config Release --parallel 6
if($LASTEXITCODE -ne 0){throw 'Release build failed'}
$evidenceSubdirectory='v'+$version+'-fusion'
$resultRoot=Join-Path $projectRoot ('tests\results\'+$evidenceSubdirectory)
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null
$ctestPath=Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
& $ctestPath --test-dir $buildRoot -C Release --output-on-failure --output-junit (Join-Path $resultRoot 'ctest-release.xml')
if($LASTEXITCODE -ne 0){throw 'Production contract tests failed; no package created'}
Copy-Item -LiteralPath (Join-Path $resultRoot 'ctest-release.xml') -Destination (Join-Path $projectRoot 'tests\results\ctest-release.xml') -Force
if(!$Package){return}
$deliveryRoot=Join-Path $projectRoot ('dist\'+$version)
$bundleRoot=Join-Path $deliveryRoot 'ResolveDlss5.ofx.bundle'
$binaryRoot=Join-Path $bundleRoot 'Contents\Win64'
$noticeRoot=Join-Path $bundleRoot 'Contents\licenses'
New-Item -ItemType Directory -Path $binaryRoot,$noticeRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'Release\ResolveDlss5.ofx') -Destination $binaryRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $noticeRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\licenses\OFX-Support-Library-BSD-3-Clause.txt') -Destination $noticeRoot
foreach($licenseName in @('AMD-FidelityFX-MIT.txt','NVIDIA-OpticalFlow-BSD-3-Clause.txt','Magpie-Build-Helper-GPL-3.0.txt')){
    Copy-Item -LiteralPath (Join-Path $projectRoot ('docs\licenses\'+$licenseName)) -Destination $noticeRoot
}
Copy-Item -LiteralPath (Join-Path $DlssSdkRoot 'LICENSE.txt') -Destination (Join-Path $noticeRoot 'NVIDIA-RTX-SDK-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\THIRD_PARTY_NOTICES.md') -Destination (Join-Path $bundleRoot 'Contents')
'This software contains source code provided by NVIDIA Corporation.' | Set-Content -LiteralPath (Join-Path $noticeRoot 'NVIDIA-NOTICE.txt') -Encoding utf8
$pluginHash=(Get-FileHash -LiteralPath (Join-Path $binaryRoot 'ResolveDlss5.ofx') -Algorithm SHA256).Hash
[ordered]@{
    Version=$version; PackageRevision=8; ReleaseReady=$false; TestsPassed=9;
    EvidenceSubdirectory=$evidenceSubdirectory;
    DeploymentAllowed=$false;
    ValidationAllowed=$true;
    BlockedReason='Manual native Resolve preview/playback/export acceptance remains pending.';
    PluginRelativePath='Contents\Win64\ResolveDlss5.ofx'; PluginSHA256=$pluginHash;
    TestedRuntimeSHA256=$runtimeHash; RuntimeIncluded=$false;
    SourceBaseline='ce52a3e425a105ab6215c1d4c0411215b873b49c';
    Scope=($version+' Fusion-compatible error reporting, temporal bypass/time-base fallback, motion-input status and streaming history; existing IDs/defaults preserved');
    Pending='Manual Resolve Inspector/playback/export acceptance, sequential preparation cache, inference scaling, suffix cache, CUDA bridge, full fault recovery and 40-series/higher-host validation'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $deliveryRoot 'manifest.json') -Encoding utf8
Write-Output "Development candidate prepared at $deliveryRoot; installation was not performed."
