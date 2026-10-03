// SPDX-License-Identifier: MIT

#include "ResolveDlss5Plugin.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "ColorPipeline.h"
#include "Feature18Parameters.h"
#include "Feature18Runtime.h"
#include "OpticalFlow.h"
#include "TimelineProcessor.h"
#include "ofxGPURender.h"

namespace {

constexpr char kPluginName[] = "DLSS Neural Video Experimental";
constexpr char kPluginGrouping[] = "DLSS Experimental";
constexpr char kPluginDescription[] =
    "Experimental SDR/HDR same-resolution neural video filter with float-base "
    "net correction, Oklab controls, 1-3 independent NR passes and streaming "
    "history. Seeks reset history; partial exports can differ from a full sequential export. "
    "AMDOF/NVOF and external float motion input share one field across all NR passes. "
    "Antiflicker and CUDA bridge are not available yet. Export errors stop "
    "rendering.";
constexpr char kPluginIdentifier[] = "com.saog.resolve.dlss5";
constexpr int kPluginVersionMajor = 0;
constexpr int kPluginVersionMinor = 4;

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
constexpr char kParamAdvanced[] = "nrShowAdvanced";
constexpr char kParamLegacyGuidance[] = "nrShowLegacyGuidance";
constexpr char kMotionClip[] = "MotionVectors";

constexpr bool kSupportsTiles = false;
constexpr bool kSupportsMultiResolution = false;
constexpr bool kSupportsMultipleClipPars = false;

enum class OutputView : int {
    Processed = 0,
    DifferenceX10 = 1,
    LeftRightCompare = 2,
};

class ResolveDlss5Plugin final : public OFX::ImageEffect {
   public:
    explicit ResolveDlss5Plugin(OfxImageEffectHandle handle)
        : OFX::ImageEffect(handle),
          destinationClip_(fetchClip(kOfxImageEffectOutputClipName)),
          sourceClip_(fetchClip(kOfxImageEffectSimpleSourceClipName)),
          motionClip_(fetchClip(kMotionClip)),
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
          outputMix_(fetchDoubleParam(kParamOutputMix)),
          outputView_(fetchChoiceParam(kParamOutputView)),
          reset_(fetchPushButtonParam(kParamReset)),
          runtime_(std::make_unique<resolve_dlss5::Feature18Runtime>()) {
        refreshParameterVisibility();
        resolve_dlss5::writeDiagnosticLog("OFX effect instance created");
    }

    ~ResolveDlss5Plugin() override {
        resolve_dlss5::writeDiagnosticLog("OFX effect instance destroyed");
    }

    void render(const OFX::RenderArguments& arguments) override {
        std::scoped_lock lock(stateMutex_);
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        const auto milliseconds = [](auto begin, auto end) {
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };
        if (arguments.isEnabledCudaRender || arguments.isEnabledOpenCLRender ||
            arguments.isEnabledMetalRender) {
            const std::string error =
                "Host supplied GPU images, but this development build requires CPU float RGBA; "
                "CUDA=" +
                std::to_string(arguments.isEnabledCudaRender) +
                ", OpenCL=" + std::to_string(arguments.isEnabledOpenCLRender) +
                ", Metal=" + std::to_string(arguments.isEnabledMetalRender);
            setPersistentMessage(OFX::Message::eMessageError, "ResolveDlss5Runtime", error);
            runtimeErrorVisible_ = true;
            resolve_dlss5::writeDiagnosticLog(error);
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
        std::unique_ptr<OFX::Image> destination(destinationClip_->fetchImage(arguments.time));
        std::unique_ptr<OFX::Image> source(sourceClip_->fetchImage(arguments.time));
        validateImage(destination.get());
        validateImage(source.get());
        const auto bounds = source->getBounds();
        const auto out = destination->getBounds();
        const auto window = arguments.renderWindow;
        if (window.x2 < window.x1 || window.y2 < window.y1 || window.x1 < out.x1 ||
            window.x2 > out.x2 || window.y1 < out.y1 || window.y2 > out.y2 ||
            window.x1 < bounds.x1 || window.x2 > bounds.x2 || window.y1 < bounds.y1 ||
            window.y2 > bounds.y2)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        if (window.x1 == window.x2 || window.y1 == window.y2) return;
        const auto fetched = Clock::now();
        const int width = bounds.x2 - bounds.x1, height = bounds.y2 - bounds.y1;
        auto settings = settingsAt(arguments.time);
        settings.premultiplied = source->getPreMultiplication() == OFX::eImagePreMultiplied;
        double mix = outputMix_->getValueAtTime(arguments.time);
        int view = 0;
        outputView_->getValueAtTime(arguments.time, view);
        std::vector<float> pixels;
        bool processed = false;
        resolve_dlss5::RuntimeTimings nrTimings;
        double neuralMs = 0, compositeMs = 0;
        bool cacheHit = false, modelReset = false;
        try {
            if (settings.enabled && !(view == 0 && (mix == 0 || settings.detail.strength == 0))) {
                if (resetRequested_) {
                    timeline_.invalidate();
                    runtime_->reset();
                    flow_.reset();
                    resetRequested_ = false;
                }
                auto current = packedFrame(*source, arguments.time, bounds);
                current.flow = flowAt(arguments.time);
                if (current.flow.external) {
                    if (!motionClip_->isConnected())
                        throw std::runtime_error(
                            "External motion input is enabled but MotionVectors is not connected");
                    std::unique_ptr<OFX::Image> external(motionClip_->fetchImage(arguments.time));
                    validateImage(external.get());
                    if (std::abs(motionClip_->getPixelAspectRatio() -
                                 sourceClip_->getPixelAspectRatio()) > 1e-6)
                        throw std::runtime_error(
                            "External motion and source pixel aspect ratios differ");
                    const auto packed = packedFrame(*external, arguments.time, bounds);
                    current.motion = resolve_dlss5::adaptExternalMotion(packed.pixels, width,
                                                                        height, current.flow);
                } else if (current.flow.method != resolve_dlss5::FlowMethod::None) {
                    const auto refTime = referenceTime(arguments.time);
                    if (refTime != arguments.time) {
                        std::unique_ptr<OFX::Image> reference(sourceClip_->fetchImage(refTime));
                        validateImage(reference.get());
                        current.referencePixels =
                            packedFrame(*reference, arguments.time, bounds).pixels;
                    }
                }
                const auto cacheHits = timeline_.stats().cacheHits;
                const auto neural = timeline_.render(
                    arguments.time, current,
                    [&](const resolve_dlss5::CpuFrame& frame, bool reset) {
                        modelReset = reset;
                        std::vector<float> result(frame.pixels.size());
                        auto motion = frame.motion;
                        if (!frame.flow.external &&
                            frame.flow.method != resolve_dlss5::FlowMethod::None &&
                            !frame.referencePixels.empty())
                            motion =
                                flow_.estimate(frame.pixels, frame.referencePixels, frame.width,
                                               frame.height, frame.settings, frame.flow);
                        if (!runtime_->process(
                                frame.pixels.data(), frame.width * 16, result.data(),
                                frame.width * 16, frame.width, frame.height, frame.settings, reset,
                                true, motion.empty() ? nullptr : motion.data(), &nrTimings))
                            throw std::runtime_error(runtime_->lastError());
                        return result;
                    },
                    [&] { runtime_->reset(); }, [&] { return abort(); },
                    arguments.interactiveRenderStatus ? resolve_dlss5::TimelineDomain::Interactive
                                                      : resolve_dlss5::TimelineDomain::Export,
                    current.flow.external || current.flow.method != resolve_dlss5::FlowMethod::None
                        ? 1.0
                        : (arguments.interactiveRenderStatus ? 1.0 : sequenceFrameStep_));
                const auto compositeStart = Clock::now();
                cacheHit = timeline_.stats().cacheHits != cacheHits;
                neuralMs = milliseconds(fetched, compositeStart);
                resolve_dlss5::compositeProxyFrame(current.pixels, *neural, width, height, settings,
                                                   pixels);
                applyOutputView(
                    static_cast<const float*>(source->getPixelAddress(bounds.x1, bounds.y1)),
                    source->getRowBytes(), pixels.data(), width * 16, width, height,
                    arguments.time);
                compositeMs = milliseconds(compositeStart, Clock::now());
                processed = true;
                if (runtimeErrorVisible_) {
                    clearPersistentMessage();
                    runtimeErrorVisible_ = false;
                }
            }
        } catch (const std::exception& error) {
            timeline_.invalidate();
            resetRequested_ = true;
            setPersistentMessage(OFX::Message::eMessageError, "ResolveDlss5Runtime", error.what());
            runtimeErrorVisible_ = true;
            resolve_dlss5::writeDiagnosticLog(std::string("Render failed: ") + error.what());
            // A failed export must not silently succeed with an untreated frame.
            if (!arguments.interactiveRenderStatus) OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        for (int y = window.y1; y < window.y2; ++y) {
            if (abort()) OFX::throwSuiteStatusException(kOfxStatFailed);
            const float* row =
                processed
                    ? pixels.data() +
                          (static_cast<size_t>(y - bounds.y1) * width + window.x1 - bounds.x1) * 4
                    : static_cast<const float*>(source->getPixelAddress(window.x1, y));
            auto* dst = static_cast<float*>(destination->getPixelAddress(window.x1, y));
            if (!row || !dst) OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
            std::memcpy(dst, row, static_cast<size_t>(window.x2 - window.x1) * 16);
        }
        ++renderCount_;
        const auto end = Clock::now();
        if (processed && (renderCount_ <= 3 || end - lastTimingLog_ >= std::chrono::seconds(1))) {
            lastTimingLog_ = end;
            resolve_dlss5::writeDiagnosticLog(
                "Render timing ms: time=" + std::to_string(arguments.time) +
                ", size=" + std::to_string(width) + "x" + std::to_string(height) +
                ", passes=" + std::to_string(settings.passCount) +
                ", total=" + std::to_string(milliseconds(start, end)) +
                ", fetch=" + std::to_string(milliseconds(start, fetched)) + ", neural-path=" +
                std::to_string(neuralMs) + ", composite=" + std::to_string(compositeMs) +
                ", cache=" + (cacheHit ? "hit" : "miss") + ", model-reset=" +
                (modelReset ? "1" : "0") + ", nr-lock=" + std::to_string(nrTimings.lockMs) +
                ", nr-init=" + std::to_string(nrTimings.initializeMs) +
                ", nr-prepare=" + std::to_string(nrTimings.prepareMs) +
                ", nr-submit-wait=" + std::to_string(nrTimings.submitWaitMs) +
                ", nr-readback=" + std::to_string(nrTimings.readbackMs));
        }
    }

    bool isIdentity(const OFX::IsIdentityArguments& arguments, OFX::Clip*& identityClip,
                    double& identityTime) override {
        int view = 0;
        outputView_->getValueAtTime(arguments.time, view);
        if (!enabled_->getValueAtTime(arguments.time) ||
            (view == 0 &&
             (outputMix_->getValueAtTime(arguments.time) == 0 ||
              fetchDoubleParam("nrResidualStrength")->getValueAtTime(arguments.time) == 0))) {
            identityClip = sourceClip_;
            identityTime = arguments.time;
            return true;
        }
        return false;
    }

    void getFramesNeeded(const OFX::FramesNeededArguments& arguments,
                         OFX::FramesNeededSetter& setter) override {
        const auto flow = flowAt(arguments.time);
        const auto ref = flow.external || flow.method == resolve_dlss5::FlowMethod::None
                             ? arguments.time
                             : referenceTime(arguments.time);
        setter.setFramesNeeded(*sourceClip_, {ref, arguments.time});
        if (flow.external && motionClip_->isConnected())
            setter.setFramesNeeded(*motionClip_, {arguments.time, arguments.time});
    }
    void changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) override {
        std::scoped_lock lock(stateMutex_);
        if (name == "nrPassCount" || name == kParamInputEncoding || name == kParamAdvanced ||
            name == "nrExternalMotion" || name == "nrOpticalFlowMethod")
            refreshParameterVisibility(true, args.time);
        // Display switches never change the image or advance/reset NR history.
        if (name == kParamAdvanced || name == kParamLegacyGuidance ||
            name == "nrHistoryStartMode" || name == "nrHistoryStartFrame" ||
            name == kParamGuidanceMode || name == kParamDepthConvention ||
            name == kParamMotionScaleX || name == kParamMotionScaleY)
            return;
        const auto flow = flowAt(args.time);
        if ((flow.external && (name == "nrOpticalFlowMethod" || name == "nrAmdFlowQuality" ||
                               name == "nrNvidiaFlowQuality")) ||
            (!flow.external && name.starts_with("nrExternal")) ||
            (!flow.external && name == "nrAmdFlowQuality" &&
             flow.method != resolve_dlss5::FlowMethod::Amd) ||
            (!flow.external && name == "nrNvidiaFlowQuality" &&
             flow.method != resolve_dlss5::FlowMethod::Nvidia))
            return;
        const bool outputOnly = name == kParamOutputMix || name == kParamOutputView ||
                                name == kParamHdrTransfer || name == "nrResidualStrength" ||
                                name == "nrResidualLightness" || name == "nrResidualChroma" ||
                                name == "nrDarkening" || name == "nrBrightening" ||
                                name == "nrHueProtection" || name == "nrDarkProtection" ||
                                name == "nrHighlightProtection" || name == "nrCompression" ||
                                name == "nrLowFrequency" || name == "nrHighFrequency";
        if (!outputOnly) {
            int count = 0;
            fetchChoiceParam("nrPassCount")->getValue(count);
            if (!(count < 1 && name.starts_with("nrPass2")) &&
                !(count < 2 && name.starts_with("nrPass3")))
                resetRequested_ = true;
        }
    }
    void changedClip(const OFX::InstanceChangedArgs&, const std::string&) override {
        std::scoped_lock lock(stateMutex_);
        resetRequested_ = true;
    }
    void beginSequenceRender(const OFX::BeginSequenceRenderArguments& args) override {
        std::scoped_lock lock(stateMutex_);
        sequenceFrameStep_ =
            std::isfinite(args.frameStep) && args.frameStep > 0 ? args.frameStep : 1;
        if (!args.isInteractive) {
            timeline_.invalidate();
            flow_.reset();
        }
        resolve_dlss5::writeDiagnosticLog(
            "Sequence begin: frameStep=" + std::to_string(args.frameStep) + ", range=" +
            std::to_string(args.frameRange.min) + ":" + std::to_string(args.frameRange.max) +
            ", interactive=" + std::to_string(args.isInteractive) + ", sequential=" +
            std::to_string(args.sequentialRenderStatus) + ", streaming history; no source replay");
    }
    void endSequenceRender(const OFX::EndSequenceRenderArguments&) override {
        resolve_dlss5::writeDiagnosticLog("Sequence end");
    }

   private:
    resolve_dlss5::FlowSettings flowAt(double time) const {
        resolve_dlss5::FlowSettings s;
        int value = 0;
        fetchChoiceParam("nrOpticalFlowMethod")->getValueAtTime(time, value);
        s.method = static_cast<resolve_dlss5::FlowMethod>(value);
        fetchChoiceParam("nrAmdFlowQuality")->getValueAtTime(time, s.amdQuality);
        fetchChoiceParam("nrNvidiaFlowQuality")->getValueAtTime(time, value);
        s.nvidiaQuality = value + 1;
        s.external = fetchBooleanParam("nrExternalMotion")->getValueAtTime(time);
        fetchChoiceParam("nrExternalXChannel")->getValueAtTime(time, s.xChannel);
        fetchChoiceParam("nrExternalYChannel")->getValueAtTime(time, s.yChannel);
        fetchChoiceParam("nrExternalUnits")->getValueAtTime(time, value);
        s.units = static_cast<resolve_dlss5::FlowUnits>(value);
        s.yUp = fetchBooleanParam("nrExternalYUp")->getValueAtTime(time);
        s.scaleX = static_cast<float>(fetchDoubleParam("nrExternalScaleX")->getValueAtTime(time));
        s.scaleY = static_cast<float>(fetchDoubleParam("nrExternalScaleY")->getValueAtTime(time));
        return s;
    }
    double referenceTime(double time) const {
        const auto range = sourceClip_->getFrameRange();
        if (std::isfinite(range.min) && std::isfinite(range.max) && range.min <= range.max &&
            time - 1 < range.min)
            return time;
        return time - 1;
    }
    void refreshParameterVisibility(bool atTime = false, double time = 0) {
        const auto choice = [&](const char* id) {
            int value = 0;
            auto* param = fetchChoiceParam(id);
            if (atTime)
                param->getValueAtTime(time, value);
            else
                param->getValue(value);
            return value;
        };
        const auto toggle = [&](const char* id) {
            auto* param = fetchBooleanParam(id);
            return atTime ? param->getValueAtTime(time) : param->getValue();
        };
        const auto show = [&](const std::string& id, bool visible) {
            getParam(id)->setIsSecret(!visible);
        };
        const int count = std::clamp(choice("nrPassCount"), 0, 2) + 1;
        for (int pass = 2; pass <= 3; ++pass) {
            const std::string prefix = "nrPass" + std::to_string(pass);
            show(prefix + "Group", pass <= count);
            for (const char* suffix : {"Preset", "Style", "UiCorrection", "Intensity", "Tone",
                                       "Structure", "Skin", "Mask"})
                show(prefix + suffix, pass <= count);
        }
        const bool advanced = toggle(kParamAdvanced);
        show("nrAdvanced", advanced);
        for (const char* id : {"nrHueProtection", "nrDarkProtection", "nrHighlightProtection",
                               "nrCompression", "nrLowFrequency", "nrHighFrequency"})
            show(id, advanced);
        const int encoding = choice(kParamInputEncoding);
        const bool hdr = encoding == 2 || encoding == 3 || encoding == 5 || encoding == 6;
        show("nrHdrControls", hdr);
        for (const char* id :
             {kParamPaperWhite, kParamHdrTransfer, "nrReferenceWhite", "nrPeakNits"})
            show(id, hdr);
        const auto flow = flowAt(time);
        show("nrOpticalFlowMethod", !flow.external);
        show("nrAmdFlowQuality", !flow.external && flow.method == resolve_dlss5::FlowMethod::Amd);
        show("nrNvidiaFlowQuality",
             !flow.external && flow.method == resolve_dlss5::FlowMethod::Nvidia);
        for (const char* id : {"nrExternalXChannel", "nrExternalYChannel", "nrExternalUnits",
                               "nrExternalYUp", "nrExternalScaleX", "nrExternalScaleY"})
            show(id, flow.external);
    }

    static void validateImage(const OFX::Image* image) {
        if (!image || image->getPixelDepth() != OFX::eBitDepthFloat ||
            image->getPixelComponents() != OFX::ePixelComponentRGBA)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    resolve_dlss5::CpuFrame packedFrame(const OFX::Image& image, double time,
                                        OfxRectI expected) const {
        const auto b = image.getBounds();
        if (b.x1 != expected.x1 || b.x2 != expected.x2 || b.y1 != expected.y1 ||
            b.y2 != expected.y2)
            throw std::runtime_error(
                "History image bounds changed; incompatible source/proxy geometry");
        resolve_dlss5::CpuFrame frame;
        frame.width = b.x2 - b.x1;
        frame.height = b.y2 - b.y1;
        if (frame.width <= 0 || frame.height <= 0 ||
            std::abs(static_cast<int64_t>(image.getRowBytes())) <
                static_cast<int64_t>(frame.width) * 16)
            throw std::runtime_error("Invalid source image stride");
        frame.pixels.resize(static_cast<size_t>(frame.width) * frame.height * 4);
        for (int y = 0; y < frame.height; ++y) {
            const auto* row = static_cast<const float*>(image.getPixelAddress(b.x1, b.y1 + y));
            if (!row) throw std::runtime_error("Source image row unavailable");
            std::memcpy(frame.pixels.data() + static_cast<size_t>(y) * frame.width * 4, row,
                        static_cast<size_t>(frame.width) * 16);
        }
        frame.settings = settingsAt(time);
        frame.settings.premultiplied = image.getPreMultiplication() == OFX::eImagePreMultiplied;
        // NR and its hidden history are independent of output-only controls.
        frame.settings.detail = {};
        frame.settings.hdrTransferStrength = 1;
        return frame;
    }
    void applyOutputView(const float* source, int sourceRowBytes, float* destination,
                         int destinationRowBytes, int width, int height, double time) const {
        double mixValue = 1.0;
        outputMix_->getValueAtTime(time, mixValue);
        const float mix = static_cast<float>(std::clamp(mixValue, 0.0, 1.0));
        int viewValue = 0;
        outputView_->getValueAtTime(time, viewValue);
        const auto view = static_cast<OutputView>(viewValue);
        if (view == OutputView::Processed && mix >= 1.0F) {
            return;
        }

        for (int y = 0; y < height; ++y) {
            const auto* sourceRow =
                reinterpret_cast<const float*>(reinterpret_cast<const std::byte*>(source) +
                                               static_cast<std::ptrdiff_t>(y) * sourceRowBytes);
            auto* destinationRow =
                reinterpret_cast<float*>(reinterpret_cast<std::byte*>(destination) +
                                         static_cast<std::ptrdiff_t>(y) * destinationRowBytes);
            for (int x = 0; x < width; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    const int offset = x * 4 + channel;
                    const float original = sourceRow[offset];
                    const float processed = destinationRow[offset];
                    if (view == OutputView::DifferenceX10) {
                        destinationRow[offset] =
                            std::clamp(0.5F + (processed - original) * 10.0F, 0.0F, 1.0F);
                    } else if (view == OutputView::LeftRightCompare && x < width / 2) {
                        destinationRow[offset] = original;
                    } else {
                        destinationRow[offset] =
                            mix == 0 ? original : original + (processed - original) * mix;
                    }
                }
                if (view == OutputView::LeftRightCompare && std::abs(x - width / 2) <= 1) {
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

        int value = 0;
        preset_->getValueAtTime(time, value);
        settings.preset = static_cast<resolve_dlss5::NrPreset>(value + 1);
        style_->getValueAtTime(time, value);
        settings.style = value;
        inputEncoding_->getValueAtTime(time, value);
        settings.inputEncoding = static_cast<resolve_dlss5::InputEncoding>(value);
        settings.referenceWhiteNits =
            static_cast<float>(fetchDoubleParam("nrReferenceWhite")->getValueAtTime(time));
        settings.peakNits =
            static_cast<float>(fetchDoubleParam("nrPeakNits")->getValueAtTime(time));
        int count = 0;
        fetchChoiceParam("nrPassCount")->getValueAtTime(time, count);
        settings.passCount = std::clamp(count, 0, 2) + 1;
        for (int i = 0; i < settings.passCount - 1; ++i) {
            const std::string prefix = "nrPass" + std::to_string(i + 2);
            auto& p = settings.additionalPasses[i];
            int choice = 0;
            fetchChoiceParam(prefix + "Preset")->getValueAtTime(time, choice);
            p.preset = static_cast<resolve_dlss5::NrPreset>(choice + 1);
            fetchChoiceParam(prefix + "Style")->getValueAtTime(time, p.style);
            p.uiCorrection = fetchBooleanParam(prefix + "UiCorrection")->getValueAtTime(time);
            p.intensity =
                static_cast<float>(fetchDoubleParam(prefix + "Intensity")->getValueAtTime(time));
            p.localToneStrength =
                static_cast<float>(fetchDoubleParam(prefix + "Tone")->getValueAtTime(time));
            p.localStructureStrength =
                static_cast<float>(fetchDoubleParam(prefix + "Structure")->getValueAtTime(time));
            p.skinStructureStrength =
                static_cast<float>(fetchDoubleParam(prefix + "Skin")->getValueAtTime(time));
            p.useAutoMask = fetchBooleanParam(prefix + "Mask")->getValueAtTime(time);
        }
        auto& d = settings.detail;
        d.strength =
            static_cast<float>(fetchDoubleParam("nrResidualStrength")->getValueAtTime(time));
        d.lightness =
            static_cast<float>(fetchDoubleParam("nrResidualLightness")->getValueAtTime(time));
        d.chroma = static_cast<float>(fetchDoubleParam("nrResidualChroma")->getValueAtTime(time));
        d.darkening = static_cast<float>(fetchDoubleParam("nrDarkening")->getValueAtTime(time));
        d.brightening = static_cast<float>(fetchDoubleParam("nrBrightening")->getValueAtTime(time));
        d.hueProtection =
            static_cast<float>(fetchDoubleParam("nrHueProtection")->getValueAtTime(time));
        d.darkProtection =
            static_cast<float>(fetchDoubleParam("nrDarkProtection")->getValueAtTime(time));
        d.highlightProtection =
            static_cast<float>(fetchDoubleParam("nrHighlightProtection")->getValueAtTime(time));
        d.compression = static_cast<float>(fetchDoubleParam("nrCompression")->getValueAtTime(time));
        d.lowFrequency =
            static_cast<float>(fetchDoubleParam("nrLowFrequency")->getValueAtTime(time));
        d.highFrequency =
            static_cast<float>(fetchDoubleParam("nrHighFrequency")->getValueAtTime(time));
        return settings;
    }

    OFX::Clip* destinationClip_ = nullptr;
    OFX::Clip* sourceClip_ = nullptr;
    OFX::Clip* motionClip_ = nullptr;
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
    OFX::DoubleParam* outputMix_ = nullptr;
    OFX::ChoiceParam* outputView_ = nullptr;
    OFX::PushButtonParam* reset_ = nullptr;
    std::unique_ptr<resolve_dlss5::Feature18Runtime> runtime_;
    std::mutex stateMutex_;
    resolve_dlss5::TimelineProcessor timeline_;
    resolve_dlss5::OpticalFlow flow_;
    double sequenceFrameStep_ = 1;
    bool runtimeErrorVisible_ = false;
    bool resetRequested_ = true;
    std::uint64_t renderCount_ = 0;
    std::chrono::steady_clock::time_point lastTimingLog_{};
};

OFX::BooleanParamDescriptor* defineBoolean(OFX::ImageEffectDescriptor& descriptor, const char* name,
                                           const char* label, const char* hint, bool defaultValue,
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

OFX::DoubleParamDescriptor* defineDouble(OFX::ImageEffectDescriptor& descriptor, const char* name,
                                         const char* label, const char* hint, double defaultValue,
                                         double minimum, double maximum, double increment,
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

OFX::ChoiceParamDescriptor* defineChoice(OFX::ImageEffectDescriptor& descriptor, const char* name,
                                         const char* label, const char* hint, int defaultValue,
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

void describeDetailControls(OFX::ImageEffectDescriptor& descriptor,
                            OFX::PageParamDescriptor& page) {
    auto* detail = descriptor.defineGroupParam("nrDetail");
    detail->setLabels("Detail Control", "Detail Control", "Detail Control");
    detail->setHint("Adjust the final total NR correction once, after all active passes.");
    detail->setOpen(true);
    page.addChild(*detail);
    struct Control {
        const char* id;
        const char* label;
        double value;
        double maximum;
    };
    const auto add = [&](const Control& c, OFX::GroupParamDescriptor* parent) {
        auto* param =
            defineDouble(descriptor, c.id, c.label,
                         "Adjusts the final total correction. Hidden controls retain their values.",
                         c.value, 0, c.maximum, .01, parent);
        page.addChild(*param);
        return param;
    };
    for (const auto& c : {Control{"nrResidualStrength", "Overall Strength", 1, 2},
                          {"nrResidualChroma", "Chroma Strength", 1, 2},
                          {"nrResidualLightness", "Overall Lightness Strength", 1, 2},
                          {"nrDarkening", "Shadow / Structure Strength", 1, 2},
                          {"nrBrightening", "Highlight / Glow Strength", 1, 2}})
        add(c, detail);
    auto* advancedToggle =
        defineBoolean(descriptor, kParamAdvanced, "Advanced Adjustments",
                      "Shows protection, compression and frequency controls; changes display only.",
                      false, detail);
    advancedToggle->setAnimates(false);
    page.addChild(*advancedToggle);
    auto* advanced = descriptor.defineGroupParam("nrAdvanced");
    advanced->setLabels("Advanced Adjustments", "Advanced Adjustments", "Advanced Adjustments");
    advanced->setOpen(true);
    advanced->setIsSecret(true);
    page.addChild(*advanced);
    for (const auto& c : {Control{"nrHueProtection", "Hue Protection", 0, 1},
                          {"nrDarkProtection", "Shadow Protection", 0, 1},
                          {"nrHighlightProtection", "Highlight Protection", 0, 1},
                          {"nrCompression", "Overcorrection Suppression", 0, 1},
                          {"nrLowFrequency", "Low-frequency Range Strength", 1, 2},
                          {"nrHighFrequency", "High-frequency Range Strength", 1, 2}})
        add(c, advanced)->setIsSecret(true);
}

}  // namespace

ResolveDlss5PluginFactory::ResolveDlss5PluginFactory()
    : OFX::PluginFactoryHelper<ResolveDlss5PluginFactory>(kPluginIdentifier, kPluginVersionMajor,
                                                          kPluginVersionMinor) {}

OfxPluginEntryPoint* ResolveDlss5PluginFactory::getMainEntry() { return diagnosticMainEntry; }

OfxStatus ResolveDlss5PluginFactory::diagnosticMainEntry(const char* action, const void* handle,
                                                         OfxPropertySetHandle in,
                                                         OfxPropertySetHandle out) {
    if (action && in && std::strcmp(action, kOfxImageEffectActionRender) == 0) {
        static std::atomic<std::uint64_t> calls = 0;
        const auto call = ++calls;
        if (call <= 3 || call % 120 == 0) {
            const OFX::PropertySet args(in);
            resolve_dlss5::writeDiagnosticLog(
                "Raw render request: time=" +
                std::to_string(args.propGetDouble(kOfxPropTime, false)) +
                ", CUDA=" + std::to_string(args.propGetInt(kOfxImageEffectPropCudaEnabled, false)) +
                ", OpenCL=" +
                std::to_string(args.propGetInt(kOfxImageEffectPropOpenCLEnabled, false)) +
                ", Metal=" +
                std::to_string(args.propGetInt(kOfxImageEffectPropMetalEnabled, false)) +
                ", interactive=" +
                std::to_string(args.propGetInt(kOfxImageEffectPropInteractiveRenderStatus, false)) +
                ", sequential=" +
                std::to_string(args.propGetInt(kOfxImageEffectPropSequentialRenderStatus, false)));
        }
    }
    const auto status =
        OFX::FactoryMainEntryHelper<ResolveDlss5PluginFactory>::mainEntry(action, handle, in, out);
    if (status > kOfxStatOK && status < kOfxStatReplyYes)
        resolve_dlss5::writeDiagnosticLog(std::string("OFX action failed: ") +
                                          (action ? action : "<null>") +
                                          " status=" + std::to_string(status));
    return status;
}

void ResolveDlss5PluginFactory::load() {
    resolve_dlss5::writeDiagnosticLog("OFX plugin load action received");
}

void ResolveDlss5PluginFactory::unload() {
    resolve_dlss5::writeDiagnosticLog("OFX plugin unload action received");
}

void ResolveDlss5PluginFactory::describe(OFX::ImageEffectDescriptor& descriptor) try {
    resolve_dlss5::writeDiagnosticLog("OFX describe started");
    descriptor.setLabels(kPluginName, kPluginName, kPluginName);
    descriptor.setPluginGrouping(kPluginGrouping);
    descriptor.setPluginDescription(kPluginDescription);
    descriptor.addSupportedContext(OFX::eContextFilter);
    descriptor.addSupportedContext(OFX::eContextGeneral);
    descriptor.addSupportedBitDepth(OFX::eBitDepthFloat);
    descriptor.setSingleInstance(false);
    descriptor.setHostFrameThreading(false);
    descriptor.setRenderThreadSafety(OFX::eRenderInstanceSafe);
    // Prefer sequential submission for streaming history. Random requests use
    // an explicit history reset, with no full-export equivalence claim.
    // Resolve 20.0.1 omits this optional descriptor property;
    // treating that omission as fatal prevents every parameter from registering.
    try {
        descriptor.getPropertySet().propSetInt(kOfxImageEffectInstancePropSequentialRender, 2);
    } catch (const OFX::Exception::PropertyUnknownToHost&) {
        resolve_dlss5::writeDiagnosticLog(
            "Host omits sequential preference; streaming history resets on discontinuity");
    } catch (const OFX::Exception::PropertyValueIllegalToHost&) {
        resolve_dlss5::writeDiagnosticLog(
            "Host rejects sequential preference; streaming history resets on discontinuity");
    }
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
    resolve_dlss5::writeDiagnosticLog("OFX describe completed");
} catch (const std::exception& error) {
    resolve_dlss5::writeDiagnosticLog(std::string("OFX describe exception: ") + error.what());
    throw;
}

void ResolveDlss5PluginFactory::describeInContext(OFX::ImageEffectDescriptor& descriptor,
                                                  OFX::ContextEnum) try {
    resolve_dlss5::writeDiagnosticLog("OFX parameter context description started");
    auto* source = descriptor.defineClip(kOfxImageEffectSimpleSourceClipName);
    source->addSupportedComponent(OFX::ePixelComponentRGBA);
    source->setTemporalClipAccess(true);
    source->setSupportsTiles(kSupportsTiles);
    source->setIsMask(false);

    auto* destination = descriptor.defineClip(kOfxImageEffectOutputClipName);
    destination->addSupportedComponent(OFX::ePixelComponentRGBA);
    destination->setSupportsTiles(kSupportsTiles);

    auto* motion = descriptor.defineClip(kMotionClip);
    motion->addSupportedComponent(OFX::ePixelComponentRGBA);
    motion->setOptional(true);
    motion->setIsMask(false);
    motion->setSupportsTiles(kSupportsTiles);
    motion->setTemporalClipAccess(false);

    auto* page = descriptor.definePageParam("Controls");
    page->addChild(*defineBoolean(descriptor, kParamEnabled, "Enable DLSS Neural Rendering",
                                  "Enables the experimental same-resolution Feature 18 pass.",
                                  true));
    describeDetailControls(descriptor, *page);
    auto* strengths = descriptor.defineGroupParam("nrStrengths");
    strengths->setLabels("DLSSNR - Pass 1", "DLSSNR - Pass 1", "DLSSNR - Pass 1");
    strengths->setHint("Original neural pass controls; existing IDs and defaults are preserved.");
    strengths->setOpen(true);
    page->addChild(*strengths);
    page->addChild(*defineChoice(descriptor, kParamPreset, "NR Preset",
                                 "Feature 18 render preset, matching the add-on's integer preset.",
                                 0, {"Preset #1", "Preset #2", "Preset #3"}, strengths));
    page->addChild(*defineBoolean(descriptor, kParamUiCorrection, "NR UI Correction",
                                  "Enables the integer DLSSNR.UICorrection flag.", false,
                                  strengths));

    page->addChild(*defineChoice(
        descriptor, "nrPassCount", "NR Pass Count",
        "Independent neural features; controls apply once to the final total correction.", 0,
        {"1 Pass", "2 Passes", "3 Passes"}, strengths));
    page->addChild(*defineChoice(descriptor, kParamStyle, "Style", "Integer DLSSNR.Style value.", 0,
                                 {"Style 0", "Style 1", "Style 2"}, strengths));
    page->addChild(*defineDouble(descriptor, kParamIntensity, "Intensity",
                                 "Overall neural strength.", 1.0, 0.0, 2.0, 0.01, strengths));
    page->addChild(*defineDouble(descriptor, kParamLocalTone, "Local Tone Strength",
                                 "Local tone contribution.", 1.0, 0.0, 2.0, 0.01, strengths));
    page->addChild(*defineDouble(descriptor, kParamLocalStructure, "Local Structure Strength",
                                 "Local detail and structure contribution.", 1.0, 0.0, 2.0, 0.01,
                                 strengths));
    page->addChild(*defineDouble(descriptor, kParamSkinStructure, "Skin Structure Strength",
                                 "Skin structure contribution.", 1.0, 0.0, 2.0, 0.01, strengths));
    page->addChild(*defineBoolean(descriptor, kParamAutoMask, "Use Automatic Mask",
                                  "Enables DLSSNR.UseAutoMask.", false, strengths));

    for (int index = 2; index <= 3; ++index) {
        const std::string prefix = "nrPass" + std::to_string(index);
        auto* group = descriptor.defineGroupParam(prefix + "Group");
        const std::string label = "NR Pass " + std::to_string(index);
        group->setLabels(label, label, label);
        group->setOpen(false);
        group->setIsSecret(true);
        page->addChild(*group);
        page->addChild(*defineChoice(descriptor, (prefix + "Preset").c_str(), "NR Preset",
                                     "Independent pass preset", 0,
                                     {"Preset #1", "Preset #2", "Preset #3"}, group));
        page->addChild(*defineChoice(descriptor, (prefix + "Style").c_str(), "Style",
                                     "Independent pass style", 0, {"Style 0", "Style 1", "Style 2"},
                                     group));
        page->addChild(*defineBoolean(descriptor, (prefix + "UiCorrection").c_str(),
                                      "NR UI Correction", "Independent pass flag", false, group));
        page->addChild(*defineDouble(descriptor, (prefix + "Intensity").c_str(), "Intensity",
                                     "Independent pass intensity", 1, 0, 2, .01, group));
        page->addChild(*defineDouble(descriptor, (prefix + "Tone").c_str(), "Local Tone",
                                     "Independent pass tone", 1, 0, 2, .01, group));
        page->addChild(*defineDouble(descriptor, (prefix + "Structure").c_str(), "Local Structure",
                                     "Independent pass structure", 1, 0, 2, .01, group));
        page->addChild(*defineDouble(
            descriptor, (prefix + "Skin").c_str(), "Skin Structure",
            "New passes default to zero; original Pass 1 retains its original default", 0, 0, 2,
            .01, group));
        page->addChild(*defineBoolean(descriptor, (prefix + "Mask").c_str(), "Automatic Mask",
                                      "Independent pass mask", false, group));
    }
    auto* codec = descriptor.defineGroupParam("nrCodec");
    codec->setLabels("Input Codec", "Input Codec", "Input Codec");
    codec->setHint("Controls the video-to-DLSSNR working-space conversion.");
    codec->setOpen(false);
    page->addChild(*codec);
    page->addChild(*defineChoice(
        descriptor, kParamInputEncoding, "Input Encoding",
        "Select the actual node input. Automatic preserves the legacy sRGB interpretation; it does "
        "not detect project color management.",
        0,
        {"sRGB (compatible default)", "SDR / sRGB", "Linear / Rec.709 HDR", "HDR / Rec.2020 PQ",
         "SDR / Rec.709 gamma 2.4", "HDR / Rec.2020 HLG", "Linear / Rec.2020 HDR"},
        codec));
    auto* hdr = descriptor.defineGroupParam("nrHdrControls");
    hdr->setLabels("HDR Reference", "HDR Reference", "HDR Reference");
    hdr->setOpen(true);
    hdr->setIsSecret(true);
    hdr->setParent(*codec);
    page->addChild(*hdr);
    page->addChild(*defineDouble(descriptor, kParamPaperWhite, "Scene Paper-White Scale",
                                 "Paper-white normalization used by the HDR proxy codec.", 1.0, 0.1,
                                 8.0, 0.01, hdr));
    page->addChild(
        *defineDouble(descriptor, kParamHdrTransfer, "HDR Transfer Strength",
                      "Blends the neural proxy change back into the original HDR signal.", 1.0, 0.0,
                      1.0, 0.01, hdr));

    page->addChild(*defineDouble(descriptor, "nrReferenceWhite", "Reference White (nits)",
                                 "HDR reference white; linear HDR 1.0 represents this luminance.",
                                 203, 1, 1000, 1, hdr));
    page->addChild(*defineDouble(
        descriptor, "nrPeakNits", "Reference Peak (nits)",
        "Fixed HDR proxy normalization and HLG reference display peak. Must be >= reference white.",
        1000, 100, 10000, 1, hdr));
    for (const char* id : {kParamPaperWhite, kParamHdrTransfer, "nrReferenceWhite", "nrPeakNits"})
        descriptor.getParamDescriptor(id)->setIsSecret(true);
    // Saved IDs remain permanently secret for migration; no Legacy UI or computation.
    auto retain = [](OFX::ParamDescriptor* p) {
        p->setIsSecret(true);
        p->setEnabled(false);
    };
    retain(defineChoice(descriptor, "nrHistoryStartMode", "Saved History Start",
                        "Unused saved value", 0, {"Source Start", "Manual Start"}));
    retain(defineDouble(descriptor, "nrHistoryStartFrame", "Saved Start Frame",
                        "Unused saved value", 0, -1000000, 1000000, 1));
    retain(defineChoice(descriptor, kParamGuidanceMode, "Saved Guidance", "Unused saved value", 0,
                        {"Zero", "Motion", "Depth", "Available"}));
    retain(defineChoice(descriptor, kParamDepthConvention, "Saved Depth", "Unused saved value", 0,
                        {"Input", "Normal", "Inverted"}));
    retain(defineDouble(descriptor, kParamMotionScaleX, "Saved Motion X", "Unused saved value", 1,
                        -4, 4, .01));
    retain(defineDouble(descriptor, kParamMotionScaleY, "Saved Motion Y", "Unused saved value", 1,
                        -4, 4, .01));
    retain(defineBoolean(descriptor, kParamLegacyGuidance, "Saved Display Flag",
                         "Unused saved value", false));

    auto* flow = descriptor.defineGroupParam("nrOpticalFlow");
    flow->setLabels("Optical Flow", "Optical Flow", "Optical Flow");
    flow->setOpen(true);
    page->addChild(*flow);
    page->addChild(
        *defineBoolean(descriptor, "nrExternalMotion", "Use External Motion (Skip Estimation)",
                       "Read current-to-previous float vectors from MotionVectors input. "
                       "Invalid/missing input reports an error. All NR passes share one field.",
                       false, flow));
    page->addChild(*defineChoice(descriptor, "nrOpticalFlowMethod", "Optical Flow Method",
                                 "Estimate only the current/previous source pair, once for all NR "
                                 "passes. No whole-clip replay.",
                                 2, {"None", "AMDOF", "NVOF"}, flow));
    page->addChild(*defineChoice(
        descriptor, "nrAmdFlowQuality", "AMDOF Quality",
        "Performance uses half-size input; Quality uses full-size FidelityFX optical flow.", 1,
        {"Performance", "Quality"}, flow));
    page->addChild(*defineChoice(descriptor, "nrNvidiaFlowQuality", "NVOF Quality",
                                 "Magpie profiles: 4/FAST, 4/MEDIUM, 4/SLOW, 2/MEDIUM, 2/SLOW. "
                                 "Unsupported profiles report an error.",
                                 2,
                                 {"Performance", "Balanced", "Quality", "High Quality (High Cost)",
                                  "Highest Quality (Very High Cost)"},
                                 flow));
    page->addChild(
        *defineChoice(descriptor, "nrExternalXChannel", "External X Channel",
                      "Horizontal vector channel. Float data bypasses neural color encoding.", 0,
                      {"R", "G", "B", "A"}, flow));
    page->addChild(*defineChoice(descriptor, "nrExternalYChannel", "External Y Channel",
                                 "Vertical vector channel. Must differ from X.", 1,
                                 {"R", "G", "B", "A"}, flow));
    page->addChild(
        *defineChoice(descriptor, "nrExternalUnits", "External Vector Units",
                      "Normalized units multiply X by width and Y by height. Current-to-previous "
                      "direction is required; invert forward fields upstream, not by negation.",
                      0, {"Pixels", "Normalized UV"}, flow));
    page->addChild(*defineBoolean(
        descriptor, "nrExternalYUp", "External Positive Y Is Up",
        "Convert upward-positive vectors to the internal downward-positive convention.", false,
        flow));
    page->addChild(*defineDouble(descriptor, "nrExternalScaleX", "External X Scale",
                                 "Additional horizontal conversion scale.", 1, -100, 100, .01,
                                 flow));
    page->addChild(*defineDouble(descriptor, "nrExternalScaleY", "External Y Scale",
                                 "Additional vertical conversion scale.", 1, -100, 100, .01, flow));

    auto* diagnostics = descriptor.defineGroupParam("nrDiagnostics");
    diagnostics->setLabels("Diagnostics", "Diagnostics", "Diagnostics");
    diagnostics->setHint(
        "Views that make a subtle neural change directly visible. Runtime "
        "events are written to %LOCALAPPDATA%\\ResolveDlss5\\ResolveDlss5.log.");
    diagnostics->setOpen(true);
    page->addChild(*diagnostics);
    page->addChild(*defineDouble(descriptor, kParamOutputMix, "Output Mix",
                                 "Blends between the original frame and the Feature 18 result.",
                                 1.0, 0.0, 1.0, 0.01, diagnostics));
    page->addChild(
        *defineChoice(descriptor, kParamOutputView, "Output View",
                      "Processed shows the normal result; Difference x10 magnifies the "
                      "signed RGB delta; Left / Right draws an original/processed split.",
                      0, {"Processed", "Difference x10", "Left / Right Compare"}, diagnostics));

    auto* reset = descriptor.definePushButtonParam(kParamReset);
    reset->setLabels("Reset NR Feature and Clear History", "Reset NR Feature and Clear History",
                     "Reset NR Feature and Clear History");
    reset->setHint("Forces a Feature 18 history reset on the next render.");
    page->addChild(*reset);
    resolve_dlss5::writeDiagnosticLog("OFX parameter context description completed");
} catch (const std::exception& error) {
    resolve_dlss5::writeDiagnosticLog(std::string("OFX parameter context exception: ") +
                                      error.what());
    throw;
}

OFX::ImageEffect* ResolveDlss5PluginFactory::createInstance(OfxImageEffectHandle handle,
                                                            OFX::ContextEnum) {
    return new ResolveDlss5Plugin(handle);
}

void OFX::Plugin::getPluginIDs(PluginFactoryArray& factories) {
    static ResolveDlss5PluginFactory factory;
    factories.push_back(&factory);
}
