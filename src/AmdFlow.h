// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace resolve_dlss5 {
std::vector<float> densifyFlow(std::span<const int16_t> sparse, int sw, int sh, int w, int h,
                               int grid, float scaleX, float scaleY);
class AmdFlow {
   public:
    AmdFlow();
    ~AmdFlow();
    void reset();
    std::vector<float> estimate(const std::vector<unsigned char>& current,
                                const std::vector<unsigned char>& previous, int w, int h,
                                int quality);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace resolve_dlss5
