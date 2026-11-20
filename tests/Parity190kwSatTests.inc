// Round 190kw (Saturn 2 -> Ember): Ember's characters that are the reference's styles run the reference's own laws when
// the hidden revision is on (/mnt/e/lynx-work/rea190kw/sat/HANDOFF.md; docs/PARITY.md "Round 190kw").
// - Each style = a curve (arctan, x / (1 + x), tanh, sine, 2nd / 3rd order, tube, rectifier) with a power and a negative-side
//   scale, a drive range (pre-gain lo … hi dB over the reference's 0 … 100 %) and the level compensation that keeps a sine
//   at the style's reference level at its peak-to-peak level, its EQ before / after the curve and the band tone stack.
// - New Ember and translated Saturn 2 states get the revision; a project saved before keeps round 150's characters bit for
//   bit (fingerprints below: revision 0 renders the same bytes as the base commit d9bfc68ce's, checked with fxrender).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/MixSuite.h"

namespace parity190kwSat
{
namespace em = lynx::mixsuite::ember;
// a 220 Hz tone with a 3.1 kHz partial, rising from -40 to 0 dBFS over the render, and a decaying noise burst each 0.4 s
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 4711u;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        const double nz = (double) (s >> 8) / 8388608.0 - 1.0;
        const double lvl = std::pow (10.0, (-40.0 + 40.0 * t / ((double) n / sr)) / 20.0);
        const double ph = std::fmod (t, 0.4);
        const double v = lvl * (0.8 * std::sin (2.0 * M_PI * 220.0 * t) + 0.2 * std::sin (2.0 * M_PI * 3100.0 * t))
                         + 0.1 * nz * std::exp (-ph * 40.0);
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (0.8 * v);
    }
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
}
} // namespace parity190kwSat

TEST (parity190kw_ember_saturn_law_is_hidden_new_and_translated_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    const auto& info = effectInfo (EffectKind::ember);
    CHECK (info.params.size() == (std::size_t) em::count && em::revision == em::count && em::lawRate == em::count + 1);
    auto s = makeEffectSlot (EffectKind::ember);
    CHECK (s.params[(std::size_t) em::revision] == 1.0f && s.params[(std::size_t) em::lawRate] == 0.0f);
    CHECK (savedEffectParamCount (s) == (std::size_t) em::revision + 1);
    auto now = s;
    upgradeLoadedEffectParams (now, (std::size_t) em::revision + 1);
    CHECK (now.params[(std::size_t) em::revision] == 1.0f);
    auto old = s; // saved before the round: round 150's characters
    upgradeLoadedEffectParams (old, (std::size_t) em::count);
    CHECK (old.params[(std::size_t) em::revision] == 0.0f);
    // a translated Saturn 2 state: the revision, and High Quality off -> the law's curve at the base rate
    for (const float hq : { 0.0f, 1.0f, 2.0f })
    {
        PluginIdentity id;
        id.name = "FabFilter Saturn 2";
        std::vector<std::pair<int, float>> v { { 5, 100.0f }, { 6, 1.0f }, { 7 + 3, 12.0f }, { 7 + 4, 0.4f }, { 7 + 10, 100.0f },
                                               { 7 + 13, 1.0f }, { 7 + 15, 5.322f }, { 7 + 16, 2.0f }, { 698, hq } };
        id.state = dyn108::ffbs (951, v);
        const auto* entry = findPluginSubstitute (id, false);
        CHECK (entry != nullptr);
        if (entry == nullptr) return;
        const auto t = makeSubstituteSlot (*entry, &id);
        CHECK (t.kind == EffectKind::ember && em::saturnLaw (t.params));
        CHECK (em::characterIndex (t.params[(std::size_t) em::characterParam (0)]) == 13);
        CHECK (em::saturnRaw (t.params) == (hq == 0.0f));
        // the drive: the reference's normalised value, reversibly
        CHECK (std::abs (em::referenceDrive (em::driveDb (t.params[(std::size_t) em::drive])) - 0.4) < 1e-6);
    }
}

TEST (parity190kw_ember_saturn_curves_drive_and_level_laws)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    constexpr double halfPi = 1.5707963267948966;
    // arctan with a negative-side scale (style 12: B = 0.2)
    for (const float x : { 0.1f, 0.7f, 3.0f })
    {
        CHECK (std::abs (em::saturnCurve (12, x) - std::atan (halfPi * x) / halfPi) < 1e-6);
        CHECK (std::abs (em::saturnCurve (12, -x) + std::atan (halfPi * 0.2 * x) / halfPi / 0.2) < 1e-6);
    }
    // 3rd order (style 6) and 2nd order (style 7): their polynomial up to 1, flat above
    CHECK (std::abs (em::saturnCurve (6, 0.5f) - 0.6875f) < 1e-6 && em::saturnCurve (6, 2.0f) == 1.0f);
    CHECK (std::abs (em::saturnCurve (7, -0.5f) + 0.75f) < 1e-6 && em::saturnCurve (7, -3.0f) == -1.0f);
    // the tube curve: continuous at its breakpoints, the hard positive ceiling, the softer negative floor
    for (const float u : { 0.32002f, -0.08905f, -1.75482f })
        CHECK (std::abs (em::saturnCurve (2, u - 1e-4f) - em::saturnCurve (2, u + 1e-4f)) < 2e-3);
    CHECK (std::abs (em::saturnCurve (2, 5.0f) - 0.62993f) < 1e-6 && std::abs (em::saturnCurve (2, -5.0f) + 1.17545f) < 1e-6);
    // the power (style 13: P = 0.8, B = 0.75): y = sign (x) f ((B|x|)^P)^(1/P) / B
    CHECK (std::abs (em::saturnCurve (13, 0.5f) - std::pow (std::atan (halfPi * std::pow (0.5, 0.8)) / halfPi, 1.25)) < 1e-5);
    // the rectifier
    CHECK (em::saturnCurve (26, -0.3f) == 0.3f);
    // the drive range and the level compensation: a sine at the reference level keeps its peak-to-peak level
    CHECK (std::abs (em::saturnPreDb (5, 0.5) - 9.0) < 1e-9 && std::abs (em::saturnPreDb (14, 1.0) - 87.0) < 1e-9);
    for (int st = 0; st < em::characters; ++st)
        for (const double d : { 0.0, 0.3, 1.0 })
        {
            const auto g = std::pow (10.0, em::saturnPreDb (st, d) / 20.0);
            const auto post = em::saturnPostGain (st, d);
            CHECK (std::isfinite (post) && post > 0.0 && g > 0.0);
        }
    // CleanTape (style 5: the sine curve, reference level -18 dBFS): post · (F (G C) − min (F (−G C), 0)) = 2 C
    {
        const double c = std::pow (10.0, -18.0 / 20.0), g = std::pow (10.0, em::saturnPreDb (5, 0.6) / 20.0);
        const double span = em::saturnCurve (5, (float) (g * c)) - std::min ((double) em::saturnCurve (5, (float) (-g * c)), 0.0);
        CHECK (std::abs (em::saturnPostGain (5, 0.6) * span - 2.0 * c) < 1e-6);
    }
    // through Ember: Subtle Tape (style 4: the sine curve, no EQ of its own, reference level -10 dBFS) keeps a 1 kHz sine at
    // its reference level at that level, at any drive
    for (const float drive : { 0.2f, 0.5f, 0.8f })
    {
        auto s = makeEffectSlot (EffectKind::ember);
        s.params[(std::size_t) em::characterParam (0)] = 5.0f / (float) em::characters;
        s.params[(std::size_t) em::drive] = drive;
        CHECK (em::lawCharacter (5) && em::lawCharacter (6) && ! em::lawCharacter (27)); // round 190lg: + Clean Tape
        const double sr = 48000.0, a = std::pow (10.0, -10.0 / 20.0);
        const int n = (int) (0.5 * sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        for (int i = 0; i < n; ++i) l[(std::size_t) i] = r[(std::size_t) i] = (float) (a * std::sin (2.0 * M_PI * 1000.0 * i / sr));
        parity190kwSat::render (s, sr, l, r);
        float lo = 1e9f, hi = -1e9f;
        for (int i = n / 2; i < n; ++i) { lo = std::min (lo, l[(std::size_t) i]); hi = std::max (hi, l[(std::size_t) i]); }
        CHECK (std::abs (20.0 * std::log10 ((hi - lo) / (2.0 * a))) < 0.3);
    }
}

TEST (parity190kw_ember_revision0_renders_as_the_base_commit_did)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    struct Case
    {
        int character, character2;
        float drive, quality;
        double sr;
        std::uint64_t hash;
        double energy;
    };
    // 1.6 s of parity190kwSat::signal in blocks of 256: FNV-1a of the float bits and the energy, revision 0 (a project saved
    // before the round). Taken with the round's objects; fxrender renders of the same settings through the base commit's
    // and the round's binaries are byte-identical (/mnt/e/lynx-work/rea190kw/sat/ident.sh: 0 of 48 files differ)
    const Case cases[] = {
#include "Parity190kwSatGold.inc" // (rollup step 9: relative to this file; CMake has no repository root on the include path)
    };
    for (const auto& c : cases)
    {
        auto s = makeEffectSlot (EffectKind::ember);
        s.params[(std::size_t) em::revision] = 0.0f;
        s.params[(std::size_t) em::drive] = c.drive;
        s.params[(std::size_t) em::quality] = c.quality;
        s.params[(std::size_t) em::characterParam (0)] = (float) c.character / (float) em::characters;
        if (c.character2 >= 0)
        {
            s.params[(std::size_t) em::bands] = 1.0f / 3.0f;
            s.params[(std::size_t) em::characterParam (1)] = (float) c.character2 / (float) em::characters;
            s.params[(std::size_t) em::bandParam (1, 1)] = c.drive;
        }
        const int n = (int) (1.6 * c.sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190kwSat::signal (l, r, c.sr);
        parity190kwSat::render (s, c.sr, l, r);
        std::uint64_t h = 1469598103934665603ull;
        double e = 0.0;
        for (int i = 0; i < n; ++i)
            for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int b = 0; b < 4; ++b) { h ^= (u >> (8 * b)) & 0xffu; h *= 1099511628211ull; }
                e += (double) v * v;
            }
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == c.hash); // the libm the fingerprints were taken with
       #endif
        CHECK (std::abs (e - c.energy) < 1e-6 * c.energy + 1e-9);
    }
}

namespace parity190kwSat
{
// the gain (dB) of a law band at small signal: a sine of `hz` at −40 dBFS through Ember (rms of the last half vs the input's)
inline double sineGainDb (const lynx::EffectSlot& s, double hz, double dbfs = -40.0, double sr = 48000.0)
{
    const int n = (int) (1.2 * sr);
    const double a = std::pow (10.0, dbfs / 20.0) * std::sqrt (2.0);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    for (int i = 0; i < n; ++i) l[(std::size_t) i] = r[(std::size_t) i] = (float) (a * std::sin (2.0 * M_PI * hz * i / sr));
    render (s, sr, l, r);
    double e = 0.0;
    for (int i = n / 2; i < n; ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
    return 10.0 * std::log10 (e / (n - n / 2)) - 20.0 * std::log10 (a / std::sqrt (2.0));
}
} // namespace parity190kwSat

TEST (parity190lg_ember_saturn_channel_clipper_butterworth_cuts_and_dc)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    // the reference's channel soft clipper (threshold 0 dB): small-signal slope 9/8, ceiling 1 from |x| = 4/3, odd, smooth
    CHECK (std::abs (em::saturnClip (1e-4f) / 1e-4f - 1.125f) < 1e-4f);
    CHECK (em::saturnClip (4.0f / 3.0f) == 1.0f && em::saturnClip (3.0f) == 1.0f && em::saturnClip (-3.0f) == -1.0f);
    CHECK (std::abs (em::saturnClip (0.5f) + em::saturnClip (-0.5f)) < 1e-7f);
    CHECK (std::abs (em::saturnClip (4.0f / 3.0f - 1e-3f) - 1.0f) < 1e-5f);
    // it acts on every style but the Subtle ones and the transformers (measured on the refcap030 level sweeps)
    for (const int st : { 0, 4, 17, 20, 21, 22 }) CHECK (! em::saturnClipStyle (st));
    for (const int st : { 1, 2, 3, 5, 6, 7, 18, 19 }) CHECK (em::saturnClipStyle (st));
    const auto slot = [] (int style, float referenceDrive) {
        auto s = makeEffectSlot (EffectKind::ember);
        s.params[(std::size_t) em::characterParam (0)] = (float) (style + 1) / (float) em::characters;
        s.params[(std::size_t) em::drive] = referenceDrive * 30.0f / 36.0f;
        s.params[(std::size_t) em::lawRate] = 1.0f; // the reference's High Quality off
        return s;
    };
    // Clean Tape at drive 0: the reference's small-signal gain is 1.1268 (+1.04 dB) at −39 dBFS (the compensation's
    // 1.0016 × the clipper's 9/8); Subtle Tape: its trim alone (+0.1 dB)
    CHECK (std::abs (parity190kwSat::sineGainDb (slot (5, 0.0f), 1000.0) - 1.04) < 0.06);
    CHECK (std::abs (parity190kwSat::sineGainDb (slot (4, 0.0f), 1000.0) - 0.10) < 0.06);
    // the cut filters' Q 1 is Butterworth: Warm Tube at 25 %, 16 kHz vs 1 kHz small-signal gain (the reference's third
    // octaves: +2.35 vs +1.79 dB; with the old resonant Q 1 the law gave +5.1 vs +0.6)
    const auto wt = slot (2, 0.25f);
    CHECK (std::abs (parity190kwSat::sineGainDb (wt, 16000.0) - parity190kwSat::sineGainDb (wt, 1000.0) - 0.56) < 0.5);
    // the law's DC blocker at 1.5 Hz: Subtle Tape (no EQ of its own) loses < 0.05 dB at 31 Hz against 1 kHz
    const auto stp = slot (4, 0.5f);
    CHECK (std::abs (parity190kwSat::sineGainDb (stp, 31.0) - parity190kwSat::sineGainDb (stp, 1000.0)) < 0.05);
}
