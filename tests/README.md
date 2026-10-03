# Test scope / 验证范围

Reference environment: Windows x64, Resolve Studio 20.0.1.6, Ryzen 9 9950X3D, RTX 5070 Ti, NVIDIA 616.92, Visual Studio 2022, CUDA 12.3. Runtime 310.8.0.0 is checked by SHA-256.

| Group | Validated boundary |
| --- | --- |
| Feature18RuntimeSmoke | Actual runtime create/evaluate |
| Core | Color encodings, correction controls, streaming/cache contracts; serial/parallel and neutral fused output bitwise checks, alpha/nonfinite inputs, concurrent instances |
| TimelineRuntimeSmoke | Streaming path versus direct NGX with the same reset rules |
| GpuContracts | Actual 1/2/3-pass, SDR/HDR, resource/fence/instance checks, negative stride and unaligned 480×270 / 288×162 readback |
| OpticalFlow | Seven quality profiles on known translation, external conversion/rejection, cache invalidation and real NR motion input |

Test images are generated in memory. No external footage is distributed. These are contract tests and do not establish visual quality, host FPS, export equivalence or fault-stress acceptance.

历史 r5 二进制的 Resolve 参数 API 检查：63 个控件/动作（含隐藏旧 ID）、9 组、隐藏值保留、旧设置往返、光流设置与 MotionVectors 连接。物理 Inspector、真实运动像素读取及时间线 NR 导出尚待[手测](MANUAL-ACCEPTANCE.md)。r5 headless Fusion 未调度 OFX Render，未输出帧；不能沿用较早版本的一次预览证明。

## Synthetic performance / 合成性能

CPU cases use the same generated 1080p source and neural output before/after r5. Three output fingerprints match. CPU stage timings discard the first of three iterations and average the remaining two. The actual NR warm path includes encoding, GPU submission/wait and readback; its separate GPU inference time is not measured.

| Stage, 1080p | Before r5 | r5 |
| --- | ---: | ---: |
| Default CPU post | 128.05 ms | 18.63 ms |
| Protection CPU post | 494.24 ms | 70.23 ms |
| Protection + frequency CPU post | 808.95 ms | 112.57 ms |
| Warm one-pass NR path, including transfers | 72.93 ms | 15.87 ms |

At 4K, r5 CPU stages are approximately 77.14 / 286.44 / 458.50 ms and the warm NR path is about 56.62 ms. Complex postprocessing remains slow. These are standalone stage measurements, not Resolve FPS or native parameter latency. GPU postprocessing and the CUDA bridge remain planned.

## Running / 运行

Follow [BUILDING.md](../docs/BUILDING.md). The build script runs all five CTest groups. Preset builds use `ctest --preset release`. Run the separate performance probe only when needed; it is not in CTest or binary packages. `src/scripts/Verify-ResolveParameters.py` and other host scripts require a deliberately prepared test session and the official Resolve Scripting API; inspect their preconditions before running them.

Public source archives omit machine logs, host projects, SDKs, private runtimes and generated outputs. The binary archive carries its original successful contract report and native parameter report with matching hashes; a fresh build can produce a different binary hash and requires its own host evidence.

## 0.4.0 preparation checks / 本次交付检查

2026-10-03: the maintained build and a clean, separate source checkout both pass all five test groups (12.55 s and 12.74 s respectively). The public source checkout was built with the documented root CMake presets and external dependency variables. PowerShell 7.6.5 and 5.1 installation/rollback previews pass for the extracted 0.4.0 ZIP, with no changes to installed plugin/runtime hashes or backup count. This preparation did not run or close the user's Resolve session and did not add native render/export acceptance.

## Compact-label update

The final 0.4.0 package changes UI label, option and hint strings only. Source token/ID comparison confirms the parameter IDs, defaults, ranges, choice ordering and computation are preserved. Compiled-binary string checks cover the new labels. Native r5 parameter round trips are historical evidence; the newly labeled binary has no new native parameter/panel/render/export acceptance. The current Resolve session remains open and is not modified by package preparation.
