// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "ColorPipeline.h"
#include "Feature18Runtime.h"
#include "GpuResourcePool.h"
using namespace resolve_dlss5;
using Microsoft::WRL::ComPtr;
void require(bool x, const char* message) {
    if (!x) throw std::runtime_error(message);
}
int main() try {
    ComPtr<ID3D12Device> device;
    require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))),
            "D3D12 test device unavailable");
    auto pool = std::make_shared<GpuResourcePool>(device.Get(), 4 * 1024 * 1024);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Fence> fence;
    require(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))),
            "Fence unavailable");
    auto first = pool->acquire(desc, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAG_NONE,
                               D3D12_RESOURCE_STATE_GENERIC_READ);
    auto* originalResource = first.get();
    const auto originalGeneration = first.generation();
    first.submitted(fence.Get(), 1);
    first = {};
    auto second = pool->acquire(desc, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAG_NONE,
                                D3D12_RESOURCE_STATE_GENERIC_READ);
    require(second.get() != originalResource, "In-flight GPU lease was reused");
    require(SUCCEEDED(fence->Signal(1)), "Cannot complete synthetic consumer fence");
    auto third = pool->acquire(desc, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAG_NONE,
                               D3D12_RESOURCE_STATE_GENERIC_READ);
    require(third.get() == originalResource && third.generation() != originalGeneration,
            "Completed lease not reused with new generation");
    require(pool->stats().hits == 1 && pool->stats().allocations == 2,
            "Pool hit/allocation counters wrong");
    // Real D3D resources and real fences above; the unresolved consumer fence
    // is deliberate and has no queued GPU workload. No NGX substitute is used.
    Feature18Runtime runtime, other;
    constexpr int w = 640, h = 360;
    std::vector<float> input(static_cast<size_t>(w) * h * 4), output(input.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            input[i] = .501234f + .1f * std::sin(x * .11f);
            input[i + 1] = .1f + y / 359.f;
            input[i + 2] = -.002f + x / 639.f;
            input[i + 3] = x % 4 == 0 ? 0.f : x % 4 == 1 ? .25f : 1;
        }
    for (int count : {1, 2, 3, 2, 1})
        for (auto encoding :
             {InputEncoding::SdrSrgb, InputEncoding::SdrRec709, InputEncoding::Pq,
              InputEncoding::Hlg, InputEncoding::LinearScRgb, InputEncoding::LinearRec2020}) {
            Feature18Settings s;
            s.passCount = count;
            s.inputEncoding = encoding;
            s.premultiplied = true;
            s.detail.hueProtection = .3f;
            s.detail.darkProtection = .1f;
            s.detail.compression = .5f;
            s.detail.lowFrequency = .8f;
            s.detail.highFrequency = 1.2f;
            s.additionalPasses[0].intensity = .8f;
            s.additionalPasses[1].intensity = .6f;
            require(runtime.process(input.data(), w * 16, output.data(), w * 16, w, h, s, true),
                    runtime.lastError().c_str());
            for (size_t i = 0; i < input.size(); ++i) {
                if (i % 4 == 3)
                    require(output[i] == input[i], "Premult source alpha changed");
                else
                    require(std::isfinite(output[i]), "SDR/HDR multi-pass output nonfinite");
            }
        }
    Feature18Settings s;
    require(other.process(input.data(), w * 16, output.data(), w * 16, w, h, s, true),
            other.lastError().c_str());
    runtime.reset();
    require(other.process(input.data(), w * 16, output.data(), w * 16, w, h, s, false),
            "Releasing sibling destroyed active NGX session");
    // Equivalent logical source represented with negative OFX strides.
    std::vector<float> flipped(input.size()), positive(input.size()), negative(input.size());
    for (int y = 0; y < h; ++y)
        std::copy_n(input.data() + static_cast<size_t>(y) * w * 4, w * 4,
                    flipped.data() + static_cast<size_t>(h - 1 - y) * w * 4);
    runtime.reset();
    require(runtime.process(input.data(), w * 16, positive.data(), w * 16, w, h, s, true),
            "Positive-stride processing failed");
    runtime.reset();
    require(runtime.process(flipped.data() + static_cast<size_t>(h - 1) * w * 4, -w * 16,
                            negative.data() + static_cast<size_t>(h - 1) * w * 4, -w * 16, w, h, s,
                            true),
            "Negative-stride processing failed");
    float maximum = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w * 4; ++x)
            maximum =
                std::max(maximum, std::abs(positive[static_cast<size_t>(y) * w * 4 + x] -
                                           negative[static_cast<size_t>(h - 1 - y) * w * 4 + x]));
    require(maximum < 2e-6f, "Negative OFX stride changes logical result");
    for (const auto extent : {std::pair{480, 270}, std::pair{288, 162}}) {
        const auto [pw, ph] = extent;
        std::vector<float> padded(static_cast<size_t>(pw) * ph * 4, .4f), result(padded.size());
        for (size_t i = 3; i < padded.size(); i += 4) padded[i] = 1;
        require(
            runtime.process(padded.data(), pw * 16, result.data(), pw * 16, pw, ph, s, true, true),
            "Unaligned-width readback failed");
        require(
            runtime.process(padded.data(), pw * 16, result.data(), pw * 16, pw, ph, s, false, true),
            "Repeated unaligned-width readback failed");
        for (float value : result)
            require(std::isfinite(value), "Padded readback returned nonfinite data");
    }
    std::cout << "Real NGX 1/2/3-pass SDR/HDR + instance lifetime + padded readback + fence pool "
                 "contracts passed; "
                 "stride_error="
              << maximum << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
