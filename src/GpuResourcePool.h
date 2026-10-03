// SPDX-License-Identifier: MIT
#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>

namespace resolve_dlss5 {
class GpuResourcePool : public std::enable_shared_from_this<GpuResourcePool> {
    using Resource = Microsoft::WRL::ComPtr<ID3D12Resource>;
    struct Slot {
        Resource resource;
        D3D12_RESOURCE_DESC description{};
        D3D12_HEAP_TYPE heap = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        std::uint64_t completion = 0, generation = 0, bytes = 0;
    };

   public:
    struct Stats {
        std::uint64_t hits = 0, allocations = 0, retirements = 0, evictions = 0, bytes = 0,
                      peak = 0;
    };
    class Lease {
       public:
        Lease() = default;
        ~Lease();
        Lease(Lease&&) noexcept;
        Lease& operator=(Lease&&) noexcept;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ID3D12Resource* get() const { return slot_ ? slot_->resource.Get() : nullptr; }
        void state(D3D12_RESOURCE_STATES s) {
            if (slot_) slot_->state = s;
        }
        void submitted(ID3D12Fence* fence, std::uint64_t value) {
            if (slot_) {
                slot_->fence = fence;
                slot_->completion = value;
            }
        }
        std::uint64_t generation() const { return slot_ ? slot_->generation : 0; }

       private:
        friend class GpuResourcePool;
        Lease(std::shared_ptr<GpuResourcePool>, std::unique_ptr<Slot>);
        void release();
        std::shared_ptr<GpuResourcePool> pool_;
        std::unique_ptr<Slot> slot_;
    };
    explicit GpuResourcePool(ID3D12Device* device, std::uint64_t budget = 512ULL * 1024 * 1024);
    Lease acquire(D3D12_RESOURCE_DESC description, D3D12_HEAP_TYPE heap, D3D12_HEAP_FLAGS flags,
                  D3D12_RESOURCE_STATES state);
    Stats stats() const;

   private:
    void retire(std::unique_ptr<Slot>);
    void evictCompleted(std::uint64_t needed);
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    std::uint64_t budget_, generation_ = 0;
    mutable std::mutex mutex_;
    std::list<std::unique_ptr<Slot>> idle_;
    Stats stats_;
};
}  // namespace resolve_dlss5
