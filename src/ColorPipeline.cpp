// SPDX-License-Identifier: MIT
#include "ColorPipeline.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ParallelRows.h"

namespace resolve_dlss5 {
namespace {
float finite(float x) { return std::isfinite(x) ? x : 0; }
float signedPow(float x, float p) { return std::copysign(std::pow(std::abs(x), p), x); }
float srgbToLinear(float x) {
    const float a = std::abs(x);
    return std::copysign(a <= .04045f ? a / 12.92f : std::pow((a + .055f) / 1.055f, 2.4f), x);
}
float linearToSrgb(float x) {
    const float a = std::abs(x);
    return std::copysign(a <= .0031308f ? 12.92f * a : 1.055f * std::pow(a, 1 / 2.4f) - .055f, x);
}
Rgb to709(Rgb x) {
    return {1.660491f * x[0] - .587641f * x[1] - .072850f * x[2],
            -.124550f * x[0] + 1.132900f * x[1] - .008349f * x[2],
            -.018151f * x[0] - .100579f * x[1] + 1.118730f * x[2]};
}
Rgb to2020(Rgb x) {
    return {.627404f * x[0] + .329283f * x[1] + .043313f * x[2],
            .069097f * x[0] + .919540f * x[1] + .011362f * x[2],
            .016391f * x[0] + .088013f * x[1] + .895595f * x[2]};
}
constexpr double m1 = 2610.0 / 16384, m2 = 2523.0 / 32;
constexpr double c1 = 3424.0 / 4096, c2 = 2413.0 / 128, c3 = 2392.0 / 128;
float pqToNits(float x) {
    double v = std::pow(std::min<double>(std::abs(x), 1), 1 / m2);
    return std::copysign(
        static_cast<float>(10000 *
                           std::pow(std::max(v - c1, 0.0) / std::max(c2 - c3 * v, 1e-12), 1 / m1)),
        x);
}
float nitsToPq(float x) {
    double v = std::pow(std::abs(x) / 10000, m1);
    return std::copysign(static_cast<float>(std::pow((c1 + c2 * v) / (1 + c3 * v), m2)), x);
}
constexpr float hlgA = .17883277f, hlgB = .28466892f, hlgC = .55991073f;
float hlgInverse(float x) {
    float v = std::abs(x);
    return std::copysign(v <= .5f ? v * v / 3 : (std::exp((v - hlgC) / hlgA) + hlgB) / 12, x);
}
float hlgForward(float x) {
    float v = std::abs(x);
    return std::copysign(v <= 1.f / 12 ? std::sqrt(3 * v) : hlgA * std::log(12 * v - hlgB) + hlgC,
                         x);
}
float luminance2020(Rgb x) { return .2627f * x[0] + .6780f * x[1] + .0593f * x[2]; }
float hlgGamma(const Feature18Settings& s) { return 1.2f + .42f * std::log10(s.peakNits / 1000); }
bool hdr(InputEncoding e) {
    return e == InputEncoding::Pq || e == InputEncoding::Hlg || e == InputEncoding::LinearScRgb ||
           e == InputEncoding::LinearRec2020;
}
Rgb hostLinear(Rgb x, const Feature18Settings& s) {
    const auto e = s.inputEncoding;
    for (float& v : x) {
        v = finite(v);
        if (e == InputEncoding::Automatic || e == InputEncoding::SdrSrgb)
            v = srgbToLinear(v);
        else if (e == InputEncoding::SdrRec709)
            v = signedPow(v, 2.4f);
        else if (e == InputEncoding::Pq)
            v = pqToNits(v) / s.referenceWhiteNits;
        else if (e == InputEncoding::Hlg)
            v = hlgInverse(v);
    }
    if (e == InputEncoding::Hlg) {
        const float lum = std::max(std::abs(luminance2020(x)), 1e-8f);
        const float gain = std::pow(lum, hlgGamma(s) - 1) * s.peakNits / s.referenceWhiteNits;
        for (float& v : x) v *= gain;
    }
    // Linear inputs use 1 = reference white; scRGB primaries are Rec.709.
    if (e == InputEncoding::Pq || e == InputEncoding::Hlg || e == InputEncoding::LinearRec2020)
        x = to709(x);
    return x;
}
Rgb hostEncoded(Rgb x, const Feature18Settings& s) {
    const auto e = s.inputEncoding;
    if (e == InputEncoding::Pq || e == InputEncoding::Hlg || e == InputEncoding::LinearRec2020)
        x = to2020(x);
    if (e == InputEncoding::Hlg) {
        const float displayLum =
            std::max(std::abs(luminance2020(x)) * s.referenceWhiteNits / s.peakNits, 1e-8f);
        const float sceneLum = std::pow(displayLum, 1 / hlgGamma(s));
        const float gain = s.referenceWhiteNits / s.peakNits * std::pow(sceneLum, 1 - hlgGamma(s));
        for (float& v : x) v *= gain;
    }
    for (float& v : x) {
        if (e == InputEncoding::Automatic || e == InputEncoding::SdrSrgb)
            v = linearToSrgb(v);
        else if (e == InputEncoding::SdrRec709)
            v = signedPow(v, 1 / 2.4f);
        else if (e == InputEncoding::Pq)
            v = nitsToPq(v * s.referenceWhiteNits);
        else if (e == InputEncoding::Hlg)
            v = hlgForward(v);
        v = finite(v);
    }
    return x;
}
Rgb lab(Rgb x) {
    for (float& v : x) v = srgbToLinear(v);
    float l = std::cbrt(.41222147f * x[0] + .53633254f * x[1] + .05144599f * x[2]);
    float m = std::cbrt(.21190350f * x[0] + .68069955f * x[1] + .10739696f * x[2]);
    float b = std::cbrt(.08830246f * x[0] + .28171884f * x[1] + .62997870f * x[2]);
    return {.21045426f * l + .79361779f * m - .00407205f * b,
            1.97799850f * l - 2.42859221f * m + .45059371f * b,
            .02590404f * l + .78277177f * m - .80867577f * b};
}
Rgb unlab(Rgb x) {
    float l = x[0] + .39633778f * x[1] + .21580376f * x[2];
    float m = x[0] - .10556135f * x[1] - .06385417f * x[2];
    float b = x[0] - .08948418f * x[1] - 1.29148555f * x[2];
    l = l * l * l;
    m = m * m * m;
    b = b * b * b;
    return {linearToSrgb(4.07674166f * l - 3.30771159f * m + .23096993f * b),
            linearToSrgb(-1.26843800f * l + 2.60975740f * m - .34131940f * b),
            linearToSrgb(-.00419609f * l - .70341861f * m + 1.70761470f * b)};
}
bool inGamut(Rgb x) {
    return x[0] >= 0 && x[1] >= 0 && x[2] >= 0 && x[0] <= 1 && x[1] <= 1 && x[2] <= 1;
}
Rgb mapLab(Rgb x, float& scale) {
    x[0] = std::clamp(x[0], 0.f, 1.f);
    Rgb result = unlab(x);
    scale = 1;
    if (!inGamut(result)) {
        float lo = 0, hi = 1;
        for (int i = 0; i < 24; ++i) {
            float a = (lo + hi) * .5f;
            if (inGamut(unlab({x[0], x[1] * a, x[2] * a})))
                lo = a;
            else
                hi = a;
        }
        scale = lo;
        result = unlab({x[0], x[1] * lo, x[2] * lo});
    }
    for (float& v : result) v = std::clamp(v, 0.f, 1.f);
    return result;
}
float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
}  // namespace

bool validColorSettings(const Feature18Settings& s) noexcept {
    bool valid = std::isfinite(s.referenceWhiteNits) && s.referenceWhiteNits >= 1 &&
                 std::isfinite(s.peakNits) && s.peakNits >= 100 &&
                 s.peakNits >= s.referenceWhiteNits && s.peakNits <= 10000 &&
                 std::isfinite(s.paperWhiteScale) && s.paperWhiteScale >= .1f &&
                 s.paperWhiteScale <= 8 && std::isfinite(s.hdrTransferStrength) &&
                 s.hdrTransferStrength >= 0 && s.hdrTransferStrength <= 1 &&
                 static_cast<int>(s.inputEncoding) >= 0 && static_cast<int>(s.inputEncoding) <= 6;
    for (float x : {s.detail.strength, s.detail.lightness, s.detail.chroma, s.detail.darkening,
                    s.detail.brightening, s.detail.lowFrequency, s.detail.highFrequency})
        valid &= std::isfinite(x) && x >= 0 && x <= 2;
    for (float x : {s.detail.hueProtection, s.detail.darkProtection, s.detail.highlightProtection,
                    s.detail.compression})
        valid &= std::isfinite(x) && x >= 0 && x <= 1;
    return valid;
}
Rgb encodeNeuralProxy(Rgb host, const Feature18Settings& s) noexcept {
    if (s.inputEncoding == InputEncoding::Automatic || s.inputEncoding == InputEncoding::SdrSrgb) {
        for (float& v : host) v = std::clamp(finite(v), 0.f, 1.f);
        return host;
    }
    Rgb x = hostLinear(host, s);
    const float range = std::asinh(s.peakNits / s.referenceWhiteNits * s.paperWhiteScale);
    for (float& v : x) {
        if (hdr(s.inputEncoding))
            v = .94f * std::asinh(std::max(v, 0.f) * s.paperWhiteScale) / range;
        else
            v = linearToSrgb(v);
        v = std::clamp(finite(v), 0.f, 1.f);
    }
    return x;
}
Rgb decodeNeuralProxy(Rgb x, const Feature18Settings& s) noexcept {
    if (s.inputEncoding == InputEncoding::Automatic || s.inputEncoding == InputEncoding::SdrSrgb) {
        for (float& v : x) v = std::clamp(finite(v), 0.f, 1.f);
        return x;
    }
    const float range = std::asinh(s.peakNits / s.referenceWhiteNits * s.paperWhiteScale);
    for (float& v : x) {
        v = std::clamp(finite(v), 0.f, 1.f);
        v = hdr(s.inputEncoding) ? std::sinh(v / .94f * range) / s.paperWhiteScale
                                 : srgbToLinear(v);
    }
    return hostEncoded(x, s);
}
Rgba8 quantizeNeuralProxy(Rgb x) noexcept {
    return {static_cast<std::uint8_t>(std::lround(std::clamp(finite(x[0]), 0.f, 1.f) * 255)),
            static_cast<std::uint8_t>(std::lround(std::clamp(finite(x[1]), 0.f, 1.f) * 255)),
            static_cast<std::uint8_t>(std::lround(std::clamp(finite(x[2]), 0.f, 1.f) * 255)), 255};
}
Rgb unpackProxy(Rgba8 x) noexcept { return {x[0] / 255.f, x[1] / 255.f, x[2] / 255.f}; }
Rgb controlProxy(Rgb o, Rgb n, const DetailSettings& s, ControlMetrics* out) noexcept {
    ControlMetrics metrics;
    if (s.strength == 0 || o == n) {
        if (out) *out = metrics;
        return o;
    }
    auto perceptual = s;
    perceptual.lowFrequency = 1;
    perceptual.highFrequency = 1;
    const bool neutral = perceptual == DetailSettings{};
    if (neutral) {
        if (out) {
            Rgb a = lab(o), b = lab(n);
            metrics.deltaL = b[0] - a[0];
            metrics.deltaC = std::hypot(b[1] - a[1], b[2] - a[2]);
            *out = metrics;
        }
        return n;
    }
    Rgb candidate{};
    for (int i = 0; i < 3; ++i) candidate[i] = o[i] + (n[i] - o[i]) * s.strength;
    const Rgb base = lab(o), target = lab(candidate);
    Rgb d = {target[0] - base[0], target[1] - base[1], target[2] - base[2]};
    d[0] *= s.lightness * (d[0] < 0 ? s.darkening : s.brightening);
    d[1] *= s.chroma;
    d[2] *= s.chroma;
    float c = std::hypot(base[1], base[2]);
    if (s.hueProtection > 0 && c > 1e-12f) {
        float nx = base[1] / c, ny = base[2] / c, tx = base[1] + d[1], ty = base[2] + d[2];
        float projection = std::max(tx * nx + ty * ny, 0.f),
              w = s.hueProtection * smooth(0, .02f, c);
        d[1] = tx + (nx * projection - tx) * w - base[1];
        d[2] = ty + (ny * projection - ty) * w - base[2];
    }
    float dark = s.darkProtection * (1 - smooth(.08f, .35f, base[0]));
    float bright = s.highlightProtection * smooth(.72f, .98f, base[0]);
    float headroom = 0;
    for (int i = 0; i < 3; ++i)
        headroom = std::max(headroom, std::max(candidate[i] - o[i], 0.f) / (1 - o[i] + 1e-4f));
    bright =
        std::max(bright * (.5f + .5f * smooth(0, .02f, d[0])), bright * smooth(.5f, 2, headroom));
    metrics.protection = (1 - dark) * (1 - bright);
    for (float& v : d) v *= metrics.protection;
    float magnitude = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (magnitude > .04f)
        metrics.compression =
            1 + s.compression *
                    ((.04f + (magnitude - .04f) / (1 + (magnitude - .04f) / .12f)) / magnitude - 1);
    for (float& v : d) v *= metrics.compression;
    metrics.deltaL = d[0];
    metrics.deltaC = std::hypot(d[1], d[2]);
    Rgb result = mapLab({base[0] + d[0], base[1] + d[1], base[2] + d[2]}, metrics.gamut);
    if (out) *out = metrics;
    return result;
}
Rgb netHostCorrection(Rgb baseline, Rgb controlled, const Feature18Settings& s) noexcept {
    if (baseline == controlled || s.detail.strength == 0) return {};
    Rgb a = decodeNeuralProxy(baseline, s), b = decodeNeuralProxy(controlled, s), delta{};
    float strength = hdr(s.inputEncoding) ? std::clamp(s.hdrTransferStrength, 0.f, 1.f) : 1;
    for (int i = 0; i < 3; ++i) delta[i] = finite((b[i] - a[i]) * strength);
    return delta;
}
void controlFrame(std::span<const Rgba8> baseline, std::span<const Rgba8> neural, int width,
                  int height, const DetailSettings& s, std::vector<Rgb>& controlled,
                  std::vector<ControlMetrics>* metrics) {
    if (width <= 0 || height <= 0 || baseline.size() != static_cast<size_t>(width) * height ||
        neural.size() != baseline.size())
        throw std::invalid_argument("Invalid proxy frame extent");
    controlled.resize(baseline.size());
    if (metrics) metrics->resize(baseline.size());
    parallelRows(height, baseline.size(), [&](int y) noexcept {
        const size_t end = static_cast<size_t>(y + 1) * width;
        for (size_t i = static_cast<size_t>(y) * width; i < end; ++i)
            controlled[i] = controlProxy(unpackProxy(baseline[i]), unpackProxy(neural[i]), s,
                                         metrics ? &(*metrics)[i] : nullptr);
    });
    if ((s.lowFrequency == 1 && s.highFrequency == 1) || s.strength == 0) return;
    const auto initial = controlled;
    parallelRows(height, baseline.size(), [&](int y) noexcept {
        for (int x = 0; x < width; ++x) {
            const size_t p = static_cast<size_t>(y) * width + x;
            Rgb o = unpackProxy(baseline[p]), low{};
            float mass = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    size_t q = static_cast<size_t>(std::clamp(y + dy, 0, height - 1)) * width +
                               std::clamp(x + dx, 0, width - 1);
                    Rgb guide = unpackProxy(baseline[q]);
                    float diff = 0;
                    for (int c = 0; c < 3; ++c) diff += (guide[c] - o[c]) * (guide[c] - o[c]);
                    float w =
                        (dx == 0 ? 2.f : 1.f) * (dy == 0 ? 2.f : 1.f) * std::exp(-diff / .005f);
                    for (int c = 0; c < 3; ++c) low[c] += (initial[q][c] - guide[c]) * w;
                    mass += w;
                }
            Rgb candidate{};
            for (int c = 0; c < 3; ++c) {
                low[c] /= mass;
                candidate[c] = o[c] + s.lowFrequency * low[c] +
                               s.highFrequency * (initial[p][c] - o[c] - low[c]);
            }
            if (candidate == o) {
                controlled[p] = o;
                continue;
            }
            float scale = 1;
            controlled[p] = mapLab(lab(candidate), scale);
            if (metrics) (*metrics)[p].gamut *= scale;
        }
    });
}
void compositeProxyFrame(std::span<const float> original, std::span<const float> neural, int width,
                         int height, const Feature18Settings& settings,
                         std::vector<float>& output) {
    const size_t count = static_cast<size_t>(width) * height;
    if (width <= 0 || height <= 0 || original.size() != count * 4 || neural.size() != count * 4 ||
        !validColorSettings(settings))
        throw std::invalid_argument("Invalid composite frame or color settings");
    // Neutral detail needs no intermediate control frame. Keep the exact
    // quantized baseline and net-delta math; this is only loop fusion.
    if (settings.detail == DetailSettings{}) {
        output.resize(original.size());
        parallelRows(height, count, [&](int y) noexcept {
            const size_t end = static_cast<size_t>(y + 1) * width;
            for (size_t i = static_cast<size_t>(y) * width; i < end; ++i) {
                Rgb rgb{original[i * 4], original[i * 4 + 1], original[i * 4 + 2]};
                const float a = original[i * 4 + 3];
                if (settings.premultiplied)
                    for (float& v : rgb) v = std::isfinite(a) && a > 1e-6f ? v / a : 0;
                const auto baseline = quantizeNeuralProxy(encodeNeuralProxy(rgb, settings));
                const auto proxy =
                    quantizeNeuralProxy({neural[i * 4], neural[i * 4 + 1], neural[i * 4 + 2]});
                const auto delta =
                    netHostCorrection(unpackProxy(baseline), unpackProxy(proxy), settings);
                const float gain =
                    settings.premultiplied ? (std::isfinite(a) && a > 1e-6f ? a : 0) : 1;
                for (int c = 0; c < 3; ++c) {
                    const float correction = delta[c] * gain;
                    output[i * 4 + c] = std::isfinite(original[i * 4 + c])
                                            ? (correction == 0 ? original[i * 4 + c]
                                                               : original[i * 4 + c] + correction)
                                            : 0;
                }
                output[i * 4 + 3] = a;
            }
        });
        return;
    }
    std::vector<Rgba8> baseline(count), proxy(count);
    parallelRows(height, count, [&](int y) noexcept {
        const size_t end = static_cast<size_t>(y + 1) * width;
        for (size_t i = static_cast<size_t>(y) * width; i < end; ++i) {
            Rgb rgb{original[i * 4], original[i * 4 + 1], original[i * 4 + 2]};
            const float a = original[i * 4 + 3];
            if (settings.premultiplied)
                for (float& v : rgb) v = std::isfinite(a) && a > 1e-6f ? v / a : 0;
            baseline[i] = quantizeNeuralProxy(encodeNeuralProxy(rgb, settings));
            proxy[i] = quantizeNeuralProxy({neural[i * 4], neural[i * 4 + 1], neural[i * 4 + 2]});
        }
    });
    std::vector<Rgb> controlled;
    controlFrame(baseline, proxy, width, height, settings.detail, controlled);
    output.assign(original.begin(), original.end());
    parallelRows(height, count, [&](int y) noexcept {
        const size_t end = static_cast<size_t>(y + 1) * width;
        for (size_t i = static_cast<size_t>(y) * width; i < end; ++i) {
            const Rgb delta = netHostCorrection(unpackProxy(baseline[i]), controlled[i], settings);
            const float a = original[i * 4 + 3];
            const float gain = settings.premultiplied ? (std::isfinite(a) && a > 1e-6f ? a : 0) : 1;
            for (int c = 0; c < 3; ++c) {
                const float correction = delta[c] * gain;
                output[i * 4 + c] =
                    std::isfinite(original[i * 4 + c])
                        ? (correction == 0 ? original[i * 4 + c] : original[i * 4 + c] + correction)
                        : 0;
            }
        }
    });
}
}  // namespace resolve_dlss5
