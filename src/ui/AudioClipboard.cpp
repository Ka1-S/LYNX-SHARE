#include "ui/AudioClipboard.h"

#include "platform/Paths.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace lynx::audioclipboard
{
audioedit::Audio& editor()
{
    static audioedit::Audio audio;
    return audio;
}

namespace
{
#if JUCE_WINDOWS
std::vector<std::uint8_t> systemWave()
{
    std::vector<std::uint8_t> out;
    if (IsClipboardFormatAvailable (CF_WAVE) && OpenClipboard (nullptr))
    {
        if (auto memory = GetClipboardData (CF_WAVE))
            if (const auto* p = static_cast<const std::uint8_t*> (GlobalLock (memory)))
            {
                out.assign (p, p + GlobalSize (memory));
                GlobalUnlock (memory);
            }
        CloseClipboard();
    }
    return out;
}
#endif
} // namespace

std::vector<std::uint8_t> wav()
{
#if JUCE_WINDOWS
    if (auto system = systemWave(); ! system.empty())
        return system;
#endif
    if (editor().empty())
        return {};
    audioedit::Document doc;
    doc.audio = editor();
    return audioedit::encodeWav (doc, audioedit::WavFormat::float32);
}

bool hasAudio()
{
#if JUCE_WINDOWS
    if (IsClipboardFormatAvailable (CF_WAVE))
        return true;
#endif
    return ! editor().empty();
}

juce::File folder()
{
    return platform::settingsFile ("Presets").getChildFile ("Clipboard files");
}

juce::File save (const juce::String& baseName, const juce::String& extension, const void* data, std::size_t size)
{
    const auto dir = folder();
    if (! dir.createDirectory().wasOk() || data == nullptr || size == 0)
        return {};
    const auto file = dir.getNonexistentChildFile (baseName, extension, false);
    return file.replaceWithData (data, size) ? file : juce::File();
}
} // namespace lynx::audioclipboard
