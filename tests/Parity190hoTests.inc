// Round 190ho (0.1.30 plugin parity follow-ups; /mnt/e/lynx-work/parity190ho/HANDOFF.md), black-box references only:
//  - Voice Shift: the Measured law's revision 1 (a hidden parameter): from a sung onset's rise in level until the
//    detector has decided, the unvoiced grains keep the source's formants — the start of the note is no longer read at
//    the formant factor (a low thump at every onset); projects saved by 0.1.27 … 0.1.29 keep revision 0 (round 190dp's
//    grains) bit for bit, older ones the Classic law.
//  - Ridge Strip: the SSL 4K G's Pink EQ (its own EQ type, measured on the trial's 206 Pink impulse renders) as a hidden
//    Pink switch of the 4K voicing set by the 4K G translation; a project saved before has it off and plays the
//    colour selector's EQ as before. The 4K G's ÷3 / ×3 act in Pink only (in its Black they do nothing).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/Effects.h"
#include "core/MixSuite.h"
#include "core/VoiceMatch.h"
#include "core/VoiceShift.h"

namespace parity190ho
{
struct Render
{
    std::uint64_t hash;
    double energy, a, b;
};

// a breath of noise, then a sung-like note (170 Hz, 8 harmonics falling 6 dB / octave), a gap, another note (220 Hz);
// right = 0.8 × left (the same signal as /mnt/e/lynx-work/parity190ho/gold/gold.cpp)
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 4242u;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        double v = t < 0.3 ? 0.05 * ((double) (s >> 8) / 8388608.0 - 1.0) : 0.0;
        const auto note = [&] (double t0, double t1, double hz) {
            if (t < t0 || t > t1)
                return 0.0;
            double a = 0.0;
            for (int h = 1; h <= 8; ++h)
                a += std::sin (2.0 * 3.14159265358979323846 * hz * h * (t - t0)) / h;
            return 0.2 * a * std::min (1.0, (t - t0) / 0.01) * std::min (1.0, (t1 - t) / 0.01);
        };
        v += note (0.3, 0.9, 170.0) + note (1.1, 1.6, 220.0);
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (0.8 * v);
    }
}

inline Render render (const lynx::EffectSlot& slot, double sr, double seconds)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) (seconds * sr), block = 256;
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    signal (l, r, sr);
    for (int off = 0; off < n; off += block)
        fx->process (l.data() + off, r.data() + off, std::min (block, n - off), slot.params, 120.0);
    Render out { 1469598103934665603ull, 0.0, (double) l[(std::size_t) (n / 3)], (double) r[(std::size_t) (n / 2)] };
    for (int i = 0; i < n; ++i)
        for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
        {
            std::uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int b = 0; b < 4; ++b)
            {
                out.hash ^= (u >> (8 * b)) & 0xffu;
                out.hash *= 1099511628211ull;
            }
            out.energy += (double) v * v;
        }
    return out;
}
} // namespace parity190ho

TEST (parity190ho_voice_shift_revision_is_hidden_new_instances_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace vs = voiceshift;
    CHECK (effectInfo (EffectKind::voiceShift).params.size() == (std::size_t) vs::count); // hidden: no control, no string
    CHECK ((int) vs::revision == (int) vs::count);
    auto slot = makeEffectSlot (EffectKind::voiceShift);
    CHECK (slot.params[(std::size_t) vs::law] == 1.0f && slot.params[(std::size_t) vs::revision] == 1.0f);
    CHECK (savedEffectParamCount (slot) == (std::size_t) vs::revision + 1);
    auto now = slot; // saved by this version
    upgradeLoadedEffectParams (now, (std::size_t) vs::revision + 1);
    CHECK (now.params[(std::size_t) vs::revision] == 1.0f && now.params[(std::size_t) vs::law] == 1.0f);
    auto v127 = slot; // saved by 0.1.27 … 0.1.29 (Law = Measured, no revision): round 190dp's grains
    upgradeLoadedEffectParams (v127, (std::size_t) vs::count);
    CHECK (v127.params[(std::size_t) vs::revision] == 0.0f && v127.params[(std::size_t) vs::law] == 1.0f);
    CHECK (savedEffectParamCount (v127) == (std::size_t) vs::count);
    auto old = slot; // saved before 0.1.27: the Classic law
    upgradeLoadedEffectParams (old, (std::size_t) vs::law);
    CHECK (old.params[(std::size_t) vs::revision] == 0.0f && old.params[(std::size_t) vs::law] == 0.0f);
    // Law = Classic ignores the revision
    auto c0 = makeEffectSlot (EffectKind::voiceShift), c1 = c0;
    c0.params[(std::size_t) vs::law] = c1.params[(std::size_t) vs::law] = 0.0f;
    c0.params[(std::size_t) vs::formant] = c1.params[(std::size_t) vs::formant] = 0.25f;
    c0.params[(std::size_t) vs::revision] = 0.0f;
    CHECK (parity190ho::render (c0, 48000.0, 1.0).hash == parity190ho::render (c1, 48000.0, 1.0).hash);
}

TEST (parity190ho_revision0_and_the_selector_colours_render_as_the_base_commit_did)
{
    using namespace lynx;
    namespace vs = voiceshift;
    namespace rd = mixsuite::ridge;
    struct Case
    {
        bool ridge;
        int idx;
        float val;
        int idx2;
        float val2;
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    // FNV-1a of the float bits, the energy, two samples; from the base commit's objects (/mnt/e/lynx-work/parity190ho/gold:
    // gold_base.txt; the round's objects print the same lines with the Voice Shift revision forced to 0, gold_new.txt).
    // Voice Shift: Law = Measured; Ridge: the 4K voicing, HPF 0.3, one band, the selector's Brown / Black / Orange
    const Case cases[] = {
        { false, vs::formant, 0.25f, -1, 0.0f, 48000.0, 0xf43fce61d9de69b2ull, 1547.8276, 0.138076171, -0.178773463 },
        { false, vs::pitch, 0.708333015f, -1, 0.0f, 44100.0, 0xbbe3e9ddef760319ull, 1625.85692, -0.0444874167, -0.137252882 },
        { false, vs::pitch, 0.208333001f, vs::formant, 0.583333015f, 48000.0, 0x4ef5dfcf30481261ull, 828.606018, 0.168560982, 0.0612535924 },
        { false, vs::mode, 0.5f, vs::formant, 0.375f, 96000.0, 0xc854e77956718c3aull, 3250.40668, 0.121605769, 0.0285715759 },
        { true, rd::k4Colour, 0.0f, rd::lfGain, 0.800000012f, 48000.0, 0xffc6706a263f674eull, 2454.67763, -0.139356554, -0.0582271926 },
        { true, rd::k4Colour, 0.5f, rd::hmfGain, 0.200000003f, 44100.0, 0xa66f5ef93f79d709ull, 1231.2362, -0.116727449, -0.0132311396 },
        { true, rd::k4Colour, 1.0f, rd::hfGain, 0.899999976f, 96000.0, 0xc597b72b69fe0cbeull, 3583.53761, -0.138628721, 0.199762866 },
        { true, rd::k4Colour, 0.5f, rd::lmfGain, 0.75f, 48000.0, 0xb9d6b389ef74e025ull, 3064.65761, -0.151665062, 0.107529953 },
    };
    int sameBits = 0, n = 0;
    for (const auto& c : cases)
    {
        auto slot = makeEffectSlot (c.ridge ? EffectKind::ridgeStrip : EffectKind::voiceShift);
        if (c.ridge)
        {
            slot.params[(std::size_t) rd::console4k] = 1.0f;
            slot.params[(std::size_t) rd::hpf] = 0.3f;
        }
        else
        {
            slot.params[(std::size_t) vs::law] = 1.0f;
            upgradeLoadedEffectParams (slot, (std::size_t) vs::count); // a 0.1.27 … 0.1.29 project: revision 0
            CHECK (slot.params[(std::size_t) vs::revision] == 0.0f);
        }
        slot.params[(std::size_t) c.idx] = c.val;
        if (c.idx2 >= 0)
            slot.params[(std::size_t) c.idx2] = c.val2;
        const auto r = parity190ho::render (slot, c.sr, c.ridge ? 1.0 : 1.8);
        CHECK (std::abs (r.energy - c.energy) <= 1e-4 * c.energy);
        CHECK (std::abs (r.a - c.a) < 2e-6 && std::abs (r.b - c.b) < 2e-6);
        sameBits += r.hash == c.hash ? 1 : 0;
        ++n;
    }
    std::printf ("  revision 0 / selector colours: %d of %d renders hash-identical to the base commit\n", sameBits, n);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == n);
   #endif
}

TEST (parity190ho_voice_shift_revision1_keeps_the_onset_out_of_the_formant_shift)
{
    using namespace lynx;
    namespace vs = voiceshift;
    // two sung-like notes (180 Hz, 8 harmonics) out of silence. At Formant −6 the start of each note, heard as unvoiced
    // while the detector decides, was read at the formant factor: its fundamental near 127 Hz, under every harmonic of
    // the note itself. The energy under ~110 Hz in the 60 ms after each onset (+ the latency) shows it
    const double sr = 48000.0;
    const int n = (int) (1.6 * sr);
    std::vector<float> in ((std::size_t) n, 0.0f);
    for (const double t0 : { 0.2, 1.0 })
        for (int i = (int) (t0 * sr); i < (int) ((t0 + 0.5) * sr); ++i)
        {
            const double t = i / sr - t0;
            double v = 0.0;
            for (int h = 1; h <= 8; ++h)
                v += std::sin (2.0 * 3.14159265358979323846 * 180.0 * h * t) / h;
            in[(std::size_t) i] = (float) (0.2 * v * std::min (1.0, t / 0.005));
        }
    auto lowAtOnsets = [&] (float revision) {
        auto slot = makeEffectSlot (EffectKind::voiceShift);
        slot.params[(std::size_t) vs::formant] = 0.25f;
        slot.params[(std::size_t) vs::revision] = revision;
        auto fx = makeEffect (slot);
        fx->prepare (sr);
        auto l = in, r = in;
        for (int off = 0; off < n; off += 256)
            fx->process (l.data() + off, r.data() + off, std::min (256, n - off), slot.params, 120.0);
        const double a = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 110.0 / sr);
        double z[4] {}, e = 0.0;
        const int lat = fx->latencySamples();
        std::vector<double> low ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double x = l[(std::size_t) i];
            for (auto& q : z)
                x = q += a * (x - q);
            low[(std::size_t) i] = x;
        }
        for (const double t0 : { 0.2, 1.0 })
            for (int i = (int) (t0 * sr) + lat; i < (int) ((t0 + 0.06) * sr) + lat && i < n; ++i)
                e += low[(std::size_t) i] * low[(std::size_t) i];
        return 10.0 * std::log10 (e + 1e-20);
    };
    const auto r0 = lowAtOnsets (0.0f), r1 = lowAtOnsets (1.0f);
    std::printf ("  Formant -6, energy under 110 Hz at the onsets: revision 0 %.1f dB, revision 1 %.1f dB\n", r0, r1);
    CHECK (r1 < r0 - 3.0);
}

// rollup 0.1.30 step 8: round 190lb's Pink EQ (one section per band, BLACKBOX_rodada190lb §2: K4G_ir Pink 6.73 against
// this round's 6.50) replaced this round's; these two tests now check 190lb's code against this round's reference points
TEST (parity190ho_ridge_4k_g_pink_is_translated_and_hidden)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    // the 4K G's EQ Type 0 = Pink (its trial's states: K4G_ir), 1 = Black
    auto p = tp172c::sslSlot ("SSL 4K G", "<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowMidEqFreq\" value=\"1.0\"/>"
                                         "<PARAM id=\"LowMidEqDiv3\" value=\"1.0\"/><PARAM id=\"HighMidEqFreq\" value=\"3.0\"/>"
                                         "<PARAM id=\"HighMidEqMult3\" value=\"1.0\"/><PARAM id=\"LowEqGain\" value=\"18.0\"/>").params;
    CHECK (r::k4Voicing (p) && p[r::k4Pink] == 1.0f && p[r::k4LmfDiv3] == 1.0f && p[r::k4HmfMult3] == 1.0f);
    CHECK (std::abs (r::lmfHz (p[r::lmfFreq]) - 1000.0) < 0.5 && std::abs (r::hmfHz (p[r::hmfFreq]) - 3000.0) < 1.0); // the dials themselves
    CHECK (std::abs (r::bandGainDb (p[r::lfGain], false) - 15.0) < 1e-3); // ±18 onto the 4K's ±15, as for Black
    p = tp172c::sslSlot ("SSL 4K G", "<PARAM id=\"EqType\" value=\"1.0\"/><PARAM id=\"LowMidEqDiv3\" value=\"1.0\"/>").params;
    CHECK (p[r::k4Pink] == 0.0f && p[r::k4LmfDiv3] == 0.0f && r::k4ColourIndex (p[r::k4Colour]) == 1);
    // the 4K E has no Pink: its colour selector as before
    p = tp172c::sslSlot ("SSL 4K E", "<PARAM id=\"EqColour\" value=\"2.0\"/><PARAM id=\"EqType\" value=\"0.0\"/>").params;
    CHECK (p[r::k4Pink] == 0.0f && r::k4ColourIndex (p[r::k4Colour]) == 2);
    // hidden: a new Ridge Strip and every project saved before have it off (0 is not saved)
    auto slot = makeEffectSlot (EffectKind::ridgeStrip);
    CHECK (slot.params[(std::size_t) r::k4Pink] == 0.0f && savedEffectParamCount (slot) <= (std::size_t) r::count);
    CHECK ((int) r::k4Pink > (int) r::count && (int) r::k4HmfMult3 < maxEffectParams);
}

TEST (parity190ho_ridge_4k_pink_follows_the_reference_curves)
{
    using namespace lynx;
    namespace ec = vmatch::echannel;
    ec::Settings s;
    s.model = 1;
    s.colour = 1;
    s.pink = true; // (round 190lb's Settings: the gains in the G's own dB)
    s.autoMakeup = false;
    // [black-box] the 4K G trial's Pink impulse renders (tp172c K4G_ir), magnitudes in dB; the 4K's ±15 dB scale carries
    // the G's ±18 / ±22 / ±20 dials. Within 0.6 dB of the reference (the fits are within 0.02 … 0.4 dB rms)
    // (rollup step 8: 190lb's one-section shelves leave out the overshoot this round fitted — less than 190ho's model there,
    // higher on the whole Pink bank, 6.73 against 6.50 — so those points carry a wider bound: `tol`)
    struct Point { double hz, db, tol = 0.6; };
    auto near = [&s] (std::initializer_list<Point> pts) {
        bool ok = true;
        for (const auto& pt : pts)
        {
            const auto m = ec::responseDb (s, 48000.0, pt.hz);
            if (std::abs (m - pt.db) > pt.tol)
            {
                std::printf ("  Pink at %.0f Hz: %.2f dB, reference %.2f dB\n", pt.hz, m, pt.db);
                ok = false;
            }
        }
        return ok;
    };
    s.lfGain = 18.0; s.lfHz = 200.0; // LF +18 at 200 Hz: steeper than Black, overshooting below 0 dB near 3 kHz
    CHECK (near ({ { 31.0, 18.2 }, { 125.0, 16.7 }, { 315.0, 10.3 }, { 800.0, 3.3 }, { 3150.0, -1.2, 1.6 }, { 12500.0, -0.1 } }));
    s.lfBell = true; // no Bell mode in Pink (the renders are identical)
    CHECK (near ({ { 31.0, 18.2 }, { 3150.0, -1.2, 1.6 } }));
    s.lfGain = 0.0; s.lfBell = false;
    s.lmfGain = 22.0; s.lmfKHz = 1.0; s.lmfQ = 1.5; // LMF +22 at 1 kHz
    CHECK (near ({ { 200.0, 3.9 }, { 500.0, 11.3 }, { 800.0, 17.8 }, { 1250.0, 20.1 }, { 3150.0, 8.3 } }));
    s.lmfDiv3 = true; // ÷3: the centre near 260 Hz (×0.24, not ÷3)
    CHECK (near ({ { 125.0, 11.8 }, { 200.0, 18.2 }, { 315.0, 19.5 }, { 800.0, 7.9 } }));
    s.lmfGain = 0.0; s.lmfDiv3 = false;
    s.hmfGain = -22.0; s.hmfKHz = 3.0; s.hmfQ = 1.5; // HMF -22 at 3 kHz
    CHECK (near ({ { 500.0, -3.1 }, { 1250.0, -9.6 }, { 3150.0, -21.4 }, { 8000.0, -9.8 } }));
    s.hmfGain = 0.0;
    s.hfGain = -20.0; s.hfKHz = 8.0; // HF -20 at 8 kHz: a wide shelf with +1.7 dB of overshoot under 1 kHz
    CHECK (near ({ { 315.0, 0.8, 1.1 }, { 800.0, 1.7, 2.4 }, { 2000.0, -4.3, 1.1 }, { 5000.0, -12.0 }, { 12500.0, -17.5 } }));
}
