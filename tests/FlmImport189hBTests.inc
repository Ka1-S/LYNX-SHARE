// --- FL Studio Mobile (.flm), round 189h: Pocket Wow & Flutter, Phaser, Flanger and Tremolo (core/PocketFxB.cpp). The
// expected values are worked by hand from FLM's laws in REA_rodada182c.md "F5 effects, round 189h".

namespace flm189hb
{
// a fresh engine at 48 kHz, 120 BPM, no song position, blocks of 512; `values` overwrite the defaults from the first
inline std::vector<std::array<float, 2>> run (lynx::EffectKind kind, std::vector<std::pair<int, float>> values, std::vector<std::array<float, 2>> in)
{
    using namespace lynx;
    auto fx = makeEffect (kind);
    fx->prepare (48000.0);
    auto p = makeEffectSlot (kind).params;
    for (const auto& [k, v] : values)
        p[(std::size_t) k] = v;
    std::vector<float> l, r;
    for (const auto& x : in)
    {
        l.push_back (x[0]);
        r.push_back (x[1]);
    }
    for (int at = 0; at < (int) l.size(); at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, (int) l.size() - at), p, 120.0);
    std::vector<std::array<float, 2>> out;
    for (std::size_t i = 0; i < l.size(); ++i)
        out.push_back ({ l[i], r[i] });
    return out;
}
inline std::vector<std::array<float, 2>> impulse (int n, float a = 1.0f)
{
    std::vector<std::array<float, 2>> x ((std::size_t) n, { 0.0f, 0.0f });
    x[0] = { a, a };
    return x;
}
inline bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }
} // namespace flm189hb

TEST (flm189h_pocket_wow_flutter)
{
    using namespace lynx;
    const auto K = EffectKind::pocketWowFlutter;
    const auto fs = 48000.0;
    const auto c = (double) (float) (1.0 - 44.1 / fs);
    // no wow and no flutter: the delay is 0, the output is the input (100 % wet of an undelayed read)
    {
        const auto o = flm189hb::run (K, { { 2, 0.0f }, { 5, 0.0f } }, { { 0.3f, -0.7f }, { 0.1f, 0.2f } });
        CHECK (o[0][0] == 0.3f && o[0][1] == -0.7f && o[1][0] == 0.1f && o[1][1] == 0.2f);
    }
    // flutter only, first sample: the LFO's value is 0 before its first step, d = Fd · s · 0.5 with s the smoother's
    // first step (1 − c) and Fd = 0.003 · fs; the read is (1 − d) × this sample (the one before is 0)
    {
        const auto o = flm189hb::run (K, { { 2, 0.0f }, { 5, 1.0f } }, { { 0.5f, 0.5f } });
        const auto d = 0.003 * fs * (1.0 - c) * 0.5;
        CHECK (flm189hb::near (o[0][0], 0.5 * (1.0 - d), 1e-6));
    }
    // full wow at 0.1 Hz on a ramp: the read lags by d = (sin(2π(i − 1)·0.1/fs) + 1) · 0.142 fs · (1 − cⁱ⁺¹) · 0.5
    // samples (linear interpolation is exact on a ramp)
    {
        std::vector<std::array<float, 2>> ramp (4001);
        for (std::size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = { (float) i * 1e-5f, (float) i * 1e-5f };
        const auto o = flm189hb::run (K, { { 0, 0.0f }, { 2, 1.0f }, { 5, 0.0f } }, ramp);
        for (const int i : { 3800, 4000 }) // (before ~3500 the read is still in the silence before the ramp)
        {
            const auto lfo = std::sin (6.283185307179586 * (double) (i - 1) * 0.1 / fs);
            const auto d = (lfo + 1.0) * (double) (float) (0.142 * fs) * (1.0 - std::pow (c, i + 1)) * 0.5;
            CHECK (flm189hb::near ((double) i - (double) o[(std::size_t) i][0] / 1e-5, d, 0.05));
        }
    }
    // a constant input comes out constant once the ring holds it (defaults: up to ~115 + 144 samples of delay)
    {
        std::vector<std::array<float, 2>> dc (20000, { 0.5f, -0.5f });
        const auto o = flm189hb::run (K, {}, dc);
        CHECK (flm189hb::near (o[19999][0], 0.5, 1e-6) && flm189hb::near (o[19999][1], -0.5, 1e-6));
    }
    CHECK (formatEffectParam (K, 0, 0.184564f, 120.0) == "1.00 Hz");
    CHECK (formatEffectParam (K, 1, 0.2f, 120.0) == "1/4");
    CHECK (formatEffectParam (K, 8, 0.8f, 120.0) == "4 beats");
    CHECK (formatEffectParam (K, 7, 0.08952f, 120.0) == "1.0 Hz");
}

TEST (flm189h_pocket_phaser)
{
    using namespace lynx;
    const auto K = EffectKind::pocketPhaser;
    const auto fs = 48000.0;
    const auto lo = (double) (float) (50.0 / fs), hi = (double) (float) (16000.0 / fs);
    // first sample: six all-passes from rest give (−a)⁶ x; the wet adds on top of the dry. Defaults: Frequency 0.25
    // (base 0.0625), the sine LFO at phase 0 (and half a cycle for the right side) = 0
    {
        const auto cc = 0.0625 * (hi - lo) + lo, a = (1.0 - cc) / (1.0 + cc);
        const auto o = flm189hb::run (K, {}, { { 0.4f, -0.2f } });
        CHECK (flm189hb::near (o[0][0], 0.4 * (1.0 + std::pow (a, 6)), 1e-6) && flm189hb::near (o[0][1], -0.2 * (1.0 + std::pow (a, 6)), 1e-6));
    }
    // square LFO at full amount, no stereo: −1 at phase 0, (0.25·(−1) + 0.0625) clamps to 50 Hz
    {
        const auto a = (1.0 - lo) / (1.0 + lo);
        const auto o = flm189hb::run (K, { { 1, 1.0f }, { 7, 1.0f }, { 8, 0.0f } }, { { 0.4f, 0.4f } });
        CHECK (flm189hb::near (o[0][0], 0.4 * (1.0 + std::pow (a, 6)), 1e-6));
    }
    // Mix 0: the dry only
    {
        const auto o = flm189hb::run (K, { { 4, 0.0f } }, { { 0.4f, 0.1f }, { -0.3f, 0.2f } });
        CHECK (o[1][0] == -0.3f && o[1][1] == 0.2f);
    }
    CHECK (formatEffectParam (K, 0, 0.112222f, 120.0) == "3/8");
    CHECK (formatEffectParam (K, 3, 0.25f, 120.0) == "1047 Hz");
}

TEST (flm189h_pocket_flanger)
{
    using namespace lynx;
    const auto K = EffectKind::pocketFlanger;
    // no LFO amount, Delay 1 (x = 0.5 of the 240-sample tape at 48 kHz): one loop = 0.5 · (240 − 2) + 2 = 121 samples;
    // no feedback, wet not inverted: the impulse comes back once, ~122 samples later
    {
        const auto o = flm189hb::run (K, { { 4, 0.0f }, { 5, 1.0f }, { 6, 0.0f }, { 10, 0.0f } }, flm189hb::impulse (400));
        int at = 0;
        float peak = 0.0f, early = 0.0f;
        for (int i = 1; i < 400; ++i)
        {
            if (std::abs (o[(std::size_t) i][0]) > std::abs (peak))
                peak = o[(std::size_t) i][0], at = i;
            if (i < 119)
                early = std::max (early, std::abs (o[(std::size_t) i][0]));
        }
        CHECK (o[0][0] == 1.0f && at >= 120 && at <= 123 && peak > 0.5f && early == 0.0f);
        float late = 0.0f;
        for (int i = 130; i < 400; ++i)
            late = std::max (late, std::abs (o[(std::size_t) i][0]));
        CHECK (late == 0.0f);
    }
    // inverted feedback 0.5: the second echo is −0.5 × the first, one loop later
    {
        const auto o = flm189hb::run (K, { { 4, 0.0f }, { 5, 1.0f }, { 6, 0.5f }, { 7, 1.0f }, { 10, 0.0f } }, flm189hb::impulse (400));
        double e1 = 0.0, e2 = 0.0;
        for (int i = 100; i < 180; ++i)
            e1 += o[(std::size_t) i][0];
        for (int i = 200; i < 300; ++i)
            e2 += o[(std::size_t) i][0];
        CHECK (e1 > 0.9 && flm189hb::near (e2, -0.5 * e1, 0.02));
    }
    // Mix 0: the dry only (inverted or not)
    {
        const auto o = flm189hb::run (K, { { 9, 0.0f } }, flm189hb::impulse (300, 0.25f));
        bool dry = o[0][0] == 0.25f;
        for (int i = 1; i < 300; ++i)
            dry = dry && o[(std::size_t) i][0] == 0.0f;
        CHECK (dry);
    }
    CHECK (formatEffectParam (K, 5, 0.25f, 120.0) == "0.16 ms");
    CHECK (formatEffectParam (K, 0, 0.4f, 120.0) == "1/1");
}

TEST (flm189h_pocket_tremolo)
{
    using namespace lynx;
    const auto K = EffectKind::pocketTremolo;
    const auto cg = (double) (float) std::exp (-2094.395084187993 / 48000.0);
    // first sample: the LFO is 0 before its first step: gain (0 + 1)·(1 − 0)·0.495, through the 333 Hz one-pole
    {
        const auto o = flm189hb::run (K, {}, { { 1.0f, 1.0f } });
        CHECK (flm189hb::near (o[0][0], (1.0 - cg) * 0.495, 1e-6));
    }
    // Min = Max = 0.5: the gain is 0.5 whatever the LFO
    {
        const auto o = flm189hb::run (K, { { 2, 0.5f }, { 3, 0.5f } }, std::vector<std::array<float, 2>> (2000, { 1.0f, 1.0f }));
        CHECK (flm189hb::near (o[1999][0], 0.5, 1e-6));
    }
    // synced to 1/8 beat at 120 BPM (3000 samples a cycle); the default shape 3 is sin(π/2 · (2·tri − 1)) blended 0.999
    // with the line; sample i uses the phase (i − 1)/3000. The gain g = (y_i − c·y_(i−1))/(1 − c)
    {
        const auto o = flm189hb::run (K, { { 1, 0.0f }, { 6, 1.0f } }, std::vector<std::array<float, 2>> (1600, { 1.0f, 1.0f }));
        const auto g = [&] (int i) { return ((double) o[(std::size_t) i][0] - cg * (double) o[(std::size_t) i - 1][0]) / (1.0 - cg); };
        const auto shape = [] (double u) { return 0.999 * std::sin (u * 1.5707963267948966) + 0.001 * u; };
        CHECK (flm189hb::near (g (751), 0.495 * (shape (0.0) + 1.0), 2e-4));
        CHECK (flm189hb::near (g (1126), 0.495 * (shape (0.5) + 1.0), 2e-4));
        CHECK (flm189hb::near (g (1501), 0.495 * (shape (1.0) + 1.0), 2e-4));
    }
    // Width 0.75 (W = −0.5): u = 4·tri − 1 clamps at +1 from phase 0.25 on
    {
        const auto o = flm189hb::run (K, { { 1, 0.0f }, { 6, 1.0f }, { 10, 0.75f } }, std::vector<std::array<float, 2>> (1600, { 1.0f, 1.0f }));
        const auto g = ((double) o[1200][0] - cg * (double) o[1199][0]) / (1.0 - cg);
        CHECK (flm189hb::near (g, 0.99, 2e-4));
    }
    CHECK (formatEffectParam (K, 0, 0.045467f, 120.0) == "1.00 Hz");
    CHECK (formatEffectParam (K, 1, 0.0f, 120.0) == "1/8");
    CHECK (formatEffectParam (K, 9, 0.5f, 120.0) == "0");
}

TEST (flm189h_pocket_extremes)
{
    using namespace lynx;
    // every value at 0 and at 1 on loud noise: finite output, nothing runs away
    std::vector<std::array<float, 2>> noise (20000);
    std::uint32_t s = 1;
    for (auto& x : noise)
    {
        s = s * 1664525u + 1013904223u;
        x[0] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 4.0f;
        x[1] = -x[0];
    }
    for (const auto K : { EffectKind::pocketWowFlutter, EffectKind::pocketPhaser, EffectKind::pocketFlanger, EffectKind::pocketTremolo })
        for (const float v : { 0.0f, 1.0f })
        {
            std::vector<std::pair<int, float>> all;
            for (int k = 0; k < 12; ++k)
                all.push_back ({ k, v });
            const auto o = flm189hb::run (K, all, noise);
            bool ok = true;
            for (const auto& x : o)
                ok = ok && std::isfinite (x[0]) && std::isfinite (x[1]) && std::abs (x[0]) < 1e4f && std::abs (x[1]) < 1e4f;
            CHECK (ok);
        }
}

TEST (flm189h_maps_pocket_modules_b)
{
    using namespace lynx;
    // Wow & Flutter 43, Phaser 14, Flanger 13, Tremolo 46: FLM's values copied (parameter k + 1 → slot parameter k)
    const auto w = flmfx::map (flm188f::moduleOf (43, { 1.0f, 0.3f, 0.2f, 0.8f }));
    CHECK (w.slots.size() == 1 && w.label == "FL Studio Mobile: Wow & Flutter");
    if (w.slots.size() == 1)
        CHECK (w.slots[0].kind == EffectKind::pocketWowFlutter && w.slots[0].params[0] == 0.3f && w.slots[0].params[2] == 0.8f
               && w.slots[0].params[8] == 0.8f);
    const auto p = flmfx::map (flm188f::moduleOf (14, { 0.0f, 0.6f }));
    CHECK (p.slots.size() == 1 && p.slots[0].kind == EffectKind::pocketPhaser && ! p.slots[0].enabled && p.slots[0].params[0] == 0.6f);
    const auto f = flmfx::map (flm188f::moduleOf (13, { 1.0f, 0.4f, 0.0f, 0.55f, 0.0f, 1.0f, 0.5f, 0.9f }));
    CHECK (f.slots.size() == 1 && f.slots[0].kind == EffectKind::pocketFlanger && f.slots[0].params[6] == 0.9f && f.slots[0].params[10] == 1.0f);
    const auto t = flmfx::map (flm188f::moduleOf (46, { 1.0f, 0.2f, 0.0f, 1.0f, 0.3f }));
    CHECK (t.slots.size() == 1 && t.slots[0].kind == EffectKind::pocketTremolo && t.slots[0].params[3] == 0.3f && t.slots[0].params[7] == 0.5f);
}

// Round 190g: the two controls the effect audit cannot see move the sound where FLM's laws say they do.
namespace flm190g
{
inline std::vector<std::array<float, 2>> render (lynx::EffectSlot slot, int moveParam = -1, float moveTo = 0.0f)
{
    using namespace lynx;
    auto fx = makeEffect (slot);
    fx->prepare (44100.0);
    std::vector<float> l (88200), r (88200);
    for (int i = 0; i < 88200; ++i)
    {
        l[(std::size_t) i] = 0.4f * std::sin ((float) i * 0.031f);
        r[(std::size_t) i] = 0.4f * std::sin ((float) i * 0.047f);
    }
    for (int done = 0; done < 88200; done += 441)
    {
        if (moveParam >= 0 && done == 4410)
            slot.params[(std::size_t) moveParam] = moveTo; // a knob moved while playing
        fx->process (l.data() + done, r.data() + done, 441, slot.params, 120.0);
    }
    std::vector<std::array<float, 2>> out (88200);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = { l[i], r[i] };
    return out;
}
inline double difference (const std::vector<std::array<float, 2>>& a, const std::vector<std::array<float, 2>>& b, int channel)
{
    double e = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        e += (double) (a[i][(std::size_t) channel] - b[i][(std::size_t) channel]) * (double) (a[i][(std::size_t) channel] - b[i][(std::size_t) channel]);
    return e;
}
} // namespace flm190g

TEST (flm190g_tremolo_stereo_offset_moves_the_right_channel_only)
{
    using namespace lynx;
    const auto base = flm190g::render (makeEffectSlot (EffectKind::pocketTremolo));
    for (const float v : { 0.25f, 0.5f })
    {
        auto slot = makeEffectSlot (EffectKind::pocketTremolo);
        slot.params[5] = v; // Stereo offset: 90°, 180° (added to the right LFO's phase)
        const auto out = flm190g::render (slot);
        CHECK (flm190g::difference (base, out, 0) == 0.0);
        CHECK (flm190g::difference (base, out, 1) > 1.0);
    }
    auto whole = makeEffectSlot (EffectKind::pocketTremolo);
    whole.params[5] = 1.0f; // 360°: a whole cycle, the same phase (what the effect audit tries)
    CHECK (flm190g::difference (base, flm190g::render (whole), 1) == 0.0);
}

TEST (flm190g_wow_random_rate_acts_when_moved_not_when_loaded)
{
    using namespace lynx;
    // FLM writes the sync length (parameter 8) after the free rate (7): a loaded state takes its random interval from
    // the sync length (4 beats = 2 s at 120 bpm), whatever the free rate is
    const auto base = flm190g::render (makeEffectSlot (EffectKind::pocketWowFlutter));
    auto loaded = makeEffectSlot (EffectKind::pocketWowFlutter);
    loaded.params[7] = 1.0f;
    const auto same = flm190g::render (loaded);
    CHECK (flm190g::difference (base, same, 0) == 0.0 && flm190g::difference (base, same, 1) == 0.0);
    // moved while playing, the free rate (10 Hz) sets the interval: the wow rate is re-picked every 0.1 s
    const auto moved = flm190g::render (makeEffectSlot (EffectKind::pocketWowFlutter), 7, 1.0f);
    CHECK (flm190g::difference (base, moved, 0) > 1e-6);
}
