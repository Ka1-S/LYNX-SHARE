#pragma once

// The slider panel of a JSFX script (issue #241, round 190pj; owner decision 4, recommended option): LYNX's generic
// plugin panel look — one row per slider the script shows, a bar for a number, a menu for an enum or a file slider —
// with the script's name, author and id above it. Sliders a script hides ('-' names, slider_show) are left out (they
// stay automatable). A script with an @gfx display shows this panel too until LYNX draws @gfx (stage 3).

#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
// The editor of a JSFX instance (engine/JsfxFormat.h); nullptr for another plugin
juce::AudioProcessorEditor* makeJsfxPanel (juce::AudioProcessor& jsfxInstance);
} // namespace lynx
