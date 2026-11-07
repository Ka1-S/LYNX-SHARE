#include "ui/ClipGridComponent.h"

#include <algorithm>
#include <cmath>

#include "core/ClipLauncher.h"
#include "engine/AudioEngine.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx
{
namespace
{
constexpr int headerHeight = 26, rowHeight = 24, footerHeight = 30, columnWidth = 104, sceneWidth = 128, launchZone = 20;
constexpr int minColumns = 8;

const std::pair<LaunchQuant, const char*> quantNames[] = {
    { LaunchQuant::global, TR_NOOP ("Global") }, { LaunchQuant::none, TR_NOOP ("None") }, { LaunchQuant::bars8, TR_NOOP ("8 bars") },
    { LaunchQuant::bars4, TR_NOOP ("4 bars") },  { LaunchQuant::bars2, TR_NOOP ("2 bars") }, { LaunchQuant::bar, TR_NOOP ("1 bar") },
    { LaunchQuant::half, "1/2" }, { LaunchQuant::halfTriplet, "1/2T" }, { LaunchQuant::quarter, "1/4" }, { LaunchQuant::quarterTriplet, "1/4T" },
    { LaunchQuant::eighth, "1/8" }, { LaunchQuant::eighthTriplet, "1/8T" }, { LaunchQuant::sixteenth, "1/16" },
    { LaunchQuant::sixteenthTriplet, "1/16T" }, { LaunchQuant::thirtySecond, "1/32" }
};

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

class ClipGridComponent final : public juce::Component,
                                private juce::Timer,
                                private juce::ComponentMovementWatcher
{
public:
    explicit ClipGridComponent (AudioEngine& e) : juce::ComponentMovementWatcher (this), engine (e)
    {
        setWantsKeyboardFocus (true);
        setTitle (tr ("Clip grid"));
        setAccessible (true);
        for (auto* b : { &stopAll, &backToPlaylist })
        {
            addAndMakeVisible (*b);
            b->setConnectedEdges (0);
        }
        stopAll.setButtonText (tr ("Stop all clips"));
        stopAll.onClick = [this] { engine.launcherCommand ({ launcher::Command::Type::stopAll }); };
        backToPlaylist.setButtonText (tr ("Back to Playlist"));
        backToPlaylist.setTooltip (tr ("Tracks that launched a clip stay silent in the Playlist until this is pressed"));
        backToPlaylist.onClick = [this] { engine.launcherCommand ({ launcher::Command::Type::backToArrangement }); };
        addAndMakeVisible (globalQuant);
        globalQuant.setTooltip (tr ("Launch quantisation: when a launched clip starts"));
        int id = 1;
        for (const auto& [q, name] : quantNames)
        {
            if (q != LaunchQuant::global)
                globalQuant.addItem (q == LaunchQuant::none || q <= LaunchQuant::bar ? tr (name) : juce::String (name), id);
            ++id;
        }
        globalQuant.onChange = [this] {
            const auto q = (LaunchQuant) (globalQuant.getSelectedId() - 1);
            engine.editSong ([q] (Song& s) { launcher::ensureGrid (s).globalQuant = q; }, false);
        };
        engine.editSong ([] (Song& s) { launcher::ensureGrid (s); }, false);
        syncControls();
    }

    void resized() override
    {
        auto foot = getLocalBounds().removeFromBottom (footerHeight).withTrimmedRight (0).reduced (4, 3);
        auto right = foot.removeFromRight (sceneWidth - 8);
        globalQuant.setBounds (right);
        foot.removeFromRight (6);
        const auto small = getWidth() < 520;
        stopAll.setBounds (foot.removeFromLeft (small ? 84 : 110));
        foot.removeFromLeft (6);
        backToPlaylist.setBounds (foot.removeFromLeft (small ? 104 : 124));
        clampScroll();
    }

    void paint (juce::Graphics& g) override
    {
        namespace c = theme::colour;
        g.fillAll (c::bg1);
        const auto* grid = gridOf();
        if (grid == nullptr)
            return;
        const auto* state = engine.launcherState();
        const auto& song = engine.getSong();
        const auto cells = cellArea();
        const auto columns = columnCount();
        const auto scenes = (int) grid->scenes.size();

        // column headers, slots and the stop row, clipped to the scrolling part
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, cells.getRight(), getHeight()));
            for (int t = firstColumn(); t < columns && columnX (t) < cells.getRight(); ++t)
            {
                const auto x = columnX (t);
                paintHeader (g, song, state, t, { x, 0, columnWidth, headerHeight });
                juce::Graphics::ScopedSaveState rows (g);
                g.reduceClipRegion (juce::Rectangle<int> (x, headerHeight, columnWidth, cells.getHeight()));
                for (int r = firstRow(); r < scenes && rowY (r) < cells.getBottom(); ++r)
                    paintSlot (g, song, *grid, state, t, r, { x, rowY (r), columnWidth, rowHeight });
            }
        }

        // the scene column stays in place
        const juce::Rectangle<int> sceneColumn (cells.getRight(), 0, sceneWidth, getHeight() - footerHeight);
        g.setColour (c::bg2);
        g.fillRect (sceneColumn);
        g.setColour (c::border);
        g.fillRect (sceneColumn.getX(), 0, 1, sceneColumn.getHeight());
        g.setColour (c::textSecondary);
        g.setFont (theme::font (theme::type::small, true));
        g.drawText (tr ("Scenes"), sceneColumn.withHeight (headerHeight).withTrimmedLeft (8), juce::Justification::centredLeft);
        {
            juce::Graphics::ScopedSaveState rows (g);
            g.reduceClipRegion (sceneColumn.withTrimmedTop (headerHeight));
            for (int r = firstRow(); r < scenes && rowY (r) < cells.getBottom(); ++r)
                paintScene (g, *grid, r, { sceneColumn.getX(), rowY (r), sceneWidth, rowHeight });
        }

        // the footer: controls on the scene side and the buttons, on a band
        const auto foot = getLocalBounds().removeFromBottom (footerHeight);
        g.setColour (c::bg3);
        g.fillRect (foot);
        g.setColour (c::border);
        g.fillRect (foot.getX(), foot.getY(), foot.getWidth(), 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto hit = hitTestCell (e.getPosition());
        if (e.mods.isPopupMenu())
        {
            if (hit.scene >= 0 && hit.track < 0)
                showSceneMenu (hit.scene);
            else if (hit.track >= 0 && hit.scene >= 0)
                showSlotMenu (hit.track, hit.scene);
            return;
        }
        if (hit.scene >= 0 && hit.track < 0)
        {
            if (e.x < cellArea().getRight() + launchZone)
                engine.launcherCommand ({ launcher::Command::Type::launchScene, 0, hit.scene });
            selectedScene = hit.scene;
            repaint();
            return;
        }
        if (hit.track < 0 || hit.scene < 0)
            return;
        selectedTrack = hit.track;
        selectedScene = hit.scene;
        repaint();
        const auto* grid = gridOf();
        if (grid == nullptr)
            return;
        const auto filled = launcher::clipAt (*grid, hit.track, hit.scene) != nullptr;
        if (e.x - columnX (hit.track) < launchZone)
        {
            if (filled)
            {
                engine.launcherCommand ({ launcher::Command::Type::launch, hit.track, hit.scene });
                pressedTrack = hit.track;
                pressedScene = hit.scene;
            }
            else
                engine.launcherCommand ({ launcher::Command::Type::stopTrack, hit.track }); // an empty slot's stop button
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressedTrack >= 0)
            engine.launcherCommand ({ launcher::Command::Type::release, pressedTrack, pressedScene });
        pressedTrack = pressedScene = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = hitTestCell (e.getPosition());
        if (hit.track >= 0 && hit.scene >= 0 && e.x - columnX (hit.track) >= launchZone)
            if (const auto* grid = gridOf(); grid != nullptr && launcher::clipAt (*grid, hit.track, hit.scene) == nullptr)
                putPattern (hit.track, hit.scene, engine.getSong().currentPattern);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        juce::ignoreUnused (e);
        const auto dx = w.deltaX != 0.0f ? w.deltaX : (e.mods.isShiftDown() ? w.deltaY : 0.0f);
        const auto dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;
        scrollX -= juce::roundToInt (dx * 240.0f);
        scrollY -= juce::roundToInt (dy * 120.0f);
        clampScroll();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto* grid = gridOf();
        if (grid == nullptr)
            return false;
        const auto code = key.getKeyCode();
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            selectedTrack = juce::jlimit (0, columnCount() - 1, selectedTrack + (code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1 : 0));
            selectedScene = juce::jlimit (0, (int) grid->scenes.size() - 1, selectedScene + (code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : 0));
            revealSelection();
            repaint();
            return true;
        }
        if (code == juce::KeyPress::returnKey)
        {
            engine.launcherCommand (key.getModifiers().isCommandDown() ? launcher::Command { launcher::Command::Type::launchScene, 0, selectedScene }
                                                                       : launcher::Command { launcher::Command::Type::launch, selectedTrack, selectedScene });
            return true;
        }
        if (code == juce::KeyPress::deleteKey)
        {
            const int t = selectedTrack, r = selectedScene;
            engine.editSong ([t, r] (Song& s) { launcher::clearClip (s, t, r); });
            repaint();
            return true;
        }
        return false;
    }

private:
    AudioEngine& engine;
    juce::TextButton stopAll, backToPlaylist;
    juce::ComboBox globalQuant;
    int scrollX = 0, scrollY = 0;
    int selectedTrack = 0, selectedScene = 0;
    int pressedTrack = -1, pressedScene = -1;

    struct Cell
    {
        int track = -1, scene = -1;
    };

    const ClipGrid* gridOf() const noexcept
    {
        const auto& g = engine.getSong().clipGrid;
        return g ? &*g : nullptr;
    }
    int columnCount() const
    {
        const auto& song = engine.getSong();
        auto n = std::max (minColumns, (int) song.playlistTracks.size());
        if (const auto* grid = gridOf())
            for (const auto& c : grid->clips)
                n = std::max (n, c.track + 1);
        return std::min (n, launcher::maxTracks);
    }
    juce::Rectangle<int> cellArea() const
    {
        return { 0, headerHeight, juce::jmax (0, getWidth() - sceneWidth), juce::jmax (0, getHeight() - headerHeight - footerHeight) };
    }
    int columnX (int t) const noexcept { return t * columnWidth - scrollX; }
    int rowY (int r) const noexcept { return headerHeight + r * rowHeight - scrollY; }
    int firstColumn() const noexcept { return juce::jmax (0, scrollX / columnWidth); }
    int firstRow() const noexcept { return juce::jmax (0, scrollY / rowHeight); }

    void clampScroll()
    {
        const auto* grid = gridOf();
        const auto cells = cellArea();
        scrollX = juce::jlimit (0, juce::jmax (0, columnCount() * columnWidth - cells.getWidth()), scrollX);
        scrollY = juce::jlimit (0, juce::jmax (0, (grid != nullptr ? (int) grid->scenes.size() : 0) * rowHeight - cells.getHeight()), scrollY);
    }
    void revealSelection()
    {
        const auto cells = cellArea();
        if (columnX (selectedTrack) < 0)
            scrollX = selectedTrack * columnWidth;
        else if (columnX (selectedTrack) + columnWidth > cells.getRight())
            scrollX = (selectedTrack + 1) * columnWidth - cells.getWidth();
        if (rowY (selectedScene) < headerHeight)
            scrollY = selectedScene * rowHeight;
        else if (rowY (selectedScene) + rowHeight > cells.getBottom())
            scrollY = (selectedScene + 1) * rowHeight - cells.getHeight();
        clampScroll();
    }
    Cell hitTestCell (juce::Point<int> p) const
    {
        Cell out;
        const auto cells = cellArea();
        const auto* grid = gridOf();
        if (grid == nullptr || p.y < headerHeight || p.y >= cells.getBottom())
            return out;
        const auto r = (p.y - headerHeight + scrollY) / rowHeight;
        if (r >= 0 && r < (int) grid->scenes.size())
            out.scene = r;
        if (p.x < cells.getRight())
            if (const auto t = (p.x + scrollX) / columnWidth; t >= 0 && t < columnCount())
                out.track = t;
        return out;
    }

    juce::String clipName (const Song& song, const ClipGrid& grid, const Clip& clip) const
    {
        const auto& slot = grid.slots[(std::size_t) clip.gridSlot];
        if (! slot.name.empty())
            return utf8 (slot.name);
        if (clip.kind == ClipKind::pattern && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size())
            return utf8 (song.patterns[(std::size_t) clip.pattern].name);
        if (clip.kind == ClipKind::audio && clip.channel >= 0 && clip.channel < (int) song.channels.size())
            return utf8 (song.channels[(std::size_t) clip.channel].name);
        return {};
    }

    void paintHeader (juce::Graphics& g, const Song& song, const launcher::Runtime* state, int t, juce::Rectangle<int> r) const
    {
        namespace c = theme::colour;
        const auto* info = t < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) t] : nullptr;
        const auto taken = state != nullptr && state->tracks[(std::size_t) t].taken.load (std::memory_order_relaxed);
        g.setColour (c::bg3);
        g.fillRect (r);
        if (info != nullptr && info->colour != 0)
        {
            g.setColour (juce::Colour (info->colour));
            g.fillRect (r.getX(), r.getBottom() - 3, r.getWidth(), 3);
        }
        g.setColour (c::border);
        g.fillRect (r.getRight() - 1, r.getY(), 1, r.getHeight());
        g.setColour (c::textPrimary);
        g.setFont (theme::font (theme::type::small));
        const auto name = info != nullptr && ! info->name.empty() ? utf8 (info->name) : tr ("Track {0}", { juce::String (t + 1) });
        g.drawText (name, r.reduced (6, 0).withTrimmedRight (taken ? 18 : 0), juce::Justification::centredLeft, true);
        if (taken)
        {
            // the Playlist's clips on this track are silent: a struck-out timeline mark
            const auto mark = r.removeFromRight (22).withSizeKeepingCentre (14, 12).toFloat();
            g.setColour (c::warning);
            g.drawRect (mark, 1.0f);
            g.drawLine (mark.getX(), mark.getBottom(), mark.getRight(), mark.getY(), 1.4f);
        }
    }

    void paintSlot (juce::Graphics& g, const Song& song, const ClipGrid& grid, const launcher::Runtime* state, int t, int row, juce::Rectangle<int> r) const
    {
        namespace c = theme::colour;
        const auto* clip = launcher::clipAt (grid, t, row);
        const auto cell = r.reduced (1).withTrimmedBottom (0);
        const auto zone = cell.withWidth (launchZone).toFloat();
        const auto playing = state != nullptr && state->tracks[(std::size_t) t].playing.load (std::memory_order_relaxed) == row;
        const auto pending = state != nullptr && state->tracks[(std::size_t) t].pending.load (std::memory_order_relaxed) == row;

        if (clip == nullptr)
        {
            g.setColour (c::bg2);
            g.fillRect (cell);
            g.setColour (c::textSecondary.withAlpha (0.55f));
            g.fillRect (zone.withSizeKeepingCentre (7.0f, 7.0f));
        }
        else
        {
            const auto& slot = grid.slots[(std::size_t) clip->gridSlot];
            auto fill = slot.colour != 0 ? juce::Colour (slot.colour) : clip->kind == ClipKind::audio ? c::audioClip : c::noteSoft;
            g.setColour (playing ? fill.brighter (0.25f) : fill.withMultipliedBrightness (0.85f));
            g.fillRect (cell);
            if (playing)
            {
                const auto progress = juce::jlimit (0.0f, 1.0f, state->tracks[(std::size_t) t].progress.load (std::memory_order_relaxed));
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                g.fillRect (cell.withHeight (3).withY (cell.getBottom() - 3).toFloat().withWidth ((float) cell.getWidth() * progress));
            }
            // the launch triangle: lit while playing, blinking while it waits for its grid point
            const auto blink = pending && (juce::Time::getMillisecondCounter() / 150) % 2 == 0;
            juce::Path triangle;
            const auto a = zone.withSizeKeepingCentre (9.0f, 11.0f);
            triangle.addTriangle (a.getX(), a.getY(), a.getRight(), a.getCentreY(), a.getX(), a.getBottom());
            g.setColour (playing ? c::ledOn : pending ? (blink ? c::ledOn : juce::Colours::white.withAlpha (0.3f)) : juce::Colours::white.withAlpha (0.85f));
            g.fillPath (triangle);
            g.setColour (juce::Colours::white);
            g.setFont (theme::font (theme::type::small));
            g.drawText (clipName (song, grid, *clip), cell.withTrimmedLeft (launchZone).withTrimmedRight (3), juce::Justification::centredLeft, true);
        }
        if (t == selectedTrack && row == selectedScene)
        {
            g.setColour (c::accent);
            g.drawRect (cell, 1);
        }
    }

    void paintScene (juce::Graphics& g, const ClipGrid& grid, int row, juce::Rectangle<int> r) const
    {
        namespace c = theme::colour;
        const auto& scene = grid.scenes[(std::size_t) row];
        const auto cell = r.reduced (1);
        g.setColour (scene.colour != 0 ? juce::Colour (scene.colour) : c::bg3);
        g.fillRect (cell);
        juce::Path triangle;
        const auto a = cell.withWidth (launchZone).toFloat().withSizeKeepingCentre (9.0f, 11.0f);
        triangle.addTriangle (a.getX(), a.getY(), a.getRight(), a.getCentreY(), a.getX(), a.getBottom());
        g.setColour (c::textPrimary);
        g.fillPath (triangle);
        g.setFont (theme::font (theme::type::small));
        g.drawText (! scene.name.empty() ? utf8 (scene.name) : juce::String (row + 1), cell.withTrimmedLeft (launchZone), juce::Justification::centredLeft, true);
    }

    void putPattern (int track, int scene, int pattern)
    {
        engine.editSong ([=] (Song& s) {
            if (pattern < 0 || pattern >= (int) s.patterns.size())
                return;
            Clip clip;
            clip.pattern = pattern;
            clip.length = std::max<std::int64_t> (ppq, s.patterns[(std::size_t) pattern].effectiveLength (s.beatsPerBar));
            launcher::setClip (s, track, scene, clip);
        });
        repaint();
    }

    void showSlotMenu (int track, int scene)
    {
        const auto* grid = gridOf();
        if (grid == nullptr)
            return;
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        juce::PopupMenu patterns;
        for (int p = 0; p < (int) song.patterns.size(); ++p)
            patterns.addItem (utf8 (song.patterns[(std::size_t) p].name), [this, track, scene, p] { putPattern (track, scene, p); });
        menu.addSubMenu (tr ("Put a pattern here"), patterns);

        if (launcher::clipAt (*grid, track, scene) != nullptr)
        {
            auto copy = *grid;
            const auto* slot = launcher::settingsAt (copy, track, scene);
            juce::PopupMenu quant;
            for (const auto& [q, name] : quantNames)
                quant.addItem ((q == LaunchQuant::global || q <= LaunchQuant::bar) ? tr (name) : juce::String (name), true, slot->quant == q, [this, track, scene, q = q] {
                    editSlot (track, scene, [q] (ClipGridSlot& s) { s.quant = q; });
                });
            menu.addSubMenu (tr ("Launch quantisation"), quant);
            menu.addItem (tr ("Legato (keep the position)"), true, slot->legato, [this, track, scene, on = slot->legato] {
                editSlot (track, scene, [on] (ClipGridSlot& s) { s.legato = ! on; });
            });
            menu.addItem (tr ("Loop"), true, slot->loop, [this, track, scene, on = slot->loop] {
                editSlot (track, scene, [on] (ClipGridSlot& s) { s.loop = ! on; });
            });
            menu.addSeparator();
            menu.addItem (tr ("Delete"), [this, track, scene] {
                engine.editSong ([=] (Song& s) { launcher::clearClip (s, track, scene); });
                repaint();
            });
        }
        menu.addSeparator();
        addGridOptions (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showSceneMenu (int scene)
    {
        juce::PopupMenu menu;
        menu.addItem (tr ("Insert scene"), [this, scene] {
            engine.editSong ([scene] (Song& s) { launcher::insertScene (s, scene); });
            clampScroll();
            repaint();
        });
        menu.addItem (tr ("Add scene below"), [this, scene] {
            engine.editSong ([scene] (Song& s) { launcher::insertScene (s, scene + 1); });
            clampScroll();
            repaint();
        });
        const auto* grid = gridOf();
        menu.addItem (tr ("Delete scene"), grid != nullptr && grid->scenes.size() > 1, false, [this, scene] {
            engine.editSong ([scene] (Song& s) { launcher::removeScene (s, scene); });
            selectedScene = juce::jmax (0, selectedScene - (selectedScene >= scene ? 1 : 0));
            clampScroll();
            repaint();
        });
        menu.addSeparator();
        addGridOptions (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void addGridOptions (juce::PopupMenu& menu)
    {
        const auto* grid = gridOf();
        const auto on = grid != nullptr && grid->backOnStop;
        menu.addItem (tr ("Give tracks back to the Playlist when the transport stops"), true, on, [this, on] {
            engine.editSong ([on] (Song& s) { launcher::ensureGrid (s).backOnStop = ! on; }, false);
        });
    }

    template <typename Fn>
    void editSlot (int track, int scene, Fn fn)
    {
        engine.editSong ([=] (Song& s) {
            if (s.clipGrid)
                if (auto* slot = launcher::settingsAt (*s.clipGrid, track, scene))
                    fn (*slot);
        });
        repaint();
    }

    void syncControls()
    {
        if (const auto* grid = gridOf())
            globalQuant.setSelectedId ((int) grid->globalQuant + 1, juce::dontSendNotification);
        const auto* state = engine.launcherState();
        auto taken = false;
        if (state != nullptr)
            for (int t = 0, n = columnCount(); t < n && ! taken; ++t)
                taken = state->tracks[(std::size_t) t].taken.load (std::memory_order_relaxed);
        if (backToPlaylist.getToggleState() != taken)
        {
            backToPlaylist.setToggleState (taken, juce::dontSendNotification);
            backToPlaylist.setColour (juce::TextButton::buttonOnColourId, theme::colour::warning);
            backToPlaylist.setColour (juce::TextButton::textColourOnId, theme::colour::bg1);
        }
    }

    // Only while the grid is on screen
    void timerCallback() override
    {
        syncControls();
        repaint();
    }
    void componentMovedOrResized (bool, bool) override {}
    void componentPeerChanged() override { updateTimer(); }
    void componentVisibilityChanged() override { updateTimer(); }
    void updateTimer()
    {
        if (isShowing())
            startTimerHz (30);
        else
            stopTimer();
    }
    using juce::ComponentMovementWatcher::componentVisibilityChanged;
};
} // namespace

std::unique_ptr<juce::Component> makeClipGrid (AudioEngine& engine)
{
    return std::make_unique<ClipGridComponent> (engine);
}
} // namespace lynx
