// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "Feature18Runtime.h"
#include "OpticalFlow.h"
#include "TimelineProcessor.h"

using namespace resolve_dlss5;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        constexpr int w = 320, h = 192, dx = 4, dy = 2;
        Feature18Settings codec;
        codec.skinStructureStrength = 0;
        std::vector<float> previous(w * h * 4), current(w * h * 4);
        uint32_t seed = 12345;
        // A textured pattern translated in top-left screen coordinates.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                const auto i = (static_cast<std::size_t>(h - 1 - y) * w + x) * 4;
                const float v = .1f + .8f * ((seed & 65535) / 65535.f);
                for (int c = 0; c < 3; ++c) previous[i + c] = v;
                previous[i + 3] = 1;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto a = (static_cast<std::size_t>(h - 1 - y) * w + x) * 4;
                const auto b = (static_cast<std::size_t>(h - 1 - std::clamp(y - dy, 0, h - 1)) * w +
                                std::clamp(x - dx, 0, w - 1)) *
                               4;
                std::copy_n(previous.begin() + b, 4, current.begin() + a);
            }
        FlowSettings external;
        external.external = true;
        external.units = FlowUnits::Normalized;
        external.yUp = true;
        std::vector<float> rgba(w * h * 4);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i] = -4.f / w;
            rgba[i + 1] = 2.f / h;
        }
        auto mv = adaptExternalMotion(rgba, w, h, external);
        require(std::abs(mv[0] + 4) < 1e-6 && std::abs(mv[1] + 2) < 1e-6,
                "External unit/Y conversion failed");
        auto invalid = rgba;
        invalid[0] = std::numeric_limits<float>::infinity();
        bool rejected = false;
        try {
            adaptExternalMotion(invalid, w, h, external);
        } catch (...) {
            rejected = true;
        }
        require(rejected, "Invalid external vector accepted");
        TimelineProcessor timeline;
        CpuFrame frame;
        frame.width = w;
        frame.height = h;
        frame.pixels = current;
        frame.flow = external;
        frame.motion = mv;
        int evaluations = 0;
        auto evaluate = [&](const CpuFrame& f, bool) {
            ++evaluations;
            return f.pixels;
        };
        timeline.render(0, frame, evaluate, [] {});
        timeline.render(0, frame, evaluate, [] {});
        require(evaluations == 1, "Same external frame advanced NR history");
        frame.motion[0] -= 1;
        timeline.render(0, frame, evaluate, [] {});
        require(evaluations == 2, "Changed external motion reused stale result");
        TimelineProcessor paired;
        frame.flow.external = false;
        frame.flow.method = FlowMethod::Nvidia;
        frame.motion.clear();
        frame.referencePixels = previous;
        paired.render(0, frame, evaluate, [] {});
        frame.referencePixels = frame.pixels;
        bool observedReset = true;
        paired.render(
            1, frame,
            [&](const CpuFrame& f, bool reset) {
                observedReset = reset;
                return f.pixels;
            },
            [] {});
        require(!observedReset, "Unchanged previous source reset continuous history");
        frame.referencePixels[0] += .01f;
        paired.render(
            2, frame,
            [&](const CpuFrame& f, bool reset) {
                observedReset = reset;
                return f.pixels;
            },
            [] {});
        require(observedReset, "Revised reference reused stale NR history");
        for (int method = 1; method <= 2; ++method)
            for (int q = 0; q < (method == 1 ? 2 : 5); ++q) {
                OpticalFlow flow;
                FlowSettings settings;
                settings.method = static_cast<FlowMethod>(method);
                settings.amdQuality = q;
                settings.nvidiaQuality = q + 1;
                auto result = flow.estimate(current, previous, w, h, codec, settings);
                require(result.size() == w * h * 2, "Wrong estimated motion extent");
                std::vector<float> xs, ys;
                for (int y = 40; y < h - 40; ++y)
                    for (int x = 40; x < w - 40; ++x) {
                        auto i = (static_cast<std::size_t>(y) * w + x) * 2;
                        require(std::isfinite(result[i]) && std::isfinite(result[i + 1]),
                                "Non-finite optical flow");
                        xs.push_back(result[i]);
                        ys.push_back(result[i + 1]);
                    }
                auto median = [](std::vector<float>& v) {
                    auto m = v.begin() + v.size() / 2;
                    std::nth_element(v.begin(), m, v.end());
                    return *m;
                };
                const float mx = median(xs), my = median(ys);
                std::cout << (method == 1 ? "AMDOF" : "NVOF") << " quality=" << q << " median=("
                          << mx << "," << my << ")\n";
                require(std::abs(mx + dx) < 1.25f && std::abs(my + dy) < 1.25f,
                        "Translation direction/units mismatch");
                if (method == 1) {
                    auto stable = flow.estimate(current, current, w, h, codec, settings);
                    for (int y = 40; y < h - 40; ++y)
                        for (int x = 40; x < w - 40; ++x) {
                            const auto i = (static_cast<std::size_t>(y) * w + x) * 2;
                            require(std::abs(stable[i]) < 1.f && std::abs(stable[i + 1]) < 1.f,
                                    "Continuous AMD history did not track the confirmed reference");
                        }
                }
                if (q == 0) {
                    Feature18Runtime runtime;
                    std::vector<float> output(current.size());
                    codec.passCount = 3;
                    require(runtime.process(previous.data(), w * 16, output.data(), w * 16, w, h,
                                            codec, true, true),
                            runtime.lastError().c_str());
                    require(runtime.process(current.data(), w * 16, output.data(), w * 16, w, h,
                                            codec, false, true, result.data()),
                            runtime.lastError().c_str());
                    require(std::all_of(output.begin(), output.end(),
                                        [](float v) { return std::isfinite(v); }),
                            "NR with real motion produced non-finite output");
                    // None must clear the previous motion texture, not leave stale guidance.
                    require(runtime.process(current.data(), w * 16, output.data(), w * 16, w, h,
                                            codec, true, true),
                            runtime.lastError().c_str());
                }
            }
        std::cout << "External + AMDOF/NVOF profiles + real NGX motion contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
