// Round 190mg (sub-round voc): REA of the Waves Doubler DSP and black-box of Waves Tune Real-Time (owner's permission of
// 2026-11-17; /mnt/e/lynx-work/waves3_190mg/voc/HANDOFF.md, docs/analise/REA_rodada190mg_voc.md).
// - Gemini Doubler gets the splice law (hidden gemini::spliceLaw): one head per voice inside the delay ± 256 samples that
//   jumps by the best of 20 candidate lags (running autocorrelation of its source) at a quiet moment or a zero crossing,
//   forced 32 samples further, the old head fading out over 256 samples. Off for new instances and every project saved
//   before (bit for bit: voc/lab/gold.sh); the Waves Doubler translator sets it.
// - LYNX Tune gets the Waves Tune Real-Time law (hidden lynxtune::wavesLaw): the lows under a 150 Hz split come from the
//   dry signal and the make-up shelf is off. Off for new instances and old projects; the Waves Tune translator sets it.
#include <cmath>
#include <random>

#include "core/Effects.h"
#include "core/LynxTune.h"
#include "core/PluginSubstitutes.h"
#include "core/VoiceTools.h"

namespace parity190mgvoc
{
inline void run (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    for (std::size_t off = 0; off < l.size(); off += 256)
        fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (256, l.size() - off), s.params, 120.0);
}
// Gemini with voice 1 only (left, +6 cents, 9.4 ms), no direct signal, no shelves
inline lynx::EffectSlot oneVoice (bool splice)
{
    namespace g = lynx::voicetools::gemini;
    auto s = lynx::makeEffectSlot (lynx::EffectKind::geminiDoubler);
    s.params[g::directOn] = 0.0f;
    s.params[g::lowOn] = s.params[g::highOn] = 0.0f;
    s.params[(std::size_t) g::voiceParam (1, g::vOn)] = 0.0f;
    s.params[(std::size_t) g::voiceParam (0, g::vGain)] = 1.0f;
    s.params[(std::size_t) g::voiceParam (0, g::vPan)] = 0.0f;
    s.params[(std::size_t) g::voiceParam (0, g::vDetune)] = 0.53f; // +6 cents
    s.params[(std::size_t) g::voiceParam (0, g::vDelay)] = 0.094f; // 9.4 ms
    s.params[(std::size_t) g::voiceParam (0, g::vDepth)] = 0.5f;
    s.params[g::spliceLaw] = splice ? 1.0f : 0.0f;
    return s;
}
// the lag (samples) of out against in over [at, at + n): the best normalised correlation over 1 … maxLag
inline int bestLag (const std::vector<float>& in, const std::vector<float>& out, std::size_t at, std::size_t n, int maxLag, double* corr = nullptr)
{
    int best = 0;
    double bc = -2.0;
    double eo = 0.0;
    for (std::size_t i = at; i < at + n; ++i)
        eo += (double) out[i] * out[i];
    for (int d = 1; d <= maxLag; ++d)
    {
        double c = 0.0, ei = 0.0;
        for (std::size_t i = at; i < at + n; ++i)
        {
            c += (double) out[i] * in[i - (std::size_t) d];
            ei += (double) in[i - (std::size_t) d] * in[i - (std::size_t) d];
        }
        c /= std::sqrt (ei * eo + 1e-30);
        if (c > bc)
            bc = c, best = d;
    }
    if (corr != nullptr)
        *corr = bc;
    return best;
}
} // namespace parity190mgvoc

TEST (parity190mg_voc_gemini_splice_law_is_hidden_and_off_for_new_and_old_instances)
{
    using namespace lynx;
    namespace g = voicetools::gemini;
    CHECK ((int) effectInfo (EffectKind::geminiDoubler).params.size() == g::paramCount && g::spliceLaw == g::paramCount);
    auto s = makeEffectSlot (EffectKind::geminiDoubler);
    CHECK (s.params[g::spliceLaw] == 0.0f && savedEffectParamCount (s) == (std::size_t) g::paramCount);
    s.params[g::spliceLaw] = 1.0f; // saved only while on
    CHECK (savedEffectParamCount (s) == (std::size_t) g::paramCount + 1);
    // the Waves Doubler translator sets it (refcap182's default: Align off)
    const std::string values = "0 1 0 0 0 * * * * 1 0 101 1306 4.1 1 16 0 0 0 0 64 20 32 -60 -40 -20 "
                               "1 -6 -45 0 6 9.4 0 0 1 * 1 -6 45 0 -6 23.7 0 0 1 *";
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 17.1_x64";
    id.state = substtest::wavesComponent ("Doubler2", values);
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry != nullptr)
    {
        int n = 0;
        const auto t = makeSubstituteSlot (*entry, &id, &n);
        CHECK (t.kind == EffectKind::geminiDoubler && t.params[g::spliceLaw] == 1.0f);
        CHECK (std::abs (g::delayMs (t.params[(std::size_t) g::voiceParam (0, g::vDelay)]) - 9.4) < 0.01);
    }
}

TEST (parity190mg_voc_gemini_splice_law_keeps_the_head_in_its_band_and_jumps_by_a_candidate_span)
{
    using namespace lynx;
    const double sr = 48000.0;
    std::mt19937 rng (190);
    std::normal_distribution<double> gauss (0.0, 0.1);
    std::vector<float> in (6 * 48000);
    for (auto& v : in)
        v = (float) gauss (rng);
    auto l = in, r = in;
    parity190mgvoc::run (parity190mgvoc::oneVoice (true), sr, l, r);
    // the head's lag every 40 ms: inside 451 ± 288 (+ a little for the window), drifting 166.7 samples / s toward the
    // present between jumps; every jump is one of the candidate lags 256 · 2^(k/20)
    std::vector<int> lags;
    for (std::size_t at = 4800; at + 256 < in.size(); at += 1920)
    {
        double c = 0.0;
        const auto d = parity190mgvoc::bestLag (in, l, at, 256, 800, &c);
        if (c > 0.6)
            lags.push_back (d);
    }
    CHECK (lags.size() > 100);
    int jumps = 0, inBand = 0, candidate = 0;
    for (std::size_t i = 0; i < lags.size(); ++i)
    {
        inBand += lags[i] >= 451 - 290 && lags[i] <= 451 + 290 ? 1 : 0;
        if (i > 0 && lags[i] - lags[i - 1] > 100)
        {
            ++jumps;
            // the step between two looks 40 ms apart: a candidate lag (256 … 495) less the drift (≤ 7 samples), give
            // or take the half-faded old head the correlation may still see
            const auto step = lags[i] - lags[i - 1];
            candidate += step >= 256 - 20 && step <= 495 + 8 ? 1 : 0;
        }
    }
    CHECK (inBand == (int) lags.size());
    CHECK (jumps >= 2 && candidate == jumps);
    // deterministic from a fresh instance; finite at other rates; law 0 is the old head (different here)
    auto l2 = in, r2 = in;
    parity190mgvoc::run (parity190mgvoc::oneVoice (true), sr, l2, r2);
    CHECK (l2 == l);
    auto l0 = in, r0 = in;
    parity190mgvoc::run (parity190mgvoc::oneVoice (false), sr, l0, r0);
    CHECK (l0 != l);
    for (const double rate : { 44100.0, 96000.0 })
    {
        auto a = in, b = in;
        parity190mgvoc::run (parity190mgvoc::oneVoice (true), rate, a, b);
        double e = 0.0;
        for (const auto v : a)
            e += (double) v * v;
        CHECK (std::isfinite (e) && e > 0.3 * 0.01 * (double) a.size()); // about the input's power (0.01)
    }
}

TEST (parity190mg_voc_lynx_tune_waves_law_is_hidden_and_keeps_the_lows_dry)
{
    using namespace lynx;
    namespace t = lynxtune;
    CHECK ((int) effectInfo (EffectKind::lynxTune).params.size() == t::count && t::wavesLaw == t::count);
    auto s = makeEffectSlot (EffectKind::lynxTune);
    CHECK (s.params[t::wavesLaw] == 0.0f && savedEffectParamCount (s) == (std::size_t) t::count);
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 17.1_x64";
    id.state = substtest::wavesComponent ("Waves Tune Real-Time",
                                          "80 900 6 0 * 1 * 7 0 0 * * * * * 1 0 80 1 1 15 120 100");
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry != nullptr)
    {
        int n = 0;
        const auto w = makeSubstituteSlot (*entry, &id, &n);
        CHECK (w.kind == EffectKind::lynxTune && w.params[t::wavesLaw] == 1.0f);
    }
    // an in-tune voice (A3, harmonics, chromatic, retune 0): with the law it comes out as the delayed input itself once
    // the crossfade has settled (the overlap-add re-synthesised it before); a voice 30 cents sharp is still corrected
    const double sr = 48000.0;
    const auto render = [&] (double cents, bool law, double& dryErrDb, double& pitchHz) {
        std::vector<float> l (2 * 48000);
        const double f = 220.0 * std::pow (2.0, cents / 1200.0);
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            double v = 0.0;
            for (int k = 1; k <= 8; ++k)
                v += 0.2 / k * std::sin (2.0 * M_PI * k * f * (double) i / sr);
            l[i] = (float) v;
        }
        auto slot = makeEffectSlot (EffectKind::lynxTune);
        slot.params[t::scale] = 0.0f; // chromatic
        slot.params[t::retune] = 0.0f;
        slot.params[t::wavesLaw] = law ? 1.0f : 0.0f;
        auto a = l, b = l;
        parity190mgvoc::run (slot, sr, a, b);
        const int lat = t::latencyFor (sr, t::rangeIndex (slot.params[t::range]), false);
        double e = 0.0, s2 = 0.0;
        for (std::size_t i = 48000; i < a.size(); ++i)
        {
            const double d = (double) a[i] - (double) l[i - (std::size_t) lat];
            e += d * d, s2 += (double) l[i] * l[i];
        }
        dryErrDb = 10.0 * std::log10 (e / s2 + 1e-30);
        // the output's pitch: zero crossings of the fundamental over the last second
        int crossings = 0;
        for (std::size_t i = 48001; i < a.size(); ++i)
            crossings += (a[i - 1] < 0.0f && a[i] >= 0.0f) ? 1 : 0;
        pitchHz = (double) crossings;
    };
    double onErr = 0.0, offErr = 0.0, hz = 0.0;
    render (0.0, true, onErr, hz);
    render (0.0, false, offErr, hz);
    CHECK (onErr < -100.0);          // the input itself
    CHECK (offErr > onErr + 20.0);   // (the overlap-add's re-synthesis before)
    double sharpErr = 0.0, sharpHz = 0.0;
    render (30.0, true, sharpErr, sharpHz);
    CHECK (std::abs (sharpHz - 220.0) <= 1.0); // corrected to A3 (the input is at 223.8 Hz)
}
