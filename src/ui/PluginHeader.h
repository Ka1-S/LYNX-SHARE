#pragma once

// The header controls every original LYNX's engines stand in for has (REA round 129, docs/PARITY.md): undo / redo of
// this plugin's own edits, A / B compare with "copy to the other", and a preset bar (◀ name ▶, a menu of LYNX's factory
// presets and the user's own, Save / Delete). Shared by the generic panels (ui/PluginEditors.cpp) and the display-first
// editors (ui/ProEditorBase.h): the owner draws its header and hands the right part of it to `PresetBar`.
//
// The state lives in the slot (EffectSlot::presetName, abOther, abShowingB — saved with the project); the undo history
// of each slot lives here, in memory, for the session (every edit is also on the song's undo list as before).

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/Song.h"

namespace lynx
{
class AudioEngine;

namespace pluginheader
{
// The parts of the bar.
enum class Part
{
    none,
    undo,
    redo,
    a,
    b,
    copy,
    preset,
    prev,
    next
};

class PresetBar
{
public:
    PresetBar (AudioEngine& engine, int track, int slot);

    // Where the bar sits: right-aligned in `area` (a header strip); returns the space left of it.
    // keepLeft (round 170k2): the width the caller needs at the left of `area` (its title): the copy button goes and the
    // preset box shrinks (down to 70 px) before the bar eats into it
    juce::Rectangle<float> place (juce::Rectangle<float> area, bool withPresets = true, float keepLeft = 0.0f);
    // round 170k2: the width the bar takes in a narrow window (no copy button, the preset box at its smallest)
    static float narrowWidth (bool withPresets = true);
    void paint (juce::Graphics& g, juce::Colour accent) const;
    Part partAt (juce::Point<float> p) const;
    bool contains (juce::Point<float> p) const { return partAt (p) != Part::none; }
    juce::String hintFor (Part part) const;

    // A click on a part: `owner` anchors the menus. `flPresets` (optional) adds FL's factory presets of an FL effect
    // to the preset menu (the generic editor's own list) and is used for ◀ ▶ when the engine has no LYNX presets.
    struct FlPresets
    {
        std::function<void (juce::PopupMenu&)> addTo;
        std::function<void (int delta)> step;
        std::function<juce::String()> name;
        // round 168r: ◀ ▶ run through LYNX's bank, the user's presets and then FL's (an engine with both)
        std::function<int()> count;       // FL's presets of this plugin
        std::function<int()> index;       // the FL preset loaded now (−1 = none)
        std::function<void (int)> load;   // loads FL preset i
    };
    void click (Part part, juce::Component& owner, const FlPresets* flPresets = nullptr);

    // Edits: call `beginGesture` on a mouse-down that may change a value and `willEdit` before any change is applied
    // (it records the setting before the change for undo; changes closer than ~0.6 s without a new gesture coalesce).
    void beginGesture() noexcept { gesture = true; }
    void willEdit();

    bool hover (juce::Point<float> p); // true when the hovered part changed (repaint)
    void hoverExit() { hovered = Part::none; }

    bool hasLynxPresets() const;
    juce::String presetLabel (const FlPresets* flPresets = nullptr) const;

private:
    const EffectSlot* current() const;
    void restore (const EffectParams& params, const std::vector<std::uint8_t>& state, const std::string& presetName, bool undoable = true);
    void showPresetMenu (juce::Component& owner, const FlPresets* flPresets);
    void stepPreset (int delta, const FlPresets* flPresets);
    void loadFactory (int index);
    void loadUser (const juce::File& file);
    void saveAs (juce::Component& owner);

    AudioEngine& engine;
    int track, slot;
    EffectKind kind = EffectKind::eq;
    juce::Rectangle<float> rUndo, rRedo, rA, rB, rCopy, rPreset, rPrev, rNext;
    bool presetsShown = true;
    Part hovered = Part::none;
    bool gesture = true;
    juce::uint32 lastEdit = 0;
};

// The user's presets of an engine: <settings>/Presets/<engine>/*.lynxpreset, sorted by name.
juce::File userFolder (EffectKind kind);
juce::Array<juce::File> userPresets (EffectKind kind);

// Drops the undo history of every slot (a new project).
void clearHistory();

// One sentence about what a control does (the reference reverbs' description line at the bottom of the window,
// round 129), translated; empty when there is none. `param` is the parameter's EffectInfo name.
juce::String describe (EffectKind kind, const char* param);

// The description line while no control is under the mouse (round 135): "Mode: Plate — a plate: dense ...", empty
// when the engine has no mode.
juce::String modeLine (EffectKind kind, const EffectParams& params, double bpm);

// The mix lock ("Keep the Mix when loading presets"), also toggled by a click on a LYNX engine's MIX caption (round 135).
bool isMixLocked (EffectKind kind);
void toggleMixLock (EffectKind kind);

// --- One-click sidechain (round 177b) ------------------------------------------------------------------------------
// FL hides its sidechain behind the route arrow's menu ("Sidechain to this track"); every effect that has a side input
// also gets a picker of its own: its window header and its slot menu list the inserts, and picking one makes the link
// (mixerops::sidechainFrom: a level-0 send when there is no route, the source keeps its Master send). FL's way still
// works as before.
// Does slot `slot` of insert `track` read a side input? (LYNX / FL engines: mixerops::acceptsSidechain; a hosted plugin:
// when it has a second input bus.)
bool takesSidechain (AudioEngine& engine, int track, int slot);
// The insert the slot listens to, as the header shows it ("3 Kick"), or "" when none.
juce::String sidechainSourceText (const Song& song, const EffectSlot& fx, bool second = false);
// The picker's items (None, then every other insert; a source that would make a loop is greyed out), added to `menu`.
void addSidechainPicker (juce::PopupMenu& menu, AudioEngine& engine, int track, int slot, bool second = false);
// The picker as a menu of its own under `target` (a header button).
void showSidechainPicker (AudioEngine& engine, int track, int slot, juce::Component* target, juce::Rectangle<int> area = {},
                          bool second = false);
} // namespace pluginheader
} // namespace lynx
