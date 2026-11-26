// Round 190ri (0.1.32 quality, community batch): nynsai's feedback video (Discord, 11-25) and the sweep's open items.
// FL facts: live checks of FL 2026 (build 5654), docs/analise/REA_rodada190ri.md.
#include "core/CopyName.h"
#include "core/StepSlide.h"

namespace c190ri
{
inline lynx::Note note (std::int64_t start, int key, int channel)
{
    lynx::Note n;
    n.start = start, n.key = key, n.channel = channel;
    return n;
}
inline std::vector<std::string> names (const lynx::Song& s)
{
    std::vector<std::string> out;
    for (const auto& p : s.patterns)
        out.push_back (p.name);
    return out;
}
} // namespace c190ri

// FL: foo -> foo #2 -> foo #3 -> foo #4; "foo #2" cloned while #3 and #4 exist -> "foo #5" right after it
TEST (community190ri_clone_names)
{
    using namespace lynx;
    const auto none = [] (const std::string&) { return false; };
    CHECK (copyname::next ("foo", none) == "foo #2");
    CHECK (copyname::next ("foo #2", none) == "foo #3");
    CHECK (copyname::next ("foo #9", none) == "foo #10");
    CHECK (copyname::next ("foo #2 #2", none) == "foo #2 #3"); // (an older LYNX's name goes on counting)
    CHECK (copyname::next ("C #x", none) == "C #x #2" && copyname::next ("C#", none) == "C# #2" && copyname::next (" #3", none) == " #3 #2" && copyname::next ("", none) == " #2");
    CHECK (copyname::defaultPatternNumber ("Pattern 12") == 12 && copyname::defaultPatternNumber ("Pattern 0") == 0
           && copyname::defaultPatternNumber ("Pattern 1 #2") == 0 && copyname::defaultPatternNumber ("Patterns") == 0 && copyname::defaultPatternNumber ("Pattern ") == 0);

    Song s;
    s.patterns.resize (1);
    s.patterns[0].name = "foo";
    for (int i = 0; i < 3; ++i)
        clonePattern (s, s.currentPattern);
    CHECK ((c190ri::names (s) == std::vector<std::string> { "foo", "foo #2", "foo #3", "foo #4" }));
    CHECK (clonePattern (s, 1) == 2);
    CHECK ((c190ri::names (s) == std::vector<std::string> { "foo", "foo #2", "foo #5", "foo #3", "foo #4" }));

    // patterns with no name of their own: FL shows their position (three clones of Pattern 1, then a clone of Pattern 2)
    Song d;
    d.patterns.resize (1);
    d.patterns[0].name = "Pattern 1";
    for (int i = 0; i < 3; ++i)
        clonePattern (d, d.currentPattern);
    CHECK ((c190ri::names (d) == std::vector<std::string> { "Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4" }));
    d.patterns[3].name = "Drop";
    d.patterns.push_back (d.patterns[2]);
    d.patterns[4].name = "Pattern 5";
    CHECK (clonePattern (d, 1) == 2);
    CHECK ((c190ri::names (d) == std::vector<std::string> { "Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4", "Drop", "Pattern 6" }));
    // a default name that is not the position's own: the lowest number that is free
    d.patterns[0].name = "Pattern 9";
    clonePattern (d, 0);
    CHECK (d.patterns[1].name == "Pattern 1");
    // every name stays one of a kind
    auto all = c190ri::names (d);
    std::sort (all.begin(), all.end());
    CHECK (std::adjacent_find (all.begin(), all.end()) == all.end());

    // channels (FL: GMS -> GMS #2 -> GMS #3 -> GMS #4, each cloned from the one before)
    Song c;
    c.channels.resize (1);
    c.channels[0].name = "GMS";
    for (int i = 0; i < 3; ++i)
        cloneChannel (c, i);
    CHECK (c.channels.size() == 4 && c.channels[1].name == "GMS #2" && c.channels[2].name == "GMS #3" && c.channels[3].name == "GMS #4");
    CHECK (quickrender::uniqueChannelName (c, "GMS #2") == "GMS #5" && quickrender::uniqueChannelName (c, "Kick") == "Kick");

    // clips: Make unique, again and again
    Song u;
    u.patterns.resize (1);
    u.patterns[0].name = "Lead";
    u.automations.resize (1);
    u.automations[0].name = "Cutoff";
    Clip p, a;
    p.pattern = 0;
    a.kind = ClipKind::automation, a.automation = 0;
    u.playlist = { p, a };
    for (int i = 0; i < 3; ++i)
        polish::makeClipsUnique (u, { 0, 1 });
    CHECK (u.patterns.back().name == "Lead #4" && u.automations.back().name == "Cutoff #4");
    u.patterns[(std::size_t) u.playlist[0].pattern].name = "Pattern 1";
    polish::makeClipsUnique (u, { 0 });
    CHECK (u.patterns.back().name == "Pattern 2");
}

// FL 2026: every channel's notes of the pattern (steps too), each clamped on its own; a channel list is the current pattern's
TEST (community190ri_transpose)
{
    using namespace lynx;
    Song s;
    s.patterns.resize (3);
    s.patterns[0].notes = { c190ri::note (0, 24, 0), c190ri::note (0, 60, 1), c190ri::note (96, 126, 0), c190ri::note (192, 127, 0) };
    s.patterns[1].notes = { c190ri::note (0, 60, 0) };
    s.patterns[2].notes = { c190ri::note (0, 60, 1) };
    CHECK (transposePatterns (s, { 0 }, {}, 2) == 3); // (127 stays)
    CHECK (s.patterns[0].notes[0].key == 26 && s.patterns[0].notes[1].key == 62 && s.patterns[0].notes[2].key == 127 && s.patterns[0].notes[3].key == 127);
    CHECK (s.patterns[1].notes[0].key == 60);
    CHECK (transposePatterns (s, { 1, 2, 1, 9, -1 }, {}, -12) == 2); // (several picked; a pattern named twice moves once)
    CHECK (s.patterns[1].notes[0].key == 48 && s.patterns[2].notes[0].key == 48);
    CHECK (transposePatterns (s, { 0 }, { 1 }, -100) == 1);
    CHECK (s.patterns[0].notes[1].key == 0 && s.patterns[0].notes[0].key == 26);
    CHECK (transposePatterns (s, { 0 }, {}, 0) == 0 && transposePatterns (s, {}, {}, 5) == 0);
}

// FL 2026: Alt + a tool's letter picks the tool while the typing keyboard is on (Playlist: C D E B Y S; Piano roll: D), never
// where the Alt key has a job of its own
TEST (community190ri_alt_tool_keys)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.findToolWithAlt ({ 'C', alt }, Context::playlist, true) == "playlist.tool.knife");
    CHECK (map.findToolWithAlt ({ 'D', alt }, Context::playlist, true) == "playlist.tool.eraser");
    CHECK (map.findToolWithAlt ({ 'E', alt }, Context::playlist, true) == "playlist.tool.lasso");
    CHECK (map.findToolWithAlt ({ 'B', alt }, Context::playlist, true) == "playlist.tool.brush");
    CHECK (map.findToolWithAlt ({ 'Y', alt }, Context::playlist, true) == "playlist.tool.listen");
    CHECK (map.findToolWithAlt ({ 'S', alt }, Context::playlist, true) == "playlist.tool.slip");
    CHECK (map.findToolWithAlt ({ 'C', alt }, Context::playlist, false).empty());
    CHECK (map.findToolWithAlt ({ 'C', alt | shift }, Context::playlist, true).empty() && map.findToolWithAlt ({ 'C', none }, Context::playlist, true).empty());
    CHECK (map.findToolWithAlt ({ 'D', alt }, Context::pianoRoll, true) == "pianoRoll.tool.eraser");
    CHECK (map.findToolWithAlt ({ 'C', alt }, Context::pianoRoll, true).empty()); // (colours the selected notes)
    CHECK (map.findToolWithAlt ({ 'C', alt }, Context::channelRack, true).empty()); // (clones the channels)
    CHECK (map.findToolWithAlt ({ 'C', alt }, Context::mixer, true).empty());
}

// issue #272: an .flp's slide notes of no length (FL 2026: a 7 px pill, and the sounding notes move at once) are one tick held,
// come back to FL with no length, and a real slide of one step keeps its length
TEST (community190ri_step_slides)
{
    using namespace lynx;
    Note n = c190ri::note (0, 60, 0);
    n.slide = true, n.length = ticksPerStep, n.held = true;
    CHECK (! stepslide::is (n)); // (what an older LYNX imported on a generator: plays as it did)
    stepslide::make (n);
    CHECK (stepslide::is (n) && n.length == 1 && ! n.heldStep());
    n.length = 240;
    CHECK (! stepslide::is (n));

    Song s;
    s.mixer = Song::defaultMixer (2);
    s.channels.resize (1);
    s.channels[0].instrument = InstrumentKind::sampler;
    s.channels[0].samplePath = "C:\\x.wav";
    s.patterns.resize (1);
    auto base = c190ri::note (0, 48, 0);
    base.length = ppq * 4;
    auto none = c190ri::note (ppq, 60, 0), step = c190ri::note (2 * ppq, 55, 0);
    none.slide = step.slide = true;
    stepslide::make (none);
    step.length = ticksPerStep;
    s.patterns[0].notes = { base, none, step };
    s.patterns[0].sortNotes();
    Clip c;
    c.pattern = 0, c.length = ppq * 4;
    s.playlist = { c };
    flpexport::Options options;
    flpexport::Report report;
    const auto parsed = flp::parseProject (flpexport::exportFlp (s, options, report));
    {
        std::vector<std::uint32_t> lengths;
        for (const auto& p : parsed.patterns)
            for (const auto& x : p.notes)
                if (x.slide())
                    lengths.push_back (x.length);
        CHECK (lengths.size() == 2 && lengths[0] == 0 && lengths[1] > 0);
        const auto back = importFlp (parsed, {});
        int found = 0;
        for (const auto& p : back.patterns)
            for (const auto& x : p.notes)
                found += stepslide::is (x) ? 1 : x.slide && x.length == ticksPerStep ? 100 : 0;
        CHECK (found == 101);
    }
}

// kainproctor (Discord 11-25): FL 2026 live with skips at bars 3 and 5, a plain marker at 7, a skip at 9, a pause at 11, a plain
// marker at 13, a skip at 15 and a marker loop at 17 (flmark/m1.flp): bars 1-2, 7-8, stop at 11; Play: 11-14, 17-18, 17-18...
TEST (community190ri_marker_skips_chain)
{
    using namespace markers165;
    lynx::Song song;
    song.patterns.emplace_back().name = "P";
    lynx::Clip clip;
    clip.pattern = 0;
    clip.length = 24 * 4 * lynx::ppq;
    song.playlist.push_back (clip);
    song.mode = lynx::PlayMode::song;
    for (const auto [b, action] : { std::pair { 3, lynx::Marker::markerSkip }, { 5, lynx::Marker::markerSkip }, { 7, lynx::Marker::none },
                                     { 9, lynx::Marker::markerSkip }, { 11, lynx::Marker::markerPause }, { 13, lynx::Marker::none },
                                     { 15, lynx::Marker::markerSkip }, { 17, lynx::Marker::markerLoop }, { 19, lynx::Marker::none } })
        song.addMarker (marker (bar (b), action));
    const auto first = play (song, 3000);
    auto expected = range (0, 8);
    for (auto b : range (24, 32))
        expected.push_back (b);
    CHECK (first.beats == expected);
    CHECK (first.paused && first.at == (double) bar (11));
    const auto second = play (song, 3000, first.at); // (Play again: from the pause)
    CHECK (second.beats.size() > 40 && second.beats[0] == 40 && second.beats[15] == 55 && second.beats[16] == 64 && second.beats[24] == 64 && second.beats[23] == 71);
    // the render: the same sections, straight through (FL's render plays no pause or loop)
    song.exportMarkers = true;
    const auto spans = lynx::exportopts::exportSpans (song);
    CHECK (spans.size() == 4 && spans[0].second == bar (3) && spans[1].first == bar (7) && spans[1].second == bar (9) && spans[2].first == bar (11)
           && spans[2].second == bar (15) && spans[3].first == bar (17));
}

// FL 2026 live (flmark/m2.flp: markers a 1, b 3, c 7, d 9; clips at 1, 3, 4, 6, 7, 9): b › Move content right puts c's section
// (7-8) at 3 and b's (3-6) after it; c (now at 3) › Move content left swaps it with a's section
TEST (community190ri_marker_move_content)
{
    using namespace markers165;
    lynx::Song s;
    s.patterns.emplace_back();
    for (const auto [b, n, t] : { std::tuple { 1, 1, 0 }, { 3, 2, 0 }, { 4, 1, 1 }, { 6, 2, 2 }, { 7, 1, 0 }, { 9, 1, 0 } })
    {
        lynx::Clip c;
        c.start = bar (b), c.length = n * 4 * lynx::ppq, c.track = t;
        s.playlist.push_back (c);
    }
    for (const auto [b, name] : { std::pair { 1, "a" }, { 3, "b" }, { 7, "c" }, { 9, "d" } })
        s.addMarker (marker (bar (b), lynx::Marker::none, name));
    CHECK (lynx::markers::canMoveContent (s, 1, 1) && ! lynx::markers::canMoveContent (s, 3, 1) && ! lynx::markers::canMoveContent (s, 0, -1));
    CHECK (lynx::markers::moveContent (s, 1, 1));
    std::vector<std::int64_t> starts;
    for (const auto& c : s.playlist)
        starts.push_back (c.start);
    CHECK ((starts == std::vector<std::int64_t> { bar (1), bar (5), bar (6), bar (8), bar (3), bar (9) }));
    CHECK (s.markers()[1].name == "c" && s.markers()[1].tick == bar (3) && s.markers()[2].name == "b" && s.markers()[2].tick == bar (5));
    CHECK (lynx::markers::moveContent (s, 1, -1));
    CHECK (s.markers()[0].name == "c" && s.markers()[0].tick == bar (1) && s.markers()[1].name == "a" && s.markers()[1].tick == bar (3)
           && s.playlist[4].start == bar (1) && s.playlist[0].start == bar (3));
}

// FL 2026 live (kainproctor 11-25): Move down with tracks 2-4 selected passes the group of tracks 5-7 at once
TEST (community190ri_move_selected_tracks)
{
    lynx::Song s;
    s.playlistTracks.resize (10);
    for (int i = 0; i < 10; ++i)
        s.playlistTracks[(std::size_t) i].name = std::to_string (i + 1);
    s.playlistTracks[5].grouped = s.playlistTracks[6].grouped = true; // (5 heads 6 and 7)
    auto moved = lynx::noteedit::moveTrackBlock (s, { 1, 2, 3 }, 1, 10);
    CHECK ((moved == std::vector<int> { 4, 5, 6 }));
    std::string order;
    for (const auto& t : s.playlistTracks)
        order += t.name + ",";
    CHECK (order == "1,5,6,7,2,3,4,8,9,10,");
    CHECK (s.playlistTracks[2].grouped && s.playlistTracks[3].grouped && ! s.playlistTracks[4].grouped);
    moved = lynx::noteedit::moveTrackBlock (s, moved, -1, 10); // (and back up, past the group again)
    CHECK ((moved == std::vector<int> { 1, 2, 3 }));
    order.clear();
    for (const auto& t : s.playlistTracks)
        order += t.name + ",";
    CHECK (order == "1,2,3,4,5,6,7,8,9,10,");
    CHECK ((lynx::noteedit::moveTrackBlock (s, { 0, 2 }, -1, 10) == std::vector<int> { 0, 2 })); // (the top: nothing)
    CHECK ((lynx::noteedit::moveTrackBlock (s, { 0, 2 }, 1, 10) == std::vector<int> { 1, 3 }));
}
