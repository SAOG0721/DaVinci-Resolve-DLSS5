// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Feature18Parameters.h"

namespace resolve_dlss5 {
using Rgb = std::array<float, 3>;
using Rgba8 = std::array<std::uint8_t, 4>;
struct ControlMetrics {
    float deltaL = 0, deltaC = 0, protection = 1, compression = 1, gamut = 1;
};
// CPU reference is also the initial CPU OFX production bridge. RGB math is
// unassociated. The host base and alpha never pass through the neural codec.
Rgb encodeNeuralProxy(Rgb host, const Feature18Settings& settings) noexcept;
Rgb decodeNeuralProxy(Rgb proxy, const Feature18Settings& settings) noexcept;
Rgba8 quantizeNeuralProxy(Rgb proxy) noexcept;
Rgb unpackProxy(Rgba8 pixel) noexcept;
Rgb controlProxy(Rgb original, Rgb neural, const DetailSettings& settings,
                 ControlMetrics* metrics = nullptr) noexcept;
Rgb netHostCorrection(Rgb baseline, Rgb controlled, const Feature18Settings& settings) noexcept;
void controlFrame(std::span<const Rgba8> baseline, std::span<const Rgba8> neural, int width,
                  int height, const DetailSettings& settings, std::vector<Rgb>& controlled,
                  std::vector<ControlMetrics>* metrics = nullptr);
bool validColorSettings(const Feature18Settings& settings) noexcept;
void compositeProxyFrame(std::span<const float> original, std::span<const float> neural, int width,
                         int height, const Feature18Settings& settings, std::vector<float>& output);
}  // namespace resolve_dlss5
