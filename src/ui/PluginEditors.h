#pragma once

#include <array>

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
// Round 190rb: the help line of every control with a parameter painted with that control under the mouse (all pages):
// a line per control past the plain parameters ("OK" / "FAIL ..."), then "<effect> checked N"
juce::String helpLineSelfTest (AudioEngine& engine, int track, int slot, int pages);
// The same editor on an effect node inside a Patcher (round 165c: the Hardware Insert): `locate` finds the node's slot in
// the song, `running` its running instance (live meters); `name` is shown where a mixer editor names its insert.
std::unique_ptr<juce::Component> makeNodeEffectEditor (AudioEngine& engine, std::function<EffectSlot* (Song&)> locate,
                                                       std::function<std::shared_ptr<Effect>()> running, const juce::String& name, float scale = 1.25f);
// --plugui: live displays (LYNX Tune's pitch graph) draw a synthetic signal instead of waiting for audio
void setNativeEditorDemo (bool on);
// Round 166f (--eq166fshot): one display tick of a native editor without its timer (Parametric EQ 2's spectrum and
// heatmap read the slot's analyser tap, which this claims); false when `editor` is not a native effect editor.
bool tickNativeEffectEditor (juce::Component& editor);
// round 190ih (--details190ihshot): Gross Beat's editor on its time (0) or volume (1) envelope
bool showGrossBeatEnvelopeForSnapshot (juce::Component& editor, int envelopeKind);
// round 190mf (DP-6 / DP-7): Gross Beat's Step / Slide / Freeze switches set, and its mapping options menu
// round 190mf (DP-26): Fruity Limiter's KNEE knob held (its transfer curve on the display)
// round 190mf (DP-29): Fruity Multiband Compressor's band meters with fixed levels
bool multibandMetersForSnapshot (juce::Component& editor);
bool multibandHistoryForSnapshot (juce::Component& editor, float speed); // round 190mk (DP-30): a fixed peak history, SPEED at `speed`
bool limiterKneeHeldForSnapshot (juce::Component& editor, bool held);
// round 190mk: 1 = Prepare for smooth editing, 2 = points 2-3 copied and pasted (insert), 3 = View gradient on / grid inverted, 0 = views back
bool grossBeatEditForSnapshot (juce::Component& editor, int what);
bool grossBeatToolsForSnapshot (juce::Component& editor, bool step, bool slide, bool frozen, juce::PopupMenu* optionsMenu);
// round 190ld (--details190ldshot): Fruity Delay 3's Time-knob menu while TEMPO SYNC is on (empty otherwise); the
// Vocoder's band columns as Vocoder::controlOutput 0…7 packs them; Fruity Limiter's graph fed `steps` synthetic frames
// and paused (false: not a Limiter / Maximus monitor)
juce::PopupMenu delay3TimeMenuForSnapshot (juce::Component& editor);
bool setVocoderBandsForSnapshot (juce::Component& editor, const std::array<std::uint32_t, 8>& packed);
bool pauseLimiterForSnapshot (juce::Component& editor, int steps);
// round 190lx (--details190lxshot): Transient Processor's lower trace fed synthetic hits, the hover panel shown or not
bool transientTraceForSnapshot (juce::Component& editor, bool hovered);
// round 190ih (--uiperf): Gross Beat editors' paints since the last reset ("" when none): count, area, time
juce::String grossBeatPaintStats (bool reset);
// round 190ih (--uiperf A/B): Gross Beat's diagnostic switches at run time (the LYNX_GB_DIAG bits)
void setGrossBeatDiagnostics (int mask);
// round 170k2: the smallest size its window allows (the header still shows the whole name); {} for another component
juce::Point<int> nativeEditorMinimumSize (juce::Component& editor);
} // namespace lynx
