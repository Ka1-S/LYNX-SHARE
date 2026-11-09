// Round 188g: the rest of GitHub issue #63 (docs/analise/REA_rodada188g.md): a note's Mod X / Y on LYNX's own Sampler,
// its fine pitch on hosted plugins (FL's wrapper: the key it rounds to, the VST3 tuning), release velocity on the
// Sampler (FL's leaves it alone) and the Fruity Slicer / Slicex slices of a sample's second half one frame early.

#include "core/HostNotePitch.h"

namespace notes188g
{
std::shared_ptr<lynx::SampleData> noise (int frames, std::uint32_t seed = 11)
{
    auto s = std::make_shared<lynx::SampleData>();
    s->sampleRate = 44100.0;
    for (int i = 0; i < frames; ++i)
    {
        seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
        s->left.push_back ((float) (((double) seed / 2147483647.0 * 2.0 - 1.0) * 0.3));
    }
    return s;
}

// FL's 12 dB filter law (X → Hz at 44.1 kHz, as FlpImport's channelFilterHz for types 1…5) and its inverse
double hzOfX (double x) { return x >= 1.0 ? 20000.0 : std::clamp (((std::pow (51.0, x) - 1.0) * 0.0596 + 0.02) * 44100.0 / 6.283185307179586, 20.0, 20000.0); }
double xOfHz (double hz) { return std::log ((hz * 6.283185307179586 / 44100.0 - 0.02) / 0.0596 + 1.0) / std::log (51.0); }

// the energy of the first differences (a crude "brightness")
double brightness (const std::vector<float>& v)
{
    double e = 0.0;
    for (std::size_t i = 1; i < v.size(); ++i)
        e += (double) (v[i] - v[i - 1]) * (v[i] - v[i - 1]);
    return e;
}
} // namespace notes188g

// FL's wrapper (Fruity Wrapper 0x879a00 → the format's note on): pitch = key + fine / 100; VST2 / MIDI and VST3 round
// it to a key (ties to even), VST3 sends the rest as tuning, CLAP truncates and sends nothing else.
TEST (notes188g_hosted_note_pitch_follows_fl_wrapper)
{
    using namespace lynx::hostpitch;
    struct Case
    {
        int key;
        float cents;
        int vst3Key;
        float tuning;
        int clapKey;
    };
    const Case cases[] = {
        { 60, 0.0f, 60, 0.0f, 60 },       { 60, 25.0f, 60, 25.0f, 60 },    { 60, -40.0f, 60, -40.0f, 59 },
        { 60, 70.0f, 61, -30.0f, 60 },    { 64, 50.0f, 64, 50.0f, 64 },    { 65, 50.0f, 66, -50.0f, 65 }, // (ties to even)
        { 64, 150.0f, 66, -50.0f, 65 },   { 67, -1200.0f, 55, 0.0f, 55 }, { 72, 1350.0f, 86, -50.0f, 85 },
        { 127, 90.0f, 127, 0.0f, 127 },   { 0, -80.0f, 0, 0.0f, 0 },
    };
    for (const auto& c : cases)
    {
        const auto v = of (c.key, c.cents, Format::vst3);
        const auto m = of (c.key, c.cents, Format::midi);
        const auto k = of (c.key, c.cents, Format::clap);
        const auto clampedEnds = c.key == 127 || c.key == 0; // (the key is clamped, the tuning keeps the rest)
        CHECK (v.key == c.vst3Key && m.key == c.vst3Key && k.key == c.clapKey);
        if (! clampedEnds)
            CHECK (std::abs (v.tuning - c.tuning) < 1.0e-3f);
        CHECK (m.tuning == 0.0f && k.tuning == 0.0f);
        if (! clampedEnds)
            CHECK (std::abs ((float) (v.key - c.key) * 100.0f + v.tuning - c.cents) < 1.0e-3f); // nothing lost
    }
    CHECK (of (60, std::numeric_limits<float>::quiet_NaN()).key == 60);
}

// The note off goes to the key its note on went out on (FL keeps it in the voice), oldest first; the VST3 tunings wait
// for their note ons in the order they were played.
TEST (notes188g_sent_keys_end_notes_on_their_own_key)
{
    using namespace lynx::hostpitch;
    SentKeys keys;
    keys.on (60, of (60, 70.0f));  // → 61, −30
    keys.on (60, of (60, -20.0f)); // → 60, −20
    keys.on (61, of (61, 0.0f));   // → 61, 0
    CHECK (keys.takeTuning (61) == of (60, 70.0f).tuning);
    CHECK (keys.takeTuning (60) == of (60, -20.0f).tuning);
    CHECK (keys.takeTuning (61) == 0.0f);
    CHECK (keys.takeTuning (61) == 0.0f); // (none waits: 0)
    CHECK (keys.off (60) == 61);
    CHECK (keys.off (60) == 60);
    CHECK (keys.off (60) == 60); // (no note sounding: its own key)
    CHECK (keys.off (61) == 61);
    for (int i = 0; i < 6; ++i) // more notes than a key keeps: the oldest are forgotten, nothing overflows
        keys.on (62, of (62, 60.0f));
    CHECK (keys.off (62) == 63);
}

// #63: a note's Mod X / Y on LYNX's own Sampler move its filter as FL's voice moves the channel filter (REA_rodada76 §1:
// X + (b − 128) / 128): a note at Mod X 0.25 on a channel at cutoff F sounds like a centred note on the channel at the
// cutoff half of FL's X range lower. A centred note is unchanged, bit for bit; Mod Y moves the resonance; an open
// channel closes for a note with Mod X below the centre.
TEST (notes188g_note_mod_x_moves_lynx_sampler_filter)
{
    using namespace lynx;
    const auto source = notes188g::noise (22050);
    auto render = [&] (float cutoffHz, float resonance, std::optional<std::pair<float, float>> mod) {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.sampler.cutoff = cutoffHz;
        c.sampler.resonance = resonance;
        auto inst = makeInstrument (InstrumentKind::sampler, source);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        if (mod)
            inst->setNextNoteModulation (mod->first, mod->second);
        inst->noteOn (60, 0.8f);
        std::vector<float> l (8000), r (8000);
        inst->render (l.data(), r.data(), 8000, 1.0f, 1.0f);
        return l;
    };
    const auto centred = render (3000.0f, 0.2f, std::nullopt);
    CHECK (render (3000.0f, 0.2f, std::pair { 0.5f, 0.5f }) == centred); // centre: nothing changes
    const auto darker = render (3000.0f, 0.2f, std::pair { 0.25f, 0.5f });
    const auto brighter = render (3000.0f, 0.2f, std::pair { 0.75f, 0.5f });
    std::printf ("  brightness: Mod X 0.25 %.4g, centred %.4g, Mod X 0.75 %.4g\n", notes188g::brightness (darker), notes188g::brightness (centred),
                 notes188g::brightness (brighter));
    CHECK (notes188g::brightness (darker) < 0.5 * notes188g::brightness (centred));
    CHECK (notes188g::brightness (brighter) > 1.5 * notes188g::brightness (centred));

    // the same as the channel's cutoff moved by half of FL's X range (Mod X 0.25 = −0.5)
    const auto moved = (float) notes188g::hzOfX (notes188g::xOfHz (3000.0) - 0.5);
    const auto same = render (moved, 0.2f, std::nullopt);
    auto diff = 0.0;
    for (std::size_t i = 0; i < same.size(); ++i)
        diff = std::max (diff, (double) std::abs (same[i] - darker[i]));
    std::printf ("  cutoff 3000 Hz + Mod X 0.25 vs cutoff %.1f Hz: max diff %.3g\n", moved, diff);
    CHECK (diff < 1.0e-4);

    CHECK (render (3000.0f, 0.2f, std::pair { 0.5f, 0.9f }) != centred); // Mod Y: the resonance
    // an open channel: untouched by a centred note, filtered by a note below the centre
    const auto open = render (20000.0f, 0.0f, std::nullopt);
    CHECK (open == render (20000.0f, 0.0f, std::pair { 0.5f, 0.5f }));
    CHECK (notes188g::brightness (render (20000.0f, 0.0f, std::pair { 0.2f, 0.5f })) < 0.5 * notes188g::brightness (open));
}

// A VFX (VFX Envelope / Color Mapper) moving a sounding note's Mod X reaches the same filter.
TEST (notes188g_vfx_mod_x_moves_a_sounding_sampler_note)
{
    using namespace lynx;
    const auto source = notes188g::noise (22050, 5);
    auto render = [&] (bool move) {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.sampler.cutoff = 5000.0f;
        auto inst = makeInstrument (InstrumentKind::sampler, source);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        inst->noteOn (60, 0.8f);
        std::vector<float> l (4000), r (4000);
        inst->render (l.data(), r.data(), 2000, 1.0f, 1.0f);
        if (move)
            inst->voiceProperty (60, (int) vfx::Prop::modX, 0.1f);
        inst->render (l.data() + 2000, r.data() + 2000, 2000, 1.0f, 1.0f);
        return std::vector<float> (l.begin() + 2000, l.end());
    };
    const auto still = render (false), moved = render (true);
    CHECK (notes188g::brightness (moved) < 0.5 * notes188g::brightness (still));
}

// FL's Sampler ignores a note's release velocity: its note off (FLEngine FUN_00d241e0) releases the voice's articulators
// with the channel's envelope records only (REA_rodada188g §3); LYNX's own Sampler does the same.
TEST (notes188g_release_velocity_leaves_the_sampler_alone)
{
    using namespace lynx;
    const auto source = notes188g::noise (44100, 3);
    auto render = [&] (std::optional<float> release) {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.sampler.release = 0.2f;
        auto inst = makeInstrument (InstrumentKind::sampler, source);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        inst->noteOn (60, 0.8f);
        std::vector<float> l (12000), r (12000);
        inst->render (l.data(), r.data(), 2000, 1.0f, 1.0f);
        if (release)
            inst->setNextNoteRelease (*release);
        inst->noteOff (60);
        inst->render (l.data() + 2000, r.data() + 2000, 10000, 1.0f, 1.0f);
        return l;
    };
    const auto plain = render (std::nullopt);
    CHECK (render (0.0f) == plain);
    CHECK (render (1.0f) == plain);
}

// Round 180c's "notes 1 frame late from 2.25 s" (/mnt/e/lynx-work/issues180c/note-timing.md): the Fruity Slicer project
// played slice k at 2.25 s + … one frame late against FL because the sampler scaled the slice starts (p / frames) by the
// last frame's index (frames − 1): every slice in the sample's second half started one frame early in the sample. FL's
// render starts each slice on its own frame.
TEST (notes188g_slices_in_the_second_half_start_on_their_frame)
{
    using namespace lynx;
    constexpr int frames = 176400; // round 180c's loop.wav: 4 s at 44.1 kHz, 16 slices of 11025 frames
    auto ramp = std::make_shared<SampleData>();
    ramp->sampleRate = 44100.0;
    for (int i = 0; i < frames; ++i)
        ramp->left.push_back ((float) i);
    ChannelDef c;
    for (int k = 0; k < 16; ++k)
        c.sampler.slices.push_back ((float) ((double) (k * 11025) / (double) frames));
    auto wrong = 0;
    for (int k = 0; k < 16; ++k)
    {
        auto inst = makeInstrument (InstrumentKind::sampler, ramp);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        inst->noteOn (60 + k, 1.0f);
        std::vector<float> l (16), r (16);
        inst->render (l.data(), r.data(), 16, 1.0f, 1.0f);
        if (std::abs ((double) l[0] - (double) (k * 11025)) > 0.01)
        {
            ++wrong;
            std::printf ("  slice %d starts at frame %.2f (exact %d)\n", k, (double) l[0], k * 11025);
        }
    }
    CHECK (wrong == 0);
}
