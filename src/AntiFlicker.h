// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ColorPipeline.h"
#include "OpticalFlow.h"
struct ID3D12Device;

namespace resolve_dlss5 {
enum class AntiFlickerMode { Off, Static, Flow, FlowPlus, LowFrequency };
struct AntiFlickerSettings {
    AntiFlickerMode mode = AntiFlickerMode::Off;
    float strength = 1, responseSeconds = .08f;
    bool operator==(const AntiFlickerSettings&) const = default;
};
constexpr bool needsTemporalMotion(AntiFlickerMode mode) { return mode >= AntiFlickerMode::Flow; }
// OFX time is in frames. Motion fields always describe t -> t-1, independently
// of export frameStep. A different interval cannot reuse that motion history.
struct TemporalFrameContext {
    double time = 0, fps = 24, frameStep = 1, scaleX = 1, scaleY = 1, pixelAspect = 1;
    int width = 0, height = 0, originX = 0, originY = 0;
    std::uint64_t inputRevision = 0, guidanceRevision = 0;
    FlowSettings guidance;
    std::array<unsigned char, 32> source{}, reference{};
    bool hasReference = false;
    bool interactive = true, reset = false;
};
struct AntiFlickerStats {
    std::uint64_t frames = 0, cacheHits = 0, resets = 0, accepted = 0, rejected = 0;
    std::size_t bytes = 0, peakBytes = 0;
    float historyWeight = 0;
    std::uint64_t gpuBytes = 0;
};
// One committed frame and reusable scratch. Only the final signed correction
// is stabilized; this class never reads earlier host frames or calls NGX.
class TemporalGpu;
class AntiFlicker {
   public:
    explicit AntiFlicker(std::size_t budget = 768U * 1024U * 1024U) : budget_(budget) {}
    void process(const TemporalFrameContext&, const Feature18Settings&, const AntiFlickerSettings&,
                 std::span<const float> original, std::vector<float>& corrected,
                 std::span<const float> motionTopLeft = {});
    void invalidate();
    void setGpuDevice(ID3D12Device*);
    const AntiFlickerStats& stats() const { return stats_; }

   private:
    struct Key {
        TemporalFrameContext context;
        Feature18Settings color;
        AntiFlickerSettings settings;
    };
    std::optional<Key> committed_;
    std::vector<std::array<float, 4>> history_, scratch_;
    std::vector<Rgba8> guide_, nextGuide_;
    std::vector<float> cachedOutput_;
    std::vector<Rgb> low_, lowGuide_;
    std::vector<unsigned char> lowValid_;
    std::size_t budget_;
    AntiFlickerStats stats_;
    std::shared_ptr<TemporalGpu> gpu_;
};
}  // namespace resolve_dlss5
