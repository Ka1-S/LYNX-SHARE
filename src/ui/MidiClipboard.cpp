#include "ui/MidiClipboard.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace lynx::midiclipboard
{
namespace
{
juce::MemoryBlock& local()
{
    static juce::MemoryBlock block; // (Linux / macOS, and a fallback when the system clipboard is busy)
    return block;
}

#if JUCE_WINDOWS
UINT format()
{
    static const UINT f = RegisterClipboardFormatW (L"Standard MIDI File");
    return f;
}
#endif
} // namespace

bool put (const juce::MemoryBlock& smf)
{
    local() = smf;
#if JUCE_WINDOWS
    if (format() == 0 || smf.getSize() == 0 || ! OpenClipboard (nullptr))
        return false;
    auto ok = false;
    EmptyClipboard();
    if (auto memory = GlobalAlloc (GMEM_MOVEABLE, smf.getSize()))
    {
        if (auto* p = GlobalLock (memory))
        {
            std::memcpy (p, smf.getData(), smf.getSize());
            GlobalUnlock (memory);
            ok = SetClipboardData (format(), memory) != nullptr;
        }
        if (! ok)
            GlobalFree (memory);
    }
    CloseClipboard();
    return ok;
#else
    return true;
#endif
}

std::optional<juce::MemoryBlock> get()
{
#if JUCE_WINDOWS
    if (format() != 0 && IsClipboardFormatAvailable (format()) && OpenClipboard (nullptr))
    {
        std::optional<juce::MemoryBlock> out;
        if (auto memory = GetClipboardData (format()))
            if (const auto* p = GlobalLock (memory))
            {
                out = juce::MemoryBlock (p, GlobalSize (memory));
                GlobalUnlock (memory);
            }
        CloseClipboard();
        if (out && out->getSize() > 0)
            return out;
    }
    return std::nullopt; // (the system clipboard is the MIDI clipboard: another program may have replaced it)
#else
    if (local().getSize() == 0)
        return std::nullopt;
    return local();
#endif
}

bool hasMidi()
{
#if JUCE_WINDOWS
    return format() != 0 && IsClipboardFormatAvailable (format());
#else
    return local().getSize() > 0;
#endif
}
} // namespace lynx::midiclipboard
