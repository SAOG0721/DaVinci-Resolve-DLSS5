# Resolve DLSS5 0.4.0

[简体中文](README_ZH.md) | English

DLSS Neural Rendering for DaVinci Resolve, with adjustable SDR/HDR detail, color and protection controls, up to three NR passes, and optical-flow or external motion input. Version 0.4.0 is an experimental release.

## Download

Visit [GitHub Releases](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/releases).

Download `ResolveDLSS5-0.4.0-win64.zip` for installation. For source, use the release page's **Source code (zip)** or **Source code (tar.gz)** downloads.

The plugin ZIP contains the OFX bundle with the community DLSSNR runtime, installation/rollback scripts, instructions and licenses.

## Requirements

| Component | Requirement |
| --- | --- |
| System | Windows x64 |
| Resolve | Version target: 20.0.1 build 6 or later |
| GPU | RTX 40 or RTX 50 series |
| Runtime | Bundled community-modified `nvngx_dlssnr.dll` 310.8.0.0, or another compatible Feature 18 runtime |

Reference environment: Resolve Studio 20.0.1.6, RTX 5070 Ti, NVIDIA driver 616.92. GPU initialization also depends on the installed driver and a compatible runtime.

## Install

Extract the plugin ZIP and open PowerShell in its `0.4.0` directory. Preview installation with the bundled runtime:

```powershell
.\Install.ps1 -Validation
```

Close Resolve, then run the command in administrator PowerShell with `-Apply`:

```powershell
.\Install.ps1 -Validation -Apply
```

To choose another compatible runtime, add `-RuntimeDll <path>` to the installation command.

Installation target: `C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`. The script backs up the previous bundle and prints a `receipt.json` path. Keep that receipt, restart Resolve and add **DLSS Neural Video Experimental** from the effects library.

To restore the previous plugin, close Resolve and preview with the receipt from your installation:

```powershell
.\Restore.ps1 -Receipt 'C:\path\to\receipt.json'
```

Add `-Apply` in administrator PowerShell after checking the preview. See [installation details](docs/RELEASE_INSTALL.md).

## Quick start

1. Choose **Input Encoding** for the data received by the effect node.
2. Start with **1 Pass**, then adjust **Overall Strength** and **Output Mix**.
3. Use **Chroma Strength**, **Lightness**, **Shadow/Structure** and **Highlight/Glow** to shape the final correction. Enable **Advanced** for protection, compression and frequency controls.
4. Choose **Flow Method** and its quality setting. The default is **NVOF / Quality**. **None** supplies zero motion while NR continues.
5. Increase the pass count or optical-flow quality according to the image and processing time.

All passes contribute to a final correction applied over the original float image. Detail and protection controls adjust that combined correction.

## Main controls

| Control | Use |
| --- | --- |
| Overall Strength / Output Mix | Set correction strength and blend with the original image |
| Chroma Strength / Lightness | Adjust color and lightness changes |
| Shadow/Structure / Highlight/Glow | Adjust darkening and brightening contributions |
| Advanced | Reveal hue, shadow and highlight protection, Soft Compression, Low Frequency and High Frequency |
| NR Pass Count | Select 1–3 passes; each pass has its own style, intensity, tone, structure, skin and mask settings |
| Flow Method | Choose None, AMDOF or NVOF |
| External Motion | Read the Fusion MotionVectors input and skip internal estimation |
| Output View | Choose Processed, Difference x10 or Split View |
| Reset NR | Reset temporal history for the next render |

Hidden pass settings are retained. High-resolution footage, additional passes, advanced frequency/protection settings and higher-quality flow increase processing time.

## SDR and HDR

Input Encoding offers sRGB, Rec.709 gamma 2.4, Rec.2020 PQ/HLG, linear Rec.709 and linear Rec.2020. The default is sRGB. Select the encoding at the effect's position in your color pipeline.

For HDR, **White (nits)** sets reference white and **Peak (nits)** sets the reference peak. Defaults are 203 and 1000 nits; set Peak ≥ White. Linear HDR 1.0 represents reference white. **White Scale** and **HDR Strength** adjust the HDR proxy and its contribution.

## Fusion motion input

Connect a float RGBA motion image to **MotionVectors**, then enable **External Motion**. Use current-to-previous vectors with the same size, bounds and pixel aspect ratio as the source. Defaults are R=X, G=Y, source-pixel units, positive X right and positive Y down.

The panel provides X/Y Channel, Vector Units, Positive Y Up and X/Y Scale. Map named Fusion Vector/AOV or EXR channels into RGBA upstream, and preserve the motion image as raw signed data. See [motion-vector setup](docs/MOTION_VECTORS.md).

## Playback and existing projects

Sequential playback retains NR history. Seeking, reverse playback and changes to active NR settings reset it, so a cold partial export can differ from a full sequential export. **Reset NR** provides an explicit reset.

0.3.x projects retain their parameter values. The updated correction and HDR processing can change the image; use a project copy and Output Mix/Difference x10 to compare.

## Source and license

See [release notes](docs/RELEASE_NOTES_v0.4.0.md), [building from source](docs/BUILDING.md), [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md).
