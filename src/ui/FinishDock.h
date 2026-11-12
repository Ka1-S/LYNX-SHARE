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
    static constexpr int width = 300;

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
    std::shared_ptr<std::atomic<int>> measuring; // Auto level's render: 0 idle, 1 running (shared with its thread)
    std::shared_ptr<std::atomic<float>> measuredLufs;
    bool updating = false;
};
} // namespace lynx
