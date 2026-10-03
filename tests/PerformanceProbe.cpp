// SPDX-License-Identifier: MIT
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "ColorPipeline.h"
#include "Feature18Runtime.h"

using namespace resolve_dlss5;
using Clock = std::chrono::steady_clock;
template <class F>
double measure(F&& operation) {
    const auto start = Clock::now();
    operation();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::uint64_t fingerprint(const std::vector<float>& pixels) {
    std::uint64_t value = 14695981039346656037ULL;
    for (auto *p = reinterpret_cast<const unsigned char*>(pixels.data()),
              *end = p + pixels.size() * sizeof(float);
         p != end; ++p)
        value = (value ^ *p) * 1099511628211ULL;
    return value;
}
int main(int argc, char** argv) try {
    const int width = argc > 1 ? std::stoi(argv[1]) : 1920;
    const int height = argc > 2 ? std::stoi(argv[2]) : 1080;
    const bool gpu = argc > 3 && std::string(argv[3]) == "--gpu";
    if (width <= 0 || height <= 0 || width > 3840 || height > 2160)
        throw std::runtime_error("Probe extent must fit 3840x2160");
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<float> original(count * 4), neural(count * 4), output;
    for (std::size_t i = 0; i < count; ++i) {
        const float x = static_cast<float>(i % width) / width;
        const float y = static_cast<float>(i / width) / height;
        original[i * 4] = x;
        original[i * 4 + 1] = y;
        original[i * 4 + 2] = .25f + .1f * std::sin(x * 120);
        original[i * 4 + 3] = 1;
        for (int c = 0; c < 3; ++c)
            neural[i * 4 + c] = std::clamp(original[i * 4 + c] * .99f + .006f, 0.f, 1.f);
        neural[i * 4 + 3] = 1;
    }
    Feature18Settings settings;
    settings.inputEncoding = InputEncoding::SdrSrgb;
    auto print = [&](const char* name, double ms, std::uint64_t hash = 0) {
        std::cout << "{\"stage\":\"" << name << "\",\"width\":" << width << ",\"height\":" << height
                  << ",\"ms\":" << ms << ",\"fingerprint\":\"" << hash << "\"}\n"
                  << std::flush;
    };
    for (int mode = 0; mode < 3; ++mode) {
        auto s = settings;
        if (mode > 0) {
            s.detail.hueProtection = .3f;
            s.detail.darkProtection = .1f;
            s.detail.compression = .5f;
        }
        if (mode == 2) {
            s.detail.lowFrequency = .8f;
            s.detail.highFrequency = 1.2f;
        }
        double sum = 0;
        for (int iteration = 0; iteration < 3; ++iteration) {
            const auto elapsed =
                measure([&] { compositeProxyFrame(original, neural, width, height, s, output); });
            if (iteration > 0) sum += elapsed;
        }
        print(mode == 0   ? "cpu-default"
              : mode == 1 ? "cpu-protection"
                          : "cpu-frequency",
              sum / 2, fingerprint(output));
    }
    if (gpu) {
        Feature18Runtime runtime;
        output.resize(original.size());
        for (int iteration = 0; iteration < 4; ++iteration) {
            RuntimeTimings timings;
            const double elapsed = measure([&] {
                if (!runtime.process(original.data(), width * 16, output.data(), width * 16, width,
                                     height, settings, iteration == 0, true, nullptr, &timings))
                    throw std::runtime_error(runtime.lastError());
            });
            print(iteration == 0 ? "nr-cold" : "nr-warm", elapsed);
            print("nr-prepare", timings.prepareMs);
            print("nr-submit-wait", timings.submitWaitMs);
            print("nr-readback", timings.readbackMs);
        }
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
