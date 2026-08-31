# Third-party notices and binary boundary

Resolve DLSS5 Experimental is distributed under the MIT License.

The Feature 18 runtime adapts DLSSNR integration work originally authored by
Jay for the experimental fork of [Magpie](https://github.com/SAOG0721/Magpie).
That implementation is separately released here by its author under MIT. This
does not change the GPL-3.0 license of Magpie or relicense code owned by other
Magpie contributors.

The source repository does not vendor the following dependencies:

- Blackmagic Design DaVinci Resolve/OpenFX SDK.
- NVIDIA NGX/DLSS SDK headers and static loader library.
- NVIDIA display-driver components.
- `nvngx_dlssnr.dll` or any other NVIDIA/community binary.

The plugin binary incorporates the OFX Support Library under its BSD-3-Clause
terms. Its required notice is included at
[`docs/licenses/OFX-Support-Library-BSD-3-Clause.txt`](docs/licenses/OFX-Support-Library-BSD-3-Clause.txt)
and is copied into every binary package.

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

For the v0.3.0 and v0.3.1 experimental binary packages:

- File version: `310.8.0.0`
- SHA-256: `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`
- The file is community-modified and must not be treated as an untouched or
  officially signed NVIDIA binary. Its Authenticode file hash may not match.

DaVinci Resolve, OpenFX, NVIDIA, DLSS, RTX, NGX, and CUDA are trademarks or
registered trademarks of their respective owners. This project is unofficial
and is not endorsed by Blackmagic Design or NVIDIA.
