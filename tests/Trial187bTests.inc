// --- Round trial187b: sound parity measured on the official trials before they end (black-box: the trials' renders of
// LYNX's own test signals through the public parameter API; docs/PARITY.md "Round trial187b") ---------------------------
// Voice Shift (dual mono, the input itself at no shift, the right channel's lag, the level laws), Ember Classic's hot
// models above drive 5, Crest's Upward / Classic refits, Pro-G's Expert-only wet / dry (Parity128bTests.inc).

#include "core/DynamicsSuite.h"
#include "core/EmberClassic.h"
#include "core/VoiceShift.h"

namespace t187b
{
constexpr double sr = 48000.0;

inline void run (lynx::EffectKind kind, const lynx::EffectParams& p, std::vector<float>& l, std::vector<float>& r)
{
    auto slot = lynx::makeEffectSlot (kind);
    slot.params = p;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    fx->reset();
    for (std::size_t off = 0; off < l.size(); off += 256)
    {
        const auto m = (int) std::min<std::size_t> (256, l.size() - off);
        fx->process (l.data() + off, r.data() + off, m, p, 120.0);
    }
}

// a harmonic "voice" at f0 Hz with a slow vibrato (so the detector has a sung line, not a test tone)
inline std::vector<float> voice (double f0, double secs, double amp = 0.25)
{
    std::vector<float> x ((std::size_t) (secs * sr));
    double ph = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto f = f0 * (1.0 + 0.01 * std::sin (2.0 * 3.14159265358979323846 * 5.0 * (double) i / sr));
        ph += 2.0 * 3.14159265358979323846 * f / sr;
        double v = 0.0;
        for (int k = 1; k <= 16; ++k)
            v += std::sin (k * ph) / k;
        x[i] = (float) (amp * v / 1.8);
    }
    return x;
}

inline double rms (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (auto i = from; i < to; ++i) e += (double) v[i] * v[i];
    return std::sqrt (e / (double) std::max<std::size_t> (1, to - from));
}

// the lag (samples, b later = +) that best lines b up with a over [from, to)
inline int lagOf (const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to, int maxLag)
{
    double best = -1e30;
    int at = 0;
    for (int L = -maxLag; L <= maxLag; ++L)
    {
        double c = 0.0;
        for (auto i = from; i < to; ++i) c += (double) a[i] * b[(std::size_t) ((long) i + L)];
        if (c > best) { best = c; at = L; }
    }
    return at;
}
} // namespace t187b

TEST (voice_shift_no_shift_is_the_input_trial187b)
{
    using namespace lynx;
    namespace vs = voiceshift;
    // Transpose at Pitch 0 / Formant 0: the input, delayed by the latency, at the wet trim (−2.7 dB) [EXEC trial: its
    // sung line nulls against 0.733 × input to −15 dB; the overlap-add here drifted up to half a period]
    auto l = t187b::voice (200.0, 1.5), r = l;
    const auto x = l;
    const auto p = makeEffectSlot (EffectKind::voiceShift).params;
    t187b::run (EffectKind::voiceShift, p, l, r);
    const auto lat = (std::size_t) vs::latencyFor (t187b::sr);
    double err = 0.0;
    for (std::size_t i = lat + 4800; i < l.size(); ++i)
        err = std::max (err, (double) std::abs (l[i] - 0.733f * x[i - lat]));
    CHECK (err < 1e-4);
}

TEST (voice_shift_channels_are_independent_trial187b)
{
    using namespace lynx;
    namespace vs = voiceshift;
    // [EXEC trial: a voice on the left only leaves the right silent; the left is the same whatever plays on the right]
    auto p = makeEffectSlot (EffectKind::voiceShift).params;
    p[vs::pitch] = vs::semitoneValue (5.0);
    auto l = t187b::voice (200.0, 1.5), r = std::vector<float> (l.size(), 0.0f);
    auto l2 = l;
    auto r2 = t187b::voice (330.0, 1.5, 0.2);
    t187b::run (EffectKind::voiceShift, p, l, r);
    t187b::run (EffectKind::voiceShift, p, l2, r2);
    CHECK (t187b::rms (r, 0, r.size()) < 1e-6);
    double diff = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) diff = std::max (diff, (double) std::abs (l[i] - l2[i]));
    CHECK (diff < 1e-6);
    // the same voice on both sides: once shifted, the right runs ~1.25 ms (60 samples) behind the left
    auto a = t187b::voice (200.0, 1.5), b = a;
    t187b::run (EffectKind::voiceShift, p, a, b);
    const auto lag = t187b::lagOf (a, b, 36000, 48000, 120);
    CHECK (std::abs (lag - 60) <= 2);
    // and at no shift both sides stay one
    auto c = t187b::voice (200.0, 1.5), d = c;
    t187b::run (EffectKind::voiceShift, makeEffectSlot (EffectKind::voiceShift).params, c, d);
    double cd = 0.0;
    for (std::size_t i = 0; i < c.size(); ++i) cd = std::max (cd, (double) std::abs (c[i] - d[i]));
    CHECK (cd < 1e-7);
}

TEST (voice_shift_level_laws_trial187b)
{
    using namespace lynx::voiceshift;
    // [EXEC trial, its sung line] the voice gains 2.6 / 3.0 dB at +5 / +12 st against the formants, loses 0.43 dB per
    // semitone going down
    CHECK (std::abs (tiltDb (5.0) - 2.6) < 0.3 && std::abs (tiltDb (12.0) - 3.0) < 0.3);
    CHECK (std::abs (tiltDb (-12.0) + 5.16) < 0.05 && tiltDb (0.0) == 0.0);
    // the drive's ceiling sinks 2.5 dB more at 10 (a loud programme came out 2.8 dB hot); small signals unchanged
    const auto hot = 20.0 * std::log10 (std::abs (driveCurve (2.0f, 10.0)));
    CHECK (hot < 2.4 - 14.6 - 2.0);
    const auto g = 20.0 * std::log10 (std::abs (driveCurve (0.001f, 2.5) / 0.001f));
    CHECK (g > 3.0 && g < 6.0);
}

TEST (ember_classic_hot_models_trial187b)
{
    using namespace lynx::emberclassic;
    // drive ≤ 5: the shipped table exactly; drive 10: the hot model (E came out 5 … 7 dB loud there)
    const auto render = [] (int style, double drive, bool tableOnly) {
        Engine e;
        e.prepare (t187b::sr);
        Settings s;
        s.style = style;
        s.drive = drive;
        if (tableOnly)
            e.set (s, &styleModel (style));
        else
            e.set (s);
        e.reset();
        std::vector<float> x (48000);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.5 * std::sin (2.0 * 3.14159265358979323846 * 220.0 * (double) i / t187b::sr));
        e.process (x.data(), nullptr, (int) x.size());
        return x;
    };
    for (int st = 0; st < styleCount; ++st)
    {
        const auto a = render (st, 5.0, false), b = render (st, 5.0, true);
        double d = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) d = std::max (d, (double) std::abs (a[i] - b[i]));
        CHECK (d < 1e-7);
    }
    const auto hot = render (1, 10.0, false), table = render (1, 10.0, true);
    CHECK (20.0 * std::log10 (t187b::rms (hot, 12000, 48000) / t187b::rms (table, 12000, 48000)) < -4.0);
    CHECK (std::all_of (hot.begin(), hot.end(), [] (float v) { return std::isfinite (v); }));
}

TEST (crest_upward_holds_less_trial187b)
{
    using namespace lynx;
    namespace c = dyn::crest;
    // [EXEC trial, Upward, threshold −24, ratio 4, auto gain off, 1 kHz at −10 dBFS]: it holds −2.8 dB over the
    // threshold; round tp172b2's fit held −6.2 dB, the refit about −5.3
    auto p = makeEffectSlot (EffectKind::crestCompressor).params;
    p[c::style] = c::styleValue (c::Style::upward);
    p[c::voicing] = 0.0f;
    p[c::threshold] = (float) ((-24.0 + 60.0) / 60.0);
    p[c::ratio] = c::ratioValue (4.0);
    p[c::autoGain] = 0.0f;
    std::vector<float> l (48000), r;
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.316 * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * (double) i / t187b::sr));
    r = l;
    const auto x = l;
    t187b::run (EffectKind::crestCompressor, p, l, r);
    const auto g = 20.0 * std::log10 (t187b::rms (l, 24000, 48000) / t187b::rms (x, 24000, 48000));
    CHECK (g > -5.8 && g < -2.0);
}
