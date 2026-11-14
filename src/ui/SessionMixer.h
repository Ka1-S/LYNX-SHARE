#pragma once

// The Session mixer under the clip grid (round 190dr, Clip-launch workflow; model in core/SessionMixer.h): a strip per
// grid column with Live's sections In/Out, Sends, Track Options and Volume, and the Main strip under the scene column
// [SHOT ableton/probe/p3/session1.png]. The clip grid owns it, places it under its status row and scrolls it with its
// columns. Painted in one component (no child per strip); its meters tick with the grid's timer.

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MeterBallistics.h"
#include "core/SessionMixer.h"
#include "ui/ValueControl.h"
#include "ui/MapMode.h"

namespace lynx
{
class AudioEngine;

class SessionMixerComponent final : public juce::Component, public juce::TooltipClient, public mapmode::SpotSource
{
public:
    // round 190ef: the volume, pan and send knobs are mappable (MIDI / Key Map Mode)
    void mapSpots (std::vector<mapmode::Spot>& out) override;
    explicit SessionMixerComponent (AudioEngine& engine);

    // The grid's columns: `count` columns `columnWidth` wide, scrolled by `scrollX`, in [0, tracksWidth); the Main strip
    // is `mainWidth` wide to the right of the tracks.
    void setColumns (int count, int columnWidth, int scrollX, int tracksWidth, int mainWidth);
    // The height the shown sections take when at most `available` px are free (0: the mixer is hidden).
    int wantedHeight (int available) const;
    // True when the Volume section (with each track's Arm button) is on screen: the grid's status row then shows no arm.
    bool armsShown() const noexcept { return sections.volume > 0; }
    // One display tick of the meters (the grid's 30 Hz timer while it is on screen). True when a meter moved (round
    // 190ge: the grid repaints the mixer only then, or while the song plays).
    bool tick();

    // Live's View > Mixer and View > Mixer Controls items, on the grid's Mixer button and in its options menu.
    void addViewItems (juce::PopupMenu& menu);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    // the parts of a strip (also what --liveshot190dr checks)
    enum class Part { none, inType, inChannel, monIn, monAuto, monOff, output, send, delay, volume, fader, pan, activator, solo, arm };
    struct Hit
    {
        int column = -1; // the grid column; -2 = the Main strip
        Part part = Part::none;
        int row = 0;     // Sends: the row
        bool operator== (const Hit&) const = default;
    };
    // where a part is drawn (empty when it is not shown); `column` -2 = Main
    juce::Rectangle<int> partBounds (int column, Part part, int row = 0) const;
    Hit hitAt (juce::Point<int> p) const;
    const std::vector<int>& sendTargets() const noexcept { return rows; }

private:
    void refreshCache() const;
    sessionmixer::Strip stripFor (int column) const;
    int insertFor (int column) const; // Main: 0
    int stripX (int column) const noexcept;
    int stripWidth (int column) const noexcept { return column == mainColumn ? mainWidth : columnWidth; }
    void layoutSections();
    ui::ValueSpec specFor (const Hit& hit) const;
    double valueFor (const Hit& hit) const;
    void setValue (const Hit& hit, double v);
    void paintStrip (juce::Graphics& g, int column);
    void paintMeter (juce::Graphics& g, int insert, juce::Rectangle<int> area) const;
    void showInputMenu (int insert, bool channels);
    void showOutputMenu (int insert);
    juce::String tipFor (const Hit& hit) const;

    static constexpr int mainColumn = -2;
    AudioEngine& engine;
    int columns = 0, columnWidth = 104, scroll = 0, tracksWidth = 0, mainWidth = 128;
    sessionmixer::Sections sections;
    std::vector<int> rows;                            // the Sends rows shown: their target inserts
    mutable std::vector<sessionmixer::Strip> strips;  // per column (refreshCache)
    mutable std::vector<int> allRows;                 // the Sends rows the strips would show
    mutable std::uint64_t cacheStamp = ~std::uint64_t (0);
    std::vector<meters::PeakHold> levels;             // per insert, left / right
    ui::ValueDrag drag;
    Hit dragging, hover;
};
} // namespace lynx
