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

    FinishDock (AudioEngine& engine, std::function<void()> onClose);
    ~FinishDock() override;

    // the song changed (undo, a Mixer edit, another project): the dock shows what the Master has now
    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class LookTile;
    void choose (int id);
    void changed (bool structural); // the intensity / tone moved: the chain follows (one undo step per drag)
    void autoLevel();
    void timerCallback() override;

    AudioEngine& engine;
    std::function<void()> onClose;
    MasterLookSettings shown;
    std::unique_ptr<LookTile> tiles[8];
    juce::Label title, status, intensityLabel, toneLabel, levelInfo;
    juce::TextButton closeButton, hearWithout, autoButton, removeButton;
    juce::Slider intensity, tone[3];
    juce::Label toneNames[3], marks[3];
    std::shared_ptr<std::atomic<int>> measuring; // Auto level's render: 0 idle, 1 running (shared with its thread)
    std::shared_ptr<std::atomic<float>> measuredLufs;
    bool updating = false;
};
} // namespace lynx
