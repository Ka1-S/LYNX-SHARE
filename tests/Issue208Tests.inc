// --- Round 190ma: issue #208, the Channel Rack graph editor's Note pitch, Shift and Repeat (FL Studio 2026) -----------

#include "core/ChannelBurn.h"
#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/NoteTools.h"
#include "core/RackOps.h"
#include "core/Scheduler.h"
#include "core/NoteRepeat.h"

namespace i208
{
lynx::Note step (int index, int repeat = 0, std::int64_t length = lynx::ticksPerStep)
{
    lynx::Note n;
    n.start = (std::int64_t) index * lynx::ticksPerStep;
    n.length = length;
    n.repeat = repeat;
    return n;
}

std::vector<std::int64_t> strikes (const lynx::Note& n)
{
    std::vector<std::int64_t> out;
    lynx::noterepeat::forEachStrike (n, n.start, n.length, [&] (std::int64_t at, std::int64_t, int) { out.push_back (at); });
    return out;
}
} // namespace i208

TEST (i208_repeat_rates_are_fls_table)
{
    using namespace lynx::noterepeat;
    // FL's table (FLEngine 0x154a138): index 0 = none, then 1/4 beat ... 1/64 triplet
    CHECK (std::string (rateName (0)) == "No repeat" && std::string (rateName (1)) == "1/4 (Beat)");
    CHECK (std::string (rateName (7)) == "1/16 (Step)" && std::string (rateName (10)) == "1/32 (Half step)");
    CHECK (std::string (rateName (14)) == "1/64 triplet" && std::string (rateName (15)).empty());
    CHECK (interval (0) == 0 && interval (1) == lynx::ppq && interval (4) == lynx::ppq / 2);
    CHECK (interval (7) == lynx::ticksPerStep && interval (10) == lynx::ticksPerStep / 2 && interval (13) == lynx::ticksPerStep / 4);
    CHECK (interval (2) == lynx::ppq * 3 / 4 && interval (3) == lynx::ppq * 2 / 3 && interval (14) == lynx::ppq / 24);
    for (int i = 2; i <= rateCount; ++i)
        CHECK (interval (i) < interval (i - 1)); // (faster and faster)
}

TEST (i208_a_repeated_note_is_struck_at_its_rate_while_it_lasts)
{
    using namespace lynx;
    // one beat at 1/16: four strikes, each to the next
    auto n = i208::step (0, 7, ppq);
    CHECK ((i208::strikes (n) == std::vector<std::int64_t> { 0, 240, 480, 720 }));
    // a one-step note at 1/32: two; at 1/64: four
    CHECK (i208::strikes (i208::step (2, 10)).size() == 2 && i208::strikes (i208::step (2, 13)).size() == 4);
    // no repeat, or a rate slower than the note: the note itself
    CHECK ((i208::strikes (i208::step (3, 0)) == std::vector<std::int64_t> { 720 }));
    CHECK ((i208::strikes (i208::step (3, 1)) == std::vector<std::int64_t> { 720 }));
    CHECK (noterepeat::strikeCount (n, n.length) == 4 && noterepeat::strikeCount (i208::step (0, 0), ticksPerStep) == 1);
    // the strikes' lengths add up to the note's
    std::int64_t total = 0;
    noterepeat::forEachStrike (n, 0, 1000, [&] (std::int64_t, std::int64_t length, int) { total += length; });
    CHECK (total == 1000);
}

TEST (i208_scheduler_plays_every_strike)
{
    using namespace lynx;
    Pattern p;
    p.notes.push_back (i208::step (0, 7, ppq)); // one beat at 1/16
    const auto tps = 960.0 * 2.0 / 44100.0;      // 120 BPM, 44.1 kHz
    EventBuffer out;
    schedulePatternLoop (p, 3840, 0.0, 24000, tps, out); // (past the note's end: its last "off" too)
    std::vector<int> ons, offs;
    for (const auto& e : out)
        (e.type == NoteEvent::Type::on ? ons : offs).push_back (e.sampleOffset);
    CHECK (ons.size() == 4 && offs.size() == 4);
    if (ons.size() == 4)
        for (int k = 0; k < 4; ++k)
            CHECK (std::abs (ons[(std::size_t) k] - (int) std::lround (k * 240 / tps)) <= 1);
    // an "off" and the next "on" on the same sample: the off first (the voice is cut, then struck again)
    for (int i = 1; i < out.size(); ++i)
        if (out[i].sampleOffset == out[i - 1].sampleOffset && out[i].type == NoteEvent::Type::off)
            CHECK (out[i - 1].type != NoteEvent::Type::on);
}

TEST (i208_scheduler_plays_strikes_of_a_playlist_clip)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.clear();
    Pattern p;
    p.notes.push_back (i208::step (0, 10)); // one step at 1/32: two strikes
    song.patterns.push_back (p);
    song.playlist.clear();
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 0;
    c.start = 0;
    c.length = ppq * 4;
    song.playlist.push_back (c);
    const auto tps = 960.0 * 2.0 / 44100.0;
    EventBuffer out;
    scheduleSong (song, 0.0, 8192, tps, out);
    int ons = 0;
    for (const auto& e : out)
        ons += e.type == NoteEvent::Type::on ? 1 : 0;
    CHECK (ons == 2);
}

TEST (i208_graph_lanes_in_fls_order)
{
    using namespace lynx::notetools;
    const Level fl[] { Level::notePitch, Level::velocity, Level::release, Level::finePitch, Level::pan, Level::modX, Level::modY, Level::shift, Level::repeat };
    for (int tab = 0; tab < stepLevelCount; ++tab)
        CHECK (stepLevel (tab) == fl[tab]);
    CHECK (std::string (stepLevelName (0)) == "Note pitch" && std::string (stepLevelName (2)) == "Release velocity");
    CHECK (std::string (stepLevelName (4)) == "Panning" && std::string (stepLevelName (8)) == "Repeat");
    CHECK (std::string (stepLevelName (2, 1)) == "Release" && std::string (stepLevelName (8, 1)) == "Repeats");
    CHECK (std::string (stepLevelName (0, 0)) == "Note" && std::string (stepLevelName (8, 0)) == "Rep" && std::string (stepLevelName (5, 0)) == "X");
    // round 190lx's lane ids and order (its Channel Rack code reads these)
    CHECK (graphNote == 6 && graphShift == 7 && graphRepeat == 8 && graphLaneCount == 9);
    for (int tab = 0; tab < graphLaneCount; ++tab)
        CHECK (graphTabOrder[tab] == (int) stepLevel (tab));
    lynx::Note n;
    setGraphValue (n, graphShift, 0.5f);
    CHECK (std::abs (graphValue (n, graphShift) - 0.5f) < 0.01f && graphDefault (graphShift) == 0.0f && ! graphIsBipolar (graphShift));
    CHECK (std::string (graphLaneName (graphRepeat)) == "Repeat");
}

TEST (i208_note_pitch_shift_and_repeat_levels)
{
    using namespace lynx;
    using namespace lynx::notetools;
    auto n = i208::step (5);
    // Note pitch: the key
    setLevelValue (n, Level::notePitch, 72.0f / 127.0f);
    CHECK (n.key == 72 && std::abs (levelValue (n, Level::notePitch) - 72.0f / 127.0f) < 1e-6f);
    CHECK (levelText (Level::notePitch, 60.0f / 127.0f) == "C5"); // (FL's C5 = 60)
    // Shift: the note moves inside its step, never into the next one
    // (FL's manual: 100 % shift is 99 % of a step; round 190lx's mapping too)
    setLevelValue (n, Level::shift, 0.5f);
    CHECK (n.start == 5 * ticksPerStep + std::llround (0.5 * 0.99 * ticksPerStep));
    setLevelValue (n, Level::shift, 1.0f);
    CHECK (n.start == 5 * ticksPerStep + std::llround (0.99 * ticksPerStep) && n.start < 6 * ticksPerStep);
    CHECK (std::abs (levelValue (n, Level::shift) - 1.0f) < 1e-3f);
    setLevelValue (n, Level::shift, 0.0f);
    CHECK (n.start == 5 * ticksPerStep && levelDefault (Level::shift) == 0.0f);
    CHECK (levelText (Level::shift, 0.25f) == "0.25");
    // Repeat: FL's rate index
    setLevelValue (n, Level::repeat, 12.0f / 14.0f);
    CHECK (n.repeat == 12 && levelText (Level::repeat, 12.0f / 14.0f) == "1/32 triplet");
    CHECK (levelDefault (Level::repeat) == 0.0f && levelText (Level::repeat, 0.0f) == "No repeat");
    CHECK (levelText (Level::velocity, 0.5f).empty()); // (the other lanes: the caller's own text)
    // a shifted step is still that step's note (the Channel Rack finds it there)
    Pattern p;
    p.notes.push_back (n);
    setLevelValue (p.notes[0], Level::shift, 0.9f);
    CHECK (notesInStep (p, 0, 5).size() == 1 && notesInStep (p, 0, 6).empty());
}

TEST (i208_flp_round_trip_of_repeat_and_levels)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.clear();
    Pattern p;
    auto a = i208::step (0, 12);
    a.release = 0.25f;
    a.modX = 0.75f;
    a.modY = 0.125f;
    auto b = i208::step (1, 4);
    b.start += 60; // (shifted: a quarter step)
    b.key = 67;
    p.notes = { a, b };
    song.patterns.push_back (p);
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    bool found12 = false, found4 = false;
    for (const auto& fp : project.patterns)
        for (const auto& n : fp.notes)
        {
            found12 = found12 || n.repeats == 12;
            found4 = found4 || (n.repeats == 4 && n.key == 67);
        }
    CHECK (found12 && found4);
    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (project, io);
    bool ok12 = false, ok4 = false;
    for (const auto& bp : back.patterns)
        for (const auto& n : bp.notes)
        {
            if (n.repeat == 12)
                ok12 = std::abs (n.release - 0.25f) < 0.01f && std::abs (n.modX - 0.75f) < 0.01f && std::abs (n.modY - 0.125f) < 0.01f;
            if (n.repeat == 4)
                ok4 = n.key == 67 && n.start == ticksPerStep + 60;
        }
    CHECK (ok12 && ok4);
}

TEST (i208_burn_to_midi_writes_each_strike)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.clear();
    Pattern p;
    p.notes.push_back (i208::step (0, 7, ppq)); // four strikes
    song.patterns.push_back (p);
    const auto played = channelburn::playedNotes (song, 0, 0);
    CHECK (played.size() == 4);
    for (const auto& n : played)
        CHECK (n.repeat == 0 && n.length == ticksPerStep);
}
