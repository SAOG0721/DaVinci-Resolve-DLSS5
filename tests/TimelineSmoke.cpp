// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "Feature18Runtime.h"
#include "TimelineProcessor.h"
using namespace resolve_dlss5;
int main() try {
    Feature18Runtime streamingRuntime, referenceRuntime;
    auto frameAt = [](double time) {
        CpuFrame frame;
        frame.width = 640;
        frame.height = 360;
        frame.pixels.resize(static_cast<size_t>(frame.width) * frame.height * 4);
        frame.settings.inputEncoding = InputEncoding::SdrSrgb;
        frame.settings.intensity = time < 3 ? .8f : .9f;
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x) {
                const size_t i = (static_cast<size_t>(y) * frame.width + x) * 4;
                frame.pixels[i] = x / 639.f;
                frame.pixels[i + 1] = y / 359.f;
                frame.pixels[i + 2] =
                    ((x + static_cast<int>(time) * 3) / 16 + y / 16) % 2 ? .8f : .2f;
                frame.pixels[i + 3] = (x % 3 == 0) ? .3f : 1;
            }
        return frame;
    };
    auto process = [](Feature18Runtime& runtime, const CpuFrame& frame, bool reset) {
        std::vector<float> result(frame.pixels.size());
        if (!runtime.process(frame.pixels.data(), frame.width * 16, result.data(), frame.width * 16,
                             frame.width, frame.height, frame.settings, reset, true))
            throw std::runtime_error(runtime.lastError());
        return result;
    };
    auto evaluate = [&](const CpuFrame& frame, bool reset) {
        return process(streamingRuntime, frame, reset);
    };
    auto recovery = [&] { streamingRuntime.reset(); };
    auto compare = [](const std::vector<float>& actual, const std::vector<float>& expected) {
        float error = 0;
        for (size_t i = 0; i < actual.size(); ++i)
            error = std::max(error, std::abs(actual[i] - expected[i]));
        if (error > 2e-6f)
            throw std::runtime_error("Streaming output differs from direct NGX reset policy");
        return error;
    };
    TimelineProcessor timeline;
    float maximum = 0;
    TimelineProcessor::Output last;
    for (int time = 0; time <= 5; ++time) {
        const auto frame = frameAt(time);
        last = timeline.render(time, frame, evaluate, recovery);
        maximum = std::max(
            maximum, compare(*last, process(referenceRuntime, frame, time == 0 || time == 3)));
    }
    const auto count = timeline.stats().evaluatedFrames;
    if (timeline.render(5, frameAt(5), evaluate, recovery) != last ||
        timeline.stats().evaluatedFrames != count)
        throw std::runtime_error("Duplicate request advanced NGX history");
    auto detailEdit = frameAt(5);
    detailEdit.settings.detail.strength = .25f;
    if (timeline.render(5, detailEdit, evaluate, recovery) != last)
        throw std::runtime_error("Detail edit discarded the neural output cache");
    const auto jumpFrame = frameAt(5000);
    const auto jumped = timeline.render(5000, jumpFrame, evaluate, recovery);
    if (timeline.stats().evaluatedFrames != count + 1)
        throw std::runtime_error("Cold jump replayed historical frames");
    maximum = std::max(maximum, compare(*jumped, process(referenceRuntime, jumpFrame, true)));
    const auto backwardsFrame = frameAt(2);
    const auto backwards = timeline.render(2, backwardsFrame, evaluate, recovery);
    maximum =
        std::max(maximum, compare(*backwards, process(referenceRuntime, backwardsFrame, true)));
    const auto exportStart =
        timeline.render(2, backwardsFrame, evaluate, recovery, {}, TimelineDomain::Export);
    maximum =
        std::max(maximum, compare(*exportStart, process(referenceRuntime, backwardsFrame, true)));
    std::cout << "Real NGX streaming passed; max_abs_error=" << maximum
              << " evaluations=" << timeline.stats().evaluatedFrames
              << " cached_bytes=" << timeline.stats().cachedBytes << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
