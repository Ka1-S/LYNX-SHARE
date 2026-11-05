#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Editor windows of LYNX's native effects (the engines compatible with FL Studio's plugins). Each one follows the
// FL plugin's arrangement (core/PluginLayout.h: same controls, sections, captions and places) drawn in LYNX's own
// style, with a presets bar on top (FL presets of the plugin from the user's installation, previous / next) and
// LYNX's control contract on every control (drag, Ctrl = fine, wheel, Alt+click / double-click = reset, right-click
// = type value / automation / MIDI link). Effects without a studied arrangement get a generated grid of knobs.
namespace NativeEffectWindows
{
// can the slot's effect be edited in a native editor window? (not external plugins nor the Patcher container)
bool canOpen (EffectKind kind);
// the same for a slot (a Soundgoodizer approximation container opens the Soundgoodizer page)
bool canOpen (const EffectSlot& slot);
void open (AudioEngine& engine, int track, int slot);
void closeAll();
// round tp172d: Ember's two editors — its Classic panel (this window) and its multiband page (ProEditorWindows) — swap
// in place when the Voicing switches: the slot's other window closes and this one opens (asynchronously)
void switchEmberEditor (AudioEngine& engine, int track, int slot, bool classic);
} // namespace NativeEffectWindows

// The editor alone at `scale` (1 = FL's size; for --snapshot / --plugui).
std::unique_ptr<juce::Component> makeNativeEffectEditor (AudioEngine& engine, int track, int slot, float scale = 1.0f, int page = 0);
// Round 170c (#14): drives every stepper of the slot's editor (all pages) with synthetic clicks, drags, wheel notches and
// trackpad scrolls; one "OK" / "FAIL <why>" line per stepper.
juce::String stepperSelfTest (AudioEngine& engine, int track, int slot, int pages);
// The same editor on an effect node inside a Patcher (round 165c: the Hardware Insert): `locate` finds the node's slot in
// the song, `running` its running instance (live meters); `name` is shown where a mixer editor names its insert.
std::unique_ptr<juce::Component> makeNodeEffectEditor (AudioEngine& engine, std::function<EffectSlot* (Song&)> locate,
                                                       std::function<std::shared_ptr<Effect>()> running, const juce::String& name, float scale = 1.25f);
// --plugui: live displays (LYNX Tune's pitch graph) draw a synthetic signal instead of waiting for audio
void setNativeEditorDemo (bool on);
// Round 166f (--eq166fshot): one display tick of a native editor without its timer (Parametric EQ 2's spectrum and
// heatmap read the slot's analyser tap, which this claims); false when `editor` is not a native effect editor.
bool tickNativeEffectEditor (juce::Component& editor);
// round 170k2: the smallest size its window allows (the header still shows the whole name); {} for another component
juce::Point<int> nativeEditorMinimumSize (juce::Component& editor);
} // namespace lynx
