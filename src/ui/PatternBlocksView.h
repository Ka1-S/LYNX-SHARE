#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"
#include "ui/ValueControl.h"

namespace lynx
{
class AudioEngine;
class PlaylistComponent;

// FL's legacy Pattern Blocks section (round 170f, issue #60; docs/analise/REA_rodada170f.md), shown under the
// Playlist's clip tracks while the project's "Pattern blocks" option is on. One row per pattern, in pattern order;
// it shares the Playlist's time zoom, scroll and playhead.
//  - divider (top edge): drag to resize the section, double-click: fit every row
//  - left click on a row: pencil places one block on the bar and drags it along its row, brush drags to paint
//    whole blocks (press a block and drag to move it; the block keeps the bar it was grabbed by under the mouse;
//    a selected block moves the whole selection, each block in its own row);
//    eraser click / drag: erases the blocks under the mouse, in whichever row the drag crosses;
//    mute click / drag: mutes the blocks under the mouse in any row (the first block decides: a muted one unmutes);
//    select: click / drag a box selects the blocks it meets in any row (Shift adds);
//    slice: click on a block cuts its pattern there (a new pattern takes the rest, FL 10's destructive slice);
//    zoom: drag zooms to that time range, a click zooms back; listen: press plays from there while held.
//    Right click / right drag erases the same way whatever the tool, except Select, Slice and Zoom (FL 10: either
//    button). Slip keeps the old paint behaviour (FL 10 had no Slip tool). Blocks can't change row, be resized or
//    slipped. (issue #127; docs/analise/REA_rodada190ht.md)
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
    // For snapshots and the gesture check: the middle of row `row` at `tick`, in this component's coordinates.
    juce::Point<int> pointForSnapshot (int row, std::int64_t tick) const;
    const std::vector<PatternBlock>& selectionForSnapshot() const noexcept { return selected; }

private:
    void scrollBarMoved (juce::ScrollBar*, double) override;
    juce::Rectangle<int> rowsArea() const; // grid + headers, under the divider
    int rowAt (int y) const;                // pattern index, or -1
    int rowTop (int row) const;
    std::int64_t tickAt (int x) const;
    bool inGrid (juce::Point<int> p) const;
    void eraseUnder (int row, std::int64_t fromTick, std::int64_t toTick); // only that row (the one under the mouse)

    AudioEngine& engine;
    PlaylistComponent& playlist;
    juce::ScrollBar vBar { true };
    int scrollY = 0;

    enum class Drag { none, divider, paint, erase, move, groupMove, mute, select, zoom, slice, listen };
    Drag drag = Drag::none;
    void beginMove (int row, std::int64_t tick, int index); // pencil / brush pressed on block `index`
    void muteUnder (int row, std::int64_t fromTick, std::int64_t toTick);
    void selectBand (juce::Point<int> from, juce::Point<int> to, bool add);
    juce::Rectangle<int> band;                 // Select / Zoom: the box or the time range being dragged
    std::vector<PatternBlock> selected;        // Select tool (not saved; pruned when the song changes)
    std::vector<PatternBlock> selectedBefore;  // Shift+Select: what was selected when the drag began
    PatternBlock grabbed;                      // groupMove: the block the mouse holds, where it is now
    bool muteDecided = false, muteTarget = true; // Mute: the first block crossed decides (gesture::MuteSweep)
    std::int64_t sliceTick = -1;               // Slice: where the cut goes (snapped like the Playlist's)
    double zoomBackPixels = 0.0, zoomBackScroll = 0.0; // Zoom: the view before the last drag zoom (a click goes back)
    int dragRow = -1;
    std::int64_t lastTick = 0;
    std::int64_t moveStart = 0;
    std::int64_t grabOffset = 0; // move: the bar under the press minus the block's start
    int dividerStartHeight = 0;
    int hoverRow = -1;
    bool hoverHeader = false, hoverDivider = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternBlocksView)
};
} // namespace lynx
