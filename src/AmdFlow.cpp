// SPDX-License-Identifier: MIT
#include "AmdFlow.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <ffx_dx12.h>
#include <ffx_opticalflow.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace resolve_dlss5 {
using Microsoft::WRL::ComPtr;
std::vector<float> densifyFlow(std::span<const int16_t> sparse, int sw, int sh, int w, int h,
                               int grid, float sx, float sy) {
    if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0 || grid <= 0 ||
        sparse.size() != static_cast<std::size_t>(sw) * sh * 2)
        throw std::invalid_argument("Invalid sparse motion field");
    std::vector<float> result(static_cast<std::size_t>(w) * h * 2);
    auto sample = [&](int x, int y, int c) {
        return static_cast<float>(sparse[(static_cast<std::size_t>(std::clamp(y, 0, sh - 1)) * sw +
                                          std::clamp(x, 0, sw - 1)) *
                                             2 +
                                         c]);
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float px = (x + .5f) / grid - .5f, py = (y + .5f) / grid - .5f;
            const int ix = static_cast<int>(std::floor(px)), iy = static_cast<int>(std::floor(py));
            const float fx = px - ix, fy = py - iy;
            for (int c = 0; c < 2; ++c)
                result[(static_cast<std::size_t>(y) * w + x) * 2 + c] =
                    std::lerp(std::lerp(sample(ix, iy, c), sample(ix + 1, iy, c), fx),
                              std::lerp(sample(ix, iy + 1, c), sample(ix + 1, iy + 1, c), fx), fy) *
                    (c == 0 ? sx : sy);
        }
    return result;
}
namespace {
void hr(HRESULT value, const char* op) {
    if (FAILED(value))
        throw std::runtime_error(std::string(op) + " failed (HRESULT " +
                                 std::to_string(static_cast<unsigned>(value)) + ")");
}
void ffx(FfxErrorCode value, const char* op) {
    if (value != FFX_OK)
        throw std::runtime_error(std::string(op) + " failed (FidelityFX " + std::to_string(value) +
                                 ")");
}
D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES from,
                               D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    return b;
}
}  // namespace
struct AmdFlow::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    uint64_t submitted = 0;
    ComPtr<ID3D12Resource> color, motion, scd, upload, readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorFootprint{}, motionFootprint{};
    FfxOpticalflowContext context{};
    bool created = false;
    std::vector<unsigned char> scratch;
    std::vector<unsigned char> committedInput;
    int width = 0, height = 0, iw = 0, ih = 0, sw = 0, sh = 0, quality = -1;
    ~Impl() {
        if (created) ffxOpticalflowContextDestroy(&context);
        if (event) CloseHandle(event);
    }
    bool inFlight() const { return fence && fence->GetCompletedValue() < submitted; }
    ComPtr<ID3D12Resource> texture(int w, int h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Flags = flags;
        ComPtr<ID3D12Resource> resource;
        hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                           D3D12_RESOURCE_STATE_COMMON, nullptr,
                                           IID_PPV_ARGS(&resource)),
           "Allocate AMD optical flow texture");
        return resource;
    }
    ComPtr<ID3D12Resource> buffer(uint64_t size, D3D12_HEAP_TYPE type,
                                  D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = type;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> resource;
        hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr,
                                           IID_PPV_ARGS(&resource)),
           "Allocate AMD optical flow buffer");
        return resource;
    }
    void initialize(int w, int h, int q) {
        width = w;
        height = h;
        quality = q;
        iw = q == 0 ? (w + 1) / 2 : w;
        ih = q == 0 ? (h + 1) / 2 : h;
        ComPtr<IDXGIFactory6> factory;
        hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Create AMD OF adapter factory");
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.VendorId == 0x10DE && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                            IID_PPV_ARGS(&device))))
                break;
        }
        if (!device)
            throw std::runtime_error("No compatible NVIDIA D3D12 device for AMD optical flow");
        D3D12_COMMAND_QUEUE_DESC qd{};
        hr(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "Create AMD OF queue");
        hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
           "Create AMD OF allocator");
        hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                     IID_PPV_ARGS(&list)),
           "Create AMD OF command list");
        hr(list->Close(), "Close AMD OF initial list");
        hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
           "Create AMD OF fence");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) throw std::runtime_error("Create AMD OF event failed");
        scratch.resize(ffxGetScratchMemorySizeDX12(FFX_OPTICALFLOW_CONTEXT_COUNT));
        FfxOpticalflowContextDescription desc{};
        desc.resolution = {static_cast<uint32_t>(iw), static_cast<uint32_t>(ih)};
        ffx(ffxGetInterfaceDX12(&desc.backendInterface, ffxGetDeviceDX12(device.Get()),
                                scratch.data(), scratch.size(), FFX_OPTICALFLOW_CONTEXT_COUNT),
            "Initialize AMD OF backend");
        ffx(ffxOpticalflowContextCreate(&context, &desc), "Create AMD optical flow context");
        created = true;
        FfxOpticalflowSharedResourceDescriptions shared{};
        ffx(ffxOpticalflowGetSharedResourceDescriptions(&context, &shared),
            "Query AMD OF output extent");
        sw = shared.opticalFlowVector.resourceDescription.width;
        sh = shared.opticalFlowVector.resourceDescription.height;
        if (shared.opticalFlowVector.resourceDescription.format !=
            FFX_API_SURFACE_FORMAT_R16G16_SINT)
            throw std::runtime_error("Unexpected AMD OF vector format");
        color =
            texture(iw, ih, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        motion =
            texture(sw, sh, DXGI_FORMAT_R16G16_SINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const auto& sd = shared.opticalFlowSCD.resourceDescription;
        scd = texture(sd.width, sd.height, DXGI_FORMAT_R32_UINT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto cd = color->GetDesc(), md = motion->GetDesc();
        uint64_t total = 0;
        device->GetCopyableFootprints(&cd, 0, 1, 0, &colorFootprint, nullptr, nullptr, &total);
        upload = buffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        device->GetCopyableFootprints(&md, 0, 1, 0, &motionFootprint, nullptr, nullptr, &total);
        readback = buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    void wait() {
        hr(queue->Signal(fence.Get(), ++submitted), "Signal AMD OF fence");
        if (fence->GetCompletedValue() >= submitted) return;
        hr(fence->SetEventOnCompletion(submitted, event), "Wait AMD OF fence");
        if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error(
                "AMD optical flow GPU wait failed/timed out; session cannot be reused");
    }
    void dispatch(const std::vector<unsigned char>& pixels, bool reset) {
        if (inFlight()) throw std::runtime_error("AMD optical flow GPU work still in flight");
        void* mapped = nullptr;
        D3D12_RANGE noRead{0, 0};
        hr(upload->Map(0, &noRead, &mapped), "Map AMD OF color upload");
        for (int y = 0; y < ih; ++y)
            for (int x = 0; x < iw; ++x) {
                unsigned sum[4]{};
                unsigned count = 0;
                const int x0 = x * width / iw, x1 = std::max(x0 + 1, (x + 1) * width / iw),
                          y0 = y * height / ih, y1 = std::max(y0 + 1, (y + 1) * height / ih);
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx) {
                        for (int c = 0; c < 4; ++c)
                            sum[c] += pixels[(static_cast<std::size_t>(yy) * width + xx) * 4 + c];
                        ++count;
                    }
                for (int c = 0; c < 4; ++c)
                    static_cast<unsigned char*>(
                        mapped)[static_cast<std::size_t>(y) * colorFootprint.Footprint.RowPitch +
                                x * 4 + c] = static_cast<unsigned char>(sum[c] / count);
            }
        upload->Unmap(0, nullptr);
        hr(allocator->Reset(), "Reset AMD OF allocator");
        hr(list->Reset(allocator.Get(), nullptr), "Reset AMD OF list");
        auto b = barrier(color.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = color.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = colorFootprint;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        b = barrier(color.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        list->ResourceBarrier(1, &b);
        FfxOpticalflowDispatchDescription d{};
        d.commandList = ffxGetCommandListDX12(list.Get());
        d.reset = reset;
        d.color = ffxGetResourceDX12(color.Get(), ffxGetResourceDescriptionDX12(color.Get()),
                                     L"Resolve OF color", FFX_API_RESOURCE_STATE_COMMON);
        d.opticalFlowVector = ffxGetResourceDX12(
            motion.Get(), ffxGetResourceDescriptionDX12(motion.Get(), FFX_API_RESOURCE_USAGE_UAV),
            L"Resolve OF vectors", FFX_API_RESOURCE_STATE_COMMON);
        d.opticalFlowSCD = ffxGetResourceDX12(
            scd.Get(), ffxGetResourceDescriptionDX12(scd.Get(), FFX_API_RESOURCE_USAGE_UAV),
            L"Resolve OF scene change", FFX_API_RESOURCE_STATE_COMMON);
        d.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        d.minMaxLuminance = {0, 1};
        ffx(ffxOpticalflowContextDispatch(&context, &d), "Dispatch AMD optical flow");
        if (!reset) {
            b = barrier(motion.Get(), D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1, &b);
            dst = {};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = motionFootprint;
            src = {};
            src.pResource = motion.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            b = barrier(motion.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                        D3D12_RESOURCE_STATE_COMMON);
            list->ResourceBarrier(1, &b);
        }
        hr(list->Close(), "Close AMD OF dispatch");
        ID3D12CommandList* lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        wait();
    }
    std::vector<float> read() {
        std::vector<int16_t> sparse(static_cast<std::size_t>(sw) * sh * 2);
        void* mapped = nullptr;
        D3D12_RANGE range{0, static_cast<SIZE_T>(readback->GetDesc().Width)};
        hr(readback->Map(0, &range, &mapped), "Read AMD OF motion");
        for (int y = 0; y < sh; ++y)
            std::memcpy(sparse.data() + static_cast<std::size_t>(y) * sw * 2,
                        static_cast<unsigned char*>(mapped) +
                            static_cast<std::size_t>(y) * motionFootprint.Footprint.RowPitch,
                        sw * 4);
        D3D12_RANGE none{0, 0};
        readback->Unmap(0, &none);
        auto dense = densifyFlow(sparse, sw, sh, iw, ih, 8, static_cast<float>(width) / iw,
                                 static_cast<float>(height) / ih);
        if (iw == width && ih == height) return dense;
        std::vector<float> result(static_cast<std::size_t>(width) * height * 2);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                float px = (x + .5f) * iw / width - .5f, py = (y + .5f) * ih / height - .5f;
                int ix = static_cast<int>(std::floor(px)), iy = static_cast<int>(std::floor(py));
                auto sample = [&](int xx, int yy, int c) {
                    return dense[(static_cast<std::size_t>(std::clamp(yy, 0, ih - 1)) * iw +
                                  std::clamp(xx, 0, iw - 1)) *
                                     2 +
                                 c];
                };
                for (int c = 0; c < 2; ++c)
                    result[(static_cast<std::size_t>(y) * width + x) * 2 + c] = std::lerp(
                        std::lerp(sample(ix, iy, c), sample(ix + 1, iy, c), px - ix),
                        std::lerp(sample(ix, iy + 1, c), sample(ix + 1, iy + 1, c), px - ix),
                        py - iy);
            }
        return result;
    }
};
AmdFlow::AmdFlow() = default;
AmdFlow::~AmdFlow() {
    if (impl_ && impl_->inFlight()) impl_.release();
}  // Quarantine instead of freeing resources still in use.
void AmdFlow::reset() {
    if (impl_ && impl_->inFlight())
        throw std::runtime_error("Cannot reset in-flight AMD optical flow session");
    impl_.reset();
}
std::vector<float> AmdFlow::estimate(const std::vector<unsigned char>& current,
                                     const std::vector<unsigned char>& previous, int w, int h,
                                     int q) {
    if (impl_ && impl_->inFlight())
        throw std::runtime_error("AMD optical flow session is unavailable after a GPU timeout");
    if (!impl_ || impl_->width != w || impl_->height != h || impl_->quality != q) {
        reset();
        auto candidate = std::make_unique<Impl>();
        candidate->initialize(w, h, q);
        impl_ = std::move(candidate);
    }
    if (impl_->committedInput != previous) {
        impl_->dispatch(previous, true);
        // FidelityFX reports scene change for frameIndex <= 5. Prime those
        // slots with the same reference, without fetching any additional frames.
        for (int i = 0; i < 5; ++i) impl_->dispatch(previous, false);
    }
    // Continuous input reuses the confirmed previous image already in the
    // SDK; the host still declares exactly the current/previous pair.
    try {
        impl_->dispatch(current, false);
        auto result = impl_->read();
        impl_->committedInput = current;
        return result;
    } catch (...) {
        impl_->committedInput.clear();
        throw;
    }
}
}  // namespace resolve_dlss5
