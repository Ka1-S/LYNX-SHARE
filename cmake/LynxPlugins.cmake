# LYNX's own plugins as VST3 / CLAP (round 180b, docs/PLUGIN_EXPORT.md). Included from CMakeLists.txt only with
# -DLYNX_BUILD_PLUGINS=ON. One static library holds JUCE and the app's sources (compiled once); each VST3 adds JUCE's wrapper
# for its catalog entry, and one CLAP module holds every entry.

add_library(lynx_plugin_runtime STATIC src/plugin/ExportProcessor.cpp ${LYNX_APP_SOURCES})
_juce_initialise_target(lynx_plugin_runtime)
set(LYNX_PLUGIN_DEFINES
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_STANDALONE_APPLICATION=0 JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_APPLICATION_NAME_STRING="LYNX" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    LYNX_EXPORT_VERSION="${PROJECT_VERSION}")
foreach(m juce_core juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra juce_audio_basics
          juce_audio_devices juce_audio_formats juce_audio_processors juce_audio_utils juce_dsp juce_opengl)
    list(APPEND LYNX_PLUGIN_DEFINES JUCE_MODULE_AVAILABLE_${m}=1)
endforeach()
if(WIN32)
    list(APPEND LYNX_PLUGIN_DEFINES JUCE_ASIO=1)
    target_include_directories(lynx_plugin_runtime PRIVATE third_party/asiosdk/common)
    target_link_libraries(lynx_plugin_runtime PRIVATE winhttp)
elseif(APPLE)
    list(APPEND LYNX_PLUGIN_DEFINES JUCE_PLUGINHOST_AU=1)
else()
    list(APPEND LYNX_PLUGIN_DEFINES JUCE_ALSA=1 JUCE_JACK=1 JUCE_PLUGINHOST_LV2=1)
    target_include_directories(lynx_plugin_runtime SYSTEM PRIVATE third_party/pipewire/include/pipewire-0.3 third_party/pipewire/include/spa-0.2)
    target_link_libraries(lynx_plugin_runtime PRIVATE ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(lynx_plugin_runtime PUBLIC ${LYNX_PLUGIN_DEFINES})
target_include_directories(lynx_plugin_runtime
    PUBLIC src third_party/clap/include "${juce_SOURCE_DIR}/modules" "${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
    PRIVATE third_party/onnxruntime ${CMAKE_CURRENT_BINARY_DIR}/lynx_build)
target_link_libraries(lynx_plugin_runtime
    PUBLIC lynx_core
    PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_opengl)
set_target_properties(lynx_plugin_runtime PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# the host's symbols are never taken for ours, and only the entry points are visible
function(lynx_finish_plugin target)
    set_target_properties(${target} PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_link_libraries(${target} PRIVATE lynx_plugin_runtime)
    if(UNIX AND NOT APPLE)
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,-z,defs)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    endif()
endfunction()

if(WIN32)
    set(LYNX_VST3_ARCH x86_64-win)
elseif(APPLE)
    set(LYNX_VST3_ARCH MacOS)
else()
    set(LYNX_VST3_ARCH x86_64-linux)
endif()

set(LYNX_PLUGIN_OUT "${CMAKE_BINARY_DIR}/plugins")
function(lynx_add_vst3 index name)
    set(target "lynx_vst3_${index}")
    add_library(${target} MODULE src/plugin/Vst3Entry.cpp)
    target_compile_definitions(${target} PRIVATE LYNX_EXPORT_INDEX=${index})
    lynx_finish_plugin(${target})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${name}"
        LIBRARY_OUTPUT_DIRECTORY "${LYNX_PLUGIN_OUT}/${name}.vst3/Contents/${LYNX_VST3_ARCH}")
    if(WIN32)
        set_target_properties(${target} PROPERTIES SUFFIX ".vst3")
    endif()
endfunction()

lynx_add_vst3(0 "LYNX Facet EQ")
lynx_add_vst3(1 "LYNX FM10")

add_library(lynx_clap MODULE src/plugin/ClapEntry.cpp)
lynx_finish_plugin(lynx_clap)
set_target_properties(lynx_clap PROPERTIES OUTPUT_NAME "LYNX Plugins" SUFFIX ".clap" LIBRARY_OUTPUT_DIRECTORY "${LYNX_PLUGIN_OUT}")

# the check: loads the built plugins as a host does (docs/PLUGIN_EXPORT.md)
add_executable(plugexporttest tools/plugexporttest.cpp)
target_link_libraries(plugexporttest PRIVATE lynx_plugin_runtime)
