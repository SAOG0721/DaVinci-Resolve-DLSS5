# Changelog

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
