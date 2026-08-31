cmake_minimum_required(VERSION 3.25)

project(ResolveDlss5 VERSION 0.3.1 LANGUAGES CXX)

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
    src/Feature18Runtime.cpp
    src/Feature18Runtime.h
    src/ResolveDlss5Plugin.cpp
    src/ResolveDlss5Plugin.h
    src/Feature18Parameters.h
    ${OFX_SUPPORT_SOURCES}
)

target_include_directories(ResolveDlss5 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
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
    CUDA::cuda_driver
    d3d12
    dxgi
    version
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
        tests/RuntimeSmoke.cpp
        src/Feature18Runtime.cpp
        src/Feature18Runtime.h
        src/Feature18Parameters.h
    )
    target_include_directories(ResolveDlss5RuntimeSmoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
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
