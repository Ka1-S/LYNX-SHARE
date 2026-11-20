// Round 190kz (dsp, owner decision 4: CPU items that could change the sound): what landed exactly — Reeverb 2's LFO sine
// and the FL sinc kernel's seeds from a guarded polynomial (the same floats as the library sine) — and Maximus's double
// gain smoothers as EffectSlot revision 1 (the same samples on every demo state). Evidence and numbers:
// /mnt/e/lynx-work/beatfl2_190kz/agents/dsp.

TEST (dsp190kz_maximus_double_smoothers_are_a_revision_with_the_same_samples)
{
    // owner decision 4b: Maximus's gain smoothers in double precision (EffectSlot::engineRev 1) instead of the x87 unit's
    // (FL's own arithmetic, revision 0 = projects saved before). On every Maximus state of the demo projects the two give
    // the same samples (agents/dsp/c/mx-ab-all.txt); here: the engine test's own states (SG_x64.dll goldens), every block
    using Env = std::vector<std::array<double, 4>>;
    const Env def { { 0.0, 0.0, 0.0, 0 }, { 0.5, 0.5, 0.0, 0 }, { 0.5, 0.5, 0.0, 0 } };
    const Env knee { { 0.0, 0.0, 0.0, 0 }, { 0.35, 0.35, 0.0, 0 }, { 0.65, 0.6, 0.36, 7 } };
    const Env t0 { { 0.0, 0.0, 0.0, 0 }, { 0.5, 0.5, 0.14, 0 }, { 0.5, 0.5, 0.0, 0 } };
    auto band = [] (int mode, int pre = 1000, int post = 1000, int sep = 0, int sat = 0, int ceil = 1000, int att = 1115, int rel2 = 0,
                    int sus = 2882, int rms = 0) { return std::array<int, 13> { mode, pre, post, sep, sat, ceil, att, rel2, 2, 5000, 3, sus, rms }; };
    const auto st = maximusTestState ({ 19800, 1, 48065, 0, 1115, 1000, 0 },
                                      { band (0, 1500, 1000, 0, 400, 900, 1115, 0, 2882, 1), band (0, 1340, 1000, 0, 0, 1000, 1115, 0, 1570, 1),
                                        band (1, 1000, 1200, 0, 1000, 1000), band (0, 1000, 1000, 0, 250, 1000, 1115, 4464, 2882, 0) },
                                      { def, def, knee, def, t0, def, def, def }, 0);
    const auto preset = lynx::hlfx::makeMaximusPreset (st);
    CHECK (preset != nullptr);
    std::vector<float> inL (44100), inR (44100);
    std::uint32_t r = 7;
    for (int i = 0; i < 44100; ++i)
    {
        r = r * 1664525u + 1013904223u;
        const auto noise = (float) ((r >> 8) & 0xffff) / 32768.0f - 1.0f;
        const auto burst = (i / 4410) % 3 == 0 ? 1.4f : 0.2f;
        inL[(std::size_t) i] = burst * (float) std::sin (2.0 * 3.141592653589793 * 110.0 * i / 44100.0) + 0.05f * noise;
        inR[(std::size_t) i] = burst * (float) std::sin (2.0 * 3.141592653589793 * 165.0 * i / 44100.0) - 0.05f * noise;
    }
    for (const int block : { 37, 128, 480, 512 })
    {
        std::vector<float> l0 (inL), r0 (inR), l1 (inL), r1 (inR);
        lynx::hlfx::MaximusProcessor x87, dbl;
        x87.prepare (preset, 44100.0, 1000, 0);
        dbl.prepare (preset, 44100.0, 1000, 1);
        for (int s = 0; s < 44100; s += block)
        {
            const auto n = std::min (block, 44100 - s);
            x87.process (l0.data() + s, r0.data() + s, n);
            dbl.process (l1.data() + s, r1.data() + s, n);
        }
        CHECK (l0 == l1 && r0 == r1);
    }
    // the slot's revision reaches the engine: a new slot is revision 1, a loaded old one 0, and both play
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::maximus);
    CHECK (slot.engineRev == 1);
    lynx::maximusfx::Blob blob;
    blob.state = st;
    slot.state = lynx::maximusfx::pack (blob);
    for (const int rev : { 0, 1 })
    {
        slot.engineRev = rev;
        auto fx = lynx::makeEffect (slot);
        CHECK (fx != nullptr);
        fx->prepare (44100.0);
        std::vector<float> l (inL.begin(), inL.begin() + 4096), rr (inR.begin(), inR.begin() + 4096);
        fx->process (l.data(), rr.data(), 4096, slot.params, 120.0);
        double e = 0.0;
        for (const auto v : l)
            e += (double) v * v;
        CHECK (e > 1.0);
    }
}

TEST (dsp190kz_new_channels_and_slots_take_the_current_revisions)
{
    // (.lynx: src/engine/ProjectFile.cpp writes "engineRev" when it is not 0 and reads a missing one as 0, for channels as
    // since natives190kj and now for effect slots: a project saved before plays the old revisions)
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    CHECK (c.engineRev == 1);
    CHECK (lynx::makeEffectSlot (lynx::EffectKind::maximus).engineRev == 1);
    lynx::EffectSlot a = lynx::makeEffectSlot (lynx::EffectKind::maximus), b = a;
    b.engineRev = 0;
    CHECK (! (a == b)); // (undo and the rebuild see a revision change)
}

TEST (dsp190kz_reeverb2_lfo_sine_is_the_plain_sine_bit_for_bit)
{
    // owner decision 4f, done exactly: Reeverb 2's LFO sine (a guarded polynomial) returns what (float) std::sin
    // ((double) ph · 2π) returns. Every float phase in [0, 1) was checked once (1,065,353,216 identical, round 190kz
    // agents/dsp/f/lfosin.txt); here a stride through them, the quadrant edges and phases outside [0, 1)
    std::uint64_t checked = 0, differing = 0;
    auto check = [&] (float ph) {
        const auto want = (float) std::sin ((double) ph * 6.283185307179586), got = lynx::reeverb2::lfoSine (ph);
        differing += std::memcmp (&want, &got, 4) != 0;
        ++checked;
    };
    for (std::uint32_t u = 0; u < 0x3F800000u; u += 997)
    {
        float ph;
        std::memcpy (&ph, &u, 4);
        check (ph);
    }
    for (const float ph : { 0.0f, 0.25f, 0.5f, 0.75f, std::nextafter (0.25f, 0.0f), std::nextafter (0.25f, 1.0f), std::nextafter (0.5f, 0.0f),
                            std::nextafter (0.75f, 1.0f), std::nextafter (1.0f, 0.0f), 1.0f, 1.5f, -0.25f, 3.0e-8f })
        check (ph);
    CHECK (checked > 1000000 && differing == 0);
}

TEST (dsp190kz_sampler_sinc_seeds_are_the_library_sines_bit_for_bit)
{
    // owner decision 4e, done exactly: the FL-sinc kernel's four sines per frame come from a guarded polynomial; its float
    // seeds equal the library sines' (the guard falls back to std::sin within 1e-14 of a float boundary). Random frames
    // over every kernel width and cutoff the sampler uses, plus the speeds of a 48 kHz device
    std::uint32_t r = 190;
    auto next = [&r] { r = r * 1664525u + 1013904223u; return (double) (r >> 8) / 16777216.0; };
    long frames = 0, differing = 0;
    for (const int quality : { 1, 2, 3, 4, 5, 6, 7, 8, 9 })
        for (const double speed : { 0.25, 0.5, 0.75, 0.9183673469387755, 1.0, 1.0884353741496599, 1.5, 2.0, 3.7 })
        {
            lynx::flrs::Kernel k;
            k.setQuality (quality);
            k.setSpeed (speed);
            for (int i = 0; i < 20000; ++i)
            {
                const double x = std::max (1e-25, next());
                const auto na = -(long double) k.step, w = (long double) k.cutoff, e0 = (long double) x * na + 1.5707963267948966L, e2 = (long double) x * w;
                float a[8], b[8];
                lynx::flrs::sincSeeds (k, (double) e0, (double) (e0 + na), (double) e2, (double) (e2 + w), a, false);
                lynx::flrs::sincSeeds (k, (double) e0, (double) (e0 + na), (double) e2, (double) (e2 + w), b, true);
                differing += std::memcmp (a, b, sizeof a) != 0;
                ++frames;
            }
        }
    CHECK (frames == 9 * 9 * 20000 && differing == 0);
}
