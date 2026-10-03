// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "Feature18Parameters.h"
#include "OpticalFlow.h"

namespace resolve_dlss5 {
struct CpuFrame {
    int width = 0, height = 0;
    std::vector<float> pixels;  // packed OFX bottom-left float RGBA
    Feature18Settings settings;
    FlowSettings flow;
    std::vector<float> referencePixels;  // at most one raw reference frame
    MotionField motion;                  // validated external current-to-previous field
};
enum class TimelineDomain { Interactive, Export };
struct TimelineStats {
    std::uint64_t inputChecks = 0, evaluatedFrames = 0, cacheHits = 0, historyResets = 0;
    std::size_t cachedBytes = 0;
};
// Streaming state accepts an already-fetched current frame. It has no API for
// fetching an earlier frame and never reconstructs a full source trajectory.
class TimelineProcessor {
   public:
    using Output = std::shared_ptr<const std::vector<float>>;
    using Evaluate = std::function<std::vector<float>(const CpuFrame&, bool)>;
    using Reset = std::function<void()>;
    explicit TimelineProcessor(std::size_t budget = 256U * 1024U * 1024U);
    Output render(double time, const CpuFrame& frame, const Evaluate& evaluate, const Reset& reset,
                  const std::function<bool()>& abort = {},
                  TimelineDomain domain = TimelineDomain::Interactive, double frameStep = 1.0);
    void invalidate();
    const TimelineStats& stats() const { return stats_; }

   private:
    struct Signature {
        double time = 0;
        int width = 0, height = 0;
        Feature18Settings settings;
        FlowSettings flow;
        std::array<unsigned char, 96> digest{};
        bool operator==(const Signature&) const = default;
    };
    static Signature signature(double time, const CpuFrame& frame);
    std::optional<Signature> committed_;
    Output cached_;
    std::size_t budget_;
    TimelineDomain domain_ = TimelineDomain::Interactive;
    double frameStep_ = 1.0;
    TimelineStats stats_;
};
}  // namespace resolve_dlss5
