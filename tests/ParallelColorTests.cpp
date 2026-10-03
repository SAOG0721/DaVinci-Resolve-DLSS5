// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstring>
#include <future>
#include <limits>
#include <stdexcept>

#include "ColorPipeline.h"
#include "ParallelRows.h"

using namespace resolve_dlss5;
namespace {
void equal(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)))
        throw std::runtime_error("Parallel/neutral composite changed pixel bits");
}
}  // namespace
void verifyParallelColor() {
    constexpr int w = 256, h = 256;
    constexpr size_t count = w * h;
    std::vector<float> original(count * 4), neural(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const float alpha = i % 7 == 0 ? 0 : i % 3 == 0 ? .25f : 1;
        for (int c = 0; c < 3; ++c) {
            original[i * 4 + c] =
                (static_cast<float>((i * (c + 3)) % 257) / 256 * 1.4f - .2f) * alpha;
            neural[i * 4 + c] = static_cast<float>((i * (c + 5)) % 257) / 256;
        }
        original[i * 4 + 3] = alpha;
        neural[i * 4 + 3] = 1;
    }
    original[4] = std::numeric_limits<float>::quiet_NaN();
    original[9] = std::numeric_limits<float>::infinity();
    original[15] = std::numeric_limits<float>::quiet_NaN();
    for (auto encoding :
         {InputEncoding::SdrSrgb, InputEncoding::SdrRec709, InputEncoding::Pq, InputEncoding::Hlg,
          InputEncoding::LinearScRgb, InputEncoding::LinearRec2020}) {
        Feature18Settings s;
        s.inputEncoding = encoding;
        s.premultiplied = true;
        s.hdrTransferStrength = .7f;
        std::vector<float> parallel, serial;
        compositeProxyFrame(original, neural, w, h, s, parallel);
        // Independent per-pixel public contract checks the fused neutral path.
        serial = original;
        for (size_t i = 0; i < count; ++i) {
            const float a = original[i * 4 + 3];
            Rgb rgb{original[i * 4], original[i * 4 + 1], original[i * 4 + 2]};
            for (float& v : rgb) v = std::isfinite(a) && a > 1e-6f ? v / a : 0;
            const auto base = unpackProxy(quantizeNeuralProxy(encodeNeuralProxy(rgb, s)));
            const auto nr = unpackProxy(
                quantizeNeuralProxy({neural[i * 4], neural[i * 4 + 1], neural[i * 4 + 2]}));
            const auto delta = netHostCorrection(base, controlProxy(base, nr, s.detail), s);
            const float gain = std::isfinite(a) && a > 1e-6f ? a : 0;
            for (int c = 0; c < 3; ++c) {
                const float correction = delta[c] * gain;
                serial[i * 4 + c] =
                    std::isfinite(original[i * 4 + c])
                        ? (correction == 0 ? original[i * 4 + c] : original[i * 4 + c] + correction)
                        : 0;
            }
        }
        equal(parallel, serial);
        s.detail.hueProtection = .4f;
        s.detail.darkProtection = .3f;
        s.detail.highlightProtection = .2f;
        s.detail.compression = .6f;
        s.detail.lowFrequency = .8f;
        s.detail.highFrequency = 1.2f;
        compositeProxyFrame(original, neural, w, h, s, parallel);
        row_detail::active = true;
        compositeProxyFrame(original, neural, w, h, s, serial);
        row_detail::active = false;
        equal(parallel, serial);
    }
    // Concurrent instances share a bounded pool and complete all callbacks
    // before their buffers are consumed or destroyed.
    Feature18Settings s;
    auto one = std::async(std::launch::async, [&] {
        std::vector<float> output;
        compositeProxyFrame(original, neural, w, h, s, output);
        return output;
    });
    std::vector<float> two;
    compositeProxyFrame(original, neural, w, h, s, two);
    equal(one.get(), two);
}
