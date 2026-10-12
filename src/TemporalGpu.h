// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <span>
#include <vector>

#include "Feature18Parameters.h"
struct ID3D12Device;
namespace resolve_dlss5 {
struct AntiFlickerSettings;
struct TemporalGpuResult {
    std::uint64_t accepted = 0, rejected = 0, bytes = 0;
};
class TemporalGpu {
   public:
    explicit TemporalGpu(ID3D12Device*);
    ~TemporalGpu();
    ID3D12Device* device() const;
    TemporalGpuResult process(int width, int height, const Feature18Settings&,
                              const AntiFlickerSettings&, std::span<const float> original,
                              std::vector<float>& corrected, std::span<const float> motion,
                              float weight, double seconds);

   private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace resolve_dlss5
