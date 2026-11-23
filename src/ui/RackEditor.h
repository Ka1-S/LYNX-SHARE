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
#include "ui/ValueControl.h"

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
// Round 190hr (Live's rack Save Preset, racks-068): the rack at `locate` as a Live rack preset (.adg) through a save
// dialog; what Live has no device for is listed after saving
void saveLivePreset (AudioEngine& engine, RackLocator locate, const juce::String& name);
// Round 190hr (racks-069): a Live instrument preset file (.adv / .adg) as a channel's instrument; nullopt + `error`
std::optional<ChannelDef> instrumentFromLivePreset (const juce::File& file, juce::String& error);
// A Live device / rack preset file (.adv / .adg)
bool isLivePreset (const juce::File& file);
} // namespace RackWindows

namespace rackcards
{
// The Rack card of the Detail View's Device View (round 183c-card, on round 183a's DetailCard hook): a rack in a mixer
// slot or a Channel Rack channel shows as the rack's own view, its Rand / Map in the card's title bar. Call once at start-up.
void install();
// Round 190nc (DL-95): where Live's Macro Mappings list shows while a Device View rack is in Map mode in the Live look —
// the component it covers (Live: the Browser), or nullptr (then it opens beside the rack). Set once by the main window.
void setMappingsHost (std::function<juce::Component*()> host);
} // namespace rackcards

class RackView final : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       public juce::DragAndDropTarget,
                       public ui::HintSource,
                       private juce::Timer
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
    // Round 190nc (DL-95, Live's look in the Device View): the selected chain's devices are shown inline (Live's rack),
    // and the Device View's title bar then spans only the rack's own part: its width (0 = the whole card)
    bool showsDevicesInline() const;
    int titleBarWidth() const;
    bool hitTest (int x, int y) override; // (the title strip over the rack's own part is the Device View's)
    ui::Hint hintAt (juce::Point<int> p) override;
    // (tests and shots) inline devices: how many, where device i and its knob k are, fold device i, pick a knob in Map
    // mode (as a click), press macro m's Map button, the Macro Mappings list's bounds now (empty: not shown)
    int inlineDeviceCountForCheck() const;
    juce::Rectangle<int> inlineDeviceBoundsForCheck (int device) const;
    juce::Rectangle<int> inlineKnobBoundsForCheck (int device, int knob) const;
    void toggleInlineFoldForCheck (int device);
    juce::Rectangle<int> mapButtonBoundsForCheck (int macro) const;
    juce::Component* mappingsListForCheck() const;
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
    // Round 190hr (Live's chain drop area, racks-065): a device or preset dragged from the Browser (an effect, an
    // instrument, a .fst / .adv / .adg file) or from the file manager goes into the chain under the mouse (else the
    // selected one), before the device it is dropped on (else last). What the rack kind can't hold is refused with a hint.
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;
    // (tests and shots) a node dropped at a point, as a drop from the Browser places it; false when refused
    bool dropNodeForCheck (PatchNode node, juce::Point<int> at);

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
    void padLitForSnapshot (int key); // round 190lr (DL-94): pad `key` drawn as sounding
    // round 190mr (DL-92): Auto Select's tick with these keys sounding (128 bits); returns the selected chain
    int autoSelectForSnapshot (const std::array<std::uint64_t, 2>& keys);
    void clickForSnapshot (juce::Point<int> at); // a left click there
    juce::Rectangle<int> autoSelectButtonForSnapshot() const;
    juce::PopupMenu ioMenuForSnapshot (int chain, int column);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    bool mapping = false;
    void timerCallback() override;
    friend struct Impl;
};
} // namespace lynx
