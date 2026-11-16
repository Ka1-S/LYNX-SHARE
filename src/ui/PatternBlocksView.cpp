#include "ui/PatternBlocksView.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "core/PatternBlocks.h"
#include "engine/AudioEngine.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/PlaylistComponent.h"
#include "ui/TextCache.h"
#include "ui/Theme.h"

namespace lynx
{
PatternBlocksView::PatternBlocksView (AudioEngine& e, PlaylistComponent& p)
    : engine (e), playlist (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    vBar.addListener (this);
    vBar.setAutoHide (false);
    addAndMakeVisible (vBar);
}

PatternBlocksView::~PatternBlocksView() { vBar.removeListener (this); }

int PatternBlocksView::contentHeight() const { return (int) engine.getSong().patterns.size() * rowHeight; }

juce::Rectangle<int> PatternBlocksView::rowsArea() const
{
    return getLocalBounds().withTrimmedTop (dividerHeight).withTrimmedRight (PlaylistComponent::scrollBarSize);
}

int PatternBlocksView::rowTop (int row) const { return rowsArea().getY() + row * rowHeight - scrollY; }

int PatternBlocksView::rowAt (int y) const
{
    const auto area = rowsArea();
    if (y < area.getY() || y >= area.getBottom())
        return -1;
    const auto row = (y - area.getY() + scrollY) / rowHeight;
    return row >= 0 && row < (int) engine.getSong().patterns.size() ? row : -1;
}

std::int64_t PatternBlocksView::tickAt (int x) const
{
    return (std::int64_t) std::floor (juce::jmax (0.0, playlist.xToTick (x)));
}

bool PatternBlocksView::inGrid (juce::Point<int> p) const
{
    return p.x >= playlist.headerWidth && rowsArea().contains (p);
}

juce::Point<int> PatternBlocksView::pointForSnapshot (int row, std::int64_t tick) const
{
    return { juce::roundToInt (playlist.tickToX ((double) tick)) + 2, rowTop (row) + rowHeight / 2 };
}

void PatternBlocksView::eraseUnder (int row, std::int64_t fromTick, std::int64_t toTick)
{
    // issue #127: erasing takes the blocks under the mouse, so only the row it is over (the drag may cross rows);
    // and only when a block of that row meets the span (no empty undo steps)
    if (row >= 0 && patternblocks::anyInRange (engine.getSong(), row, fromTick, toTick))
        engine.editSong ([row, fromTick, toTick] (Song& s) { patternblocks::eraseRange (s, row, fromTick, toTick); });
}

void PatternBlocksView::muteUnder (int row, std::int64_t fromTick, std::int64_t toTick)
{
    // issue #127 (FL 10): the Mute tool mutes the blocks under the mouse in whichever row it crosses; the first block it
    // meets decides whether the sweep mutes or unmutes (on empty space it mutes), as the Playlist's Mute tool
    if (row < 0)
        return;
    const auto& song = engine.getSong();
    std::vector<std::size_t> change;
    for (auto i : patternblocks::blocksInRange (song, row, fromTick, toTick))
    {
        if (! muteDecided)
        {
            muteTarget = ! song.blocks[i].muted;
            muteDecided = true;
        }
        if (song.blocks[i].muted != muteTarget)
            change.push_back (i);
    }
    if (! change.empty())
        engine.editSong ([change, m = muteTarget] (Song& s) {
            for (auto i : change)
                patternblocks::setMuted (s, i, m);
        });
}

void PatternBlocksView::selectBand (juce::Point<int> from, juce::Point<int> to, bool add)
{
    // issue #127 (FL 10): the Select tool's box takes every block it meets, in any row
    const auto area = rowsArea();
    const auto clampY = [&area] (int y) { return juce::jlimit (area.getY(), area.getBottom() - 1, y); };
    band = juce::Rectangle<int> (from, to).getIntersection (area.withTrimmedLeft (playlist.headerWidth));
    const auto rows = (int) engine.getSong().patterns.size();
    auto r0 = rowAt (clampY (juce::jmin (from.y, to.y))), r1 = rowAt (clampY (juce::jmax (from.y, to.y)));
    if (r0 < 0)
        r0 = 0;
    if (r1 < 0)
        r1 = rows - 1;
    auto found = patternblocks::inArea (engine.getSong(), r0, r1, tickAt (juce::jmin (from.x, to.x)), tickAt (juce::jmax (from.x, to.x)));
    selected = add ? selectedBefore : std::vector<PatternBlock> {};
    for (const auto& b : found)
        if (! patternblocks::contains (selected, b))
            selected.push_back (b);
}

void PatternBlocksView::beginMove (int row, std::int64_t tick, int index)
{
    const auto& song = engine.getSong();
    const auto block = song.blocks[(std::size_t) index];
    grabOffset = patternblocks::grabOffset (song, row, tick);
    if (selected.size() > 1 && patternblocks::contains (selected, block))
    {
        drag = Drag::groupMove; // FL 10: a selected block moves the selection with it
        grabbed = block;
        return;
    }
    selected.clear();
    drag = Drag::move;
    moveStart = block.start;
}

void PatternBlocksView::refresh()
{
    // the selection holds only blocks that still exist (undo, a pattern deleted, another project)
    const auto& song = engine.getSong();
    selected.erase (std::remove_if (selected.begin(), selected.end(), [&song] (const PatternBlock& b) { return patternblocks::indexOf (song, b) < 0; }),
                    selected.end());
    const auto area = rowsArea();
    vBar.setRangeLimits (0.0, (double) juce::jmax (contentHeight(), area.getHeight()), juce::dontSendNotification);
    vBar.setCurrentRange ((double) scrollY, (double) area.getHeight(), juce::dontSendNotification);
    scrollY = (int) vBar.getCurrentRangeStart();
    repaint();
}

void PatternBlocksView::resized()
{
    vBar.setBounds (getWidth() - PlaylistComponent::scrollBarSize, dividerHeight, PlaylistComponent::scrollBarSize,
                    juce::jmax (0, getHeight() - dividerHeight));
    refresh();
}

void PatternBlocksView::scrollBarMoved (juce::ScrollBar*, double start)
{
    scrollY = (int) start;
    repaint();
}

void PatternBlocksView::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto& song = engine.getSong();
    const auto area = rowsArea();
    const auto grid = area.withTrimmedLeft (playlist.headerWidth);
    g.fillAll (colour::bg1);

    // the divider: FL's line between the clip tracks and the block rows (drag it)
    {
        const auto strip = juce::Rectangle<int> (0, 0, getWidth(), dividerHeight);
        g.setColour (hoverDivider || drag == Drag::divider ? colour::bg5 : colour::bg3);
        g.fillRect (strip);
        g.setColour (colour::borderStrong);
        g.fillRect (0, dividerHeight - 1, getWidth(), 1);
        g.setColour (colour::textSecondary.withAlpha (0.6f));
        for (int i = -2; i <= 2; ++i)
            g.fillEllipse ((float) (getWidth() / 2 + i * 6) - 1.0f, (float) dividerHeight * 0.5f - 1.5f, 2.0f, 2.0f);
    }

    const auto rows = (int) song.patterns.size();
    const auto firstRow = juce::jmax (0, scrollY / rowHeight);
    const auto lastRow = juce::jmin (rows - 1, (scrollY + area.getHeight()) / rowHeight);

    // --- rows and bar lines ----------------------------------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid);
        for (int r = firstRow; r <= lastRow; ++r)
            if (r % 2 == 1)
            {
                g.setColour (colour::bg2.withAlpha (0.55f));
                g.fillRect (grid.getX(), rowTop (r), grid.getWidth(), rowHeight);
            }

        const auto from = (std::int64_t) juce::jmax (0.0, playlist.xToTick (grid.getX()));
        const auto to = (std::int64_t) playlist.xToTick (grid.getRight()) + 1;
        const auto bars = song.barsBetween (from, to);
        const auto barPx = bars.empty() ? 0.0 : (double) bars.front().length * playlist.pixelsPerTick;
        for (const auto& b : bars)
        {
            if (barPx < 4.0 && (b.number - 1) % 4 != 0)
                continue;
            const auto x = (float) playlist.tickToX ((double) b.tick);
            g.setColour ((b.number - 1) % 4 == 0 ? colour::borderStrong : colour::border);
            g.fillRect (juce::Rectangle<float> (x, (float) grid.getY(), 1.0f, (float) grid.getHeight()));
        }
        for (int r = firstRow; r <= lastRow + 1; ++r)
        {
            g.setColour (colour::border.withAlpha (0.7f));
            g.fillRect (grid.getX(), rowTop (r) - 1, grid.getWidth(), 1);
        }

        // --- the blocks: solid, in their pattern's colour (FL: "the blocks are more minimalist") ------------------
        const auto& blocks = song.blocks;
        for (std::size_t i = 0; i < blocks.size(); ++i)
        {
            const auto& b = blocks[i];
            if (b.pattern < firstRow || b.pattern > lastRow)
                continue;
            const auto x0 = playlist.tickToX ((double) b.start);
            const auto x1 = playlist.tickToX ((double) patternblocks::playedEnd (song, blocks, i));
            if (x1 < grid.getX() || x0 > grid.getRight())
                continue;
            const auto box = juce::Rectangle<float> ((float) x0, (float) rowTop (b.pattern) + 1.0f, (float) juce::jmax (1.0, x1 - x0),
                                                     (float) rowHeight - 3.0f);
            const auto c = PlaylistComponent::patternColour (song, b.pattern);
            if (b.muted) // the Mute tool (issue #127): faded, as the Playlist draws a muted clip
            {
                g.setColour (c.withMultipliedSaturation (0.35f).withAlpha (0.35f));
                g.fillRect (box);
                g.setColour (c.withAlpha (0.6f));
                g.drawRect (box, 1.0f);
            }
            else
            {
                g.setColour (c);
                g.fillRect (box);
                g.setColour (c.darker (0.55f)); // a thin edge so touching blocks stay countable
                g.drawRect (box, 1.0f);
            }
            if (patternblocks::contains (selected, b)) // the Select tool's selection
            {
                g.setColour (colour::textPrimary);
                g.drawRect (box.expanded (0.5f, 0.0f), 1.5f);
            }
        }

        // the Select tool's box, the Zoom tool's range and the Slice tool's cut
        if (! band.isEmpty())
        {
            g.setColour (colour::accent.withAlpha (0.12f));
            g.fillRect (band);
            g.setColour (colour::accent.withAlpha (0.8f));
            g.drawRect (band, 1);
        }
        if (drag == Drag::slice && sliceTick >= 0)
        {
            const auto x = (float) playlist.tickToX ((double) sliceTick);
            const auto r = rowAt (getMouseXYRelative().y) >= 0 ? rowAt (getMouseXYRelative().y) : dragRow;
            g.setColour (colour::accent);
            if (r >= 0)
                g.fillRect (juce::Rectangle<float> (x - 0.75f, (float) rowTop (r), 1.5f, (float) rowHeight));
            g.setColour (colour::accent.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (x - 0.5f, (float) grid.getY(), 1.0f, (float) grid.getHeight()));
        }

        // the row under the mouse while painting / erasing
        if (hoverRow >= 0 && ! hoverHeader && inGrid (getMouseXYRelative()))
        {
            g.setColour (colour::textPrimary.withAlpha (0.05f));
            g.fillRect (grid.getX(), rowTop (hoverRow), grid.getWidth(), rowHeight);
        }

        if (playlist.playheadTick >= 0.0)
        {
            g.setColour (colour::playhead);
            g.fillRect (juce::Rectangle<float> ((float) playlist.tickToX (playlist.playheadTick) - 0.5f, (float) grid.getY(), 1.5f,
                                                (float) grid.getHeight()));
        }
    }

    // --- headers: one per pattern ---------------------------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state (g);
        const auto column = area.withWidth (playlist.headerWidth);
        g.reduceClipRegion (column);
        g.setColour (colour::bg2);
        g.fillRect (column);
        g.setFont (font (type::caption));
        for (int r = firstRow; r <= lastRow; ++r)
        {
            const auto y = rowTop (r);
            const auto current = r == song.currentPattern;
            auto plate = current ? colour::bg4 : colour::bg3;
            if (hoverHeader && hoverRow == r)
                plate = plate.interpolatedWith (colour::bg5, 0.6f);
            const auto box = juce::Rectangle<float> (2.0f, (float) y + 1.0f, (float) playlist.headerWidth - 4.0f, (float) rowHeight - 2.0f);
            g.setColour (plate);
            g.fillRoundedRectangle (box, radius::sm);
            g.setColour (PlaylistComponent::patternColour (song, r));
            g.fillRoundedRectangle (box.withWidth (4.0f).reduced (0.0f, 2.0f), 1.5f);
            if (current)
            {
                g.setColour (colour::accent);
                g.fillRect (box.withX (box.getRight() - 3.0f).withWidth (2.0f).reduced (0.0f, 3.0f));
            }
            g.setColour (current ? colour::textPrimary : colour::textSecondary);
            const auto& name = song.patterns[(std::size_t) r].name;
            textcache::drawFittedText (g, name.empty() ? juce::String (r + 1) : juce::String::fromUTF8 (name.c_str()), 10,
                                       y, playlist.headerWidth - 16, rowHeight, juce::Justification::centredLeft, 1, 0.8f);
        }
        g.setColour (colour::border);
        g.fillRect (playlist.headerWidth - 1, area.getY(), 1, area.getHeight());
    }
}

void PatternBlocksView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    drag = Drag::none;
    moveStart = 0;
    grabOffset = 0;
    if (p.y < dividerHeight)
    {
        drag = Drag::divider;
        dividerStartHeight = getHeight();
        return;
    }
    const auto row = rowAt (p.y);
    if (row < 0)
        return;

    if (p.x < playlist.headerWidth)
    {
        if (e.mods.isPopupMenu())
        {
            engine.editSong ([row] (Song& s) { s.currentPattern = row; }, false);
            if (playlist.onRenamePattern)
                playlist.onRenamePattern (row);
            return;
        }
        engine.editSong ([row] (Song& s) { s.currentPattern = row; }, false);
        return;
    }
    if (! inGrid (p))
        return;

    dragRow = row;
    lastTick = tickAt (p.x);
    const auto& song = engine.getSong();
    const auto tool = playlist.currentTool();
    // FL 10 (issue #127): Select, Slice and Zoom take either button; every other tool erases with the right one
    const auto eitherButton = tool == EditTool::lasso || tool == EditTool::knife || tool == EditTool::zoom;
    if (e.mods.isPopupMenu() && ! eitherButton)
    {
        // right click / right drag erases, whatever the tool (the Playlist grammar)
        drag = Drag::erase;
        eraseUnder (row, lastTick, lastTick);
        repaint();
        return;
    }
    if (tool == EditTool::eraser)
    {
        // deleting is not selecting: the current pattern stays as it is
        drag = Drag::erase;
        eraseUnder (row, lastTick, lastTick);
    }
    else if (tool == EditTool::mute)
    {
        drag = Drag::mute;
        muteDecided = false;
        muteTarget = true; // (started on empty space: the sweep mutes)
        muteUnder (row, lastTick, lastTick);
    }
    else if (tool == EditTool::lasso)
    {
        drag = Drag::select;
        selectedBefore = e.mods.isShiftDown() ? selected : std::vector<PatternBlock> {};
        band = {};
    }
    else if (tool == EditTool::knife)
    {
        drag = Drag::slice;
        sliceTick = juce::jmax<std::int64_t> (0, playlist.snap (playlist.xToTick (p.x), ! e.mods.isAltDown()));
    }
    else if (tool == EditTool::zoom)
    {
        drag = Drag::zoom;
        band = {};
    }
    else if (tool == EditTool::listen)
    {
        // as the Playlist's Listen tool: play from there while the button is held
        drag = Drag::listen;
        playlist.seekTo (p.x, ! e.mods.isAltDown());
        engine.play();
    }
    else if (tool == EditTool::pencil)
    {
        // the row is the pattern: painting makes it the current one too (the Channel Rack shows what is painted)
        if (song.currentPattern != row)
            engine.editSong ([row] (Song& s) { s.currentPattern = row; }, false);
        if (const auto at = patternblocks::blockAt (song, row, lastTick); at >= 0)
            beginMove (row, lastTick, at);
        else if (! patternblocks::fits (song, row, patternblocks::snapToBar (song, lastTick)))
        {
            drag = Drag::none; // it would not fit: no edit, no empty undo step
        }
        else
        {
            selected.clear();
            auto placed = false;
            engine.editSong ([row, t = lastTick, &placed] (Song& s) { placed = patternblocks::place (s, row, t); });
            if (placed)
            {
                drag = Drag::move;
                moveStart = patternblocks::snapToBar (song, lastTick);
            }
            else
                drag = Drag::none; // nowhere to move from: no stuck move
        }
    }
    else if (tool == EditTool::brush)
    {
        if (song.currentPattern != row)
            engine.editSong ([row] (Song& s) { s.currentPattern = row; }, false);
        if (const auto at = patternblocks::blockAt (song, row, lastTick); at >= 0)
            beginMove (row, lastTick, at);
        else
        {
            selected.clear();
            drag = Drag::paint;
            // no immediate place where it would not fit (the drag can still paint further along)
            if (patternblocks::fits (song, row, patternblocks::snapToBar (song, lastTick))
                && patternblocks::blockAt (engine.getSong(), row, lastTick) < 0)
                engine.editSong ([row, t = lastTick] (Song& s) { patternblocks::place (s, row, t); });
        }
    }
    else
    {
        // Slip (FL 10 had none) and the Piano roll's drum brush: the first version's paint
        drag = Drag::paint;
        if (engine.getSong().currentPattern != row)
            engine.editSong ([row] (Song& s) { s.currentPattern = row; }, false);
        if (patternblocks::blockAt (engine.getSong(), row, lastTick) < 0)
            engine.editSong ([row, t = lastTick] (Song& s) { patternblocks::place (s, row, t); });
    }
    repaint();
}

void PatternBlocksView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::divider)
    {
        playlist.setBlocksHeight (dividerStartHeight - e.getDistanceFromDragStartY());
        return;
    }
    if (drag == Drag::none)
        return;
    const auto tick = tickAt (e.x);
    const auto from = lastTick;
    lastTick = tick;
    const auto row = dragRow;
    if (drag == Drag::select)
    {
        if (e.getDistanceFromDragStart() >= 3 || ! band.isEmpty())
            selectBand (e.getMouseDownPosition(), e.getPosition(), true);
    }
    else if (drag == Drag::zoom)
    {
        const auto grid = rowsArea().withTrimmedLeft (playlist.headerWidth);
        const auto x0 = juce::jlimit (grid.getX(), grid.getRight(), juce::jmin (e.getMouseDownX(), e.x));
        const auto x1 = juce::jlimit (grid.getX(), grid.getRight(), juce::jmax (e.getMouseDownX(), e.x));
        band = juce::Rectangle<int>::leftTopRightBottom (x0, grid.getY(), x1, grid.getBottom());
    }
    else if (drag == Drag::slice)
    {
        // the cut stays where it was pressed; the row it applies to is the one under the mouse when released
    }
    else if (drag == Drag::listen)
    {
        playlist.seekTo (e.x, ! e.mods.isAltDown());
    }
    else if (drag == Drag::mute)
    {
        muteUnder (rowAt (e.y), juce::jmin (from, tick), juce::jmax (from, tick));
    }
    else if (drag == Drag::groupMove)
    {
        const auto& song = engine.getSong();
        const auto target = patternblocks::dragTarget (song, tick, grabOffset);
        if (target != grabbed.start)
        {
            std::optional<std::vector<PatternBlock>> moved;
            engine.editSong ([group = selected, delta = target - grabbed.start, &moved] (Song& s) { moved = patternblocks::moveGroup (s, group, delta); });
            if (moved)
            {
                for (std::size_t i = 0; i < selected.size(); ++i)
                    if (selected[i].pattern == grabbed.pattern && selected[i].start == grabbed.start)
                        grabbed = (*moved)[i];
                selected = *moved;
            }
        }
    }
    else if (drag == Drag::move)
    {
        // the block stays in dragRow, whatever row the mouse is over
        const auto& song = engine.getSong();
        const auto target = patternblocks::dragTarget (song, tick, grabOffset); // (the grabbed bar stays under the mouse)
        if (target != moveStart)
        {
            // a cheap pre-check against a pointless undo step; when the target overlaps the moving
            // block's own span, move decides (a rare self-overlap collision may coalesce one step)
            const auto at = patternblocks::blockAt (song, row, moveStart);
            const auto ownEnd = at >= 0 ? patternblocks::playedEnd (song, song.blocks, (std::size_t) at) : moveStart;
            const auto overlapsOwn = target < ownEnd && moveStart < target + patternblocks::lengthOf (song, row);
            if (overlapsOwn || patternblocks::fits (song, row, target))
            {
                std::int64_t atTick = -1;
                engine.editSong ([row = dragRow, from = moveStart, to = target, &atTick] (Song& s) { atTick = patternblocks::move (s, row, from, to); });
                if (atTick >= 0)
                    moveStart = atTick;
                else
                    drag = Drag::none; // the block is gone: stop the drag
            }
        }
    }
    else if (drag == Drag::paint)
    {
        // only where a whole block fits: the bars already covered stay as they are
        auto fits = false;
        const auto& song = engine.getSong();
        for (auto bar = song.barAt (juce::jmin (from, tick)); bar.tick <= juce::jmax (from, tick) && bar.length > 0 && ! fits;
             bar = song.barAt (bar.tick + bar.length))
            fits = patternblocks::fits (song, row, bar.tick);
        if (fits)
            engine.editSong ([row, from, tick] (Song& s) { patternblocks::paintRange (s, row, from, tick); });
    }
    else
    {
        // the swept span, in the row the mouse is over now
        eraseUnder (rowAt (e.y), juce::jmin (from, tick), juce::jmax (from, tick));
    }
    // the view follows a drag past its edges, like the Playlist (not a zoom range or a held Listen)
    const auto grid = rowsArea().withTrimmedLeft (playlist.headerWidth);
    if (drag != Drag::zoom && drag != Drag::listen && drag != Drag::slice && (e.x > grid.getRight() - 4 || e.x < grid.getX() + 4))
        playlist.scrollTicksBy (e.x > grid.getRight() - 4 ? 24.0 / playlist.pixelsPerTick : -24.0 / playlist.pixelsPerTick);
    repaint();
}

void PatternBlocksView::mouseUp (const juce::MouseEvent& e)
{
    const auto ended = drag;
    drag = Drag::none;
    if (ended == Drag::select && band.isEmpty())
    {
        // a click: the block under it alone (Shift: added or taken out), or nothing on empty space
        const auto row = rowAt (e.getMouseDownY());
        const auto at = row >= 0 ? patternblocks::blockAt (engine.getSong(), row, tickAt (e.getMouseDownX())) : -1;
        if (at < 0)
            selected = selectedBefore;
        else
        {
            const auto b = engine.getSong().blocks[(std::size_t) at];
            selected = selectedBefore;
            if (patternblocks::contains (selected, b))
                selected.erase (std::remove_if (selected.begin(), selected.end(),
                                                [&b] (const PatternBlock& x) { return x.pattern == b.pattern && x.start == b.start; }),
                                selected.end());
            else
                selected.push_back (b);
        }
    }
    else if (ended == Drag::zoom)
    {
        if (band.getWidth() > 4)
        {
            zoomBackPixels = playlist.pixelsPerTick; // FL 10: a click afterwards goes back to this view
            zoomBackScroll = playlist.scrollTicks;
            playlist.zoomToTicks (playlist.xToTick (band.getX()), playlist.xToTick (band.getRight()));
        }
        else if (zoomBackPixels > 0.0)
        {
            playlist.pixelsPerTick = zoomBackPixels;
            playlist.scrollTicks = zoomBackScroll;
            zoomBackPixels = 0.0;
            playlist.updateScrollBars();
            playlist.repaint();
        }
        else
            playlist.zoomOut (e.x);
    }
    else if (ended == Drag::slice)
    {
        // FL 10's Slice on a block: the pattern of the row under the mouse is cut there, a new pattern takes the rest
        auto row = rowAt (e.y);
        if (row < 0)
            row = dragRow;
        const auto& song = engine.getSong();
        if (const auto at = row >= 0 ? patternblocks::blockAt (song, row, sliceTick) : -1; at >= 0)
        {
            const auto cut = sliceTick - song.blocks[(std::size_t) at].start;
            if (cut > 0 && cut < patternblocks::lengthOf (song, row))
                engine.editSong ([row, cut] (Song& s) { patternblocks::slice (s, row, cut); });
        }
    }
    else if (ended == Drag::listen)
        engine.stop();
    band = {};
    sliceTick = -1;
    dragRow = -1;
    moveStart = 0;
    grabOffset = 0;
    repaint();
}

void PatternBlocksView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const auto divider = p.y < dividerHeight;
    const auto row = divider ? -1 : rowAt (p.y);
    const auto header = p.x < playlist.headerWidth;
    if (divider != hoverDivider || row != hoverRow || header != hoverHeader)
    {
        hoverDivider = divider;
        hoverRow = row;
        hoverHeader = header;
        repaint();
    }
    setMouseCursor (divider ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void PatternBlocksView::mouseExit (const juce::MouseEvent&)
{
    hoverDivider = hoverHeader = false;
    hoverRow = -1;
    repaint();
}

void PatternBlocksView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (p.y < dividerHeight)
    {
        playlist.setBlocksHeight (contentHeight() + dividerHeight); // fit every row
        return;
    }
    const auto row = rowAt (p.y);
    if (row >= 0 && p.x < playlist.headerWidth && ! e.mods.isPopupMenu())
    {
        engine.editSong ([row] (Song& s) { s.currentPattern = row; }, false);
        if (playlist.onOpenPattern)
            playlist.onOpenPattern();
    }
}

void PatternBlocksView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isAltDown() || e.mods.isCommandDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        playlist.mouseWheelMove (e.getEventRelativeTo (&playlist), wheel); // time scroll / zoom: the Playlist's own
        return;
    }
    const auto step = (int) std::lround ((wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * rowHeight * 6.0);
    vBar.setCurrentRangeStart (vBar.getCurrentRangeStart() - (step != 0 ? step : (wheel.deltaY > 0 ? rowHeight : -rowHeight)));
}

ui::Hint PatternBlocksView::hintAt (juce::Point<int> p)
{
    if (p.y < dividerHeight)
        return { tr ("Pattern blocks: drag to resize, double-click to show every row"), {} };
    const auto row = rowAt (p.y);
    if (row < 0)
        return {};
    const auto& name = engine.getSong().patterns[(std::size_t) row].name;
    const auto label = name.empty() ? juce::String (row + 1) : juce::String::fromUTF8 (name.c_str());
    if (p.x < playlist.headerWidth)
        return { tr ("Pattern block row: click selects the pattern, double-click opens it, right-click renames and colours it"), label, false, true };
    const auto tool = playlist.currentTool();
    if (tool == EditTool::pencil)
        return { tr ("Pattern blocks: click places one block and drags it along its row, right-click erases"), label };
    if (tool == EditTool::brush)
        return { tr ("Pattern blocks: drag paints whole blocks; press a block and drag to move it, right-click erases"), label };
    if (tool == EditTool::eraser)
        return { tr ("Pattern blocks: click or drag erases blocks in any row"), label };
    if (tool == EditTool::mute)
        return { tr ("Pattern blocks: click or drag mutes blocks in any row (a muted block unmutes them), right-click erases"), label };
    if (tool == EditTool::lasso)
        return { tr ("Pattern blocks: click or drag a box to select blocks in any row, Shift adds; Pencil or Brush moves them together"), label };
    if (tool == EditTool::knife)
        return { tr ("Pattern blocks: click a block to slice its pattern there; a new pattern takes the rest"), label };
    if (tool == EditTool::zoom)
        return { tr ("Pattern blocks: drag to zoom to that time, click to zoom back"), label };
    if (tool == EditTool::listen)
        return { tr ("Pattern blocks: hold the button to play from there"), label };
    return { tr ("Pattern blocks: click or drag paints whole blocks of this row's pattern, right-click erases"), label };
}
} // namespace lynx
