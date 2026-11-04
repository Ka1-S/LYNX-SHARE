#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// FL Studio's Plugin picker (View › Plugin picker, F8; round 169k, core/PickerView.h): a veil over the main window with
// every generator and effect of the Plugin database as a tile, the categories as tabs along the bottom (generators,
// "All", effects); the mouse wheel steps through the tabs, pointing at a tab shows it. Clicking a tile adds it — a
// generator as a new channel, an effect to the selected mixer insert — and closes the picker; Escape, F8 or a click
// outside the tiles closes it. LYNX draws its own tiles (name, maker, format) instead of FL's plugin screenshots.
struct PluginPickerTargets
{
    std::function<void (ChannelDef)> addInstrument;
    std::function<void (EffectSlot)> addEffect;
    std::function<void()> close; // the owner removes the view
};

std::unique_ptr<juce::Component> makePluginPickerView (AudioEngine& engine, PluginPickerTargets targets);
} // namespace lynx
