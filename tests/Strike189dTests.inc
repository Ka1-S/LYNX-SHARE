// Round trans189d (issue #94): Strike Shaper, LYNX's level-independent transient designer (core/StrikeShaper.h).
// The role's public description: two envelopes compared by their difference, no threshold, attack ±15 dB, sustain
// ±24 dB. Checked here: the gains on a drum hit at four levels 60 dB apart (the same within 0.2 dB), the neutral
// setting bit for bit, steady tones untouched, Guard's ceiling, Mix, the laws' texts, no latency, the substitute.

namespace strike189d
{
// a drum-like hit every 2 s (a 180 Hz body plus noise, 0.5 ms rise, 120 ms decay) over an optional steady bed
inline std::vector<float> hits (double level, double bed, int count = 3)
{
    std::vector<float> x ((std::size_t) (48000 * 2 * count), 0.0f);
    std::uint32_t seed = 12345;
    auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 8388608.0 - 1.0; };
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto t = std::fmod ((double) i / 48000.0, 2.0);
        auto v = bed * std::sin (2.0 * 3.141592653589793 * 440.0 * (double) i / 48000.0);
        if (t > 0.5)
        {
            const auto u = t - 0.5;
            v += std::exp (-u / 0.12) * (0.7 * std::sin (2.0 * 3.141592653589793 * 180.0 * u) + 0.5 * noise()) * std::min (1.0, u / 0.0005);
        }
        x[i] = (float) (level * v);
    }
    return x;
}
inline std::vector<float> run (const std::vector<float>& in, const lynx::EffectParams& p, int* latency = nullptr)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::strikeShaper);
    fx->prepare (48000.0);
    auto l = in, r = in;
    for (std::size_t at = 0; at < l.size(); at += 480)
        fx->process (l.data() + at, r.data() + at, (int) std::min<std::size_t> (480, l.size() - at), p, 120.0);
    if (latency != nullptr) *latency = fx->latencySamples();
    return l;
}
// rms gain (dB) of `out` over `in` in [a, b) seconds after the second hit's start
inline double gainDb (const std::vector<float>& in, const std::vector<float>& out, double a, double b)
{
    double ei = 0.0, eo = 0.0;
    for (auto i = (std::size_t) ((2.5 + a) * 48000.0); i < (std::size_t) ((2.5 + b) * 48000.0); ++i)
    {
        ei += (double) in[i] * in[i];
        eo += (double) out[i] * out[i];
    }
    return 10.0 * std::log10 ((eo + 1e-30) / (ei + 1e-30));
}
} // namespace strike189d

TEST (strike189d_info_neutral_and_latency)
{
    using namespace lynx;
    using namespace strike189d;
    namespace s = strike;
    const auto& info = effectInfo (EffectKind::strikeShaper);
    CHECK (std::string (info.name) == "Strike Shaper" && (int) info.params.size() == s::count);
    CHECK (plugui::paramIndex (EffectKind::strikeShaper, "Attack") == s::attack);
    const auto p = makeEffectSlot (EffectKind::strikeShaper).params;
    const auto in = hits (0.5, 0.05);
    int lat = -1;
    CHECK (run (in, p, &lat) == in); // the neutral start passes the signal bit for bit
    CHECK (lat == 0);                // no look-ahead
    // Mix 0 is the dry signal; Output moves the level by its dB
    auto q = p;
    q[s::attack] = 1.0f;
    q[s::sustain] = 0.0f;
    q[s::mix] = 0.0f;
    CHECK (run (in, q) == in);
    q = p;
    q[s::output] = s::outputValue (-6.0);
    CHECK (std::abs (gainDb (in, run (in, q), 0.0, 1.0) + 6.0) < 0.01);
    // the laws' texts (the panel's printed stops)
    CHECK (formatEffectParam (EffectKind::strikeShaper, s::attack, 1.0f, 120.0) == "+15.0 dB");
    CHECK (formatEffectParam (EffectKind::strikeShaper, s::attack, 0.2f, 120.0) == "-10.0 dB");
    CHECK (formatEffectParam (EffectKind::strikeShaper, s::sustain, 0.0f, 120.0) == "-24.0 dB");
    CHECK (formatEffectParam (EffectKind::strikeShaper, s::sustain, 0.8f, 120.0) == "+12.0 dB");
    CHECK (formatEffectParam (EffectKind::strikeShaper, s::output, 0.0f, 120.0) == "-20.0 dB");
    CHECK (formatEffectParam (EffectKind::strikeShaper, s::output, 1.0f, 120.0) == "+6.0 dB");
    CHECK (formatEffectParam (EffectKind::strikeShaper, s::detectHp, 0.0f, 120.0) == "off");
    for (const auto db : { -15.0, -11.5, -3.0, 0.0, 2.0, 14.0 })
        CHECK (std::abs (s::attackDb (s::attackValue (db)) - db) < 1e-4);
    for (const auto db : { -20.0, -8.0, 0.5, 4.0, 6.0 })
        CHECK (std::abs (s::outputDb (s::outputValue (db)) - db) < 1e-4);
}

TEST (strike189d_attack_and_sustain_on_drum_hits_at_any_level)
{
    using namespace lynx;
    using namespace strike189d;
    namespace s = strike;
    const auto base = makeEffectSlot (EffectKind::strikeShaper).params;
    struct Case { float attack, sustain; };
    const Case cases[] = { { 1.0f, 0.5f }, { 0.0f, 0.5f }, { 0.5f, 1.0f }, { 0.5f, 0.0f } };
    for (const auto bed : { 0.0, 0.1 })
        for (const auto& c : cases)
        {
            auto p = base;
            p[s::attack] = c.attack;
            p[s::sustain] = c.sustain;
            std::array<double, 4> onset {}, tail {};
            int k = 0;
            for (const auto level : { 1.0, 0.1, 0.01, 0.001 }) // 0, -20, -40, -60 dB: no threshold, the same shape
            {
                const auto in = hits (level, bed);
                const auto out = run (in, p);
                onset[(std::size_t) k] = gainDb (in, out, 0.0, 0.005);
                tail[(std::size_t) k] = gainDb (in, out, 0.1, 0.3);
                ++k;
            }
            for (int i = 1; i < 4; ++i)
                CHECK (std::abs (onset[(std::size_t) i] - onset[0]) < 0.2 && std::abs (tail[(std::size_t) i] - tail[0]) < 0.2);
            if (c.attack == 1.0f) CHECK (onset[0] > (bed > 0.0 ? 10.0 : 12.0) && std::abs (tail[0]) < 1.0);   // +15 dB on the hit's start
            if (c.attack == 0.0f) CHECK (onset[0] < (bed > 0.0 ? -9.5 : -11.5) && std::abs (tail[0]) < 1.0);  // -15 dB
            if (c.sustain == 1.0f) CHECK (std::abs (onset[0]) < 0.5 && tail[0] > 10.0);                       // +24 dB on the tail
            if (c.sustain == 0.0f) CHECK (std::abs (onset[0]) < 0.5 && tail[0] < -9.0);                       // -24 dB
            std::printf ("  strike A %+.0f S %+.0f bed %.1f: onset %+.2f dB, tail %+.2f dB\n", s::attackDb (c.attack), s::sustainDb (c.sustain), bed,
                         onset[0], tail[0]);
        }
}

TEST (strike189d_steady_tones_guard_and_extremes)
{
    using namespace lynx;
    using namespace strike189d;
    namespace s = strike;
    auto p = makeEffectSlot (EffectKind::strikeShaper).params;
    // a steady tone holds no transient and no tail: the extreme settings leave it alone (the detector's 15 ms hold
    // covers a 35 Hz cycle; the dead zone eats the held peak's ripple)
    for (const auto hz : { 40.0, 100.0, 1000.0 })
        for (const auto a : { 0.0f, 1.0f })
        {
            std::vector<float> in (96000);
            for (std::size_t i = 0; i < in.size(); ++i) in[i] = (float) (0.5 * std::sin (2.0 * 3.141592653589793 * hz * (double) i / 48000.0));
            p[s::attack] = a;
            p[s::sustain] = 1.0f - a;
            const auto out = run (in, p);
            double e = 0.0, sig = 0.0;
            for (std::size_t i = 48000; i < in.size(); ++i)
            {
                e += ((double) out[i] - in[i]) * ((double) out[i] - in[i]);
                sig += (double) in[i] * in[i];
            }
            CHECK (10.0 * std::log10 (e / sig + 1e-30) < -60.0);
        }
    // Guard: full-scale hits lifted by +15 dB and +6 dB out stay under 0 dBFS
    p = makeEffectSlot (EffectKind::strikeShaper).params;
    p[s::attack] = 1.0f;
    p[s::output] = 1.0f;
    p[s::guard] = 1.0f;
    float peak = 0.0f;
    for (const auto x : run (hits (1.0, 0.0), p)) peak = std::max (peak, std::abs (x));
    CHECK (peak <= 1.0f && peak > 0.9f);
    // silence, denormal-level input, huge input and every switch flipped: finite output
    p[s::link] = 0.0f;
    p[s::detectHp] = 0.6f;
    p[s::sustain] = 1.0f;
    std::vector<float> odd (48000, 0.0f);
    for (std::size_t i = 0; i < odd.size(); ++i)
        odd[i] = i < 12000 ? 0.0f : i < 24000 ? 1.0e-38f * (float) (i % 7) : i < 36000 ? 1.0e6f * (float) std::sin ((double) i) : 0.0f;
    bool finite = true;
    for (const auto x : run (odd, p)) finite = finite && std::isfinite (x);
    CHECK (finite);
}

TEST (strike189d_stands_in_for_the_transient_designer)
{
    using namespace lynx;
    for (const auto* name : { "SPL Transient Designer", "SPL Transient Designer Plus", "UAD SPL Transient Designer" })
    {
        PluginIdentity id;
        id.format = ForeignPlugin::Format::vst3;
        id.name = name;
        const auto* entry = findPluginSubstitute (id, false);
        CHECK (entry != nullptr && entry->effect == EffectKind::strikeShaper);
    }
}
