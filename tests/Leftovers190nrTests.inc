// Round 190nr (leftovers of the 0.1.30 release): going back to an older version left the newer version's install-folder
// entries (0.1.29's installer kept 0.1.30's licenses\, round 190nk's updater test). The --apply-update helper of the
// newer version removes what arrived after the version installed (core/UpdateCheck.h entriesNewerThan).
#include "core/UpdateCheck.h"

TEST (leftovers190nr_entries_newer_than_the_version_installed)
{
    using namespace lynx::update;
    auto names = [] (const char* v) {
        std::string out;
        for (const auto& n : entriesNewerThan (parseVersion (v)))
            out += (out.empty() ? "" : ",") + n;
        return out;
    };
    CHECK (names ("0.1.29-alpha") == "licenses,installed-files.txt");
    CHECK (names ("0.1.28.1-alpha") == "licenses,installed-files.txt");
    CHECK (names ("0.1.30-alpha") == "installed-files.txt"); // 0.1.30 ships licenses\ itself
    CHECK (names ("0.1.30") == "installed-files.txt");       // CMake's bare version equals its release tag
    CHECK (names ("0.1.31-alpha").empty() && names ("0.1.31").empty() && names ("0.1.32-alpha").empty());
    CHECK (names ("").empty() && names ("garbage").empty()); // unreadable note: nothing is removed
    // every entry names one top-level item and a valid version (the helper deletes folder.getChildFile (name))
    for (const auto& e : installEntries())
    {
        const std::string n = e.name;
        CHECK (! n.empty() && n != "." && n != ".." && n.find_first_of ("/\\:") == std::string::npos);
        CHECK (parseVersion (e.since).valid);
        CHECK (n != "LYNX.exe" && n != "Uninstall.exe"); // never the program itself
    }
}

// Disc #202 (round 190nr): Transistor Bass "Copy as MIDI" / "Export MIDI..." — the shown program's sequence as notes
#include "core/TransistorBass.h"
namespace nr190
{
// design168q's synthetic state (program 3 shows sequence 3: note 48 + i, gate on the even steps, slide on step 2, accent
// on step 4, gate length 0.66) with the sequence's own BPM switch and shuffle set
std::vector<std::uint8_t> tbState (double bpm, bool ownBpm, double shuffle)
{
    auto s = design168q::tbassState (1);
    // the tail of every sequence: f64 bpm, i8 own-BPM switch, f64 shuffle, f64 gate length (= 0.66 in the builder)
    const double gateLength = 0.66;
    std::size_t at = 0;
    while (true)
    {
        auto it = std::search (s.begin() + (std::ptrdiff_t) at, s.end(), (const std::uint8_t*) &gateLength, (const std::uint8_t*) &gateLength + 8);
        if (it == s.end())
            break;
        const auto end = (std::size_t) (it - s.begin());
        std::memcpy (s.data() + end - 8, &shuffle, 8);
        s[end - 9] = ownBpm ? 1 : 0;
        std::memcpy (s.data() + end - 17, &bpm, 8);
        at = end + 8;
    }
    return s;
}
} // namespace nr190

TEST (leftovers190nr_transistor_bass_sequence_as_notes)
{
    using namespace lynx;
    std::int64_t len = 0;
    auto notes = transistorBassSequenceNotes (design168q::tbassState (1), 120.0, &len);
    CHECK (len == 16 * ticksPerStep);
    CHECK (notes.size() == 8); // the gated (even) steps
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto i = (int) k * 2;
        CHECK (notes[k].start == i * ticksPerStep && notes[k].key == 48 + i && notes[k].channel == 0);
        // step 2 slides into step 4 (lasts to it and overlaps it by an eighth of a step); the rest: 0.66 of a step
        CHECK (notes[k].length == (i == 2 ? 2 * ticksPerStep + ticksPerStep / 8 : std::llround (0.66 * ticksPerStep)));
        CHECK (notes[k].velocity == (i == 4 ? 1.0f : 0.78f)); // accent: >= 110 of 127 on Transistor Bass
    }
    CHECK (notes[1].start + notes[1].length > notes[2].start); // the slide overlaps: it glides

    // the program's transpose moves every key; the sequence length cuts the steps
    ChannelDef c;
    c.instrument = InstrumentKind::transistorbass;
    c.pluginState = design168q::tbassState (1);
    CHECK (modelparams::setRawValue (c, tbprog::transpose, 5.0f));
    CHECK (modelparams::setRawValue (c, tbseq::length, 7.0f));
    notes = transistorBassSequenceNotes (c.pluginState, 120.0, &len);
    CHECK (len == 7 * ticksPerStep && notes.size() == 4 && notes[3].key == 48 + 6 + 5);
    // a slide on the last gated step lasts to the end of the sequence
    CHECK (modelparams::setRawValue (c, tbseq::firstStep + 5 * 6 + 3, 1.0f));
    notes = transistorBassSequenceNotes (c.pluginState, 120.0, &len);
    CHECK (notes.back().start + notes.back().length == 7 * ticksPerStep);
    // no gate on: nothing
    for (int i = 0; i < 16; ++i)
        modelparams::setRawValue (c, tbseq::firstStep + 5 * i + 2, 0.0f);
    CHECK (transistorBassSequenceNotes (c.pluginState, 120.0).empty());
    CHECK (transistorBassSequenceNotes ({}, 120.0).empty());

    // its own BPM: 240 BPM against a 120 BPM song plays twice as fast (half the ticks)
    notes = transistorBassSequenceNotes (nr190::tbState (240.0, true, 0.0), 120.0, &len);
    CHECK (len == 8 * ticksPerStep && notes[1].start == ticksPerStep);
    notes = transistorBassSequenceNotes (nr190::tbState (240.0, false, 0.0), 120.0, &len); // switch off: the song's tempo
    CHECK (len == 16 * ticksPerStep && notes[1].start == 2 * ticksPerStep);
    // shuffle (clockSequencer's law, sw = 1 - shuffle / 2): the odd steps move, the even ones stay
    {
        auto s = nr190::tbState (125.0, false, 0.5); // sw 0.75: the odd step starts at 1 + 0.25 / 1.25 = 1.2 steps
        ChannelDef d;
        d.instrument = InstrumentKind::transistorbass;
        d.pluginState = s;
        CHECK (modelparams::setRawValue (d, tbseq::firstStep + 5 * 1 + 2, 1.0f)); // gate step 1 too
        notes = transistorBassSequenceNotes (d.pluginState, 120.0);
        CHECK (notes.size() == 9 && notes[0].start == 0 && notes[1].start == std::llround (1.2 * ticksPerStep) && notes[2].start == 2 * ticksPerStep);
        s = nr190::tbState (125.0, false, -0.5); // sw 1.25: at 1 / 1.25 = 0.8 steps
        d.pluginState = s;
        CHECK (modelparams::setRawValue (d, tbseq::firstStep + 5 * 1 + 2, 1.0f));
        notes = transistorBassSequenceNotes (d.pluginState, 120.0);
        CHECK (notes.size() == 9 && notes[1].start == std::llround (0.8 * ticksPerStep));
    }
}

// Patience +69 MB private in 0.1.30 (round 190nr, heaptrack of 0.1.29 / 0.1.30 on the same set): part of it was Atrium's
// dense late tank (round 190ls, 32 lines, ~2 MB at 48 kHz) allocated in every Atrium although only a translated
// ValhallaRoom (atrium::reaLaw) runs it. The rack's instance now has it only then; renders stay bit-identical.
#include "core/LynxVerbs.h"
TEST (leftovers190nr_atrium_dense_tank_only_where_it_runs)
{
    using namespace lynx;
    namespace at = verbs::atrium;
    auto render = [] (std::unique_ptr<Effect> fx, const EffectSlot& slot) {
        std::vector<float> l (48000), r (48000);
        std::uint32_t seed = 12345u;
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            l[i] = i < 4800 ? (float) ((int) (seed >> 9) - (1 << 22)) / (float) (1 << 22) * 0.5f : 0.0f;
            r[i] = 0.7f * l[i];
        }
        fx->prepare (48000.0);
        for (int off = 0; off < (int) l.size(); off += 256)
            fx->process (l.data() + off, r.data() + off, std::min (256, (int) l.size() - off), slot.params, 120.0);
        l.insert (l.end(), r.begin(), r.end());
        return l;
    };
    auto light = makeEffectSlot (EffectKind::atrium); // a new Atrium: revision 1, reaLaw 0
    CHECK (! verbs::atriumDense (light.params));
    // the rack's instance (makeEffect (slot), no dense tank) renders exactly as the full one (make (kind))
    CHECK (render (makeEffect (light), light) == render (verbs::make (EffectKind::atrium), light));
    // a translated ValhallaRoom's slot: the dense tank, the same as the full instance (and not the light tank's sound)
    auto dense = light;
    dense.params[(std::size_t) at::reaLaw] = 1.0f;
    CHECK (verbs::atriumDense (dense.params));
    const auto denseOut = render (makeEffect (dense), dense);
    CHECK (denseOut == render (verbs::make (EffectKind::atrium), dense));
    CHECK (denseOut != render (verbs::makeAtrium (false), dense)); // (without the tank it would fall back to the light one)
}
