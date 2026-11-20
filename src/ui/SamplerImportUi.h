#pragma once

// Opening other samplers' instruments (.dwp / .dwb DirectWave programs and banks, .gig GigaStudio instruments, .nki
// Kontakt instruments: core/SamplerImport.h) from the Browser, a drop on the Channel Rack or a double click (round
// 190ii, issue #149). The file becomes a DirectWave channel; a .gig with several instruments asks which one; the
// import runs off the message thread (a library's samples are written out as WAV files) and a dialog reports what
// could not be brought over.

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// .dwp, .dwb, .gig, .nki
bool isSamplerInstrumentFile (const juce::File& file);
// Into DirectWave channel `channel` when it is one (its mixer track, levels and notes stay), else a new channel
void openSamplerInstrumentFile (AudioEngine& engine, const juce::File& file, int channel = -1);
} // namespace lynx
