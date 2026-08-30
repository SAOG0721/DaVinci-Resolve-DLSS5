// SPDX-License-Identifier: MIT

#include "ResolveDlss5Plugin.h"

#include "Feature18Parameters.h"
#include "Feature18Runtime.h"

#include <cuda.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "ofxsProcessing.h"

namespace {

constexpr char kPluginName[] = "DLSS Neural Video Experimental";
constexpr char kPluginGrouping[] = "DLSS Experimental";
constexpr char kPluginDescription[] =
    "Experimental same-resolution DLSS neural video filter. The initial "
    "implementation exposes the RenoDX/Magpie parameter contract, runs the "
    "signed D3D12 Feature 18 path with zero guidance, and safely falls back "
    "to the source frame if initialization or evaluation fails.";
constexpr char kPluginIdentifier[] = "com.saog.resolve.dlss5";
constexpr int kPluginVersionMajor = 0;
constexpr int kPluginVersionMinor = 3;

constexpr char kParamEnabled[] = "nrEnabled";
constexpr char kParamPreset[] = "nrPreset";
constexpr char kParamUiCorrection[] = "nrUiCorrection";
constexpr char kParamStyle[] = "nrStyle";
constexpr char kParamIntensity[] = "nrIntensity";
constexpr char kParamLocalTone[] = "nrLocalTone";
constexpr char kParamLocalStructure[] = "nrLocalStructure";
constexpr char kParamSkinStructure[] = "nrSkinStructure";
constexpr char kParamAutoMask[] = "nrAutoMask";
constexpr char kParamInputEncoding[] = "nrInputEncoding";
constexpr char kParamPaperWhite[] = "nrPaperWhiteScale";
constexpr char kParamHdrTransfer[] = "nrHdrTransferStrength";
constexpr char kParamGuidanceMode[] = "nrGuidanceMode";
constexpr char kParamDepthConvention[] = "nrDepthConvention";
constexpr char kParamMotionScaleX[] = "nrMotionScaleX";
constexpr char kParamMotionScaleY[] = "nrMotionScaleY";
constexpr char kParamOutputMix[] = "nrOutputMix";
constexpr char kParamOutputView[] = "nrOutputView";
constexpr char kParamReset[] = "nrReset";

constexpr bool kSupportsTiles = false;
constexpr bool kSupportsMultiResolution = false;
constexpr bool kSupportsMultipleClipPars = false;

enum class OutputView : int {
    Processed = 0,
    DifferenceX10 = 1,
    LeftRightCompare = 2,
};

class PassthroughProcessor final : public OFX::ImageProcessor {
public:
    explicit PassthroughProcessor(OFX::ImageEffect& effect)
        : OFX::ImageProcessor(effect) {}

    void setSource(OFX::Image* source) noexcept { source_ = source; }

    void processImagesCUDA() override {
        if (!source_ || !_dstImg) {
            OFX::throwSuiteStatusException(kOfxStatErrBadHandle);
        }

        const OfxRectI& sourceBounds = source_->getBounds();
        const OfxRectI& destinationBounds = _dstImg->getBounds();
        const int width = std::min(
            sourceBounds.x2 - sourceBounds.x1,
            destinationBounds.x2 - destinationBounds.x1);
        const int height = std::min(
            sourceBounds.y2 - sourceBounds.y1,
            destinationBounds.y2 - destinationBounds.y1);
        if (width <= 0 || height <= 0) {
            return;
        }

        const int sourcePitch = source_->getRowBytes();
        const int destinationPitch = _dstImg->getRowBytes();
        if (sourcePitch <= 0 || destinationPitch <= 0) {
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }

        const auto sourcePointer = reinterpret_cast<CUdeviceptr>(
            source_->getPixelData());
        const auto destinationPointer = reinterpret_cast<CUdeviceptr>(
            _dstImg->getPixelData());
        if (sourcePointer == destinationPointer) {
            return;
        }

        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.srcDevice = sourcePointer;
        copy.srcPitch = static_cast<std::size_t>(sourcePitch);
        copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.dstDevice = destinationPointer;
        copy.dstPitch = static_cast<std::size_t>(destinationPitch);
        copy.WidthInBytes = static_cast<std::size_t>(width) * sizeof(float) * 4U;
        copy.Height = static_cast<std::size_t>(height);

        const CUresult result = cuMemcpy2DAsync(
            &copy, reinterpret_cast<CUstream>(_pCudaStream));
        if (result != CUDA_SUCCESS) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    void multiThreadProcessImages(OfxRectI window) override {
        if (!source_ || !_dstImg) {
            return;
        }
        for (int y = window.y1; y < window.y2; ++y) {
            if (_effect.abort()) {
                break;
            }
            const auto* source = static_cast<const float*>(
                source_->getPixelAddress(window.x1, y));
            auto* destination = static_cast<float*>(
                _dstImg->getPixelAddress(window.x1, y));
            if (!source || !destination) {
                continue;
            }
            const std::size_t bytes = static_cast<std::size_t>(
                window.x2 - window.x1) * sizeof(float) * 4U;
            std::memcpy(destination, source, bytes);
        }
    }

private:
    OFX::Image* source_ = nullptr;
};

class ResolveDlss5Plugin final : public OFX::ImageEffect {
public:
    explicit ResolveDlss5Plugin(OfxImageEffectHandle handle)
        : OFX::ImageEffect(handle),
          destinationClip_(fetchClip(kOfxImageEffectOutputClipName)),
          sourceClip_(fetchClip(kOfxImageEffectSimpleSourceClipName)),
          enabled_(fetchBooleanParam(kParamEnabled)),
          preset_(fetchChoiceParam(kParamPreset)),
          uiCorrection_(fetchBooleanParam(kParamUiCorrection)),
          style_(fetchChoiceParam(kParamStyle)),
          intensity_(fetchDoubleParam(kParamIntensity)),
          localTone_(fetchDoubleParam(kParamLocalTone)),
          localStructure_(fetchDoubleParam(kParamLocalStructure)),
          skinStructure_(fetchDoubleParam(kParamSkinStructure)),
          autoMask_(fetchBooleanParam(kParamAutoMask)),
          inputEncoding_(fetchChoiceParam(kParamInputEncoding)),
          paperWhite_(fetchDoubleParam(kParamPaperWhite)),
          hdrTransfer_(fetchDoubleParam(kParamHdrTransfer)),
          guidanceMode_(fetchChoiceParam(kParamGuidanceMode)),
          depthConvention_(fetchChoiceParam(kParamDepthConvention)),
          motionScaleX_(fetchDoubleParam(kParamMotionScaleX)),
          motionScaleY_(fetchDoubleParam(kParamMotionScaleY)),
          outputMix_(fetchDoubleParam(kParamOutputMix)),
          outputView_(fetchChoiceParam(kParamOutputView)),
          reset_(fetchPushButtonParam(kParamReset)),
          runtime_(std::make_unique<resolve_dlss5::Feature18Runtime>()) {
        resolve_dlss5::writeDiagnosticLog("OFX effect instance created");
    }

    ~ResolveDlss5Plugin() override {
        resolve_dlss5::writeDiagnosticLog("OFX effect instance destroyed");
    }

    void render(const OFX::RenderArguments& arguments) override {
        std::unique_ptr<OFX::Image> destination(
            destinationClip_->fetchImage(arguments.time));
        std::unique_ptr<OFX::Image> source(
            sourceClip_->fetchImage(arguments.time));
        if (!destination || !source) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (destination->getPixelDepth() != OFX::eBitDepthFloat ||
            source->getPixelDepth() != OFX::eBitDepthFloat ||
            destination->getPixelComponents() != OFX::ePixelComponentRGBA ||
            source->getPixelComponents() != OFX::ePixelComponentRGBA) {
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }

        const auto settings = settingsAt(arguments.time);
        const OfxRectI sourceBounds = source->getBounds();
        const OfxRectI destinationBounds = destination->getBounds();
        const int width = std::min(
            sourceBounds.x2 - sourceBounds.x1,
            destinationBounds.x2 - destinationBounds.x1);
        const int height = std::min(
            sourceBounds.y2 - sourceBounds.y1,
            destinationBounds.y2 - destinationBounds.y1);

        const bool discontinuity =
            std::isfinite(lastRenderedTime_) &&
            std::abs(arguments.time - lastRenderedTime_ - 1.0) > 1.0e-6;
        const bool settingsChanged =
            previousSettings_.has_value() &&
            previousSettings_.value() != settings;
        const bool resetHistory =
            resetRequested_ || discontinuity || settingsChanged;
        ++renderCount_;
        if (renderCount_ <= 3 || renderCount_ % 120 == 0) {
            resolve_dlss5::writeDiagnosticLog(
                "OFX render called: frame=" + std::to_string(renderCount_) +
                ", time=" + std::to_string(arguments.time) +
                ", size=" + std::to_string(width) + "x" +
                std::to_string(height) +
                ", enabled=" + (settings.enabled ? "1" : "0") +
                ", reset=" + (resetHistory ? "1" : "0"));
        }

        if (settings.enabled && width > 0 && height > 0 &&
            sourceBounds.x1 == destinationBounds.x1 &&
            sourceBounds.y1 == destinationBounds.y1) {
            const auto* sourcePixels = static_cast<const float*>(
                source->getPixelAddress(sourceBounds.x1, sourceBounds.y1));
            auto* destinationPixels = static_cast<float*>(
                destination->getPixelAddress(
                    destinationBounds.x1, destinationBounds.y1));
            const bool processed = runtime_->process(
                sourcePixels,
                source->getRowBytes(),
                destinationPixels,
                destination->getRowBytes(),
                width,
                height,
                settings,
                resetHistory);
            resetRequested_ = false;
            lastRenderedTime_ = arguments.time;
            previousSettings_ = settings;
            if (processed) {
                applyOutputView(
                    sourcePixels,
                    source->getRowBytes(),
                    destinationPixels,
                    destination->getRowBytes(),
                    width,
                    height,
                    arguments.time);
                if (runtimeErrorVisible_) {
                    clearPersistentMessage();
                    runtimeErrorVisible_ = false;
                }
                return;
            }
            if (!runtimeErrorVisible_) {
                resolve_dlss5::writeDiagnosticLog(
                    "OFX render returned source fallback: " +
                    runtime_->lastError());
                setPersistentMessage(
                    OFX::Message::eMessageError,
                    "ResolveDlss5Runtime",
                    std::string("DLSS Feature 18 is inactive: ") +
                        runtime_->lastError() +
                        ". The source frame is being returned unchanged.");
                runtimeErrorVisible_ = true;
            }
        } else if (settings.enabled) {
            resolve_dlss5::writeDiagnosticLog(
                "OFX runtime skipped because image bounds/pointers were not "
                "compatible with the full-frame CPU bridge");
        }

        PassthroughProcessor processor(*this);
        processor.setDstImg(destination.get());
        processor.setSource(source.get());
        processor.setGPURenderArgs(arguments);
        processor.setRenderWindow(arguments.renderWindow);
        processor.process();
        resetRequested_ = false;
        lastRenderedTime_ = arguments.time;
        previousSettings_ = settings;
    }

    bool isIdentity(
        const OFX::IsIdentityArguments& arguments,
        OFX::Clip*& identityClip,
        double& identityTime) override {
        if (!enabled_->getValueAtTime(arguments.time)) {
            identityClip = sourceClip_;
            identityTime = arguments.time;
            return true;
        }
        return false;
    }

    void getFramesNeeded(
        const OFX::FramesNeededArguments& arguments,
        OFX::FramesNeededSetter& setter) override {
        int mode = 0;
        guidanceMode_->getValueAtTime(arguments.time, mode);
        OfxRangeD range{arguments.time, arguments.time};
        if (mode != static_cast<int>(resolve_dlss5::GuidanceMode::ForceZero)) {
            range.min = arguments.time - 1.0;
        }
        setter.setFramesNeeded(*sourceClip_, range);
    }

    void changedParam(
        const OFX::InstanceChangedArgs&,
        const std::string& parameterName) override {
        if (parameterName == kParamReset) {
            resetRequested_ = true;
        }
    }

private:
    void applyOutputView(
        const float* source,
        int sourceRowBytes,
        float* destination,
        int destinationRowBytes,
        int width,
        int height,
        double time) const {
        double mixValue = 1.0;
        outputMix_->getValueAtTime(time, mixValue);
        const float mix = static_cast<float>(
            std::clamp(mixValue, 0.0, 1.0));
        int viewValue = 0;
        outputView_->getValueAtTime(time, viewValue);
        const auto view = static_cast<OutputView>(viewValue);
        if (view == OutputView::Processed && mix >= 1.0F) {
            return;
        }

        for (int y = 0; y < height; ++y) {
            const auto* sourceRow = reinterpret_cast<const float*>(
                reinterpret_cast<const std::byte*>(source) +
                static_cast<std::ptrdiff_t>(y) * sourceRowBytes);
            auto* destinationRow = reinterpret_cast<float*>(
                reinterpret_cast<std::byte*>(destination) +
                static_cast<std::ptrdiff_t>(y) * destinationRowBytes);
            for (int x = 0; x < width; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    const int offset = x * 4 + channel;
                    const float original = sourceRow[offset];
                    const float processed = destinationRow[offset];
                    if (view == OutputView::DifferenceX10) {
                        destinationRow[offset] = std::clamp(
                            0.5F + (processed - original) * 10.0F,
                            0.0F,
                            1.0F);
                    } else if (view == OutputView::LeftRightCompare &&
                               x < width / 2) {
                        destinationRow[offset] = original;
                    } else {
                        destinationRow[offset] =
                            original + (processed - original) * mix;
                    }
                }
                if (view == OutputView::LeftRightCompare &&
                    std::abs(x - width / 2) <= 1) {
                    destinationRow[x * 4 + 0] = 1.0F;
                    destinationRow[x * 4 + 1] = 1.0F;
                    destinationRow[x * 4 + 2] = 1.0F;
                }
                destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
            }
        }
    }

    resolve_dlss5::Feature18Settings settingsAt(double time) const {
        resolve_dlss5::Feature18Settings settings;
        settings.enabled = enabled_->getValueAtTime(time);
        settings.uiCorrection = uiCorrection_->getValueAtTime(time);
        settings.intensity = static_cast<float>(intensity_->getValueAtTime(time));
        settings.localToneStrength = static_cast<float>(localTone_->getValueAtTime(time));
        settings.localStructureStrength = static_cast<float>(localStructure_->getValueAtTime(time));
        settings.skinStructureStrength = static_cast<float>(skinStructure_->getValueAtTime(time));
        settings.useAutoMask = autoMask_->getValueAtTime(time);
        settings.paperWhiteScale = static_cast<float>(paperWhite_->getValueAtTime(time));
        settings.hdrTransferStrength = static_cast<float>(hdrTransfer_->getValueAtTime(time));
        settings.motionScaleX = static_cast<float>(motionScaleX_->getValueAtTime(time));
        settings.motionScaleY = static_cast<float>(motionScaleY_->getValueAtTime(time));

        int value = 0;
        preset_->getValueAtTime(time, value);
        settings.preset = static_cast<resolve_dlss5::NrPreset>(value + 1);
        style_->getValueAtTime(time, value);
        settings.style = value;
        inputEncoding_->getValueAtTime(time, value);
        settings.inputEncoding = static_cast<resolve_dlss5::InputEncoding>(value);
        guidanceMode_->getValueAtTime(time, value);
        settings.guidanceMode = static_cast<resolve_dlss5::GuidanceMode>(value);
        depthConvention_->getValueAtTime(time, value);
        settings.depthConvention = static_cast<resolve_dlss5::DepthConvention>(value);
        return settings;
    }

    OFX::Clip* destinationClip_ = nullptr;
    OFX::Clip* sourceClip_ = nullptr;
    OFX::BooleanParam* enabled_ = nullptr;
    OFX::ChoiceParam* preset_ = nullptr;
    OFX::BooleanParam* uiCorrection_ = nullptr;
    OFX::ChoiceParam* style_ = nullptr;
    OFX::DoubleParam* intensity_ = nullptr;
    OFX::DoubleParam* localTone_ = nullptr;
    OFX::DoubleParam* localStructure_ = nullptr;
    OFX::DoubleParam* skinStructure_ = nullptr;
    OFX::BooleanParam* autoMask_ = nullptr;
    OFX::ChoiceParam* inputEncoding_ = nullptr;
    OFX::DoubleParam* paperWhite_ = nullptr;
    OFX::DoubleParam* hdrTransfer_ = nullptr;
    OFX::ChoiceParam* guidanceMode_ = nullptr;
    OFX::ChoiceParam* depthConvention_ = nullptr;
    OFX::DoubleParam* motionScaleX_ = nullptr;
    OFX::DoubleParam* motionScaleY_ = nullptr;
    OFX::DoubleParam* outputMix_ = nullptr;
    OFX::ChoiceParam* outputView_ = nullptr;
    OFX::PushButtonParam* reset_ = nullptr;
    std::unique_ptr<resolve_dlss5::Feature18Runtime> runtime_;
    std::optional<resolve_dlss5::Feature18Settings> previousSettings_;
    double lastRenderedTime_ = std::numeric_limits<double>::quiet_NaN();
    bool runtimeErrorVisible_ = false;
    bool resetRequested_ = true;
    std::uint64_t renderCount_ = 0;
};

OFX::BooleanParamDescriptor* defineBoolean(
    OFX::ImageEffectDescriptor& descriptor,
    const char* name,
    const char* label,
    const char* hint,
    bool defaultValue,
    OFX::GroupParamDescriptor* parent = nullptr) {
    auto* parameter = descriptor.defineBooleanParam(name);
    parameter->setLabels(label, label, label);
    parameter->setScriptName(name);
    parameter->setHint(hint);
    parameter->setDefault(defaultValue);
    if (parent) {
        parameter->setParent(*parent);
    }
    return parameter;
}

OFX::DoubleParamDescriptor* defineDouble(
    OFX::ImageEffectDescriptor& descriptor,
    const char* name,
    const char* label,
    const char* hint,
    double defaultValue,
    double minimum,
    double maximum,
    double increment,
    OFX::GroupParamDescriptor* parent = nullptr) {
    auto* parameter = descriptor.defineDoubleParam(name);
    parameter->setLabels(label, label, label);
    parameter->setScriptName(name);
    parameter->setHint(hint);
    parameter->setDefault(defaultValue);
    parameter->setRange(minimum, maximum);
    parameter->setDisplayRange(minimum, maximum);
    parameter->setIncrement(increment);
    parameter->setDoubleType(OFX::eDoubleTypeScale);
    if (parent) {
        parameter->setParent(*parent);
    }
    return parameter;
}

OFX::ChoiceParamDescriptor* defineChoice(
    OFX::ImageEffectDescriptor& descriptor,
    const char* name,
    const char* label,
    const char* hint,
    int defaultValue,
    std::initializer_list<const char*> options,
    OFX::GroupParamDescriptor* parent = nullptr) {
    auto* parameter = descriptor.defineChoiceParam(name);
    parameter->setLabels(label, label, label);
    parameter->setScriptName(name);
    parameter->setHint(hint);
    parameter->setDefault(defaultValue);
    for (const char* option : options) {
        parameter->appendOption(option);
    }
    if (parent) {
        parameter->setParent(*parent);
    }
    return parameter;
}

}  // namespace

ResolveDlss5PluginFactory::ResolveDlss5PluginFactory()
    : OFX::PluginFactoryHelper<ResolveDlss5PluginFactory>(
          kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor) {}

void ResolveDlss5PluginFactory::load() {
    resolve_dlss5::writeDiagnosticLog("OFX plugin load action received");
}

void ResolveDlss5PluginFactory::unload() {
    resolve_dlss5::writeDiagnosticLog("OFX plugin unload action received");
}

void ResolveDlss5PluginFactory::describe(
    OFX::ImageEffectDescriptor& descriptor) {
    descriptor.setLabels(kPluginName, kPluginName, kPluginName);
    descriptor.setPluginGrouping(kPluginGrouping);
    descriptor.setPluginDescription(kPluginDescription);
    descriptor.addSupportedContext(OFX::eContextFilter);
    descriptor.addSupportedContext(OFX::eContextGeneral);
    descriptor.addSupportedBitDepth(OFX::eBitDepthFloat);
    descriptor.setSingleInstance(false);
    descriptor.setHostFrameThreading(false);
    descriptor.setSupportsMultiResolution(kSupportsMultiResolution);
    descriptor.setSupportsTiles(kSupportsTiles);
    descriptor.setTemporalClipAccess(true);
    descriptor.setRenderTwiceAlways(false);
    descriptor.setSupportsMultipleClipPARs(kSupportsMultipleClipPars);
    descriptor.setSupportsOpenCLRender(false);
    // The first functional backend is a CPU-facing OFX bridge into a private
    // D3D12 session. CUDA is intentionally not advertised until the shared
    // CUDA/D3D12 texture path is implemented.
    descriptor.setSupportsCudaRender(false);
    descriptor.setSupportsCudaStream(false);
    descriptor.setNoSpatialAwareness(false);
}

void ResolveDlss5PluginFactory::describeInContext(
    OFX::ImageEffectDescriptor& descriptor,
    OFX::ContextEnum) {
    auto* source = descriptor.defineClip(kOfxImageEffectSimpleSourceClipName);
    source->addSupportedComponent(OFX::ePixelComponentRGBA);
    source->setTemporalClipAccess(true);
    source->setSupportsTiles(kSupportsTiles);
    source->setIsMask(false);

    auto* destination = descriptor.defineClip(kOfxImageEffectOutputClipName);
    destination->addSupportedComponent(OFX::ePixelComponentRGBA);
    destination->setSupportsTiles(kSupportsTiles);

    auto* page = descriptor.definePageParam("Controls");
    page->addChild(*defineBoolean(
        descriptor, kParamEnabled, "Enable DLSS Neural Rendering",
        "Enables the experimental same-resolution Feature 18 pass.", true));
    page->addChild(*defineChoice(
        descriptor, kParamPreset, "NR Preset",
        "Feature 18 render preset, matching the add-on's integer preset.",
        0, {"Preset #1", "Preset #2", "Preset #3"}));
    page->addChild(*defineBoolean(
        descriptor, kParamUiCorrection, "NR UI Correction",
        "Enables the integer DLSSNR.UICorrection flag.", false));

    auto* strengths = descriptor.defineGroupParam("nrStrengths");
    strengths->setLabels("Neural Controls", "Neural Controls", "Neural Controls");
    strengths->setHint("Feature 18 style and strength controls.");
    strengths->setOpen(false);
    page->addChild(*strengths);
    page->addChild(*defineChoice(
        descriptor, kParamStyle, "Style", "Integer DLSSNR.Style value.",
        0, {"Style 0", "Style 1", "Style 2"}, strengths));
    page->addChild(*defineDouble(
        descriptor, kParamIntensity, "Intensity", "Overall neural strength.",
        1.0, 0.0, 2.0, 0.01, strengths));
    page->addChild(*defineDouble(
        descriptor, kParamLocalTone, "Local Tone Strength",
        "Local tone contribution.", 1.0, 0.0, 2.0, 0.01, strengths));
    page->addChild(*defineDouble(
        descriptor, kParamLocalStructure, "Local Structure Strength",
        "Local detail and structure contribution.",
        1.0, 0.0, 2.0, 0.01, strengths));
    page->addChild(*defineDouble(
        descriptor, kParamSkinStructure, "Skin Structure Strength",
        "Skin structure contribution.", 1.0, 0.0, 2.0, 0.01, strengths));
    page->addChild(*defineBoolean(
        descriptor, kParamAutoMask, "Use Automatic Mask",
        "Enables DLSSNR.UseAutoMask.", false, strengths));

    auto* codec = descriptor.defineGroupParam("nrCodec");
    codec->setLabels("Input Codec", "Input Codec", "Input Codec");
    codec->setHint("Controls the video-to-DLSSNR working-space conversion.");
    codec->setOpen(false);
    page->addChild(*codec);
    page->addChild(*defineChoice(
        descriptor, kParamInputEncoding, "Input Encoding",
        "Selects the Resolve-to-neural proxy transfer function.", 0,
        {"Automatic", "SDR / sRGB", "Linear / scRGB", "HDR / PQ"}, codec));
    page->addChild(*defineDouble(
        descriptor, kParamPaperWhite, "Scene Paper-White Scale",
        "Paper-white normalization used by the HDR proxy codec.",
        1.0, 0.1, 8.0, 0.01, codec));
    page->addChild(*defineDouble(
        descriptor, kParamHdrTransfer, "HDR Transfer Strength",
        "Blends the neural proxy change back into the original HDR signal.",
        1.0, 0.0, 1.0, 0.01, codec));

    auto* guidance = descriptor.defineGroupParam("nrGuidance");
    guidance->setLabels("Guide Overrides", "Guide Overrides", "Guide Overrides");
    guidance->setHint("Leave at defaults unless diagnostics require overrides.");
    guidance->setOpen(false);
    page->addChild(*guidance);
    page->addChild(*defineChoice(
        descriptor, kParamGuidanceMode, "Guidance Mode",
        "Selects real or zero-filled motion and depth inputs.", 0,
        {"Force Zero", "Motion Only", "Depth Only", "Available"}, guidance));
    page->addChild(*defineChoice(
        descriptor, kParamDepthConvention, "Depth Convention",
        "Matches the add-on's use-game/normal/inverted depth override.", 0,
        {"Use Input Flag", "Force Normal Depth", "Force Inverted Depth"},
        guidance));
    page->addChild(*defineDouble(
        descriptor, kParamMotionScaleX, "Motion Scale X Multiplier",
        "Multiplier applied to horizontal motion vectors.",
        1.0, -4.0, 4.0, 0.01, guidance));
    page->addChild(*defineDouble(
        descriptor, kParamMotionScaleY, "Motion Scale Y Multiplier",
        "Multiplier applied to vertical motion vectors.",
        1.0, -4.0, 4.0, 0.01, guidance));

    auto* diagnostics = descriptor.defineGroupParam("nrDiagnostics");
    diagnostics->setLabels("Diagnostics", "Diagnostics", "Diagnostics");
    diagnostics->setHint(
        "Views that make a subtle neural change directly visible. Runtime "
        "events are written to %LOCALAPPDATA%\\ResolveDlss5\\ResolveDlss5.log.");
    diagnostics->setOpen(true);
    page->addChild(*diagnostics);
    page->addChild(*defineDouble(
        descriptor, kParamOutputMix, "Output Mix",
        "Blends between the original frame and the Feature 18 result.",
        1.0, 0.0, 1.0, 0.01, diagnostics));
    page->addChild(*defineChoice(
        descriptor, kParamOutputView, "Output View",
        "Processed shows the normal result; Difference x10 magnifies the "
        "signed RGB delta; Left / Right draws an original/processed split.",
        0, {"Processed", "Difference x10", "Left / Right Compare"},
        diagnostics));

    auto* reset = descriptor.definePushButtonParam(kParamReset);
    reset->setLabels(
        "Reset NR Feature and Clear History",
        "Reset NR Feature and Clear History",
        "Reset NR Feature and Clear History");
    reset->setHint("Forces a Feature 18 history reset on the next render.");
    page->addChild(*reset);
}

OFX::ImageEffect* ResolveDlss5PluginFactory::createInstance(
    OfxImageEffectHandle handle,
    OFX::ContextEnum) {
    return new ResolveDlss5Plugin(handle);
}

void OFX::Plugin::getPluginIDs(PluginFactoryArray& factories) {
    static ResolveDlss5PluginFactory factory;
    factories.push_back(&factory);
}
