#pragma once

// The Simple workflow's dock for the selected track (round 190bu; design: /mnt/e/lynx-work/simple184a/DESIGN.md §2.2,
// §3.3, §3.4; slices S5, S6, S13, S14): a strip under the timeline with two tabs.
//
//   Instrument  the track's sound: ‹ name › steps through its category, and the list by category picks one — LYNX's
//               own sounds (the synth's presets and LYNX's preset banks, core/SoundLibrary.h) or the presets of the
//               user's own SoundFont files. ▶ plays a sound without choosing it (AudioEngine::auditionSound).
//   Keyboard    (round 190ds) the on-screen keyboard playing the track, with its modes Keys | Scale keys | Chord pad
//               (ui/TouchKeyboard.h, core/ScaleKeys.h) and the scale selector (the project key).
//   Editor      (round 190ds) the Piano roll docked here on the track's pattern; a track without one gets an empty
//               4-bar pattern the first time (core/SimpleTracks.h ensurePattern), so the first click writes a note.
//               "Only notes in key" folds the rows to the project key.
//   Pads        (round 190fo) a Sampler track's 16 pads (core/SimplePads.h): play, drop sounds on them, their region,
//               mode, envelope, tone, pitch, pan, volume, group and colour; kits of the user's own.
//   Controls    (round 190ke) eight big knobs on the track's key settings (GarageBand's Smart Controls, core/SmartControls.h):
//               the instrument's parameters, the chain's effects, Reverb / Pan / Volume; Edit mapping re-points a knob.
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
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/SoundFontIndex.h"
#include "core/SoundLibrary.h"

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
        // round 190ds: the Editor tab docks the main window's Piano roll (on `pattern`; nullptr: not possible) and gives it
        // back; "Only notes in key" (the Piano roll's fold to the scale); the Piano roll's own panel (F7)
        std::function<juce::Component* (int pattern)> dockPianoRoll;
        std::function<void()> undockPianoRoll;
        std::function<void (bool)> setFoldToKey;
        std::function<bool()> foldedToKey;
        std::function<void()> openPianoRollPanel;
    };

    enum class Tab { instrument, keyboard, editor, effects, pads, controls, steps };

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
    void showKeyboard(); // the Keyboard tab, as chosen by the user (round 190ds: a new instrument opens on it)

    // Round 190ds: the docked Piano roll was taken back by its own panel (F7) / must be given back (the workflow left)
    void editorTakenBack();
    void releaseEditor();
    bool isEditorDocked() const noexcept;

    // Round 190ih: the input level of an armed track's input in the header ("Input Level", BandLab); the main window's
    // 30 Hz timer calls it (nothing happens while the dock is hidden)
    void tickInputMeter();
    void inputLevelForSnapshot (float level) { inputMeterForced = true; inputLevel = level; repaint(); } // (--details190ihshot)

    // The height the owner gives it out of `available` px (the timeline keeps at least 40 %)
    int heightFor (int available) const noexcept;

    // User settings (ui-options.txt; not the project)
    static inline int preferredHeight = 248;
    static inline bool shownSetting = true; // Ctrl+Alt+L
    static constexpr int minHeight = 150, edgeHeight = 5;
    // the header strip (round 190jc, design190ia SG-7: 30 -> 36 px, BandLab's Track View header; Large controls 48)
    static int stripHeight();
    void lookChanged(); // round 190jc: View › Large controls changed

    // The folder of the user's own chain presets (.lynxtrack files)
    static juce::File userPresetFolder();
    // The favourite chain presets (factory or the user's, by name; round 190dg), kept in that folder's Favourites.txt
    static juce::File favouritesFile();
    static juce::StringArray favouritePresets();
    static bool setFavouritePreset (const juce::String& name, bool on);
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
    void setPluginsForSnapshot (std::vector<soundlib::InstalledPlugin> plugins); // shows "My plugins" with these
    void showCategoryForSnapshot (int category);
    int soundRowsForCheck() const;
    juce::String soundNameForCheck() const;
    void clickSoundForCheck (int row);   // picks row `row` of the list as a click on its name does
    void stepSoundForCheck (int delta);  // the ‹ › arrows
    // round 190kn (BandLab's effects side panel): the preset's ‹ › and the chain bypass, as their buttons do
    void stepPresetForCheck (int delta);
    void bypassChainForCheck();
    bool bypassLitForCheck() const;
    void turnKnobForCheck (int card, float value);
    void moveCardForCheck (int card, int delta);
    void removeCardForCheck (int card);
    void addOneKnobForCheck (int goal);
    void setBlendForCheck (float value);
    void setReverbForCheck (float value); // round 190fo: the track's send to the shared reverb
    // round 190dg (--simple190dgshot): where card `card` is in the dock, and a search typed into the sound list
    juce::Rectangle<int> cardBoundsForSnapshot (int card) const;
    void setSearchForSnapshot (const juce::String& text);
    // round 190ds (--simple190dsshot)
    bool hasPlayTabsForCheck() const noexcept { return playTabs; }
    bool followShownForCheck() const;
    void clickFollowForCheck();
    juce::Component* keyboardPanelForSnapshot();
    void clickFoldForCheck (bool on); // the Editor tab's "Only notes in key"
    // round 190fo (--reabl190foshot)
    class PadsPage;
    bool hasPadsTabForCheck() const noexcept { return padsTab; }
    void padsLoadForCheck (int pad, const juce::File& file);
    void padsSelectForCheck (int pad);
    void padsModeForCheck (int mode);       // the selected pad's mode button (0 Gate, 1 One-shot, 2 Loop)
    void padsKnobForCheck (int knob, float v); // 0 Attack, 1 Release, 2 Tone, 3 Pitch, 4 Pan, 5 Volume
    void padsRegionForCheck (float start, float end);
    void padsNormalizeForCheck();
    void padsHitForCheck (int pad);
    void padsPressForSnapshot (int pad, float amount); // round 190jj: a pad drawn pressed (0 = at rest, 1 = darkest)
    void padsWatchForCheck();                           // round 190jj: one look at the song's notes (the 30 Hz tick)
    float padsPressForCheck (int pad) const;            // ... and the press a pad shows now
    juce::Component* keyboardForSnapshot() { return keyboardPage.get(); }
    void padsDragForCheck (int from, int to, bool copy);
    void padsUpdateForCheck();
    // round 190mr (DS-36): the empty pad's calls to action (0 Record, 1 Import, 2 Browse), recording into the pad
    juce::Component* padsCtaForCheck (int k);
    bool padsRecordingForCheck() const;
    void padsRecordForCheck();
    void padsRecordingLookForSnapshot (bool on);
    juce::PopupMenu padMenuForSnapshot (int pad);
    juce::PopupMenu kitMenuForSnapshot();
    bool saveKitForCheck (const juce::String& name);
    bool openKitForCheck (const juce::File& file);
    static bool renameKitForCheck (const juce::File& file, const juce::String& name);
    void lightPad (int pad); // a pad played by its typing key
    bool recordHintShown() const; // the strip's "wear headphones" line (an armed Voice / Guitar / Bass track with an input)
    std::function<void()> onBrowseSounds; // the Pads tab's "Browse sounds"

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    class EffectsPage;
    class InstrumentPage;
    class EditorPage;
    class ControlsPage; // round 190ke: the Controls tab (GarageBand's Smart Controls, ui/SimpleControlsPage.inc)
    class StepsPage;    // round 190lr: the Drum Machine tab (BandLab's drum machine, ui/SimpleStepsPage.inc)

    // round 190lr (DS-39, tests and --details190lrshot): a Drums track's Drum Machine tab
    bool hasStepsTabForCheck() const noexcept { return stepsTab; }
    StepsPage* stepsPageForCheck() { return stepsPage.get(); }
    juce::Component* stepsComponentForSnapshot();
    int stepsRowsForCheck() const;
    int stepsCountForCheck() const;
    void stepsClickForCheck (int row, int step);
    void stepsLetterForCheck (int letter);
    void stepsAddClipForCheck();
    void stepsLengthForCheck (int bars);
    void stepsSwingForCheck (float v);
    void stepsPreviewForCheck();
    bool stepsPreviewingForCheck() const;
    int stepsPatternForCheck() const;
    int stepsPlayingStepForCheck() const;
    void stepsPlayingStepForSnapshot (int step);
    juce::PopupMenu stepsLengthMenuForSnapshot();
    void stepsWatchForCheck();
    void stepsVelocityForCheck (int row, int step, float v); // BandLab's V tool on a step
    void stepsVelocityToolForSnapshot (bool on);
    // a typing key while the Drum Machine tab shows: the row's channel (lit), −1 when the key plays no row
    int stepsChannelForKey (int keyCode);

    // round 190ke (tests, --simplegb190keshot): the Controls tab's knobs
    bool hasControlsTabForCheck() const noexcept { return controlsTab; }
    void controlsTurnForCheck (int knob, float value);
    juce::String controlsNameForCheck (int knob) const;
    float controlsValueForCheck (int knob) const;
    void controlsEditingForSnapshot (bool on);
    juce::PopupMenu controlsMenuForSnapshot (int knob);

private:
    void showPage();
    void updateFollow(); // the strip's "Follow the project key" switch (audio tracks with a known loop key)
    std::vector<int> trackAudioClips() const;

    AudioEngine& engine;
    Hooks hooks;
    std::unique_ptr<EffectsPage> effectsPage;
    std::unique_ptr<InstrumentPage> instrumentPage;
    std::unique_ptr<EditorPage> editorPage;
    std::unique_ptr<PadsPage> padsPage;
    std::unique_ptr<ControlsPage> controlsPage; // round 190ke
    std::unique_ptr<StepsPage> stepsPage;       // round 190lr
    std::unique_ptr<juce::Component> keyboardPage; // (made the first time the Keyboard tab shows)
    // round 190jc (design190ia SG-7, SG-8): the strip's buttons draw themselves: the tabs with a vector icon before the
    // label (a lone tab as a section title), the hide button as BandLab's round close X at the strip's left
    class StripButton final : public juce::TextButton
    {
    public:
        int glyph = -1;    // ui::simplelook::Glyph of a tab; -1: the round close X
        bool lone = false; // the only tab: drawn as a section title
        void paintButton (juce::Graphics& g, bool over, bool down) override;
    };
    StripButton instrumentButton, keyboardButton, editorButton, effectsButton, padsButton, controlsButton, stepsButton, hideButton;
    juce::ToggleButton followButton;
    juce::String followText;
    Tab tab = Tab::effects;
    bool tabChosen = false;     // the user picked a tab for this track: a refresh keeps it
    bool instrumentTab = false; // the track has an instrument whose sound can be picked
    bool playTabs = false;      // the track plays notes (Instrument / Sampler): Keyboard and Editor
    bool padsTab = false;       // round 190fo: a Sampler track's pads
    bool controlsTab = false;   // round 190ke: a track with an insert: its eight knobs
    bool stepsTab = false;      // round 190lr: a Drums track's step grid
    int track = -1, insert = -1, channel = -1;
    juce::Colour trackColourNow() const; // round 190jj: the shown track's colour (its own, else its kind's), for held keys
    // round 190ih (DS-9): the record dock's input level
    bool inputMeterShown() const;
    juce::Rectangle<int> inputMeterArea() const, inputMeterBar() const;
    juce::Rectangle<int> monitorButton() const; // round 190ld (DS-9): BandLab's headphones switch
    bool monitoring() const;
    float inputLevel = 0.0f;
    bool inputMeterWasShown = false, inputMeterForced = false;
    int dragStartHeight = 0;
    bool resizing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleDock)
};
} // namespace lynx
