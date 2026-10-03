# Resolve DLSS5 0.4.0

简体中文 | [English](README.md)

为 DaVinci Resolve 提供 DLSS Neural Rendering，支持 SDR/HDR 细节、色彩与保护控制，最多三次 NR，以及光流估算和外部运动输入。0.4.0 为实验版本。

## 下载

访问 [GitHub Releases](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/releases)。

安装请下载 `ResolveDLSS5-0.4.0-win64.zip`。源码使用发布页面自带的 **Source code (zip)** 或 **Source code (tar.gz)**。

插件包包含 OFX bundle、社区 DLSSNR 运行库、安装/回退脚本、使用说明和许可文件。

## 运行条件

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows x64 |
| Resolve | 版本目标：20.0.1 build 6 及以上 |
| GPU | RTX 40 或 RTX 50 系 |
| 运行库 | 随包提供社区修改版 `nvngx_dlssnr.dll` 310.8.0.0，也可选择其他兼容 Feature 18 的运行库 |

参考环境：Resolve Studio 20.0.1.6、RTX 5070 Ti、NVIDIA 驱动 616.92。GPU 初始化还取决于所安装的驱动与兼容运行库。

## 安装

解压插件包，在其中的 `0.4.0` 目录打开 PowerShell，使用随包运行库预览安装：

```powershell
.\Install.ps1 -Validation
```

关闭 Resolve，在管理员 PowerShell 中增加 `-Apply` 执行安装：

```powershell
.\Install.ps1 -Validation -Apply
```

选择其他兼容运行库时，在安装命令中增加 `-RuntimeDll <路径>`。

安装目标为 `C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`。脚本会备份原插件并输出 `receipt.json` 路径。保留收据，重启 Resolve，从效果库添加 **DLSS Neural Video Experimental**。

恢复原插件时，关闭 Resolve，使用本次安装的收据预览：

```powershell
.\Restore.ps1 -Receipt 'C:\path\to\receipt.json'
```

检查预览后，在管理员 PowerShell 增加 `-Apply`。详见[安装说明](docs/RELEASE_INSTALL.md)。

## 快速开始

1. 按效果节点实际收到的数据选择 **Input Encoding**。
2. 从 **1 Pass** 开始，调整 **Overall Strength** 与 **Output Mix**。
3. 使用 **Chroma Strength**、**Lightness**、**Shadow/Structure**、**Highlight/Glow** 控制总修正，开启 **Advanced** 调整保护、压缩与频率。
4. 选择 **Flow Method** 和质量，默认 **NVOF / Quality**。**None** 使用零运动，NR 继续处理。
5. 根据画面和处理时间选择更多 Pass 或更高光流质量。

所有 Pass 完成后，将总修正合成到原始 float 图像；细节与保护参数统一调整这一总修正。

## 主要参数

| 参数 | 用途 |
| --- | --- |
| Overall Strength / Output Mix | 总修正强度与原图混合 |
| Chroma Strength / Lightness | 色度与明度变化 |
| Shadow/Structure / Highlight/Glow | 变暗与变亮贡献 |
| Advanced | 展示色相/暗部/亮部保护、Soft Compression、Low Frequency、High Frequency |
| NR Pass Count | 选择 1–3 次 NR，每次分别设置风格、强度、色调、结构、皮肤与遮罩 |
| Flow Method | 选择 None、AMDOF 或 NVOF |
| External Motion | 读取 Fusion MotionVectors，跳过内部估算 |
| Output View | Processed、Difference x10、Split View |
| Reset NR | 在下一次渲染重置时序历史 |

隐藏的 Pass 参数保留数值。高分辨率、更多 Pass、进阶保护/频率与更高光流质量会增加处理时间。

## SDR 与 HDR

Input Encoding 提供 sRGB、Rec.709 gamma 2.4、Rec.2020 PQ/HLG、线性 Rec.709 和线性 Rec.2020，默认 sRGB。按效果节点在色彩流程中的实际编码选择。

HDR 的 **White (nits)** 为参考白，**Peak (nits)** 为参考峰值，默认 203 / 1000 nits，设置 Peak ≥ White。线性 HDR 的 1.0 表示参考白；**White Scale** 与 **HDR Strength** 用于调整 HDR 代理及其贡献。

## Fusion 运动输入

将 float RGBA 运动图连接到 **MotionVectors**，开启 **External Motion**。数据方向为当前帧到前一帧，尺寸、bounds、像素宽高比与原图一致；默认 R=X、G=Y、源像素单位，X 向右、Y 向下为正。

面板提供 X/Y Channel、Vector Units、Positive Y Up 和 X/Y Scale。命名 Vector/AOV 或 EXR 通道在上游映射为 RGBA，并保留运动图的有符号原始数据。详见[运动输入设置](docs/MOTION_VECTORS.md)。

## 播放与旧工程

连续播放保留 NR 历史；跳转、倒放、活动 NR 参数变化时重置。因此冷启动的部分导出与完整顺序导出可能有画面差异。**Reset NR** 可手动触发重置。

0.3.x 工程沿用已有参数值。新版净修正与 HDR 处理会改变效果外观，可先用工程副本，结合 Output Mix / Difference x10 对比。

## 源码与许可

参见[发布说明](docs/RELEASE_NOTES_v0.4.0.md)、[源码构建](docs/BUILDING.md)、[LICENSE](LICENSE) 与[第三方许可](THIRD_PARTY_NOTICES.md)。
