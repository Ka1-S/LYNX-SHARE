#pragma once

// FL's "Tempo detection" dialog (round 190ey; live check 190ew PL-8, FL 2026 shot PL-8-detect-tempo-dialog.png): what
// the audio clip menu's "Detect tempo" and the Sampler waveform menu's open. One dialog: the estimate, then a button per
// choice (core/TempoDialog.h): the estimate, the four ranges, Type in (BPM), Type in (beats), the project's tempo, No
// tempo. A choice becomes the sample's tempo (`apply` with the beats the sample lasts at it; 0 = no tempo).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Instruments.h"

namespace lynx::ui
{
struct TempoDetectionRequest
{
    juce::String name; // the clip's (or channel's) name, in the title
    std::shared_ptr<const SampleData> sample;
    double projectBpm = 120.0;
    bool hasTempoInfo = false;             // the file brings its own tempo (ACID beats)
    std::function<void (double beats)> apply; // > 0: synchronise the sample to that many beats; 0: no tempo
};

void showTempoDetectionDialog (TempoDetectionRequest request);

// --flfixshot: the dialog's content (title, text, buttons) without a window; `pressButton` (the shot route) runs button
// `index` of it as a click would (the typed ones excepted) and returns its text
std::unique_ptr<juce::Component> makeTempoDetectionPanel (TempoDetectionRequest request);
juce::String pressTempoDetectionButton (juce::Component& panel, int index);
juce::String tempoDetectionTitle (const juce::String& name);
} // namespace lynx::ui
