// Round 190pi (0.1.31): FL-parity follow-ups of round 190oz, each checked against FL 2026's own renders (REA_rodada190pi):
// Sytrus follows slide-note velocity and pan; an arpeggiator's Slide steps glide the pitch only; a slide note over a chord
// moves every held note by one interval; Ogun's link velocity; FL 11's plugin-parameter automation numbers; Sytrus' own
// parameters under automation (the "Save a copy for 0.1.30" lines: --selftest slides190pi).
#include "core/Arpeggiator.h"
#include "core/HostSlide.h"
#include "core/NativeEngines.h"
#include "core/RackRenderer.h"
#include "core/SytrusEngine.h"
#include <cmath>

namespace flparity190pi
{
constexpr double rate = 48000.0;
constexpr double tick = 250.0; // one FL tick (PPQ 96) at 120 BPM, 48 kHz

struct Stereo
{
    std::vector<float> l, r;
};

// `channel` playing `notes` (start / length in FL ticks) in one Playlist clip, the project's slide law on; 1.6 s at 48 kHz
Stereo render (const lynx::ChannelDef& channel, const std::vector<lynx::Note>& notes, int block = 256,
               std::shared_ptr<const lynx::SampleData> sample = nullptr)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.slideNoteProps = true;
    s.channels = { channel };
    Pattern p;
    for (auto n : notes)
    {
        n.start = n.start * ppq / 96;
        n.length = n.length * ppq / 96;
        p.notes.push_back (n);
    }
    s.patterns = { p };
    Clip clip;
    clip.pattern = 0;
    clip.length = 4 * ppq;
    s.playlist = { clip };
    auto state = RackState::build (s, nullptr, rate, [sample] (const std::string&) { return sample; });
    RackRenderer rr;
    rr.prepare (rate, block);
    const auto total = (int) (1.6 * rate);
    Stereo out { std::vector<float> ((std::size_t) total), std::vector<float> ((std::size_t) total) };
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int at = 0; at < total; at += block)
    {
        const auto len = std::min (block, total - at);
        rr.process (state.get(), true, (double) at * tps, len, out.l.data() + at, out.r.data() + at, nullptr);
    }
    return out;
}

lynx::Note note (int start, int length, int key, float velocity, bool slide = false, float pan = 0.0f)
{
    lynx::Note n;
    n.start = start, n.length = length, n.key = key, n.velocity = velocity, n.slide = slide, n.pan = pan;
    return n;
}

double rms (const std::vector<float>& x, double from, double to)
{
    double sum = 0.0;
    int count = 0;
    for (auto i = (std::size_t) from; i < (std::size_t) to && i < x.size(); ++i, ++count)
        sum += (double) x[i] * (double) x[i];
    return count > 0 ? std::sqrt (sum / count) : 0.0;
}

// the level of one frequency (Goertzel, normalised) in x[from, to)
double tone (const std::vector<float>& x, double hz, double from, double to)
{
    const auto w = 6.283185307179586 * hz / rate;
    double s1 = 0.0, s2 = 0.0;
    int n = 0;
    for (auto i = (std::size_t) from; i < (std::size_t) to && i < x.size(); ++i, ++n)
    {
        const auto s0 = (double) x[i] + 2.0 * std::cos (w) * s1 - s2;
        s2 = s1, s1 = s0;
    }
    return n > 0 ? std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - 2.0 * std::cos (w) * s1 * s2)) / n : 0.0;
}

double initVolume (double v) { return (std::pow (11.0, 1.28 * v) - 1.0) / 10.0; }
} // namespace flparity190pi

// FL 2026 (REA_rodada190pi §1): a slide note moves a Sytrus note's Init.Vol (velocity) and pan like every native
// generator's — Overcome's Flute starts its notes at velocity 0 and brings them up with slide notes
TEST (flparity190pi_sytrus_follows_slide_velocity_and_pan)
{
    using namespace flparity190pi;
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto channel = natives::channelFromPresetFile (
        InstrumentKind::sytrus, (std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "Sytrus" / "Default.fst").string(),
        natives142::flAccess());
    if (! channel)
    {
        std::printf ("  flparity190pi: no Sytrus preset here\n");
        return;
    }
    const auto full = render (*channel, { note (0, 384, 60, 0.78125f) });
    const auto up = render (*channel, { note (0, 384, 60, 0.0f), note (96, 96, 60, 0.78125f, true) });
    const auto ref = rms (full.l, 220 * tick, 380 * tick);
    CHECK (ref > 1.0e-3);
    CHECK (rms (up.l, 20 * tick, 90 * tick) < ref * 1.0e-3);             // velocity 0: silent until the slide
    CHECK (std::abs (rms (up.l, 220 * tick, 380 * tick) / ref - 1.0) < 0.05); // then the slide note's level
    // half way through the slide: linear in Init.Vol (FL's law), about half the level
    const auto mid = rms (up.l, 142 * tick, 146 * tick) / rms (full.l, 142 * tick, 146 * tick);
    CHECK (mid > 0.4 && mid < 0.6);
    // pan: a slide note at pan −1 takes the note to the left
    const auto left = render (*channel, { note (0, 384, 60, 0.78125f), note (96, 96, 60, 0.78125f, true, -1.0f) });
    CHECK (rms (left.r, 220 * tick, 380 * tick) < 0.05 * rms (left.l, 220 * tick, 380 * tick));
}

// FL 2026 (REA_rodada190pi §1.3): a Slide arpeggio is one voice that glides; its glides move the pitch only (they took the
// voice to velocity 0 since #236's slide of the note properties), and the voice has the first held note's velocity
TEST (flparity190pi_arpeggiator_slide_glides_the_pitch_only)
{
    using namespace lynx;
    ArpSettings s;
    s.direction = 1;
    s.stepBeats = 0.25f;
    s.gate = 1.0f;
    s.slide = true;
    const auto tps = ticksPerSample (120.0, 48000.0);
    for (const auto law : { false, true })
    {
        Arpeggiator arp;
        arp.flSlideLaw = law;
        std::vector<NoteEvent> in (2);
        in[0].type = NoteEvent::Type::on, in[0].key = 63, in[0].velocity = 0.2f; // (the first one held)
        in[1].type = NoteEvent::Type::on, in[1].key = 61, in[1].velocity = 0.9f;
        int out = 0;
        arp.process (0, in.data(), 2, 0.0, tps, 48000, s, arp.output.data(), out);
        int ons = 0, glides = 0;
        float firstVelocity = -1.0f;
        for (int k = 0; k < out; ++k)
        {
            const auto& e = arp.output[(std::size_t) k];
            if (e.type == NoteEvent::Type::on)
            {
                ++ons;
                if (firstVelocity < 0.0f)
                    firstVelocity = e.velocity;
                CHECK (e.key == 61); // (it starts on the lowest key)
            }
            if (e.type == NoteEvent::Type::slide)
            {
                ++glides;
                CHECK (e.arpGlide);
            }
        }
        CHECK (ons == 1 && glides >= 3);
        // FL's law: the first held note's velocity; before (projects saved before 0.1.31): the note it starts on
        CHECK (std::abs (firstVelocity - (law ? 0.2f : 0.9f)) < 1.0e-6f);
    }
}

// FL 2026 (REA_rodada190pi §2: Sampler, 3x Osc, DirectWave, SimSynth, Harmless, MiniSynth and Sytrus): a slide note over
// a chord moves every held note by the interval from the highest held key to the slide note's key
TEST (flparity190pi_slide_over_a_chord_moves_by_one_interval)
{
    using namespace flparity190pi;
    using namespace lynx;
    {
        // a voice of key 60 (the engine's target, 0 cents from it, reads back as key 60) in the moving keys takes the
        // interval; one of key 67 (−700 cents from the slide note's key) outside them keeps its pitch
        HostSlide a, b;
        HostSlide::Shift shift;
        shift.on = true, shift.cents = -700.0f, shift.slideKey = 60;
        shift.keys[0] = std::uint64_t { 1 } << 60;
        HostSlide::pendingShift() = shift;
        a.start (0.0f, 100.0f);
        b.start (-700.0f, 100.0f);
        HostSlide::pendingShift() = {};
        a.advance (200.0f);
        b.advance (200.0f);
        CHECK (std::abs (a.cents + 700.0f) < 1.0e-3f);
        CHECK (b.cents == 0.0f && ! b.moving());
    }
    auto data = std::make_shared<SampleData>();
    data->sampleRate = rate;
    data->left.resize ((std::size_t) (3.0 * rate));
    for (std::size_t i = 0; i < data->left.size(); ++i)
        data->left[i] = 0.25f * (float) std::sin (6.283185307179586 * 440.0 * (double) i / rate);
    data->right = data->left;
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.name = "Sine";
    c.samplePath = "sine";
    // C5 (60) and G5 (67) held, a slide note on C5: FL moves both by 60 − 67 = −7 semitones (F4 and C5)
    const auto chord = render (c, { note (0, 384, 60, 0.8f), note (0, 384, 67, 0.8f), note (96, 48, 60, 0.8f, true) }, 256, data);
    const auto f = [] (int key) { return 440.0 * std::exp2 ((key - 60) / 12.0); };
    const auto at = [&] (int key) { return tone (chord.l, f (key), 240 * tick, 380 * tick); };
    CHECK (at (53) > 0.02 && at (60) > 0.02);
    CHECK (at (67) < 0.1 * at (60));
    // one held key: the slide to its own key moves nothing, as before (and a slide to G5 takes it there)
    const auto single = render (c, { note (0, 384, 60, 0.8f), note (96, 48, 67, 0.8f, true) }, 256, data);
    CHECK (tone (single.l, f (67), 240 * tick, 380 * tick) > 10.0 * tone (single.l, f (60), 240 * tick, 380 * tick));
    // colours (FL 2026: a slide note moves the held notes of its own colour only): C5 colour 0 + G5 colour 1, a slide note
    // to D5 in colour 0 → D5 and G5; a slide note of a colour no held note has moves nothing
    const auto coloured = [] (lynx::Note n, int colour) { n.colour = colour; return n; };
    const auto two = render (c, { coloured (note (0, 384, 60, 0.8f), 0), coloured (note (0, 384, 67, 0.8f), 1),
                                  coloured (note (96, 48, 62, 0.8f, true), 0) }, 256, data);
    const auto two62 = tone (two.l, f (62), 240 * tick, 380 * tick), two67 = tone (two.l, f (67), 240 * tick, 380 * tick);
    CHECK (two62 > 0.02 && two67 > 0.02 && tone (two.l, f (55), 240 * tick, 380 * tick) < 0.1 * two62);
    const auto other = render (c, { coloured (note (0, 384, 60, 0.8f), 2), coloured (note (96, 48, 65, 0.8f, true), 0) }, 256, data);
    CHECK (tone (other.l, f (60), 240 * tick, 380 * tick) > 10.0 * tone (other.l, f (65), 240 * tick, 380 * tick));
}

// FL 2026 (REA_rodada190pi §3, /R renders at velocity 25 / 50 / 100 / 128): with byte +0x20a set Ogun leaves FL's link
// velocity on, so Final.Vol carries Init.Vol; a slide note moves it
TEST (flparity190pi_ogun_link_velocity)
{
    using namespace flparity190pi;
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto channel = natives::channelFromPresetFile (
        InstrumentKind::ogun, (std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "Ogun" / "Default (tonal).fst").string(),
        natives142::flAccess());
    if (! channel)
    {
        std::printf ("  flparity190pi: no Ogun preset here\n");
        return;
    }
    const auto loud = render (*channel, { note (0, 96, 60, 0.78125f) });
    const auto soft = render (*channel, { note (0, 96, 60, 50.0f / 128.0f) });
    const auto ratio = rms (soft.l, 0, 90 * tick) / rms (loud.l, 0, 90 * tick);
    // rollup 0.1.31 step 7: the law from engine revision 2 on (new channels, .flp imports); a channel of a project saved
    // before (revision 0 / 1) plays as LYNX did: velocity 50 renders as 100 does, bit for bit (a slide too)
    {
        auto older = *channel;
        older.engineRev = 1;
        const auto oldLoud = render (older, { note (0, 96, 60, 0.78125f) });
        const auto oldSoft = render (older, { note (0, 96, 60, 50.0f / 128.0f) });
        const auto oldSlide = render (older, { note (0, 384, 60, 0.78125f), note (48, 48, 60, 50.0f / 128.0f, true) });
        const auto oldHeld = render (older, { note (0, 384, 60, 0.78125f) });
        CHECK (oldLoud.l == oldSoft.l && oldLoud.r == oldSoft.r);
        CHECK (oldSlide.l == oldHeld.l);
    }
    const auto expected = initVolume (50.0 / 128.0);
    std::printf ("  flparity190pi: Ogun velocity 50 / 100 = %.4f (Init.Vol %.4f)\n", ratio, expected);
    CHECK (std::abs (ratio - expected) < 0.06 * expected || std::abs (ratio - 1.0) < 0.06); // (a patch without link: unchanged)
    if (std::abs (ratio - expected) < 0.06 * expected)
    {
        // a slide note at velocity 50 over a note at 100: the host's Final.Vol moves to Init.Vol (50) (FL 2026, def_vo:
        // FL and LYNX within 0.01 dB per 48 ticks); the patch's own velocity (read at note on) stays
        const auto down = render (*channel, { note (0, 384, 60, 0.78125f), note (48, 48, 60, 50.0f / 128.0f, true) });
        const auto held = render (*channel, { note (0, 384, 60, 0.78125f) });
        CHECK (std::abs (rms (down.l, 0, 40 * tick) / rms (held.l, 0, 40 * tick) - 1.0) < 1.0e-6);
        const auto fell = rms (down.l, 110 * tick, 200 * tick) / rms (held.l, 110 * tick, 200 * tick);
        std::printf ("  flparity190pi: Ogun slid to velocity 50: %.4f of the held note\n", fell);
        CHECK (std::abs (fell - expected) < 0.06 * expected);
    }
}

// FL before 12 numbered a channel's plugin parameters from 0x2000 (Overcome 11.1.0, Housework 11.0.3); FL 12 on from 0x8000
TEST (flparity190pi_fl11_plugin_parameter_numbers)
{
    for (const auto* version : { "11.1.0", "12.3.0.72" })
    {
        FlpBuilder b (96);
        b.ascii (0xC7, version);
        b.u8 (0x40); b.u16 (0);
        b.u8 (0x15); b.u8 (2);
        std::vector<std::uint8_t> link;
        const auto u32 = [&link] (std::uint32_t v) { for (int k = 0; k < 4; ++k) link.push_back ((std::uint8_t) (v >> (8 * k))); };
        u32 (5u << 16), u32 (0), u32 (0x2012u), u32 (8), u32 (0x1d5);
        b.data (0xE3, link);
        const auto p = flp::parseProject (b.bytes);
        CHECK (p.automationLinks.size() == 1);
        if (! p.automationLinks.empty())
            CHECK (p.automationLinks[0].target.param() == (std::string (version) == "11.1.0" ? 0x8012 : 0x2012));
    }
}

// FL's automation of Sytrus' own parameters (REA_rodada190pi §4, Overcome's intro): the master level (0) moves the
// sounding notes; Mod X / Y (18 / 19) are taken; the others are not Sytrus' main page
TEST (flparity190pi_sytrus_main_page_automation)
{
    using namespace lynx;
    CHECK (sytrus::automatable (0) && sytrus::automatable (18) && sytrus::automatable (19) && ! sytrus::automatable (3) && ! sytrus::automatable (20));
    if (realFlpDir.empty())
        return;
    const auto channel = natives::channelFromPresetFile (
        InstrumentKind::sytrus, (std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "Sytrus" / "Default.fst").string(),
        natives142::flAccess());
    if (! channel)
        return;
    const auto play = [&] (bool automate) {
        auto in = makeInstrument (InstrumentKind::sytrus);
        in->prepare (48000.0);
        in->configure (*channel, 120.0);
        in->settleNew();
        std::vector<float> l (48000), r (48000);
        in->noteOn (60, 0.78125f);
        in->render (l.data(), r.data(), 24000, 0.7071f, 0.7071f);
        if (automate)
            in->setParameter (0, 64.0f / 160.0f); // master level 64 (of 0…160)
        in->render (l.data() + 24000, r.data() + 24000, 24000, 0.7071f, 0.7071f);
        return l;
    };
    const auto plain = play (false), half = play (true);
    const auto ratio = flparity190pi::rms (half, 36000, 48000) / flparity190pi::rms (plain, 36000, 48000);
    const auto expected = ((std::pow (11.0, 64.0 / 128.0) - 1.0) / 10.0); // (gCurve (64, 128) against 128's 1)
    CHECK (std::abs (ratio - expected) < 0.02);
    CHECK (std::abs (flparity190pi::rms (half, 12000, 24000) / flparity190pi::rms (plain, 12000, 24000) - 1.0) < 1.0e-6);
}
