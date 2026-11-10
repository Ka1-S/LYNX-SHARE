// --- FL Studio Mobile (.flm), round 189h: Pocket Autoduck, Chorus, Spacer and Stereoizer (core/PocketFxA.cpp). The
// expected values are worked by hand from FLM's laws in REA_rodada182c.md "F5 effects, round 189h".

namespace flm189ha
{
// a fresh engine at 48 kHz, 120 BPM; `pre` frames of silence first (lets the Spacer's glides settle), then `in` in blocks
// of `block` frames, the song position from 0 at the first input frame
struct Run
{
    std::vector<float> l, r;
};
inline Run run (lynx::EffectKind kind, std::initializer_list<float> values, std::vector<float> l, std::vector<float> r,
                int pre = 0, int block = 0)
{
    using namespace lynx;
    auto fx = makeEffect (kind);
    fx->prepare (48000.0);
    auto p = makeEffectSlot (kind).params;
    std::size_t k = 0;
    for (const auto v : values)
        p[k++] = v;
    std::vector<float> zl (4096), zr (4096);
    for (int left = pre; left > 0; left -= 4096)
    {
        std::fill (zl.begin(), zl.end(), 0.0f);
        std::fill (zr.begin(), zr.end(), 0.0f);
        fx->process (zl.data(), zr.data(), std::min (left, 4096), p, 120.0);
    }
    for (int at = 0; at < (int) l.size();)
    {
        const auto n = std::min (block > 0 ? block : (int) l.size(), (int) l.size() - at);
        fx->setSongPosition ((double) at * 0.04); // LYNX ticks (960 a beat): 24000 samples a beat
        fx->process (l.data() + at, r.data() + at, n, p, 120.0);
        at += n;
    }
    return { l, r };
}
inline std::vector<float> impulse (int n, int at = 0)
{
    std::vector<float> x ((std::size_t) n, 0.0f);
    x[(std::size_t) at] = 1.0f;
    return x;
}
inline bool near (float a, double b, double tol = 1e-5) { return std::abs ((double) a - b) <= tol; }
} // namespace flm189ha

TEST (flm189ha_pocket_autoduck_envelope)
{
    using namespace lynx;
    auto p = makeEffectSlot (EffectKind::pocketAutoduck).params;
    // defaults: a = 0.5·0.1 + 0.001 = 0.051 beat, Time 1, Curve 0.501 → q = 0.7505 on the rise and on the fall
    CHECK (pocket::autoduckEnvelope (p, 0.0f) == 0.0f);
    CHECK (flm189ha::near (pocket::autoduckEnvelope (p, 0.0255f), std::pow (0.5, 0.7505), 1e-4)); // half the rise
    CHECK (pocket::autoduckEnvelope (p, 0.051f) == 1.0f);
    CHECK (flm189ha::near (pocket::autoduckEnvelope (p, 0.5255f), std::pow (0.5, 0.7505), 1e-4)); // half the fall
    // Shape 3 (Decel 2) at Curve 0: the fall is e^6; the rise keeps (c + 1)/2 = ½
    p[2] = 0.0f;
    p[6] = 0.6f;
    CHECK (flm189ha::near (pocket::autoduckEnvelope (p, 0.5255f), 1.0 / 64.0, 1e-4));
    CHECK (flm189ha::near (pocket::autoduckEnvelope (p, 0.0255f), std::sqrt (0.5), 1e-4));
    // Shape 4 (S-Curve) at Curve 0: q = 3; e = 0.25 → 0.25³·2² = 1/16; e = 0.75 → 1 − 1/16
    p[6] = 0.8f;
    const auto atE = [] (double e) { return (float) (0.051 + (1.0 - e) * 0.949); };
    CHECK (flm189ha::near (pocket::autoduckEnvelope (p, atE (0.25)), 0.0625, 1e-3));
    CHECK (flm189ha::near (pocket::autoduckEnvelope (p, atE (0.75)), 0.9375, 1e-3));
    // Time 0.5: nothing after half the beat; Hold 1 holds the top from a to Time
    p[6] = 0.0f;
    p[3] = 0.5f;
    CHECK (pocket::autoduckEnvelope (p, 0.6f) == 0.0f);
    p[5] = 1.0f;
    CHECK (pocket::autoduckEnvelope (p, 0.45f) == 1.0f);
    // Shift 0.75: the curve starts a quarter beat later
    p[5] = 0.0f;
    p[4] = 0.75f;
    CHECK (pocket::autoduckEnvelope (p, 0.2f) == 0.0f);
    CHECK (pocket::autoduckEnvelope (p, 0.25f + 0.051f) == 1.0f);
    // texts: Attack in ms at the song tempo, Curve 1 reads Linear, Hold as a share of Time, the Shape list
    CHECK (pocket::autoduckParamText (1, 0.1f, 100.0) == "31 ms");
    CHECK (pocket::autoduckParamText (2, 1.0f, 120.0) == "Linear");
    CHECK (pocket::autoduckParamText (2, 0.25f, 120.0) == "75 %");
    CHECK (formatEffectParam (EffectKind::pocketAutoduck, 5, 0.5f, 120.0, p) == "25 %");
    CHECK (pocket::autoduckParamText (6, 0.8f, 120.0) == "S-Curve");
}

TEST (flm189ha_pocket_autoduck_gain)
{
    using namespace lynx;
    // Ducking 0.25 → depth ½; Attack 0 (a = 0.001), Hold 1, Time 0.5: the gain heads for ½ over the first half beat and
    // back to 1 over the second, through the 0.01 one-pole
    const std::vector<float> ones (48000, 1.0f);
    const auto o = flm189ha::run (EffectKind::pocketAutoduck, { 0.25f, 0.0f, 0.501f, 0.5f, 0.5f, 1.0f, 0.0f }, ones, ones, 0, 250);
    CHECK (o.l[0] == 1.0f);
    CHECK (flm189ha::near (o.l[11000], 0.5, 1e-4) && flm189ha::near (o.r[11000], 0.5, 1e-4));
    CHECK (flm189ha::near (o.l[23500], 1.0, 1e-4));
    CHECK (flm189ha::near (o.l[35000], 0.5, 1e-4)); // the next beat
    // the host's block size doesn't change a sample (the phase is resynced on each song tick)
    for (const int block : { 64, 100, 1000, 4096 })
        CHECK (flm189ha::run (EffectKind::pocketAutoduck, { 0.25f, 0.0f, 0.501f, 0.5f, 0.5f, 1.0f, 0.0f }, ones, ones, 0, block).l == o.l);
    // Ducking 0: unity
    const auto u = flm189ha::run (EffectKind::pocketAutoduck, { 0.0f }, ones, ones);
    CHECK (u.l == ones && u.r == ones);
}

TEST (flm189ha_pocket_chorus)
{
    using namespace lynx;
    // defaults at 48 kHz: N = 480, Depth 0.6 → d = 0.256, Mix 0.6 → 0.36, Stereo 1: the left hears tap 1, the right tap 2.
    // An impulse in the left (sum = 1): tap 2 at once mixes (1 − u2) of the current sum, u2 = sin(2π·0.1)·d + ½
    const auto o = flm189ha::run (EffectKind::pocketChorus, {}, flm189ha::impulse (2000), std::vector<float> (2000, 0.0f));
    CHECK (flm189ha::near (o.l[0], 1.0, 1e-6));
    CHECK (flm189ha::near (o.r[0], 0.36 * (0.5 - std::sin (6.283185307179586 * 0.1) * 0.256), 1e-4));
    // and N − 1 samples later u2 of it (LFO 2 at 0.94 Hz has moved on by 479 samples)
    const auto ph2 = 0.1 + 479.0 * 0.94 / 48000.0;
    CHECK (flm189ha::near (o.r[479], 0.36 * (std::sin (6.283185307179586 * ph2) * 0.256 + 0.5), 1e-4));
    // tap 1 sits N·d·u² into the ring: u ∈ [½ − d, ½ + d] → 7.4…68.7 samples → 411…473 samples of delay
    bool quiet = true, heard = false;
    for (int i = 1; i < 2000; ++i)
    {
        if (i < 405 || i > 476)
            quiet = quiet && o.l[(std::size_t) i] == 0.0f;
        else
            heard = heard || o.l[(std::size_t) i] != 0.0f;
    }
    CHECK (quiet && heard);
    // Mix 0: the dry as it is
    const auto x = flm189ha::run (EffectKind::pocketChorus, { 0.4f, 0.6f, 1.0f, 0.0f }, flm189ha::impulse (600), flm189ha::impulse (600, 5));
    CHECK (x.l == flm189ha::impulse (600) && x.r == flm189ha::impulse (600, 5));
    CHECK (pocket::chorusParamText (0, 0.4f) == "0.84 Hz");
}

TEST (flm189ha_pocket_spacer)
{
    using namespace lynx;
    const auto K = EffectKind::pocketSpacer;
    const auto in = flm189ha::impulse (20000, 10);
    const auto in2 = flm189ha::impulse (20000, 20);
    // defaults pass L and R unchanged (the ·½ encode and the A ± B decode cancel)
    auto o = flm189ha::run (K, {}, in, in2);
    CHECK (flm189ha::near (o.l[10], 1.0, 1e-6) && flm189ha::near (o.r[20], 1.0, 1e-6) && o.l[20] == 0.0f && o.r[10] == 0.0f);
    // L/R Delay 1: the left 300 ms late (14400 samples), the right on time. FLM's glide (0.1 % a sample in single
    // precision) stops about 3e-5 short of its target, so the delay ends ~2.6 samples short of 14400
    o = flm189ha::run (K, { 1.0f }, in, in2, 200000);
    float late = 0.0f;
    for (int i = 14400; i < 14411; ++i)
        late += o.l[(std::size_t) i];
    CHECK (flm189ha::near (late, 1.0, 1e-5) && o.l[14411] == 0.0f && o.l[10] == 0.0f && flm189ha::near (o.r[20], 1.0, 1e-6));
    // L/R Inversion both: −L, −R; M/S Inversion side swaps the sides; Output M/S gives (L + R)/2, (L − R)/2
    o = flm189ha::run (K, { 0.5f, 0.5f, 0.5f, 1.0f }, in, in2);
    CHECK (flm189ha::near (o.l[10], -1.0, 1e-6) && flm189ha::near (o.r[20], -1.0, 1e-6));
    o = flm189ha::run (K, { 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.6667f }, in, in2);
    CHECK (flm189ha::near (o.r[10], 1.0, 1e-6) && flm189ha::near (o.l[20], 1.0, 1e-6) && o.l[10] == 0.0f);
    o = flm189ha::run (K, { 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f }, in, in2);
    CHECK (flm189ha::near (o.l[10], 0.5, 1e-6) && flm189ha::near (o.r[10], 0.5, 1e-6) && flm189ha::near (o.r[20], -0.5, 1e-6));
    // Separation 1 (after its glide): mono; Pan 0: the right silent
    o = flm189ha::run (K, { 0.5f, 0.5f, 1.0f }, in, in2, 200000);
    CHECK (flm189ha::near (o.l[10], 0.5, 1e-4) && flm189ha::near (o.r[10], 0.5, 1e-4));
    o = flm189ha::run (K, { 0.5f, 0.0f }, in, in2, 200000);
    CHECK (flm189ha::near (o.l[10], 1.0, 1e-5) && std::abs (o.r[20]) < 1e-5f);
    // texts
    CHECK (pocket::spacerParamText (0, 0.5f) == "off" && pocket::spacerParamText (0, 1.0f) == "300 ms" && pocket::spacerParamText (0, 0.6f) == "2.4 ms");
    CHECK (pocket::spacerParamText (4, 0.6f) == "32.0 us" && pocket::spacerParamText (1, 0.5f) == "Center" && pocket::spacerParamText (2, 0.25f) == "-50 %");
    CHECK (pocket::spacerParamText (5, 1.0f) == "both" && pocket::spacerParamText (6, 1.0f) == "Input M/S");
}

TEST (flm189ha_pocket_stereoizer)
{
    using namespace lynx;
    const auto K = EffectKind::pocketStereoizer;
    const auto in = flm189ha::impulse (4000, 0);
    const std::vector<float> zero (4000, 0.0f);
    // Strength 0, L/R Delay ½, Pan ½: nothing runs
    auto o = flm189ha::run (K, { 0.0f, 0.52f, 0.75f, 0.5f, 0.5f }, in, zero);
    CHECK (o.l == in && o.r == zero);
    // Strength 1, Timbre 0 (D0 = 24 samples), Focus ½ (no filter): +sum to the left, −sum to the right 0.5 ms later
    o = flm189ha::run (K, { 1.0f, 0.0f, 0.5f, 0.5f, 0.5f }, in, zero);
    CHECK (o.l[0] == 1.0f && flm189ha::near (o.l[24], 1.0) && flm189ha::near (o.r[24], -1.0) && o.l[23] == 0.0f && o.r[25] == 0.0f);
    // L/R Delay 0: the left M − 1 = 1199 samples late
    o = flm189ha::run (K, { 0.0f, 0.52f, 0.75f, 0.0f, 0.5f }, in, zero);
    CHECK (o.l[0] == 0.0f && flm189ha::near (o.l[1199], 1.0));
    // Pan 0: the right off from the first sample, the left at unity
    o = flm189ha::run (K, { 0.0f, 0.52f, 0.75f, 0.5f, 0.0f }, in, in);
    CHECK (o.l[0] == 1.0f && o.r[0] == 0.0f);
    CHECK (pocket::stereoizerParamText (2, 0.75f) == "50%" && pocket::stereoizerParamText (0, 0.7f) == "70");
}

TEST (flm189ha_pocket_import)
{
    using namespace lynx;
    const std::pair<std::uint32_t, EffectKind> kinds[] { { 29, EffectKind::pocketAutoduck }, { 11, EffectKind::pocketChorus },
                                                         { 34, EffectKind::pocketSpacer }, { 15, EffectKind::pocketStereoizer } };
    for (const auto& [type, kind] : kinds)
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (type, { 1.0f, 0.25f, 0.75f }));
        CHECK (mapped.slots.size() == 1 && mapped.slots[0].kind == kind);
        CHECK (mapped.slots[0].params[0] == 0.25f && mapped.slots[0].params[1] == 0.75f);
        CHECK (mapped.slots[0].params[2] == makeEffectSlot (kind).params[2]); // not in the file: FLM's default
    }
}
