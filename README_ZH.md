# Resolve DLSS5 Experimental

简体中文 | [English](README.md)

这是一个面向 Windows x64 DaVinci Resolve 的实验性 OpenFX 滤镜，在输入分辨率上应用 DLSS Neural Rendering。它不放大画面，也不生成帧。

> 非官方研究项目，与 Blackmagic Design 或 NVIDIA 没有隶属或背书关系。

## 下载与安装

请从 [GitHub Releases](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/releases) 下载带版本标签的 prerelease。二进制 ZIP 包含完整 OFX bundle 和单独分发的社区运行时；Git 源码树及 GitHub 自动生成的源码归档均不包含 DLL。

1. 关闭 DaVinci Resolve 并完整解压 ZIP。
2. 把 `ResolveDlss5.ofx.bundle` 整个目录复制到 `C:\Program Files\Common Files\OFX\Plugins\`。
3. 重启 Resolve，在 `DLSS Experimental` 分类中添加 `DLSS Neural Video Experimental`。

请保持 bundle 内部结构，不要把社区 DLL 移入 Resolve 安装目录，也不要覆盖系统或游戏中的 NGX DLL。卸载时只删除这个 bundle。

## 当前状态

v0.3.0 是首个已在 Resolve 宿主内确认生效的版本：

- 原生 OpenFX Filter/General，输入输出为 float RGBA。
- D3D12 Feature 18 的创建和输出尺寸与输入相同。
- Motion 为全零 R16G16_FLOAT，Depth 为全零 R32_FLOAT。
- 保留源 Alpha；初始化或 Evaluate 失败时返回原图。
- 参数类型参考 RenoDX/RenderShade add-on 的实现。
- Difference x10 与 Left / Right Compare 可确认细微处理效果。
- Resolve 多实例共享进程级引用计数 hook，NGX 操作全局串行化。
- 运行日志位于 `%LOCALAPPDATA%\ResolveDlss5\ResolveDlss5.log`。

当前仍是功能验证桥：CPU float RGBA 转 RGBA8，上传至插件自建 D3D12 设备，等待 Feature 18 后再读回 CPU。因此预览速度不适合生产流程。

## 编译与测试

源码不内置第三方依赖。需要 Visual Studio 2022 C++/CMake、CUDA Toolkit 12、Resolve Developer/OpenFX SDK，以及 NVIDIA NGX/DLSS SDK。通过环境变量或未提交的 `CMakeUserPresets.json` 配置本机路径：

```powershell
$env:RESOLVE_OFX_SDK_ROOT = 'C:\path\to\DaVinci Resolve\Support\Developer\OpenFX'
$env:DLSS_SDK_ROOT = 'C:\path\to\DLSS'
$env:DLSSNR_RUNTIME_DLL = 'C:\path\to\nvngx_dlssnr.dll'
cmake --preset vs2022-x64
cmake --build --preset release
ctest --test-dir build\vs2022-x64 -C Release --output-on-failure
```

DLSSNR_RUNTIME_DLL 对只编译源码不是必需项，但运行 smoke test 或制作可安装发行包时必须提供经授权的运行时。

## 已知限制

- 仅支持 Windows x64、NVIDIA 与 D3D12。
- 只做同分辨率增强，不包含 Super Resolution 或 Frame Generation。
- SDR/RGBA8 是已验证路径；Linear/scRGB 与 PQ 仍属实验。
- 当前只有 Zero Motion/Depth。
- 同步 CPU/D3D12 往返导致性能较低。
- 社区运行时仅按已确认的个人、非商业、非盈利范围分发。

## 源码与 Release 边界

源码以 MIT 发布，并通过忽略规则排除 DLL、SDK、静态库、构建目录和发行包。带标签的 prerelease ZIP 可以包含单独提供的社区 `nvngx_dlssnr.dll`；其身份、哈希和边界见 [第三方说明](THIRD_PARTY_NOTICES.md)。

Feature 18 部分来自同一作者在 Magpie 实验分支中的实现经验。Magpie 本身仍为 GPL-3.0；本仓库不会改变 Magpie 或其他贡献者代码的许可证。

## 许可证

MIT，见 [LICENSE](LICENSE)。
