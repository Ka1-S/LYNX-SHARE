// Round 190gp (A1): the built-in synth renders up to four voices side by side (lynx::setSynthVoiceLanes). The same
// notes, settings and blocks through both ways must give the same samples, bit for bit: random settings (every
// oscillator shape, AM / ring, envelopes, LFOs, key tracking, vibrato, sweep, the 3-band tone, porta, mono, filter
// types), random notes, odd block sizes, settings changed while notes sound, the pitch wheel.

#include "core/SynthPresets.h"

namespace synthlanes190gp
{
struct Rng
{
    std::uint32_t s = 1;
    std::uint32_t next() noexcept { return s = s * 1664525u + 1013904223u; }
    float unit() noexcept { return (float) (next() >> 8) / 16777216.0f; }
    int below (int n) noexcept { return (int) ((next() >> 8) % (std::uint32_t) n); }
    bool chance (float p) noexcept { return unit() < p; }
};

inline lynx::SynthSettings randomSettings (Rng& g)
{
    using lynx::Waveform;
    lynx::SynthSettings s;
    const Waveform shapes[] = { Waveform::sine, Waveform::triangle, Waveform::saw, Waveform::square, Waveform::noise };
    for (auto& o : s.osc)
    {
        o.wave = shapes[g.below (g.chance (0.1f) ? 5 : 4)];
        o.level = g.chance (0.25f) ? 0.0f : g.unit();
        o.octave = g.below (5) - 2;
        o.semitones = g.below (9) - 4;
        o.detune = g.unit() * 60.0f - 30.0f;
        o.pan = g.chance (0.3f) ? g.unit() * 2.0f - 1.0f : 0.0f;
    }
    s.attack = g.chance (0.5f) ? 0.0005f + 0.01f * g.unit() : 0.3f * g.unit();
    s.decay = 0.5f * g.unit();
    s.sustain = g.unit();
    s.release = 0.01f + 0.3f * g.unit();
    s.cutoff = 100.0f + 15000.0f * g.unit() * g.unit();
    s.resonance = g.unit();
    s.filterType = g.below (4);
    s.ladder = g.chance (0.1f);
    s.am = g.chance (0.2f);
    s.ring = s.am && g.chance (0.5f);
    s.fm = g.chance (0.1f) ? g.unit() : 0.0f;
    s.keyTrack = g.chance (0.2f) ? g.unit() : 0.0f;
    s.vibratoCents = g.chance (0.15f) ? 50.0f * g.unit() : 0.0f;
    s.stereoTremolo = g.chance (0.05f) ? g.unit() : 0.0f;
    s.sweep = g.chance (0.15f) ? 4.0f * g.unit() : 0.0f;
    s.sweepTime = 0.01f + 0.2f * g.unit();
    if (g.chance (0.3f))
        s.bandGain = { 2.0f * g.unit(), 2.0f * g.unit(), 2.0f * g.unit() };
    if (g.chance (0.3f))
    {
        s.filterEnv.amount = 4.0f * g.unit() - 2.0f;
        s.filterEnv.attack = 0.05f * g.unit();
        s.filterEnv.hold = 0.02f * g.unit();
        s.filterEnv.decay = 0.3f * g.unit();
        s.filterEnv.sustain = g.unit();
        s.filterEnv.shape = g.chance (0.3f) ? 1 : 0;
    }
    if (g.chance (0.2f))
    {
        s.pitchEnv.amount = 12.0f * g.unit() - 6.0f;
        s.pitchEnv.decay = 0.2f * g.unit();
    }
    for (auto& l : s.lfo)
        if (g.chance (0.15f))
        {
            l.amount = g.unit();
            l.period = 0.05f + g.unit();
            l.shape = g.below (3);
            l.delay = 0.05f * g.unit();
            l.attack = 0.1f * g.unit();
            l.sync = g.chance (0.3f);
            l.global = g.chance (0.2f);
        }
    s.mono = g.chance (0.1f);
    s.porta = g.chance (0.15f);
    s.portaBeats = 0.25f * g.unit();
    return s;
}

// a run of random notes and blocks; the outputs' bits hashed (every sample of both sides)
inline std::uint64_t run (std::uint32_t seed, bool laneOn, int& lanesBlocks)
{
    lynx::setSynthVoiceLanes (laneOn);
    Rng g { seed };
    auto in = lynx::makeInstrument (lynx::InstrumentKind::synth);
    in->prepare (44100.0);
    lynx::ChannelDef ch;
    ch.instrument = lynx::InstrumentKind::synth;
    ch.synth = randomSettings (g);
    in->configure (ch, 128.0);
    std::uint64_t h = 1469598103934665603ull;
    std::vector<float> l (700), r (700);
    std::vector<int> held;
    for (int block = 0; block < 400; ++block)
    {
        const int sizes[] = { 1, 7, 37, 64, 128, 256, 300, 512, 700 };
        const auto n = sizes[g.below (9)];
        if (g.chance (0.25f))
        {
            const auto key = 30 + g.below (60);
            if (g.chance (0.2f))
                in->setNextNoteExpression (g.unit() * 2.0f - 1.0f, g.unit() * 100.0f - 50.0f, g.chance (0.3f));
            in->noteOn (key, 0.1f + 0.9f * g.unit());
            held.push_back (key);
        }
        if (! held.empty() && g.chance (0.2f))
        {
            const auto k = (std::size_t) g.below ((int) held.size());
            in->noteOff (held[k]);
            held.erase (held.begin() + (std::ptrdiff_t) k);
        }
        if (g.chance (0.02f))
        {
            ch.synth = randomSettings (g);
            in->configure (ch, 128.0);
        }
        if (g.chance (0.05f))
            in->pitchBend (g.unit() * 4.0f - 2.0f);
        for (int i = 0; i < n; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = (i % 5 == 0) ? 0.25f : 0.0f; // (the voices add to what is there)
        in->render (l.data(), r.data(), n, 0.8f, 0.6f);
        for (int i = 0; i < n; ++i)
            for (const auto x : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t bits;
                std::memcpy (&bits, &x, 4);
                h = (h ^ bits) * 1099511628211ull;
            }
        lanesBlocks += n;
    }
    lynx::setSynthVoiceLanes (true);
    return h;
}
} // namespace synthlanes190gp

TEST (s190gp_synth_voice_lanes_bit_identical)
{
    int mismatches = 0, samples = 0;
    for (std::uint32_t seed = 1; seed <= 160; ++seed)
    {
        int a = 0, b = 0;
        const auto scalar = synthlanes190gp::run (seed * 7919u, false, a);
        const auto lanes = synthlanes190gp::run (seed * 7919u, true, b);
        mismatches += scalar != lanes ? 1 : 0;
        samples += a;
        if (scalar != lanes)
            std::printf ("  seed %u: lanes %016llx, voice by voice %016llx\n", seed * 7919u, (unsigned long long) lanes, (unsigned long long) scalar);
    }
    CHECK (samples > 1000000);
    CHECK (mismatches == 0);
}

// the synth presets (the perfbench scenarios' channels) take the lanes, and still sound
TEST (s190gp_synth_presets_render_with_lanes)
{
    for (const auto& [name, preset] : lynx::synthPresets())
    {
        auto in = lynx::makeInstrument (lynx::InstrumentKind::synth);
        in->prepare (44100.0);
        lynx::ChannelDef ch;
        ch.instrument = lynx::InstrumentKind::synth;
        ch.synth = preset;
        in->configure (ch, 120.0);
        for (int k : { 48, 52, 55, 60, 64 })
            in->noteOn (k, 0.8f);
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        in->render (l.data(), r.data(), 4096, 1.0f, 1.0f);
        float peak = 0.0f;
        for (auto x : l)
            peak = std::max (peak, std::abs (x));
        CHECK (peak > 1.0e-4f);
        CHECK (std::isfinite (peak));
    }
}
