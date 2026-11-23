// Round 190kr (0.1.30 performance, "beat FL on CPU"): Signalsmith Stretch's band state as structure-of-arrays, its
// spectral loops and FFT passes four values at a time (SSE2), the main prediction's band-to-band chain doing only the
// work on the chain, a mono source's second analysis copied, the output read in runs. Every value is computed by the
// same IEEE operations in the same order, so the real-time pitch shifter of a sampler that keeps its pitch (round 162,
// VoicePitchShifter) and the offline stretch (timeStretch: Signalsmith's exact()) must render exactly what they rendered
// before: the hashes below were made with the unmodified library (the base commit). Cases: both analysis blocks
// (100 / 80 ms), mono and stereo sources, transpositions from -12 to +24 semitones (beyond +12 the stretcher randomises
// each band's time step: the random path), notes re-primed, silent gaps (the stretcher's own silence path), 44.1 and
// 48 kHz.

namespace perf190kr
{
inline void hashFloats (std::uint64_t& h, const float* p, int n)
{
    for (int i = 0; i < n; ++i)
    {
        std::uint32_t u;
        std::memcpy (&u, p + i, 4);
        h = (h ^ u) * 1099511628211ull;
    }
}

// a voice-like source: phrases of a gliding tone with harmonics and noise, separated by exact silence
inline void makeSource (std::vector<float>& l, std::vector<float>& r, int frames, double rate, bool mono)
{
    l.assign ((std::size_t) frames, 0.0f);
    r.assign ((std::size_t) frames, 0.0f);
    std::uint32_t x = 12345u;
    double ph = 0.0, ph2 = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / rate;
        const double phrase = std::fmod (t, 1.1);
        const float env = phrase < 0.8 ? (float) std::sin (3.14159265358979 * phrase / 0.8) : 0.0f;
        const double f0 = 190.0 + 50.0 * std::sin (t * 1.7);
        ph += 2.0 * 3.14159265358979 * f0 / rate;
        ph2 += 2.0 * 3.14159265358979 * f0 * 1.004 / rate;
        x = x * 1664525u + 1013904223u;
        const float noise = ((float) ((x >> 8) & 0xffff) / 32768.0f - 1.0f) * 0.04f;
        float a = 0.0f, b = 0.0f;
        for (int k = 1; k <= 5; ++k)
        {
            a += (float) (std::sin (ph * k) / k);
            b += (float) (std::sin (ph2 * k + 0.4) / k);
        }
        l[(std::size_t) i] = env * (0.3f * a + noise);
        r[(std::size_t) i] = mono ? l[(std::size_t) i] : env * (0.3f * b - noise);
    }
}

// a voice's shifter as the sampler drives it: primed with primeLength (factor) frames of the transposed source, then
// 128 output frames per call with n / factor input frames owed; a new note every 1.5 s (re-primed)
inline std::uint64_t shifterRender (double rate, lynx::StretchMethod method, bool mono, double factor, double& energy)
{
    std::vector<float> l, r;
    const int seconds = 4;
    makeSource (l, r, (int) (rate * seconds * 5) + 400000, rate, mono);
    lynx::VoicePitchShifter shifter;
    shifter.allocate (rate, method);
    std::uint64_t h = 1469598103934665603ull;
    energy = 0.0;
    std::vector<float> outL (128), outR (128);
    std::size_t pos = 0;
    const int total = (int) (rate * seconds);
    for (int done = 0, note = 0; done < total; ++note)
    {
        const auto f = factor * (note % 2 == 1 ? 1.0594630943592953 : 1.0);
        const int prime = shifter.primeLength (f);
        shifter.start (l.data() + pos, r.data() + pos, prime);
        pos += (std::size_t) prime;
        double owed = 0.0;
        for (int o = 0; o < (int) (1.5 * rate) && done < total; o += 128, done += 128)
        {
            owed += 128.0 / f;
            const int n = std::max (0, (int) owed);
            owed -= n;
            shifter.process (l.data() + pos, r.data() + pos, n, outL.data(), outR.data(), 128);
            pos += (std::size_t) n;
            hashFloats (h, outL.data(), 128);
            hashFloats (h, outR.data(), 128);
            for (int i = 0; i < 128; ++i)
                energy += (double) outL[(std::size_t) i] * outL[(std::size_t) i] + (double) outR[(std::size_t) i] * outR[(std::size_t) i];
        }
    }
    return h;
}

// the offline stretch (Signalsmith's exact(): long process() calls, flush)
inline std::uint64_t offlineRender (bool mono, double ratio, double& energy)
{
    lynx::SampleData in;
    in.sampleRate = 44100.0;
    makeSource (in.left, in.right, 44100 * 3 + 321, 44100.0, false);
    if (mono)
        in.right.clear();
    const auto out = lynx::timeStretch (in, ratio);
    std::uint64_t h = 1469598103934665603ull;
    energy = 0.0;
    hashFloats (h, out->left.data(), (int) out->left.size());
    hashFloats (h, out->right.data(), (int) out->right.size());
    for (const auto v : out->left)
        energy += (double) v * v;
    return h;
}
} // namespace perf190kr

TEST (perf190kr_pitch_shifter_renders_as_the_base_commit_did)
{
    using lynx::StretchMethod;
    struct Case
    {
        double rate;
        StretchMethod method;
        bool mono;
        double factor;
        std::uint64_t hash;
        std::uint64_t clangHash = 0; // the second known-good set (0 = the same as GCC's), see below
    };
    // (factor = the voice's transposition: 0.5 = -12 st, 2.3784 = +15 st)
    const Case cases[] = {
        { 44100.0, StretchMethod::general, true, 0.5, 0xb4f59def5ba47ee2ull },
        { 44100.0, StretchMethod::general, false, 0.9438743126816935, 0x3cf31293dd5a2c4full },
        { 44100.0, StretchMethod::general, true, 1.4983070768766815, 0x74f9fa3d1f717942ull },
        { 44100.0, StretchMethod::general, false, 2.378414230005442, 0x384b3e2758c99e3bull, 0xab92afb1ef35fb2full },
        { 44100.0, StretchMethod::general, true, 4.0, 0x991803e03af60bfaull, 0x0022cd25eabb6e26ull },
        { 48000.0, StretchMethod::transients, false, 0.7491535384383408, 0x06ef0db166d5a700ull },
        { 48000.0, StretchMethod::transients, true, 1.122462048309373, 0x493fe619f4a95f7bull },
        { 48000.0, StretchMethod::transients, false, 3.174802103936399, 0xdc4da78068703dbdull, 0xc705be2727a0cfebull },
    };
    int same = 0, sameClang = 0, total = 0;
    for (const auto& c : cases)
    {
        double energy = 0.0;
        const auto h = perf190kr::shifterRender (c.rate, c.method, c.mono, c.factor, energy);
        std::printf ("  shifter %.0f Hz, %s, %s, factor %.4f: hash 0x%016llxull, energy %.3f\n", c.rate,
                     c.method == StretchMethod::general ? "general" : "transients", c.mono ? "mono" : "stereo", c.factor,
                     (unsigned long long) h, energy);
        CHECK (energy > 1.0 && std::isfinite (energy)); // it sounds (any maths library)
        ++total;
        same += h == c.hash ? 1 : 0;
        sameClang += h == (c.clangHash != 0 ? c.clangHash : c.hash) ? 1 : 0;
    }
    const struct
    {
        bool mono;
        double ratio;
        std::uint64_t hash;
        std::uint64_t clangHash = 0;
    } offline[] = {
        { false, 1.37, 0x9c0f10dccb027459ull },
        { true, 0.6, 0x41691f3968f9fca7ull },
        { false, 2.6, 0x3305e574d70141dbull, 0x28249fb87dddd3b8ull },
    };
    for (const auto& c : offline)
    {
        double energy = 0.0;
        const auto h = perf190kr::offlineRender (c.mono, c.ratio, energy);
        std::printf ("  timeStretch %s ratio %.2f: hash 0x%016llxull, energy %.3f\n", c.mono ? "mono" : "stereo", c.ratio,
                     (unsigned long long) h, energy);
        CHECK (energy > 1.0 && std::isfinite (energy));
        ++total;
        same += h == c.hash ? 1 : 0;
        sameClang += h == (c.clangHash != 0 ? c.clangHash : c.hash) ? 1 : 0;
    }
    // bit for bit where the maths library is the one the hashes were made with (glibc 2.43 on x86-64, as round 190bz):
    // the window, the twiddles and the rotations come from libm's sin / cos / exp
    // Two known-good sets (round 190qd, the 0.1.31 release): GCC's (the base commit's, CMake g++ builds) and zig's clang
    // (build-local.sh / the release train's LTO build), which computes four of these paths differently against the same
    // glibc 2.43 libm (main 0.1.30 built that way gives the same four). Every render must match ONE set as a whole.
    std::printf ("  Signalsmith Stretch: %d of %d renders hash-identical to the base commit (GCC set), %d of %d to the zig / clang set\n",
                 same, total, sameClang, total);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43)
    CHECK (same == total || sameClang == total);
   #endif
}
