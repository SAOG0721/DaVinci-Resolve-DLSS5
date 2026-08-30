# Resolve DLSS5 Experimental

[简体中文](README_ZH.md) | English

Windows x64 DaVinci Resolve OpenFX project for an experimental,
same-resolution DLSS Neural Rendering video filter.

> Unofficial research software. It is not affiliated with or endorsed by
> Blackmagic Design or NVIDIA.

## Download

Use the tagged prerelease from the
[GitHub Releases page](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/releases).
The binary ZIP contains the complete OFX bundle and a separately distributed
community runtime. Git and GitHub source archives contain no DLL.

## Current status

Version 0.3.0 is the first implementation verified inside DaVinci Resolve:

- Native OpenFX Filter/General contexts with float RGBA input and output.
- The private Feature 18 D3D12 path follows the verified Magpie contract:
  application ID `0x0876232C`, same-resolution creation, signed-snippet
  exports, scoped `GetModuleFileNameW` IAT compatibility, and SEH guards.
- Input and output use the Magpie-verified `R8G8B8A8_UNORM` neural proxy.
- Motion is zero-filled `R16G16_FLOAT`; depth is zero-filled `R32_FLOAT`.
- Source alpha is restored after evaluation.
- A failed initialization/evaluation returns the source frame and shows a
  persistent OFX error instead of returning black or stale output.
- Runtime events are written to
  `%LOCALAPPDATA%\ResolveDlss5\ResolveDlss5.log`.
- Diagnostic `Difference x10` and `Left / Right Compare` output views make a
  subtle neural change immediately visible.
- The configured community `nvngx_dlssnr.dll` is copied into the bundle's
  private `runtime` directory; `manifest.sha256` records both binary hashes.
- A two-live-runtime Feature 18 smoke test passes, and the effect has been
  confirmed active in Resolve after the multi-instance hook fix.

This remains experimental. The current bridge is deliberately simple and slow:
Resolve supplies CPU float frames, the plugin converts them to RGBA8, uploads
them to its own D3D12 device, waits for Feature 18, then reads the result back.
CUDA/D3D12 zero-copy and real motion/depth are later work.

## Inspector parameter contract

The exposed types mirror the controls recovered from the add-on implementation:

| Control | OFX type |
| --- | --- |
| Enable DLSS Neural Rendering | Boolean |
| NR Preset | Choice (`#1`, `#2`, `#3`) |
| NR UI Correction | Boolean |
| Style | Choice |
| Intensity / Local Tone / Local Structure / Skin Structure | Double |
| Use Automatic Mask | Boolean |
| Input Encoding | Choice |
| Scene Paper-White Scale / HDR Transfer Strength | Double |
| Guidance Mode / Depth Convention | Choice |
| Motion Scale X/Y Multiplier | Double |
| Output Mix | Double |
| Output View | Choice (`Processed`, `Difference x10`, `Left / Right Compare`) |
| Reset NR Feature and Clear History | Push button |

`Guidance Mode` currently exposes the intended diagnostic contract, but all
four choices still use zero MV/depth until a real provider is connected.

## Build and test

Required build dependencies are not vendored: Visual Studio 2022 with CMake and
the C++ workload, CUDA Toolkit 12, the Resolve Developer/OpenFX SDK, and the
NVIDIA NGX/DLSS SDK. Configure local paths through environment variables or a
private `CMakeUserPresets.json` copied from the included example:

```powershell
$env:RESOLVE_OFX_SDK_ROOT = 'C:\path\to\DaVinci Resolve\Support\Developer\OpenFX'
$env:DLSS_SDK_ROOT = 'C:\path\to\DLSS'
$env:DLSSNR_RUNTIME_DLL = 'C:\path\to\nvngx_dlssnr.dll'
cmake --preset vs2022-x64
cmake --build --preset release
ctest --test-dir build\vs2022-x64 -C Release --output-on-failure
```

The runtime variable is optional for compiling source, but required for the
runtime smoke test and for producing an installable release bundle.

Generated bundle:

```text
build\vs2022-x64\bundle\ResolveDlss5.ofx.bundle
```

Standalone runtime probe:

```text
build\vs2022-x64\Release\ResolveDlss5RuntimeSmoke.exe
```

## Local Resolve installation

Copy the complete bundle to:

```text
C:\Program Files\Common Files\OFX\Plugins
```

Then restart Resolve and locate `DLSS Neural Video Experimental` under
`DLSS Experimental`. Do not copy or overwrite NGX files in the DaVinci Resolve
installation directory. Removing this one bundle removes the private runtime.

## Known limits

- Windows x64 and NVIDIA D3D12 only.
- Same-resolution enhancement only; no Super Resolution or Frame Generation.
- SDR/RGBA8 is the verified path. Linear/scRGB uses a reversible proxy curve;
  PQ is experimental and has not yet passed a Resolve HDR test matrix.
- Zero motion/depth only in this revision.
- Full-frame synchronous CPU/D3D12 copies make preview performance unsuitable
  for production use.
- Multiple Resolve OFX instances share a process-level, reference-counted
  snippet compatibility hook. NGX session operations are globally serialized
  while each instance retains its own Feature 18 history.
- Community runtime use/distribution remains limited to the user's confirmed
  personal, noncommercial, non-profit authorization.

## Source and release boundary

The source tree is MIT licensed. It intentionally excludes all DLL, SDK, static
library, build, and release artifacts. The tagged prerelease ZIP may include
the separately supplied community `nvngx_dlssnr.dll`; see
[third-party notices](THIRD_PARTY_NOTICES.md) for its identity and boundary.

The Feature 18 work is based on implementation experience from the author's
experimental Magpie fork. Magpie itself remains GPL-3.0; this repository does
not relicense Magpie or code owned by other Magpie contributors.

## License

MIT. See [LICENSE](LICENSE).
