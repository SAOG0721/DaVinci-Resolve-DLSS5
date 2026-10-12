# Building / 构建

The source uses `src/`, `tests/` and `docs/`. SDKs, runtime binaries, generated shaders, build output and test media stay outside the Git source tree. Visual Studio 2022 C++ x64, CMake 3.25+, CUDA Toolkit 12, Resolve OpenFX SDK, NGX/DLSS SDK, NVIDIA Optical Flow SDK, FidelityFX SDK **v2.3.0**, DXC/FXC (Windows SDK), and Python 3 for the isolated OFX test host are required. The build helper discovers Visual Studio using `vswhere` when CMake is absent from PATH.

在本机准备外部依赖，设置路径或使用同名 CMake cache 变量。`FIDELITYFX_SDK_ROOT` 指包含 `Kits/FidelityFX` 的 v2.3.0 根目录；`NVOF_SDK_ROOT` 指直接包含 `nvOpticalFlowCuda.h` 的目录。社区 DLL 用于获授权的本机 GPU 测试，并可随授权二进制包提供；Git 源码保留代码与文本许可。

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

This configures `src/` into `build/`, builds Release and runs nine CTest groups. It never installs. The selected runtime is used for real NGX tests and its identity is recorded. CPU and real GPU results are separate from Resolve host acceptance.

添加 `-Package` 在九组测试通过后准备二进制候选、许可与 manifest。`src/scripts/Package-Distribution.ps1 -Validation -RuntimeDll $env:DLSSNR_RUNTIME_DLL` 校验匹配二进制的测试/参数证据，并从干净的暂存目录生成精简 ZIP：插件 bundle、Install.ps1、Restore.ps1 与 INSTALL.md。安装器摘要置于 bundle 的 Contents/package.json，详细报告保留在维护记录。打包使用 PowerShell 7；安装/回退支持 PowerShell 5.1 与 7。

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

The binary ZIP includes the supplied authorized community DLSSNR runtime in the private bundle runtime directory. Source ZIPs retain source files only. Installer metadata carries no runtime hash allowlist.

## Anti-flicker

FXC defaults to `fxc.exe` beside the selected DXC. Override `FXC_PATH` when needed. `Compile-TemporalShaders.ps1` builds main/reduce bytecode into `build/TemporalShaders/`; the plugin embeds it. Runtime users need only the plugin and compatible DLSSNR runtime.

The additional tests cover the CPU reference, real D3D12 temporal comparison, and an isolated suite host loading/rendering the actual OFX. `OfxHarness.py` writes a candidate report without launching Resolve. Source builds retain nine groups; native Resolve Inspector/export acceptance is recorded separately.

For a reviewable candidate without replacing the public ZIP, package with `-Validation -OutputDirectory dist/local-0.4.1` after generating the current binary/evidence. No installation occurs.

The CMake project version supplies the package version, Windows file/product version and plugin description. OFX major/minor remains 0.4 for compatible patch releases. Test evidence is stored under `tests/results/v0.4.1-fusion/`.

`ResolveDlss5.FusionContracts` exercises sixteen real OFX failure/metadata/recovery cases. The source continues to use the unmodified Resolve SDK; messages are guarded by the actual host capability.
