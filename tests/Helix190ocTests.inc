// Round 190oc: Helix round 4 (core/WavetableSynth.h): unison on the granular and spectral oscillators (voices, detune,
// width), reverse grains, the fourth state block (revision 4), two factory presets. Uses the helpers of Helix190jaTests.inc,
// Helix190miTests.inc and Helix190mtTests.inc. No data file.

namespace helix190oc
{
namespace w = lynx::wt;
using helix190ja::db;
using helix190ja::rms;
using helix190mi::finite;
using helix190mi::hash;
using helix190mt::note;
using helix190mt::partialDb;
using helix190mt::tones;
using helix190mt::x3;

inline void x4 (std::vector<double>& s, int i, double v) { s[(std::size_t) (w::ext4Base + i)] = v; }
// a revision 4 patch (190mt's: no filter, full sustain, oscillator 1 off), round 4's block at its defaults
inline std::vector<double> patch()
{
    auto s = helix190mt::patch();
    s[(std::size_t) w::extBase] = 4.0;
    return s;
}
// the mean |L − R| against the mean |L| + |R|: how far apart the two sides are
inline double sideShare (const std::vector<float>& v)
{
    double d = 0.0, a = 0.0;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2)
        d += std::fabs ((double) v[i] - v[i + 1]), a += std::fabs ((double) v[i]) + std::fabs ((double) v[i + 1]);
    return a > 0.0 ? d / a : 0.0;
}
} // namespace helix190oc

TEST (helix190oc_layout_and_parameters)
{
    using namespace lynx;
    namespace w = wt;
    // the fourth block follows round 3's; seven automatable values, no matrix rows
    CHECK (w::ext4Base == w::ext3Base + w::ext3::count && w::stateSize == w::ext4Base + w::ext4::count && w::ext4::count == 8);
    CHECK (w::currentRevision == 4 && w::revision190oc == 4 && w::allTargetCount == 80);
    CHECK (! w::automatable (w::ext4Base) && w::automatable (w::ext4Base + 1) && w::automatable (w::ext4Base + 7) && ! w::automatable (w::ext4Base + 8));
    for (const auto* name : { "Grain Unison", "Grain Unison Detune", "Grain Unison Width", "Grain Reverse", "Spectral Unison", "Spectral Unison Detune",
                              "Spectral Unison Width" })
    {
        const auto* p = modelparams::findByName (InstrumentKind::helix, name);
        CHECK (p != nullptr && p->index > w::ext4Base && w::automatable (p->index) && w::targetOfParam (p->index) == -1);
    }
    // the block's defaults change nothing (one voice, no reverse); a new channel is revision 4 with them; a round 4 value set
    // on a revision 3 channel takes it to 4 and keeps the rest
    CHECK (w::defaults()[(std::size_t) (w::ext4Base + w::ext4::grainUnison)] == 1.0 && w::defaults()[(std::size_t) (w::ext4Base + w::ext4::specUnison)] == 1.0
           && w::defaults()[(std::size_t) (w::ext4Base + w::ext4::grainReverse)] == 0.0);
    CHECK (helixDefaults()[(std::size_t) w::extBase] == 4.0f && (int) helixDefaults().size() == w::stateSize);
    ChannelDef old;
    old.instrument = InstrumentKind::helix;
    old.modelParams.assign ((std::size_t) w::ext4Base, 0.0f);
    old.modelParams[(std::size_t) w::extBase] = 3.0f;
    auto a = old;
    CHECK (modelparams::setRawValue (a, w::ext4Base + w::ext4::grainReverse, 0.5f) && a.modelParams[(std::size_t) w::extBase] == 4.0f
           && (int) a.modelParams.size() == w::stateSize && a.modelParams[(std::size_t) (w::ext4Base + w::ext4::grainUnison)] == 1.0f);
    // texts
    CHECK (w::paramText (w::ext4Base + w::ext4::grainUnison, 5.0) == "5" && w::paramText (w::ext4Base + w::ext4::specDetune, 0.5) == "25 ct");
    CHECK (w::paramText (w::ext4Base + w::ext4::grainReverse, 0.35) == "35 %" && w::paramName (w::ext4Base + w::ext4::specWidth) == "Spectral Unison Width");
    // the laws: the voices' places, the spectral bank shared by the voices (multiples of 4, at most 64 a voice)
    CHECK (w::oscUnisonSpread (0, 1) == 0.0 && w::oscUnisonSpread (0, 5) == -1.0 && w::oscUnisonSpread (2, 5) == 0.0 && w::oscUnisonSpread (4, 5) == 1.0);
    CHECK (w::spectralUnisonStride (1) == 64 && w::spectralUnisonStride (2) == 64 && w::spectralUnisonStride (3) == 40 && w::spectralUnisonStride (4) == 32
           && w::spectralUnisonStride (8) == 16);
    for (int n = 1; n <= w::maxOscUnison; ++n)
        CHECK (w::spectralUnisonStride (n) % 4 == 0 && n * w::spectralUnisonStride (n) <= w::maxSpectralBank);
}

TEST (helix190oc_old_projects_bit_identical)
{
    using namespace helix190oc;
    w::HelixUserData data;
    data.sample = helix190mt::saw (220.0, 0.6);
    data.spectral = w::analyseSpectrum (data.sample);
    // revisions 0 … 3 ignore the fourth block whatever it holds
    for (int variant = 0; variant < 4; ++variant)
    {
        const auto live = helix190mi::livePatch (variant);
        const auto before = hash (helix190mi::note (live));
        for (double rev : { 0.0, 1.0, 2.0, 3.0 })
        {
            auto s = helix190mi::plus (live, rev);
            x4 (s, w::ext4::grainUnison, 6.0), x4 (s, w::ext4::specUnison, 3.0), x4 (s, w::ext4::grainReverse, 1.0);
            CHECK (hash (helix190mi::note (s)) == before);
        }
    }
    // a revision 3 channel with grains and partials playing: revision 4 at the block's defaults is the same bit for bit;
    // revision 3 ignores the block's values
    auto s = helix190mt::patch();
    x3 (s, w::ext3::grainOn, 1.0), x3 (s, w::ext3::specOn, 1.0), x3 (s, w::ext3::grainSpray, 0.3), x3 (s, w::ext3::grainSpread, 0.6);
    x3 (s, w::ext3::grainDensity, 30.0), x3 (s, w::ext3::grainSize, 0.15), x3 (s, w::ext3::specPartials, 24.0);
    s[(std::size_t) w::cellIndex (w::tgt::specPos, w::src::lfo1)] = 0.5;
    const auto rev3 = hash (note (s, 60, 0.5, &data));
    auto t = s;
    t[(std::size_t) w::extBase] = 4.0;
    CHECK (hash (note (t, 60, 0.5, &data)) == rev3);
    auto u = s;
    x4 (u, w::ext4::grainUnison, 4.0), x4 (u, w::ext4::specUnison, 4.0), x4 (u, w::ext4::grainReverse, 0.5);
    CHECK (hash (note (u, 60, 0.5, &data)) == rev3);
    u[(std::size_t) w::extBase] = 4.0;
    CHECK (hash (note (u, 60, 0.5, &data)) != rev3);
}

TEST (helix190oc_grain_unison)
{
    using namespace helix190oc;
    auto s = patch();
    x3 (s, w::ext3::grainOn, 1.0), x3 (s, w::ext3::grainSpray, 0.0), x3 (s, w::ext3::grainSpread, 0.0);
    x3 (s, w::ext3::grainPosition, 0.3), x3 (s, w::ext3::grainSize, 0.3), x3 (s, w::ext3::grainDensity, 4.0); // (long grains: little window sideband)
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 69.0;
    w::HelixUserData d;
    d.sample = tones ({ { 440.0, 0.8 } }, 1.0);
    const auto one = note (s, 69, 0.6, &d);
    // two voices, Detune 1: both ±50 cents, none in tune
    x4 (s, w::ext4::grainUnison, 2.0), x4 (s, w::ext4::grainDetune, 1.0), x4 (s, w::ext4::grainWidth, 0.0);
    const auto two = note (s, 69, 0.6, &d);
    const auto up = 440.0 * std::pow (2.0, 50.0 / 1200.0), down = 440.0 * std::pow (2.0, -50.0 / 1200.0);
    CHECK (finite (two) && partialDb (two, up) > partialDb (two, 440.0) + 30.0 && partialDb (two, down) > partialDb (two, 440.0) + 30.0);
    // three voices, Detune 1: the outer voices ±50 cents, the middle one in tune; Width 0 keeps them in the middle
    x4 (s, w::ext4::grainUnison, 3.0), x4 (s, w::ext4::grainDetune, 1.0), x4 (s, w::ext4::grainWidth, 0.0);
    const auto three = note (s, 69, 0.6, &d);
    CHECK (finite (three) && partialDb (three, up) > partialDb (one, up) + 25.0 && partialDb (three, down) > partialDb (one, down) + 25.0);
    CHECK (sideShare (three) < 1e-6);
    // their sum kept near one voice's level (uncorrelated voices: 1 / √n)
    const auto level = db (rms (three, 4800, 24000)) - db (rms (one, 4800, 24000));
    CHECK (level > -4.0 && level < 4.0);
    // Width spreads the voices across the sides
    x4 (s, w::ext4::grainWidth, 1.0);
    const auto wide = note (s, 69, 0.6, &d);
    CHECK (sideShare (wide) > 0.2);
    // Detune 0 with one side: in tune
    x4 (s, w::ext4::grainDetune, 0.0);
    const auto tuned = note (s, 69, 0.6, &d);
    CHECK (partialDb (tuned, 440.0) - partialDb (tuned, up) > 25.0);
    // deterministic, and eight voices of dense grains stay within the voice's grains
    x4 (s, w::ext4::grainUnison, 8.0), x4 (s, w::ext4::grainDetune, 0.4);
    x3 (s, w::ext3::grainDensity, 120.0), x3 (s, w::ext3::grainSize, 0.4);
    const auto dense = note (s, 69, 0.5, &d);
    CHECK (finite (dense) && hash (dense) == hash (note (s, 69, 0.5, &d)) && rms (dense, 4800, 24000) > 0.01);
}

TEST (helix190oc_reverse_grains)
{
    using namespace helix190oc;
    // a sample that rises in level: a grain read forwards grows over its window, read backwards it fades
    w::HelixUserData d;
    d.sample = tones ({ { 440.0, 0.8 } }, 1.0);
    for (std::size_t i = 0; i < d.sample.l.size(); ++i)
        d.sample.l[i] *= (float) ((double) i / (double) d.sample.l.size());
    auto s = patch();
    x3 (s, w::ext3::grainOn, 1.0), x3 (s, w::ext3::grainSpray, 0.0), x3 (s, w::ext3::grainSpread, 0.0);
    x3 (s, w::ext3::grainPosition, 0.3), x3 (s, w::ext3::grainSize, 0.4), x3 (s, w::ext3::grainDensity, 1.0);
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 69.0;
    const auto halves = [] (const std::vector<float>& out) { return db (rms (out, 9600 - 4000, 9600)) - db (rms (out, 9600, 9600 + 4000)); };
    const auto fwd = note (s, 69, 0.4, &d);
    x4 (s, w::ext4::grainReverse, 1.0);
    const auto back = note (s, 69, 0.4, &d);
    CHECK (finite (back) && halves (fwd) < -0.5 && halves (back) > 0.5);
    // the same tone (backwards reads change no pitch) and about the same level
    CHECK (partialDb (back, 440.0, 2000, 2000 + 16384) > -40.0 && std::abs (db (rms (back, 0, 19200)) - db (rms (fwd, 0, 19200))) < 3.0);
    // a share at random: some of each, the same every time
    x4 (s, w::ext4::grainReverse, 0.5);
    x3 (s, w::ext3::grainDensity, 20.0), x3 (s, w::ext3::grainSize, 0.05), x3 (s, w::ext3::grainSpray, 0.5);
    const auto mixed = note (s, 69, 0.5, &d);
    CHECK (finite (mixed) && hash (mixed) == hash (note (s, 69, 0.5, &d)) && rms (mixed, 0, 24000) > 0.005);
    // grains near the start of the sample read backwards stay inside it
    x3 (s, w::ext3::grainPosition, 0.0), x4 (s, w::ext4::grainReverse, 1.0), x3 (s, w::ext3::grainSize, 0.5), x3 (s, w::ext3::grainPitch, 24.0);
    CHECK (finite (note (s, 69, 0.5, &d)));
}

TEST (helix190oc_spectral_unison)
{
    using namespace helix190oc;
    w::HelixUserData d;
    d.sample = tones ({ { 220.0, 0.5 }, { 660.0, 0.25 } }, 1.0);
    d.spectral = w::analyseSpectrum (d.sample);
    auto s = patch();
    x3 (s, w::ext3::specOn, 1.0), x3 (s, w::ext3::specPosition, 0.5);
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 57.0; // (220 Hz)
    const auto one = note (s, 57, 0.6, &d);
    x4 (s, w::ext4::specUnison, 3.0), x4 (s, w::ext4::specDetune, 1.0), x4 (s, w::ext4::specWidth, 0.0);
    const auto three = note (s, 57, 0.6, &d);
    const auto up = 220.0 * std::pow (2.0, 50.0 / 1200.0), down = 220.0 * std::pow (2.0, -50.0 / 1200.0);
    CHECK (finite (three) && partialDb (three, up) > partialDb (one, up) + 10.0 && partialDb (three, down) > partialDb (one, down) + 10.0);
    CHECK (partialDb (three, 660.0 * std::pow (2.0, 50.0 / 1200.0)) > partialDb (one, 660.0 * std::pow (2.0, 50.0 / 1200.0)) + 10.0);
    const auto level = db (rms (three, 4800, 24000)) - db (rms (one, 4800, 24000));
    CHECK (level > -4.0 && level < 4.0 && sideShare (three) < 1e-6);
    x4 (s, w::ext4::specWidth, 1.0);
    CHECK (sideShare (note (s, 57, 0.6, &d)) > 0.1);
    // more voices: every voice keeps at most its share of the bank; the voices' count changing while a note plays starts the
    // bank again without a fault
    x4 (s, w::ext4::specUnison, 8.0);
    const auto eight = note (s, 57, 0.6, &d);
    CHECK (finite (eight) && hash (eight) == hash (note (s, 57, 0.6, &d)) && rms (eight, 4800, 24000) > 0.01);
    w::Engine e;
    e.prepare (48000.0);
    e.setSpectral (&d.spectral);
    e.setState (s.data());
    e.note (57, 0.9f);
    std::vector<float> l (512), r (512);
    bool ok = true;
    for (int k = 0; k < 40; ++k)
    {
        if (k == 10 || k == 25)
            e.setParam (w::ext4Base + w::ext4::specUnison, k == 10 ? 2.0 : 7.0);
        std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), 512, 120.0, -1.0);
        for (int i = 0; i < 512; ++i)
            ok = ok && std::isfinite (l[(std::size_t) i]) && std::fabs (l[(std::size_t) i]) < 4.0f;
    }
    CHECK (ok);
}

TEST (helix190oc_factory_presets)
{
    using namespace lynx;
    const auto& bank = banks::bankOf (InstrumentKind::helix);
    int found = 0;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        const auto& name = bank[(std::size_t) i].name;
        if (name != "Grain Swarm" && name != "Spectral Ensemble")
            continue;
        ++found;
        ChannelDef c;
        c.instrument = InstrumentKind::helix;
        const auto loaded = banks::withPreset (c, i);
        CHECK (loaded.has_value());
        if (! loaded)
            continue;
        wt::HelixUserData d;
        CHECK (wt::decodeUserData (loaded->pluginState, d) && d.sample.frames() > 0);
        const auto state = helixState (*loaded);
        CHECK (state[(std::size_t) wt::extBase] == 4.0
               && (state[(std::size_t) (wt::ext4Base + wt::ext4::grainUnison)] > 1.5 || state[(std::size_t) (wt::ext4Base + wt::ext4::specUnison)] > 1.5));
        std::vector<wt::RefNote> notes { { 0, 48, 0.9f, 48000 }, { 0, 55, 0.9f, 48000 }, { 0, 60, 0.9f, 48000 } };
        const auto out = wt::renderReference (state, notes, 48000.0, 48000 * 2, 256, 120.0, &d);
        auto peak = 0.0f;
        for (auto f : out)
            peak = std::max (peak, std::fabs (f));
        CHECK (helix190mi::finite (out) && peak > 0.02f && peak < 1.5f);
    }
    CHECK (found == 2 && bank[(std::size_t) bank.size() - 3].name == "Frozen Choir"); // (appended: the others keep their places)
}
