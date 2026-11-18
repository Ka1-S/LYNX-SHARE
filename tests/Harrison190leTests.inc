// Round 190le: Ridge Strip's "32C" voicing — Ridge's own engine with the EQ measured on the Harrison 32Classic trial
// (tp172c H32_eq impulse grid; BLACKBOX_rodada190lb.md §4, /mnt/e/lynx-work/harrison190le): its filters (matched
// Butterworth 12 dB/oct, 25 Hz … 3.15 kHz / 20 kHz … 160 Hz), first-order shelves, matched bells at the dial with
// Q(|gain|). A hidden parameter (saved only while on) with its own switch on the panel; the Harrison 32C translation
// turns it on, every other Ridge Strip keeps the old sound.
#include <cmath>
#include <complex>

namespace h190le
{
// analog references of the measured laws (dB at hz)
inline double analogBellDb (double f0, double db, double q, double hz)
{
    const auto A = std::pow (10.0, db / 40.0);
    const std::complex<double> s (0.0, hz / f0);
    return 20.0 * std::log10 (std::abs ((s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0)));
}
inline double analogShelfDb (bool low, double centre, double db, double hz)
{
    const auto g = std::pow (10.0, db / 20.0), sg = std::sqrt (g);
    const std::complex<double> s (0.0, hz);
    const auto H = low ? (s + centre * sg) / (s + centre / sg) : g * (s + centre / sg) / (s + centre * sg);
    return 20.0 * std::log10 (std::abs (H));
}
// a Ridge Strip in the 32C voicing, EQ flat, filters out, dynamics off
inline lynx::EffectParams flat32()
{
    namespace r = lynx::mixsuite::ridge;
    auto p = lynx::makeEffectSlot (lynx::EffectKind::ridgeStrip).params;
    p[r::h32] = 1.0f;
    p[r::hpf] = p[r::lpf] = 0.0f;
    p[r::dynOn] = 0.0f;
    for (const int g : { (int) r::lfGain, (int) r::lmfGain, (int) r::hmfGain, (int) r::hfGain })
        p[(std::size_t) g] = 0.5f;
    return p;
}
inline lynx::EffectSlot c32 (const std::string& params)
{
    using tp172c::p32;
    const std::string base = p32 ("bypass", 0) + p32 ("compressor_in", 0) + p32 ("gate_in", 0) + p32 ("equalizer_in", 1)
                             + p32 ("equalizer_eq_in", 1) + p32 ("equalizer_filters_in", 1) + p32 ("input_trim", 0)
                             + p32 ("output_trim", 0) + p32 ("FaderLevel", 0);
    return tp172c::harrisonSlot ("Harrison 32Classic Channel Strip",
                                 "<SSL_PLUGIN_STATE PluginName=\"Harrison 32Classic Channel Strip\" Version=\"2.0.20\" StateASelected=\"1\"><A><PROCESSOR_STATE>"
                                     + base + params + "</PROCESSOR_STATE></A><B><PROCESSOR_STATE/></B></SSL_PLUGIN_STATE>");
}
inline std::vector<float> renderImpulse (const lynx::EffectParams& p, double sr, int n = 8192)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::ridgeStrip);
    s.params = p;
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    l[0] = r[0] = 0.5f;
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
    return l;
}
} // namespace h190le

TEST (harrison190le_32c_voicing_is_hidden_off_by_default_and_has_a_control)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    auto s = makeEffectSlot (EffectKind::ridgeStrip);
    CHECK (s.params[r::h32] == 0.0f && ! r::h32Voicing (s.params));
    CHECK ((int) r::h32 == (int) r::k4eHpLaw + 1 && (int) r::h32 > (int) r::count && (int) r::h32 < maxEffectParams);
    CHECK ((int) effectInfo (EffectKind::ridgeStrip).params.size() == r::count); // still hidden: not in the info
    CHECK (savedEffectParamCount (s) == (std::size_t) r::count);                  // off: not saved (old files unchanged)
    s.params[r::h32] = 1.0f;
    CHECK (savedEffectParamCount (s) == (std::size_t) r::h32 + 1);
    // the panel's switch reaches it by name
    CHECK (hiddenEffectParamIndex (EffectKind::ridgeStrip, "32C voicing") == (int) r::h32);
    CHECK (plugui::paramIndex (EffectKind::ridgeStrip, "32C voicing") == (int) r::h32);
    CHECK (std::string (plugui::paramName (EffectKind::ridgeStrip, r::h32)) == "32C voicing");
    CHECK (hiddenEffectParamIndex (EffectKind::ridgeStrip, "Ratio") == -1 && hiddenEffectParamIndex (EffectKind::glowLeveler, "32C voicing") == -1);
    CHECK (formatEffectParam (EffectKind::ridgeStrip, r::h32, 1.0f, 120.0, s.params) == "32C console");
    // the console voicings take precedence (the panel switches them off: linkedEdit)
    for (const int other : { (int) r::modern, (int) r::console4k, (int) r::voicing, (int) r::vocal })
    {
        auto q = s.params;
        q[(std::size_t) other] = 1.0f;
        CHECK (! r::h32Voicing (q));
    }
    const auto on32 = r::linkedEdit ([] { auto q = makeEffectSlot (EffectKind::ridgeStrip).params; q[r::console4k] = q[r::modern] = 1.0f; return q; }(), r::h32, 1.0f);
    CHECK (on32.size() == 3 && on32[0].first == r::h32 && on32[1].second == 0.0f && on32[2].second == 0.0f);
    const auto on4k = r::linkedEdit (s.params, r::console4k, 1.0f);
    CHECK (on4k.size() == 2 && on4k[1].first == r::h32 && on4k[1].second == 0.0f);
    CHECK (r::linkedEdit (s.params, r::h32, 0.0f).size() == 1 && r::linkedEdit (makeEffectSlot (EffectKind::ridgeStrip).params, r::modern, 1.0f).size() == 1);
}

TEST (harrison190le_32c_dials_read_the_originals_scales)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    // the host table's texts at ninths of the travel
    const double hp[9] { 25, 33, 85, 215, 456, 841, 1400, 2160, 3150 }, lp[9] { 160, 212, 538, 1360, 2900, 5340, 8880, 13710, 20000 };
    for (int k = 1; k < 9; ++k)
    {
        CHECK (std::abs (r::h32HpfHz ((float) k / 8.0f) - hp[k]) < 1e-3 * hp[k]);
        CHECK (std::abs (r::h32LpfHz (1.0f - (float) k / 8.0f) - lp[k]) < 1e-3 * lp[k] || k == 8);
    }
    CHECK (r::h32HpfHz (0.0f) == 0.0 && r::h32LpfHz (0.0f) == 0.0 && std::abs (r::h32LpfHz (1.0f) - 160.0) < 1e-6);
    // shelves: centres straight in the dial (25 / 145 / 1500, 400 / 2650 / 10000), plateau ±13.5 linear
    CHECK (std::abs (r::h32LfHz (0.0f, false) - 25.0) < 1e-9 && std::abs (r::h32LfHz (0.5f, false) - 145.0) < 1e-6 && std::abs (r::h32LfHz (1.0f, false) - 1500.0) < 1e-6);
    CHECK (std::abs (r::h32LfHz (0.75f, false) - 822.5) < 1e-6 && std::abs (r::h32HfHz (0.25f, false) - 1525.0) < 1e-6);
    CHECK (std::abs (r::h32HfHz (0.0f, false) - 400.0) < 1e-9 && std::abs (r::h32HfHz (0.5f, false) - 2650.0) < 1e-6 && std::abs (r::h32HfHz (1.0f, false) - 10000.0) < 1e-6);
    CHECK (std::abs (r::h32ShelfDb (1.0f) - 13.5) < 1e-9 && std::abs (r::h32ShelfDb (0.65f) - 4.05) < 1e-5 && std::abs (r::h32ShelfDb (0.15f) + 9.45) < 1e-5);
    // bells: the measured dials (0.65 / 0.85 / 1) and the table's 0.75 / 0.875, odd, monotone
    CHECK (std::abs (r::h32BellDb (0.65f) - 2.4906) < 1e-3 && std::abs (r::h32BellDb (0.85f) - 6.9366) < 1e-3 && std::abs (r::h32BellDb (1.0f) - 13.7374) < 1e-3);
    CHECK (std::abs (r::h32BellDb (0.75f) - 4.5) < 1e-3 && std::abs (r::h32BellDb (0.875f) - 7.8) < 1e-3 && std::abs (r::h32BellDb (0.625f) - 2.1) < 0.06);
    CHECK (r::h32BellDb (0.5f) == 0.0 && std::abs (r::h32BellDb (0.25f) + r::h32BellDb (0.75f)) < 1e-9); // (exact floats: odd)
    for (int k = 1; k <= 200; ++k)
        CHECK (r::h32BellDb (0.5f + 0.0025f * (float) k) > r::h32BellDb (0.5f + 0.0025f * (float) (k - 1)));
    CHECK (std::abs (r::h32Q (2.4906) - 0.525) < 2e-3 && std::abs (r::h32Q (-6.9366) - 0.57) < 2e-3 && std::abs (r::h32Q (13.7374) - 0.75) < 2e-3);
    // the translation's inverses
    for (const double db : { -13.7374, -6.9366, -2.4906, -0.3, 0.0, 1.0, 2.4906, 5.0, 6.9366, 10.0, 13.7374 })
        CHECK (std::abs (r::h32BellDb (r::h32GainValue (db)) - db) < 1e-4);
    for (const double hz : { 104.0, 312.0, 609.0, 1778.5, 4366.0 })
        CHECK (std::abs (r::h32LmfHz (r::h32LmfValue (hz)) - hz) < 1e-3 * hz);
    for (const double hz : { 271.0, 711.31, 1421.74, 3544.2, 8000.0 })
        CHECK (std::abs (r::h32HmfHz (r::h32HmfValue (hz)) - hz) < 1e-3 * hz);
    for (const double hz : { 29.34, 252.97, 1152.9, 3150.0 })
        CHECK (std::abs (r::h32HpfHz (r::h32HpfValue (hz)) - hz) < 1e-3 * hz);
    for (const double hz : { 187.57, 1607.3, 7320.9, 20000.0 })
        CHECK (std::abs (r::h32LpfHz (r::h32LpfValue (hz)) - hz) < 2e-3 * hz);
    // the end bands' saved frequency is the bell's: quarters of the travel
    CHECK (std::abs (r::h32LfValue (111.355) - 0.5f) < 1e-5 && std::abs (r::h32HfValue (9021.65) - 0.75f) < 1e-5 && std::abs (r::h32LfHz (r::h32LfValue (369.313), true) - 369.313) < 1e-3);
    // read-outs follow the band's mode
    auto p = h190le::flat32();
    p[r::hfFreq] = 0.5f;
    CHECK (formatEffectParam (EffectKind::ridgeStrip, r::hfFreq, 0.5f, 120.0, p) == "2.65 kHz");
    p[r::hfBell] = 1.0f;
    CHECK (formatEffectParam (EffectKind::ridgeStrip, r::hfFreq, 0.5f, 120.0, p) == "3.62 kHz");
    CHECK (formatEffectParam (EffectKind::ridgeStrip, r::hfGain, 1.0f, 120.0, p) == "+13.7 dB");
    p[r::hfBell] = 0.0f;
    CHECK (formatEffectParam (EffectKind::ridgeStrip, r::hfGain, 1.0f, 120.0, p) == "+13.5 dB");
    CHECK (formatEffectParam (EffectKind::ridgeStrip, r::hpf, 1.0f, 120.0, p) == "3.15 kHz" && formatEffectParam (EffectKind::ridgeStrip, r::lpf, 0.0f, 120.0, p) == "off");
}

TEST (harrison190le_32c_curve_is_the_measured_law)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    using h190le::analogBellDb;
    using h190le::analogShelfDb;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // bells at the dial: the analog peaking prototype (no cramping) with Q(|gain|), every band
        for (const float gv : { 0.0f, 0.15f, 0.65f, 1.0f })
            for (const float fv : { 0.0f, 0.5f, 1.0f })
            {
                auto p = h190le::flat32();
                p[r::hmfGain] = gv;
                p[r::hmfFreq] = fv;
                const auto f0 = r::h32HmfHz (fv), g = r::h32BellDb (gv);
                double worst = 0.0;
                for (double hz = 20.0; hz <= std::min (16000.0, sr * 0.25); hz *= 1.1)
                    worst = std::max (worst, std::abs (r::responseDb (p, sr, hz) - analogBellDb (f0, g, r::h32Q (g), hz)));
                CHECK (worst < (sr > 50000.0 ? 0.1 : 0.3));
                CHECK (std::abs (r::responseDb (p, sr, f0) - g) < 0.02);
            }
        // Lo shelf: the analog first-order shelf (prewarped at its centre: exact there)
        for (const float fv : { 0.0f, 0.5f, 1.0f })
            for (const float gv : { 0.0f, 0.85f })
            {
                auto p = h190le::flat32();
                p[r::lfFreq] = fv;
                p[r::lfGain] = gv;
                const auto c = r::h32LfHz (fv, false), g = r::h32ShelfDb (gv);
                CHECK (std::abs (r::responseDb (p, sr, c) - 0.5 * g) < 0.01);
                CHECK (std::abs (r::responseDb (p, sr, 10.0) - analogShelfDb (true, c, g, 10.0)) < 0.02);
                for (double hz = 20.0; hz <= 4000.0; hz *= 1.2)
                    CHECK (std::abs (r::responseDb (p, sr, hz) - analogShelfDb (true, c, g, hz)) < 0.15);
            }
        // Hi shelf: equal to the analog one at its centre and at Nyquist, 0 dB at DC
        for (const float fv : { 0.0f, 0.5f, 0.75f, 1.0f })
            for (const float gv : { 0.0f, 0.35f, 0.65f, 1.0f })
            {
                auto p = h190le::flat32();
                p[r::hfFreq] = fv;
                p[r::hfGain] = gv;
                const auto c = r::h32HfHz (fv, false), g = r::h32ShelfDb (gv);
                CHECK (std::abs (r::responseDb (p, sr, c) - 0.5 * g) < 0.01);
                CHECK (std::abs (r::responseDb (p, sr, sr * 0.4999) - analogShelfDb (false, c, g, sr * 0.4999)) < 0.02);
                CHECK (std::abs (r::responseDb (p, sr, 5.0)) < 0.01);
            }
        // filters: Butterworth corners at the dial (-3.01 dB), 12 dB/oct, the 20 kHz low-pass uncramped
        auto p = h190le::flat32();
        p[r::hpf] = r::h32HpfValue (215.0);
        p[r::lpf] = r::h32LpfValue (1360.0);
        CHECK (std::abs (r::responseDb (p, sr, 215.0) + 3.0103) < 0.03 && std::abs (r::responseDb (p, sr, 1360.0) + 3.0103) < 0.03);
        CHECK (std::abs (r::responseDb (p, sr, 215.0 / 8.0) - r::responseDb (p, sr, 215.0 / 16.0) - 12.0) < 0.2);
        p[r::hpf] = r::h32HpfValue (25.0);
        p[r::lpf] = r::h32LpfValue (20000.0);
        if (sr == 48000.0) // the trial's default at 48 kHz: -2.40 dB at 18 kHz (measured; at 96 kHz it is nearer the analog -2.2)
            CHECK (std::abs (r::responseDb (p, sr, 18000.0) + 2.40) < 0.06);
        CHECK (std::abs (r::responseDb (p, sr, 1000.0)) < 0.01);
    }
    // the engine plays the curve the display shows: an impulse through the strip, its spectrum at a few frequencies
    auto p = h190le::flat32();
    p[r::lfGain] = 0.85f; p[r::lfFreq] = 0.3f;
    p[r::lmfGain] = 0.2f; p[r::lmfFreq] = 0.6f;
    p[r::hmfGain] = 0.9f; p[r::hmfFreq] = 0.7f;
    p[r::hfGain] = 0.3f; p[r::hfFreq] = 0.4f; p[r::hfBell] = 1.0f;
    p[r::hpf] = 0.2f; p[r::lpf] = 0.1f;
    const auto h = h190le::renderImpulse (p, 48000.0);
    for (const double hz : { 60.0, 250.0, 1000.0, 3000.0, 9000.0 })
    {
        std::complex<double> acc;
        for (std::size_t i = 0; i < h.size(); ++i)
            acc += (double) h[i] / 0.5 * std::polar (1.0, -2.0 * 3.141592653589793 * hz * (double) i / 48000.0);
        CHECK (std::abs (20.0 * std::log10 (std::abs (acc)) - r::responseDb (p, 48000.0, hz)) < 0.02);
    }
}

TEST (harrison190le_32c_is_stable_and_leaves_other_strips_alone)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    std::mt19937 rng (190);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (const double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int t = 0; t < 12; ++t)
        {
            auto p = makeEffectSlot (EffectKind::ridgeStrip).params;
            for (const int k : { (int) r::hpf, (int) r::lpf, (int) r::lfGain, (int) r::lfFreq, (int) r::lfBell, (int) r::lmfGain, (int) r::lmfFreq,
                                 (int) r::hmfGain, (int) r::hmfFreq, (int) r::hfGain, (int) r::hfFreq, (int) r::hfBell })
                p[(std::size_t) k] = t < 2 ? (float) t : u (rng);
            p[r::h32] = 1.0f;
            const auto h = h190le::renderImpulse (p, sr, (int) (sr * 0.5) / 256 * 256); // (a 19 Hz bell rings ~0.3 s)
            double tail = 0.0, peak = 0.0;
            for (std::size_t i = 0; i < h.size(); ++i)
            {
                CHECK (std::isfinite (h[i]));
                peak = std::max (peak, (double) std::abs (h[i]));
                if (i > h.size() - 1024) tail = std::max (tail, (double) std::abs (h[i]));
            }
            CHECK (peak < 4.0 && tail < 1e-4);
        }
    // the console voicings, and the own engine with the switch off, play as before: the switch changes nothing there
    for (const int voice : { -1, (int) r::console4k, (int) r::modern, (int) r::voicing })
    {
        auto a = makeEffectSlot (EffectKind::ridgeStrip).params;
        a[r::lfGain] = 0.8f; a[r::hmfGain] = 0.3f; a[r::hpf] = 0.3f; a[r::lpf] = 0.4f; a[r::compRatio] = 0.5f; a[r::compThreshold] = 0.4f;
        if (voice >= 0)
            a[(std::size_t) voice] = 1.0f;
        auto b = a;
        b[r::h32] = voice >= 0 ? 1.0f : 0.0f;
        const auto ha = h190le::renderImpulse (a, 48000.0), hb = h190le::renderImpulse (b, 48000.0);
        CHECK (std::equal (ha.begin(), ha.end(), hb.begin()));
    }
}

TEST (harrison190le_32c_state_translates_to_the_32c_voicing)
{
    using namespace lynx;
    using tp172c::p32;
    namespace r = mixsuite::ridge;
    // the grid's states: a Hi-Mid bell at 711 Hz +6.94 dB, the Lo shelf at the 4th dial (369.3 Hz saved) -13.74,
    // the high-pass at 252.97 Hz; the default filters stay in (25 Hz / 20 kHz)
    auto s = h190le::c32 (p32 ("equalizer_band_freq_2", 711.31396484375) + p32 ("equalizer_band_gain_2", 6.936617851257324)
                          + p32 ("equalizer_band_freq_4", 369.3134765625) + p32 ("equalizer_band_gain_4", -13.73739242553711)
                          + p32 ("equalizer_highpass_filter_freq", 252.9704895019531) + p32 ("equalizer_lowpass_filter_freq", 20000.0));
    auto& p = s.params;
    CHECK (s.kind == EffectKind::ridgeStrip && r::h32Voicing (p) && ! r::k4Voicing (p) && ! r::modernVoicing (p));
    CHECK (std::abs (r::h32HmfHz (p[r::hmfFreq]) - 711.314) < 0.01 && std::abs (r::h32BellDb (p[r::hmfGain]) - 6.9366) < 1e-3);
    CHECK (p[r::lfBell] < 0.5f && std::abs (r::h32LfHz (p[r::lfFreq], false) - 822.5) < 0.1 && std::abs (r::h32ShelfDb (p[r::lfGain]) + 13.5) < 1e-3);
    CHECK (std::abs (r::h32HpfHz (p[r::hpf]) - 252.97) < 0.05 && std::abs (r::h32LpfHz (p[r::lpf]) - 20000.0) < 20.0);
    // the measured centre / plateau of that shelf: half the plateau at 822.5 Hz (with the filters' small share)
    auto q = p;
    q[r::hpf] = q[r::lpf] = 0.0f;
    q[r::hmfGain] = 0.5f;
    CHECK (std::abs (r::responseDb (q, 48000.0, 822.5) + 6.75) < 0.01);
    // Bell mode: the saved frequency is the bell's own
    s = h190le::c32 (p32 ("equalizer_lo_bell", 1) + p32 ("equalizer_band_freq_4", 58.80659866333008) + p32 ("equalizer_band_gain_4", 13.73739242553711)
                     + p32 ("equalizer_hi_bell", 1) + p32 ("equalizer_band_freq_1", 9021.6455078125) + p32 ("equalizer_band_gain_1", -13.73739242553711));
    CHECK (s.params[r::lfBell] > 0.5f && std::abs (r::h32LfHz (s.params[r::lfFreq], true) - 58.8066) < 1e-3);
    CHECK (s.params[r::hfBell] > 0.5f && std::abs (r::h32HfHz (s.params[r::hfFreq], true) - 9021.65) < 1e-2 && std::abs (r::h32BellDb (s.params[r::hfGain]) + 13.7374) < 1e-3);
    // filters out / EQ out
    s = h190le::c32 (p32 ("equalizer_filters_in", 0) + p32 ("equalizer_band_gain_3", 3.0));
    CHECK (s.params[r::hpf] == 0.0f && s.params[r::lpf] == 0.0f && s.params[r::eqOn] > 0.5f);
    // the MPC strip keeps Ridge's own curve
    const std::string mpc = "<HarrisonMPCChannelStrip plugin_size=\"1.0\" version=\"327936\">" + p32 ("bypass", 0) + p32 ("mpc_equalizerin", 1)
                            + p32 ("mpceq_bandGain3", 6) + p32 ("mpceq_bandFreq3", 185) + p32 ("mpceq_bandShape3", 7) + p32 ("mpc_trim", 0)
                            + p32 ("mpc_compressorin", 0) + "</HarrisonMPCChannelStrip>";
    CHECK (tp172c::harrisonSlot ("Harrison_MPCChannelStrip", mpc).params[r::h32] == 0.0f);
}

TEST (harrison190le_32c_panel_follows_the_voicing_switches)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    const auto& l = plugui::layoutFor (EffectKind::ridgeStrip);
    auto p = makeEffectSlot (EffectKind::ridgeStrip).params;
    int toggles = 0, qShown = 0, qShown32 = 0;
    const plugui::LayoutItem* curveTabs = nullptr;
    auto q32 = p;
    q32[r::h32] = 1.0f;
    for (const auto& item : l.items)
    {
        if (item.param != nullptr && std::string (item.param) == "32C voicing")
        {
            ++toggles;
            CHECK (item.type == plugui::Item::toggle && item.label != nullptr && std::string (item.label) == "32C console");
            CHECK (plugui::isShown (EffectKind::ridgeStrip, item, 0, p, nullptr) && plugui::isShown (EffectKind::ridgeStrip, item, 0, q32, nullptr));
        }
        if (item.param != nullptr && (std::string (item.param) == "HMF Q" || std::string (item.param) == "LMF Q"))
        {
            qShown += plugui::isShown (EffectKind::ridgeStrip, item, 0, p, nullptr) ? 1 : 0;
            qShown32 += plugui::isShown (EffectKind::ridgeStrip, item, 0, q32, nullptr) ? 1 : 0;
        }
        if (item.param != nullptr && std::string (item.param) == "EQ curve" && item.options != nullptr && std::string (item.options) == "Brown|Black")
            curveTabs = &item;
    }
    CHECK (toggles == 1 && qShown == 2 && qShown32 == 0);
    CHECK (curveTabs != nullptr && ! plugui::isIdle (EffectKind::ridgeStrip, *curveTabs, p) && plugui::isIdle (EffectKind::ridgeStrip, *curveTabs, q32));
}

// --- Lustrous Plates: its plain ValueTree state -> Foil Plate (round 190ca's physical fit, Classic law) ------------
namespace h190le
{
// a JUCE ValueTree in ValueTree::writeToStream's format (compressed ints, '\0'-ended names, typed vars)
inline void putInt (std::vector<std::uint8_t>& b, long long v)
{
    const bool neg = v < 0;
    auto u = (unsigned long long) (neg ? -v : v);
    std::vector<std::uint8_t> bytes;
    while (u != 0) { bytes.push_back ((std::uint8_t) (u & 0xff)); u >>= 8; }
    b.push_back ((std::uint8_t) (bytes.size() | (neg ? 0x80 : 0)));
    b.insert (b.end(), bytes.begin(), bytes.end());
}
inline void putName (std::vector<std::uint8_t>& b, const std::string& s) { b.insert (b.end(), s.begin(), s.end()); b.push_back (0); }
inline std::vector<std::uint8_t> lustrousState (const std::map<int, double>& params)
{
    std::vector<std::uint8_t> b { 'V', 's', 't', 'W', 0, 0, 0, 8, 0, 0, 0, 1, 0, 0, 0, 0 };
    putName (b, "LP_FullState");
    putInt (b, 1);
    putName (b, "written_by_version__string");
    putInt (b, 7);
    b.push_back (5);
    for (const char c : std::string ("1.3.7")) b.push_back ((std::uint8_t) c);
    b.push_back (0);
    putInt (b, 1);
    putName (b, "StateVT");
    putInt (b, 1);
    putName (b, "preset_name");
    putInt (b, 14);
    b.push_back (5);
    for (const char c : std::string ("Chrome Plated")) b.push_back ((std::uint8_t) c);
    putInt (b, (long long) params.size());
    for (const auto& [id, value] : params)
    {
        putName (b, "PARAM");
        putInt (b, 2);
        putName (b, "id");
        const auto text = std::to_string (id);
        putInt (b, (long long) text.size() + 2);
        b.push_back (5);
        b.insert (b.end(), text.begin(), text.end());
        b.push_back (0);
        putName (b, "value");
        putInt (b, 9);
        b.push_back (4);
        std::uint8_t d[8];
        std::memcpy (d, &value, 8);
        b.insert (b.end(), d, d + 8);
        putInt (b, 0);
    }
    return b;
}
// refcap182's four settings (D, S1, S2, S3: Damper / pre-delay / model), the rest at the plate's defaults
inline std::map<int, double> lustrousSetting (double damper, double predelayMs, double model, double dryWet = 1.0, double gainDb = 0.0)
{
    std::map<int, double> m { { 0, gainDb }, { 1, dryWet }, { 2, model }, { 3, damper }, { 4, 2.5 }, { 5, 0.5 }, { 6, predelayMs }, { 7, 0 },
                              { 8, 0 }, { 9, 7.5 }, { 10, 10 }, { 11, 0.7 }, { 12, 0 }, { 13, 20 }, { 21, 18000 } };
    return m;
}
inline lynx::EffectSlot lustrousSlot (const std::map<int, double>& params)
{
    auto f = p190ca::vst3Named ("Lustrous Plates");
    f.state = substtest::flVst3State (lustrousState (params));
    return p190ca::substituted (f);
}
} // namespace h190le

TEST (harrison190le_lustrous_plates_state_translates_to_the_fitted_foil)
{
    using namespace lynx;
    auto paramOf = [] (const EffectSlot& s, const char* name) { return s.params[(std::size_t) p190ca::paramNamed (s.kind, name)]; };
    // refcap182's settings reproduce round 190ca's fitted Foil settings (maps2/Lustrous_Plates.json)
    struct Want { double damper, pre, model; float decay, lowGain, highGain, lowCut; };
    for (const auto& w : { Want { 5.0, 10.0, 0.0, 0.38241f, 0.64922f, 0.56589f, 0.17804f }, Want { 5.0, 0.0, 0.0, 0.38241f, 0.64922f, 0.56589f, 0.17804f },
                           Want { 2.5, 10.0, 0.0, 0.3876f, 0.65531f, 0.56156f, 0.25244f }, Want { 8.0, 10.0, 12.0, 0.27528f, 0.70195f, 0.66028f, 0.15324f } })
    {
        const auto s = h190le::lustrousSlot (h190le::lustrousSetting (w.damper, w.pre, w.model));
        CHECK (s.kind == EffectKind::foilPlate && s.isSubstitute() && s.mix == 1.0f);
        CHECK (std::abs (paramOf (s, "Decay") - w.decay) < 1e-4f && std::abs (paramOf (s, "Low EQ gain") - w.lowGain) < 1e-4f);
        CHECK (std::abs (paramOf (s, "High EQ gain") - w.highGain) < 1e-4f && std::abs (paramOf (s, "Low cut") - w.lowCut) < 1e-4f);
        CHECK (paramOf (s, "Mix") == 1.0f && std::abs (paramOf (s, "Type") - 0.792f) < 1e-6f && paramOf (s, "Voicing") == 0.0f);
        CHECK (std::abs (paramOf (s, "Width") - 0.36298f) < 1e-5f && std::abs (paramOf (s, "Mod depth") - 0.04363f) < 1e-6f);
        CHECK (s.params[(std::size_t) verbs::foil::engineLaw] == 0.0f && s.params[(std::size_t) verbs::foil::revision] == 0.0f);
        CHECK (paramOf (s, "Predelay") == 0.0f);
    }
    // Dry/Wet on Foil's equal-power Mix (same wet : dry ratio): Equal Mix = 0.5, "-10 / 0 dB" = 10 dB more wet
    auto s = h190le::lustrousSlot (h190le::lustrousSetting (5.0, 10.0, 0.0, 0.5));
    CHECK (std::abs (paramOf (s, "Mix") - 0.5f) < 1e-5f);
    s = h190le::lustrousSlot (h190le::lustrousSetting (5.0, 10.0, 0.0, 0.75));
    CHECK (std::abs (std::tan (paramOf (s, "Mix") * 3.14159265 / 2.0) - std::pow (10.0, 0.5)) < 1e-3);
    CHECK (paramOf (h190le::lustrousSlot (h190le::lustrousSetting (5.0, 10.0, 0.0, 0.0)), "Mix") == 0.0f);
    // Master Gain: both shelves together (+6 dB = a quarter of the low shelf's travel)
    s = h190le::lustrousSlot (h190le::lustrousSetting (5.0, 10.0, 0.0, 1.0, 6.0));
    CHECK (std::abs (paramOf (s, "Low EQ gain") - (0.64922f + 0.25f)) < 1e-4f && std::abs (paramOf (s, "High EQ gain") - (0.56589f + 0.25f)) < 1e-4f);
    // a state without the plate's tree, or a malformed one, keeps the role preset
    auto bad = h190le::lustrousState (h190le::lustrousSetting (5.0, 10.0, 0.0));
    bad.resize (bad.size() / 2);
    auto f = p190ca::vst3Named ("Lustrous Plates");
    f.state = substtest::flVst3State (bad);
    s = p190ca::substituted (f);
    CHECK (s.kind == EffectKind::foilPlate && std::abs (s.mix - 0.25f) < 1e-6f && std::abs (paramOf (s, "Decay") - 0.3f) < 1e-6f);
}
