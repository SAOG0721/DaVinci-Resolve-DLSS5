// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "ColorPipeline.h"
#include "TimelineProcessor.h"

using namespace resolve_dlss5;
void verifyParallelColor();
namespace {
void require(bool v, const char* message) {
    if (!v) throw std::runtime_error(message);
}
void close(float a, float b, float tolerance, const char* message) {
    require(std::abs(a - b) <= tolerance, message);
}
}  // namespace
int main() try {
    verifyParallelColor();
    for (auto encoding :
         {InputEncoding::SdrSrgb, InputEncoding::SdrRec709, InputEncoding::Pq, InputEncoding::Hlg,
          InputEncoding::LinearScRgb, InputEncoding::LinearRec2020}) {
        Feature18Settings s;
        s.inputEncoding = encoding;
        for (Rgb original :
             {Rgb{-.002f, .501234f, 1.23456f}, Rgb{0, 0, 0}, Rgb{.0001f, .7f, .4f}}) {
            Rgb base = unpackProxy(quantizeNeuralProxy(encodeNeuralProxy(original, s)));
            require(netHostCorrection(base, base, s) == Rgb{},
                    "No-op must be exact for every codec");
            auto controlled = controlProxy(base, base, s.detail);
            require(controlled == base, "Controls must retain zero residual");
            Rgb n = base;
            n[1] = std::clamp(n[1] + 1 / 255.f, 0.f, 1.f);
            s.detail.strength = 0;
            require(netHostCorrection(base, n, s) == Rgb{},
                    "Strength zero must suppress all changes");
        }
    }
    // Independent published transfer-function anchor points, not a test copy
    // of production equations. ST 2084: 100 nits -> 0.5080784, 1000 -> .7518271.
    Feature18Settings pq;
    pq.inputEncoding = InputEncoding::Pq;
    for (auto pair : {std::pair{.5080784f, 100.f}, std::pair{.7518271f, 1000.f}}) {
        Rgb proxy = encodeNeuralProxy({pair.first, pair.first, pair.first}, pq);
        const float expected = .94f * std::asinh(pair.second / 203) / std::asinh(1000.f / 203);
        close(proxy[0], expected, 2e-5f, "PQ physical anchor");
        Rgb restored = decodeNeuralProxy(proxy, pq);
        close(restored[0], pair.first, 2e-5f, "PQ inverse anchor");
    }
    Feature18Settings hlg;
    hlg.inputEncoding = InputEncoding::Hlg;
    Rgb white = decodeNeuralProxy(encodeNeuralProxy({1, 1, 1}, hlg), hlg);
    close(white[0], 1, 2e-5f, "HLG display white");
    Rgb hlgMid = decodeNeuralProxy(encodeNeuralProxy({.5f, .5f, .5f}, hlg), hlg);
    close(hlgMid[0], .5f, 2e-5f, "HLG scene/display inverse");
    Feature18Settings srgb;
    require(
        quantizeNeuralProxy(encodeNeuralProxy({.5f, .5f, .5f}, srgb)) == Rgba8{128, 128, 128, 255},
        "Legacy sRGB input bytes must retain midpoint rounding");
    require(decodeNeuralProxy({.25f, .5f, .75f}, srgb) == Rgb{.25f, .5f, .75f},
            "sRGB neural proxy decode must be identity");
    Rgb original{.5f, .501234f, 1.125f};
    Rgb quantized = unpackProxy(quantizeNeuralProxy(encodeNeuralProxy(original, srgb)));
    require(quantized != original, "Fixture must contain quantization error");
    require(netHostCorrection(quantized, quantized, srgb) == Rgb{},
            "Quantization round-trip is not NR");
    for (auto encoding :
         {InputEncoding::SdrSrgb, InputEncoding::SdrRec709, InputEncoding::Pq, InputEncoding::Hlg,
          InputEncoding::LinearScRgb, InputEncoding::LinearRec2020}) {
        Feature18Settings settings;
        settings.inputEncoding = encoding;
        settings.premultiplied = true;
        std::vector<float> host{-0.0f, .250789f, -.002f, .25f, 1.12567f, .501234f, .7f, 0};
        std::vector<float> proxy(8), composite;
        for (size_t i = 0; i < 2; ++i) {
            Rgb rgb{host[i * 4], host[i * 4 + 1], host[i * 4 + 2]};
            for (float& v : rgb) v = host[i * 4 + 3] > 1e-6f ? v / host[i * 4 + 3] : 0;
            const Rgb baseline = unpackProxy(quantizeNeuralProxy(encodeNeuralProxy(rgb, settings)));
            for (int c = 0; c < 3; ++c) proxy[i * 4 + c] = baseline[c];
            proxy[i * 4 + 3] = 1;
        }
        compositeProxyFrame(host, proxy, 2, 1, settings, composite);
        require(composite == host,
                "Production composite no-op must retain host float and premult alpha exactly");
        require(std::memcmp(composite.data(), host.data(), host.size() * sizeof(float)) == 0,
                "No-op must preserve source bits, including negative zero");
        settings.detail.lowFrequency = .3f;
        settings.detail.highFrequency = 1.7f;
        settings.detail.hueProtection = .5f;
        settings.detail.darkProtection = .4f;
        settings.detail.highlightProtection = .2f;
        settings.detail.compression = .2f;
        compositeProxyFrame(host, proxy, 2, 1, settings, composite);
        require(composite == host,
                "No-op remains exact with nonneutral protection/frequency controls");
        require(std::memcmp(composite.data(), host.data(), host.size() * sizeof(float)) == 0,
                "Frequency/protection no-op must preserve source bits");
        proxy[0] = .9f;
        settings.detail.strength = 0;
        compositeProxyFrame(host, proxy, 2, 1, settings, composite);
        require(composite == host, "Production strength-zero composite must retain host exactly");
    }
    DetailSettings controls;
    controls.hueProtection = 1;
    controls.chroma = 2;
    Rgb previous{};
    float maxJump = 0;
    for (int i = -100; i <= 100; ++i) {
        Rgb o{.5f + i * 1e-6f, .5f, .5f};
        Rgb c = controlProxy(o, {.51f, .49f, .50f}, controls);
        if (i != -100)
            for (int k = 0; k < 3; ++k) maxJump = std::max(maxJump, std::abs(c[k] - previous[k]));
        previous = c;
    }
    require(maxJump < 2e-5f, "Gray-axis hue protection must be continuous");
    std::vector<Rgba8> base(35, {128, 128, 128, 255}), neural = base;
    neural[17] = {160, 140, 100, 255};
    std::vector<Rgb> output;
    controlFrame(base, neural, 7, 5, DetailSettings{}, output);
    for (size_t i = 0; i < base.size(); ++i)
        require(output[i] == unpackProxy(neural[i]), "Neutral controls exact");
    controls = DetailSettings{};
    controls.lowFrequency = 0;
    controls.highFrequency = 0;
    controlFrame(base, neural, 7, 5, controls, output);
    for (size_t i = 0; i < base.size(); ++i)
        for (int k = 0; k < 3; ++k)
            close(output[i][k], unpackProxy(base[i])[k], 3e-6f, "Complementary frequency zero");
    float hidden = 0;
    int evaluations = 0, recoveries = 0;
    auto frameAt = [](double time) {
        CpuFrame f;
        f.width = 2;
        f.height = 2;
        f.pixels.assign(16, static_cast<float>(time));
        return f;
    };
    auto recovery = [&] {
        ++recoveries;
        hidden = 0;
    };
    auto evaluate = [&](const CpuFrame& f, bool reset) {
        ++evaluations;
        if (reset) hidden = 0;
        hidden = hidden * .8f + f.pixels[0];
        auto output = f.pixels;
        output[0] = hidden;
        return output;
    };
    TimelineProcessor timeline(64);
    const auto first = timeline.render(0, frameAt(0), evaluate, recovery);
    const auto next = timeline.render(1, frameAt(1), evaluate, recovery);
    require((*next)[0] == 1 && evaluations == 2, "Sequential frames evaluated once");
    require(timeline.render(1, frameAt(1), evaluate, recovery) == next && evaluations == 2,
            "Duplicate must reuse immutable output and not advance history");
    auto controlsOnly = frameAt(1);
    controlsOnly.settings.detail.strength = .3f;
    controlsOnly.settings.hdrTransferStrength = .6f;
    controlsOnly.settings.additionalPasses[0].intensity = .2f;
    require(timeline.render(1, controlsOnly, evaluate, recovery) == next && evaluations == 2,
            "Output-only and inactive-pass edits reuse the neural output");
    const auto jump = timeline.render(5000, frameAt(5000), evaluate, recovery);
    require((*jump)[0] == 5000 && evaluations == 3,
            "Cold jump evaluates only its current frame and resets history");
    const auto continuation = timeline.render(5001, frameAt(5001), evaluate, recovery);
    require((*continuation)[0] == 9001, "Continuous frame retains hidden state");
    const auto exportStart =
        timeline.render(5001, frameAt(5001), evaluate, recovery, {}, TimelineDomain::Export);
    require((*exportStart)[0] == 5001,
            "Export never inherits preview history or preview cached output");
    auto revised = frameAt(5001);
    revised.pixels[0] = 7;
    require(
        (*timeline.render(5001, revised, evaluate, recovery, {}, TimelineDomain::Export))[0] == 7,
        "Same-time source revision resets and reevaluates");
    auto animated = frameAt(5002);
    animated.settings.intensity = .8f;
    require((*timeline.render(5002, animated, evaluate, recovery, {}, TimelineDomain::Export))[0] ==
                5002,
            "Neural parameter animation boundary resets history");
    timeline.invalidate();
    const auto beforeLong = timeline.stats().evaluatedFrames;
    for (int time = 0; time < 10000; ++time) {
        timeline.render(time, frameAt(time), evaluate, recovery);
        require(timeline.stats().cachedBytes == 64, "Long stream keeps one cached frame");
    }
    require(timeline.stats().evaluatedFrames - beforeLong == 10000,
            "Long stream performs exactly one evaluation per request");
    require((*first)[0] == 0 && (*next)[0] == 1, "Held outputs remain immutable");
    TimelineProcessor uncached(0);
    uncached.render(0, frameAt(0), evaluate, recovery);
    const auto prior = evaluations;
    uncached.render(0, frameAt(0), evaluate, recovery);
    require(evaluations == prior + 1 && uncached.stats().cachedBytes == 0,
            "Oversized cache entries do not bypass the budget");
    bool cancelled = false;
    const auto previousRecoveries = recoveries;
    try {
        timeline.render(4, frameAt(4), evaluate, recovery, [] { return true; });
    } catch (const std::runtime_error&) {
        cancelled = true;
    }
    require(cancelled && recoveries == previousRecoveries + 1 && timeline.stats().cachedBytes == 0,
            "Abort invalidates output and recovers model state");
    bool incomplete = false;
    try {
        timeline.render(
            4, frameAt(4), [](const CpuFrame&, bool) { return std::vector<float>{}; }, recovery);
    } catch (const std::runtime_error&) {
        incomplete = true;
    }
    require(incomplete && timeline.stats().cachedBytes == 0,
            "Incomplete output never enters cache");
    require((*timeline.render(4, frameAt(4), evaluate, recovery))[0] == 4,
            "First request after failure starts clean");
    std::cout << "Core color/control contracts passed; gray_axis_max_jump=" << maxJump << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
