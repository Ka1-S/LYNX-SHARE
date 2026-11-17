#pragma once

// FL Edison's "Sample properties" window (Format › Edit properties..., F2), round gaps190kf: FL's form TMEWAVPropForm
// (Edison_x64.dll DFM) laid out at its own coordinates — Info (Title, Comments, Filename), Format (Samplerate (Hz) with
// its rate list and Resample, 16Bit int / 32Bit float, Stereo / Mono), Tempo (Tempo (BPM), Length (beats), Round, Limit,
// Tempo-sync, Autodetect with its three methods, Default) and Sampler (Middle note, low, high, Fine tune (cents), Round,
// Autodetect, Default, the keyboard), then Accept. The logic is core/SampleProps.

#include <functional>
#include <memory>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/SampleProps.h"

namespace lynx
{
struct SamplePropertiesHost
{
    double selectionFrames = 0.0;   // the selection's length (the whole sample without one)
    bool partSelected = false;      // FL then labels the length "Sel (beats)"
    double projectTempo = 120.0;    // Default tempo
    std::function<double()> detectPitch;             // MIDI semitones of the sample's pitch, < 0 = none
    std::function<double (int method)> detectTempo;  // method 1: constant tempo, 2: variable tempo; 0 = none found
    std::function<void (const sampleprops::Form&)> accept;
};

std::unique_ptr<juce::Component> makeSamplePropertiesPanel (const audioedit::Document& doc, const std::string& fileName, SamplePropertiesHost host);

// FL's form size (the client area under its caption)
constexpr int samplePropertiesWidth = 472, samplePropertiesHeight = 584;
} // namespace lynx
