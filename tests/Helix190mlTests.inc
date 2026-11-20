// Round 190ml: Helix+ 2 (core/WavetableSynth.h, core/WavetableUser.cpp): user tables (import audio, draw, formulas,
// harmonics; kept in the channel's pluginState), anti-aliased hard-edged warps, oscillator 3, the sample oscillator,
// macros 5…8, chaos sources, the frequency shifter / EQ / multiband in the rack, the new factory presets. Uses the helpers
// of Helix190jaTests.inc and Helix190miTests.inc. No data file.

namespace helix190ml
{
namespace w = lynx::wt;
using helix190ja::db;
using helix190ja::rms;
using helix190mi::finite;
using helix190mi::hash;

// a revision 2 patch: Helix's law-test base (no filter, full sustain), Analog Saw, both blocks live at their defaults
inline std::vector<double> patch (const char* table = "Analog Saw")
{
    auto s = helix190mi::plus (helix190ja::base(), 2.0);
    s[(std::size_t) w::tableBase] = (double) w::tableByName (table);
    s[(std::size_t) w::volume] = 0.5;
    return s;
}
inline void x2 (std::vector<double>& s, int i, double v) { s[(std::size_t) (w::ext2Base + i)] = v; }
inline std::vector<float> note (const std::vector<double>& s, int key = 57, double seconds = 0.5, const w::HelixUserData* data = nullptr)
{
    w::RefNote n;
    n.key = key;
    n.release = (int) (seconds * 0.8 * 48000.0);
    return w::renderReference (s, { n }, 48000.0, (int) (seconds * 48000.0), 256, 120.0, data);
}
// a sine as a sample (mono, at `rate`)
inline w::SampleBuffer sine (double hz, double seconds, double rate = 44100.0)
{
    w::SampleBuffer b;
    b.rate = rate;
    b.l.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < b.l.size(); ++i)
        b.l[i] = (float) (0.8 * std::sin (2.0 * 3.14159265358979 * hz * (double) i / rate));
    return b;
}
} // namespace helix190ml

TEST (helix190ml_layout_and_parameters)
{
    using namespace lynx;
    namespace w = wt;
    // the second block follows Helix+'s; every (target, source) pair has its own value, none inside another's
    CHECK (w::ext2Base == w::extBase + w::ext::count && w::stateSize >= w::ext2Base + w::ext2::count); // (190mt: round 3's block follows)
    CHECK (w::allSourceCount == 23 && w::rev2TargetCount == 68 && w::src::macro1 + 7 == 20 && w::src::chaos1 == 21);
    std::set<int> cells;
    for (int t = 0; t < w::allTargetCount; ++t)
        for (int s = 0; s < w::allSourceCount; ++s)
        {
            const auto i = w::cellIndex (t, s);
            CHECK (i >= w::matrixBase && i < w::stateSize && ! w::automatable (i));
            cells.insert (i);
        }
    CHECK ((int) cells.size() == w::allTargetCount * w::allSourceCount);
    CHECK (w::cellIndex (3, 4) == w::matrixIndex (3, 4) && w::cellIndex (w::tgt::warpAmount, 16) == w::extBase + w::ext::extCells + 16);
    // the parameter table stays sorted and holds Helix+ 2's values by name
    const auto& t = helixEngineParams();
    CHECK (std::is_sorted (t.begin(), t.end(), [] (const EngineParam& a, const EngineParam& b) { return a.index < b.index; }));
    for (const auto* name : { "Osc 3 Pos", "Osc 3 Warp 2 Amount", "Sample Start", "Macro 8", "Chaos 2 Rate", "Shift", "EQ Mid Freq", "Multiband Depth" })
    {
        const auto* p = modelparams::findByName (InstrumentKind::helix, name);
        CHECK (p != nullptr && p->index > w::ext2Base && w::automatable (p->index));
    }
    CHECK (modelparams::findByName (InstrumentKind::helix, "Macro 4") != nullptr && ! w::automatable (w::ext2Base) && ! w::automatable (w::ext2Base + w::ext2::osc3Table)
           && ! w::automatable (w::ext2Base + w::ext2::userTable));
    CHECK (findEngineParam (t, w::extBase + w::ext::distOn)->name == "Distortion On"); // (190mi's names stay where they were)
    // revision 2 for a new channel; a Helix+ value set on an older channel takes it to 2, Live's values leave it alone
    CHECK (helixDefaults()[(std::size_t) w::extBase] >= 2.0f && (int) helixDefaults().size() == w::stateSize); // (190mt: 3)
    ChannelDef old;
    old.instrument = InstrumentKind::helix;
    old.modelParams.assign ((std::size_t) w::liveStateSize, 0.25f);
    auto a = old;
    CHECK (modelparams::setRawValue (a, w::volume, 0.5f) && a.modelParams[(std::size_t) w::extBase] == 0.0f);
    CHECK (modelparams::setRawValue (a, w::ext2Base + w::ext2::macro5 + 2, 0.7f) && a.modelParams[(std::size_t) w::extBase] == (float) w::currentRevision);
    // texts and names
    CHECK (w::paramText (w::ext2Base + w::ext2::chaosMode, 1) == "Lorenz" && w::paramText (w::ext2Base + w::ext2::osc3 + w::osc::effectMode, 3) == "Modern");
    CHECK (w::paramText (w::ext2Base + w::ext2::shift, 0.2) == "+40.0 Hz" && w::paramText (w::ext2Base + w::ext2::eqMidFreq, 2500.0) == "2.50 kHz");
    CHECK (w::sourceName (w::src::macro1 + 7) == "Macro 8" && w::sourceName (w::src::chaos1 + 1) == "Chaos 2");
    CHECK (w::targetName (w::tgt::osc3 + w::tgt::oscPos) == "Osc 3 Pos" && w::targetName (w::tgt::mbDepth) == "Multiband Depth");
    CHECK (w::targetOfParam (w::ext2Base + w::ext2::osc3 + w::osc::gain) == w::tgt::osc3 + w::tgt::oscGain
           && w::targetOfParam (w::ext2Base + w::ext2::sampleDetune) == w::tgt::samplePitch && w::targetOfParam (w::ext2Base + w::ext2::eqLow) == -1);
    for (const auto& p : t)
        if (const auto row = w::targetOfParam (p.index); row >= 0)
            CHECK (row < w::allTargetCount && ! w::targetName (row).empty());
}

TEST (helix190ml_old_projects_bit_identical)
{
    using namespace helix190ml;
    for (int variant = 0; variant < 6; ++variant)
    {
        const auto live = helix190mi::livePatch (variant);
        const auto before = hash (helix190mi::note (live));
        // revisions 0 and 1 ignore the second block whatever it holds; revision 2 with it at its defaults sounds as before
        for (double rev : { 0.0, 1.0, 2.0 })
        {
            auto s = helix190mi::plus (live, rev);
            CHECK (hash (helix190mi::note (s)) == before);
            if (rev > 1.5)
                continue;
            for (int i = 1; i < w::ext2::autoEnd; ++i)
                s[(std::size_t) (w::ext2Base + i)] = (i % 3 == 0) ? 1.0 : 0.6 + 0.01 * i;
            s[(std::size_t) w::cellIndex (w::tgt::oscPitch, w::src::chaos1)] = 1.0;
            s[(std::size_t) w::cellIndex (w::tgt::osc3 + w::tgt::oscGain, w::src::macro1)] = 1.0;
            CHECK (hash (helix190mi::note (s)) == before);
        }
    }
    // a 190mi channel's warps keep 190mi's sound at revision 1 (the anti-aliasing is revision 2's)
    auto s = helix190mi::plus (helix190mi::livePatch (0), 1.0);
    helix190mi::x (s, w::ext::warpMode, w::warp::pwm);
    helix190mi::x (s, w::ext::warpAmount, 0.6);
    const auto rev1 = hash (helix190mi::note (s));
    s[(std::size_t) w::extBase] = 2.0;
    CHECK (hash (helix190mi::note (s)) != rev1);
}

TEST (helix190ml_warps_anti_aliased)
{
    using namespace helix190ml;
    // Sync, PWM, Mirror, Flip, Crush: revision 2 folds less than revision 1 (190mi) on a saw and a sine, never more;
    // at A4 at least 10 dB less and below −75 dB [190ml aa/: −60 … −40 dB → −110 … −80 dB]
    for (const auto* table : { "Analog Saw", "Shapes" })
        for (int m : { w::warp::sync, w::warp::pwm, w::warp::mirror, w::warp::flip, w::warp::crush })
            for (double a : { 0.45, 0.9 })
                for (int key : { 45, 69 })
                {
                    double r[2];
                    for (int rev = 1; rev <= 2; ++rev)
                    {
                        auto s = patch (table);
                        s[(std::size_t) w::extBase] = rev;
                        helix190mi::x (s, w::ext::warpMode, m);
                        helix190mi::x (s, w::ext::warpAmount, a);
                        const auto out = note (s, key, 0.45);
                        CHECK (finite (out));
                        r[rev - 1] = helix190ja::nonHarmonicDb (out, 4800, 4800 + 14400, 440.0 * std::pow (2.0, (key - 69) / 12.0), 48000.0);
                    }
                    // (A4: 10 dB less, or already at the measurement's floor; below −70 dB)
                    const bool ok4 = key != 69 || ((r[1] < r[0] - 10.0 || r[1] < -88.0) && r[1] < -70.0);
                    CHECK (r[1] <= r[0] + 0.5 && ok4);
                    if (r[1] > r[0] + 0.5 || ! ok4)
                        std::printf ("  %s warp %d %.2f key %d: %.1f -> %.1f dB\n", table, m, a, key, r[0], r[1]);
                }
}

TEST (helix190ml_oscillator_3)
{
    using namespace helix190ml;
    auto s = patch ("Shapes"); // (a sine at position 0)
    const auto one = note (s, 57);
    // oscillator 3 an octave up: the octave partial appears; off: it is silent whatever it holds
    auto three = s;
    x2 (three, w::ext2::osc3 + w::osc::transpose, 12.0);
    x2 (three, w::ext2::osc3Table, (double) w::tableByName ("Shapes"));
    CHECK (hash (note (three, 57)) == hash (one));
    x2 (three, w::ext2::osc3 + w::osc::on, 1.0);
    const auto both = note (three, 57);
    CHECK (finite (both) && helix190ja::partial (one, 4800, 19200, 440.0, 48000.0) < 1e-3 && helix190ja::partial (both, 4800, 19200, 440.0, 48000.0) > 0.05);
    // its own rows: Macro 7 → Osc 3 Gain pulls it down (a level row), Chaos 1 → Osc 3 Pitch moves it
    auto mac = three;
    mac[(std::size_t) w::cellIndex (w::tgt::osc3 + w::tgt::oscGain, w::src::macro1 + 6)] = 1.0;
    CHECK (helix190ja::partial (note (mac, 57), 4800, 19200, 440.0, 48000.0) < 1e-3);
    x2 (mac, w::ext2::macro5 + 2, 1.0);
    CHECK (helix190ja::partial (note (mac, 57), 4800, 19200, 440.0, 48000.0) > 0.05);
    // Warp 2 on oscillator 3 (FM ← oscillator 1), unison 16, all three oscillators in Split routing: clean and finite
    auto w3 = three;
    x2 (w3, w::ext2::osc3WarpMode, w::warp::fm);
    x2 (w3, w::ext2::osc3WarpAmount, 0.5);
    CHECK (hash (note (w3, 57)) != hash (both));
    w3[(std::size_t) w::unisonMode] = w::unisonClassic;
    helix190mi::x (w3, w::ext::unisonCount, 16);
    w3[(std::size_t) w::routing] = w::routeSplit;
    w3[(std::size_t) w::oscBase (1) + w::osc::on] = 1.0;
    const auto rich = note (w3, 45);
    CHECK (finite (rich) && rms (rich, 4800, 19200) > 1e-3);
}

TEST (helix190ml_sample_oscillator)
{
    using namespace helix190ml;
    auto s = patch();
    s[(std::size_t) w::oscBase (0) + w::osc::on] = 0.0;
    x2 (s, w::ext2::sampleOn, 1.0);
    x2 (s, w::ext2::sampleRoot, 69.0);
    w::HelixUserData d;
    d.sample = sine (440.0, 0.2); // (0.2 s at 44.1 kHz)
    // root A4: A5 plays it an octave up; one shot: silent after its 0.1 s at that speed
    const auto out = note (s, 81, 0.5, &d);
    CHECK (finite (out) && std::fabs (helix190ja::frequency (out, 480, 4320, 48000.0) - 880.0) < 2.0);
    CHECK (rms (out, 600, 4000) > 0.05 && rms (out, 6000, 12000) == 0.0);
    // looped: it keeps playing; no sample (or Sample off): nothing
    x2 (s, w::ext2::sampleLoop, 1.0);
    const auto looped = note (s, 81, 0.5, &d);
    CHECK (rms (looped, 6000, 12000) > 0.05 && finite (looped));
    CHECK (rms (note (s, 81, 0.3), 600, 4000) == 0.0);
    // unison spreads it, Start moves into it, Sync restarts it at the note (a pitched tone from any sample), Crush steps it
    auto u = s;
    u[(std::size_t) w::unisonMode] = w::unisonClassic;
    u[(std::size_t) w::unisonVoices] = 4;
    CHECK (helix190mi::sideDifference (note (u, 69, 0.3, &d)) > 1.0);
    auto sync = s;
    x2 (sync, w::ext2::sampleWarpMode, w::warp::sync);
    x2 (sync, w::ext2::sampleWarpAmount, 0.4);
    const auto synced = note (sync, 57, 0.4, &d);
    CHECK (finite (synced) && helix190ja::nonHarmonicDb (synced, 4800, 14400, 220.0, 48000.0) < -30.0); // (periodic at the note: 220 Hz)
    auto crush = s;
    x2 (crush, w::ext2::sampleWarpMode, w::warp::crush);
    x2 (crush, w::ext2::sampleWarpAmount, 0.8);
    CHECK (finite (note (crush, 69, 0.3, &d)) && hash (note (crush, 69, 0.3, &d)) != hash (note (s, 69, 0.3, &d)));
    // a stereo sample, a tiny one, a sample swapped while notes play: no read outside it
    w::HelixUserData st;
    st.sample = sine (100.0, 0.05, 48000.0);
    st.sample.r = st.sample.l;
    for (auto& v : st.sample.r)
        v = -v;
    CHECK (finite (note (s, 60, 0.3, &st)));
    w::HelixUserData tiny;
    tiny.sample.l = { 0.1f, 0.2f, 0.3f };
    CHECK (finite (note (s, 60, 0.2, &tiny)));
    lynx::wt::Engine e;
    e.prepare (48000.0);
    e.setSample (&d.sample);
    e.setState (s.data());
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    e.note (96, 1.0f);
    e.render (l.data(), r.data(), 2400, 120.0, -1.0);
    e.setSample (&tiny.sample);
    e.render (l.data() + 2400, r.data() + 2400, 2400, 120.0, -1.0);
    CHECK (std::all_of (l.begin(), l.end(), [] (float f) { return std::isfinite (f) && std::fabs (f) < 4.0f; }));
}

TEST (helix190ml_user_tables)
{
    using namespace helix190ml;
    // formulas: a sine, a saw, errors with their place, limits
    w::UserTable t;
    std::string error;
    CHECK (w::userTableFromFormula ("sin(pi*x)", t, error) && error.empty() && ! t.empty());
    CHECK (t.amp[0] > 0.99f && t.amp[0] < 1.01f && (t.partials < 2 || t.amp[1] < 1e-4f));
    CHECK (w::userTableFromFormula ("saw(p) * (1 - t) + square(p) * t", t, error));
    CHECK (t.amp[1] > 0.1f && t.amp[(std::size_t) (63 * t.partials + 1)] < 1e-3f); // (frame 0 a saw: 2nd partial; frame 63 a square: none)
    for (const auto* bad : { "", "sin(", "x +", "foo(x)", "2 ** 3", "x ? 1", "min(x)", "system(1)" })
    {
        w::UserTable u;
        CHECK (! w::userTableFromFormula (bad, u, error) && ! error.empty() && u.empty());
    }
    CHECK (! w::userTableFromFormula (std::string (500, '(') + "x" + std::string (500, ')'), t, error));
    CHECK (w::userTableFromFormula ("1/0 + log(-1) + sqrt(-x) + x % 0 + rand(f) * 0.1 + (x > 0 && t < 0.5 ? 1 : -1) * 2^-2^1", t, error) && ! t.empty());
    // a drawn cycle comes back as drawn (band-limited), the harmonics of a frame are its partials
    std::vector<float> square (w::userFrameLength);
    for (int i = 0; i < w::userFrameLength; ++i)
        square[(std::size_t) i] = i < w::userFrameLength / 2 ? 1.0f : -1.0f;
    auto drawn = w::userTableOf (w::tableByName ("Shapes"));
    w::setUserTableCycle (drawn, 10, square);
    CHECK (drawn.partials >= 511);
    CHECK (std::fabs (drawn.amp[(std::size_t) (10 * drawn.partials)] - 4.0f / 3.14159265f) < 0.01f && drawn.amp[(std::size_t) (10 * drawn.partials + 1)] < 1e-3f);
    const auto back = w::userTableCycle (drawn, 10, w::userFrameLength);
    CHECK (std::fabs (back[(std::size_t) w::userFrameLength / 4] - 1.0f) < 0.05f && std::fabs (back[(std::size_t) w::userFrameLength * 3 / 4] + 1.0f) < 0.05f);
    // the data round trip (amplitudes as half floats, phases to 1/65536 of a turn), and a sample with it
    w::HelixUserData d;
    d.tables[1] = drawn;
    d.tables[1].name = "My Square";
    d.sampleName = "kick.wav";
    d.sample = sine (60.0, 0.1);
    const auto bytes = w::encodeUserData (d);
    w::HelixUserData r;
    CHECK (! bytes.empty() && w::decodeUserData (bytes, r) && r.tables[0].empty() && r.tables[2].empty() && r.tables[1].name == "My Square");
    CHECK (r.tables[1].partials <= drawn.partials && r.sampleName == "kick.wav" && r.sample.frames() == d.sample.frames() && r.sample.r.empty());
    double worst = 0.0;
    for (int f = 0; f < w::frameCount; ++f)
        for (int h = 0; h < r.tables[1].partials; ++h)
            worst = std::max (worst, (double) std::fabs (r.tables[1].amp[(std::size_t) (f * r.tables[1].partials + h)] - drawn.amp[(std::size_t) (f * drawn.partials + h)]));
    CHECK (worst < 2e-3);
    w::HelixUserData junk;
    CHECK (w::encodeUserData (w::HelixUserData {}).empty() && ! w::decodeUserData ({ 1, 2, 3 }, junk) && junk.empty());
    auto truncated = bytes;
    truncated.resize (bytes.size() / 2);
    w::HelixUserData part;
    CHECK (w::decodeUserData (truncated, part)); // (what is whole loads)
    // the same data → the same built table; it plays on the oscillator that uses it
    const auto t1 = w::userTableBuilt (d.tables[1]), t2 = w::userTableBuilt (r.tables[1]);
    CHECK (t1 != nullptr && t1 == w::userTableBuilt (d.tables[1]) && t2 != nullptr && t1->name == "My Square");
    auto s = patch ("Shapes");
    s[(std::size_t) w::oscBase (0) + w::osc::position] = 10.0 / 63.0;
    const auto plain = note (s, 57, 0.3, &d);
    x2 (s, w::ext2::userTable + 0, 1.0); // (oscillator 1 has no user table: the factory one plays)
    CHECK (hash (note (s, 57, 0.3, &d)) == hash (plain));
    d.tables[0] = drawn;
    const auto mine = note (s, 57, 0.3, &d);
    CHECK (hash (mine) != hash (plain) && finite (mine) && helix190ja::partial (mine, 2400, 12000, 660.0, 48000.0) > 0.05);
    CHECK (helix190ja::nonHarmonicDb (note (s, 93, 0.3, &d), 2400, 12000, 1760.0, 48000.0) < -70.0); // (its mips: clean at A6)
    // the WAV of a table: RIFF float, 64 frames
    const auto wav = w::userTableWav (drawn);
    CHECK (wav.size() == 44 + (std::size_t) w::frameCount * w::userFrameLength * 4 && std::memcmp (wav.data(), "RIFF", 4) == 0);
}

TEST (helix190ml_import_audio)
{
    using namespace helix190ml;
    // a 220 Hz saw at 48 kHz brightening over 1 s: pitch slicing finds its cycle; the table's frames follow the brightening
    std::vector<float> audio (48000);
    for (int i = 0; i < 48000; ++i)
    {
        const auto ph = std::fmod (220.0 * i / 48000.0, 1.0);
        const auto bright = (double) i / 48000.0;
        audio[(std::size_t) i] = (float) ((1.0 - bright) * std::sin (2.0 * 3.14159265358979 * ph) + bright * (1.0 - 2.0 * ph)) * 0.5f;
    }
    CHECK (std::fabs (w::detectPeriod (audio.data(), 48000, 48000.0) - 48000.0 / 220.0) < 0.5);
    std::string noteText;
    w::ImportOptions o;
    auto t = w::userTableFromAudio (audio.data(), 48000, 48000.0, o, "Saw rise", &noteText);
    CHECK (! t.empty() && noteText.find ("220.0 Hz") != std::string::npos);
    CHECK (t.amp[1] < 0.05f * t.amp[0] && t.amp[(std::size_t) (63 * t.partials + 1)] > 0.3f * t.amp[(std::size_t) (63 * t.partials)]);
    // normalised: every frame at the same loudness; phase-aligned: every fundamental at the same phase
    for (int f : { 0, 31, 63 })
    {
        double e = 0.0;
        for (int h = 0; h < t.partials; ++h)
            e += (double) t.amp[(std::size_t) (f * t.partials + h)] * t.amp[(std::size_t) (f * t.partials + h)];
        CHECK (std::fabs (std::sqrt (e) - 1.0) < 1e-3 && std::fabs (t.phase[(std::size_t) (f * t.partials)]) < 1e-3);
    }
    // fixed frames of 2048 (a table exported as a WAV comes back), spread, noise (no pitch: fixed), silence
    std::vector<float> frames;
    auto saw = w::userTableOf (w::tableByName ("Analog Saw"));
    for (int f = 0; f < w::frameCount; ++f)
    {
        const auto c = w::userTableCycle (saw, f, w::userFrameLength);
        frames.insert (frames.end(), c.begin(), c.end());
    }
    o.slicing = w::Slicing::fixed;
    o.normalise = o.phaseAlign = false;
    const auto again = w::userTableFromAudio (frames.data(), (int) frames.size(), 48000.0, o, "again", &noteText);
    CHECK (! again.empty() && std::fabs (again.amp[0] - saw.amp[0]) < 0.01f && noteText.find ("64 frames of 2048") != std::string::npos);
    o.slicing = w::Slicing::spread;
    CHECK (! w::userTableFromAudio (audio.data(), 48000, 48000.0, o, "spread", &noteText).empty());
    std::vector<float> noise (20000);
    std::uint32_t seed = 1;
    for (auto& v : noise)
        seed = seed * 1664525u + 1013904223u, v = (float) ((double) (seed >> 8) / 8388608.0 - 1.0);
    o.slicing = w::Slicing::pitch;
    CHECK (! w::userTableFromAudio (noise.data(), 20000, 48000.0, o, "noise", &noteText).empty() && noteText.find ("No pitch") != std::string::npos);
    std::vector<float> silence (4000, 0.0f);
    CHECK (w::detectPeriod (silence.data(), 4000, 48000.0) == 0.0 && w::userTableFromAudio (silence.data(), 10, 48000.0, o, "x", &noteText).empty());
}

TEST (helix190ml_macros_and_chaos)
{
    using namespace helix190ml;
    // Macro 6 → Pitch on Live's rows: +3 st at 1 (0.125 × 24 st on Osc 1 Pitch)
    auto s = patch ("Shapes");
    s[(std::size_t) w::cellIndex (w::tgt::oscPitch, w::src::macro1 + 5)] = 0.125;
    CHECK (std::fabs (helix190ja::frequency (note (s, 57), 4800, 19200, 48000.0) - 220.0) < 0.5);
    x2 (s, w::ext2::macro5 + 1, 1.0);
    CHECK (std::fabs (helix190ja::frequency (note (s, 57), 4800, 19200, 48000.0) - 220.0 * std::pow (2.0, 3.0 / 12.0)) < 0.5);
    // the chaos sources: bounded, their own per mode, deterministic for a voice's seed
    for (int mode = 0; mode < 3; ++mode)
    {
        w::ChaosState a, b;
        a.start (7), b.start (7);
        float lo = 1.0f, hi = -1.0f;
        bool same = true;
        for (int i = 0; i < 20000; ++i)
        {
            const auto va = w::chaosStep (mode, a, 2.0 * 16.0 / 48000.0), vb = w::chaosStep (mode, b, 2.0 * 16.0 / 48000.0);
            same = same && va == vb;
            lo = std::min (lo, va), hi = std::max (hi, va);
        }
        CHECK (same && lo >= -1.0f && hi <= 1.0f && hi - lo > 0.5f);
        CHECK (std::isfinite (w::chaosStep (mode, a, 1.0e9)));
    }
    // Chaos 1 (Lorenz) → Osc 1 Pitch: the pitch wanders; Global Mod Amount 0 stills it (an internal source)
    auto c = patch ("Shapes");
    x2 (c, w::ext2::chaosMode, 1);
    x2 (c, w::ext2::chaosRate, 4.0);
    c[(std::size_t) w::cellIndex (w::tgt::oscPitch, w::src::chaos1)] = 0.05;
    const auto wander = note (c, 57, 1.0);
    double lo = 1e9, hi = 0.0;
    for (int k = 0; k < 7; ++k)
    {
        const auto f = helix190ja::frequency (wander, 2400 + k * 4800, 7200 + k * 4800, 48000.0);
        lo = std::min (lo, f), hi = std::max (hi, f);
    }
    CHECK (finite (wander) && hi - lo > 5.0 && lo > 190.0 && hi < 250.0);
    c[(std::size_t) w::modAmount] = 0.0;
    CHECK (std::fabs (helix190ja::frequency (note (c, 57, 1.0), 26400, 33600, 48000.0) - 220.0) < 0.5);
}

TEST (helix190ml_effects_rack)
{
    using namespace helix190ml;
    auto s = patch ("Shapes");
    s[(std::size_t) w::envBase (0) + w::env::release] = 0.01;
    const auto dry = note (s, 81, 0.5); // (an 880 Hz sine)
    // the shifter: +100 Hz moves the sine to 980 Hz (no image at 780)
    auto f = s;
    x2 (f, w::ext2::shiftOn, 1.0);
    x2 (f, w::ext2::shift, std::cbrt (100.0 / 5000.0));
    const auto shifted = note (f, 81, 0.5);
    CHECK (finite (shifted) && std::fabs (w::shiftHz (std::cbrt (100.0 / 5000.0)) - 100.0) < 1e-6);
    const auto up = helix190ja::partial (shifted, 4800, 14400, 980.0, 48000.0), image = helix190ja::partial (shifted, 4800, 14400, 780.0, 48000.0),
               left = helix190ja::partial (shifted, 4800, 14400, 880.0, 48000.0);
    CHECK (up > 0.1 && image < 0.03 * up && left < 0.03 * up);
    // the EQ: the low shelf +12 dB lifts a 60 Hz sine about 4×, the high shelf leaves it
    auto low = patch ("Shapes");
    const auto plain = note (low, 34, 0.5);
    x2 (low, w::ext2::eqOn, 1.0);
    x2 (low, w::ext2::eqLow, 12.0);
    CHECK (std::fabs (db (rms (note (low, 34, 0.5), 9600, 19200)) - db (rms (plain, 9600, 19200)) - 12.0) < 1.0);
    x2 (low, w::ext2::eqLow, 0.0);
    x2 (low, w::ext2::eqHigh, 12.0);
    CHECK (std::fabs (db (rms (note (low, 34, 0.5), 9600, 19200)) - db (rms (plain, 9600, 19200))) < 0.5);
    // the multiband: a quiet note comes up (upward), a loud one goes down (downward); Depth 0 leaves the sound as it is
    auto mb = patch ("Shapes");
    mb[(std::size_t) w::volume] = 0.004;
    const auto quiet = note (mb, 69, 0.5);
    x2 (mb, w::ext2::mbOn, 1.0);
    CHECK (db (rms (note (mb, 69, 0.5), 12000, 19200)) > db (rms (quiet, 12000, 19200)) + 3.0);
    auto loud = patch ("Shapes");
    loud[(std::size_t) w::volume] = 1.0;
    const auto loudDry = note (loud, 69, 0.5);
    x2 (loud, w::ext2::mbOn, 1.0);
    CHECK (db (rms (note (loud, 69, 0.5), 12000, 19200)) < db (rms (loudDry, 12000, 19200)) - 2.0);
    x2 (loud, w::ext2::mbDepth, 0.0);
    CHECK (std::fabs (db (rms (note (loud, 69, 0.5), 12000, 19200)) - db (rms (loudDry, 12000, 19200))) < 0.1);
    // all together with the 190mi rack: finite, and the engine falls idle after the tail
    auto all = patch();
    for (int on : { w::ext::distOn, w::ext::chorusOn, w::ext::delayOn, w::ext::reverbOn })
        helix190mi::x (all, on, 1.0);
    for (int on : { w::ext2::shiftOn, w::ext2::eqOn, w::ext2::mbOn })
        x2 (all, on, 1.0);
    all[(std::size_t) w::envBase (0) + w::env::release] = 0.01;
    lynx::wt::Engine e;
    e.prepare (48000.0);
    e.setState (all.data());
    std::vector<float> l (48000 * 12, 0.0f), r (48000 * 12, 0.0f);
    e.note (60, 1.0f);
    e.render (l.data(), r.data(), 9600, 120.0, -1.0);
    e.note (60, 0.0f);
    for (int pos = 9600; pos < (int) l.size(); pos += 512)
        e.render (l.data() + pos, r.data() + pos, std::min (512, (int) l.size() - pos), 120.0, -1.0);
    CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return std::isfinite (v) && std::fabs (v) < 8.0f; }) && e.idle());
}

TEST (helix190ml_instrument_user_data)
{
    using namespace lynx;
    namespace w = wt;
    // the instrument builds the channel's tables and sample from its pluginState (UI thread) and plays them; new data
    // replaces the old while notes play
    auto inst = makeHelixInstrument();
    inst->prepare (48000.0);
    ChannelDef c;
    c.instrument = InstrumentKind::helix;
    c.modelParams = helixDefaults();
    c.modelParams[(std::size_t) (w::ext2Base + w::ext2::userTable)] = 1.0f;
    c.modelParams[(std::size_t) (w::ext2Base + w::ext2::sampleOn)] = 1.0f;
    w::HelixUserData d;
    std::string error;
    CHECK (w::userTableFromFormula ("square(p)", d.tables[0], error));
    d.sample = helix190ml::sine (220.0, 0.5);
    c.pluginState = w::encodeUserData (d);
    inst->configure (c, 120.0);
    inst->noteOn (60, 0.8f);
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    inst->render (l.data(), r.data(), 2400, 1.0f, 1.0f);
    CHECK (helix190ja::rms (l, 0, 2400, 1) > 1e-3);
    for (int k = 0; k < 20; ++k) // (new data again and again: the old tables are let go once the audio thread has the new)
    {
        CHECK (w::userTableFromFormula ("sin(pi*x*" + std::to_string (k + 1) + ")", d.tables[0], error));
        c.pluginState = w::encodeUserData (d);
        inst->configure (c, 120.0);
        inst->render (l.data() + 2400, r.data() + 2400, 100, 1.0f, 1.0f);
    }
    c.pluginState.clear();
    inst->configure (c, 120.0);
    inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return std::isfinite (v); }));
}

TEST (helix190ml_factory_presets)
{
    using namespace lynx;
    const auto& bank = banks::bankOf (InstrumentKind::helix);
    int newOnes = 0;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::helix;
        c.pluginState = { 'j', 'u', 'n', 'k' };
        const auto loaded = banks::withPreset (c, i);
        CHECK (loaded.has_value());
        if (! loaded)
            continue;
        CHECK (loaded->modelParams[(std::size_t) wt::extBase] == (float) wt::currentRevision);
        wt::HelixUserData d;
        CHECK (loaded->pluginState.empty() || wt::decodeUserData (loaded->pluginState, d)); // (a preset's data replaces the channel's)
        const auto state = helixState (*loaded);
        bool usesNew = ! loaded->pluginState.empty();
        for (int k = 1; k < wt::ext2::autoEnd && ! usesNew; ++k)
            usesNew = state[(std::size_t) (wt::ext2Base + k)] != wt::defaults()[(std::size_t) (wt::ext2Base + k)];
        newOnes += usesNew ? 1 : 0;
        std::vector<wt::RefNote> notes { { 0, 48, 0.9f, 48000 }, { 0, 55, 0.9f, 48000 }, { 0, 60, 0.9f, 48000 } };
        const auto out = wt::renderReference (state, notes, 48000.0, 48000 * 2, 256, 120.0, &d);
        auto peak = 0.0f;
        for (auto f : out)
            peak = std::max (peak, std::fabs (f));
        CHECK (helix190mi::finite (out) && peak > 0.02f && peak < 1.5f);
        if (! (peak > 0.02f && peak < 1.5f))
            std::printf ("  %-18s peak %.2f\n", bank[(std::size_t) i].name.c_str(), peak);
    }
    CHECK (newOnes >= 10);
}
