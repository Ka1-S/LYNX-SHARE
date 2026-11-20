#pragma once

// The Piano roll's Chord helper (round 190mc, GitHub discussion #105; the theory in core/ModalHelper.h). A LYNX extra:
// select a chord, see the key it implies with the chords before it, the related keys for modal interchange (the parallel
// minor or major first) and the chords they lend, ranked from common to uncommon, with their movement from the current
// root, the change of quality, the common tones and a mood. A click inserts the chord after the selection, voice-led
// (or in the side menu's inversion, with its extension); hovering a chord plays the move (selected chord → that chord).
//
// One panel, three looks: the Pattern layout's (LYNX's FL-style tool window, as the Chord progression tool and the MIDI
// Tools), the Clip-launch layout's (Live 12.4's Dark greys, square rows, the light-blue selection) and the Simple
// layout's (rounded cards, larger rows, the red accent). Perf: it computes only while it is open, and only when the
// selected notes (or the notes before them) change; no timer runs unless a preview is pending or sounding.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ModalHelper.h"
#include "engine/AudioEngine.h"

namespace lynx
{
class ChordHelperPanel final : public juce::Component, public juce::TooltipClient, private juce::ChangeListener, private juce::Timer
{
public:
    enum class Look
    {
        fl,     // Pattern layout
        live,   // Clip-launch layout
        simple  // Simple layout
    };

    // what the panel works on: the editor's pattern, its channel and the selected notes (indices into the pattern)
    struct Target
    {
        int pattern = -1;
        int channel = -1;
        std::vector<int> selected;
    };

    ChordHelperPanel (AudioEngine& engine, std::function<Target()> target, std::function<void (const std::vector<int>&)> select, Look look);
    ~ChordHelperPanel() override;

    static Look lookNow(); // the shown layout's look
    static juce::Point<int> preferredSize (Look look);

    // the editor's selection changed (the Piano roll calls it; cheap when nothing it reads changed)
    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    juce::String getTooltip() override;

    // --- tests and shots ---
    const modalhelp::Analysis& analysis() const noexcept { return result; }
    const std::vector<modalhelp::RelatedKey>& relatedKeys() const noexcept { return keys; }
    const std::vector<modalhelp::Borrowed>& borrowedChords() const noexcept { return chords; }
    int selectedKeyRow() const noexcept { return keyRow; }
    void selectKeyRow (int row);
    void hoverChordRowForShot (int row) { hoveredChord = row; repaint(); }
    bool insertChordRow (int row);
    void setExtension (modalhelp::Extension e);
    void setInversion (modalhelp::Inversion i);
    void setAudition (bool on) { audition = on; repaint(); }
    void overrideKey (std::optional<int> tonic, std::optional<modalhelp::Mode> mode);
    int computations() const noexcept { return computeCount; } // how often the analysis ran
    bool previewRunning() const noexcept { return isTimerRunning(); }

private:
    struct Palette;
    Palette palette() const;
    void compute (bool force = false);
    std::vector<int> selectedKeys (const Song& song, const Target& t) const;
    std::vector<int> voicedChord (int row) const;
    void startPreview (int row);
    void stopPreview();
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showKeyMenu();
    void showModeMenu();

    // geometry
    juce::Rectangle<int> headerArea() const, keysArea() const, chordsArea() const, sideArea() const, hintArea() const;
    juce::Rectangle<int> keyChip() const, modeChip() const, auditionBox() const;
    int rowHeight() const;
    int headHeight() const;
    int keyRowAt (juce::Point<int> p) const;
    int chordRowAt (juce::Point<int> p) const;
    int sideItemAt (juce::Point<int> p) const; // 0…4 inversions, 10…15 extensions, −1 none
    juce::Rectangle<int> sideItem (int id) const;

    juce::String signatureText (const modalhelp::Scale& s) const;
    juce::String keyText (const modalhelp::Scale& s) const;
    juce::String chordText (const modalhelp::Borrowed& b) const;
    juce::String movementText (const modalhelp::Borrowed& b) const;
    juce::String detailText (const modalhelp::Borrowed& b) const;
    juce::String moodText (const modalhelp::Borrowed& b) const;

    AudioEngine& engine;
    std::function<Target()> target;
    std::function<void (const std::vector<int>&)> select;
    Look look;

    modalhelp::Analysis result;
    std::vector<modalhelp::RelatedKey> keys;
    std::vector<modalhelp::Borrowed> chords;
    std::vector<int> currentKeys;              // the selected chord's keys
    std::uint64_t seen = 0;                    // what the last analysis read (the selection's keys and its context)
    int keyRow = 0, hoveredKey = -1, hoveredChord = -1, hoveredSide = -1, keyScroll = 0, chordScroll = 0;
    std::optional<int> forcedTonic;
    std::optional<modalhelp::Mode> forcedMode;
    modalhelp::Extension extension = modalhelp::Extension::triad;
    modalhelp::Inversion inversion = modalhelp::Inversion::voiceLed;
    bool audition = true;
    int computeCount = 0;

    // the preview: after a short dwell on a row, the selected chord, then the hovered one
    int previewRow = -1, previewStage = 0, previewTicks = 0;
    std::vector<int> sounding;
    int soundingChannel = -1;
};
} // namespace lynx
