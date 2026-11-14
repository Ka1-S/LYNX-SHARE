// Round 190gp (A9): smart disable takes each built-in effect's declared tail (Effect::tailSamples) instead of the fixed
// 1 s hold. The test of every effect that declares one, against the old fixed hold (lynx::setDeclaredTails): random
// settings, a loud burst (noise, a low sine, clicks; up to +12 dBFS), digital silence, a second burst, silence. While
// the old output's tail is above −120 dBFS the new one must be the same, bit for bit, and after it both are silence;
// on the second burst (the effect woken from where it stopped) the two differ by less than −120 dBFS.

namespace smarttails190gp
{
constexpr int rate = 44100;

struct Rng
{
    std::uint32_t s = 1;
    std::uint32_t next() noexcept { return s = s * 1664525u + 1013904223u; }
    float unit() noexcept { return (float) (next() >> 8) / 16777216.0f; }
};

struct Result
{
    int tailMismatches = 0;    // a sample that differs where the old tail is audible (> −120 dBFS)
    int loudSilence = 0;       // a differing sample where either output is above −120 dBFS in the tail
    double resumeDiff = 0.0;   // the largest difference on the second burst
    bool shorter = false;      // the declared tail ended the hold early (the outputs differ somewhere)
};

inline std::shared_ptr<lynx::SampleData> signal (Rng& g, double loud)
{
    auto d = std::make_shared<lynx::SampleData>();
    const auto total = (std::size_t) (4.2 * rate);
    d->left.assign (total, 0.0f);
    d->right.assign (total, 0.0f);
    auto burst = [&] (double from, double seconds) {
        for (auto i = (std::size_t) (from * rate); i < (std::size_t) ((from + seconds) * rate); ++i)
        {
            const auto t = (double) i / rate;
            const auto noise = (float) (g.unit() * 2.0f - 1.0f);
            const auto tone = (float) std::sin (2.0 * 3.141592653589793 * 31.0 * t);
            const auto click = (i % 4410) == 0 ? 1.0f : 0.0f;
            d->left[i] = (float) loud * (0.5f * noise + 0.4f * tone + 0.1f * click);
            d->right[i] = (float) loud * (0.5f * (float) (g.unit() * 2.0f - 1.0f) - 0.4f * tone);
        }
    };
    burst (0.05, 0.4);
    burst (2.1, 0.3); // (1.65 s of silence after the first: past the old hold)
    return d;
}

// the song: the signal as an audio clip into insert 1, which holds the effect
inline std::vector<float> render (lynx::EffectKind kind, const lynx::EffectParams& params, std::shared_ptr<const lynx::SampleData> data,
                                  int block, bool declared)
{
    using namespace lynx;
    setDeclaredTails (declared);
    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer (1);
    auto slot = makeEffectSlot (kind);
    slot.params = params;
    song.mixer[1].effects.push_back (slot);
    ChannelDef ch;
    ch.name = "Take";
    ch.instrument = InstrumentKind::audioClip;
    ch.samplePath = "/tails/take.wav";
    ch.mixerTrack = 1;
    song.channels.push_back (ch);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.start = 0;
    clip.length = (std::int64_t) (4.2 * song.bpm / 60.0 * ppq);
    song.playlist.push_back (clip);
    song.mode = PlayMode::song;
    auto state = RackState::build (song, nullptr, rate, [&] (const std::string&) { return data; });
    RackRenderer renderer;
    renderer.prepare (rate, 2048);
    const auto total = (int) (4.0 * rate);
    std::vector<float> out ((std::size_t) total * 2), l ((std::size_t) block), r ((std::size_t) block);
    const auto tps = ticksPerSample (song.bpm, rate);
    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        renderer.process (state.get(), true, pos * tps, n, l.data(), r.data());
        for (int i = 0; i < n; ++i)
        {
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i];
            out[(std::size_t) (pos + i) * 2 + 1] = r[(std::size_t) i];
        }
    }
    setDeclaredTails (true);
    return out;
}

inline Result compare (const std::vector<float>& a, const std::vector<float>& b)
{
    Result res;
    constexpr float audible = 1.0e-6f; // −120 dBFS
    const auto tailEnd = (std::size_t) (2.1 * rate) * 2;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto same = std::memcmp (&a[i], &b[i], sizeof (float)) == 0;
        if (same)
            continue;
        res.shorter = true;
        if (i < tailEnd)
        {
            if (std::abs (a[i]) >= audible)
                ++res.tailMismatches;
            if (std::abs (a[i]) >= audible || std::abs (b[i]) >= audible)
                ++res.loudSilence;
        }
        else
            res.resumeDiff = std::max (res.resumeDiff, (double) std::abs (a[i] - b[i]));
    }
    return res;
}
} // namespace smarttails190gp

TEST (s190gp_declared_tails_never_cut_a_tail)
{
    using namespace lynx;
    const EffectKind kinds[] = { EffectKind::eq, EffectKind::parametricEq, EffectKind::filter, EffectKind::distortion,
                                 EffectKind::utility, EffectKind::limiter, EffectKind::compressor };
    int runs = 0, shorter = 0, bad = 0;
    std::map<int, int> earlyByKind;
    double worstResume = 0.0;
    for (const auto kind : kinds)
        for (std::uint32_t seed = 1; seed <= 24; ++seed)
        {
            smarttails190gp::Rng g { seed * 2654435761u + (std::uint32_t) kind };
            auto params = makeEffectSlot (kind).params; // (seed 1: the defaults)
            if (seed > 1)
                for (std::size_t p = 0; p < effectInfo (kind).params.size() && p < params.size(); ++p)
                    params[p] = g.unit();
            const auto data = smarttails190gp::signal (g, seed % 3 == 0 ? 4.0 : 0.9);
            const auto block = seed % 2 == 0 ? 64 : 256;
            const auto fixed = smarttails190gp::render (kind, params, data, block, false);
            const auto declared = smarttails190gp::render (kind, params, data, block, true);
            const auto r = smarttails190gp::compare (fixed, declared);
            ++runs;
            shorter += r.shorter ? 1 : 0;
            earlyByKind[(int) kind] += r.shorter ? 1 : 0;
            worstResume = std::max (worstResume, r.resumeDiff);
            if (r.tailMismatches > 0 || r.loudSilence > 0 || r.resumeDiff >= 1.0e-6)
            {
                ++bad;
                std::printf ("  %s seed %u: %d audible tail samples differ, %d differing samples above -120 dBFS, resume diff %.3g\n",
                             effectInfo (kind).name, seed, r.tailMismatches, r.loudSilence, r.resumeDiff);
            }
        }
    std::printf ("  declared tails: %d runs, %d ended the hold early, worst difference on waking %.3g (%.1f dBFS)\n", runs, shorter,
                 worstResume, worstResume > 0.0 ? 20.0 * std::log10 (worstResume) : -999.0);
    for (const auto& [kind, early] : earlyByKind)
        std::printf ("    %s: %d of 24 runs ended early\n", effectInfo ((EffectKind) kind).name, early);
    CHECK (bad == 0);
    CHECK (shorter > runs / 4); // (the test does exercise the early end)
}

// every built-in that declares a tail gives a non-negative one at its defaults; the others keep the hold
TEST (s190gp_declared_tails_defaults)
{
    using namespace lynx;
    for (const auto kind : { EffectKind::eq, EffectKind::parametricEq, EffectKind::filter, EffectKind::distortion, EffectKind::utility,
                             EffectKind::limiter, EffectKind::compressor })
    {
        auto fx = makeEffect (kind);
        fx->prepare (44100.0);
        std::vector<float> l (64, 0.0f), r (64, 0.0f);
        const auto params = makeEffectSlot (kind).params;
        fx->process (l.data(), r.data(), 64, params, 120.0);
        const auto tail = fx->tailSamples();
        std::printf ("  %s: declared tail %d samples at its defaults\n", effectInfo (kind).name, tail);
        CHECK (tail >= 0);
    }
    auto reverb = makeEffect (EffectKind::reverb);
    CHECK (reverb->tailSamples() == -1);
}
