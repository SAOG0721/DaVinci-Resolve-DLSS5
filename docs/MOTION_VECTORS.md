# Motion vectors / 光流与外部运动

## 内部光流

**Flow Method** 提供 None、AMDOF、NVOF。None 使用零运动并继续 NR 处理；AMDOF 提供 Performance（半分辨率）和 Quality（全分辨率）。

| NVOF 质量 | 用途 |
| --- | --- |
| Performance | 优先处理速度 |
| Balanced | 平衡速度与质量 |
| Quality（默认） | 更充分的运动估算 |
| High Quality | 使用更细的运动网格 |
| Highest Quality | 更细网格与更高质量估算 |

所有 NR Pass 共用同一份运动。启用抗闪烁时也可复用该运动；NR Flow Method=None 时，抗闪烁的 AF Flow 可独立选择 AMDOF 或 NVOF。详见 [Anti-flicker](ANTI_FLICKER.md)。

## Fusion 外部运动

将运动图连接到 **MotionVectors**，开启 **External MV**。**Motion Input** 显示连接状态。内部方法和质量设置在外部模式下隐藏，关闭 External MV 后恢复。

输入格式：

- float RGBA，尺寸、图像边界和像素宽高比与 Source 一致。
- 当前帧到前一帧的运动，默认 R=X、G=Y，以源像素为单位，X 向右、Y 向下为正。
- X/Y Channel 选择分量通道；Vector Units 支持源像素和归一化 UV；Positive Y Up 与 X/Y Scale 调整方向和倍率。

Fusion Vector/AOV 或 EXR 命名通道可在上游映射为 RGBA。保持运动数据的有符号数值和原始范围，采用原始数据通路与无损格式。将前一帧到当前帧的光流转换为当前帧到前一帧时，需要空间反演。

仅跳过 NR 光流估算时，使用 **External MV=Off / Flow Method=None**。要同时跳过抗闪烁光流估算，选择 Off 或 Static。

连接或格式问题通过节点提示反馈。交互预览在可用时显示原图，导出通过 Resolve 报告处理错误。修正连接或关闭 External MV 后恢复处理。

## English

**Flow Method** offers None, AMDOF and NVOF. None supplies zero motion while NR continues. AMDOF provides Performance (half resolution) and Quality (full resolution); NVOF offers Performance, Balanced, Quality, High Quality and Highest Quality.

All NR passes share the motion field. Anti-flicker can reuse it, or select an independent AMDOF/NVOF provider through AF Flow when NR Flow Method is None.

In Fusion, connect a float RGBA motion image to **MotionVectors** and enable **External MV**. Match the source dimensions, bounds and pixel aspect ratio. Use current-to-previous motion; defaults are R=X, G=Y, source-pixel units, positive X right and positive Y down. X/Y Channel, Vector Units, Positive Y Up and X/Y Scale adapt the input convention.

Map named Vector/AOV or EXR channels into RGBA upstream and preserve signed raw values with lossless storage. Converting previous-to-current flow requires spatial inversion.

**Motion Input** displays connection status. Use **External MV=Off / Flow Method=None** to skip NR estimation, and Off or Static to also skip anti-flicker estimation. Connection/format issues are reported by the node; interactive preview uses the original image when available, while export reports a processing error. Correcting the connection or disabling External MV resumes processing.
