// SPDX-License-Identifier: MIT
#include "GpuResourcePool.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
namespace resolve_dlss5 {
namespace {
bool equal(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) {
    return a.Dimension == b.Dimension && a.Alignment == b.Alignment && a.Width == b.Width &&
           a.Height == b.Height && a.DepthOrArraySize == b.DepthOrArraySize &&
           a.MipLevels == b.MipLevels && a.Format == b.Format &&
           a.SampleDesc.Count == b.SampleDesc.Count &&
           a.SampleDesc.Quality == b.SampleDesc.Quality && a.Layout == b.Layout &&
           a.Flags == b.Flags;
}
bool complete(ID3D12Fence* fence, std::uint64_t value) {
    if (!fence) return value == 0;
    const auto done = fence->GetCompletedValue();
    return done != std::numeric_limits<std::uint64_t>::max() && done >= value;
}
}  // namespace
GpuResourcePool::Lease::Lease(std::shared_ptr<GpuResourcePool> pool, std::unique_ptr<Slot> slot)
    : pool_(std::move(pool)), slot_(std::move(slot)) {}
GpuResourcePool::Lease::~Lease() { release(); }
GpuResourcePool::Lease::Lease(Lease&& other) noexcept
    : pool_(std::move(other.pool_)), slot_(std::move(other.slot_)) {}
GpuResourcePool::Lease& GpuResourcePool::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        release();
        pool_ = std::move(other.pool_);
        slot_ = std::move(other.slot_);
    }
    return *this;
}
void GpuResourcePool::Lease::release() {
    if (slot_ && pool_) pool_->retire(std::move(slot_));
    pool_.reset();
}
GpuResourcePool::GpuResourcePool(ID3D12Device* device, std::uint64_t budget)
    : device_(device), budget_(budget) {}
GpuResourcePool::Stats GpuResourcePool::stats() const {
    std::scoped_lock lock(mutex_);
    return stats_;
}
void GpuResourcePool::retire(std::unique_ptr<Slot> slot) {
    std::scoped_lock lock(mutex_);
    ++stats_.retirements;
    idle_.push_back(std::move(slot));
}
void GpuResourcePool::evictCompleted(std::uint64_t needed) {
    for (auto i = idle_.begin(); i != idle_.end() && stats_.bytes + needed > budget_;) {
        if (complete((*i)->fence.Get(), (*i)->completion)) {
            stats_.bytes -= (*i)->bytes;
            i = idle_.erase(i);
            ++stats_.evictions;
        } else
            ++i;
    }
}
GpuResourcePool::Lease GpuResourcePool::acquire(D3D12_RESOURCE_DESC description,
                                                D3D12_HEAP_TYPE heap, D3D12_HEAP_FLAGS flags,
                                                D3D12_RESOURCE_STATES state) {
    std::scoped_lock lock(mutex_);
    if (FAILED(device_->GetDeviceRemovedReason()))
        throw std::runtime_error("Resource pool device was removed");
    for (auto i = idle_.begin(); i != idle_.end(); ++i) {
        const auto& p = **i;
        if (p.heap == heap && p.flags == flags && p.state == state &&
            equal(p.description, description) && complete(p.fence.Get(), p.completion)) {
            auto slot = std::move(*i);
            idle_.erase(i);
            slot->generation = ++generation_;
            ++stats_.hits;
            return Lease(shared_from_this(), std::move(slot));
        }
    }
    const auto allocation = device_->GetResourceAllocationInfo(0, 1, &description);
    if (allocation.SizeInBytes == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("Invalid GPU resource extent");
    evictCompleted(allocation.SizeInBytes);
    if (allocation.SizeInBytes > budget_ || stats_.bytes > budget_ - allocation.SizeInBytes)
        throw std::runtime_error(
            "GPU lease budget exhausted; in-flight resources cannot be reclaimed");
    auto slot = std::make_unique<Slot>();
    slot->description = description;
    slot->heap = heap;
    slot->flags = flags;
    slot->state = state;
    slot->bytes = allocation.SizeInBytes;
    slot->generation = ++generation_;
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = heap;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    const auto status = device_->CreateCommittedResource(&properties, flags, &description, state,
                                                         nullptr, IID_PPV_ARGS(&slot->resource));
    if (FAILED(status)) throw std::runtime_error("GPU lease allocation failed");
    stats_.bytes += slot->bytes;
    stats_.peak = std::max(stats_.peak, stats_.bytes);
    ++stats_.allocations;
    return Lease(shared_from_this(), std::move(slot));
}
}  // namespace resolve_dlss5
