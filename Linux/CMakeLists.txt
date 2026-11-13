cmake_minimum_required(VERSION 3.25)

# Sukut for Linux. Stage 1 (Linux/PLAN.md section 12): the engine without audio,
# built from the same sources as Windows so both platforms run identical code.
project(SukutLinux VERSION 0.5.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# The engine sources are shared with Windows (owner decision: no copy).
set(SUKUT_ENGINE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../Windows/src"
    CACHE PATH "Shared engine sources (dsp, model, realtime and their verifiers)")

# Pinned dependency sources, prepared offline by scripts/seed-deps.sh from the
# same archives as Windows (Windows/deps/DEPENDENCIES.lock.md).
set(SUKUT_DEPS_DIR "$ENV{HOME}/sukut-deps" CACHE PATH "Output of scripts/seed-deps.sh")
set(CLEANWAVE_TENSORFLOW_SOURCE "${SUKUT_DEPS_DIR}/src/tensorflow-2.16.1" CACHE PATH "")
set(CLEANWAVE_KISSFFT_SOURCE "${SUKUT_DEPS_DIR}/src/kissfft-131.1.0" CACHE PATH "")
set(CLEANWAVE_FXDIV_SOURCE "${SUKUT_DEPS_DIR}/src/FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8" CACHE PATH "")

foreach(required
        "${SUKUT_ENGINE_SOURCE}/model/AssetSeal.h"
        "${CLEANWAVE_TENSORFLOW_SOURCE}/tensorflow/lite/CMakeLists.txt"
        "${CLEANWAVE_KISSFFT_SOURCE}/CMakeLists.txt"
        "${CLEANWAVE_FXDIV_SOURCE}/CMakeLists.txt")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing ${required}. Run Linux/scripts/seed-deps.sh first.")
    endif()
endforeach()

# TFLite 2.16.1 puts each fetched dependency in ${CMAKE_BINARY_DIR}/<name>.
# Seed those folders from the pinned copies and never download during a build.
set(SUKUT_PREFETCHED abseil-cpp eigen farmhash fft2d flatbuffers gemmlowp ml_dtypes ruy cpuinfo neon2sse)
foreach(name IN LISTS SUKUT_PREFETCHED)
    if(NOT EXISTS "${SUKUT_DEPS_DIR}/prefetch/${name}")
        message(FATAL_ERROR "Missing prefetched ${name}. Run Linux/scripts/seed-deps.sh first.")
    endif()
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/${name}")
        file(COPY "${SUKUT_DEPS_DIR}/prefetch/${name}" DESTINATION "${CMAKE_BINARY_DIR}")
    endif()
endforeach()
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
set(PTHREADPOOL_SOURCE_DIR "${SUKUT_DEPS_DIR}/prefetch/pthreadpool-source" CACHE STRING "" FORCE)
set(OVERRIDABLE_FETCH_CONTENT_GIT_REPOSITORY_AND_TAG_TO_URL ON CACHE BOOL "" FORCE)
set(FXDIV_SOURCE_DIR "${CLEANWAVE_FXDIV_SOURCE}" CACHE PATH "" FORCE)

# Same TFLite options as Windows: XNNPACK off, RUY on, no delegates.
set(TFLITE_ENABLE_XNNPACK OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_EXTERNAL_DELEGATE OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_GPU OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_RUY ON CACHE BOOL "" FORCE)
set(TFLITE_KERNEL_TEST OFF CACHE BOOL "" FORCE)

set(KISSFFT_DATATYPE float CACHE STRING "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_OPENMP OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_USE_ALLOCA OFF CACHE BOOL "" FORCE)

# x86-64 baseline only (no -march=native, no SSE4.1): GCC then skips TFLite's
# NEON_2_SSE path exactly as MSVC does on Windows. No fused multiply-adds.
add_compile_options(-ffp-contract=off)
add_compile_definitions(TF_LITE_DISABLE_X86_NEON)

add_subdirectory("${CLEANWAVE_KISSFFT_SOURCE}" "${CMAKE_BINARY_DIR}/third_party/kissfft" EXCLUDE_FROM_ALL)
add_subdirectory("${CLEANWAVE_TENSORFLOW_SOURCE}/tensorflow/lite" "${CMAKE_BINARY_DIR}/third_party/tensorflow-lite" EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)
set(E "${SUKUT_ENGINE_SOURCE}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(cleanwave_model STATIC "${E}/model/SeparationModel.cpp")
target_include_directories(cleanwave_model PUBLIC "${E}")
target_link_libraries(cleanwave_model PUBLIC tensorflow-lite)

add_library(cleanwave_dsp STATIC
    "${E}/dsp/StreamingDsp.cpp"
    "${E}/dsp/AudioLevel.cpp"
    "${E}/dsp/OfflineCleaner.cpp"
    "${E}/offline/WavIO.cpp"
    "${E}/offline/OfflinePipeline.cpp")
target_include_directories(cleanwave_dsp PUBLIC "${E}")
target_link_libraries(cleanwave_dsp PUBLIC cleanwave_model kissfft::kissfft)

add_library(cleanwave_realtime STATIC
    "${E}/realtime/RecoveryController.cpp"
    "${E}/realtime/ProcessingThreadTask.cpp"
    "${E}/realtime/RealtimeCleaner.cpp"
    "${E}/realtime/BufferedAudioPipeline.cpp")
target_include_directories(cleanwave_realtime PUBLIC "${E}")
target_link_libraries(cleanwave_realtime PUBLIC cleanwave_dsp Threads::Threads)

foreach(lib cleanwave_model cleanwave_dsp cleanwave_realtime)
    target_compile_options(${lib} PRIVATE -Wall -Wextra)
endforeach()

# The verifiers and stage tools are for development; the Debian package leaves them out.
option(SUKUT_DEV_TOOLS "Build the verifiers and stage test tools" ON)
if(SUKUT_DEV_TOOLS)
    add_executable(cleanwave_bootstrap "${E}/bootstrap/main.cpp")
    target_link_libraries(cleanwave_bootstrap PRIVATE tensorflow-lite kissfft::kissfft)
    add_executable(cleanwave_model_verify "${E}/model_verify/main.cpp")
    target_link_libraries(cleanwave_model_verify PRIVATE cleanwave_model tensorflow-lite)
    # cleanwave_offline (the WAV tool) parses UTF-16 arguments throughout; it is not
    # part of the stage 1 parity check and is not built on Linux yet.
    add_executable(cleanwave_dsp_verify "${E}/dsp_verify/main.cpp")
    target_link_libraries(cleanwave_dsp_verify PRIVATE cleanwave_dsp kissfft::kissfft)
    add_executable(cleanwave_realtime_verify "${E}/realtime_verify/main.cpp")
    target_link_libraries(cleanwave_realtime_verify PRIVATE cleanwave_realtime)
    add_executable(sukut_efficiency_verify "${E}/efficiency_verify/main.cpp")
    target_link_libraries(sukut_efficiency_verify PRIVATE cleanwave_realtime)
endif()

# Stage 2: the PipeWire audio path (Linux/PLAN.md section 5).
option(SUKUT_AUDIO "Build the PipeWire audio layer" ON)
if(SUKUT_AUDIO)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3>=1.0)
    add_library(sukut_audio STATIC src/audio/PipeWireBridge.cpp src/audio/RoutingJournal.cpp)
    target_include_directories(sukut_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(sukut_audio PUBLIC PkgConfig::PIPEWIRE Threads::Threads)
    target_compile_options(sukut_audio PRIVATE -Wall -Wextra)
    # Stage 3: the shared engine between PipeWire capture and playback.
    add_library(sukut_session STATIC src/audio/PipeWireAdapters.cpp src/audio/SukutSession.cpp)
    target_link_libraries(sukut_session PUBLIC sukut_audio cleanwave_realtime)
    target_compile_options(sukut_session PRIVATE -Wall -Wextra)
    if(SUKUT_DEV_TOOLS)
        add_executable(sukut_pw_passthrough src/tools/PwPassthrough.cpp)
        target_link_libraries(sukut_pw_passthrough PRIVATE sukut_audio)
        add_executable(sukut_pw_filter src/tools/PwFilter.cpp)
        target_link_libraries(sukut_pw_filter PRIVATE sukut_session)
        add_executable(sukut_offline_replay src/tools/OfflineReplay.cpp)
        target_link_libraries(sukut_offline_replay PRIVATE sukut_session)
    endif()

    # Stage 5: the host (Linux/PLAN.md section 6): engine owner, tray, window socket.
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0>=2.80 gio-unix-2.0>=2.80)
    add_executable(sukut-host
        src/host/HostMain.cpp src/host/HostEngine.cpp src/host/HostServer.cpp
        src/host/HostSettings.cpp src/host/Tray.cpp src/host/Desktop.cpp)
    target_link_libraries(sukut-host PRIVATE sukut_session PkgConfig::GIO)
    target_compile_options(sukut-host PRIVATE -Wall -Wextra)
endif()

# Stage 6: the window (Linux/PLAN.md section 7), GTK 4 + libadwaita.
option(SUKUT_WINDOW "Build the GTK window" ON)
if(SUKUT_WINDOW)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ADW REQUIRED IMPORTED_TARGET libadwaita-1>=1.7 gtk4>=4.18 gio-unix-2.0 fontconfig)
    pkg_check_modules(LAYERSHELL QUIET IMPORTED_TARGET gtk4-layer-shell-0>=1.2)
    add_executable(sukut
        src/window/Main.cpp src/window/HostLink.cpp src/window/Style.cpp src/window/Widgets.cpp
        src/window/Window.cpp src/window/Setup.cpp src/window/Capture.cpp
        src/window/PhoneTools.cpp src/window/PhonePage.cpp src/window/AdvancedMix.cpp src/window/TrayPopup.cpp
        src/window/AppIcon.cpp)
    target_include_directories(sukut PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(sukut PRIVATE PkgConfig::ADW)
    if(LAYERSHELL_FOUND)
        # Layer shell must precede Wayland in the link order for its interposition.
        target_link_libraries(sukut PRIVATE PkgConfig::LAYERSHELL)
        target_compile_definitions(sukut PRIVATE SUKUT_HAS_LAYER_SHELL=1)
        if(TARGET sukut-host)
            target_compile_definitions(sukut-host PRIVATE SUKUT_TRAY_FLYOUT=1)
        endif()
    endif()
    set(SUKUT_VERSION_LABEL "${PROJECT_VERSION}" CACHE STRING "The version shown in About (the package sets its own)")
    target_compile_definitions(sukut PRIVATE SUKUT_VERSION="${SUKUT_VERSION_LABEL}")
    target_compile_options(sukut PRIVATE -Wall -Wextra)

    # A development build keeps its data where the programs look first:
    # <build>/share/sukut beside <build>/bin (fonts, icons, the sealed engine).
    set(SUKUT_STAGE "${CMAKE_BINARY_DIR}/share/sukut")
    file(GLOB SUKUT_FONTS "${SUKUT_ENGINE_SOURCE}/sukut-ui/Assets/*.ttf")
    file(COPY ${SUKUT_FONTS} DESTINATION "${SUKUT_STAGE}/fonts")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/hicolor" DESTINATION "${SUKUT_STAGE}/icons")
    # The marks the window draws the chosen app icon from.
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/marks" DESTINATION "${SUKUT_STAGE}/icons")
    file(GLOB SUKUT_ENGINE_FILES "${SUKUT_ENGINE_SOURCE}/../assets/engine/*")
    file(COPY ${SUKUT_ENGINE_FILES} DESTINATION "${SUKUT_STAGE}/engine")
    # Stage 7: the same signed APK as the Windows build, with its checks. The
    # activation script is Android 0.4.2's (app/src/main/assets), which also
    # retires a stale worker left by a reinstall; data/android holds it.
    file(GLOB SUKUT_ANDROID_FILES "${SUKUT_ENGINE_SOURCE}/sukut-ui/Assets/Phone/Sukut.apk*")
    list(APPEND SUKUT_ANDROID_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/android/activate-worker.sh"
         "${CMAKE_CURRENT_SOURCE_DIR}/data/android/activate-worker.sh.sha256")
    file(COPY ${SUKUT_ANDROID_FILES} DESTINATION "${SUKUT_STAGE}/android")
    # Only the two pictures the Phone page shows (Windows also has a wireless one).
    set(SUKUT_PHONE_IMAGES "${SUKUT_ENGINE_SOURCE}/sukut-ui/Assets/Setup/phone-developer-options.png"
        "${SUKUT_ENGINE_SOURCE}/sukut-ui/Assets/Setup/phone-usb-debugging.png")
    file(COPY ${SUKUT_PHONE_IMAGES} DESTINATION "${SUKUT_STAGE}/images")

    # Stage 8: what the Debian package installs (Linux/PLAN.md section 10), as
    # component "sukut" so the dependencies' own install rules stay out.
    include(GNUInstallDirs)
    set(SUKUT_SHARE "${CMAKE_INSTALL_DATADIR}/sukut")
    install(TARGETS sukut RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT sukut)
    install(TARGETS sukut-host RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/sukut" COMPONENT sukut)
    # The Phone page's Allow access: the root helper, its polkit action and a
    # udev rule for phones android-udev-rules doesn't list.
    install(PROGRAMS data/sukut-usb-access DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/sukut" COMPONENT sukut)
    install(FILES data/io.github.altarafiya.Sukut.policy DESTINATION "${CMAKE_INSTALL_DATADIR}/polkit-1/actions" COMPONENT sukut)
    install(FILES data/70-sukut-android.rules DESTINATION "lib/udev/rules.d" COMPONENT sukut)
    install(FILES ${SUKUT_ENGINE_FILES} DESTINATION "${SUKUT_SHARE}/engine" COMPONENT sukut)
    install(FILES ${SUKUT_FONTS} DESTINATION "${SUKUT_SHARE}/fonts" COMPONENT sukut)
    install(FILES ${SUKUT_ANDROID_FILES} DESTINATION "${SUKUT_SHARE}/android" COMPONENT sukut)
    install(FILES ${SUKUT_PHONE_IMAGES} DESTINATION "${SUKUT_SHARE}/images" COMPONENT sukut)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/hicolor" DESTINATION "${CMAKE_INSTALL_DATADIR}/icons" COMPONENT sukut)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/marks" DESTINATION "${SUKUT_SHARE}/icons" COMPONENT sukut)
    install(FILES data/io.github.altarafiya.Sukut.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications" COMPONENT sukut)
    install(FILES data/io.github.altarafiya.Sukut.metainfo.xml DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo" COMPONENT sukut)
    install(FILES data/sukut.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1" COMPONENT sukut)
    # The licences of everything linked into sukut-host, from the pinned sources.
    set(SUKUT_LICENCES "${CMAKE_INSTALL_DATADIR}/doc/sukut/licenses")
    foreach(entry
            "TensorFlow-Lite|${CLEANWAVE_TENSORFLOW_SOURCE}/LICENSE"
            "KissFFT|${CLEANWAVE_KISSFFT_SOURCE}/COPYING"
            "FXdiv|${CLEANWAVE_FXDIV_SOURCE}/LICENSE"
            "Abseil|${SUKUT_DEPS_DIR}/prefetch/abseil-cpp/LICENSE"
            "Eigen|${SUKUT_DEPS_DIR}/prefetch/eigen/COPYING.MPL2"
            "Eigen|${SUKUT_DEPS_DIR}/prefetch/eigen/COPYING.BSD"
            "Eigen|${SUKUT_DEPS_DIR}/prefetch/eigen/COPYING.README"
            "FlatBuffers|${SUKUT_DEPS_DIR}/prefetch/flatbuffers/LICENSE"
            "Gemmlowp|${SUKUT_DEPS_DIR}/prefetch/gemmlowp/LICENSE"
            "RUY|${SUKUT_DEPS_DIR}/prefetch/ruy/LICENSE"
            "FarmHash|${SUKUT_DEPS_DIR}/prefetch/farmhash/COPYING"
            "FFT2D|${SUKUT_DEPS_DIR}/prefetch/fft2d/readme.txt"
            "cpuinfo|${SUKUT_DEPS_DIR}/prefetch/cpuinfo/LICENSE"
            "ml_dtypes|${SUKUT_DEPS_DIR}/prefetch/ml_dtypes/LICENSE"
            "Pthreadpool|${SUKUT_DEPS_DIR}/prefetch/pthreadpool-source/LICENSE")
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 file)
        install(FILES "${file}" DESTINATION "${SUKUT_LICENCES}/${name}" COMPONENT sukut)
    endforeach()
endif()
