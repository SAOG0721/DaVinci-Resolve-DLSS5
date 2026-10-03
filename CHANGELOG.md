# Changelog

## [0.4.0] - 2026-10-03

Experimental validation build, based on revision 5. See the complete
[bilingual release notes](docs/RELEASE_NOTES_v0.4.0.md).

- Add original-float net correction, explicit SDR/HDR encoding, Oklab detail,
  protection, compression and frequency controls.
- Add independent 1–3-pass NR, shared D3D12/NGX resource leases and latest-frame
  result reuse.
- Add AMDOF/NVOF and an optional Fusion float RGBA MotionVectors input that
  bypasses estimation. Internal flow needs at most the current/previous frame.
- Remove all-history replay/prefetch and Legacy UI groups; retain hidden IDs.
- Fix unsupported optional OFX properties, unaligned output readback ranges,
  and add bounded CPU parallelism, fused compositing and stage/cache timings.
- Five standalone test groups and native parameter round trips pass. Actual
  r5 OFX timeline render/export and real-content performance remain pending.
- Antiflicker, bidirectional confidence, GPU post/CUDA bridge, inference
  scaling, disk preparation cache and complete fault recovery remain planned.
- Binary and source archives exclude community runtimes, SDKs and drivers.

All notable project changes are documented here.

## 0.3.1-experimental - 2026-08-31

- Normalized OpenFX bottom-left row order to D3D12 top-left texture order on
  upload and readback.
- Fixed the DLSS debug indicator appearing in the upper-left instead of the
  lower-left, without changing the final image orientation.

## 0.3.0-experimental - 2026-08-30

- Added a native Windows x64 OpenFX filter for DaVinci Resolve.
- Added same-resolution Feature 18 processing with zero motion/depth guidance.
- Exposed the recovered RenoDX/RenderShade-style neural rendering controls.
- Added source-frame fallback, persistent diagnostics, difference view, and split view.
- Fixed Resolve multi-instance operation with a process-level reference-counted IAT hook.
- Added a two-live-runtime Feature 18 smoke test and verified Resolve-host processing.
