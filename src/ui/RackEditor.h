#pragma once

// The Rack device (round 183c, docs/analise/REA_rodada183c.md §8–9): Live's rack layout drawn by LYNX: a view column, the
// macro variations, up to 16 macro knobs, the chain list with its zone editor, and the selected chain's devices (a Drum
// Rack, round 190ec: its pad view, I-O / send columns and return chains too). One
// component, used in three places: the body of the rack's card in the Detail View's Device View, a mixer slot's window
// and a Channel Rack channel's window (a Patcher in rack mode opens this instead of the Patcher's map).

#include <functional>
#include <memory>
#include <optional>

#include "engine/AudioEngine.h"

namespace lynx
{
// Where a rack's document lives in the song (a mixer slot, a channel, or a rack nested in another rack's chain)
using RackLocator = std::function<PatcherDoc* (Song&)>;

class RackView;

namespace RackWindows
{
// The rack of mixer slot (track, slot) / channel `channel`: its view (`ownHeader`: with its own title strip holding
// Rand and Map, for a window; the Device View card puts those in its title bar instead, via titleAccessory)
std::unique_ptr<RackView> makeSlotView (AudioEngine& engine, int track, int slot, bool ownHeader);
std::unique_ptr<RackView> makeChannelView (AudioEngine& engine, int channel, bool ownHeader);
void openSlot (AudioEngine& engine, int track, int slot);
void openChannel (AudioEngine& engine, int channel);
bool isChannelOpen (int channel);
void closeChannel (int channel);
} // namespace RackWindows

namespace rackcards
{
// The Rack card of the Detail View's Device View (round 183c-card, on round 183a's DetailCard hook): a rack in a mixer
// slot or a Channel Rack channel shows as the rack's own view, its Rand / Map in the card's title bar. Call once at start-up.
void install();
} // namespace rackcards

class RackView final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    // `automation`: the automation target of the rack's published parameter 0 (its kind / index / slot; param = macro)
    RackView (AudioEngine& engine, RackLocator locate, std::optional<AutomationTarget> automation, juce::String key, bool ownHeader,
              std::function<void (int node)> openNode);
    ~RackView() override;

    // The width the rack wants for a body of `height` (its open views side by side)
    int preferredWidth (int height) const;
    // The narrowest it lays out in: the devices pane one card wide (it scrolls with the wheel)
    int minimumWidth (int height) const;
    // Called when that width changes (a view opened or closed, macros shown / hidden)
    std::function<void()> onPreferredWidthChanged;
    // The Rand and Map buttons for a title bar that isn't the rack's own (the Device View card)
    std::unique_ptr<juce::Component> makeTitleAccessory();
    static constexpr int titleAccessoryWidth = 66;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    // a Drum Rack's selected pad (round 190el, Live's pad menu shortcuts): Ctrl+R rename, Del delete, Ctrl+Right / Left
    // next / previous similar sample, Ctrl+Up save as similarity reference, Ctrl+Down return to it
    bool keyPressed (const juce::KeyPress& key) override;
    // a Drum Rack (round 190ec): samples dropped on its pads become Samplers there
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // The title bar's on / off switch (a mixer slot's or a channel's): its state and its toggle (window header only)
    void setPowerSwitch (std::function<bool()> isOn, std::function<void()> toggle);

    // (tests and shots) Map mode on / off
    void setMapMode (bool on);
    // (tests and shots, a Drum Rack) where the pad of `key` is drawn (empty when its row isn't shown), and the pad at a point
    juce::Rectangle<int> padBounds (int key) const;
    int padKeyAt (juce::Point<int> p) const;
    bool mapMode() const noexcept { return mapping; }
    // (shots, a Drum Rack) the menu of the pad of `key` (empty for an empty pad) and of an I-O cell (0 Receive, 1 Play,
    // 2 Choke, 3 Audio To) of chain `chain`
    juce::PopupMenu padMenuForSnapshot (int key);
    juce::PopupMenu ioMenuForSnapshot (int chain, int column);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    bool mapping = false;
    void timerCallback() override;
    friend struct Impl;
};
} // namespace lynx
