# Resolve DLSS5 0.4.1

## 简体中文

本次更新新增可选抗闪烁，并改善 Fusion 的帧率处理、外部运动输入提示与节点恢复。

- **五种抗闪烁模式**：Off、Static、Flow、Flow+、Low Frequency，默认 Off。适用于 SDR/HDR，可通过 Strength 和 Response (ms) 调整稳定强度与响应时间。
- **灵活的运动来源**：复用 NR 光流或 Fusion MotionVectors；NR 的 Flow Method 为 None 时，可通过 AF Flow 单独选择抗闪烁光流。
- **Fusion 帧率控制**：Frame Rate 默认 0，自动获取有效宿主帧率，也可填写合成或媒体的实际 FPS。关闭抗闪烁时直接使用当前帧修正。
- **外部运动连接提示**：Motion Input 显示连接状态，External MV 使用 MotionVectors 输入。修正连接或关闭 External MV 后可恢复处理。
- **连续播放与调参**：按媒体时间累积抗闪烁历史；跳转、倒放或相关参数变化时重置。同帧调整细节、保护和抗闪烁参数时复用 NR 输出。

### 下载与安装

下载 **ResolveDLSS5-0.4.1-win64.zip**，包含插件、社区修改版 DLSSNR DLL、安装与回退脚本，以及安装说明。

关闭 Resolve，解压后在 `0.4.1` 目录打开管理员 PowerShell，执行：

```powershell
.\Install.ps1 -Validation -Apply
```

重启 Resolve，从效果库添加 **DLSS Neural Video Experimental**。

### 使用建议

- 抗闪烁从 Static 或 Flow 开始，Response 默认 80 ms，按素材调整。
- 跳过 NR 光流估算：**External MV=Off / Flow Method=None**。需要同时跳过抗闪烁光流时，选择 Off 或 Static。
- 使用外部光流：连接 **MotionVectors** 后开启 **External MV**。
- 在 Fusion 中可将 **Frame Rate** 设置为合成或媒体的实际 FPS。

适用于 Windows x64，Resolve 20.0.1 build 6 及以上，计划支持 RTX 40/50 系列。0.4.1 为实验版本。

详见 [README](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/blob/v0.4.1/README_ZH.md)、[抗闪烁参数](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/blob/v0.4.1/docs/ANTI_FLICKER.md)和[运动输入设置](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/blob/v0.4.1/docs/MOTION_VECTORS.md)。源码使用发布页面自带的 **Source code** 下载。

## English

This update adds optional anti-flicker and improves Fusion frame-rate handling, external motion connection feedback and node recovery.

- **Five anti-flicker modes:** Off, Static, Flow, Flow+ and Low Frequency, defaulting to Off. Strength and Response (ms) control stabilization and response time for SDR/HDR.
- **Flexible motion sources:** Reuse NR flow or Fusion MotionVectors. AF Flow selects a separate motion provider when NR Flow Method is None.
- **Fusion frame-rate control:** Frame Rate defaults to 0 for an available host rate, or accepts the actual composition/media FPS. Disabled anti-flicker uses the current correction directly.
- **External motion feedback:** Motion Input displays connection status. Connect MotionVectors and enable External MV; correcting the connection or disabling External MV resumes processing.
- **Playback and adjustments:** Sequential playback accumulates in media time. Seeking, reverse playback and relevant setting changes reset history. Same-frame detail, protection and anti-flicker adjustments reuse NR output.

### Download and install

Download **ResolveDLSS5-0.4.1-win64.zip**, including the plugin, community-modified DLSSNR runtime, installation/restore scripts and instructions.

Close Resolve, extract the ZIP, open administrator PowerShell in its `0.4.1` directory and run:

```powershell
.\Install.ps1 -Validation -Apply
```

Restart Resolve and add **DLSS Neural Video Experimental** from the effects library.

Start anti-flicker with Static or Flow and adjust the default 80 ms response to the footage. To skip NR flow estimation, use External MV=Off and Flow Method=None; Off or Static also skips anti-flicker flow estimation. For external flow, connect MotionVectors and enable External MV. In Fusion, Frame Rate can be set to the actual composition/media FPS.

Windows x64; Resolve 20.0.1 build 6 or later; intended for RTX 40/50 series. Version 0.4.1 is experimental.

See the [README](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/blob/v0.4.1/README.md), [Anti-flicker guide](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/blob/v0.4.1/docs/ANTI_FLICKER.md) and [motion setup](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5/blob/v0.4.1/docs/MOTION_VECTORS.md). Source is available through the release page's **Source code** downloads.
