// Round 187c: note Chance, Velocity Range and probability groups (Live 12's note probability; core/NoteChance.h).
// The statistics run 10 000 passes of a loop through the real schedulers; reproducibility is checked across block sizes,
// seeds, a save as .als and an import of what Live writes (Live 12.4's MidiNoteEvent attributes [DATA]).

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/NoteChance.h"
#include "core/NoteTools.h"
#include "core/RackRenderer.h"

namespace nc187c
{
constexpr double rate = 48000.0;

// a pattern of one bar with `notes`; one Sampler channel
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

inline lynx::Note note (std::int64_t start, int key, float velocity = 0.5f, float chance = 1.0f, float range = 0.0f)
{
    lynx::Note n;
    n.start = start;
    n.length = lynx::ppq / 4;
    n.key = key;
    n.velocity = velocity;
    n.chance = chance;
    n.velocityRange = range;
    return n;
}

struct Event
{
    std::int64_t sample = 0;
    int type = 0, key = 0;
    float velocity = 0.0f;
    bool operator== (const Event&) const = default;
};

// Pattern mode: `passes` loops of the pattern in blocks of `block` samples
inline std::vector<Event> playPattern (const lynx::Song& s, int passes, int block)
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
        lynx::schedulePatternLoop (p, length, (double) at * tps, n, tps, ev, 0.0f, &s.channels, false, -1.0, s.chanceSeed);
        for (const auto& e : ev)
            out.push_back ({ at + e.sampleOffset, (int) e.type, e.key, e.velocity });
    }
    return out;
}

// the "on"s per pass (pass = sample / samples per pass)
inline std::vector<std::vector<Event>> onsByPass (const lynx::Song& s, const std::vector<Event>& events, int passes)
{
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    const auto perPass = (double) s.patterns[0].effectiveLength (s.meter()) / tps;
    std::vector<std::vector<Event>> by ((std::size_t) passes);
    for (const auto& e : events)
        if (e.type == (int) lynx::NoteEvent::Type::on)
            by[(std::size_t) std::min<std::int64_t> (passes - 1, (std::int64_t) ((double) e.sample / perPass))].push_back (e);
    return by;
}

inline int count (const std::vector<Event>& events, lynx::NoteEvent::Type type, int key = -1)
{
    return (int) std::count_if (events.begin(), events.end(), [&] (const Event& e) { return e.type == (int) type && (key < 0 || e.key == key); });
}
} // namespace nc187c

TEST (nc187c_notes_without_chance_play_as_before)
{
    using namespace nc187c;
    auto s = songWith ({ note (0, 60, 0.7f), note (lynx::ppq, 62, 0.3f), note (2 * lynx::ppq, 64, 1.0f) });
    CHECK (! s.patterns[0].notes[0].hasChance());
    const auto a = playPattern (s, 8, 512);
    s.chanceSeed = 12345; // the seed doesn't touch them
    CHECK (playPattern (s, 8, 512) == a);
    CHECK (count (a, lynx::NoteEvent::Type::on) == 24 && count (a, lynx::NoteEvent::Type::off) == 24);
    for (const auto& e : a)
        if (e.type == (int) lynx::NoteEvent::Type::on)
            CHECK (e.velocity == (e.key == 60 ? 0.7f : e.key == 62 ? 0.3f : 1.0f));
}

TEST (nc187c_chance_10000_passes)
{
    using namespace nc187c;
    constexpr int passes = 10000;
    for (const auto p : { 0.3f, 0.5f, 0.9f })
    {
        // two notes of the same chance draw on their own; a 0 and a 1 never / always play
        auto s = songWith ({ note (0, 60, 0.5f, p), note (lynx::ppq, 61, 0.5f, p), note (2 * lynx::ppq, 62, 0.5f, 0.0f), note (3 * lynx::ppq, 63, 0.5f, 1.0f) });
        s.chanceSeed = 7;
        // a big block covers many passes: cheap, and the pass index comes from emitOccurrences' own count
        const auto ev = playPattern (s, passes, 8192);
        const auto on60 = count (ev, lynx::NoteEvent::Type::on, 60), on61 = count (ev, lynx::NoteEvent::Type::on, 61);
        const auto sigma = std::sqrt ((double) passes * p * (1.0 - p));
        std::printf ("  chance %.2f: %d and %d of %d passes (expected %.0f, sigma %.1f)\n", p, on60, on61, passes, passes * p, sigma);
        CHECK (std::abs (on60 - passes * p) < 4.0 * sigma);
        CHECK (std::abs (on61 - passes * p) < 4.0 * sigma);
        CHECK (count (ev, lynx::NoteEvent::Type::on, 62) == 0 && count (ev, lynx::NoteEvent::Type::off, 62) == 0);
        CHECK (count (ev, lynx::NoteEvent::Type::on, 63) == passes);
        // every "on" that plays has its "off", and a note that doesn't play sends none
        CHECK (count (ev, lynx::NoteEvent::Type::off, 60) == on60 && count (ev, lynx::NoteEvent::Type::off, 61) == on61);
        // the two notes are independent: both play in about p² of the passes
        const auto by = onsByPass (s, ev, passes);
        int both = 0;
        for (const auto& pass : by)
            both += count (pass, lynx::NoteEvent::Type::on, 60) == 1 && count (pass, lynx::NoteEvent::Type::on, 61) == 1;
        const auto pp = (double) p * p;
        CHECK (std::abs (both - passes * pp) < 4.0 * std::sqrt (passes * pp * (1.0 - pp)) + 1.0);
        // and from one pass to the next (no stuck draw): runs of "played" as long as a fair coin's
        int changes = 0;
        for (int k = 1; k < passes; ++k)
            changes += (count (by[(std::size_t) k], lynx::NoteEvent::Type::on, 60) == 1) != (count (by[(std::size_t) k - 1], lynx::NoteEvent::Type::on, 60) == 1);
        const auto expectedChanges = 2.0 * p * (1.0 - p) * (passes - 1);
        CHECK (std::abs (changes - expectedChanges) < 5.0 * std::sqrt (expectedChanges) + 1.0);
    }
}

TEST (nc187c_velocity_range_is_even_between_velocity_and_velocity_plus_range)
{
    using namespace nc187c;
    constexpr int passes = 10000;
    // Live's manual: +20 on velocity 60 plays between 60 and 80; a negative range goes down
    for (const auto range : { 20.0f / 127.0f, -30.0f / 127.0f })
    {
        auto s = songWith ({ note (0, 60, 60.0f / 127.0f, 1.0f, range) });
        s.chanceSeed = 99;
        const auto ev = playPattern (s, passes, 8192);
        double sum = 0.0, lo = 2.0, hi = -1.0;
        int quarter[4] = {};
        for (const auto& e : ev)
            if (e.type == (int) lynx::NoteEvent::Type::on)
            {
                sum += e.velocity;
                lo = std::min (lo, (double) e.velocity);
                hi = std::max (hi, (double) e.velocity);
                const auto u = ((double) e.velocity - 60.0 / 127.0) / (double) range;
                ++quarter[std::clamp ((int) (u * 4.0), 0, 3)];
            }
        CHECK (count (ev, lynx::NoteEvent::Type::on) == passes);
        const auto mean = sum / passes, expected = 60.0 / 127.0 + range * 0.5;
        std::printf ("  velocity range %+.0f: mean %.2f (expected %.2f), %.2f..%.2f, quarters %d %d %d %d\n", range * 127.0, mean * 127.0,
                     expected * 127.0, lo * 127.0, hi * 127.0, quarter[0], quarter[1], quarter[2], quarter[3]);
        CHECK (std::abs (mean - expected) < 0.01 * std::abs (range) + 0.002);
        CHECK (std::min (lo, hi) >= std::min (60.0, 60.0 + range * 127.0) / 127.0 - 1e-6);
        CHECK (std::max (lo, hi) <= std::max (60.0, 60.0 + range * 127.0) / 127.0 + 1e-6);
        for (auto q : quarter)
            CHECK (std::abs (q - passes / 4) < 4.0 * std::sqrt (passes * 0.25 * 0.75));
    }
    // never below Live's lowest velocity (1) nor above 127
    auto s = songWith ({ note (0, 60, 5.0f / 127.0f, 1.0f, -1.0f), note (lynx::ppq, 61, 120.0f / 127.0f, 1.0f, 1.0f) });
    for (const auto& e : playPattern (s, 500, 8192))
        if (e.type == (int) lynx::NoteEvent::Type::on)
            CHECK (e.velocity >= 1.0f / 127.0f - 1e-6f && e.velocity <= 1.0f);
}

TEST (nc187c_probability_groups_play_all_and_play_one)
{
    using namespace nc187c;
    constexpr int passes = 10000;
    // Play all, chance 0.5: each pass plays the three notes or none
    {
        std::vector<lynx::Note> notes { note (0, 60, 0.5f, 0.5f), note (lynx::ppq, 64, 0.5f, 0.5f), note (2 * lynx::ppq, 67, 0.5f, 0.5f) };
        for (auto& n : notes)
            n.chanceGroup = 1;
        auto s = songWith (notes);
        const auto ev = playPattern (s, passes, 8192);
        int all = 0, mixed = 0;
        for (const auto& pass : onsByPass (s, ev, passes))
        {
            all += pass.size() == 3;
            mixed += pass.size() != 3 && ! pass.empty();
        }
        std::printf ("  play all: %d of %d passes played the group, %d mixed\n", all, passes, mixed);
        CHECK (mixed == 0);
        CHECK (std::abs (all - passes / 2) < 4.0 * std::sqrt (passes * 0.25));
    }
    // Play one, chance 1: exactly one note a pass, each a third of the time; another group and a free note beside it
    {
        std::vector<lynx::Note> notes { note (0, 60), note (lynx::ppq, 64), note (2 * lynx::ppq, 67), note (0, 72), note (lynx::ppq, 76), note (3 * lynx::ppq, 48) };
        for (int i = 0; i < 3; ++i)
            notes[(std::size_t) i].chanceGroup = 1, notes[(std::size_t) i].chanceOne = true;
        for (int i = 3; i < 5; ++i)
            notes[(std::size_t) i].chanceGroup = 2, notes[(std::size_t) i].chanceOne = true;
        auto s = songWith (notes);
        const auto ev = playPattern (s, passes, 8192);
        std::map<int, int> byKey;
        bool exactlyOne = true;
        for (const auto& pass : onsByPass (s, ev, passes))
        {
            int g1 = 0, g2 = 0, free = 0;
            for (const auto& e : pass)
            {
                ++byKey[e.key];
                g1 += e.key == 60 || e.key == 64 || e.key == 67;
                g2 += e.key == 72 || e.key == 76;
                free += e.key == 48;
            }
            exactlyOne = exactlyOne && g1 == 1 && g2 == 1 && free == 1;
        }
        std::printf ("  play one: C %d, E %d, G %d | %d %d\n", byKey[60], byKey[64], byKey[67], byKey[72], byKey[76]);
        CHECK (exactlyOne);
        for (const auto key : { 60, 64, 67 })
            CHECK (std::abs (byKey[key] - passes / 3.0) < 4.0 * std::sqrt (passes * (1.0 / 3.0) * (2.0 / 3.0)));
        for (const auto key : { 72, 76 })
            CHECK (std::abs (byKey[key] - passes / 2.0) < 4.0 * std::sqrt (passes * 0.25));
    }
}

TEST (nc187c_draws_are_reproducible_and_seeded)
{
    using namespace nc187c;
    auto s = songWith ({ note (0, 60, 0.5f, 0.5f, 0.2f), note (lynx::ppq / 2, 62, 0.6f, 0.3f), note (lynx::ppq * 3, 64, 0.4f, 0.7f, -0.1f) });
    s.chanceSeed = 2026;
    // the same whatever the block size: the draw belongs to the pass, not to the block
    const auto a = playPattern (s, 64, 512);
    CHECK (playPattern (s, 64, 333) == a);
    CHECK (playPattern (s, 64, 4096) == a);
    // another seed plays otherwise
    auto t = s;
    t.chanceSeed = 2027;
    CHECK (playPattern (t, 64, 512) != a);
    // an edit to one note leaves the others' draws alone
    auto u = s;
    u.patterns[0].notes[2].velocity = 0.9f;
    const auto b = playPattern (u, 64, 512);
    const auto keyOnly = [] (const std::vector<Event>& v, int key) {
        std::vector<std::int64_t> r;
        for (const auto& e : v)
            if (e.key == key && e.type == (int) lynx::NoteEvent::Type::on)
                r.push_back (e.sample);
        return r;
    };
    CHECK (keyOnly (a, 60) == keyOnly (b, 60) && keyOnly (a, 62) == keyOnly (b, 62));
}

TEST (nc187c_song_clips_draw_on_their_own)
{
    using namespace nc187c;
    auto s = songWith ({ note (0, 60, 0.8f, 0.5f), note (lynx::ppq, 62, 0.8f, 0.5f), note (2 * lynx::ppq, 64, 0.8f, 0.5f), note (3 * lynx::ppq, 65, 0.8f, 0.5f) });
    s.mode = lynx::PlayMode::song;
    s.chanceSeed = 5;
    // 64 clips of the pattern one after the other: 256 notes at 0.5
    for (int k = 0; k < 64; ++k)
    {
        lynx::Clip c;
        c.pattern = 0;
        c.start = (std::int64_t) k * 4 * lynx::ppq;
        c.length = 4 * lynx::ppq;
        s.playlist.push_back (c);
    }
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    const auto index = lynx::makeScheduleIndex (s);
    auto play = [&] (const lynx::Song& song, int block) {
        std::vector<Event> out;
        lynx::EventBuffer ev;
        const auto total = (std::int64_t) std::llround ((double) index.songLength / tps);
        for (std::int64_t at = 0; at < total; at += block)
        {
            ev.clear();
            lynx::scheduleSong (song, (double) at * tps, (int) std::min<std::int64_t> (block, total - at), tps, ev, at == 0, &index, false);
            for (const auto& e : ev)
                out.push_back ({ at + e.sampleOffset, (int) e.type, e.key, e.velocity });
        }
        return out;
    };
    const auto a = play (s, 512);
    const auto ons = count (a, lynx::NoteEvent::Type::on);
    std::printf ("  64 clips: %d of 256 notes played\n", ons);
    CHECK (std::abs (ons - 128) < 4.0 * 8.0);
    CHECK (count (a, lynx::NoteEvent::Type::off) == ons);
    CHECK (play (s, 700) == a);
    // the clips don't all repeat the first clip's pattern of draws
    const auto perClip = (double) 4 * lynx::ppq / tps;
    std::set<int> masks;
    for (int k = 0; k < 64; ++k)
    {
        int mask = 0;
        for (const auto& e : a)
            if (e.type == (int) lynx::NoteEvent::Type::on && (int) ((double) e.sample / perClip) == k)
                mask |= 1 << (e.key - 60);
        masks.insert (mask);
    }
    CHECK (masks.size() >= 8);

    // the realised notes (MIDI export): the first pass as it plays
    const auto real = lynx::chance::realised (s.patterns[0].notes, 5);
    CHECK (real.size() <= 4);
    for (const auto& n : real)
        CHECK (! n.hasChance());
    CHECK (lynx::chance::realised (s.patterns[0].notes, 5).size() == real.size());
}

TEST (nc187c_als_probability_and_velocity_deviation_round_trip)
{
    using namespace nc187c;
    // what Live 12.4 writes (attribute order of a Live-saved set [DATA]: Time Duration Velocity VelocityDeviation
    // OffVelocity Probability IsEnabled NoteId; Live 12 leaves out the defaults)
    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::song;
    auto& p = s.patterns[0];
    p.notes.clear();
    p.notes = { note (0, 60, 100.0f / 127.0f, 0.7083332539f), note (lynx::ppq, 62, 60.0f / 127.0f, 1.0f, 20.0f / 127.0f),
                note (2 * lynx::ppq, 64, 90.0f / 127.0f, 0.25f, -12.0f / 127.0f), note (3 * lynx::ppq, 65, 0.5f) };
    for (auto& n : p.notes)
        n.channel = 0;
    p.notes[1].release = 30.0f / 127.0f;
    p.lengthTicks = 4 * lynx::ppq;
    s.playlist.clear();
    lynx::Clip c;
    c.pattern = 0;
    c.length = 4 * lynx::ppq;
    s.playlist.push_back (c);
    for (const auto version : { lynx::AlsVersion::live12, lynx::AlsVersion::live11 })
    {
        lynx::AlsExportOptions o;
        o.version = version;
        const auto out = lynx::exportAls (s, o);
        const auto has = [&out] (const char* text) { return out.xml.find (text) != std::string::npos; };
        CHECK (has ("OffVelocity=\"63.5\" Probability=\"0.70833325"));
        CHECK (has ("VelocityDeviation=\"20\" OffVelocity=\"30.0000"));
        CHECK (has ("VelocityDeviation=\"-12\" OffVelocity=\"63.5\" Probability=\"0.25\""));
        if (version == lynx::AlsVersion::live12) // the defaults left out
            CHECK (has ("Velocity=\"63.5\" OffVelocity=\"63.5\" NoteId"));
        else
            CHECK (has ("Velocity=\"63.5\" VelocityDeviation=\"0\" OffVelocity=\"63.5\" Probability=\"1\" IsEnabled=\"true\""));
        std::string error;
        const auto back = lynx::importAls (out.bytes, {}, &error);
        CHECK (back.has_value());
        if (! back)
            continue;
        std::vector<lynx::Note> got;
        for (const auto& pat : back->patterns)
            got.insert (got.end(), pat.notes.begin(), pat.notes.end());
        std::sort (got.begin(), got.end(), [] (const lynx::Note& a, const lynx::Note& b) { return a.key < b.key; });
        CHECK (got.size() == 4);
        if (got.size() != 4)
            continue;
        CHECK (std::abs (got[0].chance - 0.7083332539f) < 1e-5f && got[0].velocityRange == 0.0f);
        CHECK (got[1].chance == 1.0f && std::abs (got[1].velocityRange * 127.0f - 20.0f) < 1e-3f);
        CHECK (std::abs (got[1].release * 127.0f - 30.0f) < 1e-3f);
        CHECK (std::abs (got[2].chance - 0.25f) < 1e-6f && std::abs (got[2].velocityRange * 127.0f + 12.0f) < 1e-3f);
        CHECK (! got[3].hasChance());
    }
}

TEST (nc187c_renders_are_reproducible)
{
    using namespace nc187c;
    // a synth plays a bar of 16th notes at chance 0.5 with a velocity range, Pattern mode, four passes
    std::vector<lynx::Note> notes;
    for (int k = 0; k < 16; ++k)
        notes.push_back (note ((std::int64_t) k * lynx::ppq / 4, 60 + (k % 5), 0.6f, 0.5f, 0.3f));
    auto s = songWith (notes);
    s.channels[0].instrument = lynx::InstrumentKind::synth;
    s.mixer.resize (2);
    s.chanceSeed = 11;
    auto render = [] (const lynx::Song& song, int block) {
        auto state = lynx::RackState::build (song, nullptr, rate);
        lynx::RackRenderer rr;
        rr.prepare (rate, 1024);
        const auto total = (int) (8.0 * rate);
        std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
        const auto tps = lynx::ticksPerSample (song.bpm, rate);
        double tick = 0.0;
        for (int at = 0; at < total; at += block)
        {
            const auto n = std::min (block, total - at);
            rr.process (state.get(), true, tick, n, l.data() + at, r.data() + at);
            tick += n * tps;
        }
        return l;
    };
    const auto a = render (s, 512);
    double energy = 0.0;
    for (auto v : a)
        energy += (double) v * v;
    CHECK (energy > 0.0);
    CHECK (render (s, 512) == a); // the same project renders the same, sample for sample
    auto t = s;
    t.chanceSeed = 12;
    CHECK (render (t, 512) != a); // another seed, other draws
    // with every chance at 1 and no range the notes play exactly as before the feature
    auto plain = s, full = s;
    for (auto& n : plain.patterns[0].notes)
        n.chance = 1.0f, n.velocityRange = 0.0f;
    for (auto& n : full.patterns[0].notes)
        n.chance = 1.0f, n.velocityRange = 0.0f;
    full.chanceSeed = 999;
    CHECK (render (plain, 512) == render (full, 512));
}

TEST (nc187c_group_tools)
{
    using namespace nc187c;
    lynx::Pattern p;
    p.notes = { note (0, 60, 0.5f, 0.4f), note (0, 64, 0.5f, 0.9f), note (0, 67), note (lynx::ppq, 60) };
    lynx::notetools::groupChance (p, { 0, 1, 2 }, true);
    CHECK (p.notes[0].chanceGroup == 1 && p.notes[1].chanceGroup == 1 && p.notes[2].chanceGroup == 1 && p.notes[3].chanceGroup == 0);
    CHECK (p.notes[0].chance == 1.0f && p.notes[1].chance == 1.0f && p.notes[2].chanceOne); // a new group is at 100 % (Live)
    p.notes[2].chance = 0.75f;
    lynx::notetools::spreadGroupChance (p, { 2 });
    CHECK (p.notes[0].chance == 0.75f && p.notes[1].chance == 0.75f && p.notes[3].chance == 1.0f);
    lynx::notetools::groupChance (p, { 3 }, false); // one note: no group
    CHECK (p.notes[3].chanceGroup == 0);
    // pasted copies of a group form a group of their own
    p.notes.push_back (p.notes[0]);
    p.notes.push_back (p.notes[1]);
    lynx::notetools::regroup (p, { 4, 5 });
    CHECK (p.notes[4].chanceGroup == 2 && p.notes[5].chanceGroup == 2 && p.notes[0].chanceGroup == 1);
    lynx::notetools::ungroupChance (p, { 0, 1, 2 });
    CHECK (p.notes[0].chanceGroup == 0 && ! p.notes[2].chanceOne && p.notes[0].chance == 0.75f);
}

TEST (nc187c_als_probability_groups)
{
    using namespace nc187c;
    // the test set of round 187c: C/D Play One and E/F# Play All, as Live 12.4 saved it (live/nc-saved Project)
    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::song;
    auto& p = s.patterns[0];
    p.notes.clear();
    for (int k = 0; k < 8; ++k)
    {
        auto n = note ((std::int64_t) k * lynx::ppq / 2, 60 + (k % 4) * 2, (60.0f + 8.0f * k) / 127.0f, k == 5 ? 0.1f : 0.5f);
        n.chanceGroup = (k % 4) < 2 ? 1 : 2;
        n.chanceOne = (k % 4) < 2;
        n.chance = 1.0f;
        p.notes.push_back (n);
    }
    p.notes[2].chance = p.notes[3].chance = p.notes[6].chance = p.notes[7].chance = 0.5f; // the Play All group at 50 %
    p.lengthTicks = 4 * lynx::ppq;
    s.playlist.clear();
    lynx::Clip c;
    c.pattern = 0;
    c.length = 4 * lynx::ppq;
    s.playlist.push_back (c);
    const auto out = lynx::exportAls (s, {});
    const auto has = [&out] (const char* text) { return out.xml.find (text) != std::string::npos; };
    // NoteIds go key by key (C: 1 2, D: 3 4, E: 5 6, F#: 7 8), as Live numbers them
    CHECK (has ("<NoteProbabilityGroup Id=\"0\">"));
    CHECK (has ("<GroupType Value=\"1\" />"));
    CHECK (has ("<GroupType Value=\"0\" />"));
    CHECK (has ("<NoteId NoteId=\"4\" />"));
    CHECK (has ("<Probability Value=\"0.5\" />"));
    CHECK (has ("<GroupId Value=\"2\" />"));
    std::string error;
    auto check = [&] (const std::vector<std::uint8_t>& bytes, float allChance) {
        const auto back = lynx::importAls (bytes, {}, &error);
        CHECK (back.has_value());
        if (! back)
            return;
        std::vector<lynx::Note> got;
        for (const auto& pat : back->patterns)
            got.insert (got.end(), pat.notes.begin(), pat.notes.end());
        CHECK (got.size() == 8);
        for (const auto& n : got)
        {
            const auto cd = n.key == 60 || n.key == 62;
            CHECK (n.chanceGroup > 0 && n.chanceOne == cd);
            CHECK (n.chance == (cd ? 1.0f : allChance));
        }
        std::set<int> ids;
        for (const auto& n : got)
            ids.insert (n.chanceGroup);
        CHECK (ids.size() == 2);
    };
    check (out.bytes, 0.5f);
    // Live's own block (12.4 save of this set, groups as above at 100 %), put in place of LYNX's
    auto xml = out.xml;
    const auto from = xml.find ("<NoteProbabilityGroups>"), to = xml.find ("</NoteProbabilityGroups>");
    CHECK (from != std::string::npos && to != std::string::npos);
    if (from == std::string::npos || to == std::string::npos)
        return;
    xml.replace (from, to - from, "<NoteProbabilityGroups><NoteProbabilityGroup Id=\"0\"><GroupType Value=\"1\" /><Notes><NoteId NoteId=\"1\" />"
                                  "<NoteId NoteId=\"2\" /><NoteId NoteId=\"3\" /><NoteId NoteId=\"4\" /></Notes><Probability Value=\"1\" />"
                                  "<GroupId Value=\"1\" /></NoteProbabilityGroup><NoteProbabilityGroup Id=\"2\"><GroupType Value=\"0\" /><Notes>"
                                  "<NoteId NoteId=\"5\" /><NoteId NoteId=\"6\" /><NoteId NoteId=\"7\" /><NoteId NoteId=\"8\" /></Notes>"
                                  "<Probability Value=\"1\" /><GroupId Value=\"3\" /></NoteProbabilityGroup>");
    check (std::vector<std::uint8_t> (xml.begin(), xml.end()), 1.0f);
}
