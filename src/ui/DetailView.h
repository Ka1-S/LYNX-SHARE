#pragma once

// The Detail View of the Clip-launch workflow (round 183a; docs/analise/REA_rodada183a.md §6): the bottom dock under the
// clip grid and the Playlist that shows either the Device View (the selected track's insert as a horizontal chain of
// cards: its instrument, then its effect slots, folded or not, on/off, drag to reorder, drop from the Browser) or the
// Clip View (the selected slot's clip). Shift+Tab switches, as in the reference DAW. Own design and art.
//
// It is a VIEW of what exists (coordinator decision D2a): the cards are the insert's own slots, edited through the
// Mixer's own code (EffectsPanel), so the Mixer and the Device View always show the same chain. Fold state lives here
// only and is not saved. Made the first time Clip-launch workflow is chosen; never in Pattern workflow.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"

namespace lynx
{
class AudioEngine;

// A card body drawn by another module (round 183c's racks): the Device View keeps the shared title bar (LED, name,
// window button, fold, right-click menu, drag, the selection outline) and lays this component out as the card's body,
// as wide as it asks; the chain scrolls sideways. Made once per slot (or channel) and kept while the slot keeps its kind.
class DetailCard : public juce::Component
{
public:
    virtual int preferredWidth (int bodyHeight) const = 0;
    // extra title-bar buttons (e.g. a rack's Rand / Map), placed left of the window button; nullptr = none
    virtual juce::Component* titleAccessory() { return nullptr; }
    virtual int titleAccessoryWidth() const { return 0; }
    std::function<void()> onPreferredWidthChanged; // set by the Device View: call it when the card grows or shrinks
};

class DetailView final : public juce::Component
{
public:
    // Factories of custom cards (nullptr result = an ordinary card). Set once at start-up by the module that owns them.
    using EffectCardFactory = std::function<std::unique_ptr<DetailCard> (AudioEngine&, int insert, int slot, const EffectSlot&)>;
    using InstrumentCardFactory = std::function<std::unique_ptr<DetailCard> (AudioEngine&, int channel, const ChannelDef&)>;
    static inline EffectCardFactory effectCardFactory;
    static inline InstrumentCardFactory instrumentCardFactory;

    enum class Page { clip, devices };

    struct Hooks
    {
        std::function<float (int insert, int side)> levelOf;            // the insert's output now, 0..1 (the out meter)
        std::function<void (int channel)> toggleChannelWindow;          // the instrument card's window button
        std::function<void (int insert)> selectInMixer;                 // LYNX extra: a selected card selects its insert
        std::function<void (int insert, ChannelDef)> instrumentDropped; // a generator dropped on the chain
        std::function<void (int track, int scene)> openClip;            // the Clip page's button: the clip in its editor
        std::function<void()> layoutChanged;                            // height, visibility or a setting changed
    };

    DetailView (AudioEngine& engine, Hooks hooks);
    ~DetailView() override;

    // What it shows: the clip grid's selected track (its chain) and slot (its clip).
    void showSelection (int track, int scene);
    void setPage (Page page);
    Page getPage() const noexcept { return devicesPage ? Page::devices : Page::clip; }
    void togglePage(); // Shift+Tab

    // The height the owner gives it out of `available` px (the dock keeps at least half for the grid and Playlist).
    int heightFor (int available) const noexcept;

    // User settings (ui-options.txt; not the project).
    static inline int preferredHeight = 228;       // the reference DAW's Clip View at 100 % measures ~224 px + its frame
    static inline bool shownSetting = true;        // Ctrl+Alt+L
    static inline bool devicesPage = true;         // false: the Clip page
    static inline bool extraWindowKey = false;     // LYNX extra (off by default): W shows the selected card's window
    static inline bool extraSelectInMixer = false; // LYNX extra (off by default): selecting a card selects its insert
    static constexpr int minHeight = 140, edgeHeight = 5, stripHeight = 22;

    // tests and --snapshot
    int cardCountForCheck() const;
    int chainInsertForCheck() const;
    int chainChannelForCheck() const;
    bool isFoldedForCheck (int card) const;
    void toggleFoldForCheck (int card);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    class DeviceChain;
    class ClipPage;

private:
    void showPage();
    void showOptionsMenu();

    AudioEngine& engine;
    Hooks hooks;
    std::unique_ptr<DeviceChain> chain;
    std::unique_ptr<ClipPage> clipPage;
    juce::TextButton clipTab, devicesTab, optionsButton;
    int track = -1, scene = -1;
    int dragStartHeight = 0;
    bool resizing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailView)
};
} // namespace lynx
