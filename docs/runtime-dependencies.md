# Runtime dependencies

0.4.0 requires an independently obtained, authorized community `nvngx_dlssnr.dll`
310.8.0.0. It is not included in the binary ZIP, Git tree or source ZIP.

SHA-256: `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`.

The installer does not download a DLL and rejects a different hash. Historical
0.3.x assets do not establish new redistribution rights. See
[installation](RELEASE_INSTALL.md), [README](../README.md) and
[notices](../THIRD_PARTY_NOTICES.md). NVOF loads the installed driver API from
System32; no display-driver DLL is copied by this project.
