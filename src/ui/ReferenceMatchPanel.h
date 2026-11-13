#pragma once

// Reference match (round 190ch, LYNX's own; the measuring and the suggestion are core/ReferenceMatch.h): pick a
// finished track you like, measure it and your song (loudness, true peak, third-octave spectrum, stereo width), see
// both curves, and let Match put a suggested EQ curve and the loudness gain on the Master.
//
// What Match adds is two ordinary Master slots named "Reference match" — Facet EQ and Apex Limiter — so the result is
// a preset you can open and change, not a hidden process. Everything is measured on this computer.
//
// Opens as a tool window in every layout (Tools menu) and from the Simple layout's Finish dock.

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ReferenceMatch.h"

namespace lynx
{
class AudioEngine;

class ReferenceMatchPanel final : public juce::Component,
                                  private juce::Timer
{
public:
    static constexpr int defaultWidth = 760, defaultHeight = 600;

    struct Hooks
    {
        std::function<void()> showMaster; // "Open in the Mixer": the Master's slots
    };

    ReferenceMatchPanel (AudioEngine& engine, Hooks hooks);
    ~ReferenceMatchPanel() override;

    void refresh(); // the song changed: the buttons follow what the Master holds

    // The measuring a Match needs, also for tests of the whole route: the song rendered offline (with the match slots
    // off, or on for `withMatch`) and measured. Any thread but the audio thread; invalid when the render failed.
    static refmatch::Analysis measureSong (Song song, bool withMatch);
    // A file measured (through the sample library). Invalid when it can't be read.
    static refmatch::Analysis measureFile (AudioEngine& engine, const juce::File& file);

    // --bandlabshot: measurements put in place without a file dialog or a thread
    void showForSnapshot (const juce::String& referenceName, const refmatch::Analysis& song, const refmatch::Analysis& reference,
                          const refmatch::Analysis* after);
    bool matchForSnapshot();
    const refmatch::Suggestion& suggestionForSnapshot() const noexcept { return suggestion; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Graph;
    struct Shared;
    void timerCallback() override;
    void chooseReference();
    void start (int job); // 1 reference, 2 song, 3 result
    void recompute();
    void match();
    void updateTexts();
    void resultMeasured (const refmatch::Analysis& result);

    AudioEngine& engine;
    Hooks hooks;
    std::shared_ptr<Shared> shared;
    std::unique_ptr<Graph> graph;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File referenceFile;
    juce::String referenceName;
    refmatch::Analysis song, reference, after;
    bool hasAfter = false;
    // The measured result against the reference's loudness: a limiter that has to hold peaks back leaves the song
    // quieter than asked, so what is missing is offered as a trim of the level the next Match applies.
    double trimDb = 0.0, appliedTrimDb = 0.0, resultOffDb = 0.0;
    refmatch::Suggestion suggestion;
    juce::Label title, status, fileLabel, amountLabel, note;
    juce::Label statHead[4], statName[4], statCell[4][3];
    juce::TextButton chooseButton, measureButton, matchButton, checkButton, hearWithout, removeButton, masterButton;
    juce::ToggleButton loudnessToggle;
    juce::Slider amount;
    bool updating = false;
};
} // namespace lynx
