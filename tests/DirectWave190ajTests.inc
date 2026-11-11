// Round 190aj: a DirectWave program's delay / chorus / reverb ring on after the last voice (FL's plugin runs them in
// every block). LYNX's rack stopped rendering the channel with the last voice, which cut the tail (FL's renders of the
// Stage Grand, Strings and Choir programs: REA_rodada190aj_dw.md).

namespace dw190aj
{
inline lynx::dw::Program program (bool reverb, bool delay)
{
    lynx::dw::Program p;
    lynx::dw::Zone z;
    z.frames = 4000;
    z.sends[2] = reverb ? 0.8f : 0.0f;
    z.sends[0] = delay ? 0.8f : 0.0f;
    p.zones.push_back (z);
    p.reverbOn = reverb;
    p.reverb[0] = 0.6f; p.reverb[1] = 0.5f; p.reverb[2] = 0.5f; p.reverb[3] = 0.5f;
    p.delayOn = delay;
    p.delay[0] = 0.9f; p.delay[1] = 0.5f; p.delay[2] = 0.0f; p.delay[3] = 1.0f;
    return p;
}

inline std::vector<std::shared_ptr<const lynx::dw::ZoneSamples>> samples()
{
    auto s = std::make_shared<lynx::dw::ZoneSamples>();
    s->bits = 16; s->channels = 1; s->frames = 4000;
    std::vector<std::int16_t> v (4000 + 512, 0);
    for (int j = 0; j < 4000; ++j)
        v[(std::size_t) j + 256] = (std::int16_t) (((j * 37) % 2048 - 1024) * 12);
    s->i16.push_back (std::move (v));
    return { s };
}

struct Run { double voiceEnd = -1.0, ringEnd = -1.0, tailPeak = 0.0, lastPeak = 0.0; };
// one short note; seconds until the last voice ends and until the engine rests, the loudest sample after the voice
inline Run run (bool reverb, bool delay, double bpm = 120.0)
{
    lynx::dw::Engine e;
    e.setSampleRate (44100.0);
    e.setTempo (bpm);
    e.setProgram (program (reverb, delay), samples(), {});
    Run r;
    e.noteOn (1, 60, 0.8, 0.8, 0.0, 0.0);
    std::vector<float> buf (2 * 256);
    for (int block = 0; block < 60 * 44100 / 256; ++block)
    {
        if (block == 8)
            e.noteOff (1);
        std::fill (buf.begin(), buf.end(), 0.0f);
        e.render (buf.data(), 256);
        const auto t = (block + 1) * 256.0 / 44100.0;
        double peak = 0.0;
        for (const float x : buf)
            peak = std::max (peak, (double) std::fabs (x));
        if (r.voiceEnd < 0.0 && ! e.active())
            r.voiceEnd = t;
        else if (r.voiceEnd >= 0.0)
            r.tailPeak = std::max (r.tailPeak, peak);
        if (r.voiceEnd >= 0.0 && ! e.ringing())
        {
            r.ringEnd = t;
            r.lastPeak = peak;
            break;
        }
    }
    return r;
}
} // namespace dw190aj

TEST (directwave190aj_effects_ring_after_the_last_voice)
{
    // no program effect: the engine rests with its last voice, as before
    const auto dry = dw190aj::run (false, false);
    CHECK (dry.voiceEnd > 0.0 && dry.ringEnd == dry.voiceEnd);

    // reverb: audible after the voice, the engine keeps ringing until the tail has been under -200 dB for 131 072 frames, and then it rests
    const auto rev = dw190aj::run (true, false);
    CHECK (rev.voiceEnd > 0.0 && rev.tailPeak > 1.0e-4);
    CHECK (rev.ringEnd > rev.voiceEnd + 131072.0 / 44100.0 && rev.ringEnd < 59.0);
    CHECK (rev.lastPeak <= 1.0e-10);

    // delay (0.9 beat at 45 BPM = 52 920 frames between echoes): the gap between two echoes does not end it
    const auto del = dw190aj::run (false, true, 45.0);
    CHECK (del.voiceEnd > 0.0 && del.tailPeak > 1.0e-4);
    CHECK (del.ringEnd > del.voiceEnd + 131072.0 / 44100.0 && del.ringEnd < 59.0);
    CHECK (del.lastPeak <= 1.2e-9); // (the delay's own dither: 1e-10 + 5e-10 / (1 - feedback))
    if (rev.ringEnd < 0.0 || del.ringEnd < 0.0 || dry.ringEnd != dry.voiceEnd)
        std::printf ("  dw190aj: dry %.3f/%.3f reverb %.3f/%.3f peak %g delay %.3f/%.3f peak %g\n", dry.voiceEnd, dry.ringEnd,
                     rev.voiceEnd, rev.ringEnd, rev.tailPeak, del.voiceEnd, del.ringEnd, del.tailPeak);
}
