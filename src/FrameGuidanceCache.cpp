// SPDX-License-Identifier: MIT
#include "FrameGuidanceCache.h"

#include <Windows.h>
#include <bcrypt.h>

#include <cmath>
#include <limits>
#include <stdexcept>
namespace resolve_dlss5 {
FlowSettings activeFlowSettings(FlowSettings s) {
    if (s.external) {
        s.method = FlowMethod::None;
        s.amdQuality = 1;
        s.nvidiaQuality = 3;
    } else {
        s.xChannel = 0;
        s.yChannel = 1;
        s.units = FlowUnits::Pixels;
        s.yUp = false;
        s.scaleX = s.scaleY = 1;
        if (s.method != FlowMethod::Amd) s.amdQuality = 1;
        if (s.method != FlowMethod::Nvidia) s.nvidiaQuality = 3;
    }
    return s;
}
FlowSettings guidanceDemand(FlowSettings s, bool temporalMotion, FlowMethod fallback) {
    if (!s.external && s.method == FlowMethod::None && temporalMotion) {
        if (fallback != FlowMethod::Amd && fallback != FlowMethod::Nvidia)
            throw std::invalid_argument("Temporal motion requires AMDOF or NVOF");
        s.method = fallback;
    }
    return activeFlowSettings(s);
}
FrameGuidanceCache::Output FrameGuidanceCache::get(
    double time, int width, int height, const Feature18Settings& color, FlowSettings flow,
    std::span<const float> current, std::span<const float> reference,
    std::span<const float> external, const std::function<MotionField()>& produce) {
    const auto count = static_cast<std::size_t>(std::max(width, 0)) * std::max(height, 0);
    if (!std::isfinite(time) || width <= 0 || height <= 0 || count > 16384U * 16384U ||
        current.size() != count * 4 || (!reference.empty() && reference.size() != current.size()) ||
        (!external.empty() && external.size() != count * 2))
        throw std::invalid_argument("Invalid guidance cache input");
    Key key;
    key.time = time;
    key.width = width;
    key.height = height;
    key.flow = activeFlowSettings(flow);
    key.codec.inputEncoding = color.inputEncoding;
    key.codec.premultiplied = color.premultiplied;
    key.codec.paperWhiteScale = color.paperWhiteScale;
    key.codec.referenceWhiteNits = color.referenceWhiteNits;
    key.codec.peakNits = color.peakNits;
    auto hash = [&](std::span<const float> values, int offset) {
        if (values.empty()) return;
        const auto bytes = values.size_bytes();
        if (bytes > std::numeric_limits<ULONG>::max() ||
            BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                       reinterpret_cast<PUCHAR>(const_cast<float*>(values.data())),
                       static_cast<ULONG>(bytes), key.digest.data() + offset, 32) < 0)
            throw std::runtime_error("Cannot fingerprint guidance input");
    };
    hash(current, 0);
    hash(reference, 32);
    hash(external, 64);
    if (key_ && *key_ == key && output_) return output_;
    // One dense field; the pointer stays immutable through every consumer.
    if (count * sizeof(float) * 2 > 256U * 1024U * 1024U)
        throw std::runtime_error("Guidance cache exceeds its bounded memory budget");
    try {
        auto output = std::make_shared<const MotionField>(produce());
        if (output->size() != count * 2) throw std::runtime_error("Incomplete shared motion field");
        for (float v : *output)
            if (!std::isfinite(v)) throw std::runtime_error("Non-finite shared motion field");
        output_ = std::move(output);
        key_ = key;
        ++revision_;
        ++productions_;
        return output_;
    } catch (...) {
        invalidate();
        throw;
    }
}
}  // namespace resolve_dlss5
