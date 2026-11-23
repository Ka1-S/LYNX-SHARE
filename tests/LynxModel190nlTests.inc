// Round 190nl: LYNX Model quality round 2. Mode families across keys (the same physical mode in neighbouring anchors
// is blended as one mode, however far its ratio drifts), the strike noise's two-section band-pass, and the refitted
// compiled-in tables (sizes, provenance, the noise order they use).

namespace lynxmodel190nl
{
namespace lm = lynx::lm;

// a hand-made LPM1 table: anchors of (key, modes (ratio, ampDb, family)), velocity 1, one noise setting
struct M
{
    float ratio, ampDb;
    std::uint8_t family;
};
inline std::vector<std::uint8_t> table (const std::vector<std::pair<float, std::vector<M>>>& anchors, std::uint8_t noiseOrder = 0,
                                        float gainDb = -20.0f, std::uint16_t noiseDbHalf = 0xd000)
{
    std::size_t modes = 0;
    for (const auto& a : anchors)
        modes += a.second.size();
    std::vector<std::uint8_t> d (40 + 32 * anchors.size() + 10 * modes, 0);
    const auto put16 = [&d] (std::size_t o, std::uint16_t v) { d[o] = (std::uint8_t) v; d[o + 1] = (std::uint8_t) (v >> 8); };
    const auto putf = [&d] (std::size_t o, float v) { std::memcpy (d.data() + o, &v, 4); };
    std::memcpy (d.data(), "LPM1", 4);
    put16 (4, 1);
    put16 (6, 0);
    put16 (8, (std::uint16_t) anchors.size());
    put16 (10, (std::uint16_t) modes);
    d[13] = noiseOrder;
    putf (16, 0.25f);
    putf (20, 0.5f);
    putf (24, 0.95f);
    putf (28, 0.0f);
    std::size_t first = 0, mo = 40 + 32 * anchors.size();
    for (std::size_t i = 0; i < anchors.size(); ++i)
    {
        const auto o = 40 + 32 * i;
        putf (o, anchors[i].first);
        putf (o + 4, 1.0f);
        putf (o + 8, gainDb);
        putf (o + 12, 0.0002f);
        put16 (o + 16, noiseDbHalf);
        put16 (o + 18, 0x6800); // 2048 Hz
        put16 (o + 20, 0x3c00); // Q 1
        put16 (o + 22, 0x5640); // 100 ms
        put16 (o + 24, (std::uint16_t) first);
        put16 (o + 26, (std::uint16_t) anchors[i].second.size());
        for (const auto& m : anchors[i].second)
        {
            putf (mo, m.ratio);
            const auto h = (std::uint16_t) (m.ampDb == 0.0f ? 0x0000 : m.ampDb == -12.0f ? 0xca00 : 0xd000); // 0 / -12 / -32 dB
            put16 (mo + 4, h);
            put16 (mo + 6, 0x3c00); // 1 s
            d[mo + 8] = 0;
            d[mo + 9] = m.family;
            mo += 10;
        }
        first += anchors[i].second.size();
    }
    return d;
}

// the level (dB) of the strongest mode within +-1 % of hz in a note, 0 dB = the main mode
inline double levelNear (const lm::NoteModes& n, double hz)
{
    double best = 0.0;
    for (int k = 0; k < n.count; ++k)
        if (std::abs (n.hz[(std::size_t) k] / hz - 1.0) < 0.01)
            best = std::max (best, (double) n.amp[(std::size_t) k]);
    return 20.0 * std::log10 (std::max (best, 1e-12) / std::max ((double) n.gain, 1e-12));
}
} // namespace lynxmodel190nl

TEST (lynxmodel190nl_families_pair_across_drift)
{
    using namespace lynxmodel190nl;
    // a mode drifting from ratio 3.0 to 3.3 (10 %, past the 4 % rule) between two keys an octave apart
    for (const std::uint8_t fam : { (std::uint8_t) 0, (std::uint8_t) 7 })
    {
        const auto d = table ({ { 60.0f, { { 1.0f, 0.0f, 1 }, { 3.0f, -12.0f, fam } } }, { 72.0f, { { 1.0f, 0.0f, 1 }, { 3.3f, -12.0f, fam } } } });
        const auto t = lm::parseTable (d.data(), d.size());
        CHECK (t.has_value());
        if (! t)
            continue;
        CHECK (t->modes[1].family == fam && t->modes[0].family == 1);
        const auto n = lm::interpolate (*t, 66.0f, 1.0f);
        const auto base = 440.0 * std::pow (2.0, (66.0 - 69.0) / 12.0);
        const auto mid = base * std::sqrt (3.0 * 3.3); // (the ratio blends geometrically)
        if (fam != 0)
        {
            // one mode, between the two, at its full level
            CHECK (n.count == 2);
            CHECK (std::abs (levelNear (n, mid) + 12.0) < 0.1);
        }
        else
        {
            // untagged: the 4 % rule leaves two modes at their own ratios, each faded to half its amplitude
            CHECK (n.count == 3);
            CHECK (std::abs (levelNear (n, base * 3.0) + 18.02) < 0.1 && std::abs (levelNear (n, base * 3.3) + 18.02) < 0.1);
        }
    }
    // families never pair across ids, even when the ratios are close
    const auto d = table ({ { 60.0f, { { 1.0f, 0.0f, 1 }, { 3.0f, -12.0f, 2 } } }, { 72.0f, { { 1.0f, 0.0f, 1 }, { 3.01f, -12.0f, 3 } } } });
    const auto t = lm::parseTable (d.data(), d.size());
    CHECK (t.has_value() && lm::interpolate (*t, 66.0f, 1.0f).count == 3);
}

TEST (lynxmodel190nl_absolute_lines_keep_their_pitch)
{
    using namespace lynxmodel190nl;
    // another tine ringing along at 1.5 x the recorded key (C4: 392.4 Hz): played at other keys it stays at 392.4 Hz,
    // a tagged mode at 1.5 x would move with the key
    const auto hz = [] (double k) { return 440.0 * std::pow (2.0, (k - 69.0) / 12.0); };
    const auto line = hz (60.0) * 1.5;
    for (const std::uint8_t fam : { lm::absoluteFamily, (std::uint8_t) 0 })
    {
        const auto d = table ({ { 60.0f, { { 1.0f, 0.0f, 1 }, { 1.5f, -12.0f, fam } } }, { 72.0f, { { 1.0f, 0.0f, 1 } } } });
        const auto t = lm::parseTable (d.data(), d.size());
        CHECK (t.has_value());
        if (! t)
            continue;
        for (const float key : { 55.0f, 63.0f })
        {
            const auto n = lm::interpolate (*t, key, 1.0f);
            const auto want = fam == lm::absoluteFamily ? line : hz (key) * 1.5;
            CHECK (levelNear (n, want) > -40.0); // (there, faded across the gap)
        }
    }
}

TEST (lynxmodel190nl_noise_two_sections)
{
    using namespace lynxmodel190nl;
    // the noise alone (the mode 120 dB under it): the second section takes its skirts from -6 to -12 dB per octave
    double hi[2] {}, mid[2] {};
    for (int s = 0; s < 2; ++s)
    {
        const auto d = table ({ { 60.0f, { { 1.0f, 0.0f, 0 } } } }, (std::uint8_t) (s == 0 ? 0 : 2), -120.0f);
        auto t = lm::parseTable (d.data(), d.size());
        CHECK (t.has_value());
        if (! t)
            return;
        CHECK (t->noiseOrder == (s == 0 ? 1 : 2));
        t->anchors[0].noiseDb = 100.0f; // (the noise 0.1 in amplitude, the mode 1e-6)
        auto st = lynxmodel190ni::state (lm::marimba);
        const auto x = lm::renderReference (st, { lm::RefNote {} }, 48000.0, 9600, 256, &*t);
        // spectrum of the first 0.2 s: energy in a band at the centre and three octaves above it
        const auto band = [&x] (double f0, double f1) {
            double e = 0.0;
            for (double f = f0; f < f1; f += 25.0)
                e += std::pow (lynxmodel190ni::partial (x, 0.0, 0.2, f, 48000.0), 2.0);
            return e;
        };
        mid[s] = band (1800.0, 2300.0);
        hi[s] = band (14400.0, 18400.0);
    }
    const auto skirt1 = 10.0 * std::log10 (hi[0] / mid[0]), skirt2 = 10.0 * std::log10 (hi[1] / mid[1]);
    CHECK (skirt1 < -12.0 && skirt1 > -30.0); // (one section: about -18 dB three octaves up, -22 with the warping)
    CHECK (skirt2 < skirt1 - 12.0);          // (two: about twice that)
}

TEST (lynxmodel190nl_tables)
{
    using namespace lynxmodel190nl;
    // what round 190nl ships per model (the evaluation picked, per instrument, the tables that are no worse than 190ni's
    // on held-out and in-sample notes; HANDOFF 190nl): strike refined through the engine with the two-section noise
    // (marimba, xylophone, glockenspiel, kalimba), mode families (xylophone, glockenspiel, kalimba: refitted in 190nl;
    // the kalimba's other tines as absolute lines); the vibraphone's and the tubular bells' tables are 190ni's
    // (round 190nm refitted the tubular bells with this pipeline: two-section noise and families since)
    const int order[lm::modelCount] = { 2, 1, 2, 2, 2, 2 };
    const bool tagged[lm::modelCount] = { false, false, true, true, true, true };
    for (int m = 0; m < lm::modelCount; ++m)
    {
        const auto& t = lm::builtInTable (m);
        CHECK (t.model == m && t.noiseOrder == order[m]);
        int withFamily = 0;
        for (const auto& x : t.modes)
            withFamily += x.family != 0 ? 1 : 0;
        CHECK (tagged[m] ? withFamily > (int) t.modes.size() / 4 : withFamily == 0);
        int absolute = 0;
        for (const auto& x : t.modes)
            absolute += x.family == lm::absoluteFamily ? 1 : 0;
        CHECK (m == lm::kalimba ? absolute > 0 : absolute == 0);
        // a note at every semitone of the range: in tune, at most maxModes modes, levels finite
        for (float key = t.anchors.front().key; key <= t.anchors.back().key; key += 1.0f)
        {
            const auto n = lm::interpolate (t, key, 0.8f);
            CHECK (n.count >= 3 && n.count <= lm::maxModes);
            CHECK (std::abs (1200.0 * std::log2 (n.mainHz / (440.0 * std::pow (2.0, (key - 69.0) / 12.0)))) < 1.0);
            CHECK (std::isfinite (n.gain) && n.gain > 0.0f);
        }
    }
}
