// Round 190mt: Helix round 3 (core/WavetableSynth.h, core/WavetableUser.cpp): the granular oscillator (grains of the
// channel's sample), the spectral oscillator (the sample's partials resynthesised, warped and filtered), the sample
// oscillator's band limit ("Clean"), the third state block (revision 3), the new factory presets. Uses the helpers of
// Helix190jaTests.inc, Helix190miTests.inc and Helix190mlTests.inc. No data file.

namespace helix190mt
{
namespace w = lynx::wt;
using helix190ja::db;
using helix190ja::rms;
using helix190mi::finite;
using helix190mi::hash;

// a revision 3 patch: Helix's law-test base (no filter, full sustain), oscillator 1 off, the three blocks live at their
// defaults (Clean off, as defaults() has it)
inline std::vector<double> patch()
{
    auto s = helix190mi::plus (helix190ja::base(), 3.0);
    s[(std::size_t) (w::oscBase (0) + w::osc::on)] = 0.0;
    s[(std::size_t) w::volume] = 0.5;
    return s;
}
inline void x3 (std::vector<double>& s, int i, double v) { s[(std::size_t) (w::ext3Base + i)] = v; }
inline std::vector<float> note (const std::vector<double>& s, int key, double seconds, const w::HelixUserData* data, double fs = 48000.0)
{
    w::RefNote n;
    n.key = key;
    n.release = -1;
    return w::renderReference (s, { n }, fs, (int) (seconds * fs), 256, 120.0, data);
}
// sines (hz, amplitude) as a mono sample at `rate`
inline w::SampleBuffer tones (std::initializer_list<std::pair<double, double>> parts, double seconds, double rate = 44100.0)
{
    w::SampleBuffer b;
    b.rate = rate;
    b.l.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < b.l.size(); ++i)
    {
        double y = 0.0;
        for (const auto& [hz, a] : parts)
            y += a * std::sin (2.0 * 3.14159265358979 * hz * (double) i / rate);
        b.l[i] = (float) y;
    }
    return b;
}
// a band-limited saw (every harmonic up to the rate's Nyquist) as a mono sample
inline w::SampleBuffer saw (double hz, double seconds, double rate = 44100.0)
{
    w::SampleBuffer b;
    b.rate = rate;
    b.l.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < b.l.size(); ++i)
    {
        double y = 0.0;
        for (int h = 1; h * hz < 0.5 * rate; ++h)
            y += std::sin (2.0 * 3.14159265358979 * h * hz * (double) i / rate) / h;
        b.l[i] = (float) (0.5 * y);
    }
    return b;
}
inline double partialDb (const std::vector<float>& x, double hz, int from = 9600, int to = 9600 + 16384)
{
    return db (helix190ja::partial (x, from, to, hz, 48000.0));
}
// the energy away from the harmonics of f0 against the energy on them (dB) over the whole spectrum: a 16384-point FFT
// (Blackman-Harris) of the left channel from `from`; bins within 6 of a harmonic are the harmonic's, bins farther than 10
// are fold-over (190ml's grid at f0 / 8 steps misses fold-over that lands between its points)
inline double foldOverDb (const std::vector<float>& x, int from, double f0, double fs = 48000.0)
{
    const int n = 16384;
    std::vector<std::complex<double>> a ((std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto t = 2.0 * 3.14159265358979 * i / (n - 1);
        const auto win = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        a[(std::size_t) i] = (double) x[(std::size_t) (from + i) * 2] * win;
    }
    for (std::size_t i = 1, j = 0; i < (std::size_t) n; ++i)
    {
        auto bit = (std::size_t) n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= (std::size_t) n; len <<= 1)
    {
        const auto ang = -2.0 * 3.14159265358979 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < (std::size_t) n; i += len)
        {
            std::complex<double> wv (1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * wv;
                a[i + j] = u + v, a[i + j + len / 2] = u - v;
                wv *= wl;
            }
        }
    }
    double harm = 0.0, other = 0.0;
    const auto binHz = fs / n;
    for (int k = (int) (20.0 / binHz) + 1; k < n / 2; ++k)
    {
        const auto hz = k * binHz;
        const auto dist = std::abs (hz - std::max (1.0, std::round (hz / f0)) * f0) / binHz;
        if (dist <= 6.0)
            harm += std::norm (a[(std::size_t) k]);
        else if (dist > 10.0)
            other += std::norm (a[(std::size_t) k]);
    }
    return 10.0 * std::log10 (std::max (other, 1e-30) / std::max (harm, 1e-30));
}
} // namespace helix190mt

TEST (helix190mt_layout_and_parameters)
{
    using namespace lynx;
    namespace w = wt;
    // the third block follows Helix+ 2's; every (target, source) pair has its own value, none inside another's
    CHECK (w::ext3Base == w::ext2Base + w::ext2::count && w::ext4Base == w::ext3Base + w::ext3::count); // (190oc: round 4's block follows)
    CHECK (w::rev2TargetCount == 68 && w::allTargetCount == 80 && w::tgt::grainPos == 68 && w::tgt::specGain == 79);
    std::set<int> cells;
    for (int t = 0; t < w::allTargetCount; ++t)
        for (int s = 0; s < w::allSourceCount; ++s)
        {
            const auto i = w::cellIndex (t, s);
            CHECK (i >= w::matrixBase && i < w::stateSize && ! w::automatable (i));
            cells.insert (i);
        }
    CHECK ((int) cells.size() == w::allTargetCount * w::allSourceCount);
    CHECK (w::cellIndex (w::tgt::mbDepth, 22) == w::ext2Base + w::ext2::count - 1 && w::cellIndex (w::tgt::grainPos, 0) == w::ext3Base + w::ext3::cells);
    // the parameter table stays sorted and holds round 3's values by name
    const auto& t = helixEngineParams();
    CHECK (std::is_sorted (t.begin(), t.end(), [] (const EngineParam& a, const EngineParam& b) { return a.index < b.index; }));
    for (const auto* name : { "Grain On", "Grain Position", "Grain Size", "Grain Density", "Grain Spray", "Grain Pitch", "Spectral On", "Spectral Stretch",
                              "Spectral Shift", "Spectral Tilt", "Spectral Band", "Spectral Width", "Spectral Partials", "Sample Clean" })
    {
        const auto* p = modelparams::findByName (InstrumentKind::helix, name);
        CHECK (p != nullptr && p->index > w::ext3Base && w::automatable (p->index));
    }
    CHECK (! w::automatable (w::ext3Base) && findEngineParam (t, w::ext2Base + w::ext2::mbGain)->name == "Multiband Gain"); // (190ml's names stay)
    // revision 3 for a new channel, Clean on; the block's own defaults keep Clean off (a revision 2 channel taken to 3 keeps
    // its sound); a round 3 value set on an older channel takes it to 3
    CHECK (helixDefaults()[(std::size_t) w::extBase] == (float) w::currentRevision && (int) helixDefaults().size() == w::stateSize); // (190oc: 4)
    CHECK (helixDefaults()[(std::size_t) (w::ext3Base + w::ext3::sampleClean)] == 1.0f && w::defaults()[(std::size_t) (w::ext3Base + w::ext3::sampleClean)] == 0.0);
    ChannelDef old;
    old.instrument = InstrumentKind::helix;
    old.modelParams.assign ((std::size_t) w::ext3Base, 0.0f);
    old.modelParams[(std::size_t) w::extBase] = 2.0f;
    auto a = old;
    CHECK (modelparams::setRawValue (a, w::ext3Base + w::ext3::grainSpray, 0.4f) && a.modelParams[(std::size_t) w::extBase] == (float) w::currentRevision
           && (int) a.modelParams.size() == w::stateSize && a.modelParams[(std::size_t) (w::ext3Base + w::ext3::sampleClean)] == 0.0f);
    // texts and names
    CHECK (w::paramText (w::ext3Base + w::ext3::grainSize, 0.08) == "80.0 ms" && w::paramText (w::ext3Base + w::ext3::grainDensity, 20.0) == "20 /s");
    CHECK (w::paramText (w::ext3Base + w::ext3::specWidth, 10.0) == "Open" && w::paramText (w::ext3Base + w::ext3::specTilt, -0.5) == "-3.0 dB/oct");
    CHECK (w::paramText (w::ext3Base + w::ext3::specShift, 0.5) == "+250 Hz" && w::paramText (w::ext3Base + w::ext3::sampleClean, 1.0) == "On");
    CHECK (w::targetName (w::tgt::grainPos) == "Grain Pos" && w::targetName (w::tgt::specBand) == "Spectral Band");
    CHECK (w::targetOfParam (w::ext3Base + w::ext3::grainPitch) == w::tgt::grainPitch && w::targetOfParam (w::ext3Base + w::ext3::specWidth) == -1);
    for (const auto& p : t)
        if (const auto row = w::targetOfParam (p.index); row >= 0)
            CHECK (row < w::allTargetCount && ! w::targetName (row).empty());
    // the laws
    CHECK (w::sampleLevelFor (1.0) == 0 && w::sampleLevelFor (1.2) == 0 && w::sampleLevelFor (1.3) == 1 && w::sampleLevelFor (4.0) == 4);
    CHECK (std::abs (w::spectralShiftHz (0.5) - 250.0) < 1e-9 && w::spectralExponent (0.0) == 1.0 && w::spectralExponent (1.0) == 1.5);
    CHECK (w::grainWindow (0.0f) == 0.0f && std::abs (w::grainWindow (0.5f) - 1.0f) < 1e-6f && std::abs (w::grainWindow (0.25f) - 0.5f) < 1e-3f);
}

TEST (helix190mt_old_projects_bit_identical)
{
    using namespace helix190mt;
    w::HelixUserData data;
    data.sample = saw (220.0, 0.6);
    for (int variant = 0; variant < 6; ++variant)
    {
        const auto live = helix190mi::livePatch (variant);
        const auto before = hash (helix190mi::note (live));
        // revisions 0, 1 and 2 ignore the third block whatever it holds; revision 3 with it at its defaults sounds as before
        for (double rev : { 0.0, 1.0, 2.0, 3.0 })
        {
            auto s = helix190mi::plus (live, rev);
            CHECK (hash (helix190mi::note (s)) == before);
            if (rev > 2.5)
                continue;
            for (int i = 1; i < w::ext3::autoEnd; ++i)
                s[(std::size_t) (w::ext3Base + i)] = (i % 3 == 0) ? 1.0 : 0.6 + 0.01 * i;
            x3 (s, w::ext3::grainOn, 1.0), x3 (s, w::ext3::specOn, 1.0), x3 (s, w::ext3::sampleClean, 1.0);
            s[(std::size_t) w::cellIndex (w::tgt::grainPos, w::src::lfo1)] = 1.0;
            s[(std::size_t) w::cellIndex (w::tgt::specGain, w::src::macro1)] = -1.0;
            CHECK (hash (helix190mi::note (s)) == before);
        }
    }
    // a revision 2 channel playing a sample pitched up: revision 3 with the block's defaults (Clean off) keeps it bit for
    // bit; Clean on changes it
    auto s = helix190mi::plus (helix190ja::base(), 2.0);
    s[(std::size_t) (w::ext2Base + w::ext2::sampleOn)] = 1.0;
    s[(std::size_t) (w::ext2Base + w::ext2::sampleLoop)] = 1.0;
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 57.0;
    s[(std::size_t) (w::ext2Base + w::ext2::sampleWarpMode)] = w::warp::sync;
    s[(std::size_t) (w::ext2Base + w::ext2::sampleWarpAmount)] = 0.2;
    const auto rev2 = hash (note (s, 81, 0.3, &data));
    s[(std::size_t) w::extBase] = 3.0;
    CHECK (hash (note (s, 81, 0.3, &data)) == rev2);
    x3 (s, w::ext3::sampleClean, 1.0);
    CHECK (hash (note (s, 81, 0.3, &data)) != rev2);
    s[(std::size_t) w::extBase] = 2.0; // (revision 2 ignores Clean)
    CHECK (hash (note (s, 81, 0.3, &data)) == rev2);
}

TEST (helix190mt_granular)
{
    using namespace helix190mt;
    auto s = patch();
    x3 (s, w::ext3::grainOn, 1.0);
    x3 (s, w::ext3::grainSpray, 0.0);
    x3 (s, w::ext3::grainSpread, 0.0);
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 69.0;
    // silence with no sample
    {
        const auto out = note (s, 69, 0.2, nullptr);
        CHECK (rms (out, 0, (int) out.size() / 2) == 0.0);
    }
    // the grains play the sample at the note's speed over the root key, + Pitch
    w::HelixUserData d;
    d.sample = tones ({ { 440.0, 0.8 } }, 1.0);
    x3 (s, w::ext3::grainPosition, 0.3);
    x3 (s, w::ext3::grainSize, 0.1);
    x3 (s, w::ext3::grainDensity, 8.0); // (one grain at a time: overlapping copies of one tone would comb)
    const auto at69 = note (s, 69, 0.6, &d);
    CHECK (finite (at69) && partialDb (at69, 440.0) > -30.0 && partialDb (at69, 440.0) - partialDb (at69, 880.0) > 25.0);
    const auto at81 = note (s, 81, 0.6, &d);
    CHECK (partialDb (at81, 880.0) - partialDb (at81, 440.0) > 25.0);
    x3 (s, w::ext3::grainPitch, 7.0);
    const auto fifth = note (s, 69, 0.6, &d);
    CHECK (partialDb (fifth, 440.0 * std::pow (2.0, 7.0 / 12.0)) - partialDb (fifth, 440.0) > 25.0);
    x3 (s, w::ext3::grainPitch, 0.0);
    // Position picks the part of the sample: 300 Hz in the first half, 600 Hz in the second
    w::HelixUserData halves;
    halves.sample = tones ({ { 300.0, 0.8 } }, 1.0);
    const auto second = tones ({ { 600.0, 0.8 } }, 1.0);
    for (std::size_t i = halves.sample.l.size() / 2; i < halves.sample.l.size(); ++i)
        halves.sample.l[i] = second.l[i];
    x3 (s, w::ext3::grainPosition, 0.2);
    const auto early = note (s, 69, 0.6, &halves);
    x3 (s, w::ext3::grainPosition, 0.75);
    const auto late = note (s, 69, 0.6, &halves);
    CHECK (partialDb (early, 300.0) - partialDb (early, 600.0) > 25.0 && partialDb (late, 600.0) - partialDb (late, 300.0) > 25.0);
    // Density: short grains far apart are heard as that many bursts a second
    for (double density : { 5.0, 12.0 })
    {
        auto t = s;
        x3 (t, w::ext3::grainSize, 0.01);
        x3 (t, w::ext3::grainDensity, density);
        const auto out = note (t, 69, 1.0, &d);
        int bursts = 0;
        bool loud = false;
        for (int i = 0; i + 48 <= 48000; i += 48) // (1 ms windows)
        {
            const auto now = rms (out, i, i + 48) > 0.01;
            bursts += now && ! loud ? 1 : 0;
            loud = now;
        }
        CHECK (std::abs (bursts - density) <= 1.0);
    }
    // determinism (the voice's own random): the same note renders the same; Spray moves the grains, still the same
    x3 (s, w::ext3::grainSpray, 0.6);
    x3 (s, w::ext3::grainSpread, 0.8);
    CHECK (hash (note (s, 64, 0.4, &d)) == hash (note (s, 64, 0.4, &d)));
    const auto wide = note (s, 64, 0.4, &d);
    CHECK (finite (wide) && helix190mi::sideDifference (wide) > 1.0);
    // pitched up, the grains read the band-limited levels: a 15 kHz tone an octave up (30 kHz) is dropped, nothing folds
    // back to 18 kHz; at its own speed it plays
    w::HelixUserData high;
    high.sample = tones ({ { 15000.0, 0.8 } }, 1.0);
    auto r = s;
    x3 (r, w::ext3::grainSpray, 0.0);
    const auto own = note (r, 69, 0.6, &high);
    const auto octave = note (r, 81, 0.6, &high);
    CHECK (partialDb (own, 15000.0) > -30.0 && partialDb (octave, 18000.0) < -90.0 && finite (octave));
}

TEST (helix190mt_spectral)
{
    using namespace helix190mt;
    // the analysis finds a known tone's partials (frequency within 1 Hz, level within 0.5 dB), loudest first
    w::HelixUserData d;
    d.sample = tones ({ { 220.0, 0.5 }, { 660.0, 0.25 }, { 1100.0, 0.1 } }, 1.0);
    d.spectral = w::analyseSpectrum (d.sample);
    const auto& sp = d.spectral;
    CHECK (! sp.empty() && sp.frames == w::spectralFrames && sp.partials >= 3 && sp.partials <= w::maxSpectralPartials);
    if (sp.empty())
        return;
    const auto mid = (std::size_t) (32 * sp.partials);
    CHECK (std::abs (sp.hz[mid] - 220.0) < 1.0 && std::abs (sp.hz[mid + 1] - 660.0) < 1.0 && std::abs (sp.hz[mid + 2] - 1100.0) < 1.0);
    CHECK (std::abs (db (sp.amp[mid]) - db (0.5)) < 0.5 && std::abs (db (sp.amp[mid + 1]) - db (0.25)) < 0.5 && std::abs (db (sp.amp[mid + 2]) - db (0.1)) < 0.5);
    for (int i = 3; i < sp.partials; ++i)
        CHECK (sp.amp[mid + (std::size_t) i] < 0.001f);
    // kept exactly in the channel's data
    w::HelixUserData back;
    CHECK (w::decodeUserData (w::encodeUserData (d), back) && back.spectral.hz == sp.hz && back.spectral.amp == sp.amp && back.spectral.frames == sp.frames);
    // resynthesis at the root key: the partials at their levels
    auto s = patch();
    x3 (s, w::ext3::specOn, 1.0);
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 57.0; // (A3, 220 Hz)
    x3 (s, w::ext3::specPosition, 0.5);
    const auto plain = note (s, 57, 0.6, &d);
    CHECK (finite (plain) && std::abs ((partialDb (plain, 220.0) - partialDb (plain, 660.0)) - 6.02) < 0.5
           && std::abs ((partialDb (plain, 220.0) - partialDb (plain, 1100.0)) - 13.98) < 0.5);
    // an octave up: every partial doubled
    const auto octave = note (s, 69, 0.6, &d);
    CHECK (partialDb (octave, 440.0) > partialDb (plain, 220.0) - 0.5 && partialDb (octave, 220.0) < -80.0);
    // Stretch +1: f' = 220 × (f / 220)^1.5 (660 → 1143 Hz); Shift +0.5: +250 Hz
    {
        auto t = s;
        x3 (t, w::ext3::specStretch, 1.0);
        const auto out = note (t, 57, 0.6, &d);
        CHECK (partialDb (out, 220.0 * std::pow (3.0, 1.5)) > partialDb (out, 660.0) + 40.0 && partialDb (out, 220.0) > -30.0);
        t = s;
        x3 (t, w::ext3::specShift, 0.5);
        const auto shifted = note (t, 57, 0.6, &d);
        CHECK (partialDb (shifted, 470.0) > partialDb (shifted, 220.0) + 40.0 && partialDb (shifted, 910.0) > partialDb (shifted, 660.0) + 40.0);
    }
    // the filter: Tilt +1 lifts 1100 Hz against 220 Hz by 6 dB an octave (log2 5 octaves); a band an octave wide at 660 Hz
    // keeps 660 Hz and drops 220 Hz
    {
        auto t = s;
        x3 (t, w::ext3::specTilt, 1.0);
        const auto out = note (t, 57, 0.6, &d);
        const auto lift = (partialDb (out, 1100.0) - partialDb (out, 220.0)) - (partialDb (plain, 1100.0) - partialDb (plain, 220.0));
        CHECK (std::abs (lift - 6.0206 * std::log2 (5.0)) < 0.5);
        t = s;
        x3 (t, w::ext3::specBand, 660.0);
        x3 (t, w::ext3::specWidth, 1.0);
        const auto band = note (t, 57, 0.6, &d);
        CHECK (std::abs (partialDb (band, 660.0) - partialDb (plain, 660.0)) < 0.5 && partialDb (band, 220.0) < partialDb (plain, 220.0) - 60.0);
        t = s; // (Partials 1: the loudest only)
        x3 (t, w::ext3::specPartials, 1.0);
        const auto one = note (t, 57, 0.6, &d);
        CHECK (partialDb (one, 220.0) > -30.0 && partialDb (one, 660.0) < -80.0);
    }
    // nothing above Nyquist: five octaves up (×32) 1100 Hz would sit at 35.2 kHz; it is dropped, so nothing folds back to
    // 12.8 kHz; 660 Hz (21.1 kHz, under the fade from 21.6 kHz) still plays
    {
        const auto high = note (s, 57 + 60, 0.6, &d);
        CHECK (finite (high) && partialDb (high, 7040.0) > -30.0 && partialDb (high, 21120.0) > -40.0 && partialDb (high, 48000.0 - 35200.0) < -100.0);
    }
    // a voice renders the same every time; the spectral oscillator is silent without partials
    CHECK (hash (note (s, 60, 0.3, &d)) == hash (note (s, 60, 0.3, &d)));
    const auto none = note (s, 60, 0.2, nullptr);
    CHECK (rms (none, 0, (int) none.size() / 2) == 0.0);
    // a sample without stored partials is analysed when it plays (the same partials as stored)
    w::HelixUserData plainData;
    plainData.sample = d.sample;
    CHECK (hash (note (s, 57, 0.3, &plainData)) == hash (note (s, 57, 0.3, &d)));
}

TEST (helix190mt_sample_band_limit)
{
    using namespace helix190mt;
    // a saw sample (every harmonic up to 22 kHz, 196 samples a cycle at 44.1 kHz) at its root (A3), played higher with
    // Clean off / on: much less fold-over with Clean on (aa/ in round 190mt: 34 … 46 dB less from 1.23 × its speed up); at
    // the root (and up to 1.2 × its speed) Clean reads the sample itself, bit for bit
    w::HelixUserData d;
    d.sample = saw (225.0, 3.0);
    auto s = patch();
    s[(std::size_t) (w::ext2Base + w::ext2::sampleOn)] = 1.0;
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 57.0;
    for (int up : { 0, 2, 7, 12, 19, 31 })
    {
        double r[2];
        std::uint64_t h[2];
        for (int clean = 0; clean < 2; ++clean)
        {
            auto t = s;
            x3 (t, w::ext3::sampleClean, clean);
            const auto out = note (t, 57 + up, 0.45, &d);
            CHECK (finite (out));
            h[clean] = hash (out);
            r[clean] = foldOverDb (out, 4800, 225.0 * std::pow (2.0, up / 12.0));
        }
        if (up <= 2) // (44.1 kHz at 48 kHz: ratios 0.92 and 1.03, the sample itself)
            CHECK (h[0] == h[1]);
        else
            CHECK (r[1] < r[0] - 25.0 && r[1] < -55.0);
    }
    // the levels: each cuts where it says (a 17 kHz tone is gone from level 1; a 5 kHz one passes levels 1 and 2), at
    // twice the rate their band needs
    const auto tone = [] (double hz) { return tones ({ { hz, 0.5 } }, 0.5); };
    const auto l17 = w::sampleLevels (tone (17000.0)), l5 = w::sampleLevels (tone (5000.0));
    CHECK (l17 != nullptr && l5 != nullptr && l17->levels.size() >= 8);
    if (l17 != nullptr && l5 != nullptr && l17->levels.size() >= 8)
    {
        CHECK (l17->levels[0].shift == 0 && l17->levels[2].shift == 0 && l17->levels[3].shift == 1 && l17->levels[5].shift == 2);
        CHECK (l17->levels[3].frames == 11025 && (int) l17->levels[3].l.size() >= l17->levels[3].frames + 2 * w::SampleLevels::guard);
        const auto peak = [] (const std::vector<float>& v, int from, int to) {
            float p = 0.0f;
            for (int i = from; i < to; ++i)
                p = std::max (p, std::abs (v[(std::size_t) (w::SampleLevels::guard + i)]));
            return p;
        };
        CHECK (peak (l17->levels[0].l, 2000, 20000) < 0.5f * 1.0e-3f);
        CHECK (std::abs (peak (l5->levels[0].l, 2000, 20000) - 0.5f) < 0.01f && std::abs (peak (l5->levels[1].l, 2000, 20000) - 0.5f) < 0.02f);
        CHECK (w::sampleLevels (tone (17000.0)) == l17); // (shared by content while held)
    }
}

TEST (helix190mt_state_and_instrument)
{
    using namespace lynx;
    namespace w = wt;
    // the data round trip (sample, partials, a table) and the instrument: it makes the levels and the partials from its
    // pluginState and values (UI thread), plays the grains and the partials, and takes new data while notes play
    w::HelixUserData d;
    d.sampleName = "tones";
    d.sample = helix190mt::tones ({ { 220.0, 0.4 }, { 330.0, 0.2 } }, 0.8);
    d.spectral = w::analyseSpectrum (d.sample);
    std::string error;
    CHECK (w::userTableFromFormula ("sin(pi*x)", d.tables[1], error));
    w::HelixUserData back;
    CHECK (w::decodeUserData (w::encodeUserData (d), back) && back.spectral.amp == d.spectral.amp && back.sample.frames() == d.sample.frames()
           && ! back.tables[1].empty());
    auto inst = makeHelixInstrument();
    inst->prepare (48000.0);
    ChannelDef c;
    c.instrument = InstrumentKind::helix;
    c.modelParams = helixDefaults();
    c.modelParams[(std::size_t) (w::oscBase (0) + w::osc::on)] = 0.0f;
    c.modelParams[(std::size_t) (w::ext3Base + w::ext3::grainOn)] = 1.0f;
    c.pluginState = w::encodeUserData (d);
    inst->configure (c, 120.0);
    inst->noteOn (57, 0.8f);
    std::vector<float> l (9600, 0.0f), r (9600, 0.0f);
    inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    CHECK (helix190ja::rms (l, 2400, 4800, 1) > 1e-3);
    c.modelParams[(std::size_t) (w::ext3Base + w::ext3::grainOn)] = 0.0f;
    c.modelParams[(std::size_t) (w::ext3Base + w::ext3::specOn)] = 1.0f;
    w::HelixUserData noPartials; // (no partials stored: the instrument makes them)
    noPartials.sample = d.sample;
    c.pluginState = w::encodeUserData (noPartials);
    inst->configure (c, 120.0);
    inst->noteOn (60, 0.8f);
    inst->render (l.data() + 4800, r.data() + 4800, 4800, 1.0f, 1.0f);
    CHECK (helix190ja::rms (l, 7200, 9600, 1) > 1e-3);
    for (int k = 0; k < 12; ++k) // (new data again and again while notes play)
    {
        d.sample = helix190mt::tones ({ { 200.0 + 20.0 * k, 0.4 } }, 0.3);
        d.spectral = k % 2 ? w::analyseSpectrum (d.sample) : w::SpectralData {};
        c.pluginState = w::encodeUserData (d);
        c.modelParams[(std::size_t) (w::ext3Base + w::ext3::grainOn)] = k % 3 == 0 ? 1.0f : 0.0f;
        inst->configure (c, 120.0);
        inst->render (l.data(), r.data(), 300, 1.0f, 1.0f);
    }
    c.pluginState.clear();
    inst->configure (c, 120.0);
    inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return std::isfinite (v); }));
}

TEST (helix190mt_factory_presets)
{
    using namespace lynx;
    const auto& bank = banks::bankOf (InstrumentKind::helix);
    int found = 0;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        const auto& name = bank[(std::size_t) i].name;
        if (name != "Grain Cloud" && name != "Spectral Glass" && name != "Frozen Choir")
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
        CHECK (state[(std::size_t) wt::extBase] >= 3.0 && (state[(std::size_t) (wt::ext3Base + wt::ext3::grainOn)] > 0.5 || state[(std::size_t) (wt::ext3Base + wt::ext3::specOn)] > 0.5));
        std::vector<wt::RefNote> notes { { 0, 48, 0.9f, 48000 }, { 0, 55, 0.9f, 48000 }, { 0, 60, 0.9f, 48000 } };
        const auto out = wt::renderReference (state, notes, 48000.0, 48000 * 2, 256, 120.0, &d);
        auto peak = 0.0f;
        for (auto f : out)
            peak = std::max (peak, std::fabs (f));
        CHECK (helix190mi::finite (out) && peak > 0.02f && peak < 1.5f);
    }
    CHECK (found == 3);
    // the presets before round 3 keep their places (190ml's last one is still where it was)
    CHECK (bank.size() >= 25 && bank[(std::size_t) bank.size() - 6].name == "Drawn Pulse Bass"); // (190oc: two more after round 3's)
}
