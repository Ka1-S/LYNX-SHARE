#pragma once

// The Rack device (round 183c, docs/analise/REA_rodada183c.md §8–9): Live's rack layout drawn by LYNX: a view column, the
// macro variations, up to 16 macro knobs, the chain list with its zone editor, and the selected chain's devices. One
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

class RackView final : public juce::Component, private juce::Timer
{
public:
    // `automation`: the automation target of the rack's published parameter 0 (its kind / index / slot; param = macro)
    RackView (AudioEngine& engine, RackLocator locate, std::optional<AutomationTarget> automation, juce::String key, bool ownHeader,
              std::function<void (int node)> openNode);
    ~RackView() override;

    // The width the rack wants for a body of `height` (its open views side by side)
    int preferredWidth (int height) const;
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

    // The title bar's on / off switch (a mixer slot's or a channel's): its state and its toggle (window header only)
    void setPowerSwitch (std::function<bool()> isOn, std::function<void()> toggle);

    // (tests and shots) Map mode on / off
    void setMapMode (bool on);
    bool mapMode() const noexcept { return mapping; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    bool mapping = false;
    void timerCallback() override;
    friend struct Impl;
};
} // namespace lynx
