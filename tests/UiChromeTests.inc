// Main window chrome tests (feature/ui-chrome): hint markup, start-up layout, toolbar meters, master pitch.
// Included at the end of tests.cpp.

#include "core/HintMarkup.h"
#include "core/ModifierMap.h" // (round 190sc: the Mac key names over plain())
#include "core/History.h"
#include "core/SongStats.h"

TEST (ui_hint_markup_parses_mouse_and_key_tokens)
{
    using namespace lynx::hintmarkup;
    using K = Segment::Kind;
    const auto s = parse ("Volume: [LMB] drag  [Ctrl] fine  [Alt][LMB] reset");
    CHECK (s.size() == 8);
    CHECK ((s[0] == Segment { K::text, "Volume: " }) && (s[1] == Segment { K::mouse, "LMB" }) && (s[2] == Segment { K::text, " drag  " }));
    CHECK ((s[3] == Segment { K::key, "Ctrl" }) && (s[5] == Segment { K::key, "Alt" }) && (s[6] == Segment { K::mouse, "LMB" }));
    CHECK ((s[7] == Segment { K::text, " reset" }));
    const auto t = parse ("[2xLMB] rename  [RMB] menu  [Wheel] zoom  [MMB] pan  [F5] [L]");
    CHECK (t[0].kind == K::mouse && t[0].value == "2xLMB" && t[2].value == "RMB" && t[4].value == "Wheel" && t[6].value == "MMB");
    CHECK (t[8].kind == K::key && t[8].value == "F5" && t.back().kind == K::key && t.back().value == "L"); // a letter key is a key cap
    // not markup: spaces, punctuation, empty and unclosed brackets stay text (merged into one run)
    const auto u = parse ("a [not a key] b [] c [x");
    CHECK (u.size() == 1 && u[0].kind == K::text && u[0].value == "a [not a key] b [] c [x");
    CHECK (parse ("[[RMB]]").size() == 3 && parse ("[[RMB]]")[1].kind == K::mouse);
    CHECK (parse ("").empty());
    CHECK (plain ("[Ctrl][LMB] adds") == "Ctrl+click adds");
    CHECK (plain ("[RMB] menu", { "a", "b", "c", "d", "e" }) == "c menu");
    // round 190sc (RC sweep): one "+" per join in every modifier combination the hint bar uses, both ways the tables
    // write them, and no dangling "+" after a lone key
    CHECK (plain ("[Alt]+[LMB] reset") == "Alt+click reset");
    CHECK (plain ("[Ctrl]+[LMB] adds") == "Ctrl+click adds");
    CHECK (plain ("[Shift]+[LMB] adds") == "Shift+click adds");
    CHECK (plain ("[Alt][LMB] reset") == "Alt+click reset");
    CHECK (plain ("[Shift][LMB] adds") == "Shift+click adds");
    CHECK (plain ("[Ctrl][Shift][LMB] copies") == "Ctrl+Shift+click copies");
    CHECK (plain ("[Ctrl]+[Shift]+[LMB] copies") == "Ctrl+Shift+click copies");
    CHECK (plain ("[Ctrl]+[Shift] T") == "Ctrl+Shift T");
    CHECK (plain ("[Ctrl][Wheel] zoom  [Alt][RMB] menu") == "Ctrl+wheel zoom  Alt+right-click menu");
    CHECK (plain ("[Alt]+[Up] up") == "Alt+Up up");
    CHECK (plain ("[Ctrl] fine  [Space] play") == "Ctrl fine  Space play");
    CHECK (plain ("[Ctrl][R]") == "Ctrl+R");
    {
        using lynx::modmap::keyNames;
        using lynx::modmap::Os;
        CHECK (keyNames (plain ("[Ctrl]+[LMB] adds"), Os::mac) == "\xe2\x8c\x98+click adds");          // Cmd
        CHECK (keyNames (plain ("[Alt][LMB] reset"), Os::mac) == "\xe2\x8c\xa5+click reset");          // Opt
        CHECK (keyNames (plain ("[Ctrl][Shift][LMB]"), Os::mac) == "\xe2\x8c\x98+Shift+click");
        CHECK (keyNames (plain ("[Alt]+[LMB]"), Os::windows) == "Alt+click");
    }
}

TEST (ui_default_layout_sizes)
{
    using namespace lynx::workspace;

    // FL's fresh desktop at the common window sizes (desktop = window minus the toolbar)
    for (const auto& [w, h] : std::vector<std::pair<int, int>> { { 1280, 640 }, { 1600, 820 }, { 1920, 1000 }, { 2560, 1360 } })
    {
        const auto l = Layout::makeDefault (w, h);
        CHECK (l[Panel::playlist].visible && l[Panel::browser].visible && l[Panel::channelRack].visible);
        CHECK (! l[Panel::pianoRoll].visible && ! l[Panel::mixer].visible);
        CHECK (l[Panel::browser].bounds.w <= 260);
        // the rack leaves the Playlist's picker and track names in view and stays on the desktop
        const auto rack = l[Panel::channelRack].bounds;
        CHECK (rack.right() <= w && rack.bottom() <= h && rack.w <= 720);
        CHECK (rack.x >= l[Panel::browser].bounds.right() + 16);
        if (w - l[Panel::browser].bounds.right() >= playlistHeaderWidth + 720 + 16)
            CHECK (rack.x >= l[Panel::browser].bounds.right() + playlistHeaderWidth);
        // the Mixer sits at the bottom; on a tall desktop the Piano Roll tiles above it
        const auto mixer = l[Panel::mixer].bounds, roll = l[Panel::pianoRoll].bounds;
        CHECK (mixer.bottom() <= h && mixer.h >= std::min (mixerMinHeight, h - 32));
        if (h >= 820)
            CHECK (roll.bottom() <= mixer.y && roll.h >= pianoRollMinHeight);
        for (const auto& p : l.panels)
            CHECK (p.bounds.x >= 0 && p.bounds.y >= 0 && p.bounds.right() <= w && p.bounds.bottom() <= h);
    }

    // an untouched default follows the window to a new size with the default rects (not scaled ones),
    // keeping what is shown and focused
    auto l = Layout::makeDefault (1180, 576);
    l.toggle (Panel::mixer);
    l.relayoutDefault (2560, 1374);
    const auto fresh = Layout::makeDefault (2560, 1374);
    CHECK (l[Panel::channelRack].bounds == fresh[Panel::channelRack].bounds && l[Panel::browser].bounds.w == fresh[Panel::browser].bounds.w);
    CHECK (l[Panel::mixer].visible && l.focused == Panel::mixer && l.width == 2560 && l.height == 1374);
}

TEST (ui_polyphony_counts_sounding_notes)
{
    using namespace lynx;
    Song s;
    s.channels.resize (2);
    Pattern p;
    p.name = "P";
    auto add = [&p] (std::int64_t start, std::int64_t length, int channel) {
        Note n;
        n.start = start;
        n.length = length;
        n.channel = channel;
        p.notes.push_back (n);
    };
    add (0, ppq, 0);           // beat 1
    add (0, 0, 1);             // a step note: lasts one step
    add (ppq / 2, ppq, 0);     // overlaps the first
    p.sortNotes();
    s.patterns.push_back (p);
    s.mode = PlayMode::pattern;

    CHECK (songstats::soundingNotes (s, 0.0) == 2);
    CHECK (songstats::soundingNotes (s, ppq * 0.6) == 2); // the step ended, the second note started
    CHECK (songstats::soundingNotes (s, ppq * 1.2) == 1);
    CHECK (songstats::soundingNotes (s, ppq * 4.0 * 1 + 1.0) == 2); // the 1-bar pattern loops
    s.channels[0].muted = true;
    CHECK (songstats::soundingNotes (s, 0.0) == 1);
    s.channels[0].muted = false;

    // song mode: clips under the playhead, with their offset; muted tracks and clips do not count
    s.mode = PlayMode::song;
    Clip c;
    c.pattern = 0;
    c.start = 4 * ppq;
    c.length = 4 * ppq;
    s.playlist.push_back (c);
    c.track = 1;
    c.offset = ppq; // starts one beat in
    s.playlist.push_back (c);
    CHECK (songstats::soundingNotes (s, 0.0) == 0);
    CHECK (songstats::soundingNotes (s, 4.0 * ppq) == 2 + 1);
    s.playlist[1].muted = true;
    CHECK (songstats::soundingNotes (s, 4.0 * ppq) == 2);
}

TEST (ui_master_pitch_is_a_named_undo_step)
{
    auto before = lynx::Song::makeDefault();
    auto after = before;
    after.masterPitch = 100.0f;
    CHECK (std::string (lynx::describeChange (before, after).text) == "Master pitch");
    CHECK (lynx::Song {}.masterPitch == 0.0f);
}
