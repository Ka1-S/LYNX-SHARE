// Round 190is: Echoform's echo voicings (EchoBoy's style role) get revision 1, measured black-box on refcap030's EchoBoy
// captures (styles × Saturation 4.8 / 12 / 19.2 dB, impulses at 0 / −20 / −40 dBFS and noise bursts; the parity bank of
// round 190iq; /mnt/e/lynx-work/parity190is/HANDOFF.md): the saturation's make-up is heard once on the echoes (the
// reference's repeats fall by the same dB per pass at every Saturation), the voicing's loop diffusion stays inside the
// echo time, and the echoes sit 8 / 7.7 / 5 samples behind the dry at 44.1 / 48 / 96 kHz. The revision is a hidden
// parameter: a new Echoform and a translated state run it, a project or preset saved before keeps rounds 147 / 150's
// voicings bit for bit. Also: Little PrimalTap gets its own substitute entry playing its measured default (its state is
// encoded, so every state plays it); EchoBoy Jr its own entry with EchoBoy's fallback in revision 0; PrimalTap keeps its
// fallback (its default and EchoBoy Jr's scored lower).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/EchoSuite.h"
#include "core/PluginSubstitutes.h"

namespace parity190is
{
namespace ef = lynx::echosuite::echoform;
// the gold input (as /mnt/e/lynx-work/parity190is/gold/gold.cpp): 0.3 s of noise, a click every second, a 440 Hz tone
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 4242u;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        const double nz = (double) (s >> 8) / 8388608.0 - 1.0;
        const double v = (t < 0.3 ? 0.5 * nz : 0.0) + (std::fmod (t, 1.0) < 1e-3 ? 0.9 : 0.0) + 0.1 * std::sin (2.0 * M_PI * 440.0 * t) * (t > 1.5 && t < 2.0);
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (0.8 * v);
    }
}
inline lynx::EffectSlot slot (int voicing, float drive, float fb, float style, float mix, bool diffusion)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::echoform);
    s.params[ef::voicing] = ef::voicingValue (voicing);
    s.params[ef::drive] = drive;
    s.params[ef::feedback] = fb;
    s.params[ef::style] = style;
    s.params[ef::mix] = mix;
    s.params[ef::syncL] = s.params[ef::syncR] = 0.25f;
    s.params[ef::timeL] = 0.25f;
    s.params[ef::timeR] = 0.18f;
    if (diffusion)
        s.params[ef::diffusion] = 0.4f;
    return s;
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), s.params, 120.0);
}
inline std::uint64_t fnv (const std::vector<float>& l, const std::vector<float>& r, double& e)
{
    std::uint64_t h = 1469598103934665603ull;
    e = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
        for (const float v : { l[i], r[i] })
        {
            std::uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int b = 0; b < 4; ++b) { h ^= (u >> (8 * b)) & 0xffu; h *= 1099511628211ull; }
            e += (double) v * v;
        }
    return h;
}
} // namespace parity190is

TEST (parity190is_echoform_revision_is_hidden_new_instances_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace ef = echosuite::echoform;
    CHECK (effectInfo (EffectKind::echoform).params.size() == (std::size_t) ef::revision); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::echoform);
    CHECK (s.params[(std::size_t) ef::revision] == 1.0f);
    CHECK (savedEffectParamCount (s) == (std::size_t) ef::revision + 1);
    auto now = s;
    upgradeLoadedEffectParams (now, (std::size_t) ef::revision + 1);
    CHECK (now.params[(std::size_t) ef::revision] == 1.0f);
    auto old = s; // saved before this round (the named values only): rounds 147 / 150's voicings, and they stay
    upgradeLoadedEffectParams (old, (std::size_t) ef::revision);
    CHECK (old.params[(std::size_t) ef::revision] == 0.0f);
    CHECK (savedEffectParamCount (old) == (std::size_t) ef::revision);
}

TEST (parity190is_revision0_renders_as_the_base_commit_did)
{
    using namespace lynx;
    namespace ef = echosuite::echoform;
    struct Case
    {
        int voicing;
        float drive, fb, style, mix;
        double sr;
        int diffusion;
        std::uint64_t hash;
        double energy;
    };
    // 3 s of the gold input in blocks of 512: FNV-1a of the float bits and the energy, from the base commit's objects
    // (/mnt/e/lynx-work/parity190is/gold: gold_base.txt; the round's objects with the revision forced to 0 print the same
    // lines, gold_new.txt)
    const Case cases[] = {
        { 2, 0.00, 0.25, 0.10, 0.50, 48000, 0, 0xf51e09c3f25ef4a8ull, 1573.76017 },
        { 4, 0.80, 0.25, 0.10, 1.00, 48000, 0, 0x680363df5af3ef90ull, 11828.3339 },
        { 3, 0.50, 0.30, 0.90, 0.50, 44100, 0, 0x687d79e48ec22913ull, 1576.2982 },
        { 9, 0.20, 0.25, 0.70, 0.60, 96000, 0, 0x6d193e97ea525883ull, 1984.42005 },
        { 13, 0.80, 0.40, 0.10, 1.00, 48000, 0, 0x0a05e73882f14433ull, 6013.07573 },
        { 16, 0.30, 0.25, 0.10, 0.50, 48000, 0, 0x5d5cce3968041192ull, 1849.03832 },
        { 28, 0.50, 0.30, 0.10, 0.50, 48000, 0, 0x01e7c58d3fcf7fabull, 1434.29197 },
        { 0, 0.50, 0.30, 0.10, 0.50, 48000, 1, 0x2ad021b6bca9ad68ull, 1283.99852 },
    };
    for (const auto& c : cases)
    {
        auto s = parity190is::slot (c.voicing, c.drive, c.fb, c.style, c.mix, c.diffusion != 0);
        s.params[(std::size_t) ef::revision] = 0.0f;
        const int n = (int) (3.0 * c.sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190is::signal (l, r, c.sr);
        parity190is::render (s, c.sr, l, r);
        double e = 0.0;
        const auto h = parity190is::fnv (l, r, e);
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == c.hash); // the libm the fingerprints were taken with (sin / pow / log10 / exp may differ elsewhere)
       #endif
        CHECK (std::abs (e - c.energy) < 1e-6 * c.energy + 1e-9);
    }
}

TEST (parity190is_revision1_keeps_the_loop_gain_and_the_echo_time)
{
    using namespace lynx;
    namespace ef = echosuite::echoform;
    // a −40 dBFS impulse into a voicing at Saturation 19.2 dB, feedback 50 %, 250 ms, mix 100 %: the energy of each repeat
    const double sr = 48000.0;
    const int n = (int) (2.0 * sr), T = (int) (0.25 * sr);
    const auto repeats = [&] (int voicing, float rev, std::vector<double>& e, std::vector<int>& at) {
        auto s = parity190is::slot (voicing, 0.8f, 0.25f, 0.1f, 1.0f, false);
        s.params[(std::size_t) ef::revision] = rev;
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        l[100] = r[100] = 0.01f;
        parity190is::render (s, sr, l, r);
        e.clear();
        at.clear();
        for (int k = 1; k <= 4; ++k)
        {
            double sum = 0.0, peak = 0.0;
            int where = 0;
            for (int i = 100 + k * T - T / 3; i < 100 + k * T + T / 3; ++i)
            {
                sum += (double) l[(std::size_t) i] * l[(std::size_t) i];
                if (std::abs (l[(std::size_t) i]) > peak) { peak = std::abs (l[(std::size_t) i]); where = i - 100 - k * T; }
            }
            e.push_back (10.0 * std::log10 (sum + 1e-30));
            at.push_back (where);
        }
    };
    std::vector<double> e0, e1;
    std::vector<int> a0, a1;
    // the multi-head tape voicing (EchoBoy's style 3): its curve's small-signal gain at 19.2 dB is ~ +10 dB; on every pass
    // (revision 0) the loop ran away, once (revision 1) every repeat falls (the reference: 7.5 dB per pass)
    repeats (4, 0.0f, e0, a0);
    repeats (4, 1.0f, e1, a1);
    CHECK (e0[3] > e0[1]);
    for (int k = 1; k < 4; ++k)
        CHECK (e1[(std::size_t) k] < e1[(std::size_t) k - 1] - 5.0 && e1[(std::size_t) k] > e1[(std::size_t) k - 1] - 10.0);
    CHECK (std::abs (e1[0] - e0[0]) < 0.5); // the first echo as before (the make-up once)
    // the cartridge-echo voicing (diffusion 0.18): revision 0's repeats drifted ~33 ms per pass, revision 1 keeps the
    // echo time (the L/R offset: −4 ms on the odd repeats, +6 ms on the even ones, plus the measured 8 samples)
    repeats (3, 0.0f, e0, a0);
    repeats (3, 1.0f, e1, a1);
    CHECK (a0[1] > 0.05 * sr);
    CHECK (std::abs (a1[0] - (-192 + 8)) < 40 && std::abs (a1[1] - (288 + 8)) < 40 && std::abs (a1[2] - (-192 + 8)) < 40); // ± its tones' delay
}

TEST (parity190is_soundtoys_echoes_play_their_own_defaults)
{
    using namespace lynx;
    namespace ef = echosuite::echoform;
    const auto slotFor = [] (const char* name) {
        PluginIdentity id;
        id.format = ForeignPlugin::Format::vst3;
        id.name = name;
        const auto* e = findPluginSubstitute (id, false);
        return std::make_pair (e != nullptr ? std::string (e->key) : std::string(), e != nullptr ? makeSubstituteSlot (*e) : EffectSlot {});
    };
    const auto text = [] (const EffectSlot& s, int p) { return formatEffectParam (s.kind, p, s.params[(std::size_t) p], 120.0); };
    // EchoBoy Jr: EchoBoy's fallback in revision 0 (its own echo lag differs; "#108=0" in the entry: a hidden parameter)
    const auto [jrKey, jr] = slotFor ("EchoBoy Jr");
    CHECK (jrKey == "soundtoys-echoboy-jr" && jr.params[(std::size_t) ef::revision] == 0.0f);
    CHECK (ef::voicingOf (jr.params[ef::voicing]) == ef::voicingForEchoBoyStyle (1) && text (jr, ef::noteL) == "1/4");
    CHECK (slotFor ("EchoBoy").second.params[(std::size_t) ef::revision] == 1.0f);
    const auto [ptKey, pt] = slotFor ("PrimalTap");
    CHECK (ptKey == "soundtoys-primaltap" && pt.kind == EffectKind::echoform); // its round-129 fallback (see the entry)
    const auto [lptKey, lpt] = slotFor ("Little PrimalTap");
    CHECK (lptKey == "soundtoys-little-primaltap" && text (lpt, ef::timeL) == "113.1 ms" && text (lpt, ef::mode) == "Clean");
    // the linear mix law at 50 %: dry and echo both −6 dB (Echoform's equal-power Mix at 2/3, the Output −4.8 dB)
    float dry = 0.0f, wet = 0.0f;
    echosuite::equalPowerMix (lpt.params[ef::mix], dry, wet);
    const auto echoDb = 20.0 * std::log10 (wet) + ef::outputDb (lpt.params[ef::output]);
    CHECK (std::abs (20.0 * std::log10 (dry) + 6.02) < 0.05 && std::abs (echoDb + 6.02) < 0.05);
    CHECK (slotFor ("LittlePrimalTap").first == "soundtoys-little-primaltap" && slotFor ("EchoBoyJr").first == "soundtoys-echoboy-jr");
    CHECK (slotFor ("EchoBoy").first == "soundtoys-echoboy");
}
