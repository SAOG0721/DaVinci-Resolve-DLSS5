# r5 manual acceptance / 手测清单

Use a new test project or a copy. Add **DLSS Neural Video Experimental** manually from the effects library. Parameter registration or a completed export alone does not prove that the OFX processed frames.

1. 检查基础/进阶、Pass 2/3、SDR/HDR、光流方法及质量条件显示；Legacy 分组不再展示。隐藏值应保留。
2. 记录宿主完整版本、GPU/驱动、素材编码/尺寸和节点收到的编码。先 1 Pass、None 光流、默认细节播放，再试 2/3 Pass。
3. 暂停同一帧修改保护、低/高频与 Mix，记录响应和日志 `Render timing ms` 的 composite/cache/nr-submit-wait；试四分之一预览，检查 Map 错误。
4. 连续播放、远距离跳转、倒放；分别测 AMDOF 两档/NVOF 五档。冷跳/部分导出会重置模型历史，可能不同于完整顺序导出。
5. 以相同短范围导出 Processed、Mix=0、效果关闭的无损序列。Mix=0 应等于效果关闭；Processed 应在有 NR 修正的输入上出现实际差分且输出有限。
6. Fusion 外部模式连接当前到前一帧的同尺寸 float RGBA 运动图，检查通道/单位/Y/倍率。运动图修订应使缓存失效；未连接、无效值和尺寸错误应明确报错。
7. 在明确编码/参考白/峰值的工程中验证 HDR。继续测试长期播放、多节点、取消、工程保存/重开；40 系、更高 Resolve、Free 和多显卡分别验收。

On failures, keep `%LOCALAPPDATA%\ResolveDlss5\ResolveDlss5.log`, Resolve logs, timestamps and reproduction settings. GPU cancellation/device-loss recovery is incomplete. Use the receipt produced by your installation to preview rollback with `Restore-Development.ps1 -Receipt <path>`; close Resolve and add `-Apply` in administrator PowerShell to restore. A receipt from a different installation is not interchangeable.

The current validation evidence covers standalone tests and native parameter round trips. The r5 headless attempt produced no OFX-rendered frame or output file. Full timeline NR/export and real-content performance acceptance remain pending.
