// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>

#include "OpticalFlow.h"
namespace resolve_dlss5 {
FlowSettings activeFlowSettings(FlowSettings settings);
// Demand union: NR None still supplies zero motion to NR. Temporal flow can
// request an internal field of its own, shared when NR already has one.
FlowSettings guidanceDemand(FlowSettings nr, bool temporalMotion, FlowMethod fallback);
class FrameGuidanceCache {
   public:
    using Output = std::shared_ptr<const MotionField>;
    Output get(double time, int width, int height, const Feature18Settings&, FlowSettings,
               std::span<const float> current, std::span<const float> reference,
               std::span<const float> external, const std::function<MotionField()>& produce);
    void invalidate() {
        key_.reset();
        output_.reset();
    }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t productions() const { return productions_; }
    std::array<unsigned char, 32> referenceDigest() const {
        std::array<unsigned char, 32> result{};
        if (key_) std::copy_n(key_->digest.begin() + 32, 32, result.begin());
        return result;
    }

   private:
    struct Key {
        double time = 0;
        int width = 0, height = 0;
        Feature18Settings codec;
        FlowSettings flow;
        std::array<unsigned char, 96> digest{};
        bool operator==(const Key&) const = default;
    };
    std::optional<Key> key_;
    Output output_;
    std::uint64_t revision_ = 0, productions_ = 0;
};
}  // namespace resolve_dlss5
