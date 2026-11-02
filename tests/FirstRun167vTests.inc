// Round 167v: the first-run window layout (a fresh LYNX SHARE opened with the Channel Rack covering the Playlist on
// small/scaled screens, and the Browser over the Playlist after a restart). Included at the end of tests.cpp.

TEST (firstrun167v_default_rack_leaves_the_track_names_visible)
{
    using namespace lynx::workspace;

    // desktops of a maximized/88 % window at 1366x768 and 1920x1080 with 100/125/150 % Windows scaling
    for (const auto& [w, h] : std::vector<std::pair<int, int>> { { 945, 440 }, { 1093, 520 }, { 1186, 540 }, { 1280, 600 }, { 1350, 600 },
                                                                 { 1690, 820 }, { 1920, 960 }, { 2560, 1340 } })
    {
        const auto l = Layout::makeDefault (w, h);
        const auto browser = l[Panel::browser].bounds, playlist = l[Panel::playlist].bounds, rack = l[Panel::channelRack].bounds;
        CHECK (browser.x == 0 && playlist.x == browser.right() && playlist.right() == w); // the Playlist fills the rest
        CHECK (rack.right() <= w - 16 && rack.bottom() <= h && rack.w >= rackMinWidth && rack.w <= rackMaxWidth);
        CHECK (rack.x >= browser.right() + playlistHeaderWidth); // the pattern picker and track names stay in view
        CHECK (l.focused == Panel::channelRack && l[Panel::channelRack].visible && ! l[Panel::mixer].visible && ! l[Panel::pianoRoll].visible);
        if (w >= 1690)
            CHECK (rack.w == rackMaxWidth); // a big desktop: the usual width
    }

    // a desktop too small for that: the rack keeps a usable width and stays on the desktop
    const auto tiny = Layout::makeDefault (780, 400);
    CHECK (tiny[Panel::channelRack].bounds.w >= rackMinWidth - 64 && tiny[Panel::channelRack].bounds.right() <= 780);
}

TEST (firstrun167v_browser_and_playlist_stay_side_by_side)
{
    using namespace lynx::workspace;

    // a layout moved by hand (not the pristine default) scaled down past the Browser's minimum width and back up:
    // the Browser used to keep 220 px while the Playlist's left edge scaled, covering its pattern picker
    auto l = Layout::makeDefault (1690, 820);
    l[Panel::channelRack].bounds.x += 30;
    for (const auto& [w, h] : std::vector<std::pair<int, int>> { { 1180, 560 }, { 900, 420 }, { 1690, 820 }, { 2560, 1340 } })
    {
        l.resize (w, h);
        CHECK (l[Panel::playlist].bounds.x == l[Panel::browser].bounds.right() && l[Panel::playlist].bounds.right() == w);
    }

    // a Playlist undocked by hand stays where the user put it
    auto u = Layout::makeDefault (1690, 820);
    u[Panel::playlist].bounds = { 600, 100, 800, 500 };
    u.resize (1180, 560);
    CHECK (u[Panel::playlist].bounds.x != u[Panel::browser].bounds.right());
}

TEST (firstrun167v_saved_layout_with_the_browser_over_the_playlist_is_repaired)
{
    using namespace lynx::workspace;

    // workspace.txt as 0.1.21 saved it on a first run (Windows, 1705x947 window): Browser 0.1865 wide, Playlist at 0.1545
    const std::string saved = "lynx-layout 1\n"
                              "panel playlist 1 0 0 0.15453 0.00000 0.84547 1.00000 0 0 0 0\n"
                              "panel channelRack 1 0 0 0.30787 0.04621 0.39372 0.26185 0 0 0 0\n"
                              "panel pianoRoll 0 0 0 0.17288 0.03791 0.80876 0.46919 0 0 0 0\n"
                              "panel mixer 0 0 0 0.15867 0.51777 0.83600 0.47393 0 0 0 0\n"
                              "panel browser 1 0 0 0.00000 0.00000 0.18650 1.00000 0 0 0 0\n"
                              "order playlist browser mixer pianoRoll channelRack\n"
                              "focus channelRack\n";
    for (const auto& [w, h] : std::vector<std::pair<int, int>> { { 1180, 560 }, { 1690, 820 } })
    {
        const auto l = Layout::parse (saved, w, h);
        CHECK (l.has_value());
        if (! l)
            continue;
        CHECK ((*l)[Panel::playlist].bounds.x == (*l)[Panel::browser].bounds.right() && (*l)[Panel::playlist].bounds.right() == w);
        CHECK ((*l)[Panel::channelRack].visible && l->focused == Panel::channelRack);
    }

    // a sound layout reads back unchanged at the size it was saved at
    const auto fresh = Layout::makeDefault (1690, 820);
    const auto again = Layout::parse (fresh.serialise(), 1690, 820);
    CHECK (again.has_value() && (*again)[Panel::playlist].bounds == fresh[Panel::playlist].bounds
           && (*again)[Panel::browser].bounds == fresh[Panel::browser].bounds);
}
