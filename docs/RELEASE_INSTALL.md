# Resolve DLSS5 Experimental v0.3.1

This is an unofficial Windows x64 OpenFX build for personal, noncommercial,
non-profit experimentation. It applies DLSS Neural Rendering at the input
resolution; it does not upscale and does not generate frames.

## Install

1. Close DaVinci Resolve.
2. Extract the ZIP completely.
3. Copy `ResolveDlss5.ofx.bundle` to
   `C:\Program Files\Common Files\OFX\Plugins\`.
4. Start Resolve and add `DLSS Neural Video Experimental` from the
   `DLSS Experimental` group.

Keep the complete bundle layout. In particular, do not move
`Contents\Win64\runtime\nvngx_dlssnr.dll` into Resolve's own installation
directory and do not replace any system or game NGX DLL.

To uninstall, close Resolve and remove only
`C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`.

## Confirm that processing is active

Set `Output View` to `Left / Right Compare` or `Difference x10`. Runtime events
are logged to `%LOCALAPPDATA%\ResolveDlss5\ResolveDlss5.log`. Any initialization
or evaluation failure falls back to the source frame.

## Runtime notice

The packaged `nvngx_dlssnr.dll` is a community-modified runtime, version
310.8.0.0, SHA-256
`984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`.
It is not an untouched NVIDIA official binary; Authenticode may report a hash
mismatch. Use it only if you are authorized to do so and accept the risk.

The plugin source is MIT licensed and is available from the Git tag for this
release. The community DLL is separate and is excluded from the Git repository
and automatically generated source archives.
