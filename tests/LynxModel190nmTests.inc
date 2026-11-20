// Round 190nm: LYNX Model quality round 3 (tubular bells and kalimba). Two components per partial (a two-stage decay, a
// beating pair of a near-degenerate doublet), the antiphase bit of an LPM mode (shape byte bit 7), and the refitted
// compiled-in tables.

namespace lynxmodel190nm
{
namespace lm = lynx::lm;

struct M
{
    float ratio;
    std::uint8_t shapeByte; // (bit 7: antiphase)
    std::uint16_t t60Half;  // f16 bits of the T60
};

// a hand-made LPM1 table, one anchor (key 60, velocity 1), every mode at 0 dB, strike noise off by its level
inline std::vector<std::uint8_t> table (const std::vector<M>& modes, std::uint8_t onset = 0, float riseMs = 0.0f)
{
    std::vector<std::uint8_t> d (40 + 32 + 10 * modes.size(), 0);
    const auto put16 = [&d] (std::size_t o, std::uint16_t v) { d[o] = (std::uint8_t) v; d[o + 1] = (std::uint8_t) (v >> 8); };
    const auto putf = [&d] (std::size_t o, float v) { std::memcpy (d.data() + o, &v, 4); };
    std::memcpy (d.data(), "LPM1", 4);
    put16 (4, 1);
    put16 (6, (std::uint16_t) lm::tubularBells);
    put16 (8, 1);
    put16 (10, (std::uint16_t) modes.size());
    d[14] = onset;
    putf (16, 0.0f);
    putf (20, 0.02f);
    putf (24, 0.35f);
    putf (40, 60.0f);
    putf (44, 1.0f);
    putf (48, -20.0f);
    putf (52, 0.0002f);
    put16 (56, 0xd800); // noise -128 dB
    put16 (58, 0x6800);
    put16 (60, 0x3c00);
    put16 (62, 0x4900);
    put16 (64, 0);
    put16 (66, (std::uint16_t) modes.size());
    putf (68, riseMs);
    std::size_t mo = 72;
    for (const auto& m : modes)
    {
        putf (mo, m.ratio);
        put16 (mo + 4, 0);
        put16 (mo + 6, m.t60Half);
        d[mo + 8] = m.shapeByte;
        mo += 10;
    }
    return d;
}

inline std::vector<float> render (const lm::Table& t, int length = 24000)
{
    auto s = lm::presetValues (lm::tubularBells);
    s[(std::size_t) lm::p::noise] = -40.0; // (off)
    return lm::renderReference (std::vector<double> (s.begin(), s.end()), { { 0, 60, 1.0f, -1, 0.0f } }, 48000.0, length, 256, &t);
}

inline double peakOf (const lm::Table& t, int from = 0, int to = 24000)
{
    const auto out = render (t);
    double pk = 0.0;
    for (int i = 2 * from; i < 2 * to && i < (int) out.size(); ++i)
        pk = std::max (pk, (double) std::abs (out[(std::size_t) i]));
    return pk;
}
} // namespace lynxmodel190nm

TEST (lynxmodel190nm_antiphase_bit)
{
    using namespace lynxmodel190nm;
    // the shape byte's bit 7 is the antiphase flag; the low bits stay the bending-mode number
    const auto d = table ({ { 1.0f, 3, 0x4000 }, { 1.0f, 0x80 | 3, 0x4000 } });
    const auto t = lm::parseTable (d.data(), d.size());
    CHECK (t.has_value());
    if (! t)
        return;
    CHECK (t->modes[0].shape == 3 && ! t->modes[0].invert);
    CHECK (t->modes[1].shape == 3 && t->modes[1].invert);
    const auto n = lm::interpolate (*t, 60.0f, 1.0f);
    CHECK (n.count == 2 && n.invert[0] != n.invert[1]);
    // two equal components of one partial, one in antiphase: they cancel; in phase they add
    const auto same = table ({ { 1.0f, 3, 0x4000 }, { 1.0f, 3, 0x4000 } });
    const auto ts = lm::parseTable (same.data(), same.size());
    CHECK (ts.has_value());
    if (! ts)
        return;
    const auto loud = peakOf (*ts), quiet = peakOf (*t);
    CHECK (loud > 1.0e-3);
    CHECK (quiet < loud * 1.0e-4);
    // a beating pair (1 Hz apart at C4, 0.38 %): in phase it starts loud and dips to near silence after half a beat
    const auto beat = table ({ { 1.0f, 0, 0x4900 }, { 1.0038f, 0, 0x4900 } });
    const auto tb = lm::parseTable (beat.data(), beat.size());
    CHECK (tb.has_value() && lm::interpolate (*tb, 60.0f, 1.0f).count == 2);
}

TEST (lynxmodel190nm_onset)
{
    using namespace lynxmodel190nm;
    // header byte 14 = 1: the anchor's riseMs (2 ms) is the whole note's onset, 1 - e^(-t / 2 ms); byte 0 keeps the
    // resonated modes' build-up twins (the same riseMs there builds up the main mode instead)
    const std::vector<M> modes { { 1.0f, 0, 0x4000 }, { 2.76f, 0, 0x3c00 }, { 5.4f, 0, 0x3800 } };
    const auto plain = table (modes), onset = table (modes, 1, 2.0f), twins = table (modes, 0, 2.0f);
    const auto tp = lm::parseTable (plain.data(), plain.size()), to = lm::parseTable (onset.data(), onset.size()),
               tt = lm::parseTable (twins.data(), twins.size());
    CHECK (tp.has_value() && to.has_value() && tt.has_value());
    if (! tp || ! to || ! tt)
        return;
    CHECK (tp->onset == 0 && to->onset == 1 && tt->onset == 0);
    const auto a = render (*tp), b = render (*to);
    // the first 0.5 ms much quieter, after 30 ms (15 time constants) the same note
    double e0a = 0.0, e0b = 0.0, lateDiff = 0.0, late = 0.0;
    for (int i = 0; i < 24; ++i)
    {
        e0a += (double) a[(std::size_t) (2 * i)] * a[(std::size_t) (2 * i)];
        e0b += (double) b[(std::size_t) (2 * i)] * b[(std::size_t) (2 * i)];
    }
    for (int i = 1440; i < 4800; ++i)
    {
        lateDiff = std::max (lateDiff, (double) std::abs (a[(std::size_t) (2 * i)] - b[(std::size_t) (2 * i)]));
        late = std::max (late, (double) std::abs (a[(std::size_t) (2 * i)]));
    }
    CHECK (e0a > 0.0 && e0b < e0a * 0.05);
    CHECK (late > 0.0 && lateDiff < late * 1.0e-5);
    // with the onset no lanes go to build-up twins: the voice keeps its 3 modes
    lm::Engine e;
    e.prepare (48000.0);
    e.setTable (&*to);
    e.note (60, 1.0f);
    CHECK (e.modesOfVoice (0) == 3);
    e.setTable (&*tt);
    e.reset();
    e.note (60, 1.0f);
    CHECK (e.modesOfVoice (0) == 4); // (the main mode and its twin)
}

TEST (lynxmodel190nm_equal_power_fade)
{
    using namespace lynxmodel190nm;
    // two recorded keys (60, 72), each with a line the other lacks (absolute lines, another tine): halfway, with header
    // byte 15 = 1 each keeps half its power (-3 dB), with 0 half its amplitude (-6 dB, as before)
    for (const std::uint8_t fade : { (std::uint8_t) 0, (std::uint8_t) 1 })
    {
        auto d = table ({ { 1.0f, 0, 0x4000 } });
        // (a second anchor at key 72: rebuild the table by hand from the one-anchor layout)
        std::vector<std::uint8_t> two (40 + 64 + 40, 0);
        std::memcpy (two.data(), d.data(), 40);
        const auto put16 = [&two] (std::size_t o, std::uint16_t v) { two[o] = (std::uint8_t) v; two[o + 1] = (std::uint8_t) (v >> 8); };
        const auto putf = [&two] (std::size_t o, float v) { std::memcpy (two.data() + o, &v, 4); };
        put16 (8, 2);
        put16 (10, 4);
        two[15] = fade;
        for (int a = 0; a < 2; ++a)
        {
            std::memcpy (two.data() + 40 + 32 * a, d.data() + 40, 32);
            putf (40 + 32 * a, a == 0 ? 60.0f : 72.0f);
            put16 (40 + 32 * a + 24, (std::uint16_t) (2 * a));
            put16 (40 + 32 * a + 26, 2);
        }
        const float ratios[4] = { 1.0f, 1.5f, 1.0f, 1.26f }; // (60: main + a line at 1.5 x; 72: main + a line at 1.26 x)
        for (int k = 0; k < 4; ++k)
        {
            const auto o = (std::size_t) (104 + 10 * k);
            putf (o, ratios[k]);
            put16 (o + 4, k % 2 == 0 ? 0 : 0xca00); // 0 / -12 dB
            put16 (o + 6, 0x4000);
            two[o + 9] = k % 2 == 0 ? 1 : lm::absoluteFamily;
        }
        const auto t = lm::parseTable (two.data(), two.size());
        CHECK (t.has_value());
        if (! t)
            continue;
        CHECK (t->fade == fade);
        const auto n = lm::interpolate (*t, 66.0f, 1.0f);
        const auto hz = [] (double k) { return 440.0 * std::pow (2.0, (k - 69.0) / 12.0); };
        const auto line = hz (60.0) * 1.5; // (absolute: it keeps its pitch)
        double best = 0.0;
        for (int k = 0; k < n.count; ++k)
            if (std::abs (n.hz[(std::size_t) k] / line - 1.0) < 0.005)
                best = std::max (best, (double) n.amp[(std::size_t) k]);
        const auto db = 20.0 * std::log10 (std::max (best, 1e-12) / std::max ((double) n.gain, 1e-12));
        CHECK (std::abs (db - (fade == 1 ? -15.01 : -18.02)) < 0.1);
    }
}


TEST (lynxmodel190nm_tables)
{
    using namespace lynxmodel190nm;
    // what round 190nm ships (HANDOFF 190nm): the tubular bells refitted with their hum (a mode near 0.31 x the strike
    // note, below 190ni's 0.4 x analysis floor) and every anchor's level matched through the engine; the kalimba with
    // window-exact levels, two-component modes, the whole note's onset and the equal-power fade. The other four keep
    // 190nl's tables (no onset, no fade, no antiphase mode).
    const auto& bells = lm::builtInTable (lm::tubularBells);
    int hum = 0;
    for (const auto& a : bells.anchors)
    {
        bool low = false;
        for (int k = 0; k < a.count; ++k)
            low = low || bells.modes[(std::size_t) (a.first + k)].ratio < 0.4f;
        hum += low ? 1 : 0;
    }
    CHECK (hum * 2 > (int) bells.anchors.size());
    const auto& kal = lm::builtInTable (lm::kalimba);
    CHECK (kal.onset != 0 && kal.fade == 1); // (round 190nn: the S-shaped onset 2)
    for (const int m : { (int) lm::marimba, (int) lm::vibraphone, (int) lm::xylophone, (int) lm::glockenspiel })
    {
        const auto& t = lm::builtInTable (m);
        CHECK (t.onset == 0 && t.fade == 0);
        CHECK (std::none_of (t.modes.begin(), t.modes.end(), [] (const lm::TableMode& x) { return x.invert; }));
    }
    // the kalimba's notes start slowly now: the first 0.3 ms of a held note well under its 5-10 ms level
    auto s = lm::presetValues (lm::kalimba);
    s[(std::size_t) lm::p::noise] = -40.0;
    const auto out = lm::renderReference (std::vector<double> (s.begin(), s.end()), { { 0, 69, 0.8f, -1, 0.0f } }, 48000.0, 960, 256);
    double early = 0.0, later = 0.0;
    for (int i = 0; i < 960; ++i)
        (i < 14 ? early : later) = std::max (i < 14 ? early : later, (double) std::abs (out[(std::size_t) (2 * i)]));
    CHECK (later > 0.0 && early < later * 0.5);
}
