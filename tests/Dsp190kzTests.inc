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

// Round 190kz (dsp, owner decision 4a, opt-in): a voice that keeps its pitch reads its sample directly while its shift is
// exactly unity (ChannelDef::engineRev 2) instead of running its real-time shifter at a factor of 1 under a tempo map (issue
// #187's rule). engineRev 0 / 1 (every project saved before, every new channel and import) render exactly as before; the
// bypass cross-fades into the shifter when the tempo moves and back when it returns. Not the default: against FL's own
// render of Thief it came out slightly further from FL than the shifter (agents/dsp NOTES (a)).

namespace dsp190kz
{
// a warped, pitch-kept clip at 120 BPM under a tempo clip: 120 throughout (rampTo 0), or 120 → rampTo over the second
// quarter, back to 120 over the third, 120 to the end
inline lynx::Song unitySong (int engineRev, double rampTo, double beats = 8.0)
{
    using namespace lynx;
    auto song = warp190ik::rampSong (beats, true, true);
    song.channels.back().engineRev = engineRev;
    for (auto& a : song.automations)
        if (a.target.kind == AutomationTargetKind::tempo)
        {
            const auto len = (std::int64_t) std::llround (beats * ppq);
            if (rampTo > 0.0)
                a.points = { { 0, valueFromTempo (120.0) }, { len / 4, valueFromTempo (120.0) }, { len / 2, valueFromTempo (rampTo) },
                             { 3 * len / 4, valueFromTempo (120.0) }, { len + ppq, valueFromTempo (120.0) } };
            else
                a.points = { { 0, valueFromTempo (120.0) }, { len + ppq, valueFromTempo (120.0) } };
        }
    return song;
}

inline std::shared_ptr<const lynx::SampleData> tone (double hz)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    for (int i = 0; i < 44100 * 4; ++i)
        d->left.push_back ((float) (0.3 * std::sin (warp190ik::twoPi * hz * i / 44100.0) + 0.1 * std::sin (warp190ik::twoPi * 2.0 * hz * i / 44100.0)));
    d->right = d->left;
    return d;
}

inline std::uint64_t fnv (const std::vector<float>& x)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const auto v : x)
    {
        std::uint32_t u;
        std::memcpy (&u, &v, 4);
        h = (h ^ u) * 1099511628211ull;
    }
    return h;
}

// the largest step between neighbouring samples over [from, to): a click shows as a step beyond the tone's own
inline double largestStep (const std::vector<float>& x, long from, long to)
{
    double m = 0.0;
    for (long i = std::max (1L, from); i < std::min ((long) x.size(), to); ++i)
        m = std::max (m, (double) std::abs (x[(std::size_t) i] - x[(std::size_t) i - 1]));
    return m;
}
} // namespace dsp190kz

TEST (dsp190kz_unity_bypass_keeps_old_revisions_and_plays_the_direct_read)
{
    using namespace lynx;
    using namespace dsp190kz;
    const auto data = tone (330.0);
    const auto rev1 = warp190ik::render (unitySong (1, 0.0), data, 4.2).left;
    const auto rev0 = warp190ik::render (unitySong (0, 0.0), data, 4.2).left;
    const auto rev2 = warp190ik::render (unitySong (2, 0.0), data, 4.2).left;
    // revisions 0 and 1 run the shifter at a factor of 1, as before (the base commit's samples: hash pinned)
    CHECK (rev0 == rev1);
    // (the base commit's samples for revision 1: proved by perfbench --engine-rev 1 --fx-rev 0 against the base hashes,
    // agents/dsp/hashes-old.txt; the hashes printed here pin this test's own renders)
    std::printf ("  dsp190kz: rev1 hash %016llx rev2 hash %016llx\n", (unsigned long long) fnv (rev1), (unsigned long long) fnv (rev2));
    // revision 2 at the song's own tempo is the direct read: bit-identical to the same clip without a tempo map
    auto plain = unitySong (2, 0.0);
    plain.playlist.erase (std::remove_if (plain.playlist.begin(), plain.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::automation; }),
                          plain.playlist.end());
    // (over the clip: the song without its tempo clip ends with the clip, the other one a beat later)
    const auto direct = warp190ik::render (plain, data, 4.2).left;
    CHECK (std::equal (rev2.begin(), rev2.begin() + 44100 * 4 - 64, direct.begin()));
    CHECK (rev2 != rev1);
    // and the shifter at a factor of 1 was already close to it: its output runs one sample ahead of the direct read
    // (measured), and aligned so the two differ far below the sound
    double e = 0.0, d = 0.0;
    for (std::size_t i = 22050; i < 44100 * 3; ++i)
    {
        e += (double) rev2[i + 1] * rev2[i + 1];
        d += ((double) rev1[i] - rev2[i + 1]) * ((double) rev1[i] - rev2[i + 1]);
    }
    std::printf ("  dsp190kz: rev 1 vs rev 2 (one sample apart): %.1f dB\n", 10.0 * std::log10 (d / std::max (e, 1e-30)));
    CHECK (e > 1.0 && 10.0 * std::log10 (d / e) < -30.0);
}

TEST (dsp190kz_unity_bypass_follows_a_tempo_ramp_and_back_without_clicks)
{
    using namespace lynx;
    using namespace dsp190kz;
    // 120 → 90 → 120 BPM over the middle of 8 beats: revision 2 leaves the direct read when the tempo moves and comes back
    // when it returns; pitch kept, no click at either change, and the beats where revision 1 puts them
    const auto data = tone (440.0);
    const auto r1 = warp190ik::render (unitySong (1, 90.0), data, 5.0);
    const auto r2 = warp190ik::render (unitySong (2, 90.0), data, 5.0);
    const auto at = (long) r2.sampleOfTick (4.0 * ppq); // (the slowest point)
    CHECK (std::abs (warp190ik::cents (warp190ik::peakFrequency (r2.left, at - 6000, at + 6000, 44100.0, 300.0, 600.0), 440.0)) < 15.0);
    const auto leave = (long) r2.sampleOfTick (2.0 * ppq), back = (long) r2.sampleOfTick (6.0 * ppq);
    // the tone's own largest step is 2π·(0.3·440 + 0.2·440)/44100 ≈ 0.031; a click would be far beyond it
    const auto own = largestStep (r1.left, 4410, (long) r1.left.size() - 4410);
    CHECK (largestStep (r2.left, leave - 4410, leave + 8820) < own * 1.5);
    CHECK (largestStep (r2.left, back - 4410, back + 8820) < own * 1.5);
    // after the return, revision 2 reads directly again: the tone at its pitch and level (revision 1's shifter keeps a
    // phase of its own there, so the two are compared by pitch and level, not sample by sample)
    double e1 = 0.0, e2 = 0.0;
    const auto from = back + 8820, to = (long) r2.sampleOfTick (7.5 * ppq);
    for (long i = from; i < to; ++i)
        e1 += (double) r1.left[(std::size_t) i] * r1.left[(std::size_t) i], e2 += (double) r2.left[(std::size_t) i] * r2.left[(std::size_t) i];
    std::printf ("  dsp190kz: after the ramp, level revision 2 vs 1: %+.2f dB\n", 10.0 * std::log10 (e2 / std::max (e1, 1e-30)));
    CHECK (e1 > 1.0 && std::abs (10.0 * std::log10 (e2 / e1)) < 1.0);
    CHECK (std::abs (warp190ik::cents (warp190ik::peakFrequency (r2.left, from, to, 44100.0, 300.0, 600.0), 440.0)) < 10.0);
}

TEST (dsp190kz_unity_bypass_clicks_stay_beat_locked_under_a_ramp)
{
    using namespace lynx;
    using namespace dsp190kz;
    // issue #187's beat lock, with revision 2 (one click per beat of the file; 120 → 90 → 120)
    std::vector<double> beatsAt;
    for (int b = 0; b < 8; ++b)
        beatsAt.push_back (0.5 * b);
    const auto data = warp190ik::clicks (beatsAt);
    const auto refShift = warp190ik::clickAt (warp190ik::render (warp190ik::rampSong (8.0, true, false), data, 4.2).left, 0);
    const auto r = warp190ik::render (unitySong (2, 90.0), data, 5.2);
    int locked = 0;
    for (int b = 1; b < 8; ++b)
    {
        const auto want = (long) std::llround (r.sampleOfTick ((double) b * ppq)) + refShift;
        locked += std::abs (warp190ik::clickAt (r.left, want) - want) <= 265; // 6 ms
    }
    CHECK (locked == 7);
}
