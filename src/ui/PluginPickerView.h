#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// FL Studio's Plugin picker (View › Plugin picker, F8; rounds 169k, 171c and 175b, core/PickerView.h,
// docs/analise/REA_rodada171c.md §2, REA_rodada175b.md): a see-through veil over the whole screen with every generator
// and effect as a picture of its own editor (ui/PluginThumbs.h) at FL's scale, packed in rows like FL (big editors make
// big tiles), the categories as tabs along the bottom (generators, "All", effects). The wheel steps through the tabs, pointing at a tab shows it, clicking it opens the Browser there.
// A double-click, a right-click or Enter loads the plugin pointed at — a generator as a new channel, an effect into the
// selected mixer insert — and closes; dragging a tile drops it on a mixer insert / slot, the Channel Rack (a new
// channel, or onto a channel to replace it) or the Playlist. Typing searches (the others dim), Up / Down and Ctrl+wheel
// step through the tiles. Escape, F8, a right / middle click outside the tiles or another window closes it.
struct PluginPickerTargets
{
    std::function<void (ChannelDef)> addInstrument;
    std::function<void (EffectSlot)> addEffect;
    std::function<void()> close;                                   // the owner removes the view
    std::function<void()> showMixer;                               // an effect is being dragged (FL brings up the Mixer)
    std::function<void (const std::string&, bool)> openBrowser;    // a tab was clicked: its category ("" = All), generators?
    std::function<juce::Component*()> dragOwner;                   // inside the main window's drag container (the picker is its own window)
};

std::unique_ptr<juce::Component> makePluginPickerView (AudioEngine& engine, PluginPickerTargets targets);

// FL's picker is its own window over the whole screen (the main window's monitor, its work area) and see-through: the
// screen under it is taken once before it shows (`screenArea` in logical screen coordinates).
void takePluginPickerBackdrop (juce::Component& view, juce::Rectangle<int> screenArea);
} // namespace lynx
