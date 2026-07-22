cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")

project(SurfaceLab VERSION 0.15.7 LANGUAGES C CXX)

# The After Effects plug-in is macOS-only and needs the licensed AE SDK. The
# scene-model unit tests are host-independent and build anywhere, so they are a
# separate, opt-in target that does not require the SDK.
option(SURFACELAB_BUILD_PLUGIN "Build the After Effects plug-in (macOS only)" ON)
option(SURFACELAB_BUILD_TESTS "Build the host-independent model unit tests" OFF)

if(SURFACELAB_BUILD_PLUGIN)

if(NOT APPLE)
    message(FATAL_ERROR "The current prototype build supports macOS only.")
endif()

set(AE_SDK_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/work/vendor/AfterEffectsSDK/ae25.6_61.64bit.AfterEffectsSDK"
    CACHE PATH "Path to the extracted After Effects SDK")

set(AE_EXAMPLES_ROOT "${AE_SDK_ROOT}/Examples")
if(NOT EXISTS "${AE_EXAMPLES_ROOT}/Headers/AE_Effect.h")
    message(FATAL_ERROR "After Effects SDK not found at ${AE_SDK_ROOT}")
endif()

add_library(SurfaceLab MODULE
    src/SurfaceLab.cpp
    src/SurfaceLabModel.cpp
    src/SurfaceLabGeometry.cpp
    src/SurfaceLab.h
    src/SurfaceLabModel.h
    src/SurfaceLabGeometry.h
    src/SurfaceLabInternal.h
    "${AE_EXAMPLES_ROOT}/Util/AEFX_SuiteHelper.c"
    "${AE_EXAMPLES_ROOT}/Util/AEGP_SuiteHandler.cpp"
    "${AE_EXAMPLES_ROOT}/Util/MissingSuiteError.cpp")

# Rez runs after the bundle is linked. Make the C++ object depend on the PiPL
# source so changing plug-in metadata always relinks and regenerates the resource.
set_property(SOURCE src/SurfaceLab.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/SurfaceLabPiPL.r")

target_compile_features(SurfaceLab PRIVATE cxx_std_17)
target_compile_definitions(SurfaceLab PRIVATE __MACH__=1)
target_compile_options(SurfaceLab PRIVATE
    -fvisibility=hidden
    -Wall
    -Wextra
    -Wpedantic)
target_include_directories(SurfaceLab PRIVATE
    "${AE_EXAMPLES_ROOT}/Headers"
    "${AE_EXAMPLES_ROOT}/Headers/SP"
    "${AE_EXAMPLES_ROOT}/Util"
    "${AE_EXAMPLES_ROOT}/Resources")
target_link_libraries(SurfaceLab PRIVATE "-framework Cocoa")

set_target_properties(SurfaceLab PROPERTIES
    PREFIX ""
    OUTPUT_NAME "SurfaceLab"
    BUNDLE TRUE
    BUNDLE_EXTENSION "plugin"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in")

find_program(REZ_EXECUTABLE Rez
    HINTS "/Applications/Xcode.app/Contents/Developer/usr/bin"
    REQUIRED)

execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE MACOS_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)

add_custom_command(TARGET SurfaceLab POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_BUNDLE_DIR:SurfaceLab>/Contents/Resources"
    COMMAND "${REZ_EXECUTABLE}"
        -o "$<TARGET_BUNDLE_DIR:SurfaceLab>/Contents/Resources/SurfaceLab.rsrc"
        -d SystemSevenOrLater=1
        -useDF
        -script Roman
        -define __MACH__
        -arch arm64
        -i "${AE_EXAMPLES_ROOT}/Headers"
        -i "${AE_EXAMPLES_ROOT}/Headers/SP"
        -i "${AE_EXAMPLES_ROOT}/Util"
        -i "${AE_EXAMPLES_ROOT}/Resources"
        -isysroot "${MACOS_SDK_PATH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/SurfaceLabPiPL.r"
    # Rez mutates the finished bundle after the linker has signed it. Apply the
    # final ad-hoc signature last so the installed development build validates.
    COMMAND /usr/bin/codesign --force --deep --sign -
        "$<TARGET_BUNDLE_DIR:SurfaceLab>"
    VERBATIM)

endif()  # SURFACELAB_BUILD_PLUGIN

if(SURFACELAB_BUILD_TESTS)
    enable_testing()
    add_executable(surfacelab_model_tests
        src/SurfaceLabModel.cpp
        src/SurfaceLabGeometry.cpp
        tests/model_tests.cpp)
    target_include_directories(surfacelab_model_tests PRIVATE src)
    target_compile_features(surfacelab_model_tests PRIVATE cxx_std_17)
    target_compile_options(surfacelab_model_tests PRIVATE -Wall -Wextra)
    add_test(NAME model_tests COMMAND surfacelab_model_tests)
endif()
