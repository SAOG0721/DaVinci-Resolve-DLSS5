// SPDX-License-Identifier: MIT
#include "OpticalFlow.h"

#include <Windows.h>
#include <cuda.h>
#include <nvOpticalFlowCuda.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "AmdFlow.h"
#include "ColorPipeline.h"

namespace resolve_dlss5 {
namespace {
void checkCu(CUresult status, const char* operation) {
    if (status != CUDA_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (CUDA " +
                                 std::to_string(status) + ")");
}
void checkOf(NV_OF_STATUS status, const char* operation) {
    if (status != NV_OF_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (NVOF " +
                                 std::to_string(status) + ")");
}
void extent(std::size_t count, int w, int h) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || count != static_cast<std::size_t>(w) * h * 4)
        throw std::invalid_argument("Invalid optical flow image extent");
}
struct ContextScope {
    explicit ContextScope(CUcontext context) {
        checkCu(cuCtxPushCurrent(context), "Push optical flow context");
    }
    ~ContextScope() {
        CUcontext previous = nullptr;
        cuCtxPopCurrent(&previous);
    }
};
}  // namespace
MotionField adaptExternalMotion(std::span<const float> rgba, int w, int h, const FlowSettings& s) {
    extent(rgba.size(), w, h);
    if (s.xChannel < 0 || s.xChannel > 3 || s.yChannel < 0 || s.yChannel > 3 ||
        s.xChannel == s.yChannel || !std::isfinite(s.scaleX) || !std::isfinite(s.scaleY))
        throw std::invalid_argument("Invalid external motion channel/scale");
    MotionField result(static_cast<std::size_t>(w) * h * 2);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto input = (static_cast<std::size_t>(h - 1 - y) * w + x) * 4;
            const auto output = (static_cast<std::size_t>(y) * w + x) * 2;
            const float vx =
                rgba[input + s.xChannel] * s.scaleX * (s.units == FlowUnits::Normalized ? w : 1);
            const float vy = rgba[input + s.yChannel] * s.scaleY *
                             (s.units == FlowUnits::Normalized ? h : 1) * (s.yUp ? -1.f : 1.f);
            if (!std::isfinite(vx) || !std::isfinite(vy) || std::abs(vx) > 65504 ||
                std::abs(vy) > 65504)
                throw std::invalid_argument(
                    "External motion contains non-finite or FP16-incompatible values");
            result[output] = vx;
            result[output + 1] = vy;
        }
    return result;
}
std::vector<unsigned char> makeFlowProxy(std::span<const float> rgba, int w, int h,
                                         const Feature18Settings& s) {
    extent(rgba.size(), w, h);
    std::vector<unsigned char> result(rgba.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto i = (static_cast<std::size_t>(h - 1 - y) * w + x) * 4;
            Rgb rgb{rgba[i], rgba[i + 1], rgba[i + 2]};
            if (s.premultiplied)
                for (float& value : rgb)
                    value =
                        std::isfinite(rgba[i + 3]) && rgba[i + 3] > 1e-6f ? value / rgba[i + 3] : 0;
            const auto pixel = quantizeNeuralProxy(encodeNeuralProxy(rgb, s));
            std::copy(pixel.begin(), pixel.end(),
                      result.begin() + (static_cast<std::size_t>(y) * w + x) * 4);
        }
    return result;
}
struct OpticalFlow::Impl {
    HMODULE module = nullptr;
    CUcontext context = nullptr;
    NvOFHandle handle = nullptr;
    NV_OF_CUDA_API_FUNCTION_LIST api{};
    NvOFGPUBufferHandle input = nullptr, reference = nullptr, output = nullptr;
    int width = 0, height = 0, quality = 0, grid = 4;
    AmdFlow amd;
    ~Impl() { destroy(); }
    void destroy() noexcept {
        if (context && cuCtxPushCurrent(context) == CUDA_SUCCESS) {
            for (auto buffer : {input, reference, output})
                if (buffer) api.nvOFDestroyGPUBufferCuda(buffer);
            if (handle) api.nvOFDestroy(handle);
            CUcontext previous = nullptr;
            cuCtxPopCurrent(&previous);
        }
        input = reference = output = nullptr;
        handle = nullptr;
        if (context) cuCtxDestroy(context);
        context = nullptr;
        if (module) FreeLibrary(module);
        module = nullptr;
        width = height = quality = 0;
    }
    void initialize(int w, int h, int q) {
        destroy();
        checkCu(cuInit(0), "Initialize optical flow CUDA driver");
        CUdevice device = 0;
        checkCu(cuDeviceGet(&device, 0), "Select optical flow CUDA device");
        checkCu(cuCtxCreate(&context, CU_CTX_SCHED_BLOCKING_SYNC, device),
                "Create optical flow context");
        CUcontext popped = nullptr;
        checkCu(cuCtxPopCurrent(&popped), "Restore caller CUDA context");
        ContextScope scope(context);
        module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            throw std::runtime_error("NVIDIA optical flow driver nvofapi64.dll unavailable");
        using Create = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_CUDA_API_FUNCTION_LIST*);
        const auto create =
            reinterpret_cast<Create>(GetProcAddress(module, "NvOFAPICreateInstanceCuda"));
        if (!create) throw std::runtime_error("NVOF CUDA API unavailable");
        checkOf(create(NV_OF_API_VERSION, &api), "Load NVOF CUDA API");
        checkOf(api.nvCreateOpticalFlowCuda(context, &handle), "Create NVOF");
        uint32_t count = 0;
        checkOf(api.nvOFGetCaps(handle, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, nullptr, &count),
                "Query NVOF grid count");
        std::vector<uint32_t> supported(count);
        checkOf(api.nvOFGetCaps(handle, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, supported.data(),
                                &count),
                "Query NVOF grids");
        grid = q >= 4 ? 2 : 4;
        if (std::find(supported.begin(), supported.end(), static_cast<uint32_t>(grid)) ==
            supported.end())
            throw std::runtime_error(
                "Selected NVOF quality grid unsupported; select another quality explicitly");
        NV_OF_INIT_PARAMS init{};
        init.width = w;
        init.height = h;
        init.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(grid);
        init.mode = NV_OF_MODE_OPTICALFLOW;
        init.perfLevel = q == 1
                             ? NV_OF_PERF_LEVEL_FAST
                             : (q == 2 || q == 4 ? NV_OF_PERF_LEVEL_MEDIUM : NV_OF_PERF_LEVEL_SLOW);
        checkOf(api.nvOFInit(handle, &init), "Initialize NVOF profile");
        NV_OF_BUFFER_DESCRIPTOR desc{};
        desc.width = w;
        desc.height = h;
        desc.bufferFormat = NV_OF_BUFFER_FORMAT_ABGR8;
        desc.bufferUsage = NV_OF_BUFFER_USAGE_INPUT;
        checkOf(
            api.nvOFCreateGPUBufferCuda(handle, &desc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &input),
            "Allocate NVOF input");
        checkOf(api.nvOFCreateGPUBufferCuda(handle, &desc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR,
                                            &reference),
                "Allocate NVOF reference");
        desc.width = (w + grid - 1) / grid;
        desc.height = (h + grid - 1) / grid;
        desc.bufferFormat = NV_OF_BUFFER_FORMAT_SHORT2;
        desc.bufferUsage = NV_OF_BUFFER_USAGE_OUTPUT;
        checkOf(
            api.nvOFCreateGPUBufferCuda(handle, &desc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &output),
            "Allocate NVOF output");
        width = w;
        height = h;
        quality = q;
    }
    MotionField nvidia(const std::vector<unsigned char>& current,
                       const std::vector<unsigned char>& previous, int w, int h, int q) {
        if (!handle || width != w || height != h || quality != q) initialize(w, h, q);
        ContextScope scope(context);
        auto upload = [&](NvOFGPUBufferHandle buffer, const std::vector<unsigned char>& pixels) {
            NV_OF_CUDA_BUFFER_STRIDE_INFO stride{};
            checkOf(api.nvOFGPUBufferGetStrideInfo(buffer, &stride), "Query NVOF input stride");
            CUDA_MEMCPY2D copy{};
            copy.srcMemoryType = CU_MEMORYTYPE_HOST;
            copy.srcHost = pixels.data();
            copy.srcPitch = w * 4;
            copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
            copy.dstDevice = api.nvOFGPUBufferGetCUdeviceptr(buffer);
            copy.dstPitch = stride.strideInfo[0].strideXInBytes;
            copy.WidthInBytes = w * 4;
            copy.Height = h;
            checkCu(cuMemcpy2D(&copy), "Upload NVOF frame");
        };
        upload(input, current);
        upload(reference, previous);
        NV_OF_EXECUTE_INPUT_PARAMS in{};
        in.inputFrame = input;
        in.referenceFrame = reference;
        in.disableTemporalHints = NV_OF_TRUE;
        NV_OF_EXECUTE_OUTPUT_PARAMS out{};
        out.outputBuffer = output;
        checkOf(api.nvOFExecute(handle, &in, &out), "Estimate NVOF current-to-previous");
        checkCu(cuCtxSynchronize(), "Synchronize NVOF output");
        const int sw = (w + grid - 1) / grid, sh = (h + grid - 1) / grid;
        std::vector<int16_t> sparse(static_cast<std::size_t>(sw) * sh * 2);
        NV_OF_CUDA_BUFFER_STRIDE_INFO stride{};
        checkOf(api.nvOFGPUBufferGetStrideInfo(output, &stride), "Query NVOF output stride");
        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.srcDevice = api.nvOFGPUBufferGetCUdeviceptr(output);
        copy.srcPitch = stride.strideInfo[0].strideXInBytes;
        copy.dstMemoryType = CU_MEMORYTYPE_HOST;
        copy.dstHost = sparse.data();
        copy.dstPitch = sw * 4;
        copy.WidthInBytes = sw * 4;
        copy.Height = sh;
        checkCu(cuMemcpy2D(&copy), "Read NVOF vectors");
        return densifyFlow(sparse, sw, sh, w, h, grid, 1.f / 32, 1.f / 32);
    }
};
OpticalFlow::OpticalFlow() : impl_(std::make_unique<Impl>()) {}
OpticalFlow::~OpticalFlow() = default;
void OpticalFlow::reset() {
    impl_->destroy();
    impl_->amd.reset();
}
MotionField OpticalFlow::estimate(std::span<const float> current, std::span<const float> previous,
                                  int w, int h, const Feature18Settings& codec,
                                  const FlowSettings& s) {
    if (s.external)
        throw std::invalid_argument("External mode must supply a validated motion field");
    if (s.method == FlowMethod::None) return {};
    if ((s.method == FlowMethod::Nvidia && (s.nvidiaQuality < 1 || s.nvidiaQuality > 5)) ||
        (s.method == FlowMethod::Amd && (s.amdQuality < 0 || s.amdQuality > 1)))
        throw std::invalid_argument("Invalid optical flow quality");
    const auto a = makeFlowProxy(current, w, h, codec);
    const auto b = makeFlowProxy(previous, w, h, codec);
    if (s.method == FlowMethod::Nvidia) return impl_->nvidia(a, b, w, h, s.nvidiaQuality);
    if (s.method == FlowMethod::Amd) return impl_->amd.estimate(a, b, w, h, s.amdQuality);
    throw std::invalid_argument("Unknown optical flow method");
}
}  // namespace resolve_dlss5
