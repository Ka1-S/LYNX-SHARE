// Round 190kw (Pro-R 2 → LuxeVerb Space): the Space engine's revision 1, written from the laws recovered by static
// analysis of the reference (docs/analise/REA_rodada190kw.md: true stereo with the width matrix and the feedback
// rotation, the line-length / level / brightness laws) and fitted on refcap030's captures (docs/PARITY.md "Round 190kw").
// A new LuxeVerb and a translated Pro-R 2 state get it (hidden luxeVerbSpaceRevision = 1); a project saved before loads
// 0 and renders bit for bit as the base commit did (fingerprints below, /mnt/e/lynx-work/rea190kw/r2/gold).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/Effects.h"
#include "core/FabFilterStates.h"
#include "core/LuxeVerb.h"

namespace p190kwr2t
{
#include "Parity190kwR2Cases.inc"
inline std::vector<std::uint8_t> ffbs (const std::vector<float>& v)
{
    std::vector<std::uint8_t> b { 'F', 'F', 'B', 'S', 1, 0, 0, 0 };
    const std::uint32_t n = (std::uint32_t) v.size();
    for (int i = 0; i < 4; ++i) b.push_back ((std::uint8_t) (n >> (8 * i)));
    for (const float f : v)
    {
        std::uint32_t u;
        std::memcpy (&u, &f, 4);
        for (int i = 0; i < 4; ++i) b.push_back ((std::uint8_t) (u >> (8 * i)));
    }
    return b;
}
// a Pro-R 2 state (Modern, 100 % wet) with the given Space, Brightness and raw Stereo Width, rendered on an impulse
inline lynx::EffectSlot proR2 (float space, float bright, float width)
{
    std::vector<float> v (136, 0.0f);
    v[0] = space; v[1] = 0.0f; v[2] = 0.5f; v[3] = bright; v[5] = 0.3f; v[7] = width; v[9] = 100.0f;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::luxeVerb);
    if (const auto st = lynx::fabfilter::parseState (ffbs (v))) lynx::fabfilter::fromProR2 (*st, slot);
    return slot;
}
inline void impulse (const lynx::EffectSlot& slot, bool leftOnly, std::vector<float>& l, std::vector<float>& r, double sr = 48000.0)
{
    const int n = (int) (1.5 * sr);
    l.assign ((std::size_t) n, 0.0f); r.assign ((std::size_t) n, 0.0f);
    l[100] = 0.5f;
    if (! leftOnly) r[100] = 0.5f;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
}
inline double db (const std::vector<float>& x, int from = 200)
{
    double e = 0.0;
    for (std::size_t i = (std::size_t) from; i < x.size(); ++i) e += (double) x[i] * x[i];
    return 10.0 * std::log10 (e + 1e-30);
}
} // namespace p190kwr2t

TEST (parity190kwR2_revision_is_hidden_new_instances_and_pro_r2_run_it_old_projects_do_not)
{
    using namespace lynx;
    CHECK (effectInfo (EffectKind::luxeVerb).params.size() == (std::size_t) luxeVerbSpaceDucking); // no new control
    auto s = makeEffectSlot (EffectKind::luxeVerb);
    CHECK (s.params[(std::size_t) luxeVerbSpaceRevision] == 1.0f && s.params[(std::size_t) luxeVerbSpaceWidthExt] == 0.0f);
    CHECK (savedEffectParamCount (s) == (std::size_t) luxeVerbSpaceRevision + 1);
    auto now = s;
    upgradeLoadedEffectParams (now, (std::size_t) luxeVerbSpaceRevision + 1);
    CHECK (now.params[(std::size_t) luxeVerbSpaceRevision] == 1.0f);
    auto old = s; // saved before this round (with or without Ducking): the tp172b / 190it engine
    upgradeLoadedEffectParams (old, (std::size_t) luxeVerbSpaceDucking + 1);
    CHECK (old.params[(std::size_t) luxeVerbSpaceRevision] == 0.0f);
    upgradeLoadedEffectParams (old = s, (std::size_t) luxeVerbSpaceDucking);
    CHECK (old.params[(std::size_t) luxeVerbSpaceRevision] == 0.0f);
    // the translator: revision 1 and Stereo Width's raw 0 … 1.2 (past 1 in the hidden extension)
    const auto a = p190kwr2t::proR2 (0.5f, 0.5f, 1.2f), b = p190kwr2t::proR2 (0.5f, 0.5f, 0.6f);
    CHECK (a.params[(std::size_t) luxeVerbSpaceRevision] == 1.0f && b.params[(std::size_t) luxeVerbSpaceRevision] == 1.0f);
    CHECK (a.params[(std::size_t) (luxeVerbSpaceFirst + luxeverb::sp::width)] == 1.0f);
    CHECK (std::abs (a.params[(std::size_t) luxeVerbSpaceWidthExt] - 1.0f) < 1e-5f);
    CHECK (std::abs (b.params[(std::size_t) (luxeVerbSpaceFirst + luxeverb::sp::width)] - 0.6f) < 1e-6f);
    CHECK (b.params[(std::size_t) luxeVerbSpaceWidthExt] == 0.0f);
    // Vintage / Plate states keep revision 0 (their revision-1 fit scored lower)
    std::vector<float> v (136, 0.0f);
    v[0] = 0.5f; v[4] = 1.0f; v[7] = 0.7f; v[9] = 100.0f;
    auto c = makeEffectSlot (EffectKind::luxeVerb);
    if (const auto st = fabfilter::parseState (p190kwr2t::ffbs (v))) fabfilter::fromProR2 (*st, c);
    CHECK (c.params[(std::size_t) luxeVerbSpaceRevision] == 0.0f);
}

TEST (parity190kwR2_revision0_renders_as_the_base_commit_did)
{
    // 1.6 s of round 190it's gold input in blocks of 512 at 44.1 / 48 / 96 kHz, five Space-engine settings with the
    // revision forced to 0: FNV-1a of the float bits and the energy, taken from the base commit's objects (gold_base.txt)
    struct Gold { int k; double sr; std::uint64_t hash; double energy; };
    const Gold gold[] = {
        { 0, 44100, 0xee9bc1e0588e8e3dull, 2418.70448 },
        { 0, 48000, 0x8739eae59cc1e050ull, 2629.43206 },
        { 0, 96000, 0x526d84bc11c973cbull, 5191.49267 },
        { 1, 44100, 0x5700c3ccefffaee6ull, 216.770539 },
        { 1, 48000, 0xaa7b4131ca62bec6ull, 236.329435 },
        { 1, 96000, 0x88da870ddd71cb86ull, 474.306466 },
        { 2, 44100, 0x82ea4e036e057158ull, 6033.42343 },
        { 2, 48000, 0x03414505dde116e2ull, 6478.33799 },
        { 2, 96000, 0x2acf53b0b6acaa4dull, 12857.3257 },
        { 3, 44100, 0x02b932ece59c5dfeull, 133.18652 },
        { 3, 48000, 0xe89981ec4f59db1bull, 146.555978 },
        { 3, 96000, 0xf4e7987599bd3b87ull, 273.263248 },
        { 4, 44100, 0x953f967260295264ull, 1030.88322 },
        { 4, 48000, 0x34a57debad2e5414ull, 1107.2502 },
        { 4, 96000, 0xb1544dcde2452a57ull, 2166.12263 },
    };
    for (const auto& g : gold)
    {
        std::uint64_t h = 0;
        double e = 0.0;
        p190kwr2t::p190kwr2::fingerprint (g.k, g.sr, h, e);
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == g.hash); // the libm the fingerprints were taken with
       #endif
        CHECK (std::abs (e - g.energy) < 1e-6 * g.energy + 1e-9);
    }
}

TEST (parity190kwR2_width_law_true_stereo_and_output_matrix)
{
    using namespace lynx;
    std::vector<float> l, r;
    // Stereo Width 100 %: v = 0, s_in = 1, no rotation, X = Y = 1 — the two channels' networks are independent, so a
    // left-only impulse leaves the right channel silent
    p190kwr2t::impulse (p190kwr2t::proR2 (0.25f, 0.5f, 1.0f), true, l, r);
    const double l100 = p190kwr2t::db (l), r100 = p190kwr2t::db (r);
    std::printf ("  width 100 %%, left impulse: L %.1f dB, R %.1f dB\n", l100, r100);
    CHECK (r100 < l100 - 100.0);
    // 0 %: Y = 0 — mono out (L == R), X = 1 + 0.8 (√2 − 1)
    p190kwr2t::impulse (p190kwr2t::proR2 (0.25f, 0.5f, 0.0f), true, l, r);
    bool mono = true;
    for (std::size_t i = 200; i < l.size(); ++i) mono = mono && l[i] == r[i];
    CHECK (mono);
    // 120 %: the side gains (Y = 1.1) over the mid (X = 0.75): a centred impulse's side / mid rises against 70 %
    const auto sideOverMid = [&] (float w) {
        p190kwr2t::impulse (p190kwr2t::proR2 (0.25f, 0.5f, w), false, l, r);
        std::vector<float> m (l.size()), s (l.size());
        for (std::size_t i = 0; i < l.size(); ++i) { m[i] = 0.5f * (l[i] + r[i]); s[i] = 0.5f * (l[i] - r[i]); }
        return p190kwr2t::db (s) - p190kwr2t::db (m);
    };
    const double s70 = sideOverMid (0.7f), s120 = sideOverMid (1.2f);
    std::printf ("  centred impulse side / mid: width 70 %% %.1f dB, 120 %% %.1f dB\n", s70, s120);
    CHECK (s120 > s70 + 1.0);
}

TEST (parity190kwR2_space0_level_and_brightness_law)
{
    using namespace lynx;
    std::vector<float> l, r;
    // Space 0: the reference still rings ~0.2 s at nearly the level of Space 25 % (−14.4 vs −18.1 dB); revision 0 was
    // 6 … 11 dB short of it
    const auto level = [&] (EffectSlot s, float rev) {
        s.params[(std::size_t) luxeVerbSpaceRevision] = rev;
        p190kwr2t::impulse (s, false, l, r);
        return p190kwr2t::db (l);
    };
    const auto s0 = p190kwr2t::proR2 (0.0f, 0.5f, 0.7f), s25 = p190kwr2t::proR2 (0.25f, 0.5f, 0.7f);
    const double r0s0 = level (s0, 0.0f), r1s0 = level (s0, 1.0f), r1s25 = level (s25, 1.0f);
    std::printf ("  Space 0: revision 0 %.1f dB, revision 1 %.1f dB (Space 25 %%: %.1f dB)\n", r0s0, r1s0, r1s25);
    CHECK (r1s0 > r0s0 + 4.0);
    CHECK (r1s0 > r1s25 && r1s0 < r1s25 + 8.0);
    // Brightness 100 %: no +12 dB shelf any more — the wet level within a few dB of 50 %; 0 % much darker
    const double b50 = level (p190kwr2t::proR2 (0.25f, 0.5f, 0.7f), 1.0f), b100 = level (p190kwr2t::proR2 (0.25f, 1.0f, 0.7f), 1.0f);
    const double b0 = level (p190kwr2t::proR2 (0.25f, 0.0f, 0.7f), 1.0f);
    std::printf ("  brightness 0 / 50 / 100 %%: %.1f / %.1f / %.1f dB\n", b0, b50, b100);
    CHECK (b100 < b50 + 4.0 && b100 > b50 - 4.0);
    CHECK (b0 < b50 - 2.0);
}

// Round 190lg: revision 1's levels refitted on the captures' energy per time window (0 … 5 ms … 6.4 s) and band. The late
// network carries the tail at every decay rate (190kw's fit had let the 1.15 × displayed-time early network carry it, so
// long rates died early and a ducked long decay left no tail), far Distances lose their early energy, and Thickness acts
// on a quiet wet only (the reference's Thickner: an impulse moves by −13.2 dB per unit, a loud wet hardly at all).
namespace p190lgr2t
{
// a Pro-R 2 state (Modern, 100 % wet): Space, Decay Rate (−1 … 1), Distance, Thickness (−1 … 1), Ducking (dB)
inline lynx::EffectSlot state (float space, float rate, float distance, float thick, float duckDb)
{
    std::vector<float> v (136, 0.0f);
    v[0] = space; v[1] = rate; v[2] = distance; v[3] = 0.5f; v[5] = 0.3f; v[6] = thick; v[7] = 0.7f; v[8] = duckDb; v[9] = 100.0f;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::luxeVerb);
    if (const auto st = lynx::fabfilter::parseState (p190kwr2t::ffbs (v))) lynx::fabfilter::fromProR2 (*st, slot);
    return slot;
}
// renders `in` (mono, both channels) through the slot; returns the left + right sum
inline std::vector<double> render (const lynx::EffectSlot& slot, const std::vector<float>& in, double sr = 48000.0)
{
    std::vector<float> l (in), r (in);
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) in.size();
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
    std::vector<double> m ((std::size_t) n);
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = (double) l[i] + r[i];
    return m;
}
inline double db (const std::vector<double>& x, double t0, double t1, double sr = 48000.0)
{
    double e = 0.0;
    for (std::size_t i = (std::size_t) (t0 * sr); i < std::min (x.size(), (std::size_t) (t1 * sr)); ++i) e += x[i] * x[i];
    return 10.0 * std::log10 (e + 1e-30);
}
inline std::vector<float> impulse (double seconds, double sr = 48000.0)
{
    std::vector<float> x ((std::size_t) (seconds * sr), 0.0f);
    x[100] = 0.5f;
    return x;
}
inline std::vector<float> noise (double seconds, double from, double to, float amp, double sr = 48000.0)
{
    std::vector<float> x ((std::size_t) (seconds * sr), 0.0f);
    unsigned s = 12345u;
    for (std::size_t i = (std::size_t) (from * sr); i < (std::size_t) (to * sr); ++i)
    {
        s = s * 1664525u + 1013904223u;
        x[i] = amp * (float) ((double) (s >> 8) / 8388608.0 - 1.0);
    }
    return x;
}
} // namespace p190lgr2t

TEST (parity190lgR2_the_tail_follows_the_decay_rate)
{
    using namespace p190lgr2t;
    // Space 50 % (2.5 s): Decay Rate −100 % (× 0.25) is gone by 1.6 s (the reference: −150 dB in 1.6 … 3.2 s), +100 %
    // (× 4) still rings there (−18.5 dB against −14.0 in 0.2 … 0.4 s)
    const auto shortRate = render (state (0.5f, -1.0f, 0.5f, 0.0f, 0.0f), impulse (4.0));
    const auto longRate = render (state (0.5f, 1.0f, 0.5f, 0.0f, 0.0f), impulse (4.0));
    const double s1 = db (shortRate, 0.2, 0.4) - db (shortRate, 1.6, 3.2), l1 = db (longRate, 0.2, 0.4) - db (longRate, 1.6, 3.2);
    std::printf ("  0.2 … 0.4 s over 1.6 … 3.2 s: rate × 0.25 %.1f dB, × 4 %.1f dB\n", s1, l1);
    CHECK (s1 > 60.0);
    CHECK (l1 < 12.0);
}

TEST (parity190lgR2_ducking_a_long_decay_leaves_its_tail)
{
    using namespace p190lgr2t;
    // a 0.3 s noise burst, Decay Rate +100 %: with Ducking at 48 dB the wet dips while the burst plays and its tail
    // comes back once the input stops (the reference: 0.5 … 1.5 s after the burst within ~3 dB of the un-ducked tail)
    const auto in = noise (3.0, 0.1, 0.4, 0.3f);
    const auto dry = render (state (0.25f, 1.0f, 0.5f, 0.0f, 0.0f), in), duck = render (state (0.25f, 1.0f, 0.5f, 0.0f, 48.0f), in);
    const double during = db (duck, 0.15, 0.4) - db (dry, 0.15, 0.4), after = db (duck, 0.9, 1.9) - db (dry, 0.9, 1.9);
    std::printf ("  ducked − plain: during the burst %.1f dB, 0.5 … 1.5 s after %.1f dB\n", during, after);
    CHECK (during < -20.0);
    CHECK (after > -4.0);
}

TEST (parity190lgR2_distance_takes_the_early_energy)
{
    using namespace p190lgr2t;
    // Distance 0 → 100 %: the reference's first 20 ms fall by ~25 dB, its tail (0.2 … 1 s) stays within ~2 dB
    const auto nearS = render (state (0.25f, 0.0f, 0.0f, 0.0f, 0.0f), impulse (1.5)), farS = render (state (0.25f, 0.0f, 1.0f, 0.0f, 0.0f), impulse (1.5));
    const double early = db (farS, 0.0, 0.0225) - db (nearS, 0.0, 0.0225), tail = db (farS, 0.2, 1.0) - db (nearS, 0.2, 1.0);
    std::printf ("  far − near: first 20 ms %.1f dB, 0.2 … 1 s %.1f dB\n", early, tail);
    CHECK (early < -10.0);
    CHECK (std::abs (tail) < 4.0);
}

TEST (parity190lgR2_thickness_acts_on_a_quiet_wet_only)
{
    using namespace p190lgr2t;
    // Thickness −100 % / +100 % against 0: an impulse's response by about −13 / +6 dB, a loud noise's wet (≈ −20 dBFS) by
    // under 2 dB (the reference: −13.2 / +6.6 dB and a music wet within 1.4 dB)
    const auto ir = impulse (1.5), loud = noise (1.5, 0.0, 1.5, 0.5f);
    const double i0 = db (render (state (0.25f, 0.0f, 0.5f, 0.0f, 0.0f), ir), 0.0, 1.5);
    const double iN = db (render (state (0.25f, 0.0f, 0.5f, -1.0f, 0.0f), ir), 0.0, 1.5) - i0;
    const double iP = db (render (state (0.25f, 0.0f, 0.5f, 1.0f, 0.0f), ir), 0.0, 1.5) - i0;
    const double n0 = db (render (state (0.25f, 0.0f, 0.5f, 0.0f, 0.0f), loud), 0.5, 1.5);
    const double nN = db (render (state (0.25f, 0.0f, 0.5f, -1.0f, 0.0f), loud), 0.5, 1.5) - n0;
    const double nP = db (render (state (0.25f, 0.0f, 0.5f, 1.0f, 0.0f), loud), 0.5, 1.5) - n0;
    std::printf ("  thickness −1 / +1: impulse %.1f / %+.1f dB, loud noise %.1f / %+.1f dB\n", iN, iP, nN, nP);
    CHECK (iN < -10.0 && iN > -16.0);
    CHECK (iP > 3.0 && iP < 8.0);
    CHECK (std::abs (nN) < 2.0 && std::abs (nP) < 2.0);
}
