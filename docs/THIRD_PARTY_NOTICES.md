# Third-party notices and binary boundary

Resolve DLSS5 Experimental is distributed under the MIT License.

The source build helper `src/scripts/Generate-FidelityFXOpticalFlowShaders.ps1`
is adapted from the local SAOG0721/Magpie tree and retains GPL-3.0-only;
see [GPL text](licenses/Magpie-Build-Helper-GPL-3.0.txt). This helper is not
included in the binary distribution. Its generated shader programs are AMD
FidelityFX SDK material under AMD's MIT terms, not the helper's license.

The Feature 18 runtime adapts DLSSNR integration work originally authored by
Jay for the experimental fork of [Magpie](https://github.com/SAOG0721/Magpie).
That implementation is separately released here by its author under MIT. This
does not change the GPL-3.0 license of Magpie or relicense code owned by other
Magpie contributors.

The source repository does not vendor the following dependencies:

- Blackmagic Design DaVinci Resolve/OpenFX SDK.
- NVIDIA NGX/DLSS SDK headers and static loader library.
- NVIDIA display-driver components.
- AMD FidelityFX SDK and NVIDIA Optical Flow SDK headers.
- `nvngx_dlssnr.dll` or any other NVIDIA/community binary.

The plugin binary incorporates the OFX Support Library under its BSD-3-Clause
terms. Its required notice is included at
[`licenses/OFX-Support-Library-BSD-3-Clause.txt`](licenses/OFX-Support-Library-BSD-3-Clause.txt)
and is copied into every binary package.

Optical flow incorporates AMD FidelityFX SDK v2.3.0 optical-flow/backend code
and generated DXIL under [AMD MIT terms](licenses/AMD-FidelityFX-MIT.txt).
The NVIDIA Optical Flow CUDA API headers use
[BSD-3-Clause terms](licenses/NVIDIA-OpticalFlow-BSD-3-Clause.txt).
Both notices are included in binary packages. Driver `nvofapi64.dll` is loaded
from Windows System32 and is never copied or redistributed by this project.
The shader helper and SDKs were read from the user's existing local Magpie
dependencies on 2026-10-03; their original download dates are not established.

The plugin binary also incorporates NVIDIA NGX/DLSS SDK loader material. That
material remains governed by the NVIDIA RTX SDK License, not MIT. Every binary
package includes the SDK license obtained from the build dependency and the
required notice: “This software contains source code provided by NVIDIA
Corporation.”

The tagged GitHub Release may contain a separately supplied, community-modified
`nvngx_dlssnr.dll` as a runtime component. That DLL is not licensed under MIT
and is not part of the Git source tree or GitHub's automatically generated
source archives. Users must independently ensure that their receipt and use of
that runtime are authorized.

The 0.4.0 binary package includes the same community-modified runtime as
v0.3.1-experimental, file version 310.8.0.0. The runtime retains its applicable
NVIDIA terms and is separate from the MIT-licensed plugin source. Users may
select another compatible, authorized DLSSNR runtime during installation.
The installer records the selected file's checksum for copying and rollback.
Runtime provenance is recorded in docs/runtime-dependencies.md.

For the v0.3.0 and v0.3.1 experimental binary packages:

- File version: `310.8.0.0`
- SHA-256: `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`
- The file is community-modified and must not be treated as an untouched or
  officially signed NVIDIA binary. Its Authenticode file hash may not match.

DaVinci Resolve, OpenFX, NVIDIA, DLSS, RTX, NGX, and CUDA are trademarks or
registered trademarks of their respective owners. This project is unofficial
and is not endorsed by Blackmagic Design or NVIDIA.
