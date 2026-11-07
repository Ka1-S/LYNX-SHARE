// One VST3 plugin: JUCE's wrapper around the catalog entry LYNX_EXPORT_INDEX (compiled once per plugin, round 180b)

// No LYNX or JUCE header before JUCE's wrapper below: it has to include juce_gui_basics itself (with the X11 headers on Linux)

#ifndef LYNX_EXPORT_INDEX
 #error "LYNX_EXPORT_INDEX: the plugin's index in plugin/ExportCatalog.h"
#endif

#undef JUCE_STANDALONE_APPLICATION
#define JUCE_STANDALONE_APPLICATION 0
#define JUCE_VST3_CAN_REPLACE_VST2 0
#define JucePlugin_Build_VST 0
#define JucePlugin_Build_VST3 1
#define JucePlugin_Build_AU 0
#define JucePlugin_Build_AUv3 0
#define JucePlugin_Build_AAX 0
#define JucePlugin_Build_Standalone 0
#define JucePlugin_Build_Unity 0
#define JucePlugin_Build_LV2 0
#define JucePlugin_Enable_ARA 0
#define JucePlugin_Manufacturer "Ka1-S"
#define JucePlugin_ManufacturerWebsite "https://lynx-share.com"
#define JucePlugin_ManufacturerEmail ""
#define JucePlugin_ManufacturerCode 0x4b613153 // 'Ka1S'
#define JucePlugin_Desc "LYNX SHARE plugin"
#define JucePlugin_EditorRequiresKeyboardFocus 0
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0
#define JucePlugin_VSTNumMidiInputs 16
#define JucePlugin_VSTNumMidiOutputs 16
#define JucePlugin_AAXIdentifier com.ka1-s.lynx
#define JucePlugin_CFBundleIdentifier com.ka1-s.lynx
#define JucePlugin_Vst3ComponentFlags 0
#define JucePlugin_VersionCode 0x00000100
#define JucePlugin_Version 0.1.0
#define JucePlugin_VersionString LYNX_EXPORT_VERSION
// (identity: the build passes it from plugin/plugins.def)
#define JucePlugin_VSTUniqueID JucePlugin_PluginCode
#define JucePlugin_WantsMidiInput JucePlugin_IsSynth

#include <juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp>

namespace lynx::plugexport { juce::AudioProcessor* makeProcessorAt (int catalogIndex); } // plugin/ExportProcessor.h

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return lynx::plugexport::makeProcessorAt (LYNX_EXPORT_INDEX);
}
