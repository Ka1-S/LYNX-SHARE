#pragma once

// ONNX Runtime, shipped next to LYNX and loaded at run time by its full path (never a copy from the system's search path;
// the Windows build is cross-compiled with MinGW and can't link Microsoft's import library). Used by "Extract stems from
// sample" (ui/StemExtract) and "Convert to MIDI" (ui/AudioToMidi); nothing of it is in memory until one of them runs.

#include <juce_core/juce_core.h>
#include <onnxruntime_c_api.h>

namespace lynx::onnx
{
inline juce::File runtimeFile()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getSiblingFile ("Frameworks").getChildFile ("libonnxruntime.1.23.2.dylib");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("onnxruntime.dll");
   #else
    // /proc/self/exe: started by name from PATH, argv[0] (currentExecutableFile) would point at the current folder
    return juce::File::getSpecialLocation (juce::File::hostApplicationPath).getParentDirectory().getChildFile ("libonnxruntime.so.1.23.2");
   #endif
}

// The library and its C API. `missing` is what to say when the file isn't there ({0} = its name), `failed` when it
// doesn't load (both already translated: each feature names itself).
struct Runtime
{
    bool load (juce::String& error, const juce::String& missing, const juce::String& failed)
    {
        const auto path = runtimeFile();
        if (! path.existsAsFile())
        {
            error = missing.replace ("{0}", path.getFileName());
            return false;
        }
        const auto getBase = library.open (path.getFullPathName()) ? (const OrtApiBase* (ORT_API_CALL*) (void) noexcept) library.getFunction ("OrtGetApiBase") : nullptr;
        api = getBase != nullptr ? getBase()->GetApi (ORT_API_VERSION) : nullptr;
        if (api == nullptr)
        {
            error = failed;
            library.close();
            return false;
        }
        return true;
    }

    juce::DynamicLibrary library;
    const OrtApi* api = nullptr;
};
} // namespace lynx::onnx
