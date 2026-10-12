// SPDX-License-Identifier: MIT
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "AntiFlicker.h"
#include "FrameGuidanceCache.h"
using namespace resolve_dlss5;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void close(float a, float b, float tolerance, const char* message) {
    require(std::abs(a - b) <= tolerance, message);
}
std::vector<float> frame(int w, int h, Rgb value = {.4f, .5f, .6f}) {
    std::vector<float> out(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < out.size() / 4; ++i) {
        for (int c = 0; c < 3; ++c) out[i * 4 + c] = value[c];
        out[i * 4 + 3] = .75f;
    }
    return out;
}
std::vector<float> corrected(const std::vector<float>& base, Rgb change) {
    auto out = base;
    for (std::size_t i = 0; i < out.size() / 4; ++i)
        for (int c = 0; c < 3; ++c) out[i * 4 + c] += change[c];
    return out;
}
TemporalFrameContext context(int w, int h, double time = 0) {
    TemporalFrameContext c;
    c.width = w;
    c.height = h;
    c.time = time;
    c.inputRevision = static_cast<std::uint64_t>(time + 100);
    return c;
}
void contracts() {
    constexpr int w = 17, h = 13;
    const auto base = frame(w, h);
    MotionField zero(w * h * 2);
    Feature18Settings color;
    color.inputEncoding = InputEncoding::SdrSrgb;
    AntiFlickerSettings s;
    AntiFlicker filter;
    auto raw = corrected(base, {.1f, -.06f, .04f}), out = raw;
    filter.process(context(w, h), color, s, base, out, zero);
    require(out == raw && filter.stats().bytes == 0,
            "Off is bit-exact and allocates no temporal history");
    for (auto mode : {AntiFlickerMode::Static, AntiFlickerMode::Flow, AntiFlickerMode::FlowPlus,
                      AntiFlickerMode::LowFrequency}) {
        filter.invalidate();
        s.mode = mode;
        auto first = raw;
        filter.process(context(w, h), color, s, base, first, zero);
        if (mode != AntiFlickerMode::FlowPlus)
            require(first == raw, "Cold start preserves current correction");
        auto nextRaw = corrected(base, {.04f, -.025f, .018f}), next = nextRaw;
        filter.process(context(w, h, 1), color, s, base, next, zero);
        const auto i = (6 * w + 8) * 4;
        require(filter.stats().accepted > 0 && next[i] > nextRaw[i],
                "Every enabled mode stabilizes a valid changing correction");
        require(next[i + 1] < nextRaw[i + 1], "Signed negative correction is retained");
        close(next[i + 3], base[i + 3], 0, "Alpha remains unchanged");
        auto repeat = nextRaw;
        auto frames = filter.stats().frames;
        filter.process(context(w, h, 1), color, s, base, repeat, zero);
        require(repeat == next && filter.stats().frames == frames,
                "Duplicate render never advances EMA/history");
        auto noChange = base;
        filter.process(context(w, h, 2), color, s, base, noChange, zero);
        require(noChange == base, "Exact zero current correction cannot retain old correction");
        auto seek = nextRaw;
        filter.process(context(w, h, 1000), color, s, base, seek, zero);
        if (mode != AntiFlickerMode::FlowPlus)
            require(seek == nextRaw, "Seek resets output history");
        require(filter.stats().historyWeight == 0, "Seek has zero memory");
        auto reverse = raw;
        filter.process(context(w, h, 999), color, s, base, reverse, zero);
        require(filter.stats().historyWeight == 0, "Reverse request resets history");
    }
    // Independent analytical media-time anchor: 24 fps, 80 ms time constant.
    filter.invalidate();
    s.mode = AntiFlickerMode::Static;
    out = raw;
    filter.process(context(w, h), color, s, base, out);
    out = corrected(base, {.04f, -.025f, .018f});
    filter.process(context(w, h, 1), color, s, base, out);
    close(out[0], .4f + .04f + (.1f - .04f) * std::exp(-1.f / 24 / .08f), 2e-6f,
          "Media-time EMA anchor");
    auto faster = context(w, h, 2);
    faster.fps = 48;
    out = raw;
    filter.process(faster, color, s, base, out);
    require(filter.stats().historyWeight == 0, "FPS changes reset history");
    auto mismatched = context(w, h, 3);
    mismatched.hasReference = true;
    mismatched.reference[0] = 123;
    out = raw;
    filter.process(mismatched, color, s, base, out);
    require(filter.stats().historyWeight == 0,
            "Reference revision cannot consume another input trajectory");
    out = raw;
    filter.process(context(w, h, 3), color, s, base, out);
    auto sourceChange = frame(w, h, {.8f, .15f, .1f});
    auto changed = corrected(sourceChange, {.04f, .04f, .04f});
    filter.process(context(w, h, 4), color, s, sourceChange, changed);
    require(changed == corrected(sourceChange, {.04f, .04f, .04f}),
            "Scene change rejects the old input patch");
    color.detail.strength = .7f;
    out = raw;
    filter.process(context(w, h, 5), color, s, base, out);
    require(filter.stats().historyWeight == 0, "Output controls reset temporal history");
    auto domain = context(w, h, 6);
    domain.interactive = false;
    out = raw;
    filter.process(domain, color, s, base, out);
    require(filter.stats().historyWeight == 0, "Export and interactive histories are isolated");
    filter.invalidate();
    s.mode = AntiFlickerMode::Flow;
    out = raw;
    filter.process(context(w, h), color, s, base, out, zero);
    auto unsupported = context(w, h, 2);
    unsupported.frameStep = 2;
    out = raw;
    filter.process(unsupported, color, s, base, out, zero);
    require(filter.stats().historyWeight == 0,
            "Adjacent motion cannot target a skipped history frame");
    filter.invalidate();
    out = raw;
    filter.process(context(w, h), color, s, base, out, zero);
    zero[(static_cast<std::size_t>(h - 1 - 6) * w + 8) * 2] =
        std::numeric_limits<float>::quiet_NaN();
    out = corrected(base, {.04f, -.025f, .018f});
    auto invalidCurrent = out;
    filter.process(context(w, h, 1), color, s, base, out, zero);
    require(out[(6 * w + 8) * 4] == invalidCurrent[(6 * w + 8) * 4],
            "Invalid motion rejects history locally");
    s.strength = 0;
    out = raw;
    filter.process(context(w, h, 2), color, s, base, out, zero);
    require(out == raw && filter.stats().bytes == 0,
            "Zero AF strength preserves current result and frees memory");
    AntiFlicker tiny(100);
    s.strength = 1;
    bool rejected = false;
    try {
        out = raw;
        tiny.process(context(w, h), color, s, base, out, zero);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && tiny.stats().bytes == 0, "Memory limit fails without a partial history");
    // Every supported encoding retains signed correction and HDR headroom.
    for (auto encoding :
         {InputEncoding::SdrSrgb, InputEncoding::SdrRec709, InputEncoding::Pq, InputEncoding::Hlg,
          InputEncoding::LinearScRgb, InputEncoding::LinearRec2020})
        for (auto mode : {AntiFlickerMode::Static, AntiFlickerMode::Flow, AntiFlickerMode::FlowPlus,
                          AntiFlickerMode::LowFrequency}) {
            filter.invalidate();
            color = {};
            color.inputEncoding = encoding;
            s.mode = mode;
            auto hdr = frame(
                w, h,
                encoding == InputEncoding::LinearScRgb || encoding == InputEncoding::LinearRec2020
                    ? Rgb{2.f, -.02f, 3.f}
                    : Rgb{.6f, .4f, .8f});
            auto current = corrected(hdr, {.1f, -.03f, .06f});
            MotionField mv(w * h * 2);
            filter.process(context(w, h), color, s, hdr, current, mv);
            auto next = corrected(hdr, {.04f, -.01f, .025f});
            filter.process(context(w, h, 1), color, s, hdr, next, mv);
            for (std::size_t i = 0; i < next.size() / 4; ++i) {
                for (int c = 0; c < 3; ++c)
                    require(std::isfinite(next[i * 4 + c]), "All codecs produce finite RGB");
                require(next[i * 4 + 3] == hdr[i * 4 + 3], "All codecs preserve alpha");
            }
            if (encoding == InputEncoding::LinearScRgb || encoding == InputEncoding::LinearRec2020)
                require(next[0] > 2 && next[1] < 0 && next[2] > 3,
                        "HDR keeps superwhite and negative signed values");
        }
    // Vertical translation proves OFX bottom-left <-> top-left vector conversion.
    color = {};
    color.inputEncoding = InputEncoding::SdrSrgb;
    s.mode = AntiFlickerMode::Flow;
    filter.invalidate();
    auto previous = base;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) previous[(y * w + x) * 4] = .1f + .025f * y;
    out = corrected(previous, {.1f, .08f, .06f});
    filter.process(context(w, h), color, s, previous, out, MotionField(w * h * 2));
    auto shifted = previous;
    for (int y = 1; y < h; ++y)
        for (int x = 0; x < w; ++x) shifted[(y * w + x) * 4] = previous[((y - 1) * w + x) * 4];
    MotionField down(w * h * 2);
    for (std::size_t i = 0; i < down.size() / 2; ++i) down[i * 2 + 1] = 1;
    out = corrected(shifted, {.04f, .03f, .02f});
    filter.process(context(w, h, 1), color, s, shifted, out, down);
    require(out[(6 * w + 8) * 4] > shifted[(6 * w + 8) * 4] + .04f,
            "Vertical motion aligns the prior host row");
    // Per-frame output parameter edits and input revision changes clear only AF.
    auto revision = context(w, h, 1);
    revision.inputRevision += 100;
    out = corrected(shifted, {.04f, .03f, .02f});
    filter.process(revision, color, s, shifted, out, down);
    require(filter.stats().historyWeight == 0,
            "Same-time source revision rebuilds without advancing history");
    filter.invalidate();
    s.mode = AntiFlickerMode::LowFrequency;
    auto lowFirst = corrected(base, {.1f, .1f, .1f});
    filter.process(context(w, h), color, s, base, lowFirst, MotionField(w * h * 2));
    auto lowNext = base;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                lowNext[(y * w + x) * 4 + c] += .04f + ((x + y) % 2 ? .02f : -.02f);
    const float currentHigh = lowNext[(6 * w + 6) * 4] - lowNext[(6 * w + 5) * 4];
    filter.process(context(w, h, 1), color, s, base, lowNext, MotionField(w * h * 2));
    close(lowNext[(6 * w + 6) * 4] - lowNext[(6 * w + 5) * 4], currentHigh, 2e-6f,
          "Low-frequency accumulation preserves the current high-frequency complement");
    AntiFlicker slow;
    AntiFlickerSettings slowSettings;
    slowSettings.mode = AntiFlickerMode::Static;
    slowSettings.responseSeconds = .5f;
    auto slowContext = context(w, h);
    slowContext.fps = 2;
    auto slowFirst = corrected(base, {.1f, .1f, .1f});
    slow.process(slowContext, color, slowSettings, base, slowFirst);
    slowContext.time = 1;
    slowContext.inputRevision++;
    auto slowNext = corrected(base, {.04f, .04f, .04f});
    slow.process(slowContext, color, slowSettings, base, slowNext);
    close(slowNext[0], base[0] + .04f + .06f * std::exp(-1.f), 2e-6f,
          "Low-FPS media uses media-time response, not the window-capture timeout");
    std::cout << "Temporal contracts: four modes, six codecs, signed HDR, "
                 "time/reset/cache/motion/memory passed\n";
}
void guidance() {
    FlowSettings nr;
    nr.method = FlowMethod::None;
    auto demand = guidanceDemand(nr, true, FlowMethod::Nvidia);
    require(nr.method == FlowMethod::None && demand.method == FlowMethod::Nvidia,
            "AF motion demand is independent of NR None");
    FrameGuidanceCache cache;
    auto base = frame(8, 8);
    int evaluations = 0;
    auto produce = [&] {
        ++evaluations;
        return MotionField(8 * 8 * 2, 1.f);
    };
    Feature18Settings s;
    auto a = cache.get(1, 8, 8, s, demand, base, base, {}, produce);
    auto b = cache.get(1, 8, 8, s, demand, base, base, {}, produce);
    require(a == b && evaluations == 1, "All passes and temporal output share one immutable field");
    s.detail.strength = .1f;
    s.style = 7;
    s.passCount = 3;
    b = cache.get(1, 8, 8, s, demand, base, base, {}, produce);
    require(a == b && evaluations == 1, "Output and NR controls do not recompute raw-input motion");
    demand.amdQuality = 0;
    b = cache.get(1, 8, 8, s, demand, base, base, {}, produce);
    require(a == b && evaluations == 1, "Inactive flow quality is ignored");
    auto previous = base;
    previous[0] += .1f;
    cache.get(1, 8, 8, s, demand, base, previous, {}, produce);
    require(evaluations == 2, "Reference frame revision invalidates motion");
    nr.external = true;
    require(guidanceDemand(nr, true, FlowMethod::Amd).external,
            "External motion satisfies both consumers");
    std::cout << "Guidance demand/cache contracts passed\n";
}
void gpuContracts(ID3D12Device* device) {
    constexpr int w = 73, h = 55;
    for (auto encoding :
         {InputEncoding::SdrSrgb, InputEncoding::SdrRec709, InputEncoding::Pq, InputEncoding::Hlg,
          InputEncoding::LinearScRgb, InputEncoding::LinearRec2020})
        for (int mode = 1; mode <= 4; ++mode) {
            AntiFlicker cpu, gpu;
            gpu.setGpuDevice(device);
            Feature18Settings color;
            color.inputEncoding = encoding;
            AntiFlickerSettings s;
            s.mode = static_cast<AntiFlickerMode>(mode);
            auto base = frame(w, h);
            MotionField motion(w * h * 2);
            for (int t = 0; t < 6; ++t) {
                auto raw = base;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const auto i = (y * w + x) * 4;
                        for (int c = 0; c < 3; ++c)
                            raw[i + c] += static_cast<float>(.015 + .006 * ((x / 3 + y / 4) % 4) +
                                                             .014 * (t % 2)) *
                                          (c == 1 ? -1 : 1);
                        const auto m = (static_cast<std::size_t>(h - 1 - y) * w + x) * 2;
                        motion[m] = t == 3 ? .5f : 0;
                        motion[m + 1] = t == 3 ? -.25f : 0;
                    }
                auto a = raw, b = raw;
                auto ctx = context(w, h, t);
                cpu.process(ctx, color, s, base, a, motion);
                gpu.process(ctx, color, s, base, b, motion);
                float maximum = 0;
                for (std::size_t i = 0; i < a.size(); ++i)
                    maximum = std::max(maximum, std::abs(a[i] - b[i]));
                if (maximum > 3e-6f)
                    throw std::runtime_error(
                        "GPU/CPU difference mode=" + std::to_string(mode) +
                        " codec=" + std::to_string(static_cast<int>(encoding)) +
                        " frame=" + std::to_string(t) + " maximum=" + std::to_string(maximum));
                require(cpu.stats().accepted == gpu.stats().accepted,
                        "GPU confidence rejection matches CPU reference");
                auto duplicate = raw;
                gpu.process(ctx, color, s, base, duplicate, motion);
                require(duplicate == b,
                        "GPU duplicate is exact and does not dispatch/advance history");
            }
        }
    AntiFlicker first, second;
    first.setGpuDevice(device);
    second.setGpuDevice(device);
    Feature18Settings codec;
    codec.inputEncoding = InputEncoding::SdrSrgb;
    AntiFlickerSettings settings;
    settings.mode = AntiFlickerMode::Flow;
    auto original = frame(w, h);
    MotionField zero(w * h * 2);
    auto positive = corrected(original, {.1f, .1f, .1f}),
         negative = corrected(original, {-.1f, -.1f, -.1f});
    first.process(context(w, h), codec, settings, original, positive, zero);
    second.process(context(w, h), codec, settings, original, negative, zero);
    positive = corrected(original, {.04f, .04f, .04f});
    negative = corrected(original, {-.04f, -.04f, -.04f});
    first.process(context(w, h, 1), codec, settings, original, positive, zero);
    second.process(context(w, h, 1), codec, settings, original, negative, zero);
    require(positive[0] > .44f && negative[0] < .36f,
            "Live nodes cannot share mutable temporal history");
    const auto bytes = first.stats().gpuBytes;
    for (int t = 2; t < 202; ++t) {
        auto value = corrected(original, {.04f, .04f, .04f});
        first.process(context(w, h, t), codec, settings, original, value, zero);
        require(first.stats().gpuBytes == bytes, "Long GPU stream cannot grow the resource pool");
    }
    std::cout << "Real D3D12 GPU: 24 mode/codec sequences, fractional motion, confidence and "
                 "duplicate comparison passed\n";
}
void benchmark(int w, int h, ID3D12Device* device = nullptr) {
    auto base = frame(w, h);
    MotionField mv(static_cast<std::size_t>(w) * h * 2);
    Feature18Settings color;
    color.inputEncoding = InputEncoding::SdrSrgb;
    for (int mode = 0; mode <= 4; ++mode) {
        AntiFlicker filter;
        AntiFlickerSettings s;
        s.mode = static_cast<AntiFlickerMode>(mode);
        if (device && mode) filter.setGpuDevice(device);
        double sum = 0;
        for (int i = 0; i < 4; ++i) {
            auto out = corrected(base, i % 2 ? Rgb{.04f, -.025f, .018f} : Rgb{.1f, -.06f, .04f});
            auto start = std::chrono::steady_clock::now();
            filter.process(context(w, h, i), color, s, base, out, mv);
            if (i)
                sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                 start)
                           .count();
        }
        std::cout << "{\"backend\":\"" << (device ? "gpu" : "cpu") << "\",\"mode\":" << mode
                  << ",\"width\":" << w << ",\"height\":" << h << ",\"ms\":" << sum / 3
                  << ",\"history_bytes\":" << filter.stats().bytes
                  << ",\"gpu_pool_bytes\":" << filter.stats().gpuBytes << "}\n"
                  << std::flush;
    }
}
}  // namespace
int main(int argc, char** argv) try {
    contracts();
    guidance();
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (argc == 2 || argc == 4) {
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "DXGI factory failed");
        require(SUCCEEDED(factory->EnumAdapterByGpuPreference(
                    0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))),
                "GPU adapter not found");
        require(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                            IID_PPV_ARGS(&device))),
                "D3D12 GPU creation failed");
        gpuContracts(device.Get());
    }
    if (argc >= 3) benchmark(std::stoi(argv[1]), std::stoi(argv[2]), device.Get());
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
