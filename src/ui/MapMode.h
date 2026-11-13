#pragma once

// Round 190ef: the reference DAW's MIDI Map Mode and Key Map Mode in the Clip-launch (Ableton-style) layout
// (core/RemoteMap.h has the rules; docs/analise/REA_rodada190ef.md the evidence).
//   - Overlay: over the main window while a map mode is on. Every element that can be mapped is tinted (MIDI blue, Key
//     orange); a mapped element shows its assignment ("1/19" = MIDI channel / controller, a key's name). A click picks
//     the element; the next MIDI control (MIDI) or computer key (Key) is assigned to it. Clicks beside the elements go
//     to the window below as usual.
//   - Mapping Browser: the list of the mode's mappings in place of the Browser: Channel, Note/Control (MIDI) or Key,
//     Path, Name, Min, Max. Min / Max: drag up / down or double-click to type; right-click: Invert Range, Delete; the
//     Delete or Backspace key removes the selected row.
// Elements say they can be mapped by `tag` (one target for the whole component) or by being a SpotSource (several
// targets painted by one component: the clip grid's slots, the Session mixer's knobs, the Device View's knobs).

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/RemoteMap.h"

namespace lynx
{
class AudioEngine;

namespace mapmode
{
struct Spot
{
    juce::Rectangle<int> area; // in the source component (SpotSource) / the overlay (Overlay::spots)
    remotemap::Target target;
};

class SpotSource
{
public:
    virtual ~SpotSource() = default;
    // The mappable elements now on screen, in this component's coordinates.
    virtual void mapSpots (std::vector<Spot>& out) = 0;
};

// The whole component is one mappable element.
void tag (juce::Component& c, const remotemap::Target& target);
std::optional<remotemap::Target> tagOf (const juce::Component& c);

// Path and Name of a target, as the Mapping Browser shows them ("Bass", "Volume"; "Bass", "Clip slot 2"…).
std::pair<juce::String, juce::String> describe (AudioEngine& engine, const remotemap::Target& target);
// A computer key's name ("A", "F5", "Space").
juce::String keyText (int key);
// The label of a mapped element: "1/19" (channel / controller), "1/C3" (channel / key), a computer key's name.
juce::String assignedText (const remotemap::Assigned& a, remotemap::Mode mode);
// A value of a target's range as text (a parameter's own units where LYNX knows them, else percent).
// A mapping's Min / Max in the parameter's own unit (round 190em), and a typed one back to 0..1.
juce::String rangeText (AudioEngine& engine, const remotemap::Target& target, float value);
std::optional<float> rangeValueOf (AudioEngine& engine, const remotemap::Target& target, const juce::String& typed);
// The colour of a map mode's highlights.
juce::Colour modeColour (remotemap::Mode mode);

class Overlay final : public juce::Component, private juce::Timer
{
public:
    Overlay (AudioEngine& engine, juce::Component& root);
    ~Overlay() override;

    void setMode (remotemap::Mode mode);
    remotemap::Mode getMode() const noexcept { return mode; }
    std::optional<remotemap::Target> selection() const { return selected; }
    void select (const std::optional<remotemap::Target>& target);
    // Key Map Mode: the key for the selected element. False: nothing selected (or refused; the message says why).
    bool assignKey (int key);
    // MIDI Map Mode: a MIDI control for the selected element (the engine's catch, or a test's).
    bool assignMidi (const remotemap::MidiControl& control);
    // The selected element's mapping of this mode goes.
    bool unmapSelected();
    // What the status line says (the main window posts it).
    std::function<void (const juce::String&)> onMessage;
    // Elements now mappable, in this overlay's coordinates (refreshed by the timer; `refresh` for shots and tests).
    void refresh();
    const std::vector<Spot>& spots() const noexcept { return found; }

    void paint (juce::Graphics& g) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    void timerCallback() override;
    void collect (juce::Component& c, std::vector<Spot>& out);
    void tell (const juce::String& text);
    juce::String refusalText (remotemap::Refusal r) const;
    int spotAt (juce::Point<int> p) const;

    AudioEngine& engine;
    juce::Component& root;
    remotemap::Mode mode = remotemap::Mode::off;
    std::vector<Spot> found;
    std::vector<juce::Component*> owners; // (while collecting: the component each spot belongs to)
    bool collecting = false;
    std::optional<remotemap::Target> selected;
};

class MappingBrowser final : public juce::Component, private juce::Timer
{
public:
    explicit MappingBrowser (AudioEngine& engine);
    ~MappingBrowser() override;

    void setMode (remotemap::Mode mode);
    std::vector<remotemap::Row> rowsShown() const { return list; }
    void selectRow (int row);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    // the right-click menu of a row (--mapshot190ef draws it)
    juce::PopupMenu rowMenu (int row);

private:
    void timerCallback() override;
    void reload();
    int rowAt (int y) const;
    int columnAt (int x) const; // index into the mode's columns
    std::vector<int> columnWidths() const;
    bool isMinMaxColumn (int column, bool& isMax) const;
    void editRange (int row, bool isMax, float value);
    void typeRange (int row, bool isMax);
    void removeSelected();

    AudioEngine& engine;
    remotemap::Mode mode = remotemap::Mode::midi;
    std::vector<remotemap::Row> list;
    std::vector<int> selectedRows;
    int dragRow = -1;
    bool dragMax = false;
    float dragStart = 0.0f;
};
} // namespace mapmode
} // namespace lynx
