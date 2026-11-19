cmake_minimum_required(VERSION 3.22)

project(lynx_android C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffp-contract=off -O2 -fno-exceptions -fno-rtti -fvisibility=hidden")

# Force ARM64 (arm64-v8a) as the primary architecture
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    message(STATUS "Building LYNX for armeabi-v7a (32-bit)")
else()
    message(STATUS "Building LYNX for arm64-v8a (64-bit)")
endif()

# ---------- LYNX audio engine (no JUCE) ----------
# Pure C++20, no juce::AudioBuffer, no juce::dsp. Buffers are std::vector<float>.
set(LYNX_ENGINE_SOURCES
    audio_engine.cpp
    aaudio_output.cpp
    simple_synth.cpp
    mixer.cpp
    project_file.cpp
    importer_stub.cpp
    wav_export.cpp
    flm_parser.cpp
    flm_importer.cpp
)

add_library(lynx_engine STATIC ${LYNX_ENGINE_SOURCES})
target_include_directories(lynx_engine PUBLIC .)

# Android system libs: AAudio for low-latency audio, OpenSL ES as fallback,
# log for debugging, dlopen for dynamic AAudio loading on older devices.
target_link_libraries(lynx_engine PUBLIC
    android
    log
    OpenSLES
)

# ---------- JNI bridge (Kotlin <-> C++) ----------
add_library(lynx_jni SHARED
    jni_bridge.cpp)

target_link_libraries(lynx_jni PUBLIC lynx_engine)
