// Round 190ca: Nebula's cloud law (core/NebulaCloud.h, a port of an MIT-licensed model of six delay / reverb networks).
// New instances run it in the six modes it covers; a project or preset saved before keeps the classic engine in every
// mode (parameter 14, "Law"). What is checked: the parameter and its load rule, that the classic law still renders
// what it rendered (a fingerprint of four settings taken from the build whose classic renders are byte-identical to
// the build before the round: /mnt/e/lynx-work/nebula190ca/HANDOFF.md), the cloud law's own laws, its stability at
// every extreme, its independence of the sample rate, and a change of engine while playing.
#include "core/EffectPresets.h"
#include "core/LynxVerbs.h"

namespace nebula190ca
{
using lynx::EffectKind;
using verbs100::Stereo;
constexpr std::size_t law = (std::size_t) lynx::verbs::nebula::law;
constexpr int cloudModes[] = { 0, 2, 3, 4, 6, 19 };

inline lynx::EffectParams settings (int mode, float lawValue)
{
    auto p = verbs100::defaults (EffectKind::nebula);
    p[12] = (float) mode / 21.0f;
    p[law] = lawValue;
    return p;
}

// `seconds` of the effect's answer to a burst on both channels: noise (a fixed sequence) or a 1 kHz tone under a Hann
// window, `burstSec` long, peak 0.5
inline Stereo burst (const lynx::EffectParams& p, double seconds, double burstSec, bool tone, double sr = 48000.0, int block = 512, double bpm = 120.0)
{
    auto fx = lynx::makeEffect (EffectKind::nebula);
    fx->prepare (sr);
    const auto n = (std::size_t) (seconds * sr), m = (std::size_t) (burstSec * sr);
    Stereo s { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
    std::uint32_t seed = 190u;
    for (std::size_t i = 0; i < std::min (m, n); ++i)
    {
        if (tone)
        {
            const auto t = (double) i / sr;
            s.l[i] = s.r[i] = (float) (0.5 * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * t) * 0.5 * (1.0 - std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) m)));
        }
        else
        {
            seed = seed * 1664525u + 1013904223u;
            s.l[i] = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 0.5f;
            seed = seed * 1664525u + 1013904223u;
            s.r[i] = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 0.5f;
        }
    }
    for (std::size_t off = 0; off < n; off += (std::size_t) block)
        fx->process (s.l.data() + off, s.r.data() + off, (int) std::min<std::size_t> ((std::size_t) block, n - off), p, bpm);
    return s;
}

inline double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (std::size_t i = from; i < std::min (to, x.size()); ++i)
        e += (double) x[i] * x[i];
    return std::sqrt (e / (double) std::max<std::size_t> (1, to - from));
}
inline bool finite (const Stereo& s)
{
    for (std::size_t i = 0; i < s.l.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
            return false;
    return true;
}
inline float peak (const Stereo& s, std::size_t from = 0)
{
    float m = 0.0f;
    for (std::size_t i = from; i < s.l.size(); ++i)
        m = std::max ({ m, std::abs (s.l[i]), std::abs (s.r[i]) });
    return m;
}

// The classic law's fingerprint: four settings, 4 s of the answer to a 0.25 s noise burst at 48 kHz; the RMS of each
// channel in sixteen 0.25 s windows and eight samples of the left channel.
constexpr int fingerprintWindows = 16, fingerprintSamples = 8, fingerprintSize = 2 * fingerprintWindows + fingerprintSamples;
constexpr std::size_t fingerprintAt[fingerprintSamples] = { 6000, 14411, 20001, 33333, 48000, 77777, 120000, 180001 };
inline lynx::EffectParams classicSetting (int which, double& bpm)
{
    bpm = 120.0;
    auto p = settings (0, 0.0f);
    switch (which)
    {
        case 0: break; // the defaults
        case 1: p = settings (3, 0.0f), p[6] = 0.5f, p[4] = 0.3f, p[5] = 0.7f; break;
        case 2: p = settings (12, 0.0f), p[6] = 1.0f, p[5] = 0.6f, p[8] = 0.1f, p[9] = 0.5f, p[7] = 0.75f, p[0] = 1.0f; break;
        default: p = settings (19, 0.0f), p[1] = 1.0f / 3.0f, p[2] = 0.5f, p[10] = 0.6f, p[11] = 1.0f, p[6] = 0.3f, bpm = 140.0; break;
    }
    return p;
}
inline std::vector<double> fingerprint (const Stereo& s)
{
    std::vector<double> f;
    for (int w = 0; w < fingerprintWindows; ++w)
    {
        f.push_back (rms (s.l, (std::size_t) w * 12000, (std::size_t) (w + 1) * 12000));
        f.push_back (rms (s.r, (std::size_t) w * 12000, (std::size_t) (w + 1) * 12000));
    }
    for (const auto at : fingerprintAt)
        f.push_back ((double) s.l[at]);
    return f;
}
// (taken from the build of the round, whose classic renders are byte-identical to the build before it)
constexpr double classicFingerprint[4][fingerprintSize] = {
    { 0.202429872, 0.203783516, 0.112574536, 0.110526776, 0.0824007828, 0.0854935065, 0.0617504996, 0.0610684145,
      0.0456210333, 0.0458865458, 0.0328843988, 0.033170365, 0.0243143201, 0.0255384057, 0.0158617237, 0.0154617729,
      0.0113215907, 0.0121399422, 0.00926429555, 0.00926267719, 0.00655347033, 0.00716216362, 0.0046625661,
      0.00553240996, 0.0039079142, 0.00404923474, 0.00266085857, 0.0027138842, 0.00200493963, 0.00205156155,
      0.00158251021, 0.00162709919, -0.188275695, -0.273265272, -0.123354107, 0.134458646, 0.0253607612,
      0.00621463452, -0.00577327376, -0.00101209793 },
    { 0.20243066, 0.203783572, 0.0339864461, 0.0334147543, 0.0335944705, 0.0335360711, 0.0301053256, 0.0307472487,
      0.0275221687, 0.0279172996, 0.023980897, 0.024431903, 0.0217215686, 0.0219398039, 0.0198434535, 0.0198371137,
      0.0184004822, 0.0183672514, 0.0167467995, 0.0166243757, 0.0153477243, 0.0154152149, 0.0141558413,
      0.0142313513, 0.0132254491, 0.0130592396, 0.0120404762, 0.0122446863, 0.0113545399, 0.0109936911,
      0.0103782963, 0.0103327826, -0.188275695, 0.0191948079, -0.00905180257, -0.0115926629, 0.0319548026,
      0.0105230324, 0.0224482939, 0.011068495 },
    { 0, 0, 0.0960519224, 0.0960519224, 0.108565496, 0.108565496, 0.113093589, 0.113093589, 0.102670012,
      0.102670012, 0.0932967828, 0.0932967828, 0.0914215853, 0.0914215853, 0.0972746083, 0.0972746083, 0.0856872633,
      0.0856872633, 0.0850356039, 0.0850356039, 0.0860744679, 0.0860744679, 0.0827105447, 0.0827105447,
      0.0805641323, 0.0805641323, 0.0783718169, 0.0783718169, 0.0764508214, 0.0764508214, 0.0725373287,
      0.0725373287, 0, -0.00108751655, 0.0276510902, 0.0630112141, -0.0203567874, 0.027253978, 0.125286147,
      -0.0980521664 },
    { 0.202429872, 0.203783516, 0.0471723203, 0.0466008998, 0.0405866873, 0.0406133733, 0.026755834, 0.0273432023,
      0.0183880961, 0.0178003837, 0.011909139, 0.0122582282, 0.00667353398, 0.00679858954, 0.00491815357,
      0.00486633619, 0.00348724634, 0.0035627708, 0.00231241956, 0.00235545882, 0.0016449411, 0.00160724872,
      0.00103879451, 0.00101096187, 0.000693276608, 0.000675582483, 0.000485704398, 0.000481784001, 0.000338233075,
      0.000335579685, 0.000233653728, 0.000231911067, -0.188275695, 0.000670839858, 0.0111831669, 0.0221942011,
      -0.000104946761, 0.00635945378, -0.00161298655, -0.000237104774 },
};
} // namespace nebula190ca

TEST (nebula190ca_law_parameter_loads_and_round_trips)
{
    using namespace lynx;
    using namespace nebula190ca;
    const auto& info = effectInfo (EffectKind::nebula);
    CHECK (info.params.size() == law + 1 && std::string (info.params[law].name) == "Law" && info.params[law].defaultValue == 1.0f);
    CHECK (presets::paramByName (EffectKind::nebula, "Law") == (int) law);
    CHECK (std::string (formatEffectParam (EffectKind::nebula, (int) law, 1.0f, 120.0)) == "measured" && std::string (formatEffectParam (EffectKind::nebula, (int) law, 0.0f, 120.0)) == "classic");
    // a new instance runs the cloud law
    CHECK (makeEffectSlot (EffectKind::nebula).params[law] == 1.0f);
    // a project saved before the round has 14 values (Mix … Clear): it loads with the classic engine
    auto old = makeEffectSlot (EffectKind::nebula);
    upgradeLoadedEffectParams (old, law);
    CHECK (old.params[law] == 0.0f);
    // … and a save of it keeps the choice, as does a save of a new instance (both values survive a load)
    for (const float v : { 0.0f, 1.0f })
    {
        auto slot = makeEffectSlot (EffectKind::nebula);
        slot.params[law] = v;
        CHECK (savedEffectParamCount (slot) == law + 1);
        auto again = makeEffectSlot (EffectKind::nebula);
        for (std::size_t i = 0; i < savedEffectParamCount (slot); ++i)
            again.params[i] = (float) savedEffectParamValue (slot.params[i]);
        upgradeLoadedEffectParams (again, savedEffectParamCount (slot));
        CHECK (again.params[law] == v);
    }
    // every parameter survives a preset file, at awkward values too
    auto p = makeEffectSlot (EffectKind::nebula).params;
    for (std::size_t i = 0; i <= law; ++i)
        p[i] = i == law ? 0.0f : 0.013f + 0.071f * (float) i;
    const auto text = presets::toText (EffectKind::nebula, "round trip", p);
    const auto back = presets::fromText (EffectKind::nebula, text);
    CHECK (back.has_value());
    if (back)
        for (std::size_t i = 0; i <= law; ++i)
            CHECK (std::abs (back->params[i] - p[i]) < 1e-6f);
    // a user preset written before the round has no "Law" line: the classic engine
    std::string before;
    for (std::size_t from = 0; from < text.size();)
    {
        const auto end = std::min (text.find ('\n', from), text.size());
        const auto line = text.substr (from, end - from);
        if (line.rfind ("Law", 0) != 0)
            before += line + "\n";
        from = end + 1;
    }
    CHECK (before.size() < text.size());
    const auto oldPreset = presets::fromText (EffectKind::nebula, before);
    CHECK (oldPreset.has_value() && oldPreset->params[law] == 0.0f);
    // the six modes the cloud law covers
    int covered = 0;
    for (int m = 0; m < verbs::nebula::modeCount; ++m)
        covered += verbs::nebula::cloudMode (m) ? 1 : 0;
    CHECK (covered == 6);
    for (const auto m : cloudModes)
        CHECK (verbs::nebula::cloudMode (m));
}

TEST (nebula190ca_classic_law_renders_what_it_did)
{
    using namespace nebula190ca;
    for (int which = 0; which < 4; ++which)
    {
        double bpm = 120.0;
        const auto p = classicSetting (which, bpm);
        const auto f = fingerprint (burst (p, 4.0, 0.25, false, 48000.0, 512, bpm));
        CHECK ((int) f.size() == fingerprintSize);
        int off = 0;
        for (int i = 0; i < fingerprintSize && i < (int) f.size(); ++i)
        {
            const auto want = classicFingerprint[which][i];
            // (g++ 15 and zig c++ 0.13 / 0.14 agree with the table to 4e-9; the margin is for other maths libraries)
            const auto ok = i < 2 * fingerprintWindows ? std::abs (f[(std::size_t) i] - want) <= 1e-5 * want + 1e-9 : std::abs (f[(std::size_t) i] - want) <= 1e-5;
            off += ok ? 0 : 1;
            if (! ok)
                std::printf ("  classic setting %d, value %d: %.9g, was %.9g\n", which, i, f[(std::size_t) i], want);
        }
        CHECK (off == 0);
    }
    // a mode the cloud law has no network for renders the same samples with the law on and off, on any machine
    for (const int mode : { 1, 8, 13, 21 })
    {
        auto on = settings (mode, 1.0f), off = settings (mode, 0.0f);
        on[6] = off[6] = 0.4f;
        const auto a = burst (on, 2.0, 0.25, false), b = burst (off, 2.0, 0.25, false);
        CHECK (a.l == b.l && a.r == b.r);
    }
    // … and a covered mode does not (the law is really switched)
    const auto a = burst (settings (0, 1.0f), 1.0, 0.25, false), b = burst (settings (0, 0.0f), 1.0, 0.25, false);
    CHECK (a.l != b.l);
}

TEST (nebula190ca_cloud_laws)
{
    using namespace nebula190ca;
    using namespace verbs100;
    // sixteen-line network, Density 0 (no mixing), 100 ms, no modulation: every line repeats after 100 ms + 33 samples
    // with a gain of √Feedback per pass
    {
        auto p = settings (0, 1.0f);
        p[0] = 1.0f, p[3] = (float) (100.0 / 600.0), p[11] = 0.0f;
        const auto s = impulse (EffectKind::nebula, p, 8.0);
        CHECK (near (rt60 (s), 6.0 * (4833.0 / 48000.0) / -std::log10 (0.5), 0.03)); // 2.007 s
        const auto first = onset (s);
        CHECK (first >= 4832 && first <= 4835);
        // the second repeat is √0.5 of the first
        const auto e1 = energyDb (s, 4800, 4900), e2 = energyDb (s, 4800 + 4833, 4900 + 4833);
        CHECK (std::abs ((e2 - e1) - 10.0 * std::log10 (0.5)) < 0.1);
        // without modulation the two sides are the same signal (the reference: correlation 1.00)
        CHECK (verb183bneb::tailCorr (s, 4800, 96000) > 0.999);
        p[5] = 0.75f;
        CHECK (near (rt60 (impulse (EffectKind::nebula, p, 14.0)), 6.0 * (4833.0 / 48000.0) / -std::log10 (0.75), 0.03)); // 4.84 s
    }
    // Mix: equal power, dry untouched at 0
    {
        auto p = settings (0, 1.0f);
        p[0] = 0.0f;
        const auto s = impulse (EffectKind::nebula, p, 0.5);
        CHECK (s.l[0] == 0.5f && s.r[0] == 0.5f && peak (s, 1) == 0.0f);
        p[0] = 0.5f;
        const auto half = impulse (EffectKind::nebula, p, 0.5);
        CHECK (std::abs (half.l[0] - 0.5f * (float) std::sqrt (0.5)) < 1e-6f);
    }
    // Width: 50 % is mono, 0 % swaps the sides (a left-only burst comes out on the right)
    for (const auto mode : cloudModes)
    {
        auto p = settings (mode, 1.0f);
        p[0] = 1.0f, p[6] = 0.3f, p[7] = 0.5f;
        const auto mono = burst (p, 2.0, 0.1, false);
        CHECK (verb183bneb::tailCorr (mono, 0, mono.l.size()) > 0.999);
    }
    // Density is the scattering angle: at 0 nothing crosses between the lines (a left-only impulse stays on the left),
    // above it the sides exchange energy
    for (const float density : { 0.0f, 0.5f })
    {
        auto p = settings (0, 1.0f);
        p[0] = 1.0f, p[6] = density, p[4] = 0.3f;
        auto fx = lynx::makeEffect (EffectKind::nebula);
        fx->prepare (48000.0);
        Stereo s { std::vector<float> (96000, 0.0f), std::vector<float> (96000, 0.0f) };
        s.l[0] = 0.5f;
        for (std::size_t off = 0; off < s.l.size(); off += 512)
            fx->process (s.l.data() + off, s.r.data() + off, (int) std::min<std::size_t> (512, s.l.size() - off), p, 120.0);
        const auto left = rms (s.l, 0, s.l.size()), right = rms (s.r, 0, s.r.size());
        CHECK (left > 1e-4 && (density == 0.0f ? right == 0.0 : right > 0.1 * left));
    }
    // every network answers, decays and keeps its level in a sane range at the defaults (Mix 100 %)
    for (const auto mode : cloudModes)
    {
        auto p = settings (mode, 1.0f);
        p[0] = 1.0f;
        const auto s = burst (p, 12.0, 0.25, false);
        const auto early = rms (s.l, 0, 96000), late = rms (s.l, 480000, 576000);
        CHECK (finite (s) && early > 0.01 && early < 1.0 && late < 0.1 * early);
        if (! (early > 0.01 && early < 1.0 && late < 0.1 * early))
            std::printf ("  mode %d: early %g late %g\n", mode, early, late);
    }
    // CLEAR empties a cloud network at once
    {
        auto p = settings (2, 1.0f);
        p[0] = 1.0f, p[3] = 0.1f, p[4] = 0.3f, p[6] = 0.5f, p[5] = 0.8f; // 60 ms lines, spread and mixed: a continuous tail
        auto fx = lynx::makeEffect (EffectKind::nebula);
        fx->prepare (48000.0);
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        float tail = 0.0f, after = 0.0f;
        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < 512; ++i)
                l[(std::size_t) i] = r[(std::size_t) i] = b < 4 ? 0.3f * (float) std::sin (0.07 * (b * 512 + i)) : 0.0f;
            fx->process (l.data(), r.data(), 512, p, 120.0);
            if (b >= 70)
                for (const auto x : l)
                    tail = std::max (tail, std::abs (x));
        }
        p[13] = 1.0f;
        for (int b = 0; b < 4; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            fx->process (l.data(), r.data(), 512, p, 120.0);
            for (const auto x : l)
                after = std::max (after, std::abs (x));
        }
        CHECK (tail > 1e-4f && after == 0.0f);
    }
}

TEST (nebula190ca_cloud_stable_at_every_extreme)
{
    using namespace nebula190ca;
    // every network at the corners of its controls (Feedback 100 %, modulation as deep and fast as it goes, the
    // shortest and longest delay, both cuts at both ends), three sample rates: finite and bounded (a 33-sample comb at
    // Feedback 100 % resonates 30 dB above its input: bounded is not small), and with the shortest loops, where the
    // windows hold many passes, the level does not grow once the input has stopped
    for (const auto mode : cloudModes)
        for (const double sr : { 44100.0, 48000.0, 192000.0 })
            for (int corner = 0; corner < 6; ++corner)
            {
                if (sr > 100000.0 && corner != 0 && corner != 3)
                    continue;
                auto p = settings (mode, 1.0f);
                p[0] = 1.0f;
                p[5] = 1.0f;                                  // Feedback
                p[3] = corner & 1 ? 1.0f : 0.0f;              // Delay 2 s / 0
                p[4] = corner & 2 ? 1.0f : 0.0f;              // Warp
                p[6] = corner >= 3 ? 1.0f : 0.0f;             // Density
                p[10] = 1.0f, p[11] = corner == 5 ? 0.0f : 1.0f; // Mod rate 10 Hz, depth 100 % (one corner still)
                p[8] = corner == 4 ? 1.0f : 0.0f, p[9] = corner == 4 ? 0.0f : 1.0f; // cuts
                const auto seconds = sr > 100000.0 ? 6.0 : 12.0;
                const auto s = burst (p, seconds, 0.5, false, sr, 256);
                const auto n = s.l.size();
                const auto mid = rms (s.l, n / 2, n * 3 / 4) + rms (s.r, n / 2, n * 3 / 4), end = rms (s.l, n * 3 / 4, n) + rms (s.r, n * 3 / 4, n);
                const auto ok = finite (s) && peak (s) < 64.0f && ((corner & 1) != 0 || end <= 1.02 * mid + 1e-9);
                CHECK (ok);
                if (! ok)
                    std::printf ("  mode %d, %g Hz, corner %d: peak %g, mid %g, end %g\n", mode, sr, corner, (double) peak (s), mid, end);
            }
    // every control at 0 and at 1 together, tiny and odd block sizes
    for (const auto mode : cloudModes)
        for (const float v : { 0.0f, 1.0f })
            for (const int block : { 1, 7, 4096 })
            {
                auto p = settings (mode, 1.0f);
                for (std::size_t i = 0; i < law; ++i)
                    if (i != 12 && i != 13)
                        p[i] = v;
                const auto s = burst (p, block == 1 ? 1.0 : 4.0, 0.25, false, 48000.0, block);
                CHECK (finite (s) && peak (s) < 64.0f);
            }
    // a minute and a half at Feedback 100 % under the deepest, fastest modulation (the moving interpolators are not
    // passive: the loop gain's cap has to hold them): no network gets louder from one 15 s window to the next
    for (const auto mode : cloudModes)
    {
        auto p = settings (mode, 1.0f);
        p[0] = 1.0f, p[5] = 1.0f, p[3] = 0.02f, p[4] = 0.3f, p[6] = 0.5f, p[10] = 1.0f, p[11] = 1.0f; // 12 ms lines
        const auto s = burst (p, 90.0, 0.5, false);
        auto ok = finite (s) && peak (s) < 64.0f;
        for (int w = 1; w + 1 < 6 && ok; ++w)
            ok = rms (s.l, (std::size_t) (w + 1) * 720000, (std::size_t) (w + 2) * 720000) <= 1.02 * rms (s.l, (std::size_t) w * 720000, (std::size_t) (w + 1) * 720000) + 1e-9;
        CHECK (ok);
    }
    // the block size does not change the result (controls are glided per sample)
    {
        auto p = settings (4, 1.0f);
        p[6] = 0.5f, p[4] = 0.4f;
        const auto a = burst (p, 2.0, 0.25, false, 48000.0, 512), b = burst (p, 2.0, 0.25, false, 48000.0, 64);
        float d = 0.0f;
        for (std::size_t i = 0; i < a.l.size(); ++i)
            d = std::max (d, std::abs (a.l[i] - b.l[i]));
        CHECK (d < 1e-4f);
    }
}

TEST (nebula190ca_cloud_does_not_depend_on_the_sample_rate)
{
    using namespace nebula190ca;
    // a 1 kHz tone burst (4 ms) through every network at 44.1, 48 and 96 kHz: the same level, the same decay and the
    // same arrival (the model's fixed 33 / 32 samples are under a millisecond at every rate)
    for (const auto mode : cloudModes)
    {
        auto p = settings (mode, 1.0f);
        p[0] = 1.0f, p[3] = 0.25f, p[5] = 0.6f, p[6] = 0.5f, p[4] = 0.2f; // 150 ms, Feedback 60 %, Density 50 %, Warp 20 %
        double level[3] {}, decay[3] {}, arrival[3] {};
        int k = 0;
        for (const double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const auto s = burst (p, 8.0, 0.004, true, sr);
            const auto n = s.l.size();
            level[k] = 20.0 * std::log10 (rms (s.l, 0, n) + rms (s.r, 0, n) + 1e-30);
            // the time by which 90 % of the energy has come out
            double total = 0.0, run = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                total += (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i];
            std::size_t at = 0;
            for (; at < n && run < 0.9 * total; ++at)
                run += (double) s.l[at] * s.l[at] + (double) s.r[at] * s.r[at];
            decay[k] = (double) at / sr;
            arrival[k] = (double) verbs100::onset (s) / sr;
            ++k;
        }
        for (int i = 0; i < 3; ++i)
        {
            const auto ok = std::abs (level[i] - level[1]) < 0.5 && std::abs (decay[i] - decay[1]) < 0.05 * decay[1] + 0.005 && std::abs (arrival[i] - arrival[1]) < 0.0015;
            CHECK (ok);
            if (! ok)
                std::printf ("  mode %d rate %d: level %.2f / %.2f dB, 90 %% at %.3f / %.3f s, arrival %.2f / %.2f ms\n", mode, i, level[i], level[1], decay[i], decay[1],
                             1000.0 * arrival[i], 1000.0 * arrival[1]);
        }
    }
}

TEST (nebula190ca_engine_change_while_playing)
{
    using namespace lynx;
    using namespace nebula190ca;
    // Law and Mode changed under a steady tone (Mix 100 %): the wet signal fades out on the engine that was playing and
    // the next one starts empty — no step in the output, nothing left of the old tail, both ways and between two cloud
    // networks
    struct Change
    {
        int modeA;
        float lawA;
        int modeB;
        float lawB;
    };
    for (const auto c : { Change { 0, 0.0f, 0, 1.0f }, Change { 0, 1.0f, 0, 0.0f }, Change { 0, 1.0f, 3, 1.0f }, Change { 6, 1.0f, 1, 1.0f }, Change { 1, 1.0f, 19, 1.0f } })
    {
        auto a = settings (c.modeA, c.lawA), b = settings (c.modeB, c.lawB);
        a[0] = b[0] = 1.0f;
        a[3] = b[3] = 0.05f; // 30 ms: the networks are sounding within a few blocks
        a[6] = b[6] = 0.5f;
        auto fx = makeEffect (EffectKind::nebula);
        fx->prepare (48000.0);
        std::vector<float> l (256), r (256);
        float before = 0.0f, step = 0.0f, last = 0.0f, quiet = 1.0f;
        bool fin = true;
        for (int blk = 0; blk < 400; ++blk)
        {
            for (int i = 0; i < 256; ++i)
                l[(std::size_t) i] = r[(std::size_t) i] = 0.25f * (float) std::sin (2.0 * 3.14159265358979323846 * 220.0 * (blk * 256 + i) / 48000.0);
            fx->process (l.data(), r.data(), 256, blk < 200 ? a : b, 120.0);
            for (int i = 0; i < 256; ++i)
            {
                const auto x = l[(std::size_t) i];
                fin = fin && std::isfinite (x) && std::isfinite (r[(std::size_t) i]);
                if (blk >= 150 && blk < 200)
                    before = std::max (before, std::abs (x - last));
                if (blk >= 200 && blk < 210)
                    step = std::max (step, std::abs (x - last));
                // the change is at block 200: 512 samples of fade-out end exactly at the start of block 202
                if (blk == 202 && i == 0)
                    quiet = std::abs (x);
                last = x;
            }
        }
        const auto ok = fin && before > 0.0f && step < 3.0f * before + 0.02f && quiet < 1e-3f;
        CHECK (ok);
        if (! ok)
            std::printf ("  %d/%g -> %d/%g: step %g (steady %g), at the change %g\n", c.modeA, (double) c.lawA, c.modeB, (double) c.lawB, (double) step, (double) before, (double) quiet);
    }
}
