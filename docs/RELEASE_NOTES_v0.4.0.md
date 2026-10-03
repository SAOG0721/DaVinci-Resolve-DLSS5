# 0.4.0 — Experimental validation prerelease

Suggested tag: `v0.4.0`. Binary version: `0.4.0`; package revision: `5`. This document prepares a prerelease and does not assert that it has been published.

## 简体中文

相比 0.3.1，本版把单次 NR 扩展为可控的 SDR/HDR 总修正工作流，接入多次 NR、光流与外部运动数据，并移除导致大量宿主预取的全历史依赖。

### 新增与改进

- **SDR/HDR 总修正**：以原始 float 图像为底图，减去实际量化输入的解码基准后再合成 NR 净变化；支持 sRGB、Rec.709 gamma 2.4、Rec.2020 PQ/HLG、线性 Rec.709/2020。
- **细节与保护控制**：Oklab 明度/色度、变暗/变亮、色相/暗部/亮部保护、软压缩、互补低/高频控制，统一作用于所有 NR 完成后的总修正。
- **1–3 Pass NR**：每次独立参数与历史，GPU 链内完成，一次入口/读回；新增层按需显示，隐藏值保留。
- **真实光流**：AMDOF 两档、NVOF 五档；默认 NVOF / Quality。内部仅依赖当前与前一源帧；所有 NR Pass 共用运动。
- **Fusion 外部运动**：MotionVectors 可选 float RGBA 输入，开启 Use External Motion 跳过估算；可换算通道、像素/UV 单位、Y 方向与倍率。要求当前到前一帧，原生 Vector/AOV 需上游映射。
- **流式与缓存**：移除全片起点重放，连续帧延续模型状态，跳转/倒放重置；签名一致时复用最新 NR 输出。共享 D3D12/NGX 会话与 fence 约束资源池。
- **参数面板**：修复可选 OFX sequential 属性导致参数不可用；基本/进阶、SDR/HDR、Pass、光流质量按条件展示。Legacy 分组移除，旧 ID 隐藏保留。
- **r5 性能与读回修复**：有界 CPU 并行、默认合成循环融合、零运动批量清除；修复未对齐宽度末行 padding 导致的 Map 越界，480×270 与 288×162 实机独立测试通过。新增 Render 阶段/cache/reset 计时。

### 验证与已知限制

五组独立自动化测试通过，七个光流档位的已知平移中值为 (-4,-2)，真实三次 NR 接收运动后输出有限。Resolve Studio 20.0.1.6 参数 API 的 63 控件/动作（含隐藏 ID）、9 组、设置往返、MotionVectors 连接通过；r5 的 headless 宿主测试未完成 OFX Render，时间线播放/导出和真实调参性能仍待手测。

1080p 合成测试 CPU 后处理：默认约 128→19 ms，保护约 494→70 ms，保护加频率约 809→113 ms；这些是独立阶段计时，不是达芬奇 FPS。复杂 4K 后处理仍约 459 ms。GPU 后处理与 CUDA 图像桥仍待实现。

尚无五模式抗闪烁、双向流/置信度、推理尺寸缩放、顺序准备/磁盘缓存或 Pass 后缀复用。冷跳/部分导出不保证与完整顺序导出一致。完整取消/device-lost 回收未完成，GPU 等待仍可能阻塞。40 系、更高 Resolve、Free 版和多显卡未完成验收。

### 安装与迁移

Windows x64，最低 Resolve 目标 20.0.1 build 6，目标 GPU 为 RTX 40/50。分发包不含社区 DLL、SDK 或驱动；自行提供获授权的 `nvngx_dlssnr.dll` 310.8.0.0。先运行包内 `Install-Development.ps1 -Validation -RuntimeDll <路径>` 预览，关闭 Resolve 后，在管理员 PowerShell 增加 `-Apply`。保留安装输出的回退收据。0.3.x 工程可能因净修正/HDR 变化改变外观，应先在副本比对。

## English

This prerelease expands the 0.3.1 single-pass experiment into a controllable SDR/HDR correction workflow, with multiple NR passes, optical flow, external motion input, and bounded source-frame dependencies.

### Changes

- Composite the final NR correction onto the original float image after subtracting the decoded, actually quantized inference input. Support sRGB, Rec.709 gamma 2.4, Rec.2020 PQ/HLG and linear Rec.709/2020.
- Apply Oklab lightness/chroma, darkening/brightening, hue/shadow/highlight protection, soft compression and complementary frequency controls once to the final correction.
- Run 1–3 GPU NR passes with separate parameters/history and one upload/readback boundary. Keep hidden pass values.
- Add two AMDOF and five NVOF quality profiles, defaulting to NVOF / Quality. Estimate from the current/previous frame only and share motion across NR passes.
- Add an optional Fusion MotionVectors float RGBA input and a switch to skip estimation. Adapt channels, pixel/UV units, Y direction and scales. Require current-to-previous motion; named Vector/AOV planes need upstream mapping.
- Remove all-history source fetching/replay. Continue sequential NR state, reset on seek/reverse, and reuse the latest output when its signature matches. Share D3D12/NGX resources with fence-governed leases.
- Fix unavailable parameters caused by an unsupported optional OFX property. Add conditional basic/advanced, HDR, pass and flow controls. Remove Legacy groups while retaining hidden compatibility IDs.
- In r5, add bounded CPU parallelism, fused default compositing, bulk zero-motion clearing and stage/cache/reset timing. Fix padded readback ranges for unaligned widths; standalone 480×270 and 288×162 GPU checks pass.

### Validation and limitations

Five standalone test groups pass. Known translation produces median (-4,-2) in all seven flow profiles, and real three-pass NGX output remains finite with motion input. Resolve Studio 20.0.1.6 parameter round trips and the MotionVectors link pass. The r5 headless host check did not render an OFX frame; timeline playback/export and native adjustment performance remain pending.

Synthetic 1080p CPU postprocessing changes from approximately 128 to 19 ms for defaults, 494 to 70 ms for protection and 809 to 113 ms for protection plus frequency controls. These stage timings do not represent Resolve FPS. Complex 4K postprocessing still takes about 459 ms.

GPU postprocessing, the CUDA image bridge, five-mode antiflicker, bidirectional flow/confidence, inference scaling, disk preparation cache and pass-suffix reuse remain unimplemented. Cold partial exports may differ from full sequential exports. Cancellation/device-loss recovery is incomplete and GPU waits can block. RTX 40, newer Resolve, Free edition and multi-GPU acceptance are pending.

### Installation

The ZIP excludes community runtimes, SDKs and drivers. Supply an independently obtained, authorized `nvngx_dlssnr.dll` 310.8.0.0 with the required checksum. Preview with `Install-Development.ps1 -Validation -RuntimeDll <path>`, close Resolve and add `-Apply` in administrator PowerShell. Keep the rollback receipt. Compare 0.3.x projects on a copy because the correction/HDR changes can alter their appearance.

## Assets / 附件

- `ResolveDLSS5-0.4.0-win64.zip`
- `ResolveDLSS5-0.4.0-source.zip`
- Corresponding `.sha256.txt` files / 对应校验文件

Binary plugin SHA-256: `FF456267C516CFBCA13C86240C436BF54E43E0D674B1EA560A39758C7AD32655`.

Runtime SHA-256: `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`.
