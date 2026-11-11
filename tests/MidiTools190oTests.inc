// Round 190o: Live 12's MIDI Transformations (core/MidiTools.h) and the .als round trip of the project key
// (core/LiveScales.h). The expected notes are what Live 12.4.16 saved after applying each tool to the same 7-note clip
// (/mnt/e/lynx-work/ableton190o/live/mt-saved Project/mt-saved.als; "time:key/length/velocity" in beats and 1..127).

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/LiveScales.h"
#include "core/MidiTools.h"

namespace mt190o
{
struct Live
{
    double time;
    int key;
    double length, velocity;
};

inline lynx::Note note (double beats, int key, double lengthBeats, int velocity)
{
    lynx::Note n;
    n.start = (std::int64_t) std::llround (beats * lynx::ppq);
    n.length = (std::int64_t) std::llround (lengthBeats * lynx::ppq);
    n.key = key;
    n.velocity = (float) velocity / 127.0f;
    n.release = 64.0f / 127.0f;
    return n;
}

// the clip every tool got: a C major chord, then D, F, A, C
inline lynx::miditools::Notes input()
{
    return { note (0, 60, 1, 100), note (0, 64, 1, 100), note (0, 67, 1, 100), note (1, 62, 0.5, 80),
             note (2, 65, 0.5, 90), note (2.5, 69, 0.25, 70), note (3, 72, 1, 110) };
}

constexpr std::int64_t grid = lynx::ppq / 4; // the clip's grid was 1/16

// true when `got` is Live's note list: same order (time, then key), times and lengths within a tick, velocity within 0.01
inline bool same (const lynx::miditools::Notes& got, std::vector<Live> live)
{
    if (got.size() != live.size())
    {
        std::printf ("  %d notes, Live has %d\n", (int) got.size(), (int) live.size());
        return false;
    }
    std::stable_sort (live.begin(), live.end(), [] (const Live& a, const Live& b) {
        const auto ta = std::llround (a.time * lynx::ppq), tb = std::llround (b.time * lynx::ppq);
        return ta != tb ? ta < tb : a.key < b.key;
    });
    for (std::size_t i = 0; i < got.size(); ++i)
    {
        const auto& g = got[i];
        const auto& l = live[i];
        if (std::abs ((double) g.start - l.time * lynx::ppq) > 1.0 || g.key != l.key || std::abs ((double) g.length - l.length * lynx::ppq) > 1.0
            || std::abs ((double) g.velocity * 127.0 - l.velocity) > 0.01)
        {
            std::printf ("  note %d: %g:%d/%g/%g, Live %g:%d/%g/%g\n", (int) i, (double) g.start / lynx::ppq, g.key,
                         (double) g.length / lynx::ppq, (double) g.velocity * 127.0, l.time, l.key, l.length, l.velocity);
            return false;
        }
    }
    return true;
}
} // namespace mt190o

TEST (mt190o_span_legato_and_staccato_as_live)
{
    using namespace mt190o;
    using lynx::miditools::SpanOptions;
    CHECK (same (lynx::miditools::span (input(), { SpanOptions::tenuto }, grid),
                 { { 0, 60, 1, 100 }, { 0, 64, 1, 100 }, { 0, 67, 1, 100 }, { 1, 62, 0.5, 80 }, { 2, 65, 0.5, 90 }, { 2.5, 69, 0.25, 70 }, { 3, 72, 1, 110 } }));
    CHECK (same (lynx::miditools::span (input(), { SpanOptions::legato }, grid), // clip 06
                 { { 0, 60, 1, 100 }, { 0, 64, 1, 100 }, { 0, 67, 1, 100 }, { 1, 62, 1, 80 }, { 2, 65, 0.5, 90 }, { 2.5, 69, 0.5, 70 }, { 3, 72, 1, 110 } }));
    CHECK (same (lynx::miditools::span (input(), { SpanOptions::staccato }, grid), // clip 13
                 { { 0, 60, 0.25, 100 }, { 0, 64, 0.25, 100 }, { 0, 67, 0.25, 100 }, { 1, 62, 0.25, 80 }, { 2, 65, 0.25, 90 }, { 2.5, 69, 0.25, 70 }, { 3, 72, 0.25, 110 } }));
    // [MAN] the offset is a share of a grid step added to every end; a note never gets shorter than a tick
    SpanOptions longer { SpanOptions::tenuto, 0.5f };
    CHECK (lynx::miditools::span (input(), longer, grid)[3].length == lynx::ppq / 2 + grid / 2);
    SpanOptions shorter { SpanOptions::staccato, -1.0f };
    CHECK (lynx::miditools::span (input(), shorter, grid)[0].length == 1);
}

TEST (mt190o_strum_as_live)
{
    using namespace mt190o;
    using lynx::miditools::StrumOptions;
    const std::vector<Live> rest { { 1, 62, 0.5, 80 }, { 2, 65, 0.5, 90 }, { 2.5, 69, 0.25, 70 }, { 3, 72, 1, 110 } };
    const auto with = [&rest] (std::vector<Live> chord) { chord.insert (chord.end(), rest.begin(), rest.end()); return chord; };
    // Low 100 %: the lowest note a whole grid step late, the notes keep their length (clip 07)
    CHECK (same (lynx::miditools::strum (input(), { 1.0f, 0.0f, 0.0f }, grid), with ({ { 0, 67, 1, 100 }, { 0.125, 64, 1, 100 }, { 0.249999, 60, 1, 100 } })));
    // High 100 %: the highest note late (clip 02)
    CHECK (same (lynx::miditools::strum (input(), { 0.0f, 1.0f, 0.0f }, grid), with ({ { 0, 60, 1, 100 }, { 0.125, 64, 1, 100 }, { 0.249999, 67, 1, 100 } })));
    // Low 100 % with Tension 25 / 50 / 100 % (clips 08, 14, 15)
    CHECK (same (lynx::miditools::strum (input(), { 1.0f, 0.0f, 0.25f }, grid), with ({ { 0, 67, 1, 100 }, { 0.170417, 64, 1, 100 }, { 0.249999, 60, 1, 100 } })));
    CHECK (same (lynx::miditools::strum (input(), { 1.0f, 0.0f, 0.5f }, grid), with ({ { 0, 67, 1, 100 }, { 0.207359, 64, 1, 100 }, { 0.249999, 60, 1, 100 } })));
    CHECK (same (lynx::miditools::strum (input(), { 1.0f, 0.0f, 1.0f }, grid), with ({ { 0, 67, 1, 100 }, { 0.247805, 64, 1, 100 }, { 0.249999, 60, 1, 100 } })));

    // a five-note chord, Low 100 %, Tension 50 % (clip 16)
    const lynx::miditools::Notes five { note (0, 60, 4, 64), note (0, 63, 4, 64), note (0, 64, 1, 100), note (0, 65, 4, 64), note (0, 67, 1, 100) };
    CHECK (same (lynx::miditools::strum (five, { 1.0f, 0.0f, 0.5f }, grid),
                 { { 0, 67, 1, 100 }, { 0.154383, 65, 4, 64 }, { 0.207359, 64, 1, 100 }, { 0.234163, 63, 4, 64 }, { 0.249999, 60, 4, 64 } }));
    // seven notes, Low 100 %, Tension -50 %; and every chord of the selection is strummed (clip 04)
    lynx::miditools::Notes seven { note (0, 36, 0.25, 100), note (0, 37, 0.25, 100), note (0, 38, 0.25, 100), note (0, 39, 0.25, 100),
                                   note (0, 60, 1, 100), note (0, 64, 1, 100), note (0, 67, 1, 100) };
    for (int key = 36; key <= 39; ++key)
        seven.push_back (note (0.5, key, 0.25, 100));
    CHECK (same (lynx::miditools::strum (seven, { 1.0f, 0.0f, -0.5f }, grid),
                 { { 0, 67, 1, 100 }, { 0.0097028, 64, 1, 100 }, { 0.0231274, 60, 1, 100 }, { 0.042641, 39, 0.25, 100 }, { 0.0730954, 38, 0.25, 100 },
                   { 0.126719, 37, 0.25, 100 }, { 0.249999, 36, 0.25, 100 }, { 0.5, 39, 0.25, 100 }, { 0.523127, 38, 0.25, 100 },
                   { 0.573095, 37, 0.25, 100 }, { 0.749999, 36, 0.25, 100 } }));

    // the curve itself against Live's offsets (share of the 0.249999-beat step)
    const auto share = [] (double beats) { return beats / 0.249999; };
    CHECK (std::abs (lynx::miditools::strumCurve (0.25, 0.5) - share (0.154383)) < 5e-6);
    CHECK (std::abs (lynx::miditools::strumCurve (0.5, 0.5) - share (0.207359)) < 5e-6);
    CHECK (std::abs (lynx::miditools::strumCurve (0.75, 0.5) - share (0.234163)) < 5e-6);
    CHECK (std::abs (lynx::miditools::strumCurve (0.5, 0.25) - share (0.170417)) < 5e-6);
    CHECK (std::abs (lynx::miditools::strumCurve (0.5, 1.0) - share (0.247805)) < 5e-6);
    CHECK (std::abs (lynx::miditools::strumCurve (1.0 / 6.0, -0.5) - share (0.0097028)) < 5e-6);
    CHECK (std::abs (lynx::miditools::strumCurve (5.0 / 6.0, -0.5) - share (0.126719)) < 5e-6);
    CHECK (lynx::miditools::strumCurve (0.3, 0.0) == 0.3);
    // nothing to strum: single notes stay
    CHECK (same (lynx::miditools::strum ({ note (1, 62, 0.5, 80) }, { 1.0f, 1.0f, 0.5f }, grid), { { 1, 62, 0.5, 80 } }));
}

TEST (mt190o_ornament_flam_and_grace_notes_as_live)
{
    using namespace mt190o;
    lynx::miditools::OrnamentOptions flam; // Live's defaults: Flam -20 %, 50 %
    // clip 03: a flam 0.05 beats before each note at half its velocity; Live also writes flams at -0.05 for the chord
    // at 0, which a LYNX pattern can't hold
    const auto before = lynx::miditools::ornament (input(), flam, grid);
    CHECK (same (before, { { 0, 60, 1, 100 }, { 0, 64, 1, 100 }, { 0, 67, 1, 100 }, { 0.95, 62, 0.05, 40 }, { 1, 62, 0.5, 80 }, { 1.95, 65, 0.05, 45 },
                           { 2, 65, 0.5, 90 }, { 2.45, 69, 0.05, 35 }, { 2.5, 69, 0.25, 70 }, { 2.95, 72, 0.05, 55 }, { 3, 72, 1, 110 } }));
    CHECK (std::abs (before[3].release * 127.0f - 32.0f) < 0.01f); // Live: OffVelocity 32

    // clip 06 (after Legato): Flam +20 % takes the note's start, the note keeps its end
    flam.flamPosition = 0.2f;
    const auto legato = lynx::miditools::span (input(), { lynx::miditools::SpanOptions::legato }, grid);
    CHECK (same (lynx::miditools::ornament (legato, flam, grid),
                 { { 0, 60, 0.05, 50 }, { 0, 64, 0.05, 50 }, { 0, 67, 0.05, 50 }, { 0.05, 60, 0.95, 100 }, { 0.05, 64, 0.95, 100 }, { 0.05, 67, 0.95, 100 },
                   { 1, 62, 0.05, 40 }, { 1.05, 62, 0.95, 80 }, { 2, 65, 0.05, 45 }, { 2.05, 65, 0.45, 90 }, { 2.5, 69, 0.05, 35 }, { 2.55, 69, 0.45, 70 },
                   { 3, 72, 0.05, 55 }, { 3.05, 72, 0.95, 110 } }));

    // clip 09: Grace notes, High, 3, -20 %, 50 %: three equal notes in the 0.05 beats before the note: above, same, above
    lynx::miditools::OrnamentOptions grace;
    grace.flam = false;
    grace.grace = true;
    grace.gracePitch = lynx::miditools::OrnamentOptions::high;
    const lynx::miditools::Notes two { note (1, 62, 0.5, 25), note (3, 72, 1, 127) };
    const auto third = 0.05 / 3.0;
    CHECK (same (lynx::miditools::ornament (two, grace, grid),
                 { { 0.95, 63, third, 12.5 }, { 0.95 + third, 62, third, 12.5 }, { 0.95 + 2 * third, 63, third, 12.5 }, { 1, 62, 0.5, 25 },
                   { 2.95, 73, third, 63.5 }, { 2.95 + third, 72, third, 63.5 }, { 2.95 + 2 * third, 73, third, 63.5 }, { 3, 72, 1, 127 } }));
    // [MAN] with a scale the step is a scale degree: C major, a grace note above E is F, above F is G
    grace.scale = 1;
    for (int s = 1; s < (int) lynx::notetools::scales().size(); ++s)
        if (lynx::notetools::scales()[(std::size_t) s].intervals == std::vector<int> { 0, 2, 4, 5, 7, 9, 11 })
            grace.scale = s;
    CHECK (lynx::miditools::ornament ({ note (1, 65, 0.5, 80) }, grace, grid)[0].key == 67);
    // a velocity never falls under 1
    CHECK (same (lynx::miditools::ornament ({ note (2, 65, 0.5, 1) }, lynx::miditools::OrnamentOptions {}, grid), { { 1.95, 65, 0.05, 1 }, { 2, 65, 0.5, 1 } }));
}

TEST (mt190o_recombine_as_live)
{
    using namespace mt190o;
    using lynx::miditools::RecombineOptions;
    RecombineOptions o;
    o.rotate = 1; // clip 05: Position, one step: every chord takes the next one's place, the last comes first
    CHECK (same (lynx::miditools::recombine (input(), o, grid),
                 { { 0, 72, 1, 110 }, { 1, 60, 1, 100 }, { 1, 64, 1, 100 }, { 1, 67, 1, 100 }, { 2, 62, 0.5, 80 }, { 2.5, 65, 0.5, 90 }, { 3, 69, 0.25, 70 } }));
    o.rotate = 5; // five places = once round
    CHECK (same (lynx::miditools::recombine (input(), o, grid),
                 { { 0, 60, 1, 100 }, { 0, 64, 1, 100 }, { 0, 67, 1, 100 }, { 1, 62, 0.5, 80 }, { 2, 65, 0.5, 90 }, { 2.5, 69, 0.25, 70 }, { 3, 72, 1, 110 } }));
    o = {};
    o.mirror = true;
    o.dimension = RecombineOptions::velocity; // [MAN] the values in reverse order
    const auto mirrored = lynx::miditools::recombine (input(), o, grid);
    CHECK (std::abs (mirrored[0].velocity * 127.0f - 110.0f) < 0.01f && std::abs (mirrored[6].velocity * 127.0f - 100.0f) < 0.01f);
    o.dimension = RecombineOptions::pitch;
    const auto pitches = lynx::miditools::recombine (input(), o, grid); // in time order 60 64 67 62 65 69 72 → 72 69 65 62 67 64 60
    CHECK (pitches[0].key == 65 && pitches[1].key == 69 && pitches[2].key == 72 && pitches[3].key == 62 && pitches[6].key == 60);
    o = {};
    o.shuffle = true; // a permutation: the same start times, the same notes, and the same result for the same seed
    const auto a = lynx::miditools::recombine (input(), o, grid), b = lynx::miditools::recombine (input(), o, grid);
    CHECK (a == b && a.size() == 7);
    std::vector<std::int64_t> starts;
    for (const auto& n : a)
        starts.push_back (n.start);
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
    CHECK ((starts == std::vector<std::int64_t> { 0, 960, 1920, 2400, 2880 })); // the five places, each taken once
}

TEST (mt190o_arpeggiate_as_live)
{
    using namespace mt190o;
    // clip 01: Live's defaults (Up, +12 st, 2 steps, 1/32, 100 %)
    CHECK (same (lynx::miditools::arpeggiate (input(), {}),
                 { { 0, 60, 0.125, 100 }, { 0.125, 64, 0.125, 100 }, { 0.25, 67, 0.125, 100 }, { 0.375, 72, 0.125, 100 }, { 0.5, 76, 0.125, 100 },
                   { 0.625, 79, 0.125, 100 }, { 0.75, 84, 0.125, 100 }, { 0.875, 88, 0.125, 100 }, { 1, 62, 0.125, 80 }, { 1.125, 74, 0.125, 80 },
                   { 1.25, 86, 0.125, 80 }, { 1.375, 62, 0.125, 80 }, { 2, 65, 0.125, 90 }, { 2.125, 77, 0.125, 90 }, { 2.25, 89, 0.125, 90 },
                   { 2.375, 65, 0.125, 90 }, { 2.5, 69, 0.125, 70 }, { 2.625, 81, 0.125, 70 }, { 3, 72, 0.125, 110 }, { 3.125, 84, 0.125, 110 },
                   { 3.25, 96, 0.125, 110 }, { 3.375, 72, 0.125, 110 }, { 3.5, 84, 0.125, 110 }, { 3.625, 96, 0.125, 110 }, { 3.75, 72, 0.125, 110 },
                   { 3.875, 84, 0.125, 110 } }));
    // the other styles on the chord alone, no transposed passes, 1/16
    using A = lynx::miditools::ArpeggiateOptions;
    const lynx::miditools::Notes chord { note (0, 60, 2, 100), note (0, 64, 2, 100), note (0, 67, 2, 100), note (0, 72, 2, 100) };
    const auto keys = [&chord] (int style) {
        A o;
        o.style = style;
        o.steps = 0;
        o.rate = lynx::ppq / 4;
        std::vector<int> out;
        for (const auto& n : lynx::miditools::arpeggiate (chord, o))
            out.push_back (n.key);
        return out;
    };
    CHECK ((keys (A::down) == std::vector<int> { 72, 67, 64, 60, 72, 67, 64, 60 }));
    CHECK ((keys (A::upDown) == std::vector<int> { 60, 64, 67, 72, 67, 64, 60, 64 }));
    CHECK ((keys (A::upAndDown) == std::vector<int> { 60, 64, 67, 72, 72, 67, 64, 60 }));
    CHECK ((keys (A::converge) == std::vector<int> { 60, 72, 64, 67, 60, 72, 64, 67 }));
    // round 190aa: Live plays a triad's Diverge 64 60 67 (middle, then the lower of the pair first); four notes follow
    // the same rule [INFERRED]
    CHECK ((keys (A::diverge) == std::vector<int> { 64, 67, 60, 72, 64, 67, 60, 72 }));
    CHECK ((keys (A::pinkyUp) == std::vector<int> { 60, 72, 64, 72, 67, 72, 60, 72 }));
    CHECK ((keys (A::thumbUp) == std::vector<int> { 60, 64, 60, 67, 60, 72, 60, 64 }));
    CHECK (keys (A::chordTrigger).size() == 32);
    CHECK (keys (A::randomOnce).size() == 8 && keys (A::randomOnce) == keys (A::randomOnce));
    for (int style = 0; style < A::styleCount; ++style)
        CHECK (! keys (style).empty() && std::string (lynx::miditools::arpeggiateStyleName (style)) != "");
    // a gate over 100 % makes the notes longer than the rate; nothing above key 127
    A longer;
    longer.gate = 1.5f;
    longer.distance = 60;
    const auto out = lynx::miditools::arpeggiate ({ note (0, 72, 1, 100) }, longer);
    CHECK (out.size() == 3 && out[0].length == lynx::ppq * 3 / 16); // 72, (132 and 192 left out), 72, …, 72, …
}

TEST (mt190o_connect_and_chop_as_live)
{
    using namespace mt190o;
    // clip 02: Spread 0, Density 100 %, Rate Grid, Tie 0 %: the two silences get notes stepping from one note to the next
    const auto connected = lynx::miditools::connect (input(), {}, grid);
    CHECK (same (connected, { { 0, 60, 1, 100 }, { 0, 64, 1, 100 }, { 0, 67, 1, 100 }, { 1, 62, 0.5, 80 }, { 1.5, 63, 0.25, 83.3333 },
                              { 1.75, 64, 0.25, 86.6667 }, { 2, 65, 0.5, 90 }, { 2.5, 69, 0.25, 70 }, { 2.75, 71, 0.25, 90 }, { 3, 72, 1, 110 } }));
    lynx::miditools::ConnectOptions tied; // [MAN] a tied note lasts until the next note
    tied.tie = 1.0f;
    const auto t = lynx::miditools::connect (input(), tied, grid);
    CHECK (t.size() == 9 && t[4].start == lynx::ppq * 3 / 2 && t[4].length == lynx::ppq / 2);
    lynx::miditools::ConnectOptions none;
    none.density = 0.0f;
    CHECK (lynx::miditools::connect (input(), none, grid).size() == 7);

    // clip 10: Chop, 4 parts
    std::vector<Live> chopped;
    for (const auto& n : input())
        for (int i = 0; i < 4; ++i)
            chopped.push_back ({ ((double) n.start + (double) n.length * i / 4) / lynx::ppq, n.key, (double) n.length / 4 / lynx::ppq, n.velocity * 127.0 });
    CHECK (same (lynx::miditools::chop (input(), {}), chopped));
    CHECK (lynx::miditools::chop ({ note (0, 60, 1, 100) }, { 4, 1 }).size() == 2);  // [MAN] a gap after every part
    CHECK (lynx::miditools::chop ({ note (0, 60, 1, 100) }, { 8, -3 }).size() == 2); // three gaps after each part
}

TEST (mt190o_replace_notes_in_a_pattern)
{
    using namespace mt190o;
    lynx::Pattern p;
    p.notes = input();
    p.notes.push_back (note (3.5, 40, 0.25, 90)); // not selected
    p.sortNotes();
    std::vector<int> selection;
    for (int i = 0; i < (int) p.notes.size(); ++i)
        if (p.notes[(std::size_t) i].key != 40)
            selection.push_back (i);
    const auto picked = lynx::miditools::notesAt (p, selection);
    CHECK (picked.size() == 7);
    const auto now = lynx::miditools::replaceNotes (p, selection, lynx::miditools::chop (picked, {}));
    CHECK (p.notes.size() == 29 && now.size() == 28);
    CHECK (std::none_of (now.begin(), now.end(), [&p] (int i) { return p.notes[(std::size_t) i].key == 40; }));
    CHECK (std::is_sorted (p.notes.begin(), p.notes.end(), [] (const lynx::Note& a, const lynx::Note& b) { return a.start < b.start; }));
}

TEST (mt190o_live_scales_and_the_project_key_in_als)
{
    // [LIVE 12.4 save] C Bhairav = Root 0, Name 22; the chooser's order around it
    const auto& list = lynx::livescales::scales();
    CHECK (list.size() == 35);
    CHECK (std::string (list[20].name) == "Super Locrian" && std::string (list[21].name) == "8-Tone Spanish");
    CHECK (std::string (list[22].name) == "Bhairav" && std::string (list[23].name) == "Hungarian Minor" && std::string (list[34].name) == "Messiaen 7");
    CHECK (lynx::livescales::indexOfName ("Minor") == 1 && lynx::livescales::indexOfName ("nope") == -1);
    for (const auto& s : list) // every scale starts on its root, rises, stays inside the octave
        CHECK (s.intervals.front() == 0 && std::is_sorted (s.intervals.begin(), s.intervals.end()) && s.intervals.back() < 12);

    int both = 0;
    for (int i = 0; i < (int) list.size(); ++i)
        if (const auto key = lynx::livescales::keyFor (9, i))
        {
            ++both;
            CHECK (key->root == 9 && lynx::projectkey::intervals (*key) == list[(std::size_t) i].intervals);
            CHECK (lynx::livescales::indexFor (*key) == i);
        }
    CHECK (both >= 20); // FL's list has most of Live's scales
    CHECK (lynx::livescales::keyFor (0, 0).has_value() && lynx::livescales::keyFor (0, 1).has_value() && ! lynx::livescales::keyFor (0, 99));

    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::song;
    s.patterns[0].notes = { mt190o::note (0, 60, 1, 100) };
    s.patterns[0].notes[0].channel = 0;
    s.patterns[0].lengthTicks = 4 * lynx::ppq;
    s.playlist.clear();
    lynx::Clip c;
    c.pattern = 0;
    c.length = 4 * lynx::ppq;
    s.playlist.push_back (c);

    // no key: Live's defaults stay (Scale Mode off)
    auto out = lynx::exportAls (s, {});
    CHECK (out.xml.find ("<InKey Value=\"true\"") == std::string::npos && out.xml.find ("<IsInKey Value=\"true\"") == std::string::npos);
    auto back = lynx::importAls (out.bytes, {});
    CHECK (back && ! lynx::projectkey::get (*back));

    // D Bhairav: the set and its clips carry it as Live writes it, and it comes back
    const auto bhairav = lynx::livescales::keyFor (2, 22);
    CHECK (bhairav.has_value());
    if (bhairav)
    {
        lynx::projectkey::set (s, bhairav);
        out = lynx::exportAls (s, {});
        const auto has = [&out] (const char* text) { return out.xml.find (text) != std::string::npos; };
        CHECK (has ("<InKey Value=\"true\"") && has ("<IsInKey Value=\"true\""));
        std::string flat;
        for (auto ch : out.xml)
            if (ch != '\t' && ch != '\n' && ch != '\r')
                flat += ch;
        CHECK (flat.find ("<ScaleInformation><Root Value=\"2\" /><Name Value=\"22\" /></ScaleInformation><InKey Value=\"true\" />") != std::string::npos);
        back = lynx::importAls (out.bytes, {});
        CHECK (back && lynx::projectkey::get (*back) == bhairav);

        // Live 11: a clip scale by name
        lynx::AlsExportOptions eleven;
        eleven.version = lynx::AlsVersion::live11;
        out = lynx::exportAls (s, eleven);
        back = lynx::importAls (out.bytes, {});
        CHECK (back.has_value());
        if (out.xml.find ("<IsInKey Value=\"true\"") != std::string::npos)
        {
            CHECK (out.xml.find ("<Name Value=\"Bhairav\"") != std::string::npos);
            CHECK (back && lynx::projectkey::get (*back) == bhairav);
        }
    }
}
