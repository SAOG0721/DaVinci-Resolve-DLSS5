// SPDX-License-Identifier: MIT
#include "AntiFlicker.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

#include "ParallelRows.h"
#include "TemporalGpu.h"

namespace resolve_dlss5 {
namespace {
float smooth(float a, float b, float x) noexcept {
    const float v = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return v * v * (3 - 2 * v);
}
float error(Rgb a, Rgb b) noexcept {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}
bool finite(Rgb a) noexcept {
    return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}
Rgb rgb(std::span<const float> p, std::size_t i) noexcept {
    return {p[i * 4], p[i * 4 + 1], p[i * 4 + 2]};
}
Rgb delta(std::span<const float> a, std::span<const float> b, std::size_t i) noexcept {
    auto x = rgb(a, i), y = rgb(b, i);
    return {y[0] - x[0], y[1] - x[1], y[2] - x[2]};
}
Rgb proxy(Rgb value, float alpha, const Feature18Settings& s) noexcept {
    if (s.premultiplied)
        for (auto& v : value) v = std::isfinite(alpha) && alpha > 1e-6f ? v / alpha : 0;
    return encodeNeuralProxy(value, s);
}
bool sameGeometry(const TemporalFrameContext& a, const TemporalFrameContext& b) noexcept {
    return a.width == b.width && a.height == b.height && a.originX == b.originX &&
           a.originY == b.originY && a.fps == b.fps && a.frameStep == b.frameStep &&
           a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.pixelAspect == b.pixelAspect &&
           a.interactive == b.interactive && a.guidance == b.guidance;
}
}  // namespace
void AntiFlicker::invalidate() {
    committed_.reset();
    decltype(history_){}.swap(history_);
    decltype(scratch_){}.swap(scratch_);
    decltype(guide_){}.swap(guide_);
    decltype(nextGuide_){}.swap(nextGuide_);
    decltype(cachedOutput_){}.swap(cachedOutput_);
    decltype(low_){}.swap(low_);
    decltype(lowGuide_){}.swap(lowGuide_);
    decltype(lowValid_){}.swap(lowValid_);
    stats_.bytes = 0;
    stats_.historyWeight = 0;
    stats_.gpuBytes = 0;
}
void AntiFlicker::setGpuDevice(ID3D12Device* device) {
    if (gpu_ && gpu_->device() == device) return;
    gpu_.reset();
    invalidate();
    if (device) gpu_ = std::make_shared<TemporalGpu>(device);
}
void AntiFlicker::process(const TemporalFrameContext& context, const Feature18Settings& color,
                          const AntiFlickerSettings& settings, std::span<const float> original,
                          std::vector<float>& corrected, std::span<const float> motion) {
    if (settings.mode == AntiFlickerMode::Off || settings.strength == 0) {
        gpu_.reset();
        invalidate();
        return;
    }
    const int w = context.width, h = context.height;
    const auto count = static_cast<std::size_t>(std::max(w, 0)) * std::max(h, 0);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || original.size() != count * 4 ||
        corrected.size() != original.size() || !std::isfinite(context.time) ||
        !std::isfinite(context.fps) || context.fps <= 0 || !std::isfinite(context.frameStep) ||
        context.frameStep <= 0 || !std::isfinite(settings.responseSeconds) ||
        settings.responseSeconds < .001f || settings.responseSeconds > 2 ||
        !std::isfinite(settings.strength) || settings.strength < 0 || settings.strength > 1 ||
        settings.mode < AntiFlickerMode::Static || settings.mode > AntiFlickerMode::LowFrequency ||
        (!motion.empty() && motion.size() != count * 2)) {
        invalidate();
        throw std::invalid_argument("Invalid antiflicker frame/settings");
    }
    auto keyColor = color;
    // Inactive pass values never participate in output history identity.
    for (int i = std::max(0, keyColor.passCount - 1); i < 2; ++i) keyColor.additionalPasses[i] = {};
    const bool sameKey = committed_ && sameGeometry(context, committed_->context) &&
                         keyColor == committed_->color && settings == committed_->settings;
    if (sameKey && context.time == committed_->context.time &&
        context.inputRevision == committed_->context.inputRevision &&
        context.guidanceRevision == committed_->context.guidanceRevision && !context.reset) {
        corrected = cachedOutput_;
        ++stats_.cacheHits;
        return;
    }
    const bool useMotion = needsTemporalMotion(settings.mode);
    const double dt = committed_ ? (context.time - committed_->context.time) / context.fps : 0;
    const bool continuous =
        sameKey && !context.reset && dt > 0 &&
        (!context.hasReference || context.reference == committed_->context.source) &&
        std::abs(context.time - committed_->context.time - context.frameStep) < 1e-6 &&
        (!useMotion || (!motion.empty() && std::abs(context.frameStep - 1) < 1e-6));
    const float weight =
        continuous ? static_cast<float>(std::exp(-dt / settings.responseSeconds)) : 0;
    if (!continuous) ++stats_.resets;
    stats_.historyWeight = weight;
    const bool lowMode = settings.mode == AntiFlickerMode::LowFrequency;
    const int lw = (w + 1) / 2, lh = (h + 1) / 2;
    const auto lowCount = static_cast<std::size_t>(lw) * lh;
    const auto bytes =
        count * (2 * (sizeof(std::array<float, 4>) + sizeof(Rgba8)) + 4 * sizeof(float)) +
        (lowMode ? lowCount * (2 * sizeof(Rgb) + 1) : 0);
    if (bytes > budget_) {
        invalidate();
        throw std::runtime_error("Antiflicker history exceeds its bounded CPU memory budget");
    }
    if (gpu_) {
        const auto result =
            gpu_->process(w, h, color, settings, original, corrected, motion, weight, dt);
        cachedOutput_ = corrected;
        committed_ = Key{context, keyColor, settings};
        ++stats_.frames;
        stats_.accepted += result.accepted;
        stats_.rejected += result.rejected;
        stats_.bytes = cachedOutput_.size() * sizeof(float);
        stats_.peakBytes = std::max(stats_.peakBytes, stats_.bytes);
        stats_.gpuBytes = result.bytes;
        return;
    }
    if (history_.size() != count) invalidate();
    history_.resize(count);
    scratch_.resize(count);
    guide_.resize(count);
    nextGuide_.resize(count);
    if (lowMode) {
        low_.resize(lowCount);
        lowGuide_.resize(lowCount);
        lowValid_.resize(lowCount);
    } else {
        decltype(low_){}.swap(low_);
        decltype(lowGuide_){}.swap(lowGuide_);
        decltype(lowValid_){}.swap(lowValid_);
    }
    stats_.bytes = bytes;
    stats_.peakBytes = std::max(stats_.peakBytes, bytes);
    parallelRows(h, count, [&](int y) noexcept {
        for (std::size_t i = static_cast<std::size_t>(y) * w, end = i + w; i < end; ++i) {
            const auto c = rgb(original, i);
            auto p = quantizeNeuralProxy(proxy(c, original[i * 4 + 3], color));
            p[3] = finite(c) && std::isfinite(original[i * 4 + 3]) &&
                           (!color.premultiplied || original[i * 4 + 3] > 1e-6f)
                       ? 255
                       : 0;
            nextGuide_[i] = p;
        }
    });
    if (lowMode)
        parallelRows(lh, lowCount, [&](int y) noexcept {
            for (int x = 0; x < lw; ++x) {
                const auto i = static_cast<std::size_t>(y) * lw + x;
                Rgb r{}, g{};
                float mass = 0;
                bool valid = true;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int xx = x * 2 + dx, yy = y * 2 + dy;
                        if (xx >= w || yy >= h) continue;
                        const auto q = static_cast<std::size_t>(yy) * w + xx;
                        const auto d = delta(original, corrected, q),
                                   v = unpackProxy(nextGuide_[q]);
                        if (!nextGuide_[q][3] || !finite(d)) {
                            valid = false;
                            continue;
                        }
                        for (int c = 0; c < 3; ++c) {
                            r[c] += d[c];
                            g[c] += v[c];
                        }
                        ++mass;
                    }
                lowValid_[i] = valid && mass > 0;
                for (int c = 0; c < 3; ++c) {
                    low_[i][c] = mass > 0 ? r[c] / mass : 0;
                    lowGuide_[i][c] = mass > 0 ? g[c] / mass : 0;
                }
            }
        });
    auto position = [&](int x, int y, float& px, float& py) noexcept {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        px = static_cast<float>(x);
        py = static_cast<float>(y);
        if (useMotion) {
            if (motion.empty()) return false;
            // Host buffers are bottom-left; motion fields are top-left, Y down.
            const auto i = (static_cast<std::size_t>(h - 1 - y) * w + x) * 2;
            px += motion[i];
            py -= motion[i + 1];
        }
        return std::isfinite(px) && std::isfinite(py) && px >= 0 && py >= 0 && px <= w - 1 &&
               py <= h - 1;
    };
    auto gather = [&](float x, float y, Rgb current, std::array<float, 4>& result,
                      float& acceptance) noexcept {
        const int ax = static_cast<int>(std::floor(x)), ay = static_cast<int>(std::floor(y));
        const float fx = x - ax, fy = y - ay;
        float mass = 0, validMass = 0;
        result = {};
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const float v = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                if (v <= 0) continue;
                const int xx = ax + dx, yy = ay + dy;
                if (xx >= w || yy >= h) return false;
                const auto q = static_cast<std::size_t>(yy) * w + xx;
                if (!guide_[q][3] || history_[q][3] < .999f) continue;
                validMass += v;
                const float a = 1 - smooth(.025f, .10f, error(current, unpackProxy(guide_[q])));
                mass += v * a;
                for (int c = 0; c < 4; ++c) result[c] += history_[q][c] * v * a;
            }
        if (validMass < .999f || mass < .25f) return false;
        for (auto& v : result) v /= mass;
        acceptance = mass;
        return true;
    };
    auto reconstruct = [&](int x, int y, Rgb g, Rgb& result) noexcept {
        const float xx = x * .5f - .25f, yy = y * .5f - .25f;
        const int ax = static_cast<int>(std::floor(xx)), ay = static_cast<int>(std::floor(yy));
        const float fx = xx - ax, fy = yy - ay;
        float mass = 0;
        result = {};
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const auto q = static_cast<std::size_t>(std::clamp(ay + dy, 0, lh - 1)) * lw +
                               std::clamp(ax + dx, 0, lw - 1);
                if (!lowValid_[q]) continue;
                const float v = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) *
                                (1 - smooth(.02f, .10f, error(g, lowGuide_[q])));
                mass += v;
                for (int c = 0; c < 3; ++c) result[c] += v * low_[q][c];
            }
        if (mass < .05f) return false;
        for (auto& v : result) v /= mass;
        return true;
    };
    std::atomic<std::uint64_t> accepted = 0, rejected = 0;
    // Kernels read the unfiltered correction; never overwrite it during patch
    // validation. scratch_ holds the result until all workers have finished.
    parallelRows(h, count, [&](int y) noexcept {
        std::uint64_t good = 0, bad = 0;
        for (int x = 0; x < w; ++x) {
            const auto i = static_cast<std::size_t>(y) * w + x;
            const auto d = delta(original, corrected, i), g = unpackProxy(nextGuide_[i]);
            if (!nextGuide_[i][3] || !finite(d) || d == Rgb{} || color.detail.strength == 0) {
                scratch_[i] = {};
                ++bad;
                continue;
            }
            Rgb current = d, detail{};
            if (lowMode) {
                if (!reconstruct(x, y, g, current)) {
                    scratch_[i] = {d[0], d[1], d[2], 0};
                    ++bad;
                    continue;
                }
                for (int c = 0; c < 3; ++c) detail[c] = d[c] - current[c];
            }
            float observed = 1;
            if (settings.mode == AntiFlickerMode::FlowPlus)
                observed = smooth(.005f, .02f,
                                  error(proxy(rgb(corrected, i), original[i * 4 + 3], color), g));
            float support = observed;
            Rgb result = current;
            float px = 0, py = 0;
            std::array<float, 4> old{};
            float tapAcceptance = 0;
            bool valid =
                weight > 0 && position(x, y, px, py) && gather(px, py, g, old, tapAcceptance);
            float sum = 0, maximum = 0, mass = 0;
            Rgb lo = d, hi = d;
            if (valid)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        const auto q = static_cast<std::size_t>(yy) * w + xx;
                        float qx = 0, qy = 0;
                        std::array<float, 4> sample{};
                        float qa = 0;
                        const auto r = delta(original, corrected, q),
                                   ng = unpackProxy(nextGuide_[q]);
                        if (!nextGuide_[q][3] || !finite(r) || !position(xx, yy, qx, qy) ||
                            (useMotion && (std::abs((qx - xx) - (px - x)) > 2 ||
                                           std::abs((qy - yy) - (py - y)) > 2)) ||
                            !gather(qx, qy, ng, sample, qa)) {
                            valid = false;
                            continue;
                        }
                        const int nx = static_cast<int>(std::round(qx)),
                                  ny = static_cast<int>(std::round(qy));
                        const float e =
                            error(ng, unpackProxy(guide_[static_cast<std::size_t>(ny) * w + nx]));
                        sum += e;
                        maximum = std::max(maximum, e);
                        ++mass;
                        for (int c = 0; c < 3; ++c) {
                            lo[c] = std::min(lo[c], r[c]);
                            hi[c] = std::max(hi[c], r[c]);
                        }
                    }
            const float confidence = valid && mass > 0
                                         ? tapAcceptance * (1 - smooth(.008f, .04f, sum / mass)) *
                                               (1 - smooth(.025f, .10f, maximum))
                                         : 0;
            if (confidence > 0) {
                ++good;
                Rgb safe{};
                for (int c = 0; c < 3; ++c) {
                    const float margin = .02f * (1 + std::max(std::abs(lo[c]), std::abs(hi[c]))) +
                                         confidence * std::abs(old[c]);
                    safe[c] = std::clamp(old[c], lo[c] - margin, hi[c] + margin);
                }
                if (settings.mode == AntiFlickerMode::FlowPlus) {
                    const float oldSupport = std::clamp(old[3] - 1, 0.f, 1.f);
                    const float memory =
                        static_cast<float>(std::exp(-dt / (observed > oldSupport ? .06 : .18))) *
                        confidence;
                    support = observed + (oldSupport - observed) * memory;
                    const float dot =
                        current[0] * old[0] + current[1] * old[1] + current[2] * old[2];
                    if (oldSupport <= .001f || (observed > 0 && dot < 0)) {
                        result = current;
                        support = observed;
                    } else
                        for (int c = 0; c < 3; ++c)
                            result[c] = safe[c] + (current[c] - safe[c]) *
                                                      (1 - weight * confidence) * observed;
                } else
                    for (int c = 0; c < 3; ++c)
                        result[c] = current[c] + (safe[c] - current[c]) * weight * confidence;
            } else
                ++bad;
            scratch_[i] = {result[0], result[1], result[2],
                           settings.mode == AntiFlickerMode::FlowPlus ? 1 + support : 1};
            // The low-frequency detail complement is recomputed in the final
            // write loop; the stored history contains only low observations.
        }
        accepted.fetch_add(good, std::memory_order_relaxed);
        rejected.fetch_add(bad, std::memory_order_relaxed);
    });
    parallelRows(h, count, [&](int y) noexcept {
        for (int x = 0; x < w; ++x) {
            const auto i = static_cast<std::size_t>(y) * w + x;
            const auto d = delta(original, corrected, i);
            if (!finite(rgb(original, i)) || !finite(d)) {
                for (int c = 0; c < 3; ++c)
                    corrected[i * 4 + c] =
                        std::isfinite(original[i * 4 + c]) ? original[i * 4 + c] : 0;
            } else if (scratch_[i][3] >= .999f) {
                Rgb low{};
                if (lowMode) reconstruct(x, y, unpackProxy(nextGuide_[i]), low);
                const float support =
                    settings.mode == AntiFlickerMode::FlowPlus ? scratch_[i][3] - 1 : 1;
                for (int c = 0; c < 3; ++c) {
                    const float stable = scratch_[i][c] * support + (lowMode ? d[c] - low[c] : 0);
                    const float change = d[c] + settings.strength * (stable - d[c]);
                    const float value = original[i * 4 + c] + change;
                    corrected[i * 4 + c] = std::isfinite(value) ? value : original[i * 4 + c];
                }
            }
            corrected[i * 4 + 3] = original[i * 4 + 3];
        }
    });
    history_.swap(scratch_);
    guide_.swap(nextGuide_);
    cachedOutput_ = corrected;
    committed_ = Key{context, keyColor, settings};
    stats_.accepted += accepted.load();
    stats_.rejected += rejected.load();
    ++stats_.frames;
}
}  // namespace resolve_dlss5
