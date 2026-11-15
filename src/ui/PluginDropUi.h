#pragma once

// Issue #152 point 6: the words for a plugin drop the tracks refused (core/PluginDrop.h). FL shows them in an error
// box; the Clip-launch workflow in the status bar, as Live does.

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/PluginDrop.h"
#include "ui/I18n.h"

namespace lynx::plugindropui
{
inline juce::String message (plugindrop::Error error)
{
    using plugindrop::Error;
    switch (error)
    {
        case Error::tooMany: return tr ("Can't load more than 20 instruments at once.");
        case Error::noMixerTrack: return tr ("No available mixer track was found.");
        case Error::notInstrumentTrack: return tr ("This generator can't be used as an instrument track.");
        case Error::noEffectSlot: return tr ("There are no available effect slots on the mixer track.");
        case Error::onlyGenerators: return tr ("Only generators can be used as instrument tracks.");
        case Error::onlyAudioEffects: return tr ("Only audio effects can go on an audio track.");
        case Error::noInsert: return tr ("This track plays through no mixer track of its own: the device can't go there");
        case Error::noTrack:
        case Error::none: break;
    }
    return tr ("There is no track here to take the plugin.");
}

// FL's box title: "Can't create instrument/audio track" for a missing mixer track, "Error" otherwise
inline juce::String title (plugindrop::Error error)
{
    return error == plugindrop::Error::noMixerTrack ? tr ("Can't create instrument/audio track") : tr ("Error");
}
} // namespace lynx::plugindropui
