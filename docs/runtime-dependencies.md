# Runtime dependency record

Recorded on 2026-08-30 (Asia/Shanghai).

## Packaged community runtime

- Source: external release input; intentionally excluded from Git
- Bundle destination: `ResolveDlss5.ofx.bundle\Contents\Win64\runtime\nvngx_dlssnr.dll`
- File version: `310.8.0.0`
- Product version: `310.8.0.0`
- SHA-256: `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`
- Distribution scope: personal, noncommercial, non-profit use under the
  authorization confirmed by the user. This record is not an independent
  legal opinion.

The build does not place this DLL in the Resolve installation directory and
does not replace any system or game NGX binary. It is loaded only from the
bundle-relative private `runtime` directory.

## Build-time dependencies

- DaVinci Resolve OpenFX 1.4 SDK, configured through `RESOLVE_OFX_SDK_ROOT`
- NGX/DLSS headers and `nvsdk_ngx_s.lib`, configured through `DLSS_SDK_ROOT`
- Visual Studio 2022 Community / MSVC x64
- Windows D3D12 and DXGI import libraries

## Verified test environment

- GPU: NVIDIA GeForce RTX 5070 Ti, 16303 MiB
- NVIDIA driver: `616.56`
- Resolve version: Studio `20.0.1.0006`
- Standalone test: two sequential 640×360 Feature 18 evaluations passed;
  second-frame mean absolute RGB difference was `0.0232355`, output range
  `[0, 1]`.

Resolve's `OFXPluginCacheV2.xml` recorded the plugin with binary status `0`
and the application log contained `OFX: loading com.saog.resolve.dlss5`.
Therefore the initial report of “no effect” was not an OFX scan/load failure;
the old revision had no render telemetry to prove that an effect instance had
actually been added to a clip or node. Revision 0.2 adds a private runtime log
and visible difference/split diagnostic views.

Revision 0.2 was deployed with elevation at `2026-08-30 08:22:03` to:

```text
C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle
```

Installed plugin SHA-256:
`6C73C0D9F20795299858ACAC64CE3905D303C97A4A79D51B86618CAAB5D4E5AB`.
The plugin, manifest, and community runtime all matched their build artifacts
after deployment. The deployment verifier is
`scripts\Deploy-ResolveDlss5.ps1`; its last result is stored at
`%LOCALAPPDATA%\ResolveDlss5\deploy.log`.

## Revision 0.3 multi-instance fix

Resolve created two live OFX instances. The first session successfully created
and evaluated Feature 18, while the second was rejected by the former
single-owner IAT hook and returned the source frame for subsequent renders.
Revision 0.3 replaces that owner latch with a process-level reference count and
serializes NGX operations across instances.

The standalone test now keeps two Feature 18 runtimes alive simultaneously,
creates/evaluates both, then evaluates the first again before either is
destroyed. The hook lifecycle was verified as `1 → 2 → 1 → 0` and the test
passed. Revision 0.3 was deployed at `2026-08-30 08:38:17`.

- Installed plugin SHA-256:
  `15EA0A24BCDAAD7AF1A5475C6E86DF979F680C4233445FC12EB4B4BF6E5FAFB8`
- Installed runtime SHA-256:
  `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`

Each Release build writes the current plugin and runtime hashes to:

```text
build\vs2022-x64\bundle\ResolveDlss5.ofx.bundle\Contents\Win64\manifest.sha256
```
