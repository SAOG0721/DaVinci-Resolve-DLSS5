# Building / 构建

The source uses `src/`, `tests/` and `docs/`. No SDK, runtime, generated shader, build output or test media is vendored. Visual Studio 2022 C++ x64, CMake 3.25+, CUDA Toolkit 12, Resolve OpenFX SDK, NGX/DLSS SDK, NVIDIA Optical Flow SDK, FidelityFX SDK **v2.3.0**, and DXC are required. The build helper discovers Visual Studio using `vswhere` when CMake is absent from PATH.

在本机准备外部依赖，设置路径或使用同名 CMake cache 变量。`FIDELITYFX_SDK_ROOT` 指包含 `Kits/FidelityFX` 的 v2.3.0 根目录；`NVOF_SDK_ROOT` 指直接包含 `nvOpticalFlowCuda.h` 的目录。社区 DLL 仅用于获授权的本机 GPU 测试，不提交或加入分发。

```powershell
$env:DLSS_SDK_ROOT = 'C:\deps\DLSS'
$env:RESOLVE_OFX_SDK_ROOT = 'C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\Developer\OpenFX'
$env:FIDELITYFX_SDK_ROOT = 'C:\deps\FSR-SDK-v2.3.0'
$env:NVOF_SDK_ROOT = 'C:\deps\NVIDIAOpticalFlowSDK'
$env:DXC_PATH = 'C:\path\to\dxc.exe'
$env:DLSSNR_RUNTIME_DLL = 'C:\path\to\nvngx_dlssnr.dll'
# Set CUDA_PATH to your CUDA 12 installation if discovery needs help.
```

## Build and test with the script

From the repository root:

```powershell
.\src\scripts\Build-Local.ps1 `
    -DlssSdkRoot $env:DLSS_SDK_ROOT `
    -FidelityFxSdkRoot $env:FIDELITYFX_SDK_ROOT `
    -NvOfSdkRoot $env:NVOF_SDK_ROOT `
    -DxcPath $env:DXC_PATH `
    -RuntimeDll $env:DLSSNR_RUNTIME_DLL
```

This configures `src/` into `build/`, builds Release and runs five CTest groups. It never installs. The runtime hash must match the version documented in README. CPU and real GPU results are separate from Resolve host acceptance.

添加 `-Package` 可在五组测试通过后准备 `dist/0.4.0/` 二进制候选、许可与 manifest，**不能单靠此步骤生成可验收的发布包**：README、手测清单和同二进制的真实宿主参数报告仍须准备。`Package-Distribution.ps1 -Validation` 需要匹配的本机宿主证据，否则拒绝。普通安装/打包门禁仍保留；不得以编译成功代替宿主验收。

## CMake presets

The repository root also provides a CMake wrapper and presets for development:

```powershell
cmake --preset vs2022-x64
cmake --build --preset release --parallel 6
ctest --preset release
```

Presets use `build/vs2022-x64`, separate from the script's `build/`. Do not mix their caches. For local overrides, copy `CMakeUserPresets.json.example` to the ignored `CMakeUserPresets.json` and fill in dependency paths. Runtime absence allows source compilation but real NGX tests require the authorized DLL. The shader build helper is GPL-3.0-only; AMD-generated DXIL retains AMD's MIT terms.

## Performance probe

`ResolveDlss5PerformanceProbe` is an optional diagnostic executable built with the tests and omitted from binary packages. Run it from its build output directory, for example:

```powershell
.\ResolveDlss5PerformanceProbe.exe 1920 1080 --gpu
```

It reports synthetic CPU cases and, with `--gpu`, the real NGX path including transfers/waits. These numbers are not host playback FPS or image-quality acceptance. See [test scope](../tests/README.md).
