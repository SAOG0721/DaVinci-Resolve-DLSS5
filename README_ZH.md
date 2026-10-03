# Resolve DLSS5 Experimental

简体中文 | [English](README.md)

用于 DaVinci Resolve 的 Windows x64 OpenFX 同分辨率 DLSS Neural Rendering 效果。**0.4.0 revision 5** 增加 SDR/HDR 总修正控制、最多三次 NR、光流估算与 Fusion 外部运动输入。

当前为**实验验证版**，宿主与性能验收尚未完成。本项目非官方，与 Blackmagic Design 或 NVIDIA 无隶属或背书关系。变化与未完成部分见[发布说明](docs/RELEASE_NOTES_v0.4.0.md)。

## 下载与运行条件

本次准备的分发文件为 `ResolveDLSS5-0.4.0-win64.zip`，附 `.sha256.txt` 校验文件。已发布版本见 [GitHub Releases](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/releases)。包内包含插件、安装/回退脚本、许可与验证报告，**不含社区 `nvngx_dlssnr.dll`、SDK 或显卡驱动 DLL**。

| 项目 | 当前范围 |
| --- | --- |
| 系统 | Windows x64 |
| Resolve | 最低目标 20.0.1 build 6；Studio 20.0.1.6 注册与参数往返已测 |
| 显卡 | 目标 RTX 40/50 系；真实独立 GPU 测试使用 RTX 5070 Ti |
| 驱动 | 测试机为 NVIDIA 616.92，其他版本待测 |
| 社区运行库 | 自行取得且获授权的 `nvngx_dlssnr.dll` 310.8.0.0，须匹配下列哈希 |

运行库 SHA256：

```text
984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014
```

指定 DLL 的 FP8 兼容方案面向 40/50 系。插件没有显卡型号白名单，仍要求可用的 NVIDIA/D3D12 设备和成功的 NGX 初始化。40 系、Resolve Free、更高宿主版本、多显卡流程尚未完成实测。

## 安装与回退

解压分发包，在解压后的 `0.4.0` 目录运行下列命令，指定自己获授权的 DLL；默认只预览：

```powershell
.\Install-Development.ps1 -Validation -RuntimeDll 'C:\path\to\nvngx_dlssnr.dll'
```

关闭 Resolve，在管理员 PowerShell 中增加 `-Apply` 才执行安装：

```powershell
.\Install-Development.ps1 -Validation -RuntimeDll 'C:\path\to\nvngx_dlssnr.dll' -Apply
```

固定目标为 `C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`。脚本检查插件与运行库哈希，备份原有 bundle，并输出 `receipt.json` 路径。保留该收据，安装后重启 Resolve。本版不带 `-Validation` 的安装被阻止。

回退时关闭 Resolve，使用**本次安装**输出的收据：

```powershell
.\Restore-Development.ps1 -Receipt 'C:\path\to\receipt.json'
# 检查预览后，在管理员 PowerShell 增加 -Apply。
```

详见[安装说明](docs/RELEASE_INSTALL.md)，不要混用不同安装的收据。

## 使用

从效果库添加 **DLSS Neural Video Experimental**，或在 Fusion 使用对应 OFX 节点。先以 1 Pass、默认细节试用，再调整 Overall Strength、Mix、Chroma、Lightness、Shadow/Structure、Highlight/Glow。Show Advanced 展示色相/暗部/亮部保护、软压缩、低/高频控制。增加 Pass Count 后显示 Pass 2/3；隐藏参数保留数值。

输出以原始 float 图像为底图，将最终 NR 输出减去**实际量化推理输入的解码基准**后得到总修正。多次 NR 完成后统一调整总修正。神经代理仍为 RGBA8；FP8 指运行库模型兼容，不代表图像纹理精度。

Input Encoding 必须符合**节点实际收到的数据**：sRGB、Rec.709 gamma 2.4、Rec.2020 PQ/HLG、线性 Rec.709 或线性 Rec.2020。不能仅根据 HDR 导出设置选择 PQ。线性 HDR 的 1.0 表示参考白；默认参考白 203 nits、峰值 1000 nits。当前无自动识别，尚无 Log/DWG/ACES 原生模式。

内部光流提供 **None / AMDOF / NVOF**，按方法显示对应质量选项，默认 NVOF / Quality。估算最多需要当前与前一源帧。None 使用零运动，仍执行 NR；深度仍置零。

Fusion 中可以向 **MotionVectors** 接入同尺寸 float RGBA 数据图，开启 **Use External Motion (Skip Estimation)** 跳过内部估算。方向须为**当前帧到前一帧**，默认 R/G、源像素单位、X 向右与 Y 向下为正，可设置通道、UV 单位、Y 方向与倍率。原生 Vector/AOV 平面需先映射为 RGBA，详见[运动数据接口](docs/MOTION_VECTORS.md)。

## 验证与性能

五组独立测试通过：真实运行库、CPU/色彩控制、流式 NR、GPU 合同与光流。Resolve 参数 API 检查了含隐藏兼容 ID 的 63 个控件/动作、9 个组、设置往返及 MotionVectors 连接。**r5 的 headless 宿主检查没有渲染 OFX 帧，不能据此确认时间线导出通过**，手测见[验收清单](tests/MANUAL-ACCEPTANCE.md)。

Ryzen 9 9950X3D / RTX 5070 Ti 上的 1080p 合成测试：默认 CPU 后处理约 128→19 ms，保护约 494→70 ms，保护加频率约 809→113 ms。这是独立阶段计时，不能换算为达芬奇 FPS。复杂 4K 后处理仍约 459 ms；GPU 后处理、Resolve CUDA 图像桥尚未实现。

## 已知限制

- 跳转、倒放或活动推理参数变化会重置 NR 历史，冷启动的部分导出可能与完整顺序导出不同。全历史取帧与重放已移除。
- 输入与推理签名一致时，最新帧 NR 缓存支持出口调参复用；宿主序列边界可能使缓存失效，真实调参响应仍待验证。
- 后处理、宿主图像传输仍使用 CPU，SDK 调用串行；缓存/资源池预算不是进程总显存上限。
- 五模式抗闪烁、双向光流/置信度、推理尺寸缩放、磁盘顺序准备缓存、Pass 后缀复用尚未实现。
- GPU 取消与 device-lost 回收尚未完整实现，既有 NR fence 等待仍可能阻塞。
- Legacy 界面已移除，旧 ID 隐藏保留以兼容设置。净修正和 HDR 变化可能改变旧工程外观，应在工程副本比对。

## 源码与许可

构建需要 Visual Studio 2022、CMake、CUDA、NGX、Resolve OpenFX SDK、FidelityFX v2.3.0、NVIDIA Optical Flow SDK 与 DXC，详见[构建说明](docs/BUILDING.md)。依赖与社区 DLL 均由使用者在外部提供；源码包不含二进制、缓存或媒体，测试图案由代码生成。

主体源码采用 MIT，shader 构建辅助脚本单独保留 GPL-3.0-only，依赖各遵循自己的许可。见 [LICENSE](LICENSE) 与[第三方说明](THIRD_PARTY_NOTICES.md)。实现参考作者的 [Magpie DLSSNR](https://github.com/SAOG0721/Magpie)。
