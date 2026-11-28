// The first object of every LYNX plugin binary built by CMake (round 190sh, cmake/LynxPlugins.cmake): it asks the linker for
// the shared engine before the binary's own entry object (Vst3Entry.cpp, Lv2Entry.cpp or ClapEntry.cpp), which is linked last.
// So the engine is laid out the same way in the CLAP, every VST3 and every LV2, and the release archives' solid xz stream
// finds most of each plugin file in the ones before it. Nothing calls this: --gc-sections / -dead_strip drop it again.

namespace juce { class AudioProcessor; }
namespace lynx::plugexport { juce::AudioProcessor* makeProcessorAt (int catalogIndex); } // plugin/ExportProcessor.h

juce::AudioProcessor* (*lynxPluginLinkAnchor) (int) = &lynx::plugexport::makeProcessorAt;
