// --- Round 190pm (research190oe P19): Note Repeats and Conditions (core/NoteConditions.h, core/NoteRepeat.h ratchets,
// core/NoteChance.h Roll passes / Fill). Through the real schedulers: Pattern mode, the Playlist (a clip's repeats, the
// song loop), a launched clip's loops (scheduleSingleClip's pass); the .flp and .als exports and the realised notes.

#include <cmath>
#include <set>
#include <vector>

#include "core/AlsExport.h"
#include "core/FlpExport.h"
#include "core/NoteChance.h"
#include "core/NoteConditions.h"
#include "core/RackRenderer.h"
#include "flp/Flp.h"

namespace rp190pm
{
constexpr double rate = 48000.0;

inline lynx::Note note (std::int64_t start, std::int64_t length, int key, float velocity = 0.8f)
{
    lynx::Note n;
    n.start = start;
    n.length = length;
    n.key = key;
    n.velocity = velocity;
    return n;
}

// one bar of `notes` on one Sampler channel
inline lynx::Song songWith (const std::vector<lynx::Note>& notes)
{
    lynx::Song s;
    s.bpm = 120.0;
    s.channels.resize (1);
    auto& p = s.patterns.emplace_back();
    p.notes = notes;
    p.lengthTicks = 4 * lynx::ppq;
    p.sortNotes();
    return s;
}

struct Event
{
    double tick = 0.0; // song ticks
    int type = 0, key = 0;
    float velocity = 0.0f;
    bool operator== (const Event&) const = default;
};

inline std::vector<Event> playPattern (const lynx::Song& s, int passes, int block, bool fill = false)
{
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    const auto& p = s.patterns[0];
    const auto length = p.effectiveLength (s.meter());
    const auto total = (std::int64_t) std::llround ((double) length * passes / tps);
    std::vector<Event> out;
    lynx::EventBuffer ev;
    for (std::int64_t at = 0; at < total; at += block)
    {
        ev.clear();
        const auto n = (int) std::min<std::int64_t> (block, total - at);
        lynx::schedulePatternLoop (p, length, (double) at * tps, n, tps, ev, 0.0f, &s.channels, false, -1.0, s.chanceSeed, fill);
        for (const auto& e : ev)
            out.push_back ({ (double) (at + e.sampleOffset) * tps, (int) e.type, e.key, e.velocity });
    }
    return out;
}

inline std::vector<Event> playSong (const lynx::Song& s, int block, std::int64_t ticks)
{
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    const auto index = lynx::makeScheduleIndex (s);
    const auto total = (std::int64_t) std::llround ((double) ticks / tps);
    std::vector<Event> out;
    lynx::EventBuffer ev;
    for (std::int64_t at = 0; at < total; at += block)
    {
        ev.clear();
        lynx::scheduleSong (s, (double) at * tps, (int) std::min<std::int64_t> (block, total - at), tps, ev, at == 0, &index, true);
        for (const auto& e : ev)
            out.push_back ({ (double) (at + e.sampleOffset) * tps, (int) e.type, e.key, e.velocity });
    }
    return out;
}

inline std::vector<Event> ons (const std::vector<Event>& events)
{
    std::vector<Event> out;
    for (const auto& e : events)
        if (e.type == (int) lynx::NoteEvent::Type::on)
            out.push_back (e);
    return out;
}

// the 1-based passes (of `passLength` ticks) with an "on"
inline std::vector<int> passesWithOn (const std::vector<Event>& events, double passLength)
{
    std::set<int> passes;
    for (const auto& e : ons (events))
        passes.insert (1 + (int) std::floor ((e.tick + 0.5) / passLength));
    return { passes.begin(), passes.end() };
}
} // namespace rp190pm

TEST (rp190pm_ratchet_of_four_on_a_quarter_note_is_four_sixteenths)
{
    using namespace rp190pm;
    auto n = note (0, lynx::ppq, 60, 0.8f);
    n.ratchets = 4;
    auto s = songWith ({ n });
    const auto ev = playPattern (s, 1, 512);
    const auto on = ons (ev);
    CHECK (on.size() == 4);
    for (std::size_t k = 0; k < on.size(); ++k)
    {
        // a sixteenth each (ppq / 4 ticks), every strike at the note's velocity (no bend)
        CHECK (std::abs (on[k].tick - (double) k * lynx::ppq / 4) <= 1.0);
        CHECK (on[k].velocity == 0.8f);
    }
    // each strike ends where the next starts ("off" before "on"), the last at the note's end
    int offs = 0;
    for (const auto& e : ev)
        offs += e.type == (int) lynx::NoteEvent::Type::off;
    CHECK (offs == 4);
    // FL's repeat rate wins when it is set (issue #208: 1/8 = rate 4 → two strikes in a quarter)
    s.patterns[0].notes[0].repeat = 4;
    CHECK (ons (playPattern (s, 1, 512)).size() == 2);
    // 1 or 0 strikes: the note as before; a note shorter than its strikes: once
    s.patterns[0].notes[0].repeat = 0;
    s.patterns[0].notes[0].ratchets = 1;
    CHECK (ons (playPattern (s, 1, 512)).size() == 1);
    s.patterns[0].notes[0].ratchets = 8;
    s.patterns[0].notes[0].length = 5;
    CHECK (ons (playPattern (s, 1, 512)).size() == 1);
}

TEST (rp190pm_ratchet_velocity_bend_is_a_line)
{
    using namespace rp190pm;
    auto n = note (0, lynx::ppq, 60, 0.8f);
    n.ratchets = 4;
    n.ratchetVelocity = -0.75f; // 0.8 → 0.2 over four strikes
    const auto on = ons (playPattern (songWith ({ n }), 1, 333));
    CHECK (on.size() == 4);
    const float expected[] = { 0.8f, 0.6f, 0.4f, 0.2f };
    for (std::size_t k = 0; k < on.size() && k < 4; ++k)
        CHECK (std::abs (on[k].velocity - expected[k]) < 1.0e-5f);
    // never below 1/127 nor above 1
    n.ratchetVelocity = -1.0f;
    CHECK (std::abs (ons (playPattern (songWith ({ n }), 1, 512)).back().velocity - 1.0f / 127.0f) < 1.0e-6f);
    n.ratchetVelocity = 1.0f;
    CHECK (ons (playPattern (songWith ({ n }), 1, 512)).back().velocity == 1.0f);
}

TEST (rp190pm_conditions_pick_the_passes)
{
    using namespace rp190pm;
    const auto bar = 4.0 * lynx::ppq;
    const auto passes = [&] (std::uint8_t condition, std::uint8_t cycle, std::uint16_t mask, bool fill = false) {
        auto n = note (lynx::ppq, lynx::ppq / 2, 64);
        n.condition = condition;
        n.cycle = cycle;
        n.cycleMask = mask;
        // a note without a condition beside it plays every pass
        auto s = songWith ({ note (0, lynx::ppq / 2, 60), n });
        const auto ev = playPattern (s, 8, 1024, fill);
        std::vector<Event> keyed;
        for (const auto& e : ev)
            if (e.key == 64)
                keyed.push_back (e);
        int plain = 0;
        for (const auto& e : ons (ev))
            plain += e.key == 60;
        CHECK (plain == 8);
        return passesWithOn (keyed, bar);
    };
    using V = std::vector<int>;
    CHECK (passes (0, 0, 0) == (V { 1, 2, 3, 4, 5, 6, 7, 8 }));
    CHECK (passes (lynx::notecondition::firstPass, 0, 0) == (V { 1 }));
    CHECK (passes (lynx::notecondition::notFirst, 0, 0) == (V { 2, 3, 4, 5, 6, 7, 8 }));
    CHECK (passes (lynx::notecondition::fill, 0, 0).empty());
    CHECK (passes (lynx::notecondition::fill, 0, 0, true) == (V { 1, 2, 3, 4, 5, 6, 7, 8 }));
    CHECK (passes (lynx::notecondition::notFill, 0, 0, true).empty());
    CHECK (passes (lynx::notecondition::notFill, 0, 0) == (V { 1, 2, 3, 4, 5, 6, 7, 8 }));
    // Elektron's 2:4 (pass 2 of every 4) and Bitwig's recurrence with passes 2 and 4 of 4 lit
    CHECK (passes (0, 4, 0b0010) == (V { 2, 6 }));
    CHECK (passes (0, 4, 0b1010) == (V { 2, 4, 6, 8 }));
    CHECK (passes (0, 2, 0b10) == (V { 2, 4, 6, 8 }));
    CHECK (passes (0, 3, 0b001) == (V { 1, 4, 7 }));
    // both must pass: not first and 1:2 → 3, 5, 7
    CHECK (passes (lynx::notecondition::notFirst, 2, 0b01) == (V { 3, 5, 7 }));
    // every "on" has its "off": a note that doesn't play sends neither
    auto n = note (0, lynx::ppq, 60);
    n.cycle = 4;
    n.cycleMask = 0b0010;
    const auto ev = playPattern (songWith ({ n }), 8, 1024);
    int on = 0, off = 0;
    for (const auto& e : ev)
        (e.type == (int) lynx::NoteEvent::Type::on ? on : off) += 1;
    CHECK (on == 2 && off == 2);
}

TEST (rp190pm_conditions_with_chance_and_ratchets_are_deterministic_per_seed)
{
    using namespace rp190pm;
    auto a = note (0, lynx::ppq, 60);
    a.ratchets = 3;
    a.ratchetVelocity = -0.5f;
    a.chance = 0.5f;
    a.velocityRange = 0.1f;
    auto b = note (2 * lynx::ppq, lynx::ppq, 62);
    b.chance = 0.6f;
    b.condition = lynx::notecondition::notFirst;
    b.cycle = 3;
    b.cycleMask = 0b011;
    auto s = songWith ({ a, b });
    s.chanceSeed = 4242;
    const auto x = playPattern (s, 64, 512);
    CHECK (playPattern (s, 64, 97) == x);  // any block size
    CHECK (playPattern (s, 64, 4096) == x);
    // a ratcheted note's strikes play together (one draw per pass) and bend from the drawn velocity
    const auto passLength = 4.0 * lynx::ppq;
    for (int pass = 0; pass < 64; ++pass)
    {
        std::vector<Event> strikes;
        for (const auto& e : ons (x))
            if (e.key == 60 && (int) std::floor ((e.tick + 0.5) / passLength) == pass)
                strikes.push_back (e);
        CHECK (strikes.empty() || strikes.size() == 3);
        if (strikes.size() == 3)
        {
            CHECK (std::abs (strikes[1].velocity - strikes[0].velocity * 0.75f) < 1.0e-5f);
            CHECK (std::abs (strikes[2].velocity - strikes[0].velocity * 0.5f) < 1.0e-5f);
        }
    }
    // b: never on the first pass, never on the third of each cycle of three
    for (const auto p : passesWithOn ([&] {
             std::vector<Event> keyed;
             for (const auto& e : x)
                 if (e.key == 62)
                     keyed.push_back (e);
             return keyed;
         }(), passLength))
        CHECK (p != 1 && (p - 1) % 3 != 2);
    // another seed draws again
    s.chanceSeed = 4243;
    CHECK (playPattern (s, 64, 512) != x);
}

TEST (rp190pm_playlist_clip_passes_song_loop_and_launched_loops)
{
    using namespace rp190pm;
    const auto bar = 4 * (std::int64_t) lynx::ppq;
    auto n = note (0, lynx::ppq, 60);
    n.cycle = 2;
    n.cycleMask = 0b01; // 1:2
    auto s = songWith ({ n });
    s.mode = lynx::PlayMode::song;
    // a clip four bars long of the one-bar pattern: it repeats the pattern (LYNX's default), passes 1 and 3 play
    lynx::Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = 4 * bar;
    s.playlist = { c };
    CHECK (passesWithOn (playSong (s, 512, 4 * bar), (double) bar) == (std::vector<int> { 1, 3 }));
    // the song loop going round again counts on: 4 passes a loop, the second loop's first pass is pass 5 (plays)
    CHECK (passesWithOn (playSong (s, 512, 8 * bar), (double) bar) == (std::vector<int> { 1, 3, 5, 7 }));
    // first pass: the clip's first pass only, in every clip of the pattern
    s.patterns[0].notes[0].cycle = 0;
    s.patterns[0].notes[0].cycleMask = 0;
    s.patterns[0].notes[0].condition = lynx::notecondition::firstPass;
    c.length = 2 * bar;
    lynx::Clip d = c;
    d.start = 2 * bar;
    s.playlist = { c, d };
    CHECK (passesWithOn (playSong (s, 512, 4 * bar), (double) bar) == (std::vector<int> { 1, 3 }));
    // a launched clip's loops (Clip-launch / Performance: scheduleSingleClip's pass): 1:2 plays on loops 0, 2, ...
    s.patterns[0].notes[0].condition = 0;
    s.patterns[0].notes[0].cycle = 2;
    s.patterns[0].notes[0].cycleMask = 0b01;
    lynx::Clip one = c;
    one.length = bar;
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    const auto index = lynx::makeScheduleIndex (s);
    for (int loop = 0; loop < 4; ++loop)
    {
        lynx::EventBuffer ev;
        lynx::scheduleSingleClip (s, one, 0, 0.0, (int) std::ceil ((double) bar / tps) + 1, tps, ev, &index, loop);
        int on = 0;
        for (const auto& e : ev)
            on += e.type == lynx::NoteEvent::Type::on;
        CHECK (on == (loop % 2 == 0 ? 1 : 0));
    }
    // ratchets in a clip: the end of the clip cuts them (the strikes of the whole note, the ones before the end)
    auto r = note (0, lynx::ppq, 62);
    r.ratchets = 4;
    s.patterns[0].notes = { r };
    one.length = lynx::ppq / 2; // half the note: two of its four strikes
    s.playlist = { one };
    s.playlist[0].length = lynx::ppq / 2;
    const auto inClip = ons (playSong (s, 256, lynx::ppq / 2));
    CHECK (inClip.size() == 2);
    if (inClip.size() == 2)
        CHECK (std::abs (inClip[1].tick - lynx::ppq / 4.0) <= 1.0);
}

TEST (rp190pm_notes_without_the_new_fields_play_as_before)
{
    using namespace rp190pm;
    // chance, FL's repeat and plain notes: the same events whatever the new code does (no roll, gain 1)
    auto a = note (0, lynx::ppq, 60, 0.7f);
    a.repeat = 7;
    auto b = note (lynx::ppq, lynx::ppq / 2, 62, 0.3f);
    b.chance = 0.4f;
    b.velocityRange = 0.2f;
    auto s = songWith ({ a, b, note (3 * lynx::ppq, lynx::ppq, 64, 1.0f) });
    s.chanceSeed = 77;
    const auto x = playPattern (s, 16, 512);
    for (const auto& e : ons (x))
    {
        if (e.key == 60)
            CHECK (e.velocity == 0.7f);
        if (e.key == 64)
            CHECK (e.velocity == 1.0f);
    }
    CHECK (std::count_if (x.begin(), x.end(), [] (const Event& e) { return e.key == 60 && e.type == (int) lynx::NoteEvent::Type::on; }) == 16 * 4);
    CHECK (! a.hasCondition() && ! a.hasRatchets() && ! lynx::chance::rolls (a) && lynx::chance::rolls (b));
}

TEST (rp190pm_presets_and_cycle_text)
{
    namespace nc = lynx::noteconditions;
    CHECK (nc::presetCount == 40);
    // every preset round-trips through presetOf, and they are all different
    std::set<std::tuple<int, int, int>> seen;
    for (int i = 0; i < nc::presetCount; ++i)
    {
        lynx::Note n;
        nc::apply (n, nc::preset (i));
        CHECK (nc::presetOf (n) == i);
        seen.insert ({ n.condition, n.cycle, n.cycleMask });
    }
    CHECK ((int) seen.size() == nc::presetCount);
    CHECK (nc::cycleText (4, 0b0010) == "2:4");
    CHECK (nc::cycleText (4, 0b1010) == "2+4:4");
    CHECK (nc::cycleText (0, 0).empty());
    std::uint8_t cycle = 9;
    std::uint16_t mask = 9;
    CHECK (nc::parseCycle ("2+4:4", cycle, mask) && cycle == 4 && mask == 0b1010);
    CHECK (nc::parseCycle ("1, 3 / 4", cycle, mask) && cycle == 4 && mask == 0b0101);
    CHECK (nc::parseCycle ("3 of 16", cycle, mask) && cycle == 16 && mask == 0b100);
    CHECK (nc::parseCycle ("", cycle, mask) && cycle == 0 && mask == 0);
    CHECK (! nc::parseCycle ("5:4", cycle, mask));
    CHECK (! nc::parseCycle ("2:17", cycle, mask));
    CHECK (! nc::parseCycle ("abc", cycle, mask));
    CHECK (! nc::parseCycle ("4", cycle, mask));
    // a custom cycle (two passes) is no preset
    lynx::Note n;
    n.cycle = 4;
    n.cycleMask = 0b1010;
    CHECK (nc::presetOf (n) == -1);
}

TEST (rp190pm_realised_notes_and_ratchets_as_notes)
{
    auto n = rp190pm::note (lynx::ppq, lynx::ppq, 60, 0.8f);
    n.ratchets = 4;
    n.ratchetVelocity = -0.5f;
    auto later = rp190pm::note (0, lynx::ppq / 2, 62);
    later.condition = lynx::notecondition::notFirst;
    auto first = rp190pm::note (2 * lynx::ppq, lynx::ppq / 2, 64);
    first.condition = lynx::notecondition::firstPass;
    const auto out = lynx::chance::realised ({ later, n, first }, 1);
    // the first pass: the not-first note is out, the first-pass note in, the ratchets as four notes (sorted)
    CHECK (out.size() == 5);
    CHECK (std::is_sorted (out.begin(), out.end(), [] (const lynx::Note& a, const lynx::Note& b) { return a.start < b.start; }));
    int strikes = 0;
    for (const auto& x : out)
    {
        CHECK (! x.hasCondition() && ! x.hasRatchets());
        if (x.key == 60)
        {
            CHECK (x.length == lynx::ppq / 4 && x.start == lynx::ppq + strikes * lynx::ppq / 4);
            CHECK (std::abs (x.velocity - 0.8f * (1.0f - 0.5f * (float) strikes / 3.0f)) < 1.0e-5f);
            ++strikes;
        }
    }
    CHECK (strikes == 4);
    // FL's rates that hold a ratchet exactly
    CHECK (lynx::noteconditions::flRateFor (n) == 0); // (a velocity bend: no)
    n.ratchetVelocity = 0.0f;
    CHECK (lynx::noteconditions::flRateFor (n) == 7); // 1/16 (Step)
    n.ratchets = 3;
    CHECK (lynx::noteconditions::flRateFor (n) == 6); // 1/8 triplet (320 ticks)
    n.ratchets = 5;
    CHECK (lynx::noteconditions::flRateFor (n) == 0); // 192 ticks: no FL rate
}

TEST (rp190pm_flp_export_writes_what_fl_can_hold)
{
    using namespace rp190pm;
    auto even = note (0, lynx::ppq, 60);
    even.ratchets = 4; // 1/16: FL's repeat rate 7
    auto odd = note (lynx::ppq, lynx::ppq, 62);
    odd.ratchets = 5; // no FL rate: five notes
    auto bent = note (2 * lynx::ppq, lynx::ppq, 64);
    bent.ratchets = 2;
    bent.ratchetVelocity = -0.5f; // a bend: two notes
    auto cond = note (3 * lynx::ppq, lynx::ppq, 65);
    cond.cycle = 2;
    cond.cycleMask = 1;
    auto s = songWith ({ even, odd, bent, cond });
    lynx::flpexport::Options options;
    lynx::flpexport::Report report;
    const auto project = flp::parseProject (lynx::flpexport::exportFlp (s, options, report));
    CHECK (! project.patterns.empty());
    if (project.patterns.empty())
        return;
    const auto& notes = project.patterns[0].notes;
    int k60 = 0, k62 = 0, k64 = 0, k65 = 0;
    for (const auto& f : notes)
    {
        k60 += f.key == 60;
        k62 += f.key == 62;
        k64 += f.key == 64;
        k65 += f.key == 65;
        if (f.key == 60)
            CHECK (f.repeats == 7);
        if (f.key == 62 || f.key == 64)
            CHECK (f.repeats == 0);
    }
    CHECK (k60 == 1 && k62 == 5 && k64 == 2 && k65 == 1);
    // the second bent strike at half the velocity (FL's 0..128)
    std::vector<int> bentVelocities;
    for (const auto& f : notes)
        if (f.key == 64)
            bentVelocities.push_back (f.velocity);
    CHECK (bentVelocities.size() == 2 && bentVelocities[0] == (int) std::lround (0.8 * 128.0) && bentVelocities[1] == (int) std::lround (0.4 * 128.0));
    const auto has = [] (const std::vector<std::string>& lines, const char* what) {
        return std::any_of (lines.begin(), lines.end(), [&] (const std::string& l) { return l.find (what) != std::string::npos; });
    };
    CHECK (has (report.exact, "as FL's note repeat at the same rate"));
    CHECK (has (report.notes, "written as their strikes"));
    CHECK (has (report.notes, "with a Condition"));
    // a project without them: no line
    lynx::flpexport::Report plain;
    lynx::flpexport::exportFlp (songWith ({ note (0, lynx::ppq, 60) }), options, plain);
    CHECK (! has (plain.notes, "Condition") && ! has (plain.notes, "ratchets") && ! has (plain.exact, "ratchets"));
}

TEST (rp190pm_als_export_bakes_ratchets_keeps_chance)
{
    using namespace rp190pm;
    auto r = note (0, lynx::ppq, 60);
    r.ratchets = 4;
    r.chance = 0.5f;
    auto c = note (2 * lynx::ppq, lynx::ppq, 62);
    c.condition = lynx::notecondition::notFirst;
    auto s = songWith ({ r, c });
    s.mode = lynx::PlayMode::song;
    lynx::Clip clip;
    clip.pattern = 0;
    clip.length = 4 * lynx::ppq;
    s.playlist = { clip };
    const auto result = lynx::exportAls (s);
    std::size_t events = 0, probabilities = 0;
    for (auto at = result.xml.find ("<MidiNoteEvent"); at != std::string::npos; at = result.xml.find ("<MidiNoteEvent", at + 1))
    {
        ++events;
        const auto end = result.xml.find ('>', at);
        if (result.xml.substr (at, end - at).find ("Probability=\"0.5\"") != std::string::npos)
            ++probabilities;
    }
    CHECK (events == 5);       // the four strikes and the condition note
    CHECK (probabilities == 4); // each strike keeps the note's Chance
    const auto noted = [&] (const char* what) {
        return std::any_of (result.notes.begin(), result.notes.end(), [&] (const std::string& l) { return l.find (what) != std::string::npos; });
    };
    CHECK (noted ("ratchets"));
    CHECK (noted ("Condition"));
}

// --- Round 190pm (research190oe P23): Touch mode's gesture state machine (core/TouchGestures.h), with synthetic fingers
#include "core/TouchGestures.h"

namespace tg190pm
{
inline std::vector<lynx::touch::Action> actions (lynx::touch::Tracker& t)
{
    std::vector<lynx::touch::Action> out;
    for (const auto& e : t.take())
        out.push_back (e.action);
    return out;
}
} // namespace tg190pm

TEST (tg190pm_tap_drag_long_press)
{
    using namespace lynx::touch;
    using tg190pm::actions;
    using A = std::vector<Action>;
    // a tap: held, then the press and the release reach the editor
    {
        Tracker t;
        CHECK (t.down (0, { 100, 100 }, 0.0));
        CHECK (actions (t).empty() && t.state() == Tracker::State::held);
        CHECK (t.move (0, { 104, 103 }, 50.0)); // within the slop: still held
        CHECK (! t.up (0, { 104, 103 }, 120.0));
        CHECK (actions (t) == (A { Action::press }));
        CHECK (t.state() == Tracker::State::idle);
    }
    // a drag past the slop: the press, then the drag and the release go to the editor as they come
    {
        Tracker t;
        t.down (0, { 100, 100 }, 0.0);
        CHECK (! t.move (0, { 100, 115 }, 30.0));
        CHECK (actions (t) == (A { Action::press }));
        CHECK (t.state() == Tracker::State::dragging);
        CHECK (! t.move (0, { 100, 140 }, 60.0));
        t.tick (2000.0); // no long press once it moves
        CHECK (actions (t).empty());
        CHECK (! t.up (0, { 100, 140 }, 900.0));
        CHECK (t.state() == Tracker::State::idle);
    }
    // a long press: 500 ms still: the right click at the first place; the finger's later events are swallowed
    {
        Tracker t;
        t.down (0, { 50, 60 }, 0.0);
        t.tick (499.0);
        CHECK (actions (t).empty());
        t.tick (500.0);
        const auto e = t.take();
        CHECK (e.size() == 1 && e[0].action == Action::longPress && e[0].at.x == 50.0f && e[0].at.y == 60.0f);
        CHECK (t.move (0, { 90, 90 }, 600.0));
        CHECK (t.up (0, { 90, 90 }, 700.0));
        CHECK (actions (t).empty() && t.state() == Tracker::State::idle);
    }
}

TEST (tg190pm_two_fingers_pan_and_pinch)
{
    using namespace lynx::touch;
    Tracker t;
    t.down (0, { 100, 100 }, 0.0);
    CHECK (t.down (1, { 200, 100 }, 40.0)); // a second finger before the first moved: two-finger mode, no press
    CHECK (t.state() == Tracker::State::twoFinger && t.take().empty());
    // both move right by 30: a pan of the centre by 15 then 15 (one finger at a time), no pinch... the distance changes
    // while one moves alone, so move them as a pair: first finger, then second
    CHECK (t.move (0, { 130, 100 }, 60.0));
    CHECK (t.move (1, { 230, 100 }, 60.0));
    float dx = 0.0f, scale = 1.0f;
    for (const auto& e : t.take())
    {
        CHECK (e.action == Action::pan || e.action == Action::pinch);
        if (e.action == Action::pan)
            dx += e.delta.x;
        else
            scale *= e.scale;
    }
    CHECK (std::abs (dx - 30.0f) < 1.0e-4f);
    CHECK (std::abs (scale - 1.0f) < 1.0e-4f); // apart by 100 before and after
    // spread to twice the distance: a pinch of 2 around the centre
    t.move (1, { 330, 100 }, 80.0);
    scale = 1.0f;
    for (const auto& e : t.take())
        if (e.action == Action::pinch)
            scale *= e.scale;
    CHECK (std::abs (scale - 2.0f) < 1.0e-4f);
    // one up: nothing; the last up ends the gesture; the press never came
    CHECK (t.up (0, { 130, 100 }, 100.0));
    CHECK (t.move (1, { 400, 100 }, 110.0)); // (one finger left: no pan)
    CHECK (t.take().empty());
    CHECK (t.up (1, { 400, 100 }, 120.0));
    const auto end = t.take();
    CHECK (end.size() == 1 && end[0].action == Action::gestureEnd && t.state() == Tracker::State::idle);
    // without two-finger gestures (outside the Simple layout) a second finger makes the first a press
    Tracker one ({ 500.0, 10.0, false });
    one.down (0, { 10, 10 }, 0.0);
    CHECK (one.down (1, { 50, 10 }, 10.0));
    const auto e = one.take();
    CHECK (e.size() == 1 && e[0].action == Action::press && one.state() == Tracker::State::dragging);
    CHECK (one.move (1, { 80, 10 }, 20.0));     // the second finger is ignored
    CHECK (! one.move (0, { 30, 10 }, 20.0));   // the first goes on as a drag
    // a finger moved first: a one-finger edit; a second finger later is ignored
    Tracker late;
    late.down (0, { 10, 10 }, 0.0);
    late.move (0, { 40, 10 }, 10.0);
    late.take();
    CHECK (late.down (1, { 90, 10 }, 20.0) && late.state() == Tracker::State::dragging && late.take().empty());
}
