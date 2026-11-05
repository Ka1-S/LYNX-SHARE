#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ValueControl.h"

namespace lynx
{
class AudioEngine;
class PlaylistComponent;

// FL's legacy Pattern Blocks section (round 170f, issue #60; docs/analise/REA_rodada170f.md), shown under the
// Playlist's clip tracks while the project's "Pattern blocks" option is on. One row per pattern, in pattern order;
// it shares the Playlist's time zoom, scroll and playhead.
//  - divider (top edge): drag to resize the section, double-click: fit every row
//  - left click on a row: a block of that row's pattern on the bar (whole pattern, snapped to the bar); drag: paints
//    along the row; right click / right drag: erases. Blocks can't be moved, resized or sent to another row.
//  - header: click = current pattern, double click = open it, right click = Rename and color
//  - wheel: scrolls the rows; Shift / Ctrl / Alt+wheel go to the Playlist (time scroll, zoom)
class PatternBlocksView final : public juce::Component,
                                public ui::HintSource,
                                private juce::ScrollBar::Listener
{
public:
    PatternBlocksView (AudioEngine& engine, PlaylistComponent& playlist);
    ~PatternBlocksView() override;

    static constexpr int rowHeight = 18;
    static constexpr int dividerHeight = 6;
    int contentHeight() const; // every row

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    void refresh(); // the song changed: scroll range, repaint

private:
    void scrollBarMoved (juce::ScrollBar*, double) override;
    juce::Rectangle<int> rowsArea() const; // grid + headers, under the divider
    int rowAt (int y) const;                // pattern index, or -1
    int rowTop (int row) const;
    std::int64_t tickAt (int x) const;
    bool inGrid (juce::Point<int> p) const;

    AudioEngine& engine;
    PlaylistComponent& playlist;
    juce::ScrollBar vBar { true };
    int scrollY = 0;

    enum class Drag { none, divider, paint, erase };
    Drag drag = Drag::none;
    int dragRow = -1;
    std::int64_t lastTick = 0;
    int dividerStartHeight = 0;
    int hoverRow = -1;
    bool hoverHeader = false, hoverDivider = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternBlocksView)
};
} // namespace lynx
