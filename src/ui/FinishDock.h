#pragma once

// The Simple workflow's Finish dock (round 189k, slice S8; design: /mnt/e/lynx-work/simple184a/DESIGN.md §2.3.1): the
// Master looks. Eight looks, an intensity slider (Light / Normal / Heavy), Tone (Low / Mid / High), Auto level and
// "Hear without". A look is a short chain of FL's own effects on the Master (core/MasterLooks.h), so everything it does
// is visible and editable in the Mixer and exports to FL Studio as those plugins.

#include <atomic>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"

namespace lynx
{
class AudioEngine;

class FinishDock final : public juce::Component,
                         private juce::Timer
{
public:
    // tests and --simplegb190keshot: the fade switch as a click sets it
    void setFadeForCheck (bool on, double seconds) { setFade (on, seconds); }
    static constexpr int width = 360; // (round 190jc, SG-16: 300 -> 360, look tiles near BandLab Mastering's 182 x 76)

    // What "Save the song as a file" opens (round 190t): the Export window on one format (0 WAV, 1 MP3) and
    // File › Export › For other apps. Without them the block is not shown.
    struct Saving
    {
        std::function<void (int format)> exportAudio;
        std::function<void()> exportOtherApps;
        // round 190ch: the Reference match window (ui/ReferenceMatchPanel.h); without it the button is not shown
        std::function<void()> referenceMatch;
    };

    FinishDock (AudioEngine& engine, std::function<void()> onClose, Saving saving = {});
    ~FinishDock() override;

    // the song changed (undo, a Mixer edit, another project): the dock shows what the Master has now
    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class LookTile;
    class SaveRow;
    void save (int format); // the look is switched back on first: the file is what "Hear without" is not
    void choose (int id);
    void changed (bool structural); // the intensity / tone moved: the chain follows (one undo step per drag)
    void autoLevel();
    // round 190ld (DS-44, owner decision S7): "Hear without" plays the song without the look at the look's loudness
    // (both measured offline, once per look setting); the raw level is one right-click away
    void updateListenGain();
    void measureMatch();
    void mouseDown (const juce::MouseEvent& e) override;
    void setFade (bool on, double seconds); // round 190ke: the song's fade-out (core/SongFade.h)
    void refreshFade();
    void timerCallback() override;

    AudioEngine& engine;
    std::function<void()> onClose;
    MasterLookSettings shown;
    Saving saving;
    std::unique_ptr<LookTile> tiles[8];
    std::unique_ptr<SaveRow> saveRows[2]; // WAV, MP3
    juce::Label title, status, intensityLabel, toneLabel, levelInfo, peakHint, saveLabel;
    juce::TextButton closeButton, hearWithout, autoButton, removeButton, otherApps, matchButton;
    juce::Slider intensity, tone[3];
    juce::Label toneNames[3], marks[3];
    // round 190ke (owner decision D5, GarageBand's Mix › Fade Out): "Fade out the end" and its length
    juce::ToggleButton fadeButton;
    juce::ComboBox fadeLength;
    juce::Label fadeInfo;
    std::shared_ptr<std::atomic<int>> measuring; // Auto level's render: 0 idle, 1 running (shared with its thread)
    std::shared_ptr<std::atomic<float>> measuredLufs;
    // round 190ld: the loudness match's render (0 idle, 1 running, 2 done) and its result (dB to add to the A side)
    std::shared_ptr<std::atomic<int>> matching;
    std::shared_ptr<std::atomic<float>> matchDb;
    MasterLookSettings matchedFor { -1 }; // the look setting matchDb belongs to (id -1: none yet)
    bool matchValid = false;
public:
    static inline bool matchLoudness = true; // right-click on "Hear without": off = hear the raw level
private:
    bool updating = false;
};
} // namespace lynx
