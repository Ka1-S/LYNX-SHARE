// --- round tp172b: sound parity of the FabFilter-role effects, measured black-box against the trials
// (/mnt/e/lynx-work/tp172b/report.md): Apex's two-stage release, Crest's auto gain, LuxeVerb's Space engine and the
// Pro-L 2 / Pro-R 2 state translators.

#include "core/ApexLimiter.h"
#include "core/DynamicsSuite.h"
#include "core/FabFilterStates.h"
#include "core/LuxeVerb.h"

namespace p172b
{
constexpr double sr = 48000.0;

inline std::vector<std::uint8_t> ffbs (const std::vector<float>& values)
{
    std::vector<std::uint8_t> b { 'F', 'F', 'B', 'S' };
    auto put = [&b] (std::uint32_t u) {
        for (int i = 0; i < 4; ++i)
            b.push_back ((std::uint8_t) (u >> (8 * i)));
    };
    put (1);
    put ((std::uint32_t) values.size());
    for (const auto v : values)
    {
        std::uint32_t u;
        std::memcpy (&u, &v, 4);
        put (u);
    }
    return b;
}

inline void run (lynx::Effect& fx, std::vector<float>& l, std::vector<float>& r, const lynx::EffectParams& p)
{
    const int n = (int) l.size();
    for (int i = 0; i < n; i += 256)
        fx.process (l.data() + i, r.data() + i, std::min (256, n - i), p, 120.0);
}

// RT60 (s) of a mono impulse response from its Schroeder curve (−5 … −25 dB, × 3)
inline double rt60 (const std::vector<float>& h)
{
    std::vector<double> e (h.size() + 1, 0.0);
    for (std::size_t i = h.size(); i-- > 0;)
        e[i] = e[i + 1] + (double) h[i] * h[i];
    std::size_t a = 0, b = 0;
    for (std::size_t i = 0; i < h.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (e[i] / e[0], 1e-30));
        if (a == 0 && db <= -5.0) a = i;
        if (db <= -25.0) { b = i; break; }
    }
    return b > a ? 3.0 * (double) (b - a) / sr : 0.0;
}
} // namespace p172b

TEST (parity172b_apex_fast_transient_recovery)
{
    // a 1.6 ms burst 9.6 dB over the ceiling barely touches the quiet sine after it (the reference's Transparent:
    // −0.1 dB 5 ms later); a 200 ms burst leaves the slow stage pulling the sine down for a while
    using namespace lynx::apex;
    lynx::EffectParams p {};
    for (std::size_t i = 0; i < info().params.size(); ++i) p[i] = info().params[i].defaultValue;
    float lo = 0.0f, hi = 1.0f; // the Gain value that gives +10 dB
    for (int i = 0; i < 40; ++i) (gainDb (0.5f * (lo + hi)) < 10.0 ? lo : hi) = 0.5f * (lo + hi);
    p[pGain] = 0.5f * (lo + hi);
    p[pAttack] = (float) std::pow (250.0 / 10000.0, 0.25); // 250 ms
    p[pRelease] = releaseValue (500.0);
    CHECK (std::abs (gainDb (p[pGain]) - 10.0) < 0.01 && std::abs (attackMs (p[pAttack]) - 250.0) < 1.0);
    const auto after = [&] (double burstMs) {
        auto fx = make();
        fx->prepare (p172b::sr);
        const int n = (int) (p172b::sr * 1.2);
        std::vector<float> l ((std::size_t) n), r;
        const int b0 = (int) (0.5 * p172b::sr), b1 = b0 + (int) (burstMs * 0.001 * p172b::sr);
        for (int i = 0; i < n; ++i)
            l[(std::size_t) i] = (float) ((i >= b0 && i < b1 ? 0.95 : 0.1) * std::sin (2.0 * M_PI * 1000.0 * i / p172b::sr));
        r = l;
        p172b::run (*fx, l, r, p);
        const int lat = fx->latencySamples(), from = b1 + lat + (int) (0.005 * p172b::sr), len = (int) (0.005 * p172b::sr);
        double e = 0.0;
        for (int i = from; i < from + len; ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        const double rmsOut = std::sqrt (e / len), rmsIn = 0.1 * std::sqrt (0.5) * std::pow (10.0, 10.0 / 20.0);
        return 20.0 * std::log10 (rmsOut / rmsIn);
    };
    const double shortBurst = after (1.6), longBurst = after (200.0);
    std::printf ("  apex: 5 ms after a 1.6 ms burst %.2f dB, after a 200 ms burst %.2f dB\n", shortBurst, longBurst);
    CHECK (shortBurst > -0.6);
    CHECK (longBurst < -3.0);
}

TEST (parity172b_crest_auto_gain_follows_attack)
{
    // the reference's auto gain makes up less for a slow attack (+5.1 dB at 0.005 ms … +0.9 dB at 50 ms, defaults)
    using namespace lynx::dyn::crest;
    const auto makeupDb = [] (float attackValue) {
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::crestCompressor);
        slot.params[attack] = attackValue;
        slot.params[autoGain] = 1.0f;
        auto fx = lynx::makeEffect (slot);
        fx->prepare (p172b::sr);
        std::vector<float> l (24000), r;
        for (int i = 0; i < 24000; ++i) l[(std::size_t) i] = (float) (0.001 * std::sin (2.0 * M_PI * 1000.0 * i / p172b::sr)); // −60 dB: no reduction
        r = l;
        p172b::run (*fx, l, r, slot.params);
        double e = 0.0;
        for (int i = 12000; i < 24000; ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return 20.0 * std::log10 (std::sqrt (e / 12000.0) / (0.001 * std::sqrt (0.5)));
    };
    const double fast = makeupDb (0.0f), slow = makeupDb (1.0f);
    std::printf ("  crest auto gain: fastest attack %+.2f dB, slowest %+.2f dB\n", fast, slow);
    CHECK (fast > 2.0 && fast < 8.0);
    CHECK (slow < 0.5);
}

TEST (parity172b_crest_no_lift_after_silence)
{
    // round tp172b2 (control audit 173d): after digital silence (or a −100 dBFS floor) a loud burst never comes out
    // above its own peak for any style or voicing (auto gain off, range 60 dB, the lowest threshold, the highest
    // ratio, the fastest and the slowest attack); the original's Upward lift stops at 30 dB, Crest's at the same
    using namespace lynx::dyn::crest;
    double worst = -100.0;
    for (int st = 0; st < allStyles; ++st)
        for (const float att : { 0.0f, 1.0f })
            for (const double floorAmp : { 0.0, 1e-5 })
            {
                auto slot = lynx::makeEffectSlot (lynx::EffectKind::crestCompressor);
                slot.params[style] = styleValue ((Style) st);
                slot.params[voicing] = voicingValue ((Style) st);
                slot.params[threshold] = 0.0f;
                slot.params[ratio] = 1.0f;
                slot.params[range] = 1.0f;
                slot.params[attack] = att;
                slot.params[autoGain] = 0.0f;
                auto fx = lynx::makeEffect (slot);
                fx->prepare (p172b::sr);
                std::vector<float> l (120000), r;
                std::uint32_t rng = 12345u;
                double inPeak = 0.0;
                for (int i = 0; i < 120000; ++i)
                {
                    rng = rng * 1664525u + 1013904223u;
                    const double u = ((double) (rng >> 8) / 16777216.0 - 0.5) * 2.0;
                    const double v = i >= 96000 ? 0.5 * u : floorAmp * u;
                    l[(std::size_t) i] = (float) v;
                    if (i >= 96000) inPeak = std::max (inPeak, std::abs (v));
                }
                r = l;
                p172b::run (*fx, l, r, slot.params);
                double outPeak = 0.0;
                for (int i = 96000; i < 120000; ++i) outPeak = std::max (outPeak, (double) std::abs (l[(std::size_t) i]));
                worst = std::max (worst, 20.0 * std::log10 (outPeak / inPeak));
            }
    std::printf ("  crest silence -> burst: the highest peak over the input %+.2f dB\n", worst);
    CHECK (worst < 1.0);
}

TEST (parity172b_luxeverb_space_engine)
{
    using namespace lynx;
    const auto& info = effectInfo (EffectKind::luxeVerb);
    CHECK (info.params.size() == (std::size_t) luxeVerbAllParamCount);
    CHECK (std::string (info.params[(std::size_t) luxeVerbSpaceFirst].name) == "Space engine" && info.params[(std::size_t) luxeVerbSpaceFirst].defaultValue == 0.0f);
    CHECK (luxeVerbParamText (luxeVerbSpaceFirst + luxeverb::sp::space, 0.5f) == "2.50 s");
    CHECK (luxeVerbParamText (luxeVerbSpaceFirst + luxeverb::sp::style, 1.0f) == "Plate");
    CHECK (std::abs (luxeverb::sp::decaySeconds (luxeverb::sp::decayValue (4.0)) - 4.0) < 1e-6);

    // engine off: the Space controls change nothing (the FL-exact engine runs)
    const auto render = [] (const EffectSlot& slot, int n) {
        auto fx = makeEffect (slot);
        fx->prepare (p172b::sr);
        std::vector<float> l ((std::size_t) n, 0.0f), r;
        l[100] = 0.5f;
        r = l;
        p172b::run (*fx, l, r, slot.params);
        return std::pair { l, r };
    };
    auto a = makeEffectSlot (EffectKind::luxeVerb), b = a;
    b.params[(std::size_t) (luxeVerbSpaceFirst + luxeverb::sp::space)] = 0.9f;
    b.params[(std::size_t) (luxeVerbSpaceFirst + luxeverb::sp::mix)] = 1.0f;
    CHECK (render (a, 9600).first == render (b, 9600).first);

    // engine on, Modern, 2.5 s, 100 % wet: the reference measured 2.75 s at 1 kHz (2.4 … 3.0 broadband)
    b.params[(std::size_t) luxeVerbSpaceFirst] = 1.0f;
    b.params[(std::size_t) (luxeVerbSpaceFirst + luxeverb::sp::space)] = 0.5f;
    const auto [l, r] = render (b, (int) (p172b::sr * 5));
    const double t = p172b::rt60 (std::vector<float> (l.begin() + 100, l.end()));
    double corr = 0.0, el = 0.0, er = 0.0;
    for (std::size_t i = 4800; i < 48000; ++i) { corr += (double) l[i] * r[i]; el += (double) l[i] * l[i]; er += (double) r[i] * r[i]; }
    std::printf ("  luxeverb space: RT60 %.2f s, L/R correlation %.3f\n", t, corr / std::sqrt (el * er));
    CHECK (t > 2.2 && t < 3.2);
    CHECK (std::abs (corr / std::sqrt (el * er)) < 0.2); // decorrelated at the default width
    bool finite = true;
    for (const auto v : l) finite = finite && std::isfinite (v);
    CHECK (finite);
}

TEST (parity172b_pro_r2_and_pro_l2_translators)
{
    using namespace lynx;
    // Pro-R 2: Plate, space 0.7 (4 s), mix 50 %, pre-delay 0.3333 (20 ms)
    std::vector<float> v (136, 0.0f);
    v[0] = 0.7f; v[2] = 0.5f; v[3] = 0.5f; v[4] = 2.0f; v[5] = 0.3f; v[7] = 0.7f; v[9] = 50.0f; v[16] = 0.3333f;
    v[21] = 9.966f; v[25] = 1.0f;
    const auto st = fabfilter::parseState (p172b::ffbs (v));
    CHECK (st.has_value());
    auto slot = makeEffectSlot (EffectKind::luxeVerb);
    const int n = st ? fabfilter::fromProR2 (*st, slot) : 0;
    const auto at = [&] (int i) { return slot.params[(std::size_t) (luxeVerbSpaceFirst + i)]; };
    CHECK (n >= 11);
    CHECK (at (luxeverb::sp::engine) == 1.0f && at (luxeverb::sp::style) == 1.0f);
    CHECK (std::abs (luxeverb::sp::decaySeconds (at (luxeverb::sp::space)) - 4.0) < 1e-3);
    CHECK (std::abs (at (luxeverb::sp::mix) - 0.5f) < 1e-6);
    CHECK (std::abs (luxeverb::sp::predelayMs (at (luxeverb::sp::predelay)) - 18.5) < 1.0);
    CHECK (fabfilter::translatorNamed ("pro-r2") != nullptr && fabfilter::translatorNamed ("pro-l2") != nullptr);
}

TEST (parity172b_crest_release_follows_attack)
{
    // the reference (Clean, threshold −30, ratio 4, 1 kHz at −6 dBFS): −17.1 dB at attack 10 / release 200 but only
    // −12.6 dB at attack = release = 100 ms — the release shortens with a slow attack, so a steady sine sags
    using namespace lynx::dyn::crest;
    const auto grDb = [] (double attMs, double relMs) {
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::crestCompressor);
        slot.params[style] = styleValue (Style::clean);
        slot.params[threshold] = 0.5f; // −30 dB
        slot.params[ratio] = ratioValue (4.0);
        slot.params[attack] = attackValue (attMs);
        slot.params[release] = releaseValue (relMs);
        slot.params[autoGain] = 0.0f;
        auto fx = lynx::makeEffect (slot);
        fx->prepare (p172b::sr);
        std::vector<float> l (48000), r;
        for (int i = 0; i < 48000; ++i) l[(std::size_t) i] = (float) (0.5012 * std::sin (2.0 * M_PI * 1000.0 * i / p172b::sr));
        r = l;
        p172b::run (*fx, l, r, slot.params);
        double e = 0.0;
        for (int i = 24000; i < 48000; ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return 20.0 * std::log10 (std::sqrt (e / 24000.0) / (0.5012 * std::sqrt (0.5)));
    };
    const double fast = grDb (10.0, 200.0), slow = grDb (100.0, 100.0);
    std::printf ("  crest steady gain: attack 10 / release 200 %.1f dB (ref -17.1), attack = release = 100 %.1f dB (ref -12.6)\n", fast, slow);
    CHECK (std::abs (fast + 17.1) < 1.0);
    CHECK (std::abs (slow + 12.6) < 1.5);
}

TEST (parity172b_hush_pro_ds_allround_level_law)
{
    // a Pro-DS state in Allround (threshold −40, range 8, wide band, the translated values): the reference turns a
    // 7 kHz sine down by 0.2 dB per dB over the threshold — −4.5 dB at −18 dBFS, the full 8 dB only at 0 dBFS
    const float v[] { 0.574031174f, 0.333333343f, 0.333333343f, 1, 0, 0, 1, 0.56f, 0.5f, 0, 1, 0.845098317f, 0.666666687f,
                      1, 0, 0, 0.5f, 1, 0, 0, 0.5f, 0.5f, 1, 0, 0 };
    const auto grDb = [&v] (double dbfs) {
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::hushDeEsser);
        for (std::size_t i = 0; i < std::size (v); ++i) slot.params[i] = v[i];
        auto fx = lynx::makeEffect (slot);
        fx->prepare (p172b::sr);
        const double a = std::pow (10.0, dbfs / 20.0);
        std::vector<float> l (48000), r;
        for (int i = 0; i < 48000; ++i) l[(std::size_t) i] = (float) (a * std::sin (2.0 * M_PI * 7000.0 * i / p172b::sr));
        r = l;
        p172b::run (*fx, l, r, slot.params);
        double e = 0.0;
        for (int i = 24000; i < 48000; ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return 20.0 * std::log10 (std::sqrt (e / 24000.0) / (a * std::sqrt (0.5)));
    };
    const double mid = grDb (-18.0), top = grDb (0.0);
    std::printf ("  hush allround: %.1f dB at -18 dBFS (ref -4.5), %.1f dB at 0 dBFS (ref -8.0)\n", mid, top);
    CHECK (std::abs (mid + 4.5) < 0.8);
    CHECK (std::abs (top + 8.0) < 0.5);
}
