// Round 190mt (Waves part 5, docs/analise/REA_rodada190mt.md): the SSL G-Channel EQ law on Ridge Strip, the exact Q10 bell
// law on Facet, the API-560 law (graphic-EQ network, 65 samples of latency) on Facet and the Renaissance Channel's "RVox"
// compressor type on Compressor's Renaissance law. Every one is hidden, 0 in new instances and in every older project.
#include <cmath>

#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"

namespace parity190mt
{
inline lynx::EffectSlot gSlot()
{
    using namespace lynx;
    namespace rg = mixsuite::ridge;
    auto s = makeEffectSlot (EffectKind::ridgeStrip);
    s.params[rg::voicing] = 1.0f; // the E voicing (the G law rides on it)
    return s;
}
inline void gBands (lynx::EffectSlot& s, double lf, double lfHz, double lmf, double lmfKHz, double lmfQ, double hmf, double hmfKHz,
                    double hmfQ, double hf, double hfKHz)
{
    namespace rg = lynx::mixsuite::ridge;
    namespace g = rg::gch;
    s.params[rg::gLfGain] = g::gainValue (lf); s.params[rg::gLfHz] = g::lfValue (lfHz);
    s.params[rg::gLmfGain] = g::gainValue (lmf); s.params[rg::gLmfKHz] = g::lmfValue (lmfKHz); s.params[rg::gLmfQ] = g::qValue (lmfQ);
    s.params[rg::gHmfGain] = g::gainValue (hmf); s.params[rg::gHmfKHz] = g::hmfValue (hmfKHz); s.params[rg::gHmfQ] = g::qValue (hmfQ);
    s.params[rg::gHfGain] = g::gainValue (hf); s.params[rg::gHfKHz] = g::hfValue (hfKHz);
}
// the steady gain (dB) of a slot for a sine at hz (level −20 dBFS), measured on the second half of 1 s
inline double sineGainDb (const lynx::EffectSlot& slot, double hz, double sr = 48000.0, double level = 0.1)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) sr;
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    for (int i = 0; i < n; ++i) l[(std::size_t) i] = r[(std::size_t) i] = (float) (level * std::sin (2.0 * M_PI * hz * i / sr));
    for (int o = 0; o < n; o += 256)
        fx->process (l.data() + o, r.data() + o, std::min (256, n - o), slot.params, 120.0);
    double e = 0.0;
    for (int i = n / 2; i < n; ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
    return 10.0 * std::log10 (e / (n / 2) / (0.5 * level * level));
}
} // namespace parity190mt

TEST (parity190mt_ssl_g_law_is_hidden_and_inert_while_off)
{
    using namespace lynx;
    namespace rg = mixsuite::ridge;
    CHECK (effectInfo (EffectKind::ridgeStrip).params.size() == (std::size_t) rg::count);
    CHECK (rg::gEqLaw == rg::h32 + 1 && rg::gHmfGain < rg::omniLaw && std::size (rg::gParams) == 13); // (rollup step 13: 93 … 99, 108 / 109, 136 … 139)
    auto s = parity190mt::gSlot();
    for (const int k : rg::gParams) CHECK (s.params[(std::size_t) k] == 0.0f);
    CHECK (savedEffectParamCount (s) <= (std::size_t) rg::count + 2);
    // the G band values alone (law off) change nothing: the E bands play
    s.params[rg::lfGain] = 0.7f; s.params[rg::hmfGain] = 0.3f;
    const double before = rg::responseDb (s.params, 48000.0, 100.0), before2 = rg::responseDb (s.params, 48000.0, 3500.0);
    parity190mt::gBands (s, 12.0, 120.0, 0.0, 1.0, 1.5, 15.0, 3.0, 1.5, 0.0, 8.0);
    CHECK (rg::responseDb (s.params, 48000.0, 100.0) == before && rg::responseDb (s.params, 48000.0, 3500.0) == before2);
    CHECK (savedEffectParamCount (s) == (std::size_t) rg::gHfKHz + 1);
    s.params[rg::gEqLaw] = 1.0f;
    CHECK (rg::responseDb (s.params, 48000.0, 100.0) != before);
    // the stored scales read back
    CHECK (std::abs (rg::gch::gainDb (rg::gch::gainValue (-17.0)) + 17.0) < 1e-4 && std::abs (rg::gch::lfHz (rg::gch::lfValue (80.0)) - 80.0) < 1e-3);
    CHECK (std::abs (rg::gch::q (rg::gch::qValue (2.5)) - 2.5) < 1e-5 && std::abs (rg::gch::hmfKHz (rg::gch::hmfValue (5.0)) - 5.0) < 1e-5);
}

TEST (parity190mt_ssl_g_law_follows_the_measured_sections)
{
    using namespace lynx;
    namespace rg = mixsuite::ridge;
    auto s = parity190mt::gSlot();
    s.params[rg::gEqLaw] = 1.0f;
    // LF +12 dB at 120 Hz: [EXEC] the probe's section reaches +14.1 dB at DC (its gain law overshoots) and returns to 0
    // above its shared upper corner (−0.2 dB at 10 kHz, within 0.1 dB at 20 kHz)
    parity190mt::gBands (s, 12.0, 120.0, 0.0, 1.0, 1.5, 0.0, 3.0, 1.5, 0.0, 8.0);
    CHECK (std::abs (rg::responseDb (s.params, 48000.0, 10.0) - 14.08) < 0.3);
    CHECK (std::abs (rg::responseDb (s.params, 48000.0, 10000.0)) < 0.35 && std::abs (rg::responseDb (s.params, 48000.0, 20000.0)) < 0.1);
    // the other rates keep the shelf where it was
    CHECK (std::abs (rg::responseDb (s.params, 96000.0, 10.0) - rg::responseDb (s.params, 48000.0, 10.0)) < 0.3);
    CHECK (std::abs (rg::responseDb (s.params, 44100.0, 120.0) - rg::responseDb (s.params, 48000.0, 120.0)) < 0.3);
    // HMF +20 dB at 1.5 kHz, Q 1.5: a peaking section, its peak a little above the dial (proportional Q: +21 … +22 dB)
    parity190mt::gBands (s, 0.0, 120.0, 0.0, 1.0, 1.5, 20.0, 1.5, 1.5, 0.0, 8.0);
    double peak = -100.0, at = 0.0;
    for (double f = 800.0; f < 3000.0; f *= 1.01)
        if (const double v = rg::responseDb (s.params, 48000.0, f); v > peak) { peak = v; at = f; }
    CHECK (peak > 20.5 && peak < 22.5 && at > 1300.0 && at < 1800.0);
    CHECK (std::abs (rg::responseDb (s.params, 48000.0, 30.0)) < 0.2);
    // gain 0 everywhere = flat (no section runs)
    parity190mt::gBands (s, 0.0, 120.0, 0.0, 1.0, 1.5, 0.0, 3.0, 1.5, 0.0, 8.0);
    CHECK (std::abs (rg::responseDb (s.params, 48000.0, 1000.0)) < 1e-9);
}

TEST (parity190mt_ssl_g_translates_onto_the_g_law)
{
    using namespace lynx;
    namespace rg = mixsuite::ridge;
    int n = 0;
    // SSL G-Channel: LF +5 dB at 80 Hz, HMF +3 dB at 5 kHz Q 1.5, HF +17 dB (beyond the E's ±15) at 10 kHz, HMF ×3
    auto g = parity190jh::waves ("SSLGChannel Stereo", "SSLGChannel", parity190jh::list (57, { { 1, "1" }, { 3, "0.4" }, { 5, "-30" }, { 9, "0.2" },
        { 14, "5" }, { 15, "80" }, { 17, "0.5" }, { 18, "2.5" }, { 19, "3" }, { 20, "5" }, { 21, "1.5" }, { 23, "17" }, { 24, "10" }, { 28, "30" }, { 56, "1" } }), n);
    CHECK (g.kind == EffectKind::ridgeStrip && g.params[rg::voicing] == 1.0f && g.params[rg::gEqLaw] == 1.0f);
    CHECK (std::abs (rg::gch::gainDb (g.params[rg::gLfGain]) - 5.0) < 1e-4 && std::abs (rg::gch::lfHz (g.params[rg::gLfHz]) - 80.0) < 1e-3);
    CHECK (std::abs (rg::gch::gainDb (g.params[rg::gHfGain]) - 17.0) < 1e-4 && g.params[rg::gHmfX3] == 1.0f && g.params[rg::gLmfDiv3] == 0.0f);
    CHECK (std::abs (rg::gch::hmfKHz (g.params[rg::gHmfKHz]) - 5.0) < 1e-4);
}

TEST (parity190mt_q10_bell_tan_law_is_exact)
{
    using namespace lynx;
    auto s = makeEffectSlot (EffectKind::facetEq);
    s.params[(std::size_t) facet::wavesLawParam] = 1.0f;
    const auto set = [&s] (double hz, double db, double q10) {
        s.params[(std::size_t) facet::param (0, 0)] = facet::shapeValue (facet::Shape::bell);
        s.params[(std::size_t) facet::param (0, 1)] = facet::hzValue (hz);
        s.params[(std::size_t) facet::param (0, 2)] = facet::gainValue (db);
        s.params[(std::size_t) facet::param (0, 3)] = facet::qValue (q10 / 4.0);
        s.params[(std::size_t) facet::wavesBandParam0] = facet::waveCodeValue (facet::WaveCode::q10BellTan);
    };
    // the cookbook section with alpha = tan (min (π f / (Q fs), π / 4)) / (0.090904 (√G + 1/√G)) — its exact magnitude
    for (const double hz : { 1000.0, 4000.0, 10000.0, 16000.0 })
        for (const double q10 : { 0.5, 1.0, 7.0 })
        {
            set (hz, 6.0, facet::q (facet::qValue (q10 / 4.0)) * 4.0);
            const double q = facet::q (s.params[(std::size_t) facet::param (0, 3)]) * 4.0, f = facet::hz (s.params[(std::size_t) facet::param (0, 1)]);
            const double db = facet::gainDb (s.params[(std::size_t) facet::param (0, 2)]);
            const double G = std::pow (10.0, db / 20.0), A = std::pow (10.0, db / 40.0), fs = 48000.0, w0 = 2.0 * M_PI * f / fs;
            const double al = std::tan (std::min (M_PI * f / (q * fs), 0.25 * M_PI)) / (0.090904 * (std::sqrt (G) + 1.0 / std::sqrt (G)));
            for (const double probe : { 0.5 * f, 0.8 * f, std::min (1.3 * f, 23000.0) })
            {
                const std::complex<double> z1 = std::exp (std::complex<double> (0.0, -2.0 * M_PI * probe / fs));
                const auto num = (1.0 + al * A) - 2.0 * std::cos (w0) * z1 + (1.0 - al * A) * z1 * z1;
                const auto den = (1.0 + al / A) - 2.0 * std::cos (w0) * z1 + (1.0 - al / A) * z1 * z1;
                const double want = 20.0 * std::log10 (std::abs (num / den));
                CHECK (std::abs (parity190mm::peakDb (s, probe) - want) < 1e-3);
            }
            CHECK (std::abs (parity190mm::peakDb (s, f) - db) < 1e-3);
        }
    // the PQ-Bell: alpha = θ (1 + β θ²) / k — at 12 kHz, Q 1, +6 dB (θ = π / 4): the probe's alpha 1.4648 (β 0.04289, k 0.5503)
    s.params[(std::size_t) facet::wavesBandParam0] = facet::waveCodeValue (facet::WaveCode::q10PqBellX);
    s.params[(std::size_t) facet::param (0, 1)] = facet::hzValue (12000.0);
    s.params[(std::size_t) facet::param (0, 2)] = facet::gainValue (6.0);
    s.params[(std::size_t) facet::param (0, 3)] = facet::qValue (0.25);
    {
        const double f = facet::hz (s.params[(std::size_t) facet::param (0, 1)]), q = facet::q (s.params[(std::size_t) facet::param (0, 3)]) * 4.0;
        const double db = facet::gainDb (s.params[(std::size_t) facet::param (0, 2)]), A = std::pow (10.0, db / 40.0), w0 = 2.0 * M_PI * f / 48000.0;
        const double th = std::min (M_PI * f / (q * 48000.0), 0.25 * M_PI), al = th * (1.0 + 0.04289 * th * th) / 0.5503;
        CHECK (std::abs (al - 1.4648) < 0.01);
        const std::complex<double> z1 = std::exp (std::complex<double> (0.0, -2.0 * M_PI * 6000.0 / 48000.0));
        const auto num = (1.0 + al * A) - 2.0 * std::cos (w0) * z1 + (1.0 - al * A) * z1 * z1;
        const auto den = (1.0 + al / A) - 2.0 * std::cos (w0) * z1 + (1.0 - al / A) * z1 * z1;
        CHECK (std::abs (parity190mm::peakDb (s, 6000.0) - 20.0 * std::log10 (std::abs (num / den))) < 0.01);
    }
    // the Q10 translator picks code 14 for its Bell (type 0) and 15 for its PQ-Bell (type 5)
    int n = 0;
    auto q = parity190jh::waves ("Q10 Stereo", "Q10", parity190jh::list (122, { { 20, "1" }, { 21, "0" }, { 22, "3" }, { 23, "4000" }, { 24, "1" },
        { 48, "16000" } }), n);
    CHECK (facet::waveCode (q.params[(std::size_t) facet::wavesBandParam0]) == (int) facet::WaveCode::q10BellTan);
    auto pq = parity190jh::waves ("Q10 Stereo", "Q10", parity190jh::list (122, { { 20, "1" }, { 21, "5" }, { 22, "3" }, { 23, "8000" }, { 24, "1" },
        { 48, "16000" } }), n);
    CHECK (facet::waveCode (pq.params[(std::size_t) facet::wavesBandParam0]) == (int) facet::WaveCode::q10PqBellX);
}

TEST (parity190mt_api560_law_latency_and_network)
{
    using namespace lynx;
    auto s = makeEffectSlot (EffectKind::facetEq);
    CHECK (s.params[(std::size_t) facet::api560LawParam] == 0.0f && facet::storedParamCount <= maxEffectParams);
    CHECK (facet::api560LawParam == facet::wavesBandParam0 + facet::bands && facet::storedParamCount == facet::api560GainParam0 + 10);
    {
        auto fx = makeEffect (s);
        fx->prepare (48000.0);
        CHECK (fx->latencySamples() == 0); // every older project: no latency
    }
    s.params[(std::size_t) facet::api560LawParam] = 1.0f;
    for (int k = 0; k < 10; ++k) s.params[(std::size_t) (facet::api560GainParam0 + k)] = facet::api560GainValue (0.0);
    CHECK (savedEffectParamCount (s) == (std::size_t) facet::api560GainParam0 + 10);
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        auto fx = makeEffect (s);
        fx->prepare (sr);
        CHECK (fx->latencySamples() == 65); // known before the first block (PDC plans it)
        // every slider at 0: a plain 65-sample delay
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        l[10] = 0.5f; r[10] = -0.25f;
        fx->process (l.data(), r.data(), 512, s.params, 120.0);
        CHECK (l[75] == 0.5f && r[75] == -0.25f && l[74] == 0.0f && l[76] == 0.0f);
    }
    // 1 kHz +6 dB: [EXEC] the reference's slider law is not linear (±6 dB → about ±2.3 dB at the band)
    s.params[(std::size_t) (facet::api560GainParam0 + 5)] = facet::api560GainValue (6.0);
    const double g1k = parity190mt::sineGainDb (s, 1000.0), g100 = parity190mt::sineGainDb (s, 100.0), g10k = parity190mt::sineGainDb (s, 10000.0);
    CHECK (g1k > 1.5 && g1k < 3.5 && std::abs (g100) < 0.7 && std::abs (g10k) < 0.7);
    s.params[(std::size_t) (facet::api560GainParam0 + 5)] = facet::api560GainValue (-6.0);
    CHECK (std::abs (parity190mt::sineGainDb (s, 1000.0) + g1k) < 0.15); // the cut mirrors the boost
    // the translator: API-560 1 kHz +12 dB (RealWorld 3) -> the law with slider 5 at +12
    int n = 0;
    auto b = parity190jh::waves ("API-560 Stereo", "API-560", parity190jh::list (23, { { 3, "12" }, { 13, "1" } }), n);
    CHECK (b.params[(std::size_t) facet::api560LawParam] == 1.0f);
    CHECK (std::abs (facet::api560GainDb (b.params[(std::size_t) (facet::api560GainParam0 + 5)]) - 12.0) < 1e-4);
    CHECK (std::abs (facet::api560GainDb (b.params[(std::size_t) (facet::api560GainParam0 + 0)])) < 1e-4);
}

TEST (parity190mt_rchannel_rvox_type)
{
    using namespace lynx;
    auto plain = makeEffectSlot (EffectKind::compressor);
    CHECK (plain.params[(std::size_t) compressorRVoxType] == 0.0f);
    int n = 0;
    // RChannel, RVox type (44), threshold −24 dB, ratio 2 (ignored by the type), EQ off
    auto s = parity190jh::waves ("RChannel Stereo", "RChannel", parity190jh::list (137, { { 20, "0" }, { 41, "1" }, { 42, "16" }, { 43, "160" },
        { 44, "1" }, { 45, "-24" }, { 46, "2" } }), n);
    CHECK (s.kind == EffectKind::compressor && s.params[(std::size_t) compressorRVoxType] == 1.0f && s.params[(std::size_t) compressorRenLaw] == 1.0f);
    auto t = parity190jh::waves ("RChannel Stereo", "RChannel", parity190jh::list (137, { { 20, "0" }, { 41, "1" }, { 42, "16" }, { 43, "160" },
        { 44, "0" }, { 45, "-24" }, { 46, "2" } }), n);
    CHECK (t.params[(std::size_t) compressorRVoxType] == 0.0f);
    // [EXEC] a 1 kHz sine at 0 dBFS peak through the type at −24 dB: −24.5 dB (the stair probe); the ratio is ignored
    const double g = parity190mt::sineGainDb (s, 1000.0, 48000.0, 1.0);
    CHECK (g < -22.5 && g > -26.5);
    auto r50 = parity190jh::waves ("RChannel Stereo", "RChannel", parity190jh::list (137, { { 20, "0" }, { 41, "1" }, { 42, "16" }, { 43, "160" },
        { 44, "1" }, { 45, "-24" }, { 46, "50" } }), n);
    CHECK (std::abs (parity190mt::sineGainDb (r50, 1000.0, 48000.0, 1.0) - g) < 1e-6);
}

TEST (parity190mt_rchannel_gate)
{
    using namespace lynx;
    int n = 0;
    // gate on (57), gate mode (58), threshold −40 dB, floor −30 dB, release 100 ms; the compressor at 1:1, EQ off
    auto s = parity190jh::waves ("RChannel Stereo", "RChannel", parity190jh::list (137, { { 20, "0" }, { 41, "1" }, { 42, "16" }, { 43, "160" },
        { 45, "0" }, { 46, "1" }, { 57, "1" }, { 58, "1" }, { 59, "-40" }, { 60, "-30" }, { 61, "130" }, { 62, "100" } }), n);
    CHECK (s.params[(std::size_t) compressorRchGate] == 1.0f && s.params[(std::size_t) compressorRchGateMode] == 1.0f);
    // its threshold at −80 (the slider's −inf) leaves the gate off; a plain Compressor has none
    auto off = parity190jh::waves ("RChannel Stereo", "RChannel", parity190jh::list (137, { { 20, "0" }, { 41, "1" }, { 45, "0" }, { 46, "1" },
        { 57, "1" }, { 58, "1" }, { 59, "-80" }, { 60, "-80" }, { 62, "250" } }), n);
    CHECK (off.params[(std::size_t) compressorRchGate] == 0.0f && makeEffectSlot (EffectKind::compressor).params[(std::size_t) compressorRchGate] == 0.0f);
    // a tone 20 dB under the threshold settles at the floor; 20 dB over it passes
    const double quiet = parity190mt::sineGainDb (s, 1000.0, 48000.0, 0.001), loud = parity190mt::sineGainDb (s, 1000.0, 48000.0, 0.1);
    CHECK (std::abs (quiet + 30.0) < 1.0 && std::abs (loud) < 0.5);
}
