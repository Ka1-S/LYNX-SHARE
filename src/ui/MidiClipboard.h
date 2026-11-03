#pragma once

#include <optional>

#include <juce_core/juce_core.h>

namespace lynx::midiclipboard
{
// The Piano roll's "Copy to MIDI clipboard" / "Paste from MIDI clipboard" (round 168g): FL puts a Standard MIDI File
// on the Windows clipboard under the registered format "Standard MIDI File" (FUN_00988b90 RegisterClipboardFormatW,
// FUN_00670260 SetClipboardData), so other programs that use that format exchange MIDI with it. On Windows LYNX does
// the same; on Linux and macOS (no such system format) the MIDI clipboard lives inside LYNX.
bool put (const juce::MemoryBlock& smf);
std::optional<juce::MemoryBlock> get();
bool hasMidi();
} // namespace lynx::midiclipboard
