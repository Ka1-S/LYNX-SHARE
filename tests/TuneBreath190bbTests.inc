// Round 190bb (issue #103): LYNX Tune's Breathiness (docs/analise/REA_rodada190bb-tune.md) — aspiration noise with the
// corrected voice's own formants, on voiced sounds only; LYNX's own addition, off (and skipped) by default. Synthetic
// vowels and the helpers of LynxTuneTests95.inc (r95::).

namespace tb190
{
// power spectrum of x[from … from + n) (Hann; n a power of two), bins 0 … n/2 − 1
std::vector<double> spectrum (const std::vector<float>& x, int from, int n)
{
    std::vector<double> re ((std::size_t) n), im ((std::size_t) n, 0.0);
    for (int i = 0; i < n; ++i)
        re[(std::size_t) i] = (double) x[(std::size_t) (from + i)] * (0.5 - 0.5 * std::cos (2.0 * r95::pi * i / n));
    for (int i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[(std::size_t) i], re[(std::size_t) j]); std::swap (im[(std::size_t) i], im[(std::size_t) j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const auto ang = -2.0 * r95::pi / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; ++k)
            {
                const auto wr = std::cos (ang * k), wi = std::sin (ang * k);
                const auto a = (std::size_t) (i + k), b = (std::size_t) (i + k + len / 2);
                const auto vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr; im[b] = im[a] - vi;
                re[a] += vr; im[a] += vi;
            }
    }
    std::vector<double> p ((std::size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
        p[(std::size_t) k] = re[(std::size_t) k] * re[(std::size_t) k] + im[(std::size_t) k] * im[(std::size_t) k];
    return p;
}
double band (const std::vector<double>& p, double fs, double lo, double hi)
{
    const auto n = 2.0 * (double) p.size();
    double s = 0.0;
    for (int k = (int) std::ceil (lo * n / fs); k <= (int) std::floor (hi * n / fs) && k < (int) p.size(); ++k)
        s += p[(std::size_t) k];
    return s;
}
double power (const std::vector<float>& x, int a, int b)
{
    double s = 0.0;
    for (int i = a; i < b; ++i) s += (double) x[(std::size_t) i] * x[(std::size_t) i];
    return s;
}
std::vector<float> minus (const std::vector<float>& a, const std::vector<float>& b)
{
    std::vector<float> d (a.size());
    for (std::size_t i = 0; i < a.size(); ++i) d[i] = a[i] - b[i];
    return d;
}
} // namespace tb190

TEST (lynx_tune_round190bb_breathiness_law_and_default)
{
    namespace t = lynx::lynxtune;
    CHECK (t::breathGain (0.0f) == 0.0 && std::abs (t::breathGain (1.0f) - t::breathFull) < 1e-12);
    CHECK (std::abs (t::breathGain (0.5f) - t::breathFull / 4.0) < 1e-12); // a square law: half travel = −12 dB
    CHECK (t::breathGain (-1.0f) == 0.0 && t::breathGain (2.0f) == t::breathFull);
    // appended after the round 166c parameters with a neutral default: an old project keeps its sound
    const auto& info = t::info();
    CHECK ((int) info.params.size() == t::count && t::count <= lynx::maxEffectParams);
    CHECK (t::breathiness > t::scaleTranspose && t::scaleTranspose == t::throatLength + 5);
    CHECK (info.params[(std::size_t) t::breathiness].defaultValue == 0.0f);
    CHECK (std::string (info.params[(std::size_t) t::breathiness].name) == "Breathiness");
    CHECK (t::paramText (t::breathiness, 0.5f) == "50%" && t::paramText (t::breathiness, 0.0f) == "0%");

    // the band-pass bank: third-octaves from 250 Hz, as many as fit under 0.42 fs; the tilt is +6 dB per octave around
    // 2 kHz within −18 … +12 dB
    lynx::vocal::BreathNoise b;
    b.prepare (44100.0);
    CHECK (b.bands() == lynx::vocal::BreathNoise::maxBands && std::abs (b.bandHz (0) - 250.0) < 1e-9 && std::abs (b.bandHz (9) - 2000.0) < 1e-6);
    CHECK (std::abs (b.bandWeight (9) - 1.0) < 1e-9 && std::abs (20.0 * std::log10 (b.bandWeight (12)) - 6.0) < 1e-6);
    CHECK (std::abs (20.0 * std::log10 (b.bandWeight (0)) + 18.0) < 1e-6 && std::abs (20.0 * std::log10 (b.bandWeight (18)) - 12.0) < 1e-6);
    b.prepare (8000.0);
    CHECK (b.bands() == 12); // … 3175 Hz
    // a white "voice": the noise comes out white times the tilt (+12 dB from the 1 kHz to the 4 kHz octave), and — the
    // tilt would lift it over the voice here — as a whole no louder than the voice (the limit)
    b.prepare (44100.0);
    std::vector<float> in (1 << 16), out (1 << 16);
    unsigned seed = 9;
    for (auto& v : in)
    {
        seed = seed * 1664525u + 1013904223u;
        v = 0.25f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
    }
    for (std::size_t i = 0; i < in.size(); ++i)
        out[i] = b.process (in[i], 1.0f);
    const auto pi2 = tb190::spectrum (in, 16384, 32768), po = tb190::spectrum (out, 16384, 32768);
    const auto at1k = 10.0 * std::log10 (tb190::band (po, 44100.0, 800.0, 1250.0) / tb190::band (pi2, 44100.0, 800.0, 1250.0));
    const auto at4k = 10.0 * std::log10 (tb190::band (po, 44100.0, 3200.0, 5000.0) / tb190::band (pi2, 44100.0, 3200.0, 5000.0));
    CHECK (std::abs (at4k - at1k - 12.0) < 3.0);
    const auto whole = 10.0 * std::log10 (tb190::band (po, 44100.0, 220.0, 18000.0) / tb190::band (pi2, 44100.0, 220.0, 18000.0));
    CHECK (whole < 1.0 && whole > -6.0);
    // a dark "voice" (a 700 Hz tone) is under the limit: its band's noise is the tone's power times the tilt (−9.1 dB)
    b.reset();
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        in[i] = 0.25f * (float) std::sin (2.0 * r95::pi * 700.0 * (double) i / 44100.0);
        out[i] = b.process (in[i], 1.0f);
    }
    const auto ti = tb190::spectrum (in, 16384, 32768), to = tb190::spectrum (out, 16384, 32768);
    CHECK (std::abs (10.0 * std::log10 (tb190::band (to, 44100.0, 500.0, 1000.0) / tb190::band (ti, 44100.0, 500.0, 1000.0)) + 9.1) < 3.0);
    // deterministic: reset() restarts the noise
    b.reset();
    bool same = true;
    for (std::size_t i = 0; i < 4096; ++i)
        same = same && b.process (in[i], 1.0f) == out[i];
    CHECK (same);
    // a voice so quiet that the bands' powers are in the denormal range: finite, and as quiet (the vector square root
    // must be the exact one — on ARM64 sse2neon's "precise" _mm_sqrt_ps returned inf / NaN there)
    b.reset();
    bool finiteTiny = true;
    float peakTiny = 0.0f;
    for (int i = 0; i < 20000; ++i)
    {
        const auto y = b.process (1.0e-20f * (float) std::sin (2.0 * r95::pi * 1000.0 * (double) i / 44100.0), 2.0f);
        finiteTiny = finiteTiny && std::isfinite (y);
        peakTiny = std::max (peakTiny, std::abs (y));
    }
    CHECK (finiteTiny);
    CHECK (peakTiny < 1.0e-15f);
    {
        volatile float tiny = 1.0e-39f;
        const bool denormals = tiny * 0.5f != 0.0f; // (off under flush-to-zero: the powers are then 0 and so is the noise)
        CHECK (! denormals || peakTiny > 1.0e-24f);  // … and not silenced by the non-finite guard either
    }
    // non-finite or absurd input never gets in or out
    b.reset();
    CHECK (b.process (std::numeric_limits<float>::quiet_NaN(), 1.0f) == 0.0f && b.process (std::numeric_limits<float>::infinity(), 1.0f) == 0.0f);
    CHECK (b.process (0.3f, std::numeric_limits<float>::quiet_NaN()) == 0.0f && std::isfinite (b.process (0.3f, 1.0f)));
    // gain 0 and a silent voice give exactly nothing
    b.reset();
    CHECK (b.process (0.5f, 0.0f) == 0.0f);
    b.reset();
    CHECK (b.process (0.0f, 1.0f) == 0.0f);
}

TEST (lynx_tune_round190bb_breathiness_off_is_the_old_path)
{
    namespace t = lynx::lynxtune;
    const double fs = 44100.0;
    const auto x = r95::vowel (fs, (int) (fs * 1.5), [] (double s) { return 190.0 + 20.0 * s; });
    auto p = r95::tuneDefaults();
    p[t::retune] = 0.0f;
    int latency = 0;
    const auto plain = r95::runTune (x, fs, p, latency);
    // an instance whose Breathiness was up for the first 0.4 s and then set to 0: once the level has faded the stage is
    // skipped and the output is bit-identical to the instance that never had it (the stage never feeds the corrector)
    auto fx = lynx::lynxtune::make();
    fx->prepare (fs);
    std::vector<float> l = x, r = x;
    for (int i = 0; i < (int) x.size(); i += 256)
    {
        auto q = p;
        q[t::breathiness] = i < (int) (0.4 * fs) ? 1.0f : 0.0f;
        fx->process (l.data() + i, r.data() + i, std::min (256, (int) x.size() - i), q, 120.0);
    }
    bool differs = false, identical = true;
    for (int i = 8000; i < (int) (0.4 * fs); ++i)
        differs = differs || l[(std::size_t) i] != plain[(std::size_t) i];
    for (int i = (int) (0.65 * fs); i < (int) x.size(); ++i)
        identical = identical && l[(std::size_t) i] == plain[(std::size_t) i];
    CHECK (differs);
    CHECK (identical);
    CHECK (fx->latencySamples() == latency); // no latency of its own
}

TEST (lynx_tune_round190bb_breath_follows_the_formants_and_the_control)
{
    namespace t = lynx::lynxtune;
    const double fs = 44100.0;
    const int n = (int) (fs * 1.6), from = 30000, len = 32768;
    struct V { double f1, f2, f3; };
    const V open { 700.0, 1220.0, 2600.0 }, close { 300.0, 2300.0, 3000.0 }; // an "a"-like and an "i"-like vowel
    double tiltOf[2] {};
    int k = 0;
    for (const auto& v : { open, close })
    {
        const auto x = r95::vowel (fs, n, [] (double) { return 110.0; }, v.f1, v.f2, v.f3); // A2, on its note
        auto p = r95::tuneDefaults();
        int latency = 0;
        const auto dry = r95::runTune (x, fs, p, latency);
        p[t::breathiness] = 1.0f;
        const auto wet = r95::runTune (x, fs, p, latency);
        const auto noise = tb190::minus (wet, dry);
        const auto pv = tb190::spectrum (dry, from, len), pn = tb190::spectrum (noise, from, len);
        // the breath at each formant: the voice's own level there, times the tilt and the full gain, within 4 dB
        // (third-octave bands around the formant)
        for (const double f : { v.f1, v.f2, v.f3 })
        {
            const auto lo = f * std::exp2 (-1.0 / 6.0), hi = f * std::exp2 (1.0 / 6.0);
            const auto expected = std::clamp (6.0 * std::log2 (f / 2000.0), -18.0, 12.0) + 20.0 * std::log10 (t::breathFull);
            const auto got = 10.0 * std::log10 (tb190::band (pn, fs, lo, hi) / tb190::band (pv, fs, lo, hi));
            CHECK (std::abs (got - expected) < 4.0); // (measured: 0.2 … 2.6 dB under)
        }
        // the balance between the upper formants (1.8 … 3.6 kHz) and the first (250 … 900 Hz): the noise's follows the
        // voice's, plus the tilt between the two regions
        const auto voiceBalance = 10.0 * std::log10 (tb190::band (pv, fs, 1800.0, 3600.0) / tb190::band (pv, fs, 250.0, 900.0));
        const auto noiseBalance = 10.0 * std::log10 (tb190::band (pn, fs, 1800.0, 3600.0) / tb190::band (pn, fs, 250.0, 900.0));
        tiltOf[k++] = noiseBalance - voiceBalance;
        CHECK (noiseBalance - voiceBalance > 8.0 && noiseBalance - voiceBalance < 24.0);

        // the control: more breath at every step, +12 dB from 50 % to 100 % (the square law), and the pitch stays
        double last = -1e9, at50 = 0.0, at100 = 0.0;
        bool rising = true;
        for (const float c : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            p[t::breathiness] = c;
            const auto w = r95::runTune (x, fs, p, latency);
            const auto level = 10.0 * std::log10 (tb190::power (tb190::minus (w, dry), from, from + len) / tb190::power (dry, from, from + len));
            rising = rising && level > last + 3.0;
            last = level;
            if (c == 0.5f) at50 = level;
            if (c == 1.0f) at100 = level;
            if (c == 0.5f)
                CHECK (std::abs (r95::cents (r95::measureHz (w, from, 4096, fs), 110.0)) < 3.0);
        }
        CHECK (rising);
        CHECK (std::abs (at100 - at50 - 12.04) < 0.5);
        CHECK (at100 > -20.0 && at100 < 3.0); // [model] full: between 20 dB under the voice and about its level
    }
    CHECK (std::abs (tiltOf[0] - tiltOf[1]) < 8.0); // the same tilt on both vowels: the envelope is the voice's
}

TEST (lynx_tune_round190bb_breath_only_on_voiced_sounds)
{
    namespace t = lynx::lynxtune;
    const double fs = 44100.0;
    // a vowel, then a consonant (noise), then silence
    auto x = r95::vowel (fs, (int) (fs * 1.2), [] (double) { return 196.0; });
    unsigned seed = 3;
    for (int i = 26000; i < 36000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        x[(std::size_t) i] = 0.1f * ((float) (seed >> 8) / 16777216.0f - 0.5f);
    }
    for (int i = 36000; i < (int) x.size(); ++i)
        x[(std::size_t) i] = 0.0f;
    auto p = r95::tuneDefaults();
    int latency = 0;
    const auto dry = r95::runTune (x, fs, p, latency);
    p[t::breathiness] = 1.0f;
    const auto wet = r95::runTune (x, fs, p, latency);
    const auto added = tb190::minus (wet, dry);
    auto rel = [&] (int a, int b) { return 10.0 * std::log10 (std::max (1e-30, tb190::power (added, a + latency, b + latency)) / std::max (1e-30, tb190::power (dry, a + latency, b + latency))); };
    CHECK (rel (10000, 24000) > -20.0);  // the vowel: breath
    CHECK (rel (30000, 35500) < -40.0);  // the consonant, 90 ms in: (almost) none — it is not doubled
    // silence: the breath dies with the voice (no noise floor left behind)
    float tail = 0.0f;
    for (int i = 36000 + latency + 13000; i < (int) x.size(); ++i)
        tail = std::max (tail, std::abs (wet[(std::size_t) i]));
    CHECK (tail < 1e-6f);
    // a signal that is never voiced (white noise) and pure silence get nothing at all
    std::vector<float> nz ((std::size_t) (fs * 0.6)), zero ((std::size_t) (fs * 0.3), 0.0f);
    for (auto& v : nz)
    {
        seed = seed * 1664525u + 1013904223u;
        v = 0.2f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
    }
    p[t::breathiness] = 0.0f;
    const auto nzDry = r95::runTune (nz, fs, p, latency);
    p[t::breathiness] = 1.0f;
    const auto nzWet = r95::runTune (nz, fs, p, latency);
    CHECK (10.0 * std::log10 (std::max (1e-30, tb190::power (tb190::minus (nzWet, nzDry), 0, (int) nz.size())) / tb190::power (nzDry, 4000, (int) nz.size())) < -60.0);
    const auto z = r95::runTune (zero, fs, p, latency);
    CHECK (std::all_of (z.begin(), z.end(), [] (float v) { return v == 0.0f; }));
}

TEST (lynx_tune_round190bb_breath_is_stable_and_mono_compatible)
{
    namespace t = lynx::lynxtune;
    // every sample rate, extreme settings with it, a huge and a non-finite input: finite output, bounded
    for (const double fs : { 8000.0, 44100.0, 96000.0, 192000.0 })
    {
        auto x = r95::vowel (fs, (int) (fs * 0.5), [] (double s) { return 110.0 + 600.0 * s; });
        for (std::size_t i = x.size() / 2; i < x.size(); ++i)
            x[i] *= 20.0f; // far over full scale
        x[x.size() / 3] = std::numeric_limits<float>::infinity();
        auto p = r95::tuneDefaults();
        p[t::breathiness] = 1.0f;
        p[t::retune] = 0.0f;
        p[t::transpose] = 1.0f;
        p[t::throatLength] = 0.0f;
        p[t::wideFormant] = 1.0f;
        p[t::formantShift] = 1.0f;
        int latency = 0;
        const auto y = r95::runTune (x, fs, p, latency, 333);
        float peak = 0.0f;
        bool finite = true;
        for (auto v : y)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::abs (v));
        }
        CHECK (finite);
        CHECK (peak < 400.0f);
    }
    // stereo: the breath is the same in both channels (a centred voice stays centred); two runs give the same samples
    const double fs = 48000.0;
    const auto x = r95::vowel (fs, (int) (fs * 0.8), [] (double) { return 246.94; });
    auto p = r95::tuneDefaults();
    auto render = [&] (float breath, std::vector<float>& l, std::vector<float>& r) {
        auto q = p;
        q[t::breathiness] = breath;
        auto fx = lynx::lynxtune::make();
        fx->prepare (fs);
        l = x;
        r = x;
        for (auto& v : r) v *= 0.5f;
        for (int i = 0; i < (int) x.size(); i += 512)
            fx->process (l.data() + i, r.data() + i, std::min (512, (int) x.size() - i), q, 120.0);
    };
    std::vector<float> l0, r0, l1, r1, l2, r2;
    render (0.0f, l0, r0);
    render (0.7f, l1, r1);
    render (0.7f, l2, r2);
    CHECK (l1 == l2 && r1 == r2);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 20000; i < x.size(); ++i)
    {
        const auto a = (double) l1[i] - l0[i], b = (double) r1[i] - r0[i];
        diff += (a - b) * (a - b);
        ref += a * a;
    }
    CHECK (ref > 0.0 && diff < 1e-8 * ref);
}
