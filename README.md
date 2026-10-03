# Resolve DLSS5 Experimental

[简体中文](README_ZH.md) | English

An experimental Windows x64 OpenFX filter for same-resolution DLSS Neural Rendering in DaVinci Resolve. Version **0.4.0, revision 5** adds SDR/HDR correction controls, up to three NR passes, optical flow and a Fusion motion-vector input.

This is a **validation prerelease**, with incomplete host and performance acceptance. It is unofficial and is not affiliated with Blackmagic Design or NVIDIA. See the [release notes](docs/RELEASE_NOTES_v0.4.0.md) for the changes and remaining work.

## Download and requirements

The prepared binary asset is `ResolveDLSS5-0.4.0-win64.zip`, with a `.sha256.txt` checksum. Published versions are listed on [GitHub Releases](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/releases). The ZIP contains the plugin, installation and rollback scripts, licenses, and validation reports. **It contains no `nvngx_dlssnr.dll`, SDK, or display-driver DLL.**

| Component | Current scope |
| --- | --- |
| OS | Windows x64 |
| Resolve | Minimum target: 20.0.1 build 6; Studio 20.0.1.6 registration and parameter round trips tested |
| GPU | RTX 40/50 series target; actual standalone GPU tests used RTX 5070 Ti |
| NVIDIA driver | Test machine: 616.92; other versions need validation |
| Community runtime | Independently obtained, authorized `nvngx_dlssnr.dll` 310.8.0.0 with the exact checksum below |

Runtime SHA-256:

```text
984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014
```

The specified FP8-compatible runtime targets RTX 40/50. The plugin has no GPU model whitelist, but requires a suitable NVIDIA/D3D12 adapter and successful NGX initialization. This does not establish compatibility with every card, driver or Resolve edition. RTX 40, Resolve Free, newer Resolve versions, and multi-GPU workflows remain unverified.

## Installation and rollback

Extract the binary ZIP. Run the installer in the extracted `0.4.0` directory, specifying your authorized runtime. This command only previews the installation:

```powershell
.\Install-Development.ps1 -Validation -RuntimeDll 'C:\path\to\nvngx_dlssnr.dll'
```

Close Resolve. To install, run the same command in an administrator PowerShell with `-Apply`:

```powershell
.\Install-Development.ps1 -Validation -RuntimeDll 'C:\path\to\nvngx_dlssnr.dll' -Apply
```

The fixed target is `C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`. The installer checks plugin/runtime hashes, preserves the previous bundle, and prints its `receipt.json` path. Keep that receipt. Restart Resolve after installation. Installation without `-Validation` is blocked for this build.

To roll back, close Resolve and use the receipt from this installation:

```powershell
.\Restore-Development.ps1 -Receipt 'C:\path\to\receipt.json'
# After checking the preview, add -Apply in administrator PowerShell.
```

See [installation details](docs/RELEASE_INSTALL.md). Do not use a receipt from another installation.

## Using the effect

Add **DLSS Neural Video Experimental** from Resolve's effects library to a test clip or use its Fusion OFX node. Start with one NR pass and the default detail settings. Adjust Overall Strength and Mix, then Chroma, Lightness, Shadow/Structure and Highlight/Glow. Show Advanced reveals hue, dark and highlight protection, soft compression, and low/high-frequency controls. Pass 2/3 controls appear when enabled, and hidden values are retained.

The effect preserves the original float image as its base. It applies the final NR correction after subtracting the decoded, actually quantized inference input. All NR passes share the final correction controls. The neural proxy remains RGBA8; FP8 describes the runtime's model compatibility, not the image texture format.

Choose the encoding that the **node actually receives**: sRGB, Rec.709 gamma 2.4, Rec.2020 PQ/HLG, linear Rec.709, or linear Rec.2020. HDR delivery settings do not identify the node's input encoding. Linear HDR 1.0 represents reference white; the defaults are 203 nits reference white and 1000 nits peak. There is no automatic encoding detection, and Log/DWG/ACES modes are not implemented.

Internal motion offers **None, AMDOF, and NVOF**, with method-specific quality controls. The default is NVOF / Quality. Internal estimation needs only the current and previous source frames. None uses zero motion and still runs NR. Depth remains zero.

In Fusion, connect a same-size float RGBA motion image to **MotionVectors** and enable **Use External Motion (Skip Estimation)** to bypass internal estimation. Motion must point from the current frame to the previous frame. The default is R/G, source-pixel units, positive X right and positive Y down. Channel, UV-unit, Y-axis and scale conversions are available. Native Vector/AOV planes require upstream mapping to RGBA. See the [motion-vector contract](docs/MOTION_VECTORS.md).

## Validation and performance

Five standalone test groups pass: runtime smoke, CPU/color controls, streaming NR, GPU contracts, and optical flow. Resolve's parameter API checked 63 controls/actions including hidden compatibility IDs, nine groups, setting round trips and the MotionVectors link. **The r5 headless host check did not render an OFX frame or establish timeline-export acceptance.** Follow the [manual checks](tests/MANUAL-ACCEPTANCE.md).

On the Ryzen 9 9950X3D / RTX 5070 Ti test machine, synthetic 1080p CPU postprocessing changed from about 128 to 19 ms with defaults, 494 to 70 ms with protection, and 809 to 113 ms with protection plus frequency controls. These are isolated stage measurements, not Resolve FPS. Complex 4K CPU postprocessing still takes about 459 ms. GPU postprocessing and a Resolve CUDA image bridge remain planned.

## Known limitations

- Seeking, reverse playback and changes to active inference settings reset NR history. A cold partial export can differ from a full sequential export. The previous all-history fetching/replay path has been removed.
- The latest-frame NR result cache can reuse output for post-only edits when the request signature matches. Resolve sequence boundaries can invalidate it; responsive native adjustments are not yet verified.
- Postprocessing and host image transfers still use the CPU. SDK calls are serialized. Cache/pool budgets are not a process-wide VRAM limit.
- Five-mode antiflicker, bidirectional flow/confidence, inference-resolution scaling, disk preparation cache, and pass-suffix reuse are not implemented.
- Full GPU cancellation/device-loss recovery is incomplete; existing NR fence waits can block.
- Legacy UI groups are removed. Hidden legacy IDs remain for settings compatibility. The net-correction and HDR changes can alter the appearance of 0.3.x projects; compare on a copy.

## Source and license

See [BUILDING.md](docs/BUILDING.md) for Visual Studio 2022, CMake, CUDA, NGX, Resolve OpenFX SDK, FidelityFX v2.3.0, Optical Flow SDK, and DXC setup. Dependencies and private runtimes are external. Tests generate their own inputs; source archives contain no media, caches or binaries.

Project source is MIT, with a separately identified GPL-3.0-only shader-build helper and dependency notices. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The implementation draws on the author's [Magpie DLSSNR work](https://github.com/SAOG0721/Magpie).
