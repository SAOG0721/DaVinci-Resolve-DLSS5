// SPDX-License-Identifier: MIT

#pragma once

#include "Feature18Parameters.h"

#include <memory>
#include <string>

namespace resolve_dlss5 {

// Appends a timestamped line to
// %LOCALAPPDATA%\ResolveDlss5\ResolveDlss5.log and OutputDebugString.
void writeDiagnosticLog(const std::string& message) noexcept;

// CPU-facing prototype backend. Resolve supplies float RGBA frames; this
// class owns the verified Magpie-style D3D12 Feature 18 session internally.
class Feature18Runtime final {
public:
    Feature18Runtime();
    ~Feature18Runtime();

    Feature18Runtime(const Feature18Runtime&) = delete;
    Feature18Runtime& operator=(const Feature18Runtime&) = delete;

    bool process(
        const float* source,
        int sourceRowBytes,
        float* destination,
        int destinationRowBytes,
        int width,
        int height,
        const Feature18Settings& settings,
        bool resetHistory);

    [[nodiscard]] const std::string& lastError() const noexcept;
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace resolve_dlss5
