# Anti-flicker

适用于 **0.4.1**。

在 **Anti-flicker** 分组选择 **Mode**，稳定各次 NR 与细节／保护控制完成后的总修正。默认 **Off**。

| Mode | 用途 |
| --- | --- |
| Off | 使用当前帧的修正 |
| Static | 在原图稳定区域累积修正；适合固定机位和静止区域 |
| Flow | 使用当前到前一帧的运动，对齐并累积历史修正 |
| Flow+ | 分别稳定修正的持续性与幅度，增强持续变化的连贯性 |
| Low Frequency | 对低频修正进行时域重建，保留当前帧的高频细节补偿 |

**Strength** 控制抗闪烁贡献，范围 0–1，默认 1。**Response (ms)** 调节修正幅度的累积时长，默认 80 ms：较小值更快跟随当前帧，较大值保留更多历史贡献。设置 Strength 为 0 使用当前帧修正，并清除抗闪烁历史。

**Frame Rate** 默认为 0，自动使用有效宿主帧率。Fusion 中可输入合成/媒体的实际 FPS，确保 Response (ms) 按媒体时间工作；时间基准未知时继续输出当前帧 NR 修正。Off 与 Strength=0 直接使用当前修正。

流水线为：各次 NR → 细节与保护 → Anti-flicker → Output Mix／Output View。SDR 与 HDR 共用这些控制，原图亮度余量和 alpha 随最终图像保留。

## 运动来源

开启 **External MV** 时，NR 与抗闪烁共用 Fusion **MotionVectors**。使用 [MotionVectors 设置](MOTION_VECTORS.md)中的当前到前一帧方向、通道、单位和 Y 方向约定。

内部光流开启时，所有 NR Pass 与抗闪烁共用同一份运动。NR 的 **Flow Method** 为 **None** 且 Mode 为 Flow、Flow+ 或 Low Frequency 时，**AF Flow** 提供 AMDOF／NVOF 选择；对应质量控件位于 Optical Flow 分组，NR 继续使用零运动。

| 设置组合 | 运动处理 |
| --- | --- |
| NR None + Off／Static | 当前帧处理，使用零运动 |
| NR None + Flow／Flow+／Low Frequency | 按 AF Flow 估算相邻帧运动 |
| NR AMDOF／NVOF + 任意启用模式 | 复用 NR 的运动场 |
| External MV | 使用输入运动图 |

## 播放与调参

顺序播放按媒体时间累积。跳转、倒放、输入或相关处理参数变化时重置历史；同一帧的重复请求复用结果。预览与导出的历史独立，冷启动的部分导出从目标帧开始。Reset NR 同时重置 NR 与抗闪烁。

光流模式对齐相邻媒体帧；跳帧／非单位步进请求从当前修正重新开始。暂停调节抗闪烁、细节或保护参数时复用当前 NR 输出，抗闪烁从新设置重新开始。

## English

The **0.4.1** provides Off, Static, Flow, Flow+ and Low Frequency. The default is Off. **Strength** blends temporal stabilization; **Response (ms)** controls correction-amplitude memory, defaulting to 80 ms.

Anti-flicker processes the combined signed correction after detail/protection controls and before Output Mix and diagnostic views. SDR/HDR share the controls. Flow modes reuse NR motion or Fusion MotionVectors; when NR Flow Method is None, **AF Flow** selects the temporal motion provider independently.

Sequential playback accumulates in media time. Seeking, reverse playback and relevant changes reset history. Repeated requests reuse results. Preview and export histories are separate, and cold partial exports start at the requested frame. Postprocessing changes reuse the current NR result.

**Frame Rate** defaults to 0 for a valid host media rate. Enter the actual composition/media FPS when needed. With an unknown time base, the current NR correction remains available. Off and zero Strength use the current correction directly.
