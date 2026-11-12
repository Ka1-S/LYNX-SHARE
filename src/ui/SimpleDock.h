#pragma once

// The Simple workflow's dock for the selected track (round 190bu; design: /mnt/e/lynx-work/simple184a/DESIGN.md §2.2,
// §3.3, §3.4; slices S5, S6, S13, S14): a strip under the timeline with two tabs.
//
//   Instrument  the track's sound: ‹ name › steps through its category, and the list by category picks one — LYNX's
//               own sounds (the synth's presets and LYNX's preset banks, core/SoundLibrary.h) or the presets of the
//               user's own SoundFont files. ▶ plays a sound without choosing it (AudioEngine::auditionSound).
//   Effects     the track's insert as cards: one-knob effects (core/GoalKnobs.h) show one big knob, any other effect
//               its Mix; on / off, move, remove, "Editor" opens the effect's own window. On the left the chain's
//               preset (core/ChainPresets.h, the user's own in Documents/LYNX/Chain presets) and Blend, the dry/wet
//               of the whole chain.
//
// It is a VIEW of what exists: the cards are the insert's own slots and the sound is the channel's own instrument,
// edited through the same code as the Mixer (EffectsPanel) and the Channel rack, so every other layout shows the same
// plugins in the same slots. Made the first time the Simple workflow shows it; nothing of it exists in the other
// workflows. No timer runs while it is hidden.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/SoundFontIndex.h"

namespace lynx
{
class AudioEngine;

class SimpleDock final : public juce::Component
{
public:
    struct Hooks
    {
        std::function<void (int channel)> toggleChannelWindow; // "Open full editor" of the instrument
        std::function<void()> showKeys;                        // the on-screen keyboard
        std::function<void()> layoutChanged;                   // the height or the shown state changed
    };

    enum class Tab { instrument, effects };

    SimpleDock (AudioEngine& engine, Hooks hooks);
    ~SimpleDock() override;

    // The Playlist track it shows (-1: none)
    void showTrack (int track);
    int shownTrack() const noexcept { return track; }
    // The song changed: the cards and the sound follow
    void refresh();

    void setTab (Tab tab);
    Tab getTab() const noexcept { return tab; }
    void cycleTab(); // Shift+Tab

    // The height the owner gives it out of `available` px (the timeline keeps at least 40 %)
    int heightFor (int available) const noexcept;

    // User settings (ui-options.txt; not the project)
    static inline int preferredHeight = 248;
    static inline bool shownSetting = true; // Ctrl+Alt+L
    static constexpr int minHeight = 150, edgeHeight = 5, stripHeight = 30;

    // The folder of the user's own chain presets (.lynxtrack files)
    static juce::File userPresetFolder();
    // Saves insert `insert`'s chain as the user preset `name` (its slots and Blend; plugin states captured first) and
    // names the insert's chain after it. False when the file can't be written.
    static bool saveUserPreset (AudioEngine& engine, int insert, const juce::String& name);
    // Puts a user preset file on insert `insert` (its slots and Blend only: level, pan and EQ stay), one undo step.
    static bool loadUserPreset (AudioEngine& engine, int insert, const juce::File& file);

    // tests and --simplefxshot
    int cardCountForCheck() const;
    int oneKnobCardsForCheck() const;
    int insertForCheck() const noexcept { return insert; }
    int channelForCheck() const noexcept { return channel; }
    bool hasInstrumentTabForCheck() const noexcept { return instrumentTab; }
    juce::PopupMenu presetMenuForSnapshot();
    juce::PopupMenu addMenuForSnapshot();
    void setFontsForSnapshot (std::vector<sf::IndexedFont> fonts); // shows "My SoundFonts" with these
    void showCategoryForSnapshot (int category);
    int soundRowsForCheck() const;
    juce::String soundNameForCheck() const;
    void clickSoundForCheck (int row);   // picks row `row` of the list as a click on its name does
    void stepSoundForCheck (int delta);  // the ‹ › arrows
    void turnKnobForCheck (int card, float value);
    void moveCardForCheck (int card, int delta);
    void removeCardForCheck (int card);
    void addOneKnobForCheck (int goal);
    void setBlendForCheck (float value);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    class EffectsPage;
    class InstrumentPage;

private:
    void showPage();

    AudioEngine& engine;
    Hooks hooks;
    std::unique_ptr<EffectsPage> effectsPage;
    std::unique_ptr<InstrumentPage> instrumentPage;
    juce::TextButton instrumentButton, effectsButton, hideButton;
    Tab tab = Tab::effects;
    bool tabChosen = false;     // the user picked a tab for this track: a refresh keeps it
    bool instrumentTab = false; // the track has an instrument whose sound can be picked
    int track = -1, insert = -1, channel = -1;
    int dragStartHeight = 0;
    bool resizing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleDock)
};
} // namespace lynx
