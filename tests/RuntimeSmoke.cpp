// SPDX-License-Identifier: MIT

#include "Feature18Runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

int main() {
    constexpr int width = 640;
    constexpr int height = 360;
    std::vector<float> source(
        static_cast<std::size_t>(width) * height * 4U);
    std::vector<float> destination(source.size(), 0.0F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            source[offset + 0] = static_cast<float>(x) / (width - 1);
            source[offset + 1] = static_cast<float>(y) / (height - 1);
            source[offset + 2] = ((x / 16 + y / 16) & 1) ? 0.8F : 0.2F;
            source[offset + 3] = 1.0F;
        }
    }

    resolve_dlss5::Feature18Settings settings;
    settings.enabled = true;
    settings.guidanceMode = resolve_dlss5::GuidanceMode::ForceZero;
    settings.inputEncoding = resolve_dlss5::InputEncoding::SdrSrgb;

    resolve_dlss5::Feature18Runtime runtime;
    const int rowBytes = width * 4 * static_cast<int>(sizeof(float));
    if (!runtime.process(
            source.data(),
            rowBytes,
            destination.data(),
            rowBytes,
            width,
            height,
            settings,
            true)) {
        std::cerr << "Feature 18 smoke test failed: "
                  << runtime.lastError() << '\n';
        return 1;
    }

    // Resolve can keep a render instance and a UI/proxy instance alive at
    // once. Verify that both sessions share the process-level caller hook,
    // and that the first session remains valid while the second exists.
    std::vector<float> secondDestination(source.size(), 0.0F);
    resolve_dlss5::Feature18Runtime secondRuntime;
    if (!secondRuntime.process(
            source.data(),
            rowBytes,
            secondDestination.data(),
            rowBytes,
            width,
            height,
            settings,
            true)) {
        std::cerr << "Feature 18 second-runtime test failed: "
                  << secondRuntime.lastError() << '\n';
        return 1;
    }
    if (!runtime.process(
            source.data(),
            rowBytes,
            destination.data(),
            rowBytes,
            width,
            height,
            settings,
            false)) {
        std::cerr << "Feature 18 shared-hook reuse test failed: "
                  << runtime.lastError() << '\n';
        return 1;
    }

    // Exercise a second temporal evaluation without resetting Feature 18.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            source[offset + 2] =
                (((x + 3) / 16 + y / 16) & 1) ? 0.8F : 0.2F;
        }
    }
    if (!runtime.process(
            source.data(),
            rowBytes,
            destination.data(),
            rowBytes,
            width,
            height,
            settings,
            false)) {
        std::cerr << "Feature 18 second-frame test failed: "
                  << runtime.lastError() << '\n';
        return 1;
    }

    double absoluteDifference = 0.0;
    float minimum = 1.0F;
    float maximum = 0.0F;
    for (std::size_t index = 0; index < destination.size(); index += 4U) {
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const float value = destination[index + channel];
            if (!std::isfinite(value)) {
                std::cerr << "Feature 18 returned a non-finite value\n";
                return 2;
            }
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            absoluteDifference += std::abs(
                static_cast<double>(value - source[index + channel]));
        }
    }
    absoluteDifference /=
        static_cast<double>(width) * height * 3.0;
    std::cout << "Feature 18 smoke test passed; mean_abs_diff="
              << absoluteDifference << " range=[" << minimum << ", "
              << maximum << "]\n";
    return 0;
}
