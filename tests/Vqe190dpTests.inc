// Round 190dp: Voice Shift's measured unvoiced path, Quell's noise-floor tracker (matched engine) and the classic laws
// they leave in place for projects saved before. Laws measured black-box on the reference trials' renders
// (/mnt/e/lynx-work/vqe190dp/HANDOFF.md); old projects keep the classic sound bit for bit.

namespace vqe190dp
{
struct Case
{
    lynx::EffectKind kind;
    int lawIdx;
    int idx[4];
    float val[4];
    double sr;
    std::uint64_t hash;
    double energy, a, b;
    int saved = -1; // ≥ 0: loaded as a project saved with this many parameters (upgradeLoadedEffectParams)
};

struct Render
{
    std::uint64_t hash;
    double energy, a, b;
};

// 2.5 s in blocks of 256: a 196 Hz pulse-like voice (0 … 0.9 s), noise (0.9 … 1.5 s), a 330 Hz voice gliding up with
// noise under it (1.5 … 2.2 s), silence. Left = the signal, right = 0.8 × it.
inline Render render (const lynx::EffectSlot& slot, double sr)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) (2.5 * sr), block = 256;
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    unsigned s = 12345u;
    auto noise = [&s] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; };
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        float v = 0.0f;
        if (t < 0.9 || (t >= 1.5 && t < 2.2))
        {
            const double f = t < 0.9 ? 196.0 : 330.0 * std::exp2 ((t - 1.5) * 0.5);
            ph += f / sr;
            ph -= std::floor (ph);
            for (int h = 1; h <= 12; ++h)
                v += (float) (0.25 / h * std::sin (2.0 * 3.141592653589793 * h * ph));
            if (t >= 1.5)
                v += 0.02f * noise();
        }
        else if (t < 1.5)
            v = 0.1f * noise();
        l[(std::size_t) i] = v;
        r[(std::size_t) i] = 0.8f * v;
    }
    for (int off = 0; off < n; off += block)
        fx->process (l.data() + off, r.data() + off, std::min (block, n - off), slot.params, 120.0);
    Render out { 1469598103934665603ull, 0.0, l[(std::size_t) (n / 3)], r[(std::size_t) (n * 2 / 3)] };
    for (int i = 0; i < n; ++i)
        for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
        {
            std::uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int b = 0; b < 4; ++b)
            {
                out.hash ^= (u >> (8 * b)) & 0xffu;
                out.hash *= 1099511628211ull;
            }
            out.energy += (double) v * v;
        }
    return out;
}

inline double centroidHz (const std::vector<float>& x, double sr)
{
    // the spectral centroid of the whole signal through 2048-point DFT frames (Hann), summed
    constexpr int n = 2048;
    std::vector<double> mag (n / 2, 0.0);
    for (std::size_t off = 0; off + n <= x.size(); off += n)
        for (int k = 1; k < n / 2; k += 4)
        {
            double re = 0.0, im = 0.0;
            for (int j = 0; j < n; ++j)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * j / n);
                const double a = 2.0 * 3.141592653589793 * k * j / n;
                re += w * x[off + (std::size_t) j] * std::cos (a);
                im -= w * x[off + (std::size_t) j] * std::sin (a);
            }
            mag[(std::size_t) k] += re * re + im * im;
        }
    double num = 0.0, den = 0.0;
    for (int k = 1; k < n / 2; k += 4)
    {
        num += k * sr / n * mag[(std::size_t) k];
        den += mag[(std::size_t) k];
    }
    return den > 0.0 ? num / den : 0.0;
}
} // namespace vqe190dp

TEST (vqe190dp_law_parameters_are_appended_and_old_projects_keep_the_classic_laws)
{
    using namespace lynx;
    const auto& vs = effectInfo (EffectKind::voiceShift).params;
    CHECK (vs.size() == (std::size_t) voiceshift::count && voiceshift::law == 7);
    CHECK (std::string (vs[(std::size_t) voiceshift::law].name) == "Law" && vs[(std::size_t) voiceshift::law].defaultValue == 1.0f);
    const auto& q = effectInfo (EffectKind::quellDenoiser).params;
    CHECK (q.size() == (std::size_t) voicetools::quell::paramCount && voicetools::quell::law == 4);
    CHECK (std::string (q[(std::size_t) voicetools::quell::law].name) == "Law" && q[(std::size_t) voicetools::quell::law].defaultValue == 1.0f);
    // a new instance gets the measured laws, a slot saved with fewer parameters the classic ones
    auto fresh = makeEffectSlot (EffectKind::voiceShift);
    upgradeLoadedEffectParams (fresh, (std::size_t) voiceshift::count);
    CHECK (fresh.params[(std::size_t) voiceshift::law] == 1.0f);
    auto old = makeEffectSlot (EffectKind::voiceShift);
    upgradeLoadedEffectParams (old, (std::size_t) voiceshift::midi + 1);
    CHECK (old.params[(std::size_t) voiceshift::law] == 0.0f);
    auto qFresh = makeEffectSlot (EffectKind::quellDenoiser);
    upgradeLoadedEffectParams (qFresh, (std::size_t) voicetools::quell::paramCount);
    CHECK (qFresh.params[(std::size_t) voicetools::quell::law] == 1.0f);
    auto qOld = makeEffectSlot (EffectKind::quellDenoiser);
    upgradeLoadedEffectParams (qOld, (std::size_t) voicetools::quell::matched + 1);
    CHECK (qOld.params[(std::size_t) voicetools::quell::law] == 0.0f);
    CHECK (voiceshift::paramText (voiceshift::law, 1.0f) == "Measured" && voiceshift::paramText (voiceshift::law, 0.0f) == "Classic");
    CHECK (voicetools::paramText (EffectKind::quellDenoiser, voicetools::quell::law, 0.0f) == "Classic");
    // the panels show the law switch (only while a project's classic law is set) and place every parameter
    for (const auto kind : { EffectKind::voiceShift, EffectKind::quellDenoiser })
    {
        const auto& l = plugui::layoutFor (kind);
        bool lawPlaced = false;
        for (const auto& item : l.items)
        {
            CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) l.width + 0.5f && item.y + item.h <= (float) l.height + 0.5f);
            if (item.param != nullptr && std::string (item.param) == "Law")
                lawPlaced = true;
        }
        CHECK (lawPlaced);
    }
}

TEST (vqe190dp_classic_laws_render_as_the_base_commit_did)
{
    using namespace lynx;
    // Voice Shift / Quell with the classic law, and LYNX Tune as a new instance (default and two settings) and as a
    // project saved before: Tune shares the TD-PSOLA shifter, whose new unvoiced path only Voice Shift asks for, so Tune
    // has no law switch and sounds as before everywhere. FNV-1a of the float bits, the energy and two samples, from the
    // base commit 77026c3a2's Psola users (golden: /mnt/e/lynx-work/vqe190dp/golden, mkt.sh)
    const vqe190dp::Case cases[] = {
        { EffectKind::voiceShift, 7, { -1, -1, -1, -1 }, {  }, 48000.0, 0x1314dc020b9b2a48ull, 3397.19714, -0.135365844, 0.227989152 }, // vs default
        { EffectKind::voiceShift, 7, { 0, -1, -1, -1 }, { 17.0f / 24.0f }, 48000.0, 0xc07450ca160f6651ull, 3334.94096, -0.224264145, -0.134423494 }, // vs pitch +5
        { EffectKind::voiceShift, 7, { 1, -1, -1, -1 }, { 9.0f / 24.0f }, 44100.0, 0x2b3e978543b721d7ull, 3582.29768, 0.0358026773, 0.0153768063 }, // vs formant -3
        { EffectKind::voiceShift, 7, { 0, 1, 4, -1 }, { 5.0f / 24.0f, 14.0f / 24.0f, 0.3f }, 48000.0, 0x59e86b35a6b53993ull, 4544.57305, -0.123456597, -0.0301668346 }, // vs -7/+2/drive 3
        { EffectKind::voiceShift, 7, { 0, 2, 3, -1 }, { 17.0f / 24.0f, 1.0f, 0.5f }, 48000.0, 0x60e88d5e50335381ull, 3371.41526, 0.213404402, 0.0182051361 }, // vs link +5, quantize
        { EffectKind::quellDenoiser, 4, { -1, -1, -1, -1 }, {  }, 48000.0, 0x80ef0f891ad75b06ull, 6079.43437, 0.124753676, 0.0408606194 }, // quell 0.3 classic engine
        { EffectKind::quellDenoiser, 4, { 0, 3, -1, -1 }, { 0.2f, 1.0f }, 48000.0, 0x9d301b8189960364ull, 6213.24266, 0.00172530219, 0.363674372 }, // quell matched 0.2
        { EffectKind::quellDenoiser, 4, { 0, 3, -1, -1 }, { 0.5f, 1.0f }, 44100.0, 0x7d50e4b19ba42b7eull, 5686.8071, 0.00172298297, 0.381973952 }, // quell matched 0.5
        { EffectKind::quellDenoiser, 4, { 0, 3, -1, -1 }, { 1.0f, 1.0f }, 48000.0, 0xafde312c82eb4ba1ull, 6102.93156, 0.00323212706, 0.350229144 }, // quell matched 1.0
        { EffectKind::lynxTune, -1, { -1, -1, -1, -1 }, {  }, 48000.0, 0xe135bcf9fa391efeull, 6355.39939, 0.0627515689, 0.0538308322 }, // tune default
        { EffectKind::lynxTune, -1, { -1, -1, -1, -1 }, {  }, 48000.0, 0xe135bcf9fa391efeull, 6355.39939, 0.0627515689, 0.0538308322, 6 }, // tune old project
        { EffectKind::lynxTune, -1, { 6, 5, 2, 4 }, { 17.0f / 24.0f, 9.0f / 14.0f, 0.3f, 1.0f }, 44100.0, 0x9ae869baf7a05c46ull, 4777.8601, 0.223027334, -0.273250163, -1 }, // tune +5 formant +2 retune
        { EffectKind::lynxTune, -1, { 9, 0, -1, -1 }, { 1.0f, 2.0f / 11.0f }, 48000.0, 0xd1f9d54494ba43a0ull, 6390.72495, -0.0923300385, -0.0844917744, -1 }, // tune low latency
    };
    int sameBits = 0, n = 0;
    for (const auto& c : cases)
    {
        auto slot = makeEffectSlot (c.kind);
        if (c.saved >= 0)
            upgradeLoadedEffectParams (slot, (std::size_t) c.saved);
        if (c.lawIdx >= 0)
            slot.params[(std::size_t) c.lawIdx] = 0.0f;
        for (int k = 0; k < 4; ++k)
            if (c.idx[k] >= 0)
                slot.params[(std::size_t) c.idx[k]] = c.val[k];
        const auto r = vqe190dp::render (slot, c.sr);
        // the same sound with any maths library: energy within 0.01 %, two samples within 2e-6
        CHECK (std::abs (r.energy - c.energy) <= 1e-4 * c.energy);
        CHECK (std::abs (r.a - c.a) < 2e-6 && std::abs (r.b - c.b) < 2e-6);
        sameBits += r.hash == c.hash ? 1 : 0;
        ++n;
    }
    // bit for bit where the maths library is the one the goldens were made with (glibc 2.43 on x86-64: the local builds);
    // other libm versions may round some transcendental calls differently (see Reverb190bzTests.inc)
    std::printf ("  classic laws: %d of %d renders hash-identical to the base commit\n", sameBits, n);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == n);
   #endif
}

TEST (vqe190dp_voice_shift_unvoiced_path_follows_the_formants)
{
    using namespace lynx;
    // noise (nothing to detect) through Formant −3: the measured law moves it down with the formants (the
    // reference's sweep and noise above its detector come out at the Formant shift), the classic law passes it
    const double sr = 48000.0;
    const int n = (int) (1.5 * sr);
    // band-limited noise (four one-pole low-passes at 3 kHz, 0.2 RMS): its spectrum stays clear of the Nyquist
    // frequency when it is moved up, so the centroid shows the shift itself
    std::vector<float> in ((std::size_t) n);
    unsigned s = 99u;
    double z[4] {}, power = 0.0;
    const double a = 1.0 - std::exp (-2.0 * 3.141592653589793 * 3000.0 / sr);
    for (auto& v : in)
    {
        s = s * 1664525u + 1013904223u;
        double x = (double) (s >> 8) / 8388608.0 - 1.0;
        for (auto& q : z)
            x = q += a * (x - q);
        v = (float) x;
        power += x * x;
    }
    for (auto& v : in)
        v = (float) (v * 0.2 / std::sqrt (power / n));
    auto run = [&] (float law, float formant, float link, float pitch) {
        auto slot = makeEffectSlot (EffectKind::voiceShift);
        slot.params[(std::size_t) voiceshift::law] = law;
        slot.params[(std::size_t) voiceshift::formant] = formant;
        slot.params[(std::size_t) voiceshift::link] = link;
        slot.params[(std::size_t) voiceshift::pitch] = pitch;
        auto fx = makeEffect (slot);
        fx->prepare (sr);
        auto l = in, r = in;
        for (int off = 0; off < n; off += 256)
            fx->process (l.data() + off, r.data() + off, std::min (256, n - off), slot.params, 120.0);
        bool finite = true;
        for (const auto v : l)
            finite = finite && std::isfinite (v);
        CHECK (finite);
        l.erase (l.begin(), l.begin() + 4096); // past the latency
        return l;
    };
    const auto c0 = vqe190dp::centroidHz (std::vector<float> (in.begin() + 4096, in.end()), sr);
    const auto classic = vqe190dp::centroidHz (run (0.0f, 9.0f / 24.0f, 0.0f, 0.5f), sr);
    const auto measured = vqe190dp::centroidHz (run (1.0f, 9.0f / 24.0f, 0.0f, 0.5f), sr);
    const auto up = vqe190dp::centroidHz (run (1.0f, 15.0f / 24.0f, 0.0f, 0.5f), sr);
    const auto linked = vqe190dp::centroidHz (run (1.0f, 0.5f, 1.0f, 17.0f / 24.0f), sr); // Link: + 5 st with the pitch
    const auto plain = vqe190dp::centroidHz (run (1.0f, 0.5f, 0.0f, 17.0f / 24.0f), sr);  // no link: unvoiced stays
    const auto st = [c0] (double c) { return 12.0 * std::log2 (c / c0); };
    std::printf ("  centroid shift (st): formant -3 classic %+.2f / measured %+.2f, formant +3 %+.2f, link +5 %+.2f, pitch +5 %+.2f\n",
                 st (classic), st (measured), st (up), st (linked), st (plain));
    CHECK (std::abs (st (classic)) < 0.5);
    CHECK (st (measured) < -2.0 && st (measured) > -4.0);
    CHECK (st (up) > 1.5);
    CHECK (st (linked) > 3.5);
    CHECK (std::abs (st (plain)) < 0.5);
}

TEST (vqe190dp_quell_tracker_takes_a_steady_programme_down_at_high_settings_only)
{
    using namespace lynx;
    // a steady mix-like signal (pink-ish noise plus a chord) at −20 dBFS for 6 s: at 100 % the tracked floor sits near
    // the programme and takes it down (the reference: a dense mix 10 … 24 dB at 100 %), at 35 % it leaves it alone;
    // the classic law is the fixed one. Silence stays silent, nothing is NaN.
    const double sr = 48000.0;
    const int n = (int) (6.0 * sr);
    std::vector<float> in ((std::size_t) n);
    unsigned s = 4242u;
    double b0 = 0.0, b1 = 0.0, b2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        s = s * 1664525u + 1013904223u;
        const double w = (double) (s >> 8) / 8388608.0 - 1.0;
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        const double t = i / sr;
        in[(std::size_t) i] = (float) (0.03 * (b0 + b1 + b2 + 0.1848 * w)
                                       + 0.03 * (std::sin (2 * 3.14159265 * 220 * t) + std::sin (2 * 3.14159265 * 277 * t) + std::sin (2 * 3.14159265 * 330 * t)));
    }
    auto tailDb = [&] (float amount, float law) {
        auto slot = makeEffectSlot (EffectKind::quellDenoiser);
        slot.params[(std::size_t) voicetools::quell::amount] = amount;
        slot.params[(std::size_t) voicetools::quell::matched] = 1.0f;
        slot.params[(std::size_t) voicetools::quell::law] = law;
        auto fx = makeEffect (slot);
        fx->prepare (sr);
        auto l = in, r = in;
        for (int off = 0; off < n; off += 512)
            fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
        double eo = 0.0, ei = 0.0;
        bool finite = true;
        for (int i = n - (int) sr; i < n; ++i)
        {
            eo += (double) l[(std::size_t) i] * l[(std::size_t) i];
            ei += (double) in[(std::size_t) i] * in[(std::size_t) i];
            finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
        }
        CHECK (finite);
        return 10.0 * std::log10 ((eo + 1e-30) / ei);
    };
    const auto high = tailDb (1.0f, 1.0f), highClassic = tailDb (1.0f, 0.0f), low = tailDb (0.35f, 1.0f), lowClassic = tailDb (0.35f, 0.0f);
    std::printf ("  last second: 100 %% %.1f dB (classic %.1f), 35 %% %.1f dB (classic %.1f)\n", high, highClassic, low, lowClassic);
    CHECK (high < highClassic - 6.0);
    CHECK (std::abs (low - lowClassic) < 1.0);
    // silence in, silence out (the floor of digital silence does not open a gain above unity)
    auto slot = makeEffectSlot (EffectKind::quellDenoiser);
    slot.params[(std::size_t) voicetools::quell::amount] = 1.0f;
    slot.params[(std::size_t) voicetools::quell::matched] = 1.0f;
    auto fx = makeEffect (slot);
    fx->prepare (sr);
    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    for (int off = 0; off < 48000; off += 480)
        fx->process (l.data() + off, r.data() + off, 480, slot.params, 120.0);
    float peak = 0.0f;
    for (const auto v : l)
        peak = std::max (peak, std::abs (v));
    CHECK (peak == 0.0f);
}
