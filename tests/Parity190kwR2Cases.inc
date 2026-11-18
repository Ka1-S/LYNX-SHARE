// round 190kw: the revision-0 gold cases (shared with /mnt/e/lynx-work/rea190kw/r2/gold/gold.cpp)
namespace p190kwr2
{
// LuxeVerb Space engine settings (index from luxeVerbSpaceFirst = 53 → value), revision 0
struct Case { float v[15]; float duck; };
// engine, style, space, rate, distance, brightness, character, thickness, width, predelay, mix, lowMult, lowFreq, highMult, highFreq
inline const Case cases[] = {
    { { 1, 0, 0.5f, 0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.7f, 0, 0.3f, 0.5f, 0.5f, 0.5f, 0.5637f }, 0 },
    { { 1, 0, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.5f, 0.0f, 0, 1.0f, 0.5f, 0.5f, 0.5f, 0.5637f }, 0 },
    { { 1, 0.5f, 1.0f, 1.0f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 0.3f, 0.5f, 0.7f, 0.3f, 0.2f, 0.8f }, 0.5f },
    { { 1, 1, 0.25f, 0.6f, 0.6f, 0.8f, 0.5f, 0.2f, 0.3f, 0.1f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5637f }, 0 },
    { { 1, 0, 0.75f, 0.4f, 0.0f, 0.3f, 0.7f, 0.7f, 0.55f, 0, 0.8f, 0.5f, 0.5f, 0.5f, 0.5637f }, 0.25f },
};
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 777u;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        const double nz = (double) (s >> 8) / 8388608.0 - 1.0;
        double v = i == (int) (0.05 * sr) ? 0.5 : 0.0;
        if (t > 0.3 && t < 0.45) v += 0.25 * nz;
        if (t > 0.6 && t < 1.2) v += 0.3 * std::sin (2.0 * M_PI * 220.0 * t) + 0.1 * std::sin (2.0 * M_PI * 1000.0 * t);
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (t > 0.3 && t < 0.45 ? 0.7 * v : v);
    }
}
inline void fingerprint (int k, double sr, std::uint64_t& h, double& e)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::luxeVerb);
    for (int i = 0; i < 15; ++i) slot.params[(std::size_t) (53 + i)] = cases[k].v[i];
    slot.params[68] = cases[k].duck;
    slot.params[69] = 0.0f; // the Space engine's revision 0 (round 190kw; on the base commit an unused slot)
    slot.params[70] = 0.0f;
    const int n = (int) (1.6 * sr);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    signal (l, r, sr);
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
    h = 1469598103934665603ull; e = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
        for (const float v : { l[i], r[i] })
        {
            std::uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int b = 0; b < 4; ++b) { h ^= (u >> (8 * b)) & 0xffu; h *= 1099511628211ull; }
            e += (double) v * v;
        }
}
} // namespace p190kwr2
