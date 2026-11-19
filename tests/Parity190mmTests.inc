// Round 190mm (coordinator): Facet EQ's hidden Waves design law (REA of the Waves EQs on our demo-mode impulse responses,
// docs/analise/REA_rodada190mm.md §2 / §3) and the translators that use it — Renaissance EQ, Q10, API-550A / 550B / 560
// (fitted Facet bands) — and SSL G-Channel on Ridge Strip. 0 for new instances and every project saved before.
#include <cmath>

#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"

namespace parity190mm
{
inline double peakDb (const lynx::EffectSlot& s, double hz)
{
    return lynx::facet::responseDb (s.params, 48000.0, hz) - lynx::facet::outputDb (s.params[(std::size_t) lynx::facet::outputParam]);
}
} // namespace parity190mm

TEST (parity190mm_facet_waves_law_is_hidden_and_saved_only_while_set)
{
    using namespace lynx;
    CHECK (effectInfo (EffectKind::facetEq).params.size() == (std::size_t) facet::paramCount);
    CHECK (facet::storedParamCount <= maxEffectParams);
    auto s = makeEffectSlot (EffectKind::facetEq);
    CHECK (savedEffectParamCount (s) <= (std::size_t) facet::paramCount);
    // a band code alone (law off) changes nothing; the law alone changes nothing (no band carries a code)
    s.params[(std::size_t) facet::param (0, 0)] = facet::shapeValue (facet::Shape::bell);
    s.params[(std::size_t) facet::param (0, 1)] = facet::hzValue (1000.0);
    s.params[(std::size_t) facet::param (0, 2)] = facet::gainValue (6.0);
    s.params[(std::size_t) facet::param (0, 3)] = facet::qValue (1.0);
    const double before = facet::responseDb (s.params, 48000.0, 1500.0);
    s.params[(std::size_t) (facet::wavesBandParam0 + 0)] = facet::waveCodeValue (facet::WaveCode::reqBell);
    CHECK (facet::responseDb (s.params, 48000.0, 1500.0) == before);
    CHECK (savedEffectParamCount (s) == (std::size_t) facet::wavesBandParam0 + 1);
    s.params[(std::size_t) (facet::wavesBandParam0 + 0)] = 0.0f;
    s.params[(std::size_t) facet::wavesLawParam] = 1.0f;
    CHECK (facet::responseDb (s.params, 48000.0, 1500.0) == before);
    CHECK (facet::waveCode (facet::waveCodeValue (facet::WaveCode::butterLowPass)) == (int) facet::WaveCode::butterLowPass);
}

TEST (parity190mm_req_bell_and_shelf_follow_the_measured_structure)
{
    using namespace lynx;
    auto s = makeEffectSlot (EffectKind::facetEq);
    s.params[(std::size_t) facet::wavesLawParam] = 1.0f;
    const auto set = [&s] (facet::WaveCode c, double hz, double db, double q) {
        s.params[(std::size_t) facet::param (0, 0)] = facet::shapeValue (facet::Shape::bell);
        s.params[(std::size_t) facet::param (0, 1)] = facet::hzValue (hz);
        s.params[(std::size_t) facet::param (0, 2)] = facet::gainValue (db);
        s.params[(std::size_t) facet::param (0, 3)] = facet::qValue (q);
        s.params[(std::size_t) facet::wavesBandParam0] = facet::waveCodeValue (c);
    };
    // the bell: exactly its gain at its frequency at every frequency (the prescribed-Nyquist-gain design), and its
    // Nyquist gain the analog prototype's: (s² + s G/q + 1) / (s² + s/q + 1) at 12 kHz, +12 dB, Q 1.4 -> +5.73 dB
    for (const double hz : { 100.0, 1000.0, 5000.0, 12000.0 })
        for (const double db : { -12.0, 4.0, 12.0 })
        {
            set (facet::WaveCode::reqBell, hz, db, 1.4);
            CHECK (std::abs (parity190mm::peakDb (s, hz) - db) < 0.02);
        }
    set (facet::WaveCode::reqBell, 12000.0, 12.0, 1.4);
    CHECK (std::abs (parity190mm::peakDb (s, 24000.0) - 5.734) < 0.05);
    // the shelves: the full gain far below (low) / above (high), 0 dB at the other end; Q clamped to 0.707 … 1.414
    set (facet::WaveCode::reqLowShelf, 1000.0, 12.0, 0.7);
    CHECK (std::abs (parity190mm::peakDb (s, 20.0) - 12.0) < 0.05 && std::abs (parity190mm::peakDb (s, 20000.0)) < 0.1);
    const double q07 = parity190mm::peakDb (s, 1500.0);
    set (facet::WaveCode::reqLowShelf, 1000.0, 12.0, 0.26);
    CHECK (parity190mm::peakDb (s, 1500.0) == q07);
    set (facet::WaveCode::reqHighShelf, 1000.0, -12.0, 1.0);
    CHECK (std::abs (parity190mm::peakDb (s, 20000.0) + 12.0) < 0.1 && std::abs (parity190mm::peakDb (s, 20.0)) < 0.05);
    // the 18 dB/oct cuts: −3 dB-ish at the corner region, ≈ 18 dB per octave far below (high pass)
    set (facet::WaveCode::reqHighPass, 1000.0, 0.0, 1.0);
    CHECK (std::abs (parity190mm::peakDb (s, 20000.0)) < 0.1);
    CHECK (std::abs ((parity190mm::peakDb (s, 125.0) - parity190mm::peakDb (s, 62.5)) - 18.0) < 1.0);
    // Q10's first-order shelf: half its gain (in dB) at its frequency
    set (facet::WaveCode::lowShelf1, 1000.0, 12.0, 1.0);
    CHECK (std::abs (parity190mm::peakDb (s, 1000.0) - 6.0) < 0.05);
}

TEST (parity190mm_req_q10_api_and_ssl_g_translate)
{
    using namespace lynx;
    int n = 0;
    // REQ 6: band 3 on, bell, −3 dB at 300 Hz, Q 1; band 1 on, high pass 80 Hz; output −1 dB
    auto r = parity190jh::waves ("REQ 6 Stereo", "REQ", parity190jh::list (140, { { 0, "1" }, { 1, "2" }, { 3, "80" }, { 4, "0.92" },
        { 10, "1" }, { 12, "-3" }, { 13, "300" }, { 14, "1" }, { 28, "8000" }, { 60, "-1" } }), n);
    CHECK (r.kind == EffectKind::facetEq && n == 12 && r.params[(std::size_t) facet::wavesLawParam] == 1.0f);
    CHECK (facet::waveCode (r.params[(std::size_t) (facet::wavesBandParam0 + 2)]) == (int) facet::WaveCode::reqBell);
    CHECK (facet::waveCode (r.params[(std::size_t) facet::wavesBandParam0]) == (int) facet::WaveCode::reqHighPass);
    CHECK (std::abs (parity190mm::peakDb (r, 300.0) + 3.0) < 0.1);
    CHECK (std::abs (facet::outputDb (r.params[(std::size_t) facet::outputParam]) + 1.0) < 0.01);
    // Q10: band 5 in, PQ bell +5 dB at 500 Hz
    auto q = parity190jh::waves ("Q10 Stereo", "Q10", parity190jh::list (122, { { 20, "1" }, { 21, "5" }, { 22, "5" }, { 23, "500" }, { 24, "1" },
        { 48, "16000" } }), n);
    CHECK (q.kind == EffectKind::facetEq && facet::waveCode (q.params[(std::size_t) facet::wavesBandParam0]) == (int) facet::WaveCode::q10PqBellX); // round 190mt: the PQ-Bell's own law (code 3 kept for 190mm's projects)
    CHECK (std::abs (parity190mm::peakDb (q, 500.0) - 5.0) < 0.1);
    // API-550A: mid −4 dB at 0.4 kHz -> one fitted band at the position, plus the flat response's two
    auto a = parity190jh::waves ("API-550A Stereo", "API-550", parity190jh::list (26, { { 2, "50" }, { 3, "-4" }, { 4, "0.4" }, { 6, "5" }, { 9, "1" }, { 17, "30" }, { 18, "75" }, { 19, "1.5" } }), n);
    CHECK (a.kind == EffectKind::facetEq && n == 4);
    CHECK (std::abs (parity190mm::peakDb (a, 400.0) + 4.0) < 0.6);
    // API-560: 1 kHz +12 dB (RealWorld 3)
    auto b = parity190jh::waves ("API-560 Stereo", "API-560", parity190jh::list (23, { { 3, "12" }, { 13, "1" } }), n);
    CHECK (b.kind == EffectKind::facetEq && n == 2 && parity190mm::peakDb (b, 1000.0) > 11.0);
    // SSL G-Channel: Ridge Strip's E voicing, LMF ÷3 at RealWorld 55
    auto g = parity190jh::waves ("SSLGChannel Stereo", "SSLGChannel", parity190jh::list (57, { { 1, "1" }, { 3, "0.4" }, { 5, "-30" }, { 9, "0.2" },
        { 15, "60" }, { 17, "0.9" }, { 18, "2.5" }, { 20, "3.5" }, { 21, "1.5" }, { 24, "8" }, { 28, "30" }, { 55, "1" } }), n);
    namespace rg = mixsuite::ridge;
    CHECK (g.kind == EffectKind::ridgeStrip && g.params[rg::voicing] == 1.0f && g.params[rg::lfBell] == 0.0f);
    CHECK (std::abs (g.params[rg::lmfFreq] - std::log (300.0 / 200.0) / std::log (2500.0 / 200.0)) < 1e-4);
}

TEST (parity190mm_rchannel_eq_runs_on_the_renaissance_law)
{
    using namespace lynx;
    int n = 0;
    // RChannel: EQ on (20), band 3 bell +6 dB at 1 kHz Q 1; compressor threshold 0 dB, ratio 1 (nothing compressed)
    auto s = parity190jh::waves ("RChannel Stereo", "RChannel", parity190jh::list (137, { { 10, "1" }, { 11, "0" }, { 12, "6" }, { 13, "1000" },
        { 14, "1" }, { 20, "1" }, { 42, "16" }, { 43, "160" }, { 45, "0" }, { 46, "1" } }), n);
    CHECK (s.kind == EffectKind::compressor && n == 11);
    CHECK (s.params[(std::size_t) compressorRenLaw] == 1.0f && s.params[(std::size_t) compressorChannelLaw] == 1.0f);
    CHECK (facet::waveCode (s.params[(std::size_t) (compressorChannelBand0 + 8)]) == (int) facet::WaveCode::reqBell);
    const auto level = [] (const EffectSlot& slot) {
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        std::vector<float> l (48000), r (48000);
        for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = (float) (0.01 * std::sin (2.0 * M_PI * 1000.0 * (double) i / 48000.0));
        for (std::size_t o = 0; o < l.size(); o += 256)
            fx->process (l.data() + o, r.data() + o, (int) std::min<std::size_t> (256, l.size() - o), slot.params, 120.0);
        double e = 0.0;
        for (std::size_t i = 24000; i < l.size(); ++i) e += (double) l[i] * l[i];
        return 10.0 * std::log10 (e / 24000.0);
    };
    auto plain = s;
    plain.params[(std::size_t) compressorChannelLaw] = 0.0f;
    CHECK (std::abs (level (s) - level (plain) - 6.0) < 0.2); // the bell's +6 dB at its frequency
}
