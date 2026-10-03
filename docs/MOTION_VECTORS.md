# Motion vectors / 光流与外部运动

Internal methods are None, AMDOF and NVOF. None supplies zero motion and still runs NR. AMDOF has Performance (half resolution) and Quality (full resolution). NVOF profiles combine output grid and SDK preset:

| UI profile | Grid / preset |
| --- | --- |
| Performance | 4 / FAST |
| Balanced | 4 / MEDIUM |
| Quality (default) | 4 / SLOW |
| High Quality | 2 / MEDIUM |
| Highest Quality | 2 / SLOW |

Unsupported grid/preset combinations report an error. They do not silently change quality. Internal estimation fetches at most the current and previous valid source frames; the first source frame uses zero motion. Every NR pass consumes the same motion.

Fusion 中开启 **External Motion** 后读取 **MotionVectors**，内部方法/质量隐藏并保留设置。关闭开关后恢复内部设置。外部数据必须满足：

- 与 Source 相同尺寸、bounds 和像素宽高比的 **float RGBA** 图。
- **当前帧到前一帧**，默认 R=X、G=Y、源像素单位，向右/向下为正。
- 可选不同 RGBA 通道、归一化 UV、Y 向上与额外倍率。UV 向量分别乘宽/高；倍率用于单位换算。
- 原生 Fusion Vector/AOV 或 EXR 命名通道需上游映射到 RGBA。本版不直接读取这些多平面通道。
- 不经过色彩变换、0–1 裁切、有损压缩。前一帧到当前帧的光流需空间反演，单纯取负号不能得到正确的反向场。

Missing/unconnected images, mismatched domains/PAR, identical X/Y channels, nonfinite values and values outside the FP16 motion range are rejected. External mode does not fall back to estimation. Based on the host Render interaction flag, failures can return the original image with an error during interaction and fail formal renders; actual host handling remains to be verified.

已知向右 4、向下 2 的平移测试在七个内部质量档位得到中值 (-4,-2)。这证明合成帧的方向与单位合同，不证明真实素材画质或导出验收。深度仍置零；双向光流、遮挡/cost 置信度和出口抗闪烁尚未实现。
