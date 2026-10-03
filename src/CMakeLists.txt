cmake_minimum_required(VERSION 3.25)

project(ResolveDlss5 VERSION 0.4.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ResolveDlss5 currently supports Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    RESOLVE_DLSS5_BUILD_SMOKE_TEST
    "Build a standalone Feature 18 create/evaluate smoke test"
    ON
)

set(_default_ofx_sdk "$ENV{RESOLVE_OFX_SDK_ROOT}")
if(_default_ofx_sdk STREQUAL "")
    set(
        _default_ofx_sdk
        "C:/ProgramData/Blackmagic Design/DaVinci Resolve/Support/Developer/OpenFX"
    )
endif()
set(
    RESOLVE_OFX_SDK_ROOT
    "${_default_ofx_sdk}"
    CACHE PATH
    "DaVinci Resolve Developer/OpenFX directory"
)

set(
    DLSS_SDK_ROOT
    "$ENV{DLSS_SDK_ROOT}"
    CACHE PATH
    "NVIDIA NGX/DLSS SDK used for the parameter ABI and static loader"
)
set(
    DLSSNR_RUNTIME_DLL
    "$ENV{DLSSNR_RUNTIME_DLL}"
    CACHE FILEPATH
    "Community nvngx_dlssnr.dll copied into the private OFX runtime directory"
)

set(OFX_INCLUDE_DIR "${RESOLVE_OFX_SDK_ROOT}/OpenFX-1.4/include")
set(OFX_SUPPORT_INCLUDE_DIR "${RESOLVE_OFX_SDK_ROOT}/Support/include")
set(OFX_SUPPORT_LIBRARY_DIR "${RESOLVE_OFX_SDK_ROOT}/Support/Library")

foreach(required_path IN ITEMS
        "${OFX_INCLUDE_DIR}/ofxImageEffect.h"
        "${OFX_SUPPORT_INCLUDE_DIR}/ofxsImageEffect.h"
        "${OFX_SUPPORT_LIBRARY_DIR}/ofxsCore.cpp")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR
            "Resolve OpenFX SDK file not found: ${required_path}\n"
            "Set RESOLVE_OFX_SDK_ROOT in the environment or CMake cache.")
    endif()
endforeach()

foreach(required_path IN ITEMS
        "${DLSS_SDK_ROOT}/include/nvsdk_ngx.h"
        "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR
            "NVIDIA NGX/DLSS SDK file not found: ${required_path}\n"
            "Set DLSS_SDK_ROOT in the environment or CMake cache. "
            "The SDK is intentionally not vendored in this repository.")
    endif()
endforeach()

find_package(CUDAToolkit 12 REQUIRED)

set(FIDELITYFX_SDK_ROOT "$ENV{FIDELITYFX_SDK_ROOT}" CACHE PATH "External FidelityFX SDK v2.3.0 root")
set(NVOF_SDK_ROOT "$ENV{NVOF_SDK_ROOT}" CACHE PATH "External NVIDIA optical flow headers")
set(DXC_PATH "$ENV{DXC_PATH}" CACHE FILEPATH "DXC compiler for optical flow")
if(NOT DXC_PATH)
    find_program(_resolved_dxc NAMES dxc.exe dxc)
    if(_resolved_dxc)
        set(DXC_PATH "${_resolved_dxc}" CACHE FILEPATH "DXC compiler for optical flow" FORCE)
    endif()
endif()
set(FFX_ROOT "${FIDELITYFX_SDK_ROOT}/Kits/FidelityFX")
foreach(required_path IN ITEMS
        "${FFX_ROOT}/backend/dx12/ffx_dx12.cpp"
        "${FFX_ROOT}/framegeneration/fsr3/include/ffx_opticalflow.h"
        "${NVOF_SDK_ROOT}/nvOpticalFlowCuda.h"
        "${DXC_PATH}")
    if(NOT EXISTS "${required_path}" OR IS_DIRECTORY "${required_path}")
        message(FATAL_ERROR
            "Optical flow build dependency not found: ${required_path}\n"
            "Set FIDELITYFX_SDK_ROOT, NVOF_SDK_ROOT and DXC_PATH in the environment or CMake cache.")
    endif()
endforeach()
set(FFX_SHADERS "${CMAKE_BINARY_DIR}/FidelityFXOpticalFlow")
add_custom_target(GenerateOpticalFlowShaders
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Generate-FidelityFXOpticalFlowShaders.ps1"
        -SdkDirectory "${FIDELITYFX_SDK_ROOT}" -OutputDirectory "${FFX_SHADERS}" -DxcPath "${DXC_PATH}"
    VERBATIM)
add_library(ResolveOpticalFlow STATIC OpticalFlow.cpp AmdFlow.cpp ColorPipeline.cpp
    "${FFX_ROOT}/api/internal/ffx_assert.cpp"
    "${FFX_ROOT}/api/internal/ffx_message.cpp"
    "${FFX_ROOT}/api/internal/ffx_object_management.cpp"
    "${FFX_ROOT}/backend/dx12/ffx_dx12.cpp"
    "${FFX_ROOT}/backend/dx12/ffx_backends_dx12.cpp"
    "${FFX_ROOT}/framegeneration/fsr3/internal/ffx_opticalflow.cpp"
    "${FFX_ROOT}/framegeneration/fsr3/internal/ffx_opticalflow_shaderblobs.cpp")
add_dependencies(ResolveOpticalFlow GenerateOpticalFlowShaders)
target_include_directories(ResolveOpticalFlow PRIVATE "${FFX_SHADERS}" "${NVOF_SDK_ROOT}"
    "${FFX_ROOT}/backend/dx12" "${FFX_ROOT}/framegeneration/fsr3/include" "${CMAKE_CURRENT_SOURCE_DIR}/compat")
target_compile_definitions(ResolveOpticalFlow PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(ResolveOpticalFlow PRIVATE /utf-8 /EHsc)
target_link_libraries(ResolveOpticalFlow PUBLIC CUDA::cuda_driver d3d12 dxgi)
set_target_properties(ResolveOpticalFlow PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(OFX_SUPPORT_SOURCES
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsCore.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsImageEffect.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsInteract.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsLog.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsMultiThread.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsParams.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsProperty.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsPropertyValidation.cpp"
)

add_library(ResolveDlss5 MODULE
    ColorPipeline.cpp
    GpuResourcePool.cpp
    TimelineProcessor.cpp
    Feature18Runtime.cpp
    Feature18Runtime.h
    ResolveDlss5Plugin.cpp
    ResolveDlss5Plugin.h
    Feature18Parameters.h
    ${OFX_SUPPORT_SOURCES}
)

target_include_directories(ResolveDlss5 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OFX_INCLUDE_DIR}"
    "${OFX_SUPPORT_INCLUDE_DIR}"
    "${DLSS_SDK_ROOT}/include"
)

target_compile_definitions(ResolveDlss5 PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(ResolveDlss5 PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

target_link_libraries(ResolveDlss5 PRIVATE
    ResolveOpticalFlow
    CUDA::cuda_driver
    d3d12
    dxgi
    version
    bcrypt
    "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib"
)

set_target_properties(ResolveDlss5 PROPERTIES
    PREFIX ""
    SUFFIX ".ofx"
    OUTPUT_NAME "ResolveDlss5"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

set(BUNDLE_ROOT "${CMAKE_BINARY_DIR}/bundle/ResolveDlss5.ofx.bundle")
set(BUNDLE_BINARY_DIR "${BUNDLE_ROOT}/Contents/Win64")
set(BUNDLE_RUNTIME_DIR "${BUNDLE_BINARY_DIR}/runtime")

add_custom_command(TARGET ResolveDlss5 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUNDLE_BINARY_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUNDLE_RUNTIME_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:ResolveDlss5>"
            "${BUNDLE_BINARY_DIR}/ResolveDlss5.ofx"
    VERBATIM
)

if(EXISTS "${DLSSNR_RUNTIME_DLL}")
    add_custom_command(TARGET ResolveDlss5 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${DLSSNR_RUNTIME_DLL}"
                "${BUNDLE_RUNTIME_DIR}/nvngx_dlssnr.dll"
        COMMAND "${CMAKE_COMMAND}"
                -DPLUGIN_FILE=${BUNDLE_BINARY_DIR}/ResolveDlss5.ofx
                -DRUNTIME_DLL=${BUNDLE_RUNTIME_DIR}/nvngx_dlssnr.dll
                -DOUTPUT_FILE=${BUNDLE_BINARY_DIR}/manifest.sha256
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateManifest.cmake"
        VERBATIM
    )
else()
    message(WARNING
        "DLSSNR runtime was not found and will not be packaged. "
        "Set DLSSNR_RUNTIME_DLL only when producing an authorized local/release bundle.")
endif()

if(RESOLVE_DLSS5_BUILD_SMOKE_TEST)
    enable_testing()
    add_executable(ResolveDlss5RuntimeSmoke
        ColorPipeline.cpp
        GpuResourcePool.cpp
        ../tests/RuntimeSmoke.cpp
        Feature18Runtime.cpp
        Feature18Runtime.h
        Feature18Parameters.h
    )
    target_include_directories(ResolveDlss5RuntimeSmoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${DLSS_SDK_ROOT}/include"
    )
    target_compile_definitions(ResolveDlss5RuntimeSmoke PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    if(MSVC)
        target_compile_options(
            ResolveDlss5RuntimeSmoke PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    target_link_libraries(ResolveDlss5RuntimeSmoke PRIVATE
        d3d12
        dxgi
        version
        "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib"
    )
    set_target_properties(ResolveDlss5RuntimeSmoke PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    if(EXISTS "${DLSSNR_RUNTIME_DLL}")
        add_custom_command(TARGET ResolveDlss5RuntimeSmoke POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "$<TARGET_FILE_DIR:ResolveDlss5RuntimeSmoke>/runtime"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DLSSNR_RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:ResolveDlss5RuntimeSmoke>/runtime/nvngx_dlssnr.dll"
            VERBATIM
        )
    endif()
    add_test(
        NAME ResolveDlss5.Feature18RuntimeSmoke
        COMMAND ResolveDlss5RuntimeSmoke
    )
endif()

enable_testing()
add_executable(ResolveDlss5CoreTests ../tests/CoreTests.cpp ../tests/ParallelColorTests.cpp ColorPipeline.cpp TimelineProcessor.cpp)
target_link_libraries(ResolveDlss5CoreTests PRIVATE bcrypt)
target_include_directories(ResolveDlss5CoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ResolveDlss5CoreTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(ResolveDlss5CoreTests PRIVATE /W4 /permissive- /utf-8)
endif()
add_test(NAME ResolveDlss5.Core COMMAND ResolveDlss5CoreTests)

add_executable(ResolveDlss5TimelineSmoke ../tests/TimelineSmoke.cpp ColorPipeline.cpp TimelineProcessor.cpp Feature18Runtime.cpp GpuResourcePool.cpp)
target_include_directories(ResolveDlss5TimelineSmoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${DLSS_SDK_ROOT}/include")
target_compile_definitions(ResolveDlss5TimelineSmoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(ResolveDlss5TimelineSmoke PRIVATE d3d12 dxgi version bcrypt "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
set_target_properties(ResolveDlss5TimelineSmoke PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(EXISTS "${DLSSNR_RUNTIME_DLL}")
    add_custom_command(TARGET ResolveDlss5TimelineSmoke POST_BUILD COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:ResolveDlss5TimelineSmoke>/runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DLSSNR_RUNTIME_DLL}" "$<TARGET_FILE_DIR:ResolveDlss5TimelineSmoke>/runtime/nvngx_dlssnr.dll" VERBATIM)
endif()
add_test(NAME ResolveDlss5.TimelineRuntimeSmoke COMMAND ResolveDlss5TimelineSmoke)

add_executable(ResolveDlss5GpuContracts ../tests/GpuContracts.cpp ColorPipeline.cpp Feature18Runtime.cpp GpuResourcePool.cpp)
target_include_directories(ResolveDlss5GpuContracts PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${DLSS_SDK_ROOT}/include")
target_compile_definitions(ResolveDlss5GpuContracts PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(ResolveDlss5GpuContracts PRIVATE d3d12 dxgi version "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
set_target_properties(ResolveDlss5GpuContracts PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(EXISTS "${DLSSNR_RUNTIME_DLL}")
    add_custom_command(TARGET ResolveDlss5GpuContracts POST_BUILD COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:ResolveDlss5GpuContracts>/runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DLSSNR_RUNTIME_DLL}" "$<TARGET_FILE_DIR:ResolveDlss5GpuContracts>/runtime/nvngx_dlssnr.dll" VERBATIM)
endif()
add_test(NAME ResolveDlss5.GpuContracts COMMAND ResolveDlss5GpuContracts)
add_executable(ResolveDlss5OpticalFlowSmoke ../tests/OpticalFlowSmoke.cpp TimelineProcessor.cpp Feature18Runtime.cpp GpuResourcePool.cpp)
target_include_directories(ResolveDlss5OpticalFlowSmoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${DLSS_SDK_ROOT}/include")
target_compile_definitions(ResolveDlss5OpticalFlowSmoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(ResolveDlss5OpticalFlowSmoke PRIVATE /utf-8 /EHsc)
target_link_libraries(ResolveDlss5OpticalFlowSmoke PRIVATE ResolveOpticalFlow version bcrypt "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
set_target_properties(ResolveDlss5OpticalFlowSmoke PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME ResolveDlss5.OpticalFlow COMMAND ResolveDlss5OpticalFlowSmoke)
set_tests_properties(ResolveDlss5.OpticalFlow PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
set_tests_properties(ResolveDlss5.TimelineRuntimeSmoke ResolveDlss5.GpuContracts PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
add_executable(ResolveDlss5PerformanceProbe ../tests/PerformanceProbe.cpp ColorPipeline.cpp Feature18Runtime.cpp GpuResourcePool.cpp)
target_include_directories(ResolveDlss5PerformanceProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${DLSS_SDK_ROOT}/include")
target_compile_definitions(ResolveDlss5PerformanceProbe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(ResolveDlss5PerformanceProbe PRIVATE /utf-8 /EHsc)
target_link_libraries(ResolveDlss5PerformanceProbe PRIVATE d3d12 dxgi version "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
set_target_properties(ResolveDlss5PerformanceProbe PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Explicit diagnostic CLI, deliberately excluded from CTest and distribution.
if(RESOLVE_DLSS5_BUILD_SMOKE_TEST)
    set_tests_properties(ResolveDlss5.Feature18RuntimeSmoke PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
endif()
