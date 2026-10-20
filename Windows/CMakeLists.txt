cmake_minimum_required(VERSION 3.24)

project(CleanWaveWindows VERSION 0.2.27 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CLEANWAVE_CPU_RUNTIME "Baseline" CACHE STRING "TFLite CPU code generation: Baseline, Avx, Avx2")
set_property(CACHE CLEANWAVE_CPU_RUNTIME PROPERTY STRINGS Baseline Avx Avx2)

set(CLEANWAVE_TENSORFLOW_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/src/tensorflow-2.16.1"
    CACHE PATH "Pinned TensorFlow 2.16.1 source tree")
set(CLEANWAVE_KISSFFT_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/src/kissfft-131.1.0"
    CACHE PATH "Pinned KissFFT 131.1.0 source tree")
set(CLEANWAVE_FXDIV_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/src/FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8"
    CACHE PATH "Pinned FXdiv source required by TensorFlow's pthreadpool dependency")

if(NOT EXISTS "${CLEANWAVE_TENSORFLOW_SOURCE}/tensorflow/lite/CMakeLists.txt")
    message(FATAL_ERROR "TensorFlow Lite 2.16.1 source is missing. See deps/DEPENDENCIES.lock.md")
endif()
if(NOT EXISTS "${CLEANWAVE_KISSFFT_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "KissFFT 131.1.0 source is missing. See deps/DEPENDENCIES.lock.md")
endif()
if(NOT EXISTS "${CLEANWAVE_FXDIV_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Pinned FXdiv source is missing. See deps/DEPENDENCIES.lock.md")
endif()

# TensorFlow 2.16.1 can rewrite its pinned git-origin FetchContent declarations
# to archive URLs. This host intentionally does not require a system Git install.
set(OVERRIDABLE_FETCH_CONTENT_GIT_REPOSITORY_AND_TAG_TO_URL ON CACHE BOOL "" FORCE)
set(FXDIV_SOURCE_DIR "${CLEANWAVE_FXDIV_SOURCE}" CACHE PATH "" FORCE)

# Conservative CPU baseline: no acceleration delegates. Step 3 enables RUY
# because TensorFlow Lite 2.16.1 forces that CPU kernel backend on Android,
# making this the closest practical Windows build to the current arm64 app.
set(TFLITE_ENABLE_XNNPACK OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_EXTERNAL_DELEGATE OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_GPU OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_RUY ON CACHE BOOL "" FORCE)
set(TFLITE_KERNEL_TEST OFF CACHE BOOL "" FORCE)

# Exact-size mixed-radix FFT dependency. Step 2 only proves a 1536-point plan
# can be created; streaming analysis/synthesis belongs to Step 4.
set(KISSFFT_DATATYPE float CACHE STRING "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_OPENMP OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_USE_ALLOCA OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CLEANWAVE_KISSFFT_SOURCE}"
    "${CMAKE_BINARY_DIR}/third_party/kissfft"
    EXCLUDE_FROM_ALL)

add_subdirectory(
    "${CLEANWAVE_TENSORFLOW_SOURCE}/tensorflow/lite"
    "${CMAKE_BINARY_DIR}/third_party/tensorflow-lite"
    EXCLUDE_FROM_ALL)

add_executable(cleanwave_bootstrap
    src/bootstrap/main.cpp)

target_link_libraries(cleanwave_bootstrap PRIVATE
    tensorflow-lite
    kissfft::kissfft)

if(MSVC)
    target_compile_options(cleanwave_bootstrap PRIVATE /W4 /permissive-)
endif()

set_target_properties(cleanwave_bootstrap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(cleanwave_model STATIC
    src/model/SeparationModel.cpp)

target_include_directories(cleanwave_model PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(cleanwave_model PUBLIC
    tensorflow-lite)
if(NOT CLEANWAVE_CPU_RUNTIME STREQUAL "Baseline")
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The vector runtime options require MSVC x64")
    endif()
    # Preserve one-thread/no-delegate kernels and precise floating-point mode.
    if(CLEANWAVE_CPU_RUNTIME STREQUAL "Avx")
        target_compile_options(tensorflow-lite PRIVATE /arch:AVX /MP4)
        target_compile_definitions(cleanwave_model PUBLIC CLEANWAVE_AVX_RUNTIME=1)
    elseif(CLEANWAVE_CPU_RUNTIME STREQUAL "Avx2")
        target_compile_options(tensorflow-lite PRIVATE /arch:AVX2 /MP4)
        target_compile_definitions(cleanwave_model PUBLIC CLEANWAVE_AVX2_RUNTIME=1)
    else()
        message(FATAL_ERROR "Unknown CLEANWAVE_CPU_RUNTIME")
    endif()
endif()

add_executable(cleanwave_model_verify
    src/model_verify/main.cpp)

target_link_libraries(cleanwave_model_verify PRIVATE
    cleanwave_model
    tensorflow-lite)

if(MSVC)
    target_compile_options(cleanwave_model PRIVATE /W4 /permissive-)
    target_compile_options(cleanwave_model_verify PRIVATE /W4 /permissive-)
endif()

set_target_properties(cleanwave_model_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(cleanwave_dsp STATIC
    src/dsp/StreamingDsp.cpp
    src/dsp/AudioLevel.cpp
    src/dsp/OfflineCleaner.cpp
    src/offline/WavIO.cpp
    src/offline/OfflinePipeline.cpp)

target_include_directories(cleanwave_dsp PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(cleanwave_dsp PUBLIC
    cleanwave_model
    kissfft::kissfft)

add_executable(cleanwave_offline
    src/offline/main.cpp)

target_link_libraries(cleanwave_offline PRIVATE
    cleanwave_dsp)

add_executable(cleanwave_dsp_verify
    src/dsp_verify/main.cpp)

target_link_libraries(cleanwave_dsp_verify PRIVATE
    cleanwave_dsp
    kissfft::kissfft)

if(MSVC)
    target_compile_options(cleanwave_dsp PRIVATE /W4 /permissive-)
    target_compile_options(cleanwave_offline PRIVATE /W4 /permissive-)
    target_compile_options(cleanwave_dsp_verify PRIVATE /W4 /permissive-)
endif()

set_target_properties(cleanwave_offline cleanwave_dsp_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(cleanwave_realtime STATIC
    src/realtime/RecoveryController.cpp
    src/realtime/ProcessingThreadTask.cpp
    src/realtime/RealtimeCleaner.cpp
    src/realtime/BufferedAudioPipeline.cpp)
target_link_libraries(cleanwave_realtime PUBLIC cleanwave_dsp)
target_link_libraries(cleanwave_realtime PUBLIC avrt)
target_include_directories(cleanwave_realtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(cleanwave_realtime_verify src/realtime_verify/main.cpp)
target_link_libraries(cleanwave_realtime_verify PRIVATE cleanwave_realtime)
if(MSVC)
    target_compile_options(cleanwave_realtime PRIVATE /W4 /permissive-)
    target_compile_options(cleanwave_realtime_verify PRIVATE /W4 /permissive-)
endif()
set_target_properties(cleanwave_realtime_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(cleanwave_audio STATIC src/audio/AudioEndpoints.cpp src/audio/WasapiAdapters.cpp)
target_include_directories(cleanwave_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cleanwave_audio PUBLIC ole32 uuid propsys avrt)
add_executable(cleanwave_audio_probe src/audio_probe/main.cpp)
target_link_libraries(cleanwave_audio_probe PRIVATE cleanwave_audio)
if(MSVC)
    target_compile_options(cleanwave_audio PRIVATE /W4 /permissive-)
    target_compile_options(cleanwave_audio_probe PRIVATE /W4 /permissive-)
endif()
set_target_properties(cleanwave_audio_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(cleanwave_audio_verify src/audio_verify/main.cpp)
target_link_libraries(cleanwave_audio_verify PRIVATE cleanwave_audio cleanwave_realtime)
if(MSVC)
    target_compile_options(cleanwave_audio_verify PRIVATE /W4 /permissive-)
endif()
set_target_properties(cleanwave_audio_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(cleanwave_live_core STATIC src/live/LiveSession.cpp)
target_link_libraries(cleanwave_live_core PUBLIC cleanwave_audio cleanwave_realtime)
add_executable(cleanwave_live src/live/main.cpp)
target_link_libraries(cleanwave_live PRIVATE cleanwave_live_core)
add_executable(cleanwave_live_source src/live_source/main.cpp)
target_link_libraries(cleanwave_live_source PRIVATE cleanwave_audio)
add_executable(cleanwave_live_verify src/live_verify/main.cpp)
target_link_libraries(cleanwave_live_verify PRIVATE cleanwave_live_core)
foreach(target cleanwave_live_core cleanwave_live cleanwave_live_source cleanwave_live_verify)
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  endif()
endforeach()
set_target_properties(cleanwave_live cleanwave_live_source cleanwave_live_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_target(cleanwave_step7 DEPENDS cleanwave_live cleanwave_live_source cleanwave_live_verify cleanwave_realtime_verify cleanwave_audio_verify)

add_library(cleanwave_desktop_core STATIC src/desktop/DesktopPreferences.cpp src/desktop/DesktopController.cpp)
target_link_libraries(cleanwave_desktop_core PUBLIC cleanwave_live_core)
target_compile_definitions(cleanwave_desktop_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_executable(CleanWave WIN32 src/desktop/main.cpp src/desktop/resources.rc)
target_link_libraries(CleanWave PRIVATE cleanwave_desktop_core comctl32 dwmapi shell32 uxtheme advapi32)
target_compile_definitions(CleanWave PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_options(CleanWave PRIVATE /MANIFEST:NO)
add_executable(cleanwave_desktop_verify src/desktop_verify/main.cpp)
target_link_libraries(cleanwave_desktop_verify PRIVATE cleanwave_desktop_core)
foreach(target cleanwave_desktop_core CleanWave cleanwave_desktop_verify)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  endif()
endforeach()
set_target_properties(CleanWave cleanwave_desktop_verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_target(cleanwave_step8 DEPENDS CleanWave cleanwave_desktop_verify)

# Separate WinUI 3 frontend; the existing Win32 preview remains available.
add_library(SukutEngine SHARED src/sukut/Bridge.cpp src/sukut/AudioRouting.cpp src/sukut/VolumeGuard.cpp)
target_link_libraries(SukutEngine PRIVATE cleanwave_desktop_core runtimeobject)
target_compile_definitions(SukutEngine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(SukutEngine PRIVATE /W4 /permissive- /utf-8)
endif()
set_target_properties(SukutEngine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(sukut_routing_verify src/sukut_routing_verify/main.cpp src/sukut/AudioRouting.cpp)
target_link_libraries(sukut_routing_verify PRIVATE cleanwave_desktop_core runtimeobject shell32)
target_compile_definitions(sukut_routing_verify PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(sukut_routing_verify PRIVATE /W4 /permissive- /utf-8)
endif()
set_target_properties(sukut_routing_verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(SukutVolumeGuard WIN32 src/sukut-volume-guard/main.cpp)
target_compile_definitions(SukutVolumeGuard PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(SukutVolumeGuard PRIVATE shell32)
set_target_properties(SukutVolumeGuard PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  target_compile_options(SukutVolumeGuard PRIVATE /W4 /permissive- /utf-8)
endif()

set(SUKUT_UI_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/sukut-0.2.27" CACHE PATH "Sukut WinUI application directory")
configure_file(src/sukut-launcher/AppPath.h.in "${CMAKE_BINARY_DIR}/sukut-launcher/AppPath.h" @ONLY)
add_executable(SukutDesktopLauncher WIN32 src/sukut-launcher/main.cpp src/sukut-launcher/resources.rc)
target_include_directories(SukutDesktopLauncher PRIVATE "${CMAKE_BINARY_DIR}/sukut-launcher")
target_compile_definitions(SukutDesktopLauncher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(SukutDesktopLauncher PRIVATE shell32 ole32 user32)
set_target_properties(SukutDesktopLauncher PROPERTIES OUTPUT_NAME "SukutDesktop" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(SukutPortableLauncher WIN32 src/sukut-launcher/main.cpp src/sukut-launcher/resources.rc)
target_compile_definitions(SukutPortableLauncher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE SUKUT_PORTABLE_LAUNCHER)
target_link_libraries(SukutPortableLauncher PRIVATE shell32 ole32 user32)
set_target_properties(SukutPortableLauncher PROPERTIES OUTPUT_NAME "SukutPortable" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(cleanwave_transport_verify src/transport_verify/main.cpp)
target_link_libraries(cleanwave_transport_verify PRIVATE cleanwave_live_core)
target_compile_definitions(cleanwave_transport_verify PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(cleanwave_transport_verify PRIVATE /W4 /permissive-)
endif()
set_target_properties(cleanwave_transport_verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
