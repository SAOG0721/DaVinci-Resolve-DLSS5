# Runtime dependencies

The 0.4.0 binary package includes community-modified `nvngx_dlssnr.dll`
310.8.0.0, the same runtime published in
[v0.3.1-experimental](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/releases/tag/v0.3.1-experimental).

Provenance: reused from the existing local plugin installation and verified
against the referenced release on 2026-10-04. The bundled runtime has the same
file bytes as that release's documented community DLL. Its applicable NVIDIA
terms and separate community-modified status are recorded in
[third-party notices](../THIRD_PARTY_NOTICES.md); the binary bundle includes
NVIDIA-RTX-SDK-LICENSE.txt. The plugin source uses the repository's MIT license.

Installation uses the bundled runtime by default. `-RuntimeDll <path>` selects
another compatible, authorized Feature 18 runtime. The selected file's checksum
is recorded for file-copy and rollback integrity. Real NGX tests use the runtime
selected for the build and report its identity.

See [installation](RELEASE_INSTALL.md) and [README](../README.md). NVOF uses the
installed NVIDIA driver's API from System32.
