// Round 190dl: Echoform's measured tape stage (Law = Measured: record tone, feedback tone, a record level law with
// memory, 0.102 ms more delay per pass, hiss while the tape holds anything) and the clocked modulation law (H-Delay's:
// the echo time swings by a fraction of itself, both sides in step). The classic law (projects saved before) must
// render as it did: its goldens come from the base commit's objects (the work folder's base/gold.cpp linked against
// rollup027 at 77026c3a2; the same program linked against this round's objects with Law = Classic prints the same lines).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/EchoSuite.h"

namespace echo190dl
{
struct Stereo
{
    std::vector<float> l, r;
};

// `seconds` of an input through a fresh Echoform (blocks of 256)
inline Stereo render (const lynx::EffectSlot& slot, Stereo x, double sr = 48000.0)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    for (std::size_t off = 0; off < x.l.size(); off += 256)
        fx->process (x.l.data() + off, x.r.data() + off, (int) std::min<std::size_t> (256, x.l.size() - off), slot.params, 120.0);
    return x;
}

inline Stereo impulse (double seconds, double sr = 48000.0, float a = 0.5f)
{
    Stereo x { std::vector<float> ((std::size_t) (seconds * sr), 0.0f), std::vector<float> ((std::size_t) (seconds * sr), 0.0f) };
    x.l[0] = x.r[0] = a;
    return x;
}

// the magnitude (dB) of a window of `v` at `hz` (a plain DFT bin, Hann window)
inline double binDb (const std::vector<float>& v, std::size_t from, std::size_t len, double hz, double sr = 48000.0)
{
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < len && from + i < v.size(); ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) i / (double) len);
        const auto ph = 2.0 * 3.141592653589793 * hz * (double) i / sr;
        re += w * v[from + i] * std::cos (ph);
        im -= w * v[from + i] * std::sin (ph);
    }
    return 10.0 * std::log10 (re * re + im * im + 1e-30);
}

inline std::size_t peakIn (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    std::size_t best = from;
    for (auto i = from; i < std::min (to, v.size()); ++i)
        if (std::abs (v[i]) > std::abs (v[best]))
            best = i;
    return best;
}

// Tape, 300 ms, Mix 100 %, Wow / Flutter 0 (the bank's deterministic setting)
inline lynx::EffectSlot tape300 (float law)
{
    namespace e = lynx::echosuite::echoform;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::echoform);
    slot.params[e::mix] = 1.0f;
    slot.params[e::syncL] = slot.params[e::syncR] = 0.0f;
    slot.params[e::timeL] = slot.params[e::timeR] = e::timeValue (300.0);
    slot.params[e::wow] = slot.params[e::flutter] = 0.0f;
    slot.params[e::engineLaw] = law;
    return slot;
}
} // namespace echo190dl

TEST (echo190dl_new_instances_run_the_measured_law_and_saved_projects_keep_the_classic_one)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto& info = effectInfo (EffectKind::echoform);
    CHECK ((int) info.params.size() == e::paramCount && e::modClock == e::paramCount - 1);
    CHECK (std::string (info.params[e::engineLaw].name) == "Law" && std::string (info.params[e::modClock].name) == "Mod clock");
    CHECK (formatEffectParam (EffectKind::echoform, e::engineLaw, 0.0f, 120.0) == "Classic"
           && formatEffectParam (EffectKind::echoform, e::engineLaw, 1.0f, 120.0) == "Measured");
    auto fresh = makeEffectSlot (EffectKind::echoform);
    CHECK (fresh.params[e::engineLaw] >= 0.5f && fresh.params[e::modClock] < 0.5f);
    auto old = makeEffectSlot (EffectKind::echoform);
    upgradeLoadedEffectParams (old, (std::size_t) e::engineLaw); // saved by 0.1.26: the round-150 parameters, no more
    CHECK (old.params[e::engineLaw] < 0.5f && old.params[e::modClock] < 0.5f);
    for (const float v : { 0.0f, 1.0f })
    {
        auto again = makeEffectSlot (EffectKind::echoform);
        again.params[e::engineLaw] = v;
        again.params[e::modClock] = 1.0f - v;
        const auto count = savedEffectParamCount (again);
        auto loaded = makeEffectSlot (EffectKind::echoform);
        for (std::size_t i = 0; i < count; ++i)
            loaded.params[i] = std::clamp ((float) savedEffectParamValue (again.params[i]), 0.0f, 1.0f);
        upgradeLoadedEffectParams (loaded, count);
        CHECK (loaded.params[e::engineLaw] == v && loaded.params[e::modClock] == 1.0f - v);
    }
}

TEST (echo190dl_classic_law_renders_as_the_base_commit_did)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    struct Case
    {
        int idx[3];
        float val[3];
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    const float tape = 0.5f / 12.0f, hifi = 2.5f / 12.0f, bucket = 3.5f / 12.0f, clean = 6.5f / 12.0f, lofiM = 1.5f / 12.0f;
    // an impulse (0.5 / −0.25) and a noise burst, 1.5 s in blocks of 256: FNV-1a of the float bits, the energy, two samples
    const Case cases[] = {
        { { -1, -1, -1 }, { 0, 0, 0 }, 48000.0, 0x920fdb9afceffa93ull, 71.2978164, 0, -3.0281246e-08 }, // the default (Tape)
        { { e::feedback, e::wow, e::flutter }, { 0.6f, 1.0f, 1.0f }, 44100.0, 0xddffaa1946669514ull, 83.5439747, 0, -5.47800667e-08 }, // Tape, high feedback, full wow / flutter
        { { e::mode, e::style, e::drive }, { tape, 0.9f, 0.5f }, 48000.0, 0x0cfa2cde21466da0ull, 61.528707, 0, 1.49016284e-08 }, // Tape ping-pong, driven
        { { e::mode, e::feedback, -1 }, { hifi, 0.45f, 0 }, 96000.0, 0x7ec0358974f0a0d6ull, 98.0392646, 0, -5.62914479e-08 }, // Hi-Fi tape
        { { e::mode, e::age, -1 }, { bucket, 0.8f, 0 }, 48000.0, 0xa72eb644b42c1072ull, 66.4183142, 0, -0.000175900364 }, // Bucket brigade
        { { e::mode, e::modDepth, e::modShape }, { clean, 0.8f, 0.3f }, 48000.0, 0xd7d969cfa7ba74bbull, 80.5531519, 0, -1.55528284e-07 }, // Clean, triangle LFO
        { { e::mode, e::lofi, e::diffusion }, { lofiM, 1.0f, 0.5f }, 48000.0, 0xe56c057805b28695ull, 74.1242764, 0, 3.95245161e-06 }, // Lo-Fi, LoFi switch, diffusion
        { { e::voicing, e::style, -1 }, { 5.0f / 31.0f, 0.7f, 0 }, 48000.0, 0xaba8f3caf45c79b5ull, 74.4465694, 0, -1.60156437e-11 }, // a voicing in Dual
        { { e::hybrid, e::feedback, -1 }, { 1.0f, 0.4f, 0 }, 48000.0, 0xa7586bbc909225f5ull, 99.4974303, 0, 0 }, // the hybrid engine
        { { e::tapeVoicing, -1, -1 }, { 2.0f / 3.0f, 0, 0 }, 48000.0, 0x57123ec4764413d1ull, 104.445431, 0, -3.05682805e-11 }, // a tape voicing
    };
    int sameBits = 0;
    for (const auto& c : cases)
    {
        auto slot = makeEffectSlot (EffectKind::echoform);
        slot.params[e::engineLaw] = 0.0f;
        for (int k = 0; k < 3; ++k)
            if (c.idx[k] >= 0)
                slot.params[(std::size_t) c.idx[k]] = c.val[k];
        auto fx = makeEffect (slot);
        fx->prepare (c.sr);
        const int n = (int) (1.5 * c.sr), block = 256;
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        unsigned s = 777u;
        l[0] = 0.5f;
        r[0] = -0.25f;
        for (int i = n / 3; i < n / 3 + 2000; ++i)
        {
            s = s * 1664525u + 1013904223u;
            l[(std::size_t) i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.3f;
            s = s * 1664525u + 1013904223u;
            r[(std::size_t) i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.3f;
        }
        for (int off = 0; off < n; off += block)
            fx->process (l.data() + off, r.data() + off, std::min (block, n - off), slot.params, 120.0);
        std::uint64_t h = 1469598103934665603ull;
        double en = 0.0;
        for (int i = 0; i < n; ++i)
            for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int b = 0; b < 4; ++b)
                {
                    h ^= (u >> (8 * b)) & 0xffu;
                    h *= 1099511628211ull;
                }
                en += (double) v * v;
            }
        // the same sound with any maths library: energy within 0.01 %, two samples within 2e-6
        CHECK (std::abs (en - c.energy) <= 1e-4 * c.energy);
        CHECK (std::abs ((double) l[(std::size_t) (n / 7)] - c.a) < 2e-6);
        CHECK (std::abs ((double) r[(std::size_t) (n / 2)] - c.b) < 2e-6);
        sameBits += h == c.hash ? 1 : 0;
    }
    // bit for bit where the maths library is the one the goldens were made with (glibc 2.43 on x86-64, as round 190bz)
    std::printf ("  classic law: %d of 10 renders hash-identical to the base commit\n", sameBits);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == 10);
   #endif
}

TEST (echo190dl_measured_tape_follows_the_measured_tones)
{
    using namespace echo190dl;
    const double sr = 48000.0;
    const auto m = render (tape300 (1.0f), impulse (1.2));
    const auto c = render (tape300 (0.0f), impulse (1.2));
    // the first echo 0.102 ms later than the set time (minimum-phase tones + that delay, measured)
    const auto p1 = peakIn (m.l, (std::size_t) (0.29 * sr), (std::size_t) (0.31 * sr));
    const auto p1c = peakIn (c.l, (std::size_t) (0.29 * sr), (std::size_t) (0.31 * sr));
    CHECK (p1 >= p1c + 4 && p1 <= p1c + 7);
    // echo 1 and echo 2 per frequency: the record stage (echo 1: lows 4 … 8 dB, 8 kHz 3 … 9 dB under 1 kHz) and the
    // feedback tone (each pass loses ≥ 5.5 dB more at 63 Hz than at 1 kHz; the classic law: under 5 dB more)
    const auto w = (std::size_t) 4096, s1 = (std::size_t) (0.3 * sr) - 1024, s2 = (std::size_t) (0.6 * sr) - 1024;
    const auto e1lo = binDb (m.l, s1, w, 63.0), e1k = binDb (m.l, s1, w, 1000.0), e1hi = binDb (m.l, s1, w, 8000.0);
    std::printf ("  [190dl] echo 1: 63 Hz %+.1f, 8 kHz %+.1f dB against 1 kHz; pass: 63 Hz %+.1f, 1 kHz %+.1f dB (classic %+.1f / %+.1f)\n",
                 e1lo - e1k, e1hi - e1k, binDb (m.l, s2, w, 63.0) - e1lo, binDb (m.l, s2, w, 1000.0) - e1k,
                 binDb (c.l, s2, w, 63.0) - binDb (c.l, s1, w, 63.0), binDb (c.l, s2, w, 1000.0) - binDb (c.l, s1, w, 1000.0));
    CHECK (e1k - e1lo > 4.0 && e1k - e1lo < 8.0);
    CHECK (e1k - e1hi > 3.0 && e1k - e1hi < 9.0);
    const auto passLo = binDb (m.l, s2, w, 63.0) - e1lo, passK = binDb (m.l, s2, w, 1000.0) - e1k;
    CHECK (passK - passLo > 5.5);
    const auto cPassLo = binDb (c.l, s2, w, 63.0) - binDb (c.l, s1, w, 63.0), cPassK = binDb (c.l, s2, w, 1000.0) - binDb (c.l, s1, w, 1000.0);
    CHECK (cPassK - cPassLo < 5.0);
}

TEST (echo190dl_measured_tape_hiss_while_it_runs_and_silence_when_idle)
{
    using namespace echo190dl;
    namespace e = lynx::echosuite::echoform;
    const double sr = 48000.0;
    // no input at all: not a single non-zero sample (an idle insert stays silent)
    Stereo quiet { std::vector<float> ((std::size_t) sr, 0.0f), std::vector<float> ((std::size_t) sr, 0.0f) };
    const auto q = render (tape300 (1.0f), quiet);
    bool zero = true;
    for (std::size_t i = 0; i < q.l.size(); ++i)
        zero = zero && q.l[i] == 0.0f && q.r[i] == 0.0f;
    CHECK (zero);
    // after an impulse, before its first echo: the hiss at −91.5 dBFS (± 2 dB) at Mix 100 %, the sides uncorrelated
    auto slot = tape300 (1.0f);
    slot.params[e::feedback] = 0.0f;
    const auto m = render (slot, impulse (6.0));
    double el = 0.0, er = 0.0, lr = 0.0;
    for (auto i = (std::size_t) (0.02 * sr); i < (std::size_t) (0.28 * sr); ++i)
    {
        el += (double) m.l[i] * m.l[i];
        er += (double) m.r[i] * m.r[i];
        lr += (double) m.l[i] * m.r[i];
    }
    const auto n = 0.26 * sr, db = 10.0 * std::log10 (el / n);
    std::printf ("  [190dl] hiss %.1f dBFS\n", db);
    CHECK (db > -93.5 && db < -89.5);
    CHECK (std::abs (lr) < 0.2 * std::sqrt (el * er));
    // the echo gone and 0.5 s more: the hiss has faded under −140 dBFS
    double tail = 0.0;
    for (auto i = (std::size_t) (5.0 * sr); i < m.l.size(); ++i)
        tail = std::max (tail, (double) std::abs (m.l[i]));
    CHECK (tail < 1e-7);
    // the classic law has no hiss
    const auto c = render (tape300 (0.0f), impulse (0.28));
    double ec = 0.0;
    for (auto i = (std::size_t) (0.02 * sr); i < c.l.size(); ++i)
        ec += (double) c.l[i] * c.l[i];
    CHECK (ec == 0.0);
}

TEST (echo190dl_mod_clock_swings_the_time_by_a_fraction_of_itself)
{
    using namespace echo190dl;
    namespace e = lynx::echosuite::echoform;
    const double sr = 48000.0;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::echoform);
    slot.params[e::mode] = e::modeValue (e::Mode::clean);
    slot.params[e::mix] = 1.0f;
    slot.params[e::feedback] = 0.0f;
    slot.params[e::syncL] = slot.params[e::syncR] = 0.0f;
    slot.params[e::timeL] = slot.params[e::timeR] = e::timeValue (400.0);
    slot.params[e::modShape] = 1.5f / 5.0f; // triangle: −1 at the LFO's start
    slot.params[e::modRate] = 0.0f;         // 0.01 Hz: still at its trough for the first echo
    slot.params[e::modDepth] = 0.4f;
    slot.params[e::modClock] = 1.0f;
    const auto y = render (slot, impulse (1.0));
    // 400 ms × (1 − ½ · 0.4) = 320 ms, both sides in step
    const auto p = peakIn (y.l, (std::size_t) (0.2 * sr), (std::size_t) (0.6 * sr));
    CHECK (std::abs ((double) p / sr - 0.320) < 0.002);
    CHECK (peakIn (y.r, (std::size_t) (0.2 * sr), (std::size_t) (0.6 * sr)) == p);
    // the ms law (Clock off): 10.4 ms × 0.4² at most, the right side a quarter period on
    slot.params[e::modClock] = 0.0f;
    const auto z = render (slot, impulse (1.0));
    const auto q = peakIn (z.l, (std::size_t) (0.2 * sr), (std::size_t) (0.6 * sr));
    CHECK (std::abs ((double) q / sr - 0.400) < 0.0025);
}

TEST (echo190dl_hdelay_modulation_translates_to_the_clock_law)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 17.1_x64";
    // the bank's S2 state: own-BPM sync, 1/8 dotted at 120, feedback 60, depth 50 %, rate 0.1, LoFi on, Analog Mod2
    id.state = substtest::wavesComponent ("H-Delay", "* 0 120 375 11 * 100 60 0 0 0 * * * 0 20100 * 19 * 50 0.1 1 * 2");
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    int n = 0;
    const auto s = makeSubstituteSlot (*entry, &id, &n);
    CHECK (n > 0 && s.params[e::modClock] == 1.0f && e::modShapeOf (s.params[e::modShape]) == 1);
    CHECK (std::abs (e::modRateHz (s.params[e::modRate]) - 0.2) < 0.005);
    CHECK (std::abs (s.params[e::modDepth] - 0.466f) < 1e-3f && s.params[e::lofi] == 1.0f);
    // no modulation in the state: the clock law stays off
    id.state = substtest::wavesComponent ("H-Delay", "* 0 120 375 11 * 100 60 0 0 0 * * * 0 20100 * 19 * 0 0.1 1 * 2");
    const auto t = makeSubstituteSlot (*findPluginSubstitute (id, false), &id, &n);
    CHECK (t.params[e::modClock] == 0.0f);
}

TEST (echo190dl_measured_law_and_clock_stay_finite_at_the_extremes)
{
    using namespace echo190dl;
    namespace e = lynx::echosuite::echoform;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const float v : { 0.0f, 1.0f })
        {
            auto slot = lynx::makeEffectSlot (lynx::EffectKind::echoform);
            slot.params[e::feedback] = v > 0.5f ? 1.0f : 0.6f;
            slot.params[e::drive] = v;
            slot.params[e::timeL] = slot.params[e::timeR] = v;
            slot.params[e::syncL] = slot.params[e::syncR] = 0.0f;
            slot.params[e::wow] = slot.params[e::flutter] = 1.0f;
            slot.params[e::style] = v > 0.5f ? 0.9f : 0.1f;
            Stereo x { std::vector<float> ((std::size_t) (2.0 * sr), 0.0f), std::vector<float> ((std::size_t) (2.0 * sr), 0.0f) };
            unsigned s = 99u;
            for (std::size_t i = 0; i < x.l.size(); ++i)
            {
                s = s * 1664525u + 1013904223u;
                x.l[i] = x.r[i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 2.0f;
            }
            auto y = render (slot, x, sr);
            bool ok = true;
            for (std::size_t i = 0; i < y.l.size(); ++i)
                ok = ok && std::isfinite (y.l[i]) && std::isfinite (y.r[i]) && std::abs (y.l[i]) < 16.0f;
            CHECK (ok);
            // the clock law at full depth on the longest time stays inside the line
            slot.params[e::mode] = e::modeValue (e::Mode::clean);
            slot.params[e::modClock] = 1.0f;
            slot.params[e::modDepth] = 1.0f;
            slot.params[e::modRate] = 1.0f;
            y = render (slot, x, sr);
            for (std::size_t i = 0; i < y.l.size(); ++i)
                ok = ok && std::isfinite (y.l[i]) && std::isfinite (y.r[i]) && std::abs (y.l[i]) < 16.0f;
            CHECK (ok);
        }
}
