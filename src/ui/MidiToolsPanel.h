#pragma once

// The MIDI Tools panel (round 190s; docs/analise/REA_rodada190o.md §1, GAPMAP-0126 D7): Ableton Live 12's Transform
// section of the Clip View as a LYNX addition beside FL's Piano roll tools (which stay as they are). One tool at a time
// from a chooser, the tool's controls with Live's defaults, an Auto toggle (on by default) and a Transform button.
//
// Auto on: every control change runs the tool again on the notes as they were before the tool, so the notes always
// show the current settings only [LIVE]. Transform makes the result final: the next change starts from the notes as
// they are now. Auto off: nothing happens until Transform. Each change is an undo step (one per mouse press).
// The maths is core/MidiTools.h. Own design and art; shown in the Clip View's column of the Clip-launch workflow and
// from the Piano roll's Tools menu.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiTools.h"
#include "engine/AudioEngine.h"

namespace lynx
{
class MidiToolsPanel final : public juce::Component
{
public:
    // Live's Transform chooser, in its order (the tools LYNX has).
    enum Tool { arpeggiate, chop, connect, ornament, quantize, recombine, span, strum, timeWarp, velocityShaper, toolCount };
    static const char* toolName (int tool) noexcept; // English, for tr()

    // What the tool works on now: `indices` of `pattern`'s notes (the editor's selection, or every note it shows when
    // nothing is selected: `selected` false), and the editor's grid in ticks.
    struct Target
    {
        int pattern = -1;
        std::vector<int> indices;
        bool selected = false;
        std::int64_t grid = ppq / 4;
    };

    MidiToolsPanel (AudioEngine& engine, std::function<Target()> target, std::function<void (const std::vector<int>&)> select = {});
    ~MidiToolsPanel() override;

    static constexpr int panelWidth = 172, panelHeight = 190;

    void setTool (int tool);
    int getTool() const noexcept { return tool; }
    void transformNow(); // the Transform button
    void setAuto (bool on);
    // --detailshot: the settings of round 190aa's Live shots (Chop part 1 emphasised, Quantize 1/8 + 1/8T, Time Warp
    // breakpoint 2 at x2)
    void setLiveShotSettingsForSnapshot();

    void paint (juce::Graphics& g) override;
    void resized() override;

    struct Body; // the current tool's controls

private:
    void rebuild();
    void changed();          // a control moved
    void run();
    bool sessionHolds (const Target& t) const;
    miditools::Notes apply (const miditools::Notes& notes, std::int64_t grid) const;

    AudioEngine& engine;
    std::function<Target()> target;
    std::function<void (const std::vector<int>&)> select;

    int tool = arpeggiate;
    bool autoApply = true;

    // every tool's settings (kept while the panel lives, as Live keeps them per tool)
    miditools::ArpeggiateOptions arp;
    miditools::ChopOptions chopOptions;
    miditools::ConnectOptions connectOptions;
    miditools::OrnamentOptions ornamentOptions;
    miditools::QuantizeOptions quantizeOptions;
    miditools::RecombineOptions recombineOptions;
    miditools::SpanOptions spanOptions;
    miditools::StrumOptions strumOptions;
    miditools::TimeWarpOptions timeWarpOptions;
    miditools::VelocityShaperOptions shaperOptions;
    int warpSelected = 0; // Time Warp's selected breakpoint (its Time and Speed show)
    std::uint32_t seed = 1;
    juce::String warpTimeText (double share) const;

    // the notes before the tool, while the pattern still holds this panel's last result
    struct Session
    {
        bool valid = false, selected = false;
        int pattern = -1;
        std::vector<Note> base, output;
        std::vector<int> indices, outIndices;
        std::int64_t grid = ppq / 4;
    } session;

    juce::Label title;
    juce::ComboBox chooser;
    juce::TextButton resetButton, autoButton, transformButton;
    std::unique_ptr<Body> body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiToolsPanel)
};
} // namespace lynx
