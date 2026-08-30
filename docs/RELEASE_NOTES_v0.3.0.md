# Resolve DLSS5 Experimental v0.3.0

首个已在 DaVinci Resolve 宿主内验证生效的 Windows x64 实验版本。

## 下载与安装

1. 下载 `Resolve-DLSS5-Experimental-x64.zip` 并完整解压。
2. 退出 DaVinci Resolve。
3. 把 `ResolveDlss5.ofx.bundle` 整个目录复制到
   `C:\Program Files\Common Files\OFX\Plugins\`。
4. 重启 Resolve，在 `DLSS Experimental` 分类下添加
   `DLSS Neural Video Experimental`。

不要把 DLL 移入 Resolve 安装目录，也不要覆盖系统或游戏中的 NGX DLL。

## 当前实现

- 同分辨率 DLSS Neural Rendering；不放大、不插帧。
- 原生 OpenFX Filter/General 上下文，可用于 Edit、Color 和 Fusion 效果链。
- 暂使用 Zero Motion / Zero Depth；参数类型参考 RenoDX/RenderShade add-on。
- 提供 `Difference x10` 与 `Left / Right Compare`，便于确认细微效果。
- 初始化或 Evaluate 失败时自动返回原图。
- Resolve 多实例共享兼容 hook，NGX 操作全局串行化。

该版本仍是 CPU float RGBA 与 D3D12 RGBA8 之间同步上传/读回的功能桥，
预览性能不适合生产工作流。SDR 是已验证路径；HDR/PQ 仍属实验。

## 社区 DLL 说明

Release ZIP 内的 `nvngx_dlssnr.dll` 是社区修改版，并非 NVIDIA 未修改官方文件。

- 文件版本：`310.8.0.0`
- SHA-256：`984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`
- 修改后的文件可能无法通过原始 Authenticode 文件哈希校验。

请仅在确认拥有相应使用权限且理解实验风险时使用。

## 源码边界

本 Release 对应源码位于 `v0.3.0-experimental` 标签。第三方 SDK、静态库和
`nvngx_dlssnr.dll` 均不包含在 Git 源码树或 GitHub 自动生成的源码归档中；
社区 DLL 只存在于手工上传的二进制 Release 资产中。

## English summary

This first host-verified build provides an unofficial, same-resolution DLSS
Neural Rendering OpenFX filter for DaVinci Resolve. The release ZIP includes a
separate community-modified runtime; no DLL is stored in Git or GitHub source
archives. The implementation is experimental, zero-guidance, Windows x64 only,
and falls back to the source frame on failure.
