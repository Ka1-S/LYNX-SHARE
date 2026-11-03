// Round 168e: FL's renders back into the project (Patterns › Quick render / Render as audio clip / Render and replace,
// Tools › Consolidate playlist selection, the track menu's Consolidate this track, File › Export › All playlist tracks /
// track groups). docs/analise/REA_rodada168e.md.

#include "core/Mastering.h"
#include "core/QuickRender.h"
#include <algorithm>
#include <cmath>

namespace
{
lynx::Clip patternClip (int pattern, int track, std::int64_t start, std::int64_t length)
{
    lynx::Clip c;
    c.pattern = pattern;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}
} // namespace

TEST (rea168e_pattern_render_names_and_empty)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (3);
    song.patterns[0].notes.clear();
    song.patterns[0].events.clear();
    song.patterns[1].name = "Drums";
    song.patterns[1].notes.push_back ({});
    song.patterns[2].name.clear();
    song.patterns[2].events.push_back ({});
    // FL's "Pattern is empty, nothing to render." (FUN_012d8d70): no notes and no event lanes with events
    CHECK (! quickrender::patternRenderable (song, 0));
    CHECK (quickrender::patternRenderable (song, 1));
    CHECK (! quickrender::patternRenderable (song, 2)); // a lane without events
    song.patterns[2].events[0].events.push_back ({});
    CHECK (quickrender::patternRenderable (song, 2));
    CHECK (! quickrender::patternRenderable (song, 9));
    // the file / channel name: the pattern's, else "Pattern N" (FUN_011880b0)
    CHECK (quickrender::patternName (song, 1) == "Drums");
    CHECK (quickrender::patternName (song, 2) == "Pattern 3");
    // channel names made unique as FL does (FUN_00fe87a0): "<name> #2"
    song.channels.resize (1);
    song.channels[0].name = "Drums";
    CHECK (quickrender::uniqueChannelName (song, "Drums") == "Drums #2");
    song.channels.push_back ({});
    song.channels.back().name = "Drums #2";
    CHECK (quickrender::uniqueChannelName (song, "Drums") == "Drums #3");
    CHECK (quickrender::uniqueChannelName (song, "Bass") == "Bass");

    const auto render = quickrender::makePatternRenderSong (song, 1);
    CHECK (render.currentPattern == 1 && render.mode == PlayMode::pattern && ! render.hasLoop());
}

TEST (rea168e_render_as_clip_and_replace)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (2);
    song.patterns[1].name = "Lead";
    song.patterns[1].colour = 0xff336699u;
    song.playlist.clear();
    song.playlist.push_back (patternClip (1, 0, 0, ppq * 4));
    song.playlist.push_back (patternClip (0, 1, 0, ppq * 4));
    auto trimmed = patternClip (1, 2, ppq * 8, ppq * 2);
    trimmed.offset = ppq;
    song.playlist.push_back (trimmed);
    const auto channels = song.channels.size();

    // "Render as audio clip": only the channel (FUN_01188230 without the replace flag)
    auto a = song;
    const auto ch = quickrender::applyPatternRender (a, 1, "/tmp/Lead.wav", false);
    CHECK (ch == (int) channels && a.channels.size() == channels + 1);
    CHECK (a.channels.back().instrument == InstrumentKind::audioClip && a.channels.back().samplePath == "/tmp/Lead.wav");
    CHECK (a.channels.back().name == "Lead" && a.channels.back().colour == 0xff336699u && a.channels.back().mixerTrack == 0);
    CHECK (a.playlist[0].kind == ClipKind::pattern && a.playlist[2].kind == ClipKind::pattern);

    // "Render and replace": every clip of the pattern becomes a clip of the render, same place, length and offset
    auto b = song;
    const auto ch2 = quickrender::applyPatternRender (b, 1, "/tmp/Lead.wav", true);
    CHECK (b.playlist[0].kind == ClipKind::audio && b.playlist[0].channel == ch2 && b.playlist[0].length == ppq * 4);
    CHECK (b.playlist[2].kind == ClipKind::audio && b.playlist[2].offset == ppq && b.playlist[2].start == ppq * 8 && b.playlist[2].track == 2);
    CHECK (b.playlist[1].kind == ClipKind::pattern && b.playlist[1].pattern == 0); // another pattern stays
}

TEST (rea168e_consolidate_selection_plan)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (3);
    song.patterns[1].name = "Keys";
    song.patterns[1].colour = 0xff112233u;
    song.playlist.clear();
    song.playlistTracks.resize (6);
    song.playlist.push_back (patternClip (1, 3, ppq * 4, ppq * 4));  // 0
    song.playlist.push_back (patternClip (2, 2, ppq * 12, ppq * 4)); // 1
    auto muted = patternClip (1, 1, 0, ppq * 4);
    muted.muted = true;
    song.playlist.push_back (muted);                                 // 2: muted, not part of it
    Clip automation;
    automation.kind = ClipKind::automation;
    automation.track = 5;
    automation.start = 0;
    automation.length = ppq * 32;
    song.playlist.push_back (automation);                            // 3

    quickrender::Plan plan;
    // FL: "Nothing selected|There is no unmuted playlist selection to consolidate."
    CHECK (quickrender::planSelection (song, {}, false, plan) == quickrender::Result::nothingSelected);
    CHECK (quickrender::planSelection (song, { 2 }, false, plan) == quickrender::Result::nothingSelected);
    CHECK (quickrender::planSelection (song, { 3 }, false, plan) == quickrender::Result::nothingSelected); // (automation)

    CHECK (quickrender::planSelection (song, { 1, 0, 2 }, false, plan) == quickrender::Result::ok);
    CHECK (plan.clips == std::vector<int> ({ 0, 1 }) && plan.topTrack == 2 && ! plan.timeSelection);
    CHECK (plan.start == ppq * 4 && plan.end == ppq * 16);
    // the name and colour of the first clip's source (FUN_00e50820 / FUN_00e50890), " (consolidated)"
    CHECK (plan.name == "Keys (consolidated)" && plan.colour == 0xff112233u);
    CHECK (quickrender::planSelection (song, { 0, 1 }, true, plan) == quickrender::Result::ok && plan.start == 0 && plan.end == ppq * 16);

    // a time selection is the range (FL keeps it)
    song.loopStart = ppq * 6;
    song.loopEnd = ppq * 14;
    CHECK (quickrender::planSelection (song, { 0, 1 }, false, plan) == quickrender::Result::ok);
    CHECK (plan.timeSelection && plan.start == ppq * 6 && plan.end == ppq * 14);

    // the render: only the plan's clips and the automation clips play, over the range
    const auto render = quickrender::makePlanRenderSong (song, plan);
    CHECK (render.playlist.size() == 3 && render.loopStart == ppq * 6 && render.loopEnd == ppq * 14 && render.mode == PlayMode::song);

    // a pattern with tempo events can't be consolidated
    song.patterns[2].events.push_back ({});
    song.patterns[2].events.back().target.kind = AutomationTargetKind::tempo;
    song.patterns[2].events.back().events.push_back ({});
    CHECK (quickrender::planSelection (song, { 0, 1 }, false, plan) == quickrender::Result::tempoEvents);
}

TEST (rea168e_consolidate_apply)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (2);
    song.playlist.clear();
    song.playlistTracks.assign (4, {});
    song.playlistTracks[1].name = "Synth";
    song.playlist.push_back (patternClip (1, 1, 0, ppq * 4));
    song.playlist.push_back (patternClip (1, 1, ppq * 8, ppq * 4));
    song.playlist.push_back (patternClip (0, 2, 0, ppq * 4)); // a track below: moves down

    quickrender::Plan plan;
    CHECK (quickrender::planTrack (song, 1, false, quickrender::Start::clipStart, plan) == quickrender::Result::ok);
    CHECK (plan.name == "Synth (consolidated)" && plan.start == 0 && plan.end == ppq * 12 && plan.topTrack == 1);
    const auto channels = song.channels.size();
    const auto ch = quickrender::applyConsolidation (song, plan, "/tmp/Synth (consolidated).wav", ppq * 13);
    CHECK (ch == (int) channels && song.channels.back().name == "Synth (consolidated)" && song.channels.back().instrument == InstrumentKind::audioClip);
    // the new track (FL inserts it at the consolidated track): its clip; the old track below it, grouped, clips muted
    CHECK (song.playlistTracks.size() >= 5);
    CHECK (song.playlistTracks[1].name.empty() && ! song.playlistTracks[1].grouped);
    CHECK (song.playlistTracks[2].name == "Synth" && song.playlistTracks[2].grouped);
    int onNew = 0, mutedOld = 0, below = 0;
    for (const auto& c : song.playlist)
    {
        if (c.track == 1)
        {
            ++onNew;
            CHECK (c.kind == ClipKind::audio && c.channel == ch && c.start == 0 && c.length == ppq * 13 && ! c.muted);
        }
        if (c.track == 2 && c.muted && c.kind == ClipKind::pattern)
            ++mutedOld;
        if (c.track == 3 && ! c.muted)
            ++below;
    }
    CHECK (onNew == 1 && mutedOld == 2 && below == 1 && song.playlist.size() == 4);

    // with a time selection the clips are cut at its edges and only the inside is muted
    auto t = Song::makeDefault();
    t.playlist.clear();
    t.playlistTracks.assign (2, {});
    t.playlist.push_back (patternClip (0, 0, 0, ppq * 8));
    t.loopStart = ppq * 2;
    t.loopEnd = ppq * 4;
    CHECK (quickrender::planTrack (t, 0, false, quickrender::Start::clipStart, plan) == quickrender::Result::ok);
    CHECK (plan.timeSelection && plan.start == ppq * 2 && plan.end == ppq * 4);
    quickrender::applyConsolidation (t, plan, "/tmp/x.wav", ppq * 2);
    int pieces = 0, mutedInside = 0;
    for (const auto& c : t.playlist)
        if (c.track == 1 && c.kind == ClipKind::pattern)
        {
            ++pieces;
            if (c.muted)
            {
                ++mutedInside;
                CHECK (c.start == ppq * 2 && c.length == ppq * 2 && c.offset == ppq * 2);
            }
        }
    CHECK (pieces == 3 && mutedInside == 1);

    // a muted track and an empty one: "Track is empty"
    auto e = song;
    e.playlistTracks[3].muted = true;
    CHECK (quickrender::planTrack (e, 3, false, quickrender::Start::songStart, plan) == quickrender::Result::empty);
    CHECK (quickrender::planTrack (e, 0, false, quickrender::Start::songStart, plan) == quickrender::Result::empty);
    // "Time selection" without one: nothing
    CHECK (quickrender::planTrack (song, 3, false, quickrender::Start::timeSelection, plan) == quickrender::Result::empty);
}

TEST (rea168e_export_tracks_and_groups)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (2);
    song.playlist.clear();
    song.playlistTracks.assign (5, {});
    song.playlistTracks[0].name = "Drums";
    song.playlistTracks[2].grouped = true; // member of track 1's group
    song.playlist.push_back (patternClip (1, 0, ppq * 4, ppq * 4));
    song.playlist.push_back (patternClip (1, 1, ppq * 8, ppq * 4));
    song.playlist.push_back (patternClip (1, 2, ppq * 16, ppq * 4));
    song.playlist.push_back (patternClip (1, 3, 0, ppq * 4));

    CHECK (quickrender::exportTracks (song, false) == std::vector<int> ({ 0, 1, 2, 3 }));
    CHECK (quickrender::exportTracks (song, true) == std::vector<int> ({ 0, 1, 3 }));
    // "<project> - <track>" (FUN_012fd7a0: the project's file name, " - ", the track's name or "Track N")
    CHECK (quickrender::trackExportName (song, "My song", 0) == "My song - Drums");
    CHECK (quickrender::trackExportName (song, "My song", 3) == "My song - Track 4");
    song.playlistTracks[1].name = "A/B";
    CHECK (quickrender::trackExportName (song, "x", 1) == "x - A_B");

    quickrender::Plan plan;
    // a group: the header with its members (flag 0x20), from the track's start
    CHECK (quickrender::planTrack (song, 1, true, quickrender::Start::clipStart, plan) == quickrender::Result::ok);
    CHECK (plan.clips.size() == 2 && plan.start == ppq * 8 && plan.end == ppq * 20);
    CHECK (quickrender::planTrack (song, 1, false, quickrender::Start::songStart, plan) == quickrender::Result::ok);
    CHECK (plan.clips.size() == 1 && plan.start == 0 && plan.end == ppq * 12);
}

TEST (rea168e_ticks_for_seconds)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.bpm = 120.0;
    song.playlist.clear();
    CHECK (quickrender::ticksForSeconds (song, 0, 2.0) == ppq * 4); // 2 s at 120 BPM = 4 beats
    CHECK (quickrender::ticksForSeconds (song, ppq * 8, 0.5) == ppq);
    CHECK (quickrender::ticksForSeconds (song, 0, 0.0) == 0);
}

// File › Export › Master... (LYNX's own mastering where FL uses FL Cloud): the loudness reaches the target and the
// true peak stays under the ceiling; silence is left alone
TEST (rea168e_mastering_target_and_ceiling)
{
    using namespace lynx;
    const double sr = 48000.0;
    std::vector<float> l ((std::size_t) (sr * 6.0)), r (l.size());
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        // a quiet tone with a loud transient every half second (the limiter has peaks to hold)
        const auto t = (double) i / sr;
        const auto hit = std::fmod (t, 0.5) < 0.01 ? 0.9 : 0.0;
        l[i] = (float) (0.05 * std::sin (2.0 * 3.14159265358979 * 220.0 * t) + hit * std::sin (2.0 * 3.14159265358979 * 1000.0 * t));
        r[i] = l[i];
    }
    for (auto target : { -14.0, -9.0, -23.0 })
    {
        auto a = l, b = r;
        mastering::Settings s;
        s.targetLufs = target;
        const auto res = mastering::process (a, b, sr, s);
        double lufs = 0.0, tp = 0.0;
        mastering::measure (a, b, sr, lufs, tp);
        CHECK (a.size() == l.size());
        CHECK (std::abs (lufs - target) < 0.5);
        CHECK (tp <= -0.9);
        CHECK (std::abs (res.outputLufs - lufs) < 0.01);
    }
    std::vector<float> silent (48000, 0.0f), silent2 = silent;
    mastering::process (silent, silent2, sr, {});
    CHECK (std::all_of (silent.begin(), silent.end(), [] (float v) { return v == 0.0f; }));
    CHECK (mastering::targets().size() == 5 && mastering::targets()[2] == -14.0);
}

// File › Export › "Project bones..." (FUN_011ecca0): the mixer states, their effects and each channel's notes per pattern
TEST (rea168e_project_bones)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    for (auto& t : song.mixer)
    {
        t.effects.clear();
        t.name.clear();
    }
    for (auto& p : song.patterns)
        p.notes.clear();
    song.mixer.resize (std::max<std::size_t> (song.mixer.size(), 4));
    song.mixer[1].name = "Kick/Bus";
    song.mixer[1].effects.resize (2); // two of the same effect: two files
    song.mixer[3].effects.resize (1);
    song.mixer[2].name = "Insert 2"; // (a default name alone: not exported)
    song.patterns.resize (2);
    song.patterns[1].name = "Verse";
    song.channels.resize (2);
    song.channels[0].name = "Kick";
    song.channels[1].name = "Hat";
    Note n;
    n.channel = 1;
    song.patterns[1].notes.push_back (n);
    song.patterns[1].notes.push_back (n);

    const auto bones = quickrender::projectBones (song);
    CHECK (bones.size() == 6);
    if (bones.size() != 6)
        return;
    CHECK (bones[0].folder == "Mixer" && bones[0].name == "Kick_Bus" && bones[0].track == 1);
    CHECK (bones[1].folder == "Effects" && bones[1].slot == 0);
    CHECK (bones[2].folder == "Effects" && bones[2].name == bones[1].name + " (2)");
    CHECK (bones[3].folder == "Mixer" && bones[3].name == "Insert 3");
    CHECK (bones[4].folder == "Effects" && bones[4].name == bones[1].name + " (3)");
    CHECK (bones[5].kind == quickrender::Bone::Kind::score && bones[5].name == "Verse - Hat" && bones[5].pattern == 1 && bones[5].channel == 1);
    CHECK (quickrender::safeFileName (" . ", "x") == "x");
}

// The keys (FL's ShortcutsModule): Ctrl+Alt+C / Ctrl+Shift+Alt+C consolidate, Ctrl+Alt+M masters, from any panel
TEST (rea168e_keys)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    for (auto focus : { Context::global, Context::playlist, Context::pianoRoll, Context::mixer })
    {
        CHECK (map.find ({ 'C', ctrl | alt }, focus, false) == "tools.consolidateSelection");
        CHECK (map.find ({ 'C', ctrl | shift | alt }, focus, false) == "tools.consolidateSelectionSongStart");
        CHECK (map.find ({ 'M', ctrl | alt }, focus, false) == "file.exportMaster");
    }
}
