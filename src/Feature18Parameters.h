// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>

namespace resolve_dlss5 {

enum class NrPreset : int {
    Preset1 = 1,
    Preset2 = 2,
    Preset3 = 3,
};

enum class GuidanceMode : int {
    ForceZero = 0,
    MotionOnly = 1,
    DepthOnly = 2,
    Available = 3,
};

enum class DepthConvention : int {
    UseInputFlag = 0,
    ForceNormal = 1,
    ForceInverted = 2,
};

enum class InputEncoding : int {
    Automatic = 0,
    SdrSrgb = 1,
    LinearScRgb = 2,
    Pq = 3,
    SdrRec709 = 4,
    Hlg = 5,
    LinearRec2020 = 6,
};

struct DetailSettings {
    float strength = 1;
    float lightness = 1;
    float chroma = 1;
    float darkening = 1;
    float brightening = 1;
    float hueProtection = 0;
    float darkProtection = 0;
    float highlightProtection = 0;
    float compression = 0;
    float lowFrequency = 1;
    float highFrequency = 1;
    bool operator==(const DetailSettings&) const = default;
};

struct NrPassSettings {
    NrPreset preset = NrPreset::Preset1;
    bool uiCorrection = false;
    int style = 0;
    float intensity = 1, localToneStrength = 1, localStructureStrength = 1,
          skinStructureStrength = 0;
    bool useAutoMask = false;
    bool operator==(const NrPassSettings&) const = default;
};

struct Feature18Settings {
    bool enabled = true;
    NrPreset preset = NrPreset::Preset1;
    bool uiCorrection = false;

    int style = 0;
    float intensity = 1.0F;
    float localToneStrength = 1.0F;
    float localStructureStrength = 1.0F;
    float skinStructureStrength = 1.0F;
    bool useAutoMask = false;

    InputEncoding inputEncoding = InputEncoding::Automatic;
    float paperWhiteScale = 1.0F;
    float hdrTransferStrength = 1.0F;
    float referenceWhiteNits = 203.0F;
    float peakNits = 1000.0F;
    bool premultiplied = false;
    DetailSettings detail;
    int passCount = 1;
    std::array<NrPassSettings, 2> additionalPasses{};

    GuidanceMode guidanceMode = GuidanceMode::ForceZero;
    DepthConvention depthConvention = DepthConvention::UseInputFlag;
    float motionScaleX = 1.0F;
    float motionScaleY = 1.0F;

    bool operator==(const Feature18Settings&) const = default;
};

namespace ngx_parameter {

inline constexpr char Width[] = "DLSSNR.Width";
inline constexpr char Height[] = "DLSSNR.Height";
inline constexpr char InputWidth[] = "DLSSNR.InputWidth";
inline constexpr char InputHeight[] = "DLSSNR.InputHeight";
inline constexpr char OutputWidth[] = "DLSSNR.OutputWidth";
inline constexpr char OutputHeight[] = "DLSSNR.OutputHeight";
inline constexpr char Upscaling[] = "DLSSNR.Upscaling";
inline constexpr char Scale[] = "DLSSNR.Scale";
inline constexpr char ScalingRatio[] = "DLSSNR.ScalingRatio";
inline constexpr char ScalingRatioCallback[] = "DLSSNRComputeScalingRatioCallback";
inline constexpr char Preset[] = "DLSSNR.Hint.Render.Preset";

inline constexpr char Color[] = "DLSSNR.Color";
inline constexpr char Output[] = "DLSSNR.Output";
inline constexpr char MotionVectors[] = "DLSSNR.MVec";
inline constexpr char Depth[] = "DLSSNR.Depth";
inline constexpr char MotionScaleX[] = "DLSSNR.MVecScaleX";
inline constexpr char MotionScaleY[] = "DLSSNR.MVecScaleY";
inline constexpr char DepthInverted[] = "DLSSNR.DepthInverted";
inline constexpr char Enabled[] = "DLSSNR.Enabled";
inline constexpr char Reset[] = "DLSSNR.Reset";
inline constexpr char Style[] = "DLSSNR.Style";
inline constexpr char Intensity[] = "DLSSNR.Intensity";
inline constexpr char LocalToneStrength[] = "DLSSNR.LocalToneStrength";
inline constexpr char LocalStructureStrength[] = "DLSSNR.LocalStructureStrength";
inline constexpr char SkinStructureStrength[] = "DLSSNR.SkinStructureStrength";
inline constexpr char UseAutoMask[] = "DLSSNR.UseAutoMask";
inline constexpr char UiCorrection[] = "DLSSNR.UICorrection";
inline constexpr char OutputDotWidth[] = "DLSSNR.Output.Width";
inline constexpr char OutputDotHeight[] = "DLSSNR.Output.Height";
inline constexpr char IndicatorInvertX[] = "DLSS.Indicator.Invert.X.Axis";
inline constexpr char IndicatorInvertY[] = "DLSS.Indicator.Invert.Y.Axis";

inline constexpr char ColorSubrectBaseX[] = "DLSSNR.ColorSubrectBaseX";
inline constexpr char ColorSubrectBaseY[] = "DLSSNR.ColorSubrectBaseY";
inline constexpr char ColorSubrectWidth[] = "DLSSNR.ColorSubrectWidth";
inline constexpr char ColorSubrectHeight[] = "DLSSNR.ColorSubrectHeight";
inline constexpr char OutputSubrectBaseX[] = "DLSSNR.OutputSubrectBaseX";
inline constexpr char OutputSubrectBaseY[] = "DLSSNR.OutputSubrectBaseY";
inline constexpr char OutputSubrectWidth[] = "DLSSNR.OutputSubrectWidth";
inline constexpr char OutputSubrectHeight[] = "DLSSNR.OutputSubrectHeight";
inline constexpr char MotionSubrectBaseX[] = "DLSSNR.MVecSubrectBaseX";
inline constexpr char MotionSubrectBaseY[] = "DLSSNR.MVecSubrectBaseY";
inline constexpr char MotionSubrectWidth[] = "DLSSNR.MVecSubrectWidth";
inline constexpr char MotionSubrectHeight[] = "DLSSNR.MVecSubrectHeight";
inline constexpr char DepthSubrectBaseX[] = "DLSSNR.DepthSubrectBaseX";
inline constexpr char DepthSubrectBaseY[] = "DLSSNR.DepthSubrectBaseY";
inline constexpr char DepthSubrectWidth[] = "DLSSNR.DepthSubrectWidth";
inline constexpr char DepthSubrectHeight[] = "DLSSNR.DepthSubrectHeight";

}  // namespace ngx_parameter

}  // namespace resolve_dlss5
