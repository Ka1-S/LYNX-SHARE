// --- Round 150b: parity items (docs/PARITY.md, REA_rodada150b) ----------------------------------------------------

namespace p150b
{
// a steady sine through Ember (512-sample blocks), left / right gains; returns both outputs
inline std::pair<std::vector<float>, std::vector<float>> run (const lynx::EffectParams& p, double hz, double amp, float gl = 1.0f, float gr = 1.0f)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::ember);
    fx->prepare (48000.0);
    constexpr int n = 24000;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
    {
        const auto s = (float) (amp * std::sin (2.0 * 3.14159265358979 * hz * i / 48000.0));
        l[(std::size_t) i] = gl * s;
        r[(std::size_t) i] = gr * s;
    }
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), p, 120.0);
    return { l, r };
}
// the amplitude of the hz component over the last half (a 1-second-periodic projection)
inline double amp (const std::vector<float>& y, double hz)
{
    double c = 0.0, s = 0.0;
    const std::size_t from = y.size() / 2;
    for (std::size_t i = from; i < y.size(); ++i)
    {
        const auto w = 2.0 * 3.14159265358979 * hz * (double) i / 48000.0;
        c += y[i] * std::cos (w);
        s += y[i] * std::sin (w);
    }
    return 2.0 * std::sqrt (c * c + s * s) / (double) (y.size() - from);
}
inline double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }
} // namespace p150b

TEST (ember_six_bands_pans_characters_block_round150b)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    CHECK (em::count <= maxEffectParams);
    CHECK (effectInfo (EffectKind::ember).params.size() == (std::size_t) em::count);
    const auto base = makeEffectSlot (EffectKind::ember).params;
    CHECK (em::activeBands (base) == 1);
    CHECK (base[em::bandsMore] == 0.0f && base[em::inputPan] == 0.5f && base[em::outputPan] == 0.5f);
    std::set<int> seen;
    for (int b = 0; b < em::maxBands; ++b)
    {
        CHECK (base[(std::size_t) em::characterParam (b)] == 0.0f);
        for (int f = 0; f < 4; ++f) seen.insert (em::bandParam (b, f));
        for (int f = 0; f < em::extraFields; ++f) seen.insert (em::bandExtra (b, f));
        seen.insert (em::bandDrivePanParam (b));
        seen.insert (em::bandPanParam (b));
        seen.insert (em::characterParam (b));
        // the new bands' defaults are the old bands' defaults
        for (int f = 0; f < 4; ++f) CHECK (b < 2 || base[(std::size_t) em::bandParam (b, f)] == base[(std::size_t) em::bandParam (1, f)]);
        for (int f = 0; f < em::extraFields; ++f) CHECK (base[(std::size_t) em::bandExtra (b, f)] == base[(std::size_t) em::bandExtra (0, f)]);
    }
    CHECK (seen.size() == (std::size_t) em::maxBands * (4 + em::extraFields + 3));
    for (const auto k : seen) CHECK (k >= 0 && k < em::count);
    for (int n = 1; n <= 6; ++n)
    {
        auto p = base;
        const auto [bv, mv] = em::bandCountValues (n);
        p[em::bands] = bv;
        p[em::bandsMore] = mv;
        CHECK (em::activeBands (p) == n);
    }
    CHECK (em::crossoverParam (3) == em::xover4 && em::crossoverParam (4) == em::xover5 && em::crossoverParam (1) == em::xover2);
    CHECK (formatEffectParam (EffectKind::ember, em::bandsMore, 1.0f, 120.0) == "6 bands");
    CHECK (formatEffectParam (EffectKind::ember, em::band6Pan, 0.25f, 120.0) == "0.0 / -6.0 dB");
    CHECK (formatEffectParam (EffectKind::ember, em::inputPan, 0.5f, 120.0) == "centre");
    CHECK (std::string (em::characterName (0)) == "Off");
    CHECK (formatEffectParam (EffectKind::ember, em::band5Character, 26.0f / 28.0f, 120.0) == em::characterName (26));
}

TEST (ember_six_bands_sum_flat_round150b)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    auto p = makeEffectSlot (EffectKind::ember).params;
    p[em::autoGain] = 0.0f;
    const auto [bv, mv] = em::bandCountValues (6);
    p[em::bands] = bv;
    p[em::bandsMore] = mv;
    static const double xo[5] { 120.0, 400.0, 1500.0, 4000.0, 9000.0 };
    for (int i = 0; i < 5; ++i) p[(std::size_t) em::crossoverParam (i)] = em::crossoverValue (xo[i]);
    for (int b = 0; b < 6; ++b) p[(std::size_t) em::bandExtra (b, 0)] = 0.0f; // every band off: clean, the split must sum flat
    for (const double hz : { 60.0, 250.0, 1000.0, 3000.0, 6000.0, 12000.0 })
    {
        const auto [l, r] = p150b::run (p, hz, 0.25);
        CHECK (std::abs (p150b::db (p150b::amp (l, hz) / 0.25)) < 0.05);
    }
    // band 6 muted: the top octaves go, the bottom stays
    p[(std::size_t) em::bandExtra (5, 1)] = 1.0f;
    {
        const auto [l, r] = p150b::run (p, 16000.0, 0.25);
        CHECK (p150b::db (p150b::amp (l, 16000.0) / 0.25) < -20.0);
        const auto [l2, r2] = p150b::run (p, 250.0, 0.25);
        CHECK (std::abs (p150b::db (p150b::amp (l2, 250.0) / 0.25)) < 0.1);
    }
}

TEST (ember_characters_match_the_measured_styles_round150b)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    auto p = makeEffectSlot (EffectKind::ember).params;
    // [EXEC] the trial, 1 kHz sine: the fundamental's gain (dB) at a level for (style, drive %) — D:\lynx-work\parity150\b\an1.json
    struct Case { int style; double drive, inDb, gainDb; };
    // (an1.json's amplitudes read 1.76 dB high — the energy of five Hann bins — corrected here)
    static const Case cases[] { { 0, 1.0, -12.0, 0.74 }, { 0, 1.0, 0.0, -3.36 }, { 6, 0.4, -12.0, 1.34 }, { 6, 0.4, 0.0, -6.56 },
                                { 1, 0.0, -48.0, 1.14 }, { 19, 0.4, -12.0, -7.26 }, { 21, 1.0, -24.0, 4.04 }, { 21, 1.0, -6.0, -7.96 } };
    for (const auto& c : cases)
    {
        auto q = p;
        q[(std::size_t) em::characterParam (0)] = (float) (c.style + 1) / (float) em::characters;
        q[em::drive] = (float) (30.0 * c.drive / 36.0);
        const auto [l, r] = p150b::run (q, 1000.0, std::pow (10.0, c.inDb / 20.0));
        const auto g = p150b::db (p150b::amp (l, 1000.0)) - c.inDb;
        // within 1 dB; at 0 dBFS within 1.8 dB (the fitted curve's worst points are the full-scale ones)
        CHECK (std::abs (g - c.gainDb) < (c.inDb >= 0.0 ? 1.8 : 1.0));
        // the curve alone (no EQ for these: flat styles)
        if (c.style == 21)
        {
            const auto y = em::characterShape (c.style + 1, c.drive, 0.001f);
            CHECK (std::abs (p150b::db (y / 0.001f) - 4.04) < 0.3);
        }
    }
    // a guitar-amp voicing's tone: +7.3 dB at 4 kHz against 1 kHz, small signal (the trial's −60 dBFS noise response)
    {
        auto q = p;
        q[(std::size_t) em::characterParam (0)] = 9.0f / (float) em::characters; // style 8
        const auto [l1, r1] = p150b::run (q, 1000.0, 0.004);
        const auto [l4, r4] = p150b::run (q, 4000.0, 0.004);
        CHECK (std::abs (p150b::db (p150b::amp (l4, 4000.0) / p150b::amp (l1, 1000.0)) - 7.3) < 1.5);
        const auto [l10, r10] = p150b::run (q, 10000.0, 0.004);
        CHECK (p150b::db (p150b::amp (l10, 10000.0) / p150b::amp (l1, 1000.0)) < -30.0);
    }
    // in a band of the multiband mode too
    {
        auto q = p;
        q[em::bands] = 1.0f / 3.0f; // two bands
        q[em::xover1] = em::crossoverValue (200.0);
        q[(std::size_t) em::characterParam (1)] = 7.0f / (float) em::characters; // style 6 on band 2
        q[(std::size_t) em::bandParam (1, 1)] = (float) (12.0 / 36.0);
        const auto [l, r] = p150b::run (q, 1000.0, 1.0);
        CHECK (std::abs (p150b::db (p150b::amp (l, 1000.0)) - (-6.56)) < 1.0);
    }
}

TEST (ember_input_output_pan_round150b)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    auto p = makeEffectSlot (EffectKind::ember).params;
    p[em::autoGain] = 0.0f;
    p[em::style] = 1.0f / 3.0f; // tape, drive 0: nearly linear at −40 dBFS
    p[em::law] = 0.0f;          // (round 190fj: the Classic tape curve, unity at low level; the measured one trims 0.08 dB)
    const double a = 0.01;
    // output pan fully left: the right channel silent
    {
        auto q = p;
        q[em::outputPan] = 0.0f;
        const auto [l, r] = p150b::run (q, 1000.0, a);
        CHECK (std::abs (p150b::db (p150b::amp (l, 1000.0) / a)) < 0.05);
        CHECK (p150b::amp (r, 1000.0) < 1e-6);
    }
    // [EXEC] mid / side, input pan −0.5, a left-only tone: L −2.5 dB, R −12.0 dB (side × 0.5)
    for (const int which : { 0, 1 })
    {
        auto q = p;
        q[em::channelMode] = 1.0f;
        q[which == 0 ? em::inputPan : em::outputPan] = 0.25f;
        const auto [l, r] = p150b::run (q, 1000.0, a, 1.0f, 0.0f);
        CHECK (std::abs (p150b::db (p150b::amp (l, 1000.0) / a) - (-2.50)) < 0.1);
        CHECK (std::abs (p150b::db (p150b::amp (r, 1000.0) / a) - (-12.04)) < 0.1);
    }
}

TEST (ember_saturn2_six_bands_styles_pans_translated_round150b)
{
    using namespace lynx;
    PluginIdentity id;
    id.name = "FabFilter Saturn 2";
    // [EXEC] read back from the trial's state: 1 / 3 input / output pan (−1 … +1), 6 band count, band b at 7 + 17b:
    // 3 style index, 15 crossover (log2 Hz)
    std::vector<std::pair<int, float>> v { { 1, -0.5f }, { 3, 0.5f }, { 5, 100.0f }, { 6, 6.0f } };
    for (int b = 0; b < 6; ++b)
    {
        v.push_back ({ 7 + 17 * b + 3, (float) (b == 4 ? 25 : 4 + b) });
        v.push_back ({ 7 + 17 * b + 10, 100.0f });
        v.push_back ({ 7 + 17 * b + 13, 1.0f });
        v.push_back ({ 7 + 17 * b + 15, (float) std::log2 (100.0 * std::pow (2.0, b + 1)) });
    }
    id.state = dyn108::ffbs (951, v);
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr) return;
    const auto slot = makeSubstituteSlot (*entry, &id);
    namespace e = mixsuite::ember;
    CHECK (e::activeBands (slot.params) == 6);
    CHECK (e::characterIndex (slot.params[(std::size_t) e::characterParam (4)]) == 26);
    CHECK (e::characterIndex (slot.params[(std::size_t) e::characterParam (0)]) == 5);
    CHECK (std::abs (e::crossoverHz (slot.params[(std::size_t) e::crossoverParam (4)]) - 3200.0) < 20.0);
    CHECK (std::abs (e::panOf (slot.params[e::inputPan]) + 0.5) < 1e-4);
    CHECK (std::abs (e::panOf (slot.params[e::outputPan]) - 0.5) < 1e-4);
}

TEST (lynxtune_send_receive_round150b)
{
    using namespace lynx;
    namespace lt = lynxtune;
    lt::clearPublishedScale();
    lt::SharedScale none;
    CHECK (! lt::publishedScale (none));
    Song song;
    song.mixer.resize (4);
    for (int t = 1; t < 4; ++t) song.mixer[(std::size_t) t].effects.push_back (makeEffectSlot (EffectKind::lynxTune));
    song.mixer[2].effects.push_back (makeEffectSlot (EffectKind::eq));
    CHECK (song.mixer[1].effects[0].params[lt::receiveScale] == 1.0f); // Receive on by default
    CHECK (lt::paramText (lt::receiveScale, 1.0f) == "On");
    auto& src = song.mixer[1].effects[0].params;
    src[lt::key] = 7.0f / 11.0f;
    src[lt::noteC + 1] = 0.0f;
    src[lt::bypassC + 4] = 1.0f;
    src[lt::noteDirC + 2] = 0.5f;
    src[lt::rangeLow] = lt::rangeNoteValue (40);
    song.mixer[3].effects[0].params[lt::receiveScale] = 0.0f; // locked
    const auto before3 = song.mixer[3].effects[0].params;
    CHECK (lt::sendScaleToAll (song, 1, 0) == 1);
    CHECK (lt::captureScale (song.mixer[2].effects[0].params.data()) == lt::captureScale (src.data()));
    CHECK (song.mixer[3].effects[0].params == before3);
    lt::SharedScale got;
    CHECK (lt::publishedScale (got) && got == lt::captureScale (src.data()));
    // not a LYNX Tune / out of range: nothing
    CHECK (lt::sendScaleToAll (song, 2, 1) == 0);
    CHECK (lt::sendScaleToAll (song, 9, 0) == 0);
    // receiving applies the published scale
    auto fresh = makeEffectSlot (EffectKind::lynxTune).params;
    lt::applyScale (fresh.data(), got);
    CHECK (lt::rangeNote (fresh[lt::rangeLow]) == 40 && fresh[lt::noteC + 1] == 0.0f);
    lt::clearPublishedScale();
}
