// REA_rodada101: LYNX's delay / modulation suite (core/EchoSuite.h) against the black-box measurements of the
// reference plugins (docs/analise/REA_rodada101.md): value laws, echo times, sync, feedback laws, mix laws, character
// filtering, modulation, frequency / pitch shift, multitap layout, and the substitutes' parameter translation.
// Tolerances are wide enough for any IEEE compiler (gcc / clang / zig, x86-64 / arm64).

#include "core/EchoSuite.h"

namespace es101
{
using lynx::EffectKind;
namespace es = lynx::echosuite;
constexpr double sr = 48000.0;
constexpr double pi = 3.141592653589793;

struct Stereo
{
    std::vector<float> l, r;
};

inline lynx::EffectParams defaults (EffectKind kind) { return lynx::makeEffectSlot (kind).params; }

inline std::vector<float> sine (double hz, double amp, int n, int silenceAfter = 0)
{
    std::vector<float> s ((std::size_t) (n + silenceAfter), 0.0f);
    for (int i = 0; i < n; ++i)
        s[(std::size_t) i] = (float) (amp * std::sin (2.0 * pi * hz * i / sr));
    return s;
}

inline std::vector<float> impulse (int n, float amp = 0.25f)
{
    std::vector<float> s ((std::size_t) n, 0.0f);
    s[0] = amp;
    return s;
}

inline Stereo run (EffectKind kind, const lynx::EffectParams& p, const std::vector<float>& in, double rate = sr, int block = 480, double bpm = 120.0)
{
    auto fx = lynx::makeEffect (kind);
    fx->prepare (rate);
    Stereo s { in, in };
    for (std::size_t off = 0; off < in.size(); off += (std::size_t) block)
    {
        const auto m = (int) std::min<std::size_t> ((std::size_t) block, in.size() - off);
        fx->process (s.l.data() + off, s.r.data() + off, m, p, bpm);
    }
    return s;
}

inline double amplitude (const std::vector<float>& x, double hz, std::size_t start, std::size_t len)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (std::size_t i = 0; i < len && start + i < x.size(); ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) len);
        const auto ph = 2.0 * pi * hz * (double) (start + i) / sr;
        re += w * x[start + i] * std::cos (ph);
        im += w * x[start + i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

// the strongest frequency in [lo, hi] over a window (fine scan of the projection)
inline double peakHz (const std::vector<float>& x, std::size_t start, std::size_t len, double lo, double hi, double step = 0.25)
{
    double best = lo, bestA = -1.0;
    for (auto f = lo; f <= hi; f += step)
        if (const auto a = amplitude (x, f, start, len); a > bestA)
        {
            bestA = a;
            best = f;
        }
    return best;
}

inline std::size_t argmaxAbs (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    to = to == 0 ? x.size() : std::min (to, x.size());
    std::size_t k = from;
    for (auto i = from; i < to; ++i)
        if (std::abs (x[i]) > std::abs (x[k]))
            k = i;
    return k;
}

inline double db (double a) { return 20.0 * std::log10 (std::max (a, 1e-15)); }

inline bool finite (const Stereo& s)
{
    for (std::size_t i = 0; i < s.l.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
            return false;
    return true;
}

inline float peak (const Stereo& s)
{
    float m = 0.0f;
    for (std::size_t i = 0; i < s.l.size(); ++i)
        m = std::max ({ m, std::abs (s.l[i]), std::abs (s.r[i]) });
    return m;
}

// Echoform set up for a clean measurement: wet only, no feedback / modulation / age
inline lynx::EffectParams echoform (es::echoform::Mode mode, double ms)
{
    namespace ef = es::echoform;
    auto p = defaults (EffectKind::echoform);
    p[ef::mix] = 1.0f;
    p[ef::mode] = ef::modeValue (mode);
    p[ef::feedback] = 0.0f;
    p[ef::modDepth] = 0.0f;
    p[ef::wow] = 0.0f;
    p[ef::flutter] = 0.0f;
    p[ef::age] = 0.0f;
    p[ef::timeL] = p[ef::timeR] = ef::timeValue (ms);
    p[ef::engineLaw] = 0.0f; // round 190dl: these laws are the classic characters' (the measured tape stage: Echo190dlTests)
    return p;
}
} // namespace es101

TEST (echosuite101_value_laws_follow_the_measurements)
{
    namespace es = lynx::echosuite;
    namespace ef = es::echoform;
    CHECK (std::abs (ef::timeMs (0.5f) - 500.0) < 1e-6 && std::abs (ef::timeMs (0.76f) - 1000.0) < 0.01);
    CHECK (std::abs (ef::timeMs (0.55f) - 596.15) < 0.1 && std::abs (ef::timeMs (0.9f) - 4000.0) < 0.1 && ef::timeMs (1.0f) == 20000.0);
    for (const auto ms : { 0.0, 25.0, 300.0, 980.8, 1333.3, 2250.0, 8333.3, 20000.0 })
        CHECK (std::abs (ef::timeMs (ef::timeValue (ms)) - ms) < 0.05);
    CHECK (std::abs (ef::modRateHz (0.2738341f) - 0.50) < 0.01 && std::abs (ef::modRateHz (0.5f) - 2.0) < 0.01);
    CHECK (std::abs (ef::shiftHz (0.75f) - 31.25) < 1e-3 && std::abs (ef::shiftHz (0.1f) + 327.68) < 0.01);
    CHECK (std::abs (ef::shiftDetuneHz (0.58f) - 0.1) < 0.01 && std::abs (ef::shiftDetuneHz (0.25f) + 1.7678) < 0.01);
    CHECK (ef::feedbackGain (0.5f) == 1.0 && ef::pitchSemis (0.625f) == 6.0);
    CHECK (ef::syncKind (0.25f) == 0 && ef::syncKind (0.5f) == 1 && ef::syncKind (0.75f) == 2 && ef::syncKind (1.0f) == 3);
    CHECK (ef::noteIndex (0.45f) == 4 && ef::noteBeats (4, 1) == 1.0 && ef::noteBeats (4, 2) == 1.5 && std::abs (ef::noteBeats (4, 3) - 2.0 / 3.0) < 1e-12);
    for (int m = 0; m < ef::modeCount; ++m)
        CHECK ((int) ef::modeOf (ef::modeValue ((ef::Mode) m)) == m);

    namespace sp = es::spiral;
    CHECK (std::abs (sp::delayMs (0.5f) - 100.0) < 0.2 && std::abs (sp::delayMs (0.0009900099f) - 0.01) < 1e-3);
    CHECK (std::abs (sp::delayMs (sp::delayValue (384.55)) - 384.55) < 0.01);
    CHECK (sp::syncIndex (5.5f / 23.0f) == 5 && sp::syncIndex (1.0f) == 23 && sp::syncBeats (5) == 1.0 && sp::syncBeats (13) == 1.5 && std::abs (sp::syncBeats (20) - 2.0 / 3.0) < 1e-12);
    CHECK (sp::syncBeats (1) == 1.0 / 16.0 && sp::syncBeats (8) == 8.0 && sp::syncBeats (15) == 6.0 && std::abs (sp::syncBeats (23) - 16.0 / 3.0) < 1e-12);
    CHECK (std::abs (sp::loopGain (0.5f) - 0.75) < 1e-6 && std::abs (sp::lowCutHz (0.0604027f) - 200.0) < 0.5);

    namespace ob = es::orbit;
    CHECK (std::abs (ob::depthMs (0.5f) - 10.0) < 0.05 && std::abs (ob::rateHz (0.626f) - 0.10) < 0.01 && ob::rateHz (0.5f) == 0.0);
    CHECK (std::abs (ob::manualMs (0.9f) - 20.48) < 0.01 && std::abs (ob::manualMs (0.1f) + 20.48) < 0.01);
    for (int m = 0; m < ob::modeCount; ++m)
        CHECK ((int) ob::modeOf ((float) (m + 1) / 11.0f) == m);
    CHECK (ob::modeOf (0.0f) == ob::Mode::rise && ob::modeOf (0.0909091f) == ob::Mode::rise && ob::modeOf (1.0f) == ob::Mode::symphonic);

    namespace lt = es::lattice;
    const int expected[10] = { 1, 1, 2, 3, 3, 3, 4, 8, 8, 16 };
    for (int k = 0; k < 10; ++k)
        CHECK (lt::tapCount (((float) k + 0.5f) / 24.0f) == expected[k]);
    CHECK (lt::tapCount (0.0416666679f) == 1 && lt::tapCount (1.0f) == 16);
}

TEST (echosuite101_echoform_time_sync_feedback_and_mix)
{
    using namespace es101;
    namespace ef = es::echoform;
    // echo position (digital character: the 8 kHz low-pass adds a few samples of group delay)
    for (const auto ms : { 10.0, 100.0, 500.0 })
    {
        const auto y = run (EffectKind::echoform, echoform (ef::Mode::digital, ms), impulse ((int) (sr * ms / 1000.0) + 4800));
        const auto at = (double) argmaxAbs (y.l);
        CHECK (at >= ms * 48.0 && at <= ms * 48.0 + 14.0);
    }
    // tempo sync at 120 BPM: 1/4 = 500 ms, dotted 750 ms, triplet 333.3 ms
    for (const auto& [kind, ms] : { std::pair<float, double> { 0.5f, 500.0 }, { 0.75f, 750.0 }, { 1.0f, 333.333 } })
    {
        auto p = echoform (ef::Mode::clean, 300.0);
        p[ef::syncL] = p[ef::syncR] = kind;
        p[ef::noteL] = p[ef::noteR] = 0.45f;
        const auto y = run (EffectKind::echoform, p, impulse (40000));
        CHECK (std::abs ((double) argmaxAbs (y.l) - ms * 48.0) <= 3.0);
    }
    // loop gain = Feedback / 100 % (sine burst, clean character): 25 % of the knob = 50 % = −6 dB per pass
    {
        auto p = echoform (ef::Mode::clean, 100.0);
        p[ef::feedback] = 0.25f;
        const auto y = run (EffectKind::echoform, p, sine (1000.0, 0.1, 960, 30000));
        const auto a1 = amplitude (y.l, 1000.0, 4800 + 100, 760), a2 = amplitude (y.l, 1000.0, 9600 + 100, 760), a3 = amplitude (y.l, 1000.0, 14400 + 100, 760);
        CHECK (std::abs (db (a1 / 0.1)) < 0.3);
        CHECK (std::abs (db (a2 / a1) + 6.02) < 0.3 && std::abs (db (a3 / a2) + 6.02) < 0.3);
    }
    // feedback 200 %: grows but stays bounded (soft limit in the loop)
    {
        auto p = echoform (ef::Mode::clean, 50.0);
        p[ef::feedback] = 1.0f;
        const auto y = run (EffectKind::echoform, p, sine (1000.0, 0.5, 4800, (int) sr * 4));
        CHECK (finite (y) && peak (y) < 4.0f && peak (y) > 0.5f);
    }
    // equal-power mix: 25 % → dry −0.69 dB / wet −8.34 dB, 50 % → −3.01 / −3.01 dB (clean echo at 300 ms)
    for (const auto& [m, dryDb, wetDb] : { std::tuple<float, double, double> { 0.25f, -0.69, -8.34 }, { 0.5f, -3.01, -3.01 } })
    {
        auto p = echoform (ef::Mode::clean, 300.0);
        p[ef::mix] = m;
        const auto y = run (EffectKind::echoform, p, sine (1000.0, 0.25, 12000, 20000));
        CHECK (std::abs (db (amplitude (y.l, 1000.0, 2400, 7200) / 0.25) - dryDb) < 0.15);
        CHECK (std::abs (db (amplitude (y.l, 1000.0, 14400 + 4000, 7000) / 0.25) - wetDb) < 0.3);
    }
}

TEST (echosuite101_echoform_characters)
{
    using namespace es101;
    namespace ef = es::echoform;
    const auto echoLevel = [] (ef::Mode mode, double ms, double hz, float age = 0.0f) {
        auto p = echoform (mode, ms);
        p[ef::age] = age;
        const auto start = (std::size_t) (ms * 48.0) + 2400;
        const auto y = run (EffectKind::echoform, p, sine (hz, 0.05, (int) start + 4800));
        return db (amplitude (y.l, hz, start, 2400) / 0.05);
    };
    // tape: the top end falls with the delay time (tape speed): 8 kHz −6 dB at 100 ms, much lower at 1 s
    const auto tape100 = echoLevel (ef::Mode::tape, 100.0, 8000.0), tape1000 = echoLevel (ef::Mode::tape, 1000.0, 8000.0);
    CHECK (tape100 < -4.0 && tape100 > -9.0 && tape1000 < tape100 - 8.0);
    CHECK (echoLevel (ef::Mode::tape, 100.0, 40.0) < -5.0); // the low end too
    // bucket brigade ≈ 1.6 kHz and analog ≈ 3.4 kHz steep low-passes; digital ≈ 8 kHz; clean is flat
    CHECK (echoLevel (ef::Mode::bucket, 100.0, 500.0) > -1.0 && echoLevel (ef::Mode::bucket, 100.0, 4000.0) < -40.0);
    CHECK (echoLevel (ef::Mode::analog, 100.0, 2000.0) > -1.5 && echoLevel (ef::Mode::analog, 100.0, 6000.0) < -30.0);
    CHECK (echoLevel (ef::Mode::digital, 100.0, 4000.0) > -1.5 && echoLevel (ef::Mode::digital, 100.0, 12000.0) < -30.0);
    CHECK (std::abs (echoLevel (ef::Mode::clean, 100.0, 12000.0)) < 1.0);
    // clean's age is a low-pass (≈ 5 kHz at 50 %)
    CHECK (echoLevel (ef::Mode::clean, 100.0, 8000.0, 0.5f) < -20.0 && echoLevel (ef::Mode::clean, 100.0, 1000.0, 0.5f) > -1.0);
    // tape saturation: 2nd and 3rd harmonics of a −10 dBFS sine near the measured −36 / −42 dB
    {
        auto p = echoform (ef::Mode::tape, 50.0);
        const auto y = run (EffectKind::echoform, p, sine (1000.0, 0.3, 14400));
        const auto h1 = amplitude (y.l, 1000.0, 7200, 4800), h2 = amplitude (y.l, 2000.0, 7200, 4800), h3 = amplitude (y.l, 3000.0, 7200, 4800);
        CHECK (std::abs (db (h2 / h1) + 37.0) < 4.0 && std::abs (db (h3 / h1) + 43.5) < 4.0);
    }
    // bucket noise at full age ≈ −60 dBFS (silence in)
    {
        auto p = echoform (ef::Mode::bucket, 50.0);
        p[ef::age] = 1.0f;
        const auto y = run (EffectKind::echoform, p, std::vector<float> (48000, 0.0f));
        double e = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i)
            e += (double) y.l[i] * y.l[i];
        const auto rmsDb = 10.0 * std::log10 (e / 24000.0);
        CHECK (rmsDb > -72.0 && rmsDb < -55.0);
    }
}

TEST (echosuite101_echoform_shift_pitch_and_modulation)
{
    using namespace es101;
    namespace ef = es::echoform;
    // shifter: every echo moves by the shift (+31.25 Hz); the detune splits the sides (+1 Hz left, −1 Hz right)
    {
        auto p = echoform (ef::Mode::shifter, 100.0);
        p[ef::shift] = 0.75f;
        p[ef::shiftDetune] = 0.5f;
        const auto y = run (EffectKind::echoform, p, sine (1000.0, 0.1, 24000));
        CHECK (std::abs (peakHz (y.l, 9600, 9600, 1000.0, 1060.0) - 1031.25) < 0.5);
        p[ef::shift] = 0.5f;
        p[ef::shiftDetune] = 0.7f; // +1.01 Hz
        const auto z = run (EffectKind::echoform, p, sine (1000.0, 0.1, 48000));
        CHECK (std::abs (peakHz (z.l, 9600, 38400, 995.0, 1005.0, 0.05) - 1001.01) < 0.1);
        CHECK (std::abs (peakHz (z.r, 9600, 38400, 995.0, 1005.0, 0.05) - 998.99) < 0.1);
    }
    // pitch: +6 semitones → 1414.2 Hz; echo time kept (the window's delay is taken off)
    {
        auto p = echoform (ef::Mode::pitch, 100.0);
        p[ef::pitch] = 0.625f;
        const auto y = run (EffectKind::echoform, p, sine (1000.0, 0.1, 24000));
        CHECK (std::abs (peakHz (y.l, 12000, 9600, 1380.0, 1450.0) - 1414.2) < 2.0);
        p[ef::pitch] = 0.5f;
        const auto imp = run (EffectKind::echoform, p, impulse (9600));
        CHECK (std::abs ((double) argmaxAbs (imp.l) - 4800.0) < 40.0);
    }
    // the LFO of the digital character: 2 Hz at 100 % depth = ±6.1 ms → about ±0.077 relative pitch (±127 cents)
    {
        auto p = echoform (ef::Mode::digital, 300.0);
        p[ef::modRate] = 0.5f;
        p[ef::modDepth] = 1.0f;
        const auto y = run (EffectKind::echoform, p, sine (1000.0, 0.1, 96000));
        double lo = 1e9, hi = 0.0;
        for (int k = 0; k < 8; ++k)
        {
            const auto f = peakHz (y.l, (std::size_t) (24000 + k * 3000), 2400, 900.0, 1100.0, 1.0);
            lo = std::min (lo, f);
            hi = std::max (hi, f);
        }
        CHECK (hi - lo > 100.0 && hi - lo < 200.0);
    }
    // all characters and styles stay finite at other rates and block sizes, with everything turned up
    for (int m = 0; m < ef::modeCount; ++m)
        for (int st = 0; st < ef::styleCount; ++st)
        {
            auto p = echoform ((ef::Mode) m, 30.0);
            p[ef::style] = ef::styleValue ((ef::Style) st);
            p[ef::feedback] = 0.7f;
            p[ef::age] = 1.0f;
            p[ef::modDepth] = p[ef::wow] = p[ef::flutter] = p[ef::diffusion] = p[ef::ducking] = 1.0f;
            p[ef::pitch] = 0.8f;
            p[ef::drive] = 1.0f;
            const auto y = run (EffectKind::echoform, p, sine (440.0, 0.5, 9000, 3000), m % 2 == 0 ? 44100.0 : 96000.0, 64 + 37 * st);
            CHECK (finite (y) && peak (y) < 8.0f);
        }
}

TEST (echosuite101_spiral_echo)
{
    using namespace es101;
    namespace sp = es::spiral;
    auto base = defaults (EffectKind::spiralEcho);
    base[sp::mix] = 1.0f;
    base[sp::lowCut] = 0.0f;
    base[sp::highCut] = 1.0f;
    base[sp::feedback] = 0.0f;
    base[sp::delay] = sp::delayValue (100.0);
    // echo time
    CHECK (std::abs ((double) argmaxAbs (run (EffectKind::spiralEcho, base, impulse (9600)).l) - 4800.0) < 8.0);
    // loop gain 1.5 × feedback (the classic loop; round 190do's measured loop limits at 0.256, so a 0.1 tone loses
    // 0.4 dB a pass there: tests/Mod190doTests.inc checks its gain at a lower level)
    for (const auto fb : { 0.25f, 0.5f })
    {
        auto p = base;
        p[sp::law] = 0.0f;
        p[sp::feedback] = fb;
        const auto y = run (EffectKind::spiralEcho, p, sine (1000.0, 0.1, 1920, 20000));
        const auto a1 = amplitude (y.l, 1000.0, 4800 + 300, 1400), a2 = amplitude (y.l, 1000.0, 9600 + 300, 1400);
        CHECK (std::abs (db (a1 / 0.1)) < 0.3 && std::abs (db (a2 / a1) - db (1.5 * fb)) < 0.3);
    }
    // the shift accumulates per repeat; in stereo the right side hears each repeat with the opposite shift
    {
        auto p = base;
        p[sp::feedback] = 0.5f;
        p[sp::shift] = 0.5f + 0.5f * (float) std::pow (0.1, 0.2); // +100 Hz
        p[sp::delay] = sp::delayValue (200.0);
        const auto y = run (EffectKind::spiralEcho, p, sine (1000.0, 0.2, 4800, 48000));
        for (int k = 1; k <= 3; ++k)
        {
            const auto at = (std::size_t) (k * 9600 + 1080);
            CHECK (std::abs (peakHz (y.l, at, 3840, 1000.0 + 100.0 * k - 30.0, 1000.0 + 100.0 * k + 30.0, 0.5) - (1000.0 + 100.0 * k)) < 1.0);
            CHECK (std::abs (peakHz (y.r, at, 3840, 800.0 + 100.0 * k - 30.0, 800.0 + 100.0 * k + 30.0, 0.5) - (800.0 + 100.0 * k)) < 1.0);
        }
    }
    // tempo sync index 5 = 1/4 = 500 ms at 120 BPM; the mix is a linear crossfade
    {
        auto p = base;
        p[sp::sync] = 5.5f / 23.0f; // 1/4
        CHECK (std::abs ((double) argmaxAbs (run (EffectKind::spiralEcho, p, impulse (30000)).l) - 24000.0) < 8.0);
        p = base;
        p[sp::mix] = 0.25f;
        p[sp::delay] = sp::delayValue (300.0);
        const auto y = run (EffectKind::spiralEcho, p, sine (1000.0, 0.25, 12000, 20000));
        CHECK (std::abs (db (amplitude (y.l, 1000.0, 2400, 7200) / 0.25) + 2.50) < 0.15);
    }
    // feedback 100 % stays bounded
    {
        auto p = base;
        p[sp::feedback] = 1.0f;
        p[sp::shift] = 0.7f;
        const auto y = run (EffectKind::spiralEcho, p, sine (300.0, 0.5, 4800, (int) sr * 3));
        CHECK (finite (y) && peak (y) < 3.0f);
    }
}

TEST (echosuite101_orbit_modes)
{
    using namespace es101;
    namespace ob = es::orbit;
    auto base = defaults (EffectKind::orbit);
    base[ob::mix] = 1.0f;
    base[ob::feedback] = 0.5f;
    base[ob::manual] = 0.5f;
    // manual delay (depth 0): +20 ms of manual → echo at ≈ 20.5 ms; equal-power mix (the classic voices: round 190do's
    // measured sweep reads from the manual delay alone, tests/Mod190doTests.inc)
    {
        auto p = base;
        p[ob::law] = 0.0f;
        p[ob::depth] = 0.0f;
        p[ob::manual] = 0.5f + 0.5f * (float) std::pow (20.0 / 50.0, 0.25);
        p[ob::mode] = ob::modeValue (ob::Mode::riseFall);
        p[ob::mix] = 0.5f;
        const auto y = run (EffectKind::orbit, p, impulse (4800));
        CHECK (std::abs (y.l[0] - 0.25f * 0.70711f) < 1e-3f);
        const auto at = (double) argmaxAbs (y.l, 100);
        CHECK (at > 20.0 * 48.0 && at < 21.5 * 48.0);
    }
    // negative feedback keeps its sign (second echo inverted)
    {
        auto p = base;
        p[ob::depth] = 0.0f;
        p[ob::feedback] = 0.25f;
        p[ob::manual] = 0.5f + 0.5f * (float) std::pow (5.0 / 50.0, 0.25);
        p[ob::mode] = ob::modeValue (ob::Mode::riseFall);
        const auto y = run (EffectKind::orbit, p, impulse (4800));
        const auto first = argmaxAbs (y.l), second = argmaxAbs (y.l, first + 100);
        CHECK (y.l[first] > 0.0f && y.l[second] < 0.0f && std::abs (y.l[second] / y.l[first] + 0.247f) < 0.06f);
    }
    // every mode: the delay sweeps within the measured range for depth 10 ms (impulse train, strongest tap per impulse)
    for (int m = 0; m < ob::modeCount; ++m)
    {
        auto p = base;
        p[ob::mode] = ob::modeValue ((ob::Mode) m);
        p[ob::rate] = 0.5f + 0.5f * (float) std::pow (0.05, 1.0 / 3.333); // 0.5 Hz
        p[ob::depth] = (float) std::pow (0.2, 1.0 / 2.322);              // 10 ms
        std::vector<float> train ((std::size_t) sr * 4, 0.0f);
        for (std::size_t i = 0; i < train.size(); i += 2400)
            train[i] = 0.25f;
        const auto y = run (EffectKind::orbit, p, train);
        double lo = 1e9, hi = 0.0;
        for (std::size_t k = 4; k + 1 < train.size() / 2400; ++k)
        {
            const auto at = argmaxAbs (y.l, k * 2400 + 1, (k + 1) * 2400);
            lo = std::min (lo, (double) (at - k * 2400) / 48.0);
            hi = std::max (hi, (double) (at - k * 2400) / 48.0);
        }
        const auto tz = m == (int) ob::Mode::throughZeroPlus || m == (int) ob::Mode::throughZeroMinus;
        CHECK (finite (y) && lo >= 0.0 && hi <= (tz ? 25.0 : 10.6) && hi - lo > (tz ? 10.0 : 1.0));
    }
}

TEST (echosuite101_lattice_taps_feedback_and_layout)
{
    using namespace es101;
    namespace lt = es::lattice;
    auto base = defaults (EffectKind::latticeMod);
    base[lt::mix] = 1.0f;
    base[lt::depth] = 0.0f;
    base[lt::feedback] = 0.0f;
    base[lt::delay] = 0.1f; // 100 ms
    base[lt::spread] = 0.5f;
    // taps at delay × (1 − spread · k / N), gain 1/√N (in phase: round 172d, the measured UberMod wet is not inverted)
    for (const auto& [v, n] : { std::pair<float, int> { 2.5f / 24.0f, 2 }, { 6.5f / 24.0f, 4 } })
    {
        auto p = base;
        p[lt::taps] = v;
        const auto y = run (EffectKind::latticeMod, p, impulse (6000));
        for (int k = 0; k < n; ++k)
        {
            const auto at = (std::size_t) std::lround (lt::tapMs (p, k, n, false) * 48.0);
            CHECK (std::abs (lt::tapMs (p, k, n, false) - 100.0 * (1.0 - 0.5 * k / n)) < 1e-3);
            const auto found = argmaxAbs (y.l, at - 20, at + 20);
            CHECK (found >= at - 2 && found <= at + 2 && y.l[found] > 0.0f);
            CHECK (std::abs (std::abs (y.l[found]) / 0.25 - 1.0 / std::sqrt ((double) n)) < 0.12);
        }
    }
    // skew −100 %: the left taps at half the delay
    {
        auto p = base;
        p[lt::taps] = 1.5f / 24.0f;
        p[lt::skew] = 0.0f;
        const auto y = run (EffectKind::latticeMod, p, impulse (6000));
        CHECK (std::abs ((double) argmaxAbs (y.l) - 2400.0) <= 2.0 && std::abs ((double) argmaxAbs (y.r) - 4800.0) <= 2.0);
    }
    // loop gain = feedback (no rotation)
    {
        auto p = base;
        p[lt::taps] = 1.5f / 24.0f;
        p[lt::feedback] = 0.5f;
        p[lt::rotate] = 0.0f;
        const auto y = run (EffectKind::latticeMod, p, sine (1000.0, 0.1, 1920, 20000));
        const auto a1 = amplitude (y.l, 1000.0, 4800 + 300, 1400), a2 = amplitude (y.l, 1000.0, 9600 + 300, 1400);
        CHECK (std::abs (db (a1 / 0.1)) < 0.5 && std::abs (db (a2 / a1) + 6.02) < 0.4);
    }
    // everything on: finite and bounded
    {
        auto p = base;
        p[lt::taps] = 1.0f;
        p[lt::depth] = p[lt::detuneDepth] = p[lt::vibratoDepth] = p[lt::overMod] = 1.0f;
        p[lt::feedback] = p[lt::diffOn] = p[lt::driveOn] = p[lt::diffusion] = p[lt::random] = p[lt::halfSpeed] = 1.0f;
        p[lt::sync] = 0.5f;
        const auto y = run (EffectKind::latticeMod, p, sine (220.0, 0.7, 24000, 24000), 44100.0, 333);
        CHECK (finite (y) && peak (y) < 10.0f);
    }
    // the four panels: every parameter has a control inside the body
    for (const auto kind : { EffectKind::echoform, EffectKind::spiralEcho, EffectKind::orbit, EffectKind::latticeMod })
    {
        const auto& l = lynx::plugui::layoutFor (kind);
        std::vector<bool> placed (lynx::effectInfo (kind).params.size(), false);
        bool inside = true;
        for (const auto& item : l.items)
        {
            inside = inside && item.x >= 0 && item.y >= 0 && item.x + item.w <= (float) l.width + 0.5f && item.y + item.h <= (float) l.height + 0.5f;
            if (lynx::plugui::isValueControl (item.type) && item.param != nullptr) // (page tabs have none)
            {
                const auto p = lynx::plugui::paramIndex (kind, item.param);
                CHECK (p >= 0);
                if (p >= 0 && p < (int) placed.size())
                    placed[(std::size_t) p] = true;
            }
        }
        // round 137: Echoform's rhythm tap levels are edited on a display (one bar per tap); round 144: the "rhythmedit"
        // display (the rhythm editor) edits both each tap's level and its time
        if (kind == EffectKind::echoform)
            for (const auto& item : l.items)
                if (item.type == lynx::plugui::Item::display && item.display != nullptr && std::strcmp (item.display, "rhythmedit") == 0)
                {
                    for (int t = lynx::echosuite::echoform::rhythmTap1; t <= lynx::echosuite::echoform::rhythmTapLast; ++t)
                        placed[(std::size_t) t] = true;
                    for (int t = lynx::echosuite::echoform::rhythmTime1; t <= lynx::echosuite::echoform::rhythmTimeLast; ++t)
                        placed[(std::size_t) t] = true;
                }
        CHECK (inside && std::all_of (placed.begin(), placed.end(), [] (bool b) { return b; }));
        // controls that swap in place (style / mode dependent) never overlap while both are shown
        auto params = lynx::makeEffectSlot (kind).params;
        for (int a = 0; a < 12; ++a)
            for (int b = 0; b < 5; ++b)
                for (int c = 0; c < 2; ++c)
                {
                    if (kind == EffectKind::echoform)
                    {
                        params[es::echoform::mode] = ((float) a + 0.5f) / 12.0f;
                        params[es::echoform::style] = ((float) b + 0.5f) / 5.0f;
                        params[es::echoform::syncL] = params[es::echoform::syncR] = c == 0 ? 0.25f : 0.75f;
                    }
                    for (int page = 0; page < std::max (1, (int) l.pages.size()); ++page)
                    {
                        std::vector<const lynx::plugui::LayoutItem*> shown;
                        for (const auto& item : l.items)
                            if (lynx::plugui::isValueControl (item.type) && lynx::plugui::isShown (kind, item, page, params))
                                shown.push_back (&item);
                        for (std::size_t i = 0; i < shown.size(); ++i)
                            for (std::size_t j = i + 1; j < shown.size(); ++j)
                            {
                                const auto& p = *shown[i];
                                const auto& q = *shown[j];
                                const auto overlap = p.x + 0.5f < q.x + q.w && q.x + 0.5f < p.x + p.w && p.y + 0.5f < q.y + q.h && q.y + 0.5f < p.y + p.h;
                                if (overlap)
                                    std::printf ("  %s: '%s' overlaps '%s'\n", lynx::effectInfo (kind).name, p.param ? p.param : "-", q.param ? q.param : "-");
                                CHECK (! overlap);
                            }
                    }
                }
        CHECK (std::find (lynx::plugui::ownLayoutKinds().begin(), lynx::plugui::ownLayoutKinds().end(), kind) != lynx::plugui::ownLayoutKinds().end());
    }
    CHECK (lynx::formatEffectParam (EffectKind::echoform, es::echoform::timeL, 0.3f, 120.0) == "300.0 ms");
    CHECK (lynx::formatEffectParam (EffectKind::echoform, es::echoform::mode, es::echoform::modeValue (es::echoform::Mode::bucket), 120.0) == "Bucket brigade");
    CHECK (lynx::formatEffectParam (EffectKind::spiralEcho, es::spiral::sync, 13.5f / 23.0f, 120.0) == "1/4.");
    CHECK (lynx::formatEffectParam (EffectKind::spiralEcho, es::spiral::sync, 20.5f / 23.0f, 120.0) == "1/4T");
    CHECK (lynx::formatEffectParam (EffectKind::orbit, es::orbit::mode, 3.0f / 11.0f, 120.0) == "Through-zero +");
}

TEST (echosuite101_substitutes_translate_valhalla_states)
{
    using namespace substtest;
    namespace ef = lynx::echosuite::echoform;
    const auto slotFor = [] (const char* name, const char* classHex, const std::string& xml, const std::string& key, lynx::EffectKind kind) {
        lynx::ForeignPlugin f;
        f.format = lynx::ForeignPlugin::Format::vst3;
        f.classId = classIdOf (classHex);
        f.hasClassId = true;
        f.name = name;
        f.path = std::string ("C:\\Program Files\\Common Files\\VST3\\") + name + ".vst3";
        f.state = flVst3State (juceXmlComponent (xml));
        const auto id = lynx::pluginIdentity (f);
        const auto* entry = lynx::findPluginSubstitute (id, false);
        CHECK (entry != nullptr && entry->key == key && entry->effect == kind);
        lynx::EffectSlot slot;
        slot.kind = lynx::EffectKind::plugin;
        slot.plugin.name = f.name;
        slot.plugin.foreign = f;
        const auto before = slot.plugin;
        CHECK (lynx::substituteEffectSlot (slot, id));
        CHECK (slot.kind == kind && slot.isSubstitute() && slot.mix == 1.0f);
        CHECK (slot.substituteFor == before && slot.substituteFor.foreign.state == f.state); // the original, byte for byte
        return slot;
    };

    // ValhallaDelay (class ID derived from its VST2 ID "dLay"): Digital mode, 1/8 dotted sync on the left, 100 ms right
    const auto d = slotFor ("ValhallaDelay", "64545356614c7679616c68616c6c6164",
                            "<ValhallaDelay pluginVersion=\"3.0.5\" presetName=\"Default\" Mix=\"0.42\" DelayStyle=\"0.2\" DelayLSync=\"0.75\" "
                            "DelayLNote=\"0.3\" DelayL_Ms=\"0.3\" DelayRSync=\"0.25\" DelayRNote=\"0.2\" DelayR_Ms=\"0.1\" Feedback=\"0.35\" "
                            "Age=\"0.25\" HighCut=\"0.5\" ModRate=\"0.2738341093063354\" Mode=\"0.1875\" Era=\"0.3333\" Ducking=\"0.1\" mixLock=\"0\"/>",
                            "valhalla-delay", lynx::EffectKind::echoform);
    CHECK (std::abs (d.params[ef::mix] - 0.42f) < 1e-6f && ef::styleOf (d.params[ef::style]) == ef::Style::dual);
    CHECK (ef::modeOf (d.params[ef::mode]) == ef::Mode::digital && std::abs (d.params[ef::feedback] - 0.35f) < 1e-6f);
    CHECK (std::abs (ef::sideMs (d.params, false, 120.0) - 375.0) < 1e-6 && std::abs (ef::sideMs (d.params, true, 120.0) - 100.0) < 1e-3);
    CHECK (std::abs (d.params[ef::age] - 0.25f) < 1e-6f && std::abs (d.params[ef::highCut] - 0.5f) < 1e-6f && std::abs (d.params[ef::ducking] - 0.1f) < 1e-6f);
    // modes onto characters: Ghost → shifter, RevPitch → reverse, Quartz → clean, Analog → analog, Tape (default 1/24)
    for (const auto& [value, mode] : { std::pair<const char*, ef::Mode> { "0.2291667", ef::Mode::shifter }, { "0.3125", ef::Mode::reverse },
                                       { "0.5625", ef::Mode::clean }, { "0.7291667", ef::Mode::analog }, { "0.0416666679084301", ef::Mode::tape } })
    {
        const auto s = slotFor ("ValhallaDelay", "64545356614c7679616c68616c6c6164", std::string ("<ValhallaDelay Mode=\"") + value + "\"/>",
                                "valhalla-delay", lynx::EffectKind::echoform);
        CHECK (ef::modeOf (s.params[ef::mode]) == mode);
    }

    namespace sp = lynx::echosuite::spiral;
    const auto f = slotFor ("ValhallaFreqEcho", "4654535645717668616c68616c6c6166",
                            "<MYPLUGINSETTINGS pluginVersion=\"1\" wetDryMix=\"0.3\" shift=\"0.8155\" delay=\"0.5\" sync=\"0.0\" feedback=\"0.6\" "
                            "lowCut=\"0.06040268391370773\" highCut=\"0.3311036825180054\" stereo=\"0.0\"/>",
                            "valhalla-freqecho", lynx::EffectKind::spiralEcho);
    CHECK (std::abs (f.params[sp::mix] - 0.3f) < 1e-6f && std::abs (sp::shiftHz (f.params[sp::shift]) - 100.0) < 0.5);
    CHECK (std::abs (sp::delayMs (f.params[sp::delay]) - 100.0) < 0.2 && f.params[sp::stereo] == 0.0f && std::abs (f.params[sp::feedback] - 0.6f) < 1e-6f);

    namespace ob = lynx::echosuite::orbit;
    const auto o = slotFor ("ValhallaSpaceModulator", "535453564d707664616c68616c6c6173",
                            "<ValhallaSpaceModulator wetDry=\"0.7\" rate=\"0.626\" depth=\"0.5\" feedback=\"0.25\" manual=\"0.5\" Mode=\"0.4545454680919647\"/>",
                            "valhalla-spacemodulator", lynx::EffectKind::orbit);
    CHECK (ob::modeOf (o.params[ob::mode]) == ob::Mode::tri && std::abs (o.params[ob::mix] - 0.7f) < 1e-6f && ob::feedbackGain (o.params[ob::feedback]) < 0.0);

    namespace lt = lynx::echosuite::lattice;
    const auto u = slotFor ("ValhallaUberMod", "7554535665627672616c68616c6c6175",
                            "<ValhallaUberMod Mix=\"0.6\" Depth=\"0.4\" Delay=\"0.25\" Feedback=\"0.3\" type=\"0.25\" DiffEnable=\"1.0\" "
                            "ColorMode=\"1.0\" DelaySync=\"0.5\" InputPan=\"0.5\"/>",
                            "valhalla-ubermod", lynx::EffectKind::latticeMod);
    CHECK (lt::tapCount (u.params[lt::taps]) == 4 && std::abs (lt::delayMs (u.params[lt::delay]) - 250.0) < 1e-3);
    CHECK (u.params[lt::diffOn] == 1.0f && u.params[lt::colour] == 1.0f && u.params[lt::sync] == 0.0f);
    // round 144: DelaySync onto its own law (0.5 = index 8 = 1/16 dotted)
    CHECK (std::abs (u.params[lt::delaySync] - 0.5f) < 1e-6f && lt::delaySyncIndex (u.params[lt::delaySync]) == 8);

    // by name (no class ID), and the map laws of the table: steps / curve
    lynx::ForeignPlugin byName;
    byName.format = lynx::ForeignPlugin::Format::vst2;
    byName.name = "ValhallaUberMod";
    const auto* e = lynx::findPluginSubstitute (lynx::pluginIdentity (byName), false);
    CHECK (e != nullptr && e->effect == lynx::EffectKind::latticeMod);
    int errors = -1;
    const auto t = lynx::parseSubstituteTable ("[x]\nlynx = Echoform\nnames = X\nmap = xml:A -> Mode | steps 0:0.1 0.33:0.2 0.66:0.3\n"
                                               "map = xml:B -> Feedback | curve 0:0 0.5:0.1 1:1\nmap = xml:C -> Mix | curve 1:0 0.5:1\n"
                                               "map = xml:D -> Mix | steps\n",
                                               &errors);
    CHECK (errors == 2 && t.size() == 1 && t[0].params.size() == 2);
    if (t.size() == 1 && t[0].params.size() == 2)
    {
        lynx::ForeignPlugin x;
        x.format = lynx::ForeignPlugin::Format::vst3;
        x.name = "X";
        x.state = flVst3State (juceXmlComponent ("<X A=\"0.5\" B=\"0.75\"/>"));
        const auto id = lynx::pluginIdentity (x);
        int translated = 0;
        const auto s = lynx::makeSubstituteSlot (t[0], &id, &translated);
        CHECK (translated == 2 && std::abs (s.params[ef::mode] - 0.2f) < 1e-6f && std::abs (s.params[ef::feedback] - 0.55f) < 1e-6f);
    }
}
