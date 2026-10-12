// SPDX-License-Identifier: MIT
#include "TemporalGpu.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "AntiFlicker.h"
#include "GpuResourcePool.h"
#include "ParallelRows.h"
#include "Temporalmain.h"
#include "Temporalreduce.h"
namespace resolve_dlss5 {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT result, const char* operation) {
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}
D3D12_RESOURCE_DESC buffer(std::uint64_t bytes, bool writable) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = writable ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    return d;
}
std::mutex poolsMutex;
std::unordered_map<ID3D12Device*, std::weak_ptr<GpuResourcePool>> pools;
std::shared_ptr<GpuResourcePool> poolFor(ID3D12Device* device) {
    std::scoped_lock lock(poolsMutex);
    std::erase_if(pools, [](const auto& item) { return item.second.expired(); });
    auto pool = pools[device].lock();
    if (!pool) {
        pool = std::make_shared<GpuResourcePool>(device, 1024ULL * 1024 * 1024);
        pools[device] = pool;
    }
    return pool;
}
}  // namespace
struct TemporalGpu::Impl {
    ComPtr<ID3D12Device> device;
    std::shared_ptr<GpuResourcePool> pool;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> main, reduce;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    std::uint64_t value = 0, pending = 0;
    int width = 0, height = 0, index = 0;
    bool failed = false, lowAllocated = false;
    GpuResourcePool::Lease current, guide, motion, output, readback;
    std::array<GpuResourcePool::Lease, 2> histories, guides;
    GpuResourcePool::Lease low, lowGuide;
    ~Impl() {
        if (event) CloseHandle(event);
    }
    bool complete() const {
        return !pending || fence->GetCompletedValue() >= pending ||
               FAILED(device->GetDeviceRemovedReason());
    }
    void init(ID3D12Device* d) {
        device = d;
        pool = poolFor(d);
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "AF queue");
        check(device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&allocator)), "AF allocator");
        check(
            device->CreateCommandList(0, desc.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
            "AF command list");
        check(list->Close(), "AF initial close");
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "AF fence");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) throw std::runtime_error("AF fence event creation failed");
        std::array<D3D12_ROOT_PARAMETER, 13> parameters{};
        for (int i = 0; i < 7; ++i) {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameters[i].Descriptor.ShaderRegister = i;
        }
        for (int i = 0; i < 5; ++i) {
            parameters[i + 7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            parameters[i + 7].Descriptor.ShaderRegister = i;
        }
        parameters[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[12].Constants.Num32BitValues = 8;
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = static_cast<UINT>(parameters.size());
        signature.pParameters = parameters.data();
        ComPtr<ID3DBlob> blob, errors;
        check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors),
              "AF root serialization");
        check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&root)),
              "AF root signature");
        auto compile = [&](const void* bytecode, std::size_t size,
                           ComPtr<ID3D12PipelineState>& pipeline) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC p{};
            p.pRootSignature = root.Get();
            p.CS = {bytecode, size};
            check(device->CreateComputePipelineState(&p, IID_PPV_ARGS(&pipeline)), "AF pipeline");
        };
        compile(kTemporalmainBytecode, sizeof(kTemporalmainBytecode), main);
        compile(kTemporalreduceBytecode, sizeof(kTemporalreduceBytecode), reduce);
    }
    void allocate(int w, int h, bool lowMode) {
        if (width == w && height == h && (!lowMode || lowAllocated)) return;
        // Only completed leases reach the pool. Size changes replace all state.
        current = {};
        guide = {};
        motion = {};
        output = {};
        readback = {};
        histories = {};
        guides = {};
        low = {};
        lowGuide = {};
        const auto count = static_cast<std::uint64_t>(w) * h;
        auto acquire = [&](std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                           bool writable = false) {
            return pool->acquire(buffer(bytes, writable), heap, D3D12_HEAP_FLAG_NONE, state);
        };
        current = acquire(count * 16, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        guide = acquire(count * 4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        motion = acquire(count * 8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        output = acquire(count * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         true);
        readback = acquire(count * 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        for (int i = 0; i < 2; ++i) {
            histories[i] = acquire(count * 16, D3D12_HEAP_TYPE_DEFAULT,
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
            guides[i] = acquire(count * 4, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
        }
        const auto lc = lowMode ? static_cast<std::uint64_t>((w + 1) / 2) * ((h + 1) / 2) : 1;
        low = acquire(lc * 16, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
        lowGuide = acquire(lc * 16, D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
        width = w;
        height = h;
        index = 0;
        lowAllocated = lowMode;
    }
    void barrier(GpuResourcePool::Lease& lease, D3D12_RESOURCE_STATES before,
                 D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = lease.get();
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        lease.state(after);
    }
    void submitted(std::uint64_t completion) {
        for (auto* lease : {&current, &guide, &motion, &output, &readback, &histories[0],
                            &histories[1], &guides[0], &guides[1], &low, &lowGuide})
            lease->submitted(fence.Get(), completion);
    }
};
TemporalGpu::TemporalGpu(ID3D12Device* device) : impl_(std::make_shared<Impl>()) {
    impl_->init(device);
}
TemporalGpu::~TemporalGpu() {
    // A timeout must not free resources still referenced by a live GPU queue.
    // Quarantine is bounded by the shared lease budget and reclaimed only after
    // completion/device removal. It never submits further work on that queue.
    static std::mutex mutex;
    static std::vector<std::shared_ptr<Impl>> retired;
    std::scoped_lock lock(mutex);
    std::erase_if(retired, [](const auto& p) { return p->complete(); });
    if (impl_ && !impl_->complete()) retired.push_back(impl_);
}
ID3D12Device* TemporalGpu::device() const { return impl_->device.Get(); }
TemporalGpuResult TemporalGpu::process(int w, int h, const Feature18Settings& color,
                                       const AntiFlickerSettings& settings,
                                       std::span<const float> original,
                                       std::vector<float>& corrected, std::span<const float> motion,
                                       float weight, double dt) {
    auto& p = *impl_;
    if (p.failed) throw std::runtime_error("AF GPU queue needs reset after a failed submission");
    const auto count = static_cast<std::size_t>(w) * h;
    const bool lowMode = settings.mode == AntiFlickerMode::LowFrequency;
    p.allocate(w, h, lowMode);
    float* current = nullptr;
    std::uint32_t* guide = nullptr;
    float* vectors = nullptr;
    D3D12_RANGE none{0, 0};
    check(p.current.get()->Map(0, &none, reinterpret_cast<void**>(&current)), "AF current map");
    check(p.guide.get()->Map(0, &none, reinterpret_cast<void**>(&guide)), "AF guide map");
    check(p.motion.get()->Map(0, &none, reinterpret_cast<void**>(&vectors)), "AF motion map");
    parallelRows(h, count, [&](int y) noexcept {
        for (std::size_t i = static_cast<std::size_t>(y) * w, end = i + w; i < end; ++i) {
            Rgb rgb{original[i * 4], original[i * 4 + 1], original[i * 4 + 2]},
                candidate{corrected[i * 4], corrected[i * 4 + 1], corrected[i * 4 + 2]};
            const float alpha = original[i * 4 + 3];
            const bool valid = std::isfinite(rgb[0]) && std::isfinite(rgb[1]) &&
                               std::isfinite(rgb[2]) && std::isfinite(alpha) &&
                               (!color.premultiplied || alpha > 1e-6f);
            for (int c = 0; c < 3; ++c)
                current[i * 4 + c] =
                    color.detail.strength == 0 ? 0 : corrected[i * 4 + c] - original[i * 4 + c];
            if (color.premultiplied)
                for (int c = 0; c < 3; ++c) {
                    rgb[c] = valid ? rgb[c] / alpha : 0;
                    candidate[c] = valid ? candidate[c] / alpha : 0;
                }
            auto g = quantizeNeuralProxy(encodeNeuralProxy(rgb, color));
            g[3] = valid ? 255 : 0;
            std::memcpy(guide + i, g.data(), 4);
            float observed = 1;
            if (settings.mode == AntiFlickerMode::FlowPlus) {
                auto now = encodeNeuralProxy(candidate, color), base = unpackProxy(g);
                float e = std::max({std::abs(now[0] - base[0]), std::abs(now[1] - base[1]),
                                    std::abs(now[2] - base[2])});
                float v = std::clamp((e - .005f) / .015f, 0.f, 1.f);
                observed = v * v * (3 - 2 * v);
            }
            current[i * 4 + 3] = observed;
        }
    });
    if (motion.empty())
        std::memset(vectors, 0, count * 8);
    else
        std::memcpy(vectors, motion.data(), count * 8);
    p.current.get()->Unmap(0, nullptr);
    p.guide.get()->Unmap(0, nullptr);
    p.motion.get()->Unmap(0, nullptr);
    check(p.allocator->Reset(), "AF allocator reset");
    check(p.list->Reset(p.allocator.Get(), p.main.Get()), "AF list reset");
    p.list->SetComputeRootSignature(p.root.Get());
    const int next = 1 - p.index;
    ID3D12Resource* reads[] = {
        p.current.get(),         p.guide.get(), p.motion.get(),  p.histories[p.index].get(),
        p.guides[p.index].get(), p.low.get(),   p.lowGuide.get()};
    ID3D12Resource* writes[] = {p.output.get(), p.histories[next].get(), p.guides[next].get(),
                                p.low.get(), p.lowGuide.get()};
    for (int i = 0; i < 7; ++i)
        p.list->SetComputeRootShaderResourceView(i, reads[i]->GetGPUVirtualAddress());
    for (int i = 0; i < 5; ++i)
        p.list->SetComputeRootUnorderedAccessView(i + 7, writes[i]->GetGPUVirtualAddress());
    struct Constants {
        UINT w, h, mode, useMotion;
        float weight, seconds, strength;
        UINT padding;
    } constants{static_cast<UINT>(w),
                static_cast<UINT>(h),
                static_cast<UINT>(settings.mode),
                needsTemporalMotion(settings.mode) && !motion.empty() ? 1U : 0U,
                weight,
                static_cast<float>(dt),
                settings.strength,
                0};
    p.list->SetComputeRoot32BitConstants(12, 8, &constants, 0);
    constexpr auto read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    p.barrier(p.histories[next], read, uav);
    p.barrier(p.guides[next], read, uav);
    if (lowMode) {
        p.barrier(p.low, read, uav);
        p.barrier(p.lowGuide, read, uav);
        p.list->SetPipelineState(p.reduce.Get());
        p.list->Dispatch(((w + 1) / 2 + 7) / 8, ((h + 1) / 2 + 7) / 8, 1);
        p.barrier(p.low, uav, read);
        p.barrier(p.lowGuide, uav, read);
        p.list->SetPipelineState(p.main.Get());
    }
    p.list->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    p.barrier(p.histories[next], uav, read);
    p.barrier(p.guides[next], uav, read);
    p.barrier(p.output, uav, D3D12_RESOURCE_STATE_COPY_SOURCE);
    p.list->CopyResource(p.readback.get(), p.output.get());
    p.barrier(p.output, D3D12_RESOURCE_STATE_COPY_SOURCE, uav);
    check(p.list->Close(), "AF list close");
    ID3D12CommandList* lists[] = {p.list.Get()};
    p.queue->ExecuteCommandLists(1, lists);
    p.pending = ++p.value;
    const auto signal = p.queue->Signal(p.fence.Get(), p.pending);
    p.submitted(p.pending);
    if (FAILED(signal)) {
        p.failed = true;
        check(signal, "AF signal");
    }
    if (p.fence->GetCompletedValue() < p.pending) {
        const auto eventStatus = p.fence->SetEventOnCompletion(p.pending, p.event);
        if (FAILED(eventStatus)) {
            p.failed = true;
            check(eventStatus, "AF completion event");
        }
        if (WaitForSingleObject(p.event, 10000) != WAIT_OBJECT_0) {
            p.failed = true;
            throw std::runtime_error("AF GPU completion timed out");
        }
    }
    check(p.device->GetDeviceRemovedReason(), "AF device status");
    const float* result = nullptr;
    D3D12_RANGE range{0, count * 16};
    check(p.readback.get()->Map(0, &range, reinterpret_cast<void**>(const_cast<float**>(&result))),
          "AF readback map");
    TemporalGpuResult stats;
    std::atomic<std::uint64_t> accepted = 0;
    parallelRows(h, count, [&](int y) noexcept {
        std::uint64_t good = 0;
        for (std::size_t i = static_cast<std::size_t>(y) * w, end = i + w; i < end; ++i) {
            good += result[i * 4 + 3] > 0;
            for (int c = 0; c < 3; ++c) {
                const float base = original[i * 4 + c], delta = result[i * 4 + c];
                const float value = base + delta;
                corrected[i * 4 + c] = std::isfinite(value) ? (delta == 0 ? base : value)
                                                            : (std::isfinite(base) ? base : 0);
            }
            corrected[i * 4 + 3] = original[i * 4 + 3];
        }
        accepted.fetch_add(good, std::memory_order_relaxed);
    });
    p.readback.get()->Unmap(0, &none);
    p.index = next;
    p.pending = 0;
    stats.accepted = accepted.load();
    stats.rejected = count - stats.accepted;
    stats.bytes = p.pool->stats().bytes;
    return stats;
}
}  // namespace resolve_dlss5
