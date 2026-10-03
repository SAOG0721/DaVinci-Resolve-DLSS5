# SPDX-License-Identifier: GPL-3.0-only
# Adapted from the local SAOG0721/Magpie shader generator (2026-10-03).
# This build helper's license is separate from the AMD MIT-licensed DXIL it generates.
param(
    [Parameter(Mandatory = $true)]
    [string]$SdkDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$DxcPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$sdk = [System.IO.Path]::GetFullPath($SdkDirectory)
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $DxcPath)) {
    throw "DXC was not found: $DxcPath"
}
New-Item -ItemType Directory -Force -Path $output | Out-Null

$passes = @(
    "ffx_opticalflow_compute_luminance_pyramid_pass",
    "ffx_opticalflow_compute_optical_flow_advanced_pass_v5",
    "ffx_opticalflow_compute_scd_divergence_pass",
    "ffx_opticalflow_filter_optical_flow_pass_v5",
    "ffx_opticalflow_generate_scd_histogram_pass",
    "ffx_opticalflow_prepare_luma_pass",
    "ffx_opticalflow_scale_optical_flow_advanced_pass_v5"
)

function Write-IfChanged([string]$path, [string]$content) {
    if ([System.IO.File]::Exists($path) -and
        [System.IO.File]::ReadAllText($path) -ceq $content) { return }
    [System.IO.File]::WriteAllText($path, $content, [System.Text.UTF8Encoding]::new($false))
}

function Get-Sha256([string]$path) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($path)
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}

# Hash the generator (including its compiler options), SDK includes, and the
# actual compiler/runtime. Timestamps alone miss restored or switched SDKs.
$inputs = @($PSCommandPath, $DxcPath)
foreach ($dll in @('dxcompiler.dll', 'dxil.dll')) {
    $path = Join-Path (Split-Path $DxcPath) $dll
    if (Test-Path -LiteralPath $path) { $inputs += $path }
}
foreach ($directory in @('Kits\FidelityFX\api\internal\gpu',
    'Kits\FidelityFX\framegeneration\fsr3\include\gpu',
    'Kits\FidelityFX\framegeneration\fsr3\internal\shaders')) {
    $inputs += Get-ChildItem -LiteralPath (Join-Path $sdk $directory) -File -Recurse |
        Select-Object -ExpandProperty FullName
}
$fingerprintText = ($inputs | Sort-Object -Unique | ForEach-Object {
    "$_|$(Get-Sha256 $_)"
}) -join "`n"
$hasher = [System.Security.Cryptography.SHA256]::Create()
try {
    $fingerprint = [Convert]::ToBase64String($hasher.ComputeHash(
        [System.Text.Encoding]::UTF8.GetBytes($fingerprintText)))
} finally { $hasher.Dispose() }
$stampPath = Join-Path $output 'generation.json'
if (Test-Path -LiteralPath $stampPath) {
    try {
        $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
        $current = $stamp.fingerprint -ceq $fingerprint -and $stamp.outputs.Count -eq 28
        foreach ($item in $stamp.outputs) {
            $path = Join-Path $output $item.name
            if (-not (Test-Path -LiteralPath $path) -or
                (Get-Sha256 $path) -cne $item.hash) {
                $current = $false
                break
            }
        }
        if ($current) {
            Write-Host 'FidelityFX Optical Flow shader headers are up to date.'
            exit 0
        }
    } catch { # Missing/incompatible stamp: regenerate and commit a new one below.
    }
}

function Get-ResourceMetadata([string]$assemblyPath) {
    $lines = Get-Content -LiteralPath $assemblyPath
    $start = [Array]::FindIndex($lines, [Predicate[string]]{
        param($line) $line -eq "; Resource Bindings:"
    })
    if ($start -lt 0) { throw "DXC reflection table is missing: $assemblyPath" }

    $groups = @{
        cb = [System.Collections.Generic.List[object]]::new()
        srvTexture = [System.Collections.Generic.List[object]]::new()
        uavTexture = [System.Collections.Generic.List[object]]::new()
        srvBuffer = [System.Collections.Generic.List[object]]::new()
        uavBuffer = [System.Collections.Generic.List[object]]::new()
        sampler = [System.Collections.Generic.List[object]]::new()
    }
    for ($i = $start + 4; $i -lt $lines.Count; ++$i) {
        $line = $lines[$i]
        if ($line -eq ";") { break }
        if ($line -notmatch '^;\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+\S+\s+(\S+)\s+(\d+)\s*$') {
            continue
        }
        $name = $Matches[1]
        $type = $Matches[2]
        $dimension = $Matches[4]
        $bind = $Matches[5]
        $count = [int]$Matches[6]
        if ($bind -notmatch '^(cb|[tus])(\d+)(?:,space(\d+))?$') {
            throw "Unknown HLSL binding '$bind' in $assemblyPath"
        }
        $prefix = $Matches[1]
        $binding = [int]$Matches[2]
        $space = if ($Matches[3]) { [int]$Matches[3] } else { 0 }
        $item = [pscustomobject]@{
            Name = $name; Binding = $binding; Count = $count; Space = $space
        }
        if ($prefix -eq 'cb') { $groups.cb.Add($item); continue }
        if ($prefix -eq 's') { $groups.sampler.Add($item); continue }
        $isBuffer = $dimension -eq 'buf' -or $dimension -eq 'r/o'
        if ($prefix -eq 't') {
            if ($isBuffer) { $groups.srvBuffer.Add($item) }
            else { $groups.srvTexture.Add($item) }
        } elseif ($prefix -eq 'u') {
            if ($isBuffer) { $groups.uavBuffer.Add($item) }
            else { $groups.uavTexture.Add($item) }
        }
    }
    return $groups
}

function Format-ByteArray([byte[]]$bytes) {
    $parts = for ($i = 0; $i -lt $bytes.Length; ++$i) {
        if (($i % 20) -eq 0) { "`n    0x{0:x2}" -f $bytes[$i] }
        else { " 0x{0:x2}" -f $bytes[$i] }
    }
    return (($parts -join ',') + "`n")
}

function Format-ResourceArrays([string]$prefix, $items) {
    if ($items.Count -eq 0) { return "" }
    $names = ($items | ForEach-Object { '"' + $_.Name + '"' }) -join ', '
    $bindings = ($items | ForEach-Object { $_.Binding }) -join ', '
    $counts = ($items | ForEach-Object { $_.Count }) -join ', '
    $spaces = ($items | ForEach-Object { $_.Space }) -join ', '
    return @"
static const char* ${prefix}Names[] = { $names };
static const uint32_t ${prefix}Bindings[] = { $bindings };
static const uint32_t ${prefix}Counts[] = { $counts };
static const uint32_t ${prefix}Spaces[] = { $spaces };
"@
}

foreach ($pass in $passes) {
    $compiled = @()
    for ($hdr = 0; $hdr -le 1; ++$hdr) {
        $dxil = Join-Path $output "$pass.$hdr.dxil"
        $assembly = Join-Path $output "$pass.$hdr.asm"
        $shader = Join-Path $sdk "Kits\FidelityFX\framegeneration\fsr3\internal\shaders\$pass.hlsl"
        $arguments = @(
            '-E', 'CS', '-T', 'cs_6_2', '-O3',
            '-D', 'FFX_GPU=1',
            '-D', 'FFX_IMPLICIT_SHADER_REGISTER_BINDING_HLSL=0',
            '-D', 'FFX_HLSL=1',
            '-D', 'FFX_OPTICALFLOW_EMBED_ROOTSIG=0',
            '-D', "FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=$hdr",
            '-D', 'FFX_HALF=0', '-D', 'FFX_HLSL_SM=62',
            '-I', (Join-Path $sdk 'Kits\FidelityFX\api\internal\gpu'),
            '-I', (Join-Path $sdk 'Kits\FidelityFX\framegeneration\fsr3\include\gpu'),
            '-Fo', $dxil, '-Fc', $assembly, $shader
        )
        & $DxcPath @arguments
        if ($LASTEXITCODE -ne 0) { throw "DXC failed for $pass HDR=$hdr" }
        $compiled += [pscustomobject]@{
            Bytes = [System.IO.File]::ReadAllBytes($dxil)
            Resources = Get-ResourceMetadata $assembly
        }
    }

    foreach ($suffix in @('', '_wave64', '_16bit', '_wave64_16bit')) {
        $symbol = "$pass$suffix"
        $header = Join-Path $output "$symbol`_permutations.h"
        if ($suffix) {
            # The adapter compiles FP32 once. Satisfy the SDK's lookup names
            # with aliases, without pretending these are optimized variants.
            $alias = @"
#pragma once
#include "${pass}_permutations.h"
using ${symbol}_PermutationKey = ${pass}_PermutationKey;
using ${symbol}_PermutationInfo = ${pass}_PermutationInfo;
static constexpr auto& g_${symbol}_IndirectionTable = g_${pass}_IndirectionTable;
static constexpr auto& g_${symbol}_PermutationInfo = g_${pass}_PermutationInfo;
"@
            Write-IfChanged $header $alias
            continue
        }
        $body = @"
#pragma once
#include <stdint.h>

typedef union ${symbol}_PermutationKey {
    struct { uint32_t FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT : 1; };
    uint32_t index;
} ${symbol}_PermutationKey;

typedef struct ${symbol}_PermutationInfo {
    const uint8_t* blobData; uint32_t blobSize; const char* entryName;
    uint32_t numConstantBuffers, numSRVTextures, numUAVTextures;
    uint32_t numSRVBuffers, numUAVBuffers, numSamplers;
    uint32_t numRTAccelerationStructures;
    const char** constantBufferNames; const uint32_t* constantBufferBindings;
    const uint32_t* constantBufferCounts; const uint32_t* constantBufferSpaces;
    const char** srvTextureNames; const uint32_t* srvTextureBindings;
    const uint32_t* srvTextureCounts; const uint32_t* srvTextureSpaces;
    const char** uavTextureNames; const uint32_t* uavTextureBindings;
    const uint32_t* uavTextureCounts; const uint32_t* uavTextureSpaces;
    const char** srvBufferNames; const uint32_t* srvBufferBindings;
    const uint32_t* srvBufferCounts; const uint32_t* srvBufferSpaces;
    const char** uavBufferNames; const uint32_t* uavBufferBindings;
    const uint32_t* uavBufferCounts; const uint32_t* uavBufferSpaces;
    const char** samplerNames; const uint32_t* samplerBindings;
    const uint32_t* samplerCounts; const uint32_t* samplerSpaces;
    const char** rtAccelerationStructureNames;
    const uint32_t* rtAccelerationStructureBindings;
    const uint32_t* rtAccelerationStructureCounts;
    const uint32_t* rtAccelerationStructureSpaces;
} ${symbol}_PermutationInfo;

"@
        for ($hdr = 0; $hdr -le 1; ++$hdr) {
            $resources = $compiled[$hdr].Resources
            $prefix = "${symbol}_$hdr"
            $body += "static const uint8_t ${prefix}Blob[] = {" +
                (Format-ByteArray $compiled[$hdr].Bytes) + "};`n"
            foreach ($kind in @('cb','srvTexture','uavTexture','srvBuffer','uavBuffer','sampler')) {
                $body += Format-ResourceArrays "${prefix}$kind" $resources[$kind]
            }
        }
        $body += "static const int32_t g_${symbol}_IndirectionTable[] = { 0, 1 };`n"
        $body += "static const ${symbol}_PermutationInfo g_${symbol}_PermutationInfo[] = {`n"
        for ($hdr = 0; $hdr -le 1; ++$hdr) {
            $resources = $compiled[$hdr].Resources
            $prefix = "${symbol}_$hdr"
            $ptr = { param($kind, $field) if ($resources[$kind].Count) { "${prefix}${kind}$field" } else { "nullptr" } }
            $body += "    { ${prefix}Blob, sizeof(${prefix}Blob), `"CS`", "
            $body += "$($resources.cb.Count), $($resources.srvTexture.Count), $($resources.uavTexture.Count), "
            $body += "$($resources.srvBuffer.Count), $($resources.uavBuffer.Count), $($resources.sampler.Count), 0, "
            foreach ($kind in @('cb','srvTexture','uavTexture','srvBuffer','uavBuffer','sampler')) {
                $body += "$( & $ptr $kind 'Names'), $( & $ptr $kind 'Bindings'), "
                $body += "$( & $ptr $kind 'Counts'), $( & $ptr $kind 'Spaces'), "
            }
            $body += "nullptr, nullptr, nullptr, nullptr },`n"
        }
        $body += "};`n"
        Write-IfChanged $header $body
    }
}

$generated = foreach ($pass in $passes) {
    foreach ($suffix in @('', '_wave64', '_16bit', '_wave64_16bit')) {
        $name = "$pass$suffix`_permutations.h"
        @{ name = $name; hash = (Get-Sha256 (Join-Path $output $name)) }
    }
}
Write-IfChanged $stampPath (@{ fingerprint = $fingerprint; outputs = @($generated) } | ConvertTo-Json -Depth 4)

Write-Host "Generated FidelityFX Optical Flow DXIL headers in $output"
