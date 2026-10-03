# Resolve DLSS5 0.4.0

## 简体中文

0.4.0 增加多次 NR、SDR/HDR 总修正控制、光流估算与 Fusion 外部运动输入，并优化后处理效率与参数面板。

### 新功能

- **细节与保护**：明度、色度、变暗/变亮、色相/暗部/亮部保护、软压缩及低/高频控制，统一调整最终 NR 总修正。
- **SDR/HDR**：支持 sRGB、Rec.709 gamma 2.4、Rec.2020 PQ/HLG、线性 Rec.709/2020；HDR 可调参考白、峰值与处理强度。
- **1–3 次 NR**：每次独立设置风格、强度、色调、结构、皮肤与遮罩；隐藏参数保留数值。
- **光流估算**：AMDOF 两档、NVOF 五档，默认 NVOF / Quality。
- **Fusion 外部运动**：MotionVectors 接收当前到前一帧的 float RGBA 运动图；External Motion 跳过内部估算，可换算通道、单位、Y 方向与倍率。
- **诊断视图**：Processed、Difference x10、Split View，配合 Output Mix 比较处理效果。

### 改进与修复

- 以原始 float 图像为底图合成 NR 净修正，改进色彩与 HDR 处理。
- 使用相邻帧光流与流式 NR，优化跳转时的取帧方式。
- 共享推理资源并复用最新帧 NR 结果，提升细节、保护与频率计算效率。
- 修复部分预览尺寸的图像读回错误与参数面板加载问题。
- 缩短过长的参数名称、质量选项与重置按钮，统一各 Pass 的名称。

### 使用

Windows x64，Resolve 版本目标 20.0.1 build 6 及以上，GPU 面向 RTX 40/50。安装前准备匹配哈希的 `nvngx_dlssnr.dll` 310.8.0.0；安装步骤与参数用法见 README。

从 1 Pass、默认细节开始，再根据素材调整。连续播放保留时序历史，跳转/倒放或活动 NR 参数变化会重置；冷启动的部分导出与完整顺序导出可能有画面差异。高分辨率、更多 Pass、进阶处理与更高光流质量会增加处理时间。

## English

0.4.0 adds multiple NR passes, SDR/HDR correction controls, optical flow and Fusion motion input, with improvements to postprocessing efficiency and panel labels.

### Features

- Lightness/chroma, darkening/brightening, hue/shadow/highlight protection, soft compression and low/high-frequency controls for the final combined NR correction.
- sRGB, Rec.709 gamma 2.4, Rec.2020 PQ/HLG and linear Rec.709/2020, with adjustable HDR reference white, peak and strength.
- 1–3 NR passes with separate style, intensity, tone, structure, skin and mask settings; hidden values are retained.
- Two AMDOF and five NVOF quality profiles, defaulting to NVOF / Quality.
- A Fusion MotionVectors float RGBA input for current-to-previous motion. External Motion skips estimation and provides channel, unit, Y-axis and scale conversions.
- Processed, Difference x10 and Split View, plus Output Mix for comparison.

### Improvements

- Composite the NR correction over the original float image, with updated color and HDR processing.
- Use adjacent-frame optical flow and streaming NR for seeking.
- Share inference resources, reuse the latest NR output and improve detail/protection/frequency computation efficiency.
- Fix image readback at some preview sizes and parameter-panel loading.
- Shorten long parameter labels, quality options and the reset button; use consistent pass names.

### Usage

Windows x64, Resolve version target 20.0.1 build 6 or later, and RTX 40/50 GPUs. Prepare `nvngx_dlssnr.dll` 310.8.0.0 with the required checksum and follow README for installation and controls.

Start with one pass/default detail. Sequential playback retains temporal history; seeking, reverse playback and active NR changes reset it, so cold partial exports can differ from full sequential exports. Higher resolution, additional passes, advanced processing and higher-quality flow increase processing time.
