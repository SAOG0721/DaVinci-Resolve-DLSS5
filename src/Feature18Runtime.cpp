// SPDX-License-Identifier: MIT

#include "Feature18Runtime.h"

#include <DirectXPackedVector.h>
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <nvsdk_ngx.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "ColorPipeline.h"
#include "GpuResourcePool.h"
#include "ParallelRows.h"

namespace resolve_dlss5 {
namespace {

using Microsoft::WRL::ComPtr;

constexpr NVSDK_NGX_Feature kFeatureDlssNr = static_cast<NVSDK_NGX_Feature>(18);
constexpr unsigned long long kSignedSnippetApplicationId = 0x0876232Cull;
constexpr char kProjectId[] = "7c134ab9-9677-4af5-a2b2-bca943350861";
constexpr char kEngineVersion[] = "Resolve-DLSS5-1";

using SnippetInitExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*,
                                                       ID3D12Device*, NVSDK_NGX_Version,
                                                       const NVSDK_NGX_Parameter*);
using CreateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature,
                                                      NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,
                                                        const NVSDK_NGX_Handle*,
                                                        const NVSDK_NGX_Parameter*,
                                                        PFN_NVSDK_NGX_ProgressCallback);
using ReleaseFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using ShutdownFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
using GetModuleFileNameWFn = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

std::atomic<HMODULE> g_callerModule = nullptr;
std::atomic<GetModuleFileNameWFn> g_originalGetModuleFileNameW = nullptr;
std::mutex g_logMutex;
std::mutex g_hookMutex;
std::mutex g_runtimeExecutionMutex;
HMODULE g_hookedSnippetModule = nullptr;
void** g_hookedIatSlot = nullptr;
std::uint32_t g_hookReferenceCount = 0;

template <typename T>
void* functionAddress(T function) noexcept {
    void* result = nullptr;
    static_assert(sizeof(function) == sizeof(result));
    std::memcpy(&result, &function, sizeof(result));
    return result;
}

template <typename T>
T getExport(HMODULE module, const char* name) noexcept {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

bool ngxSucceeded(NVSDK_NGX_Result result) noexcept { return NVSDK_NGX_SUCCEED(result); }

LONG captureNgxException(DWORD code, DWORD* sehCode) noexcept {
    *sehCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

NVSDK_NGX_Result callCoreInitSafely(const wchar_t* applicationDataPath, ID3D12Device* device,
                                    const NVSDK_NGX_FeatureCommonInfo* featureInfo,
                                    DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return NVSDK_NGX_D3D12_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                                                   kEngineVersion, applicationDataPath, device,
                                                   featureInfo, NVSDK_NGX_Version_API);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result callAllocateParametersSafely(NVSDK_NGX_Parameter** parameters,
                                              DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return NVSDK_NGX_D3D12_AllocateParameters(parameters);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result callCreateFeatureSafely(CreateFeatureFn function,
                                         ID3D12GraphicsCommandList* commandList,
                                         NVSDK_NGX_Parameter* parameters,
                                         NVSDK_NGX_Handle** feature, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return function(commandList, kFeatureDlssNr, parameters, feature);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result callEvaluateFeatureSafely(EvaluateFeatureFn function,
                                           ID3D12GraphicsCommandList* commandList,
                                           const NVSDK_NGX_Handle* feature,
                                           const NVSDK_NGX_Parameter* parameters,
                                           DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return function(commandList, feature, parameters, nullptr);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result callReleaseFeatureSafely(ReleaseFeatureFn function, NVSDK_NGX_Handle* feature,
                                          DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return function(feature);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result callDestroyParametersSafely(NVSDK_NGX_Parameter* parameters,
                                             DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return NVSDK_NGX_D3D12_DestroyParameters(parameters);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result callShutdownSafely(ShutdownFn function, ID3D12Device* device,
                                    DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return function(device);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result callSnippetInitSafely(SnippetInitExtFn function,
                                       const wchar_t* applicationDataPath, ID3D12Device* device,
                                       DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return function(kSignedSnippetApplicationId, applicationDataPath, device,
                        NVSDK_NGX_Version_API, nullptr);
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result NVSDK_CONV setScalingRatioCallback(NVSDK_NGX_Parameter* parameters) noexcept {
    __try {
        if (!parameters) {
            return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
        parameters->Set(ngx_parameter::ScalingRatio, 1.0F);
        return NVSDK_NGX_Result_Success;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

bool setCreateParametersSafely(NVSDK_NGX_Parameter* parameters, unsigned int width,
                               unsigned int height, int preset, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        parameters->Set(ngx_parameter::Width, width);
        parameters->Set(ngx_parameter::Height, height);
        parameters->Set(ngx_parameter::InputWidth, width);
        parameters->Set(ngx_parameter::InputHeight, height);
        parameters->Set(ngx_parameter::OutputWidth, width);
        parameters->Set(ngx_parameter::OutputHeight, height);
        parameters->Set(ngx_parameter::OutputDotWidth, width);
        parameters->Set(ngx_parameter::OutputDotHeight, height);
        parameters->Set(ngx_parameter::Upscaling, 0U);
        parameters->Set(ngx_parameter::Scale, 1.0F);
        parameters->Set(ngx_parameter::ScalingRatio, 1.0F);
        parameters->Set(ngx_parameter::ScalingRatioCallback,
                        functionAddress(&setScalingRatioCallback));
        parameters->Set(ngx_parameter::Preset, preset);
        parameters->Set(NVSDK_NGX_Parameter_Width, width);
        parameters->Set(NVSDK_NGX_Parameter_Height, height);
        parameters->Set(NVSDK_NGX_Parameter_PerfQualityValue,
                        static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
        parameters->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1U);
        parameters->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1U);
        return true;
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return false;
    }
}

void setFullSubrect(NVSDK_NGX_Parameter* parameters, const char* baseX, const char* baseY,
                    const char* widthName, const char* heightName, unsigned int width,
                    unsigned int height) {
    parameters->Set(baseX, 0U);
    parameters->Set(baseY, 0U);
    parameters->Set(widthName, width);
    parameters->Set(heightName, height);
}

bool setEvaluateParametersSafely(NVSDK_NGX_Parameter* parameters, ID3D12Resource* color,
                                 ID3D12Resource* output, ID3D12Resource* motion,
                                 ID3D12Resource* depth, unsigned int width, unsigned int height,
                                 const Feature18Settings* settings, bool resetHistory,
                                 DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        parameters->Set(ngx_parameter::Color, color);
        parameters->Set(ngx_parameter::Output, output);
        parameters->Set(ngx_parameter::MotionVectors, motion);
        parameters->Set(ngx_parameter::Depth, depth);
        setFullSubrect(parameters, ngx_parameter::ColorSubrectBaseX,
                       ngx_parameter::ColorSubrectBaseY, ngx_parameter::ColorSubrectWidth,
                       ngx_parameter::ColorSubrectHeight, width, height);
        setFullSubrect(parameters, ngx_parameter::OutputSubrectBaseX,
                       ngx_parameter::OutputSubrectBaseY, ngx_parameter::OutputSubrectWidth,
                       ngx_parameter::OutputSubrectHeight, width, height);
        setFullSubrect(parameters, ngx_parameter::MotionSubrectBaseX,
                       ngx_parameter::MotionSubrectBaseY, ngx_parameter::MotionSubrectWidth,
                       ngx_parameter::MotionSubrectHeight, width, height);
        setFullSubrect(parameters, ngx_parameter::DepthSubrectBaseX,
                       ngx_parameter::DepthSubrectBaseY, ngx_parameter::DepthSubrectWidth,
                       ngx_parameter::DepthSubrectHeight, width, height);
        parameters->Set(ngx_parameter::MotionScaleX, settings->motionScaleX);
        parameters->Set(ngx_parameter::MotionScaleY, settings->motionScaleY);
        const int depthInverted = settings->depthConvention == DepthConvention::ForceNormal ? 0 : 1;
        parameters->Set(ngx_parameter::DepthInverted, depthInverted);
        parameters->Set(ngx_parameter::IndicatorInvertX, 0);
        parameters->Set(ngx_parameter::IndicatorInvertY, 0);
        parameters->Set(ngx_parameter::Enabled, 1);
        parameters->Set(ngx_parameter::Reset, resetHistory ? 1 : 0);
        parameters->Set(ngx_parameter::Style, settings->style);
        parameters->Set(ngx_parameter::Intensity, settings->intensity);
        parameters->Set(ngx_parameter::LocalToneStrength, settings->localToneStrength);
        parameters->Set(ngx_parameter::LocalStructureStrength, settings->localStructureStrength);
        parameters->Set(ngx_parameter::SkinStructureStrength, settings->skinStructureStrength);
        parameters->Set(ngx_parameter::UseAutoMask, settings->useAutoMask ? 1 : 0);
        parameters->Set(ngx_parameter::UiCorrection, settings->uiCorrection ? 1 : 0);
        return true;
    } __except (captureNgxException(GetExceptionCode(), sehCode)) {
        return false;
    }
}

DWORD WINAPI snippetGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size) noexcept {
    if (module == g_callerModule.load(std::memory_order_acquire)) {
        constexpr wchar_t kAuthorizedCaller[] = L"nvngx.dll";
        constexpr DWORD kAuthorizedLength = ARRAYSIZE(kAuthorizedCaller) - 1;
        if (!filename || !size) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        if (size <= kAuthorizedLength) {
            if (size > 1) {
                std::memcpy(filename, kAuthorizedCaller,
                            static_cast<std::size_t>(size - 1) * sizeof(wchar_t));
            }
            filename[size - 1] = L'\0';
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return size;
        }
        std::memcpy(filename, kAuthorizedCaller, sizeof(kAuthorizedCaller));
        return kAuthorizedLength;
    }

    const auto original = g_originalGetModuleFileNameW.load(std::memory_order_acquire);
    if (original) {
        return original(module, filename, size);
    }
    SetLastError(ERROR_INVALID_FUNCTION);
    return 0;
}

void** findImportedFunctionSlot(HMODULE module, const char* functionName) noexcept {
    if (!module || !functionName) {
        return nullptr;
    }
    auto* base = reinterpret_cast<std::byte*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return nullptr;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return nullptr;
    }
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || !directory.Size ||
        directory.VirtualAddress >= nt->OptionalHeader.SizeOfImage ||
        directory.Size > nt->OptionalHeader.SizeOfImage ||
        directory.VirtualAddress > nt->OptionalHeader.SizeOfImage - directory.Size) {
        return nullptr;
    }
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    const auto* descriptorEnd = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        base + directory.VirtualAddress + directory.Size);
    for (; descriptor < descriptorEnd && descriptor->Name; ++descriptor) {
        if (descriptor->Name >= nt->OptionalHeader.SizeOfImage) {
            continue;
        }
        const char* libraryName = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(libraryName, "KERNEL32.dll") != 0 &&
            _stricmp(libraryName, "api-ms-win-core-libraryloader-l1-2-0.dll") != 0 &&
            _stricmp(libraryName, "api-ms-win-core-libraryloader-l1-1-0.dll") != 0) {
            continue;
        }
        if (!descriptor->OriginalFirstThunk || !descriptor->FirstThunk) {
            continue;
        }
        auto* nameThunk =
            reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
        auto* addressThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
        for (; nameThunk->u1.AddressOfData; ++nameThunk, ++addressThunk) {
            if (IMAGE_SNAP_BY_ORDINAL64(nameThunk->u1.Ordinal)) {
                continue;
            }
            const auto nameRva = static_cast<std::uint32_t>(nameThunk->u1.AddressOfData);
            if (nameRva >= nt->OptionalHeader.SizeOfImage) {
                return nullptr;
            }
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + nameRva);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), functionName) == 0) {
                return reinterpret_cast<void**>(&addressThunk->u1.Function);
            }
        }
    }
    return nullptr;
}

bool installSnippetCallerCompatibility(HMODULE snippetModule, void*** slotOut) noexcept {
    std::scoped_lock lock(g_hookMutex);
    void** slot = findImportedFunctionSlot(snippetModule, "GetModuleFileNameW");
    if (!slot) {
        return false;
    }
    if (g_hookReferenceCount != 0) {
        if (snippetModule != g_hookedSnippetModule || slot != g_hookedIatSlot) {
            return false;
        }
        ++g_hookReferenceCount;
        *slotOut = slot;
        writeDiagnosticLog("Reused process-level DLSSNR caller hook: refs=" +
                           std::to_string(g_hookReferenceCount));
        return true;
    }
    const void* hookAddress = functionAddress(&snippetGetModuleFileNameW);
    HMODULE callerModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(hookAddress), &callerModule)) {
        return false;
    }
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) {
        return false;
    }
    g_callerModule.store(callerModule, std::memory_order_release);
    void* original = InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),
                                                const_cast<void*>(hookAddress));
    GetModuleFileNameWFn originalFunction = nullptr;
    std::memcpy(&originalFunction, &original, sizeof(original));
    g_originalGetModuleFileNameW.store(originalFunction, std::memory_order_release);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    if (!originalFunction) {
        return false;
    }
    g_hookedSnippetModule = snippetModule;
    g_hookedIatSlot = slot;
    g_hookReferenceCount = 1;
    *slotOut = slot;
    writeDiagnosticLog("Installed process-level DLSSNR caller hook: refs=1");
    return true;
}

bool restoreSnippetCallerCompatibility(void** slot) noexcept {
    if (!slot) {
        return true;
    }
    std::scoped_lock lock(g_hookMutex);
    if (g_hookReferenceCount == 0 || slot != g_hookedIatSlot) {
        return false;
    }
    if (g_hookReferenceCount > 1) {
        --g_hookReferenceCount;
        writeDiagnosticLog("Released shared DLSSNR caller hook reference: refs=" +
                           std::to_string(g_hookReferenceCount));
        return true;
    }
    const auto original = g_originalGetModuleFileNameW.load(std::memory_order_acquire);
    DWORD oldProtection = 0;
    if (!original || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) {
        return false;
    }
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), functionAddress(original));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    g_originalGetModuleFileNameW.store(nullptr, std::memory_order_release);
    g_callerModule.store(nullptr, std::memory_order_release);
    g_hookedSnippetModule = nullptr;
    g_hookedIatSlot = nullptr;
    g_hookReferenceCount = 0;
    writeDiagnosticLog("Restored process-level DLSSNR caller hook: refs=0");
    return true;
}

std::filesystem::path moduleDirectory() {
    HMODULE module = nullptr;
    const void* address = functionAddress(&moduleDirectory);
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module)) {
        return {};
    }
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) {
        return {};
    }
    path.resize(size);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path applicationDataDirectory() {
    std::wstring root(32768, L'\0');
    const DWORD size =
        GetEnvironmentVariableW(L"LOCALAPPDATA", root.data(), static_cast<DWORD>(root.size()));
    if (!size || size >= root.size()) {
        return moduleDirectory();
    }
    root.resize(size);
    return std::filesystem::path(root) / L"ResolveDlss5" / L"NGX";
}

D3D12_RESOURCE_DESC textureDescription(int width, int height, DXGI_FORMAT format,
                                       D3D12_RESOURCE_FLAGS flags) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = static_cast<UINT64>(width);
    description.Height = static_cast<UINT>(height);
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = flags;
    return description;
}

D3D12_RESOURCE_DESC bufferDescription(UINT64 size) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_BARRIER transitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after) noexcept {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

struct SharedDeviceSession {
    ComPtr<ID3D12Device> device;
    std::shared_ptr<GpuResourcePool> pool;
    bool coreInitialized = false, snippetInitialized = false;
    HMODULE module = nullptr;
    void** hook = nullptr;
    SnippetInitExtFn init = nullptr;
    CreateFeatureFn create = nullptr;
    EvaluateFeatureFn evaluate = nullptr;
    ReleaseFeatureFn release = nullptr;
    ShutdownFn shutdown = nullptr;
    ~SharedDeviceSession() {
        DWORD seh = 0;
        if (snippetInitialized && shutdown) callShutdownSafely(shutdown, device.Get(), &seh);
        const bool restored = restoreSnippetCallerCompatibility(hook);
        if (module && restored) FreeLibrary(module);
        if (coreInitialized)
            callShutdownSafely(static_cast<ShutdownFn>(&NVSDK_NGX_D3D12_Shutdown1), device.Get(),
                               &seh);
        writeDiagnosticLog("Last shared device/session consumer released");
    }
};
std::weak_ptr<SharedDeviceSession> g_deviceSession;

}  // namespace

struct Feature18Runtime::Impl {
    std::mutex mutex;
    std::string lastError;
    int width = 0;
    int height = 0;
    NrPreset preset = NrPreset::Preset1;
    int passCount = 1;
    struct Pass {
        NVSDK_NGX_Parameter* parameters = nullptr;
        NVSDK_NGX_Handle* feature = nullptr;
        ComPtr<ID3D12Resource> output;
        NrPreset preset = NrPreset::Preset1;
    };
    std::array<Pass, 2> passes;
    bool initialized = false;
    bool failureLatched = false;
    bool returnNeuralProxy = false;

    std::shared_ptr<SharedDeviceSession> session;
    std::vector<GpuResourcePool::Lease> leases;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    std::uint64_t fenceValue = 0;
    std::uint64_t evaluateCount = 0;

    ComPtr<ID3D12Resource> input;
    ComPtr<ID3D12Resource> output;
    ComPtr<ID3D12Resource> motion;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> inputUpload;
    ComPtr<ID3D12Resource> motionUpload;
    ComPtr<ID3D12Resource> outputReadback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT inputFootprint{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT motionFootprint{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT outputFootprint{};
    std::uint8_t* mappedInput = nullptr;
    std::uint8_t* mappedMotion = nullptr;
    std::vector<Rgba8> baseline;

    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    bool coreInitialized = false;
    bool snippetInitialized = false;
    HMODULE snippetModule = nullptr;
    SnippetInitExtFn snippetInit = nullptr;
    CreateFeatureFn snippetCreate = nullptr;
    EvaluateFeatureFn snippetEvaluate = nullptr;
    ReleaseFeatureFn snippetRelease = nullptr;
    ShutdownFn snippetShutdown = nullptr;
    void** snippetIatSlot = nullptr;

    bool fail(const std::string& message) {
        lastError = message;
        writeDiagnosticLog("ERROR: " + message);
        return false;
    }

    bool failHr(const char* operation, HRESULT result) {
        char message[192]{};
        std::snprintf(message, sizeof(message), "%s failed (HRESULT 0x%08X)", operation,
                      static_cast<unsigned int>(result));
        return fail(message);
    }

    bool failNgx(const char* operation, NVSDK_NGX_Result result, DWORD sehCode) {
        char message[224]{};
        if (sehCode) {
            std::snprintf(message, sizeof(message), "%s raised SEH 0x%08X", operation,
                          static_cast<unsigned int>(sehCode));
        } else {
            std::snprintf(message, sizeof(message), "%s failed (NGX 0x%08X)", operation,
                          static_cast<unsigned int>(result));
        }
        return fail(message);
    }

    bool waitForQueue() {
        const std::uint64_t value = ++fenceValue;
        HRESULT result = queue->Signal(fence.Get(), value);
        if (FAILED(result)) {
            return failHr("Signal D3D12 fence", result);
        }
        if (fence->GetCompletedValue() >= value) {
            return true;
        }
        ResetEvent(fenceEvent);
        result = fence->SetEventOnCompletion(value, fenceEvent);
        if (FAILED(result)) {
            return failHr("Set D3D12 fence event", result);
        }
        return WaitForSingleObject(fenceEvent, INFINITE) == WAIT_OBJECT_0 ||
               fail("Wait for D3D12 fence failed");
    }

    bool executeAndWait() {
        const HRESULT result = commandList->Close();
        if (FAILED(result)) {
            return failHr("Close D3D12 command list", result);
        }
        ID3D12CommandList* lists[]{commandList.Get()};
        for (auto& lease : leases) lease.submitted(fence.Get(), fenceValue + 1);
        queue->ExecuteCommandLists(1, lists);
        return waitForQueue();
    }

    bool resetCommandList() {
        HRESULT result = allocator->Reset();
        if (SUCCEEDED(result)) {
            result = commandList->Reset(allocator.Get(), nullptr);
        }
        return SUCCEEDED(result) || failHr("Reset D3D12 command list", result);
    }

    bool createTexture(int textureWidth, int textureHeight, DXGI_FORMAT format,
                       D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState,
                       ComPtr<ID3D12Resource>& resource) {
        const auto description = textureDescription(textureWidth, textureHeight, format, flags);
        try {
            auto lease = session->pool->acquire(description, D3D12_HEAP_TYPE_DEFAULT,
                                                D3D12_HEAP_FLAG_NONE, initialState);
            resource = lease.get();
            leases.push_back(std::move(lease));
            return true;
        } catch (const std::exception& error) {
            return fail(error.what());
        }
    }

    bool createBuffer(UINT64 size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState,
                      ComPtr<ID3D12Resource>& resource) {
        try {
            auto lease = session->pool->acquire(bufferDescription(size), heapType,
                                                D3D12_HEAP_FLAG_NONE, initialState);
            resource = lease.get();
            leases.push_back(std::move(lease));
            return true;
        } catch (const std::exception& error) {
            return fail(error.what());
        }
    }
    void setLeaseState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
        for (auto& lease : leases)
            if (lease.get() == resource) lease.state(state);
    }

    bool recordZeroTexture(ID3D12Resource* texture, ComPtr<ID3D12Resource>& zeroUpload) {
        const D3D12_RESOURCE_DESC description = texture->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowSize = 0;
        UINT64 total = 0;
        device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, &rows, &rowSize, &total);
        if (!createBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                          zeroUpload)) {
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        HRESULT result = zeroUpload->Map(0, &readRange, &mapped);
        if (FAILED(result) || !mapped) {
            return failHr("Map zero-guidance upload", result);
        }
        std::memset(mapped, 0, static_cast<std::size_t>(total));
        const D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(total)};
        zeroUpload->Unmap(0, &writtenRange);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = zeroUpload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        const auto toCopy =
            transitionBarrier(texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->ResourceBarrier(1, &toCopy);
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        const auto barrier =
            transitionBarrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        commandList->ResourceBarrier(1, &barrier);
        return true;
    }

    bool chooseDevice() {
        session = g_deviceSession.lock();
        if (session) {
            device = session->device;
            writeDiagnosticLog("Reused shared D3D12 device/session");
            return true;
        }
        session = std::make_shared<SharedDeviceSession>();
        ComPtr<IDXGIFactory6> factory;
        HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
        if (FAILED(result)) {
            return failHr("Create DXGI factory", result);
        }

        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            result =
                factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                    IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(result)) {
                continue;
            }
            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(adapter->GetDesc1(&description)) || description.VendorId != 0x10DE ||
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                continue;
            }
            result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                       IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
            if (SUCCEEDED(result)) {
                char adapterName[256]{};
                WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, adapterName,
                                    static_cast<int>(sizeof(adapterName)), nullptr, nullptr);
                writeDiagnosticLog(std::string("D3D12 adapter selected: ") + adapterName);
                session->device = device;
                session->pool = std::make_shared<GpuResourcePool>(device.Get());
                g_deviceSession = session;
                return true;
            }
        }
        return fail("No compatible NVIDIA D3D12 adapter was found");
    }

    bool createD3DObjects() {
        if (!chooseDevice()) {
            return false;
        }
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        HRESULT result = device->CreateCommandQueue(&queueDescription,
                                                    IID_PPV_ARGS(queue.ReleaseAndGetAddressOf()));
        if (SUCCEEDED(result)) {
            result = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.ReleaseAndGetAddressOf()));
        }
        if (SUCCEEDED(result)) {
            result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                               nullptr,
                                               IID_PPV_ARGS(commandList.ReleaseAndGetAddressOf()));
        }
        if (SUCCEEDED(result)) {
            result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                         IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
        }
        if (FAILED(result)) {
            return failHr("Create D3D12 command objects", result);
        }
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fenceEvent != nullptr || fail("Create D3D12 fence event failed");
    }

    bool createFrameResources() {
        if (!createTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_COPY_DEST, input) ||
            !createTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
                           output) ||
            !createTexture(width, height, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COMMON, motion) ||
            !createTexture(width, height, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COMMON, depth)) {
            return false;
        }

        const auto inputDescription = input->GetDesc();
        UINT inputRows = 0;
        UINT64 inputRowSize = 0;
        UINT64 inputTotal = 0;
        device->GetCopyableFootprints(&inputDescription, 0, 1, 0, &inputFootprint, &inputRows,
                                      &inputRowSize, &inputTotal);
        if (!createBuffer(inputTotal, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                          inputUpload)) {
            return false;
        }
        const D3D12_RANGE noRead{0, 0};
        void* mapped = nullptr;
        const HRESULT mapResult = inputUpload->Map(0, &noRead, &mapped);
        if (FAILED(mapResult) || !mapped) {
            return failHr("Map input upload", mapResult);
        }
        mappedInput = static_cast<std::uint8_t*>(mapped);

        const auto motionDescription = motion->GetDesc();
        UINT64 motionTotal = 0;
        device->GetCopyableFootprints(&motionDescription, 0, 1, 0, &motionFootprint, nullptr,
                                      nullptr, &motionTotal);
        if (!createBuffer(motionTotal, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                          motionUpload))
            return false;
        mapped = nullptr;
        const auto motionMap = motionUpload->Map(0, &noRead, &mapped);
        if (FAILED(motionMap) || !mapped) return failHr("Map motion upload", motionMap);
        mappedMotion = static_cast<std::uint8_t*>(mapped);

        const auto outputDescription = output->GetDesc();
        UINT outputRows = 0;
        UINT64 outputRowSize = 0;
        UINT64 outputTotal = 0;
        device->GetCopyableFootprints(&outputDescription, 0, 1, 0, &outputFootprint, &outputRows,
                                      &outputRowSize, &outputTotal);
        return createBuffer(outputTotal, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                            outputReadback);
    }

    bool initializeNgx() {
        DWORD sehCode = 0;
        NVSDK_NGX_Result ngxResult = NVSDK_NGX_Result_Success;
        if (!session->snippetInitialized) {
            const std::filesystem::path runtimeDirectory = moduleDirectory() / L"runtime";
            const std::filesystem::path dataDirectory = applicationDataDirectory();
            std::error_code ignored;
            std::filesystem::create_directories(dataDirectory, ignored);
            writeDiagnosticLog("NGX paths: runtime=" + runtimeDirectory.string() +
                               ", data=" + dataDirectory.string());

            const std::wstring featurePath = runtimeDirectory.wstring();
            const wchar_t* paths[]{featurePath.c_str()};
            NVSDK_NGX_FeatureCommonInfo featureInfo{};
            featureInfo.PathListInfo.Path = paths;
            featureInfo.PathListInfo.Length = 1;

            ngxResult =
                callCoreInitSafely(dataDirectory.c_str(), device.Get(), &featureInfo, &sehCode);
            if (sehCode || !ngxSucceeded(ngxResult)) {
                return failNgx("NGX Core Init", ngxResult, sehCode);
            }
            coreInitialized = true;
            session->coreInitialized = true;
            writeDiagnosticLog("NGX Core Init succeeded");

            const std::filesystem::path dllPath = runtimeDirectory / L"nvngx_dlssnr.dll";
            snippetModule =
                LoadLibraryExW(dllPath.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!snippetModule) {
                return fail("Load private runtime/nvngx_dlssnr.dll failed");
            }
            session->module = snippetModule;
            writeDiagnosticLog("Private DLSSNR runtime loaded: " + dllPath.string());
            snippetInit = getExport<SnippetInitExtFn>(snippetModule, "NVSDK_NGX_D3D12_Init_Ext");
            snippetCreate =
                getExport<CreateFeatureFn>(snippetModule, "NVSDK_NGX_D3D12_CreateFeature");
            snippetEvaluate =
                getExport<EvaluateFeatureFn>(snippetModule, "NVSDK_NGX_D3D12_EvaluateFeature");
            snippetRelease =
                getExport<ReleaseFeatureFn>(snippetModule, "NVSDK_NGX_D3D12_ReleaseFeature");
            snippetShutdown = getExport<ShutdownFn>(snippetModule, "NVSDK_NGX_D3D12_Shutdown1");
            if (!snippetInit || !snippetCreate || !snippetEvaluate || !snippetRelease ||
                !snippetShutdown) {
                return fail("Private DLSSNR runtime exports are incomplete");
            }
            if (!installSnippetCallerCompatibility(snippetModule, &snippetIatSlot)) {
                return fail("Install DLSSNR caller compatibility hook failed");
            }
            session->hook = snippetIatSlot;
            ngxResult =
                callSnippetInitSafely(snippetInit, dataDirectory.c_str(), device.Get(), &sehCode);
            if (sehCode || !ngxSucceeded(ngxResult)) {
                return failNgx("DLSSNR Init_Ext", ngxResult, sehCode);
            }
            snippetInitialized = true;
            session->snippetInitialized = true;
            session->init = snippetInit;
            session->create = snippetCreate;
            session->evaluate = snippetEvaluate;
            session->release = snippetRelease;
            session->shutdown = snippetShutdown;
            writeDiagnosticLog("DLSSNR Init_Ext succeeded");

        } else {
            snippetModule = session->module;
            snippetInit = session->init;
            snippetCreate = session->create;
            snippetEvaluate = session->evaluate;
            snippetRelease = session->release;
            snippetShutdown = session->shutdown;
        }
        ngxResult = callAllocateParametersSafely(&parameters, &sehCode);
        if (sehCode || !ngxSucceeded(ngxResult) || !parameters) {
            return failNgx("NGX AllocateParameters", ngxResult, sehCode);
        }
        if (!setCreateParametersSafely(parameters, static_cast<unsigned int>(width),
                                       static_cast<unsigned int>(height), static_cast<int>(preset),
                                       &sehCode)) {
            return failNgx("Set Feature 18 creation parameters",
                           NVSDK_NGX_Result_FAIL_PlatformError, sehCode);
        }

        ComPtr<ID3D12Resource> zeroMotion;
        ComPtr<ID3D12Resource> zeroDepth;
        if (!recordZeroTexture(motion.Get(), zeroMotion) ||
            !recordZeroTexture(depth.Get(), zeroDepth)) {
            return false;
        }
        ngxResult = callCreateFeatureSafely(snippetCreate, commandList.Get(), parameters, &feature,
                                            &sehCode);
        if (sehCode || !ngxSucceeded(ngxResult) || !feature) {
            return failNgx("Feature 18 CreateFeature", ngxResult, sehCode);
        }
        setLeaseState(motion.Get(), D3D12_RESOURCE_STATE_COMMON);
        setLeaseState(depth.Get(), D3D12_RESOURCE_STATE_COMMON);
        if (!executeAndWait()) return false;
        // Uploads are only needed through this completed initialization fence.
        // Return their leases before allocating additional pass outputs.
        auto* zeroMotionUpload = zeroMotion.Get();
        auto* depthUpload = zeroDepth.Get();
        zeroMotion.Reset();
        zeroDepth.Reset();
        std::erase_if(leases, [=](const GpuResourcePool::Lease& lease) {
            return lease.get() == zeroMotionUpload || lease.get() == depthUpload;
        });
        writeDiagnosticLog("Feature 18 CreateFeature succeeded");
        return true;
    }

    bool initialize(int requestedWidth, int requestedHeight, const Feature18Settings& requested) {
        const NrPreset value = requested.preset;
        passCount = requested.passCount;
        for (int i = 0; i < passCount - 1; ++i)
            passes[i].preset = requested.additionalPasses[i].preset;
        width = requestedWidth;
        height = requestedHeight;
        preset = value;
        lastError.clear();
        writeDiagnosticLog("Initialize requested: " + std::to_string(width) + "x" +
                           std::to_string(height) +
                           ", preset=" + std::to_string(static_cast<int>(preset)));
        if (!createD3DObjects() || !createFrameResources() || !initializeNgx()) {
            const std::string savedError = lastError;
            shutdown();
            lastError = savedError;
            failureLatched = true;
            return false;
        }
        if (passCount > 1) {
            if (!resetCommandList()) return false;
            for (int i = 0; i < passCount - 1; ++i) {
                auto& p = passes[i];
                DWORD seh = 0;
                if (!createTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COMMON, p.output))
                    return false;
                auto result = callAllocateParametersSafely(&p.parameters, &seh);
                if (seh || !ngxSucceeded(result) || !p.parameters)
                    return failNgx("Allocate extra pass parameters", result, seh);
                if (!setCreateParametersSafely(p.parameters, static_cast<unsigned>(width),
                                               static_cast<unsigned>(height),
                                               static_cast<int>(p.preset), &seh))
                    return failNgx("Configure extra pass", NVSDK_NGX_Result_FAIL_PlatformError,
                                   seh);
                result = callCreateFeatureSafely(snippetCreate, commandList.Get(), p.parameters,
                                                 &p.feature, &seh);
                if (seh || !ngxSucceeded(result) || !p.feature)
                    return failNgx("Create extra NR pass", result, seh);
            }
            if (!executeAndWait()) return false;
        }
        initialized = true;
        failureLatched = false;
        lastError.clear();
        writeDiagnosticLog("Feature 18 D3D12 session initialized");
        return true;
    }

    void shutdown() noexcept {
        if (queue && fence) {
            waitForQueue();
        }
        for (auto& p : passes) {
            DWORD seh = 0;
            if (p.feature && snippetRelease)
                callReleaseFeatureSafely(snippetRelease, p.feature, &seh);
            if (p.parameters) callDestroyParametersSafely(p.parameters, &seh);
            p.feature = nullptr;
            p.parameters = nullptr;
            p.output.Reset();
        }
        if (feature && snippetRelease) {
            DWORD sehCode = 0;
            callReleaseFeatureSafely(snippetRelease, feature, &sehCode);
            feature = nullptr;
        }
        if (parameters) {
            DWORD sehCode = 0;
            callDestroyParametersSafely(parameters, &sehCode);
            parameters = nullptr;
        }
        // Session exports, hook and NGX shutdown belong to the final consumer.
        snippetModule = nullptr;
        snippetIatSlot = nullptr;
        coreInitialized = false;
        snippetInitialized = false;
        if (mappedInput && inputUpload) {
            const D3D12_RANGE noWrite{0, 0};
            inputUpload->Unmap(0, &noWrite);
        }
        mappedInput = nullptr;
        if (mappedMotion && motionUpload) motionUpload->Unmap(0, nullptr);
        mappedMotion = nullptr;
        motionUpload.Reset();
        outputReadback.Reset();
        inputUpload.Reset();
        depth.Reset();
        motion.Reset();
        output.Reset();
        input.Reset();
        commandList.Reset();
        allocator.Reset();
        queue.Reset();
        fence.Reset();
        device.Reset();
        leases.clear();
        session.reset();
        if (fenceEvent) {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
        initialized = false;
        evaluateCount = 0;
        width = 0;
        height = 0;
    }

    bool uploadSource(const float* source, int sourceRowBytes, const Feature18Settings& settings) {
        const std::size_t sourceStride =
            static_cast<std::size_t>(std::abs(static_cast<std::int64_t>(sourceRowBytes)));
        if (sourceStride < static_cast<std::size_t>(width) * 4U * sizeof(float)) {
            return fail("Source row stride is smaller than float RGBA width");
        }
        // OpenFX addresses rows from the bottom-left, while D3D textures use
        // the top-left as their screen-space origin. Normalize that boundary
        // here so NGX receives an upright texture (including its indicator).
        parallelRows(height, static_cast<size_t>(width) * height, [&](int d3dY) noexcept {
            const int ofxY = height - 1 - d3dY;
            const auto* sourceRow =
                reinterpret_cast<const float*>(reinterpret_cast<const std::byte*>(source) +
                                               static_cast<std::ptrdiff_t>(ofxY) * sourceRowBytes);
            auto* destinationRow =
                mappedInput + static_cast<std::size_t>(d3dY) * inputFootprint.Footprint.RowPitch;
            for (int x = 0; x < width; ++x) {
                // Quantization is tracked as the actual neural baseline.
                Rgb rgb{sourceRow[x * 4], sourceRow[x * 4 + 1], sourceRow[x * 4 + 2]};
                if (settings.premultiplied) {
                    const float a = sourceRow[x * 4 + 3];
                    for (float& v : rgb) v = std::isfinite(a) && a > 1e-6f ? v / a : 0;
                }
                const auto pixel = quantizeNeuralProxy(encodeNeuralProxy(rgb, settings));
                baseline[static_cast<size_t>(ofxY) * width + x] = pixel;
            }
            // Upload heaps are typically write-combined: stream a whole row
            // after computing it in cacheable memory instead of tiny stores.
            std::memcpy(destinationRow, baseline.data() + static_cast<size_t>(ofxY) * width,
                        static_cast<size_t>(width) * sizeof(Rgba8));
        });
        return true;
    }

    bool downloadOutput(const float* source, int sourceRowBytes, float* destination,
                        int destinationRowBytes, const Feature18Settings& settings) {
        void* mapped = nullptr;
        // GetCopyableFootprints excludes the final row's unused padding.
        // A full RowPitch*height range can exceed the allocated resource.
        const UINT64 readSize =
            outputFootprint.Offset +
            static_cast<UINT64>(outputFootprint.Footprint.RowPitch) * (height - 1) +
            static_cast<UINT64>(width) * 4;
        if (readSize > outputReadback->GetDesc().Width)
            return fail("Feature 18 output readback footprint exceeds buffer");
        const D3D12_RANGE readRange{0, static_cast<SIZE_T>(readSize)};
        const HRESULT result = outputReadback->Map(0, &readRange, &mapped);
        if (FAILED(result) || !mapped) {
            return failHr("Map Feature 18 output readback", result);
        }
        std::vector<Rgba8> neural(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y) {
            const auto* row =
                static_cast<const std::uint8_t*>(mapped) +
                static_cast<size_t>(height - 1 - y) * outputFootprint.Footprint.RowPitch;
            for (int x = 0; x < width; ++x)
                std::memcpy(neural[static_cast<size_t>(y) * width + x].data(), row + x * 4, 4);
        }
        const D3D12_RANGE noWrite{0, 0};
        outputReadback->Unmap(0, &noWrite);
        if (returnNeuralProxy) {
            parallelRows(height, static_cast<size_t>(width) * height, [&](int y) noexcept {
                auto* row =
                    reinterpret_cast<float*>(reinterpret_cast<std::byte*>(destination) +
                                             static_cast<std::ptrdiff_t>(y) * destinationRowBytes);
                for (int x = 0; x < width; ++x) {
                    const Rgb pixel = unpackProxy(neural[static_cast<size_t>(y) * width + x]);
                    for (int c = 0; c < 3; ++c) row[x * 4 + c] = pixel[c];
                    row[x * 4 + 3] = 1;
                }
            });
            return true;
        }
        std::vector<Rgb> controlled;
        controlFrame(baseline, neural, width, height, settings.detail, controlled);
        // Convert the top-left D3D output back to OpenFX's bottom-left row
        // order. Source and destination remain on the same logical OFX row
        // for proxy decoding and alpha restoration.
        for (int ofxY = 0; ofxY < height; ++ofxY) {
            const auto* sourceRow =
                reinterpret_cast<const float*>(reinterpret_cast<const std::byte*>(source) +
                                               static_cast<std::ptrdiff_t>(ofxY) * sourceRowBytes);
            auto* destinationRow =
                reinterpret_cast<float*>(reinterpret_cast<std::byte*>(destination) +
                                         static_cast<std::ptrdiff_t>(ofxY) * destinationRowBytes);
            for (int x = 0; x < width; ++x) {
                const size_t i = static_cast<size_t>(ofxY) * width + x;
                const Rgb delta =
                    netHostCorrection(unpackProxy(baseline[i]), controlled[i], settings);
                const float alpha = settings.premultiplied ? (std::isfinite(sourceRow[x * 4 + 3]) &&
                                                                      sourceRow[x * 4 + 3] > 1e-6f
                                                                  ? sourceRow[x * 4 + 3]
                                                                  : 0)
                                                           : 1;
                for (int channel = 0; channel < 3; ++channel) {
                    const float original = sourceRow[x * 4 + channel];
                    const float correction = delta[channel] * alpha;
                    destinationRow[x * 4 + channel] =
                        std::isfinite(original)
                            ? (correction == 0 ? original : original + correction)
                            : 0;
                }
                destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
            }
        }
        return true;
    }

    bool evaluate(const float* source, int sourceRowBytes, float* destination,
                  int destinationRowBytes, const Feature18Settings& settings, bool resetHistory,
                  const float* motionTopLeft, RuntimeTimings* timings) {
        using Clock = std::chrono::steady_clock;
        const auto preparationStart = Clock::now();
        if (!uploadSource(source, sourceRowBytes, settings) || !resetCommandList()) {
            return false;
        }

        D3D12_TEXTURE_COPY_LOCATION inputDestination{};
        inputDestination.pResource = input.Get();
        inputDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION inputSource{};
        inputSource.pResource = inputUpload.Get();
        inputSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        inputSource.PlacedFootprint = inputFootprint;
        commandList->CopyTextureRegion(&inputDestination, 0, 0, 0, &inputSource, nullptr);

        for (int y = 0; y < height; ++y) {
            auto* row = reinterpret_cast<uint16_t*>(
                mappedMotion + static_cast<std::size_t>(y) * motionFootprint.Footprint.RowPitch);
            if (!motionTopLeft) {
                // Keep explicit per-frame clearing after external/internal
                // guidance, but do not convert millions of constant zeros.
                std::memset(row, 0, static_cast<size_t>(width) * 2 * sizeof(uint16_t));
                continue;
            }
            for (int x = 0; x < width * 2; ++x) {
                const float value = motionTopLeft[static_cast<std::size_t>(y) * width * 2 + x];
                if (!std::isfinite(value) || std::abs(value) > 65504) {
                    commandList->Close();
                    return fail("Invalid dense motion vector");
                }
                row[x] = DirectX::PackedVector::XMConvertFloatToHalf(value);
            }
        }
        auto motionBarrier = transitionBarrier(motion.Get(), D3D12_RESOURCE_STATE_COMMON,
                                               D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->ResourceBarrier(1, &motionBarrier);
        D3D12_TEXTURE_COPY_LOCATION motionDestination{};
        motionDestination.pResource = motion.Get();
        motionDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION motionSource{};
        motionSource.pResource = motionUpload.Get();
        motionSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        motionSource.PlacedFootprint = motionFootprint;
        commandList->CopyTextureRegion(&motionDestination, 0, 0, 0, &motionSource, nullptr);

        D3D12_RESOURCE_BARRIER before[4]{
            transitionBarrier(input.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transitionBarrier(output.Get(), D3D12_RESOURCE_STATE_COMMON,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transitionBarrier(motion.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transitionBarrier(depth.Get(), D3D12_RESOURCE_STATE_COMMON,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        };
        commandList->ResourceBarrier(ARRAYSIZE(before), before);

        ID3D12Resource* previous = input.Get();
        ID3D12Resource* last = output.Get();
        for (int index = 0; index < passCount; ++index) {
            Feature18Settings passSettings = settings;
            auto* params = index == 0 ? parameters : passes[index - 1].parameters;
            auto* handle = index == 0 ? feature : passes[index - 1].feature;
            last = index == 0 ? output.Get() : passes[index - 1].output.Get();
            if (index > 0) {
                const auto& p = settings.additionalPasses[index - 1];
                passSettings.preset = p.preset;
                passSettings.uiCorrection = p.uiCorrection;
                passSettings.style = p.style;
                passSettings.intensity = p.intensity;
                passSettings.localToneStrength = p.localToneStrength;
                passSettings.localStructureStrength = p.localStructureStrength;
                passSettings.skinStructureStrength = p.skinStructureStrength;
                passSettings.useAutoMask = p.useAutoMask;
                D3D12_RESOURCE_BARRIER barriers[]{
                    transitionBarrier(previous, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                    transitionBarrier(last, D3D12_RESOURCE_STATE_COMMON,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
                commandList->ResourceBarrier(2, barriers);
            }
            DWORD sehCode = 0;
            if (!setEvaluateParametersSafely(
                    params, previous, last, motion.Get(), depth.Get(), static_cast<unsigned>(width),
                    static_cast<unsigned>(height), &passSettings, resetHistory, &sehCode)) {
                commandList->Close();
                return failNgx("Set NR pass parameters", NVSDK_NGX_Result_FAIL_PlatformError,
                               sehCode);
            }
            const auto result = callEvaluateFeatureSafely(snippetEvaluate, commandList.Get(),
                                                          handle, params, &sehCode);
            if (sehCode || !ngxSucceeded(result)) {
                commandList->Close();
                return failNgx(("Evaluate NR pass " + std::to_string(index + 1)).c_str(), result,
                               sehCode);
            }
            previous = last;
        }

        const auto outputToCopy = transitionBarrier(last, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                    D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->ResourceBarrier(1, &outputToCopy);
        D3D12_TEXTURE_COPY_LOCATION outputDestination{};
        outputDestination.pResource = outputReadback.Get();
        outputDestination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        outputDestination.PlacedFootprint = outputFootprint;
        D3D12_TEXTURE_COPY_LOCATION outputSource{};
        outputSource.pResource = last;
        outputSource.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commandList->CopyTextureRegion(&outputDestination, 0, 0, 0, &outputSource, nullptr);

        D3D12_RESOURCE_BARRIER after[4]{
            transitionBarrier(input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                              D3D12_RESOURCE_STATE_COPY_DEST),
            transitionBarrier(last, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
            transitionBarrier(motion.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                              D3D12_RESOURCE_STATE_COMMON),
            transitionBarrier(depth.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                              D3D12_RESOURCE_STATE_COMMON),
        };
        commandList->ResourceBarrier(ARRAYSIZE(after), after);
        for (int i = 0; i < passCount - 1; ++i) {
            auto* r = i == 0 ? output.Get() : passes[i - 1].output.Get();
            const auto barrier = transitionBarrier(
                r, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            commandList->ResourceBarrier(1, &barrier);
        }
        const auto submitStart = Clock::now();
        if (timings)
            timings->prepareMs =
                std::chrono::duration<double, std::milli>(submitStart - preparationStart).count();
        if (!executeAndWait()) {
            return false;
        }
        const auto readbackStart = Clock::now();
        if (timings)
            timings->submitWaitMs =
                std::chrono::duration<double, std::milli>(readbackStart - submitStart).count();
        if (!downloadOutput(source, sourceRowBytes, destination, destinationRowBytes, settings)) {
            return false;
        }
        if (timings)
            timings->readbackMs =
                std::chrono::duration<double, std::milli>(Clock::now() - readbackStart).count();
        ++evaluateCount;
        if (evaluateCount <= 3 || evaluateCount % 120 == 0) {
            writeDiagnosticLog(
                "Feature 18 EvaluateFeature succeeded: frame=" + std::to_string(evaluateCount) +
                ", reset=" + (resetHistory ? "1" : "0"));
        }
        return true;
    }
};

void writeDiagnosticLog(const std::string& message) noexcept {
    try {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        char prefix[96]{};
        std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u ", time.wYear,
                      time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                      time.wMilliseconds);
        const std::string line = std::string(prefix) + message + "\n";
        OutputDebugStringA(("ResolveDlss5: " + line).c_str());

        std::scoped_lock lock(g_logMutex);
        std::filesystem::path directory = applicationDataDirectory().parent_path();
        std::error_code ignored;
        std::filesystem::create_directories(directory, ignored);
        std::ofstream stream(directory / L"ResolveDlss5.log",
                             std::ios::out | std::ios::app | std::ios::binary);
        if (stream) {
            stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        }
    } catch (...) {
        OutputDebugStringA("ResolveDlss5: diagnostic logging failed\n");
    }
}

Feature18Runtime::Feature18Runtime() : impl_(std::make_unique<Impl>()) {
    writeDiagnosticLog("Feature18Runtime instance created");
}

Feature18Runtime::~Feature18Runtime() {
    if (!impl_) {
        return;
    }
    std::scoped_lock executionLock(g_runtimeExecutionMutex);
    std::scoped_lock lock(impl_->mutex);
    impl_->shutdown();
    writeDiagnosticLog("Feature18Runtime instance destroyed");
}

bool Feature18Runtime::process(const float* source, int sourceRowBytes, float* destination,
                               int destinationRowBytes, int width, int height,
                               const Feature18Settings& settings, bool resetHistory,
                               bool returnNeuralProxy, const float* motionTopLeft,
                               RuntimeTimings* timings) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    if (timings) *timings = {};
    std::scoped_lock executionLock(g_runtimeExecutionMutex);
    std::scoped_lock lock(impl_->mutex);
    const auto locked = Clock::now();
    if (timings)
        timings->lockMs = std::chrono::duration<double, std::milli>(locked - start).count();
    if (!source || !destination || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        std::abs(static_cast<std::int64_t>(sourceRowBytes)) <
            static_cast<std::int64_t>(width) * 16 ||
        std::abs(static_cast<std::int64_t>(destinationRowBytes)) <
            static_cast<std::int64_t>(width) * 16 ||
        !validColorSettings(settings) || settings.passCount < 1 || settings.passCount > 3) {
        return impl_->fail(
            "Invalid frame/stride, pass count or color settings. HDR peak must be 100-10000 nits "
            "and at least reference white.");
    }
    impl_->returnNeuralProxy = returnNeuralProxy;
    impl_->baseline.resize(static_cast<size_t>(width) * height);
    bool configurationChanged = impl_->width != width || impl_->height != height ||
                                impl_->preset != settings.preset ||
                                impl_->passCount != settings.passCount;
    for (int i = 0; i < settings.passCount - 1; ++i)
        configurationChanged |= impl_->passes[i].preset != settings.additionalPasses[i].preset;
    if (configurationChanged && impl_->initialized) {
        impl_->shutdown();
    }
    if (resetHistory && impl_->failureLatched) {
        impl_->shutdown();
        impl_->failureLatched = false;
    }
    if (impl_->failureLatched) {
        return false;
    }
    if (!impl_->initialized && !impl_->initialize(width, height, settings)) {
        const std::string error = impl_->lastError;
        impl_->shutdown();
        impl_->lastError = error;
        impl_->failureLatched = true;
        return false;
    }
    if (timings)
        timings->initializeMs =
            std::chrono::duration<double, std::milli>(Clock::now() - locked).count();
    if (!impl_->evaluate(source, sourceRowBytes, destination, destinationRowBytes, settings,
                         resetHistory || configurationChanged, motionTopLeft, timings)) {
        impl_->failureLatched = true;
        return false;
    }
    impl_->lastError.clear();
    if (timings)
        timings->totalMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return true;
}

const std::string& Feature18Runtime::lastError() const noexcept { return impl_->lastError; }

void Feature18Runtime::reset() {
    std::scoped_lock executionLock(g_runtimeExecutionMutex);
    std::scoped_lock lock(impl_->mutex);
    impl_->shutdown();
    impl_->failureLatched = false;
    impl_->lastError.clear();
}

}  // namespace resolve_dlss5
