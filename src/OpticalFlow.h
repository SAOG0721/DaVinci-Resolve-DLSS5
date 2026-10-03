// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <span>
#include <vector>

#include "Feature18Parameters.h"

namespace resolve_dlss5 {
enum class FlowMethod { None, Amd, Nvidia };
enum class FlowUnits { Pixels, Normalized };
// External input is already current-to-previous. A previous-to-current field
// requires spatial inversion; negating it is not a valid conversion.
struct FlowSettings {
    FlowMethod method = FlowMethod::None;
    int amdQuality = 1, nvidiaQuality = 3;
    bool external = false;
    int xChannel = 0, yChannel = 1;
    FlowUnits units = FlowUnits::Pixels;
    bool yUp = false;
    float scaleX = 1, scaleY = 1;
    bool operator==(const FlowSettings&) const = default;
};
// Dense current-to-previous vectors, full-resolution pixel units, top-left
// row order and positive Y down. Empty fields mean explicit zero motion.
using MotionField = std::vector<float>;
MotionField adaptExternalMotion(std::span<const float> rgbaBottomLeft, int width, int height,
                                const FlowSettings& settings);
std::vector<unsigned char> makeFlowProxy(std::span<const float> rgbaBottomLeft, int width,
                                         int height, const Feature18Settings& settings);
class OpticalFlow final {
   public:
    OpticalFlow();
    ~OpticalFlow();
    MotionField estimate(std::span<const float> current, std::span<const float> previous, int width,
                         int height, const Feature18Settings& codec, const FlowSettings& settings);
    void reset();

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace resolve_dlss5
