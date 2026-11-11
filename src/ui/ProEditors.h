#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Display-first editors of LYNX's dynamics and saturation engines (REA round 114). Each one follows the workflow of
// the plugin it is inspired by, the way Facet EQ does (round 103): the window is the display — scrolling levels and
// gain reduction for the limiter / compressor / gate / de-esser, the spectrum with draggable bands for the
// multiband and the saturator —, the main knobs float over its bottom, the rarely used settings live in a bottom
// bar, and the display itself is edited by dragging (threshold line, gain, crossovers, band nodes). Drawn in LYNX's
// own style (colours, knobs, fonts); no graphics of the references are used.
namespace ProEditorWindows
{
bool handles (EffectKind kind) noexcept;
void open (AudioEngine& engine, int track, int slot);
void close (int track, int slot); // round tp172d: the slot's window, if open (Ember's Classic / multiband swap)
void closeAll();
// What the window's content shows at its default size (round 190bj, --snapshot), drawn off-screen with a native
// Windows frame: `juceAspect` = the sizing before round 190bj (JUCE's ratio on the whole window: the editor cut off).
juce::Image windowContentForSnapshot (AudioEngine& engine, int track, int slot, bool juceAspect);
} // namespace ProEditorWindows

// The editor alone (for --plugui); `demo` fills the displays with a synthetic signal.
std::unique_ptr<juce::Component> makeProEditor (AudioEngine& engine, int track, int slot, bool demo = false);
} // namespace lynx
