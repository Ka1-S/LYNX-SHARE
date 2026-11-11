// --- Round 190ar: Ember Saturator's Classic voicing at high drive (docs/PARITY.md) ---------------------------------
// The coupling ahead of the curve (styles T and P above drive 5): an uneven square wave like the reference's, versioned
// by the Classic hot law parameter so that saved projects keep round trial187b's tables.

namespace p190ar
{
// Classic voicing on a 1 kHz tone: the last 100 ms of one second (left channel)
inline std::vector<float> classicTone (int style, float drive, double dbfs, bool hotLaw, bool punish = false)
{
    namespace e = lynx::mixsuite::ember;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::ember);
    slot.params[(size_t) e::voicing] = 1.0f;
    slot.params[(size_t) e::classicStyle] = (float) style / 4.0f;
    slot.params[(size_t) e::classicDrive] = drive / 10.0f;
    slot.params[(size_t) e::classicPunish] = punish ? 1.0f : 0.0f;
    slot.params[(size_t) e::classicHot] = hotLaw ? 1.0f : 0.0f;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    fx->reset();
    const int n = 48000;
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = r[(size_t) i] = (float) (std::pow (10.0, dbfs / 20.0) * std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0));
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), slot.params, 120.0);
    return std::vector<float> (l.end() - 4800, l.end());
}
// share of the time spent above the middle of the two ceilings
inline double upperShare (const std::vector<float>& x)
{
    const auto [lo, hi] = std::minmax_element (x.begin(), x.end());
    const float mid = 0.5f * (*lo + *hi);
    return (double) std::count_if (x.begin(), x.end(), [mid] (float v) { return v > mid; }) / (double) x.size();
}
} // namespace p190ar

TEST (p190ar_ember_classic_hot_law_upgrade)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    CHECK ((int) em::count <= maxEffectParams);
    auto fresh = makeEffectSlot (EffectKind::ember);
    CHECK (fresh.params[(size_t) em::classicHot] == 1.0f); // new instances: the coupled tables
    upgradeLoadedEffectParams (fresh, (std::size_t) em::count);
    CHECK (fresh.params[(size_t) em::classicHot] == 1.0f);
    // a project saved before this round ends at the Classic law: it keeps round trial187b's tables and its Classic law
    auto old = makeEffectSlot (EffectKind::ember);
    upgradeLoadedEffectParams (old, (std::size_t) em::classicHot);
    CHECK (old.params[(size_t) em::classicHot] == 0.0f);
    CHECK (old.params[(size_t) em::classicLaw] == 1.0f);
}

TEST (p190ar_ember_classic_coupling_holds_the_uneven_square)
{
    // [EXEC trial] styles T (3) and P (4) at drive 10, 1 kHz at −3 dBFS peak: the lower ceiling (the upper side) lasts
    // 71 … 75 % of the period in the reference; round trial187b's tables go even there
    for (int style : { 3, 4 })
    {
        const auto on = p190ar::upperShare (p190ar::classicTone (style, 10.0f, -3.0, true));
        const auto off = p190ar::upperShare (p190ar::classicTone (style, 10.0f, -3.0, false));
        CHECK (on > 0.62 && on < 0.85);
        CHECK (on > off + 0.05);
    }
}

TEST (p190ar_ember_classic_hot_law_leaves_the_rest_alone)
{
    // drives up to 5, style A and style T with Punish: the same samples with the law on or off
    const auto same = [] (int style, float drive, bool punish) {
        return p190ar::classicTone (style, drive, -11.0, true, punish) == p190ar::classicTone (style, drive, -11.0, false, punish); };
    CHECK (same (3, 5.0f, false));
    CHECK (same (4, 3.0f, false));
    CHECK (same (0, 10.0f, false));
    CHECK (same (3, 10.0f, true));
    // … and the small-signal gain at drive 10 stays the measured drive law's (−70 dBFS, within 0.5 dB: the ceilings
    // are lower, so the tone's top already leans on them)
    for (int style : { 3, 4 })
    {
        const auto rms = [] (const std::vector<float>& x) { double s = 0.0; for (auto v : x) s += (double) v * v; return std::sqrt (s / (double) x.size()); };
        const auto d = 20.0 * std::log10 (rms (p190ar::classicTone (style, 10.0f, -70.0, true)) / rms (p190ar::classicTone (style, 10.0f, -70.0, false)));
        CHECK (std::abs (d) < 0.5);
    }
}

TEST (p190ar_ember_classic_coupling_is_bounded_and_rests)
{
    // silence stays silence, and a full-scale burst leaves no offset or runaway behind
    namespace e = lynx::mixsuite::ember;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::ember);
    slot.params[(size_t) e::voicing] = 1.0f;
    slot.params[(size_t) e::classicStyle] = 1.0f; // P
    slot.params[(size_t) e::classicDrive] = 1.0f;
    slot.params[(size_t) e::classicPunish] = 1.0f;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    fx->reset();
    const int n = 96000;
    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    for (int i = 0; i < 24000; ++i)
        l[(size_t) i] = r[(size_t) i] = i < 12000 ? 1.0f : (float) std::sin (2.0 * 3.141592653589793 * 60.0 * i / 48000.0); // a stuck input, then a loud bass tone
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), slot.params, 120.0);
    float peak = 0.0f, tail = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        CHECK (std::isfinite (l[(size_t) i]));
        peak = std::max (peak, std::abs (l[(size_t) i]));
        if (i >= n - 4800) tail = std::max (tail, std::abs (l[(size_t) i]));
    }
    CHECK (peak < 4.0f);
    CHECK (tail < 1.0e-3f);
}
