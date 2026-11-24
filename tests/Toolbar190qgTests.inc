// Round 190qg (Discord #feedback 11-23): the main toolbar never hides anything silently, FL's "Single line" toolbar preset
// and the Grouped look. core/ToolbarLayout.h decides which items go to the overflow ("»"), FL's one-row order, the new
// options' saving and the presets; the app's walk over every preset, icon size, Interface size and window width is in
// --selftest (toolbar_overflow_190qg) and --toolbarshot190qg.

#include "core/ToolbarLayout.h"

TEST (toolbar190qg_every_item_belongs_to_one_group_in_toolbar_order)
{
    using namespace lynx::toolbarlayout;
    std::array<int, groupCount> perGroup {};
    for (int i = 0; i < itemCount; ++i)
    {
        const auto g = groupOf ((Item) i);
        ++perGroup[(std::size_t) g];
        CHECK (std::string (itemId ((Item) i)).size() > 1);
        CHECK (std::string (itemName ((Item) i)).size() > 1);
        if (i > 0) // the items of a group are next to each other
            CHECK (groupOf ((Item) (i - 1)) == g || perGroup[(std::size_t) g] == 1);
    }
    for (int g = 0; g < groupCount; ++g) // every panel has at least one item
        CHECK (perGroup[(std::size_t) g] >= 1);
    CHECK (groupOf (Item::time) == Group::transport && groupOf (Item::songPosition) == Group::transport);
    CHECK (groupOf (Item::typingPiano) == Group::record && groupOf (Item::midiLight) == Group::record);
    CHECK (groupOf (Item::audioEditor) == Group::file && groupOf (Item::pluginPicker) == Group::windows);
    CHECK (groupOf (Item::link) == Group::link && groupOf (Item::key) == Group::key && groupOf (Item::meters) == Group::meters);
    // ids are unique
    for (int a = 0; a < itemCount; ++a)
        for (int b = a + 1; b < itemCount; ++b)
            CHECK (std::string (itemId ((Item) a)) != itemId ((Item) b));
}

TEST (toolbar190qg_overflow_lists_dropped_panels_and_switches_never_hidden_ones)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    std::array<bool, groupCount> visible {};
    for (const auto& e : l.entries)
        visible[(std::size_t) e.group] = e.shown;
    // all shown panels fit: nothing in the overflow
    CHECK (overflowItems (l, visible, {}).empty());
    // the output monitor and the CPU panel dropped: their items, in toolbar order (CPU before Output, as on the bar)
    visible[(std::size_t) Group::output] = visible[(std::size_t) Group::meters] = false;
    auto o = overflowItems (l, visible, {});
    CHECK (o.size() == 2 && o[0] == Item::meters && o[1] == Item::output);
    // the recording panel dropped: all ten of its switches
    visible[(std::size_t) Group::record] = false;
    o = overflowItems (l, visible, {});
    CHECK (o.size() == 12 && o[0] == Item::metronome && o[9] == Item::midiLight);
    // a switch a tighter panel form leaves out is listed after the whole panels; one of a dropped panel only once
    visible.fill (true);
    for (auto g : { Group::multilink, Group::key, Group::link })
        visible[(std::size_t) g] = false; // (hidden by default: not shown, so not in the overflow)
    o = overflowItems (l, visible, { Item::typingPiano, Item::midiLight, Item::audioEditor });
    CHECK (o.size() == 3 && o[0] == Item::typingPiano && o[2] == Item::audioEditor);
    // a group the user hid never goes to the overflow, not even its switches
    l.setShown (Group::file, false);
    o = overflowItems (l, visible, { Item::audioEditor });
    CHECK (o.empty());
    // Ctrl+F11 hides the panels on purpose: no overflow
    l.visible = false;
    visible.fill (false);
    CHECK (overflowItems (l, visible, {}).empty());
}

TEST (toolbar190qg_fl_classic_preset_is_fl_single_line_order_on_one_row)
{
    using namespace lynx::toolbarlayout;
    auto base = Layout::makeDefault();
    base.iconSize = IconSize::large;
    base.grouped = true;
    const auto fl = applyPreset (base, Preset::flClassic);
    // FL 2026 "Single line": transport (PAT/SONG, play, stop, record, tempo) and time, monitor, pattern + snap, the windows,
    // the CPU panel; the rest hidden
    CHECK (fl.orderToString() == "transport,output,snap,windows,meters,-link,-master,-record,-multilink,-file,-key,-chord");
    CHECK (fl.oneRow && fl.iconSize == IconSize::large && fl.grouped); // (presets keep the other options)
    CHECK (matchingPreset (fl) == (int) Preset::flClassic);
    // the other presets are two rows; the same groups on two rows are a custom layout
    CHECK (! applyPreset (fl, Preset::standard).oneRow && ! applyPreset (fl, Preset::compact).oneRow);
    auto twoRows = fl;
    twoRows.oneRow = false;
    CHECK (matchingPreset (twoRows) == presetCount);
    CHECK (matchingPreset (base) == (int) Preset::standard);
    CHECK (std::string (presetName (Preset::flClassic)) == "FL classic (one row)");
    // the spare room: before the CPU panel (FL's last spacer), and before Chord / CPU / Output in the default order
    std::array<bool, groupCount> visible {};
    for (const auto& e : fl.entries)
        visible[(std::size_t) e.group] = e.shown;
    CHECK (firstOfRightRun (fl, visible) == 4);
    const auto d = Layout::makeDefault();
    for (const auto& e : d.entries)
        visible[(std::size_t) e.group] = e.shown;
    CHECK (d.entries[(std::size_t) firstOfRightRun (d, visible)].group == Group::chord);
    visible[(std::size_t) Group::chord] = visible[(std::size_t) Group::meters] = visible[(std::size_t) Group::output] = false;
    CHECK (firstOfRightRun (d, visible) == -1);
    // FL's single line is 41 px at 100 %: LYNX's medium row (31) + 10
    CHECK (oneRowHeight (rowHeight (IconSize::medium)) == 41);
    CHECK (overflowWidth (rowHeight (IconSize::small)) >= 18);
}

TEST (toolbar190qg_new_options_change_is_default_and_keep_old_lines)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    CHECK (! l.oneRow && ! l.grouped && l.isDefault());
    l.oneRow = true;
    CHECK (! l.isDefault());
    l.oneRow = false;
    l.grouped = true;
    CHECK (! l.isDefault());
    // an order line saved before this round reads as before (the options are separate keys in ui-options.txt)
    Layout r;
    r.orderFromString ("link,transport,master,record,snap,windows,-multilink,file,-key,chord,meters,output");
    CHECK (r.orderToString() == "link,transport,master,record,snap,windows,-multilink,file,-key,chord,meters,output");
    CHECK (! r.oneRow && ! r.grouped);
    // the Simple bar's "⋯" never leaves the bar (it holds what the bar leaves out)
    CHECK (simplebar::priority (simplebar::Part::more) == 100);
}

TEST (toolbar190qg_one_row_drops_panels_from_the_right_like_fl)
{
    using namespace lynx::toolbarlayout;
    const auto fl = applyPreset (Layout::makeDefault(), Preset::flClassic);
    std::array<int, groupCount> widths {};
    widths.fill (100);
    // room for the transport and three more: the CPU panel (the last) goes first, then the window buttons (FL at 1600 / 1366)
    auto v = fitFromTheRight (fl, widths, 0, 400, 0);
    CHECK (v[(std::size_t) Group::transport] && v[(std::size_t) Group::output] && v[(std::size_t) Group::snap] && v[(std::size_t) Group::windows]);
    CHECK (! v[(std::size_t) Group::meters]);
    v = fitFromTheRight (fl, widths, 0, 300, 0);
    CHECK (! v[(std::size_t) Group::windows] && v[(std::size_t) Group::snap]);
    // the transport always stays, even when nothing fits; hidden groups never show
    v = fitFromTheRight (fl, widths, 0, 10, 0);
    CHECK (v[(std::size_t) Group::transport] && ! v[(std::size_t) Group::output] && ! v[(std::size_t) Group::master]);
    // the two-row toolbar's overflow room: the lowest priority goes (Chord before Output before CPU), never the transport
    auto d = Layout::makeDefault();
    std::array<bool, groupCount> visible {};
    for (const auto& e : d.entries)
        visible[(std::size_t) e.group] = e.shown;
    int shown = 0;
    for (auto b : visible)
        shown += b ? 1 : 0;
    dropUntilFits (visible, widths, 0, (shown - 1) * 100, 0);
    CHECK (! visible[(std::size_t) Group::chord] && visible[(std::size_t) Group::output]);
    dropUntilFits (visible, widths, 0, 0, 0);
    CHECK (visible[(std::size_t) Group::transport]);
    // the narrow toolbar (four-column recording panel): the window buttons go before the recording switches (170e5)
    for (const auto& e : d.entries)
        visible[(std::size_t) e.group] = e.shown;
    visible[(std::size_t) Group::chord] = visible[(std::size_t) Group::meters] = visible[(std::size_t) Group::output] = visible[(std::size_t) Group::file] = false;
    dropUntilFits (visible, widths, 0, 400, 0, true); // (transport, master, record, snap, windows: one must go)
    CHECK (! visible[(std::size_t) Group::windows] && visible[(std::size_t) Group::record]);
}
