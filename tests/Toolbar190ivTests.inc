// Round 190iv (design190ia PLAN §2, slices SG-A and CL-G): the toolbar per layout style. core/ToolbarLayout.h decides the
// look of each style, which groups the Simple bar shows, how the Simple bar and Live's control bar fit a window, the
// Simple "Last saved" states and which of round 190ih's transport states each look shows.

#include "core/ToolbarLayout.h"

TEST (toolbar190iv_each_layout_style_has_its_toolbar_look)
{
    using namespace lynx::toolbarlayout;
    using lynx::layoutstyle::Style;
    CHECK (lookFor (Style::pattern) == Look::fl);
    CHECK (lookFor (Style::simple) == Look::simple);
    CHECK (lookFor (Style::clipLaunch) == Look::live);
    CHECK (std::string (lookId (Look::fl)).empty()); // (FL's widgets keep no look property: their paint is untouched)
    // round 190ih's R3 states (green Pause, Record blink) stay FL's; Simple and Live light their buttons instead
    CHECK (transportStates (Look::fl).pauseWhilePlaying && transportStates (Look::fl).recordBlinks);
    // (round 190jj, capture190ji: BandLab's capsule shows Pause and Stop while it runs, its Record steady; Live keeps none)
    CHECK (transportStates (Look::simple).pauseWhilePlaying && transportStates (Look::simple).stopWhilePlaying && ! transportStates (Look::simple).recordBlinks);
    CHECK (! transportStates (Look::live).pauseWhilePlaying && ! transportStates (Look::live).recordBlinks && ! transportStates (Look::fl).stopWhilePlaying);
    // Simple: BandLab's landmarks; the expert groups go to "⋯" (windows, meters, snap and pattern, recording options)
    for (auto g : { Group::transport, Group::master, Group::file, Group::key, Group::link })
        CHECK (simpleShows (g));
    for (auto g : { Group::windows, Group::meters, Group::snap, Group::record, Group::output, Group::chord, Group::multilink })
        CHECK (! simpleShows (g));
}

TEST (toolbar190iv_simple_bar_fits_by_priority)
{
    using namespace lynx::toolbarlayout::simplebar;
    std::array<int, partCount> widths {};
    for (int i = 0; i < partCount; ++i)
        widths[(std::size_t) i] = 100;
    // everything fits in a wide window
    auto shown = fit (widths, gap, partCount * (100 + gap)); // (round 190ke: + the tuner)
    for (int i = 0; i < partCount; ++i)
        CHECK (shown[(std::size_t) i]);
    // narrower: the master knobs go first, then the Link toggle, then the file buttons; transport and Finish always stay
    shown = fit (widths, gap, 8 * (100 + gap));
    CHECK (! shown[(std::size_t) Part::master] && shown[(std::size_t) Part::file]);
    shown = fit (widths, gap, 6 * (100 + gap));
    CHECK (! shown[(std::size_t) Part::master] && ! shown[(std::size_t) Part::link] && ! shown[(std::size_t) Part::file]);
    CHECK (shown[(std::size_t) Part::status] && shown[(std::size_t) Part::tempo]);
    shown = fit (widths, gap, 10);
    CHECK (shown[(std::size_t) Part::transport] && shown[(std::size_t) Part::finish]);
    // a part not wanted (width 0) never shows
    widths[(std::size_t) Part::key] = 0;
    CHECK (! fit (widths, gap, 5000)[(std::size_t) Part::key]);
    // BandLab's capsule (SG-1): 40 px, radius 20, 44 px segments
    CHECK (capsuleHeight == 40 && radius == 20 && segment == 44);
}

TEST (toolbar190iv_save_status_has_four_states)
{
    using namespace lynx::toolbarlayout;
    CHECK (saveState (false, false, false) == SaveState::fresh);
    CHECK (saveState (false, true, false) == SaveState::saved);
    CHECK (saveState (true, false, false) == SaveState::unsaved);
    CHECK (saveState (true, true, false) == SaveState::unsaved);
    CHECK (saveState (true, false, true) == SaveState::backedUp);
    CHECK (saveState (true, true, true) == SaveState::backedUp);
}

TEST (toolbar190iv_live_control_bar_keeps_live_order_and_fits)
{
    using namespace lynx::toolbarlayout::livebar;
    // Live 12.4's measures (CL-2): a 38 px band of 17 px boxes with 2 px gaps under a 23 px menu row
    CHECK (band == 38 && box == 17 && gap == 2 && menuRow == 23);
    // Live's order: left (Link ... key), the transport in the centre, punch, then the right part ending with the view selector
    CHECK (sectionOf (Item::tap) == Section::left && sectionOf (Item::key) == Section::left);
    CHECK (sectionOf (Item::play) == Section::centre && sectionOf (Item::sessionRecord) == Section::centre);
    CHECK (sectionOf (Item::punch) == Section::loop);
    CHECK (sectionOf (Item::cpu) == Section::right && sectionOf (Item::arrangementView) == Section::right);
    CHECK ((int) Item::sessionView + 1 == (int) Item::arrangementView && (int) Item::arrangementView + 1 == itemCount);

    std::array<int, itemCount> widths {};
    int total = 4 * sectionGap;
    for (int i = 0; i < itemCount; ++i)
    {
        widths[(std::size_t) i] = defaultWidth ((Item) i) > 0 ? defaultWidth ((Item) i) : 40;
        total += widths[(std::size_t) i] + gap;
    }
    // at 1920 px everything shows (as in Live)
    auto shown = fit (widths, 1920 - 16);
    for (int i = 0; i < itemCount; ++i)
        CHECK (shown[(std::size_t) i]);
    CHECK (total < 1280); // (Live's measured bar fits a 1280 px window too)
    // a very narrow window: the lowest priorities go to "⋯" first; the transport, position, tempo, "⋯" and the view stay
    shown = fit (widths, 420);
    for (auto i : { Item::play, Item::stop, Item::record, Item::position, Item::tempo, Item::more, Item::sessionView, Item::arrangementView })
        CHECK (shown[(std::size_t) i]);
    CHECK (! shown[(std::size_t) Item::keyMap] && ! shown[(std::size_t) Item::punch]);
    // dropped in priority order: the Key map first (then MIDI map, punch, capture, the browser toggle...)
    shown = fit (widths, total - 10);
    CHECK (! shown[(std::size_t) Item::keyMap] && shown[(std::size_t) Item::midiMap] && shown[(std::size_t) Item::punch]);
}
