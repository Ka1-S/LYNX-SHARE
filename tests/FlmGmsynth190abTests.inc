// Round 190ab: FL Studio Mobile's GMSynth (module type 2) on the "GMSynth (FLM)" engine (core/GmsMobile.h).
// Uses the .flm builders of FlmImport182cTests.inc and the helpers of FlmEngines190uTests.inc. The engine reads FLM's
// own data files (wave spectra, filter table) from FL's copy of FLM's resources: the tests that render skip without it.

namespace flm190ab
{
// a GMSynth module with its 75 parameters and, when `cstm`, FLM's 20-byte custom chunk after SMPR (its own place)
inline flm182c::Bytes gmsModule (const std::vector<float>& values, bool cstm, int waveA = 18, int waveB = 18)
{
    namespace t = flm182c;
    t::Bytes b, h, desc, prst, prms, smpr, c;
    t::u32 (b, 2);
    t::u32 (b, 2);
    t::u32 (h, 2);
    t::u32 (h, 0);
    t::u8 (h, 1);
    const t::Bytes add1 { 0, 0, 127 };
    t::fixed (desc, "Default", 256);
    t::fixed (desc, "", 256);
    t::lp32 (prst, "");
    t::lp32 (prst, "Default/Default");
    for (const auto v : values)
        t::f32 (prms, v);
    for (const auto v : { 0.5f, 1.0f, 0.5f })
        t::f32 (smpr, v);
    t::u32 (c, (std::uint32_t) waveA);
    t::u32 (c, (std::uint32_t) waveB);
    for (const auto v : { 1.0f / 3.0f, 0.5f, 1.0f })
        t::f32 (c, v);
    auto parts = t::cat ({ b, t::chunk ("HEAD", h), t::chunk ("DESC", desc), t::chunk ("PRST", prst), t::chunk ("ADD1", add1), t::chunk ("PRMS", prms),
                           t::chunk ("SMPR", smpr) });
    if (cstm)
        parts = t::cat ({ parts, t::chunk ("CSTM", c) });
    return t::chunk ("RMOd", parts);
}

inline flm182c::Bytes gmsProject (const flm182c::Bytes& module)
{
    namespace t = flm182c;
    t::Bytes f, m;
    t::tag (f, "10LF");
    t::fixed (m, "2025-07-06 11-39-10", 256);
    t::f64 (m, 0);
    t::fixed (m, "", 256);
    t::fixed (m, "GMSynth", 256);
    t::u8 (m, 0);
    t::fixed (m, "", 2048);
    t::fixed (m, "2026-11-10 15:00:00", 256);
    const t::Bytes tdiv { 4, 4 };
    return t::cat ({ f, t::head ("GMSynth", 120.0), t::chunk ("META", m), t::chunk ("TDIV", tdiv), t::rack (lynx::flm::kindMaster, { t::module (28, 1) }),
                     t::rack (lynx::flm::kindNormal, { module }), t::channel (lynx::flm::kindMaster, "MASTER", 0, {}),
                     t::channel (lynx::flm::kindNormal, "Keys", 1, { t::lane (0, "Keys", { t::clip (0, 4, 4, 0, 1, { { 0, 2, 60 } }) }) }) });
}

// the import options with FLM's GMSynth data files "present" under the test's resource root
inline lynx::FlmImportOptions optionsWithData (lynx::FlpImportReport* report)
{
    auto o = flm182c::options (report);
    o.common.fileExists = [] (const std::string& p) {
        const std::string root = "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed/";
        return p == root + "FilterTable_Ctf_Res.data" || p == root + "GMSynthWaveshapes/Sine.spectra" || p == root + "GMSynthWaveshapes/Saw.spectra"
               || p == root + "GMSynthWaveshapes/Devils Horn.spectra";
    };
    return o;
}

// FL's copy of FLM's resources on this PC ("" when FL isn't installed: the rendering tests skip)
inline std::string realResources()
{
    if (realFlpDir.empty())
        return {};
    const auto root = std::filesystem::path (realFlpDir).parent_path() / "Plugins" / "Fruity" / "Generators" / "FL Studio Mobile" / "Installed";
    std::error_code ec;
    if (! std::filesystem::exists (root / "FilterTable_Ctf_Res.data", ec) || ! std::filesystem::exists (root / "GMSynthWaveshapes" / "Sine.spectra", ec))
        return {};
    return root.string();
}

inline double rmsOf (const std::vector<float>& stereo, int from, int to, int channel)
{
    double a = 0.0;
    for (int i = from; i < to; ++i)
        a += (double) stereo[2 * (std::size_t) i + (std::size_t) channel] * (double) stereo[2 * (std::size_t) i + (std::size_t) channel];
    return std::sqrt (a / (double) std::max (1, to - from));
}
} // namespace flm190ab

TEST (flm190ab_gmsynth_imports_its_parameters_and_waves)
{
    using namespace lynx;
    namespace gm = gmsmobile;
    // the defaults, without a CSTM chunk: Sine / Sine
    {
        FlpImportReport report;
        std::string error;
        const auto d = gm::defaultPatch();
        const auto song = importFlm (flm190ab::gmsProject (flm190ab::gmsModule (std::vector<float> (d.P.begin(), d.P.end()), false)),
                                     flm190ab::optionsWithData (&report), &error);
        CHECK (song.has_value());
        if (! song)
            return;
        const auto* keys = flm182c::channelNamed (*song, "Keys");
        CHECK (keys != nullptr && keys->instrument == InstrumentKind::gms && gmsIsFlm (*keys) && ! gmsStateUsable (keys->pluginState));
        CHECK (keys != nullptr && keys->samplePath == "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed");
        CHECK (report.converted.count ("FL Studio Mobile: GMSynth") == 1 && report.unsupported.count ("FL Studio Mobile: GMSynth") == 0);
        CHECK (report.partialEffects.empty()); // no effect is switched on
        if (keys == nullptr)
            return;
        const auto p = gm::readState (keys->pluginState);
        CHECK (p.has_value() && p->P == d.P && p->waveA == 18 && p->waveB == 18 && p->bendRange == 1.0f / 3.0f && p->mwAmount == 0.5f && p->mwDest == 1.0f);
        // FLM's own default table: Level 0 dB, the three slots' kinds FLNG / DLY / F LP (0.111·9 truncates to 1)
        CHECK (d.P[1] == 0.72727275f && d.P[6] == 0.2f && d.P[21] == 1.0f && d.P[32] == 1.0f && d.P[47] == 0.0f);
        CHECK (gm::fxKindOf (d.P[46]) == gm::fx::flanger && gm::fxKindOf (d.P[53]) == gm::fx::delay && gm::fxKindOf (d.P[60]) == gm::fx::lowPass);
    }
    // a changed set: every parameter its own value, waves Saw / Double Spike from the CSTM chunk
    {
        FlpImportReport report;
        std::string error;
        std::vector<float> v (75);
        for (int k = 0; k < 75; ++k)
            v[(std::size_t) k] = (float) k / 100.0f;
        v[0] = 1.0f;
        const auto song = importFlm (flm190ab::gmsProject (flm190ab::gmsModule (v, true, 16, 2)), flm190ab::optionsWithData (&report), &error);
        CHECK (song.has_value());
        if (! song)
            return;
        const auto* keys = flm182c::channelNamed (*song, "Keys");
        CHECK (keys != nullptr && gmsIsFlm (*keys));
        if (keys == nullptr)
            return;
        const auto p = gm::readState (keys->pluginState);
        CHECK (p.has_value());
        if (! p)
            return;
        bool same = true;
        for (int k = 0; k < 75; ++k)
            same = same && p->P[(std::size_t) k] == v[(std::size_t) k];
        CHECK (same && p->waveA == 16 && p->waveB == 2);
        CHECK (gm::waveFileName (2) == "Devils Horn" && gm::waveFileName (18) == "Sine" && gm::waveFileName (29).empty());
        // slot 1: kind ⌊9·0.46⌋ = 4 (F HP), on (0.47); slot 2: ⌊9·0.53⌋ = 4 too; slot 3: 5 (DLY): all rendered since
        // round 190dw (F HP was listed as not rendered before)
        CHECK (report.partialEffects.count ("FL Studio Mobile: GMSynth effect F HP (not rendered)") == 0);
        CHECK (report.partialEffects.empty());
    }
    // without FLM's data files the channel stays where it was: the built-in synth, listed as not converted
    {
        FlpImportReport report;
        std::string error;
        const auto d = gm::defaultPatch();
        const auto song = importFlm (flm190ab::gmsProject (flm190ab::gmsModule (std::vector<float> (d.P.begin(), d.P.end()), true)), flm182c::options (&report), &error);
        CHECK (song.has_value());
        if (! song)
            return;
        const auto* keys = flm182c::channelNamed (*song, "Keys");
        CHECK (keys != nullptr && keys->instrument != InstrumentKind::gms && ! gmsIsFlm (*keys));
        CHECK (report.converted.count ("FL Studio Mobile: GMSynth") == 0 && report.unsupported.count ("FL Studio Mobile: GMSynth") == 1);
    }
    // the state's own checks
    {
        auto s = gm::writeState (gm::defaultPatch());
        CHECK (s.size() == 8 + 300 + 20 && gm::readState (s).has_value());
        auto bad = s;
        bad[0] = 'X';
        CHECK (! gm::readState (bad));
        bad = s;
        bad[8 + 300] = 29; // wave A outside the list
        CHECK (! gm::readState (bad));
        CHECK (! gm::readState (std::vector<std::uint8_t> (s.begin(), s.begin() + 100)));
    }
}

TEST (flm190ab_gmsynth_laws)
{
    namespace gm = lynx::gmsmobile;
    const double fs = 44100.0;
    const auto near = [] (double a, double b) { return std::fabs (a - b) <= 1e-6 * std::fabs (b); };
    // envelope followers x += (target − x)·c: attack 8.82 / (100·v⁴·fs) (v = 0: at once), decay 8.82 / ((200·v⁷ + 0.001)·fs),
    // amp release 8.82 / ((v + 0.001)·fs), EG release 8.82 / ((200·v⁴ + 0.001)·fs)
    CHECK (gm::attackRate (0.0f, fs) == 1.0f);
    CHECK (near (gm::attackRate (0.5f, fs), 8.82 / (100.0 * 0.0625 * fs)) && near (gm::attackRate (1.0f, 48000.0), 8.82 / (100.0 * 48000.0)));
    CHECK (near (gm::decayRate (1.0f, fs), 8.82 / (200.001 * fs)) && near (gm::decayRate (0.5f, fs), 8.82 / ((200.0 / 128.0 + 0.001) * fs)));
    CHECK (near (gm::decayRate (0.0f, fs), 8.82 / (0.001 * fs)));
    CHECK (near (gm::ampReleaseRate (0.2f, fs), 8.82 / (((double) 0.2f + 0.001) * fs)));
    CHECK (near (gm::egReleaseRate (0.5f, fs), 8.82 / ((200.0 * 0.0625 + 0.001) * fs)));
    // the filter table's remap: entry i reads the file's entry ⌊(2^(i·1.4145113e-4 + 5) − 11)·3.336982534752279⌋: 32 Hz at
    // the bottom, about 0.3 Hz per file entry, the top clamped to the file's last pair
    CHECK (gm::filterFileIndex (0) == 70 && gm::filterFileIndex (65535) == 65535);
    CHECK (gm::filterFileIndex (32768) == (int) ((std::pow (2.0, 32768 * 0.00014145113298237583 + 5.0) - 11.0) * 3.336982534752279));
    bool rising = true;
    for (int i = 1; i < 65536; ++i)
        rising = rising && gm::filterFileIndex (i) >= gm::filterFileIndex (i - 1);
    CHECK (rising);
    // the tables: as the desktop engine, ⌊fs / 2f⌋ + 1 harmonics above fs / 1024, all 512 below
    CHECK (gm::tableFor (261.6256f, fs) == 84 && gm::tableFor (10.0f, fs) == 511 && gm::tableFor (20000.0f, fs) == 1 && gm::tableFor (30000.0f, fs) == 0);
    // the effect kind: 9·v truncated in float
    CHECK (gm::fxKindOf (0.0f) == 0 && gm::fxKindOf (0.11111111f) == 1 && gm::fxKindOf (0.33333334f) == 3 && gm::fxKindOf (0.5555556f) == 5
           && gm::fxKindOf (0.7222f) == 6 && gm::fxKindOf (1.0f) == 9);
    CHECK (gm::fx::implemented (gm::fx::dist) && gm::fx::implemented (gm::fx::phaser) && gm::fx::implemented (gm::fx::lowPass) && gm::fx::implemented (gm::fx::pan));
    CHECK (gm::fx::implemented (gm::fx::delay) && gm::fx::implemented (gm::fx::reverb) && gm::fx::implemented (gm::fx::flanger) /* (round 190dw: all ten) */ && std::string (gm::fx::name (gm::fx::reverb)) == "RVRB");
    // nothing plays without the data files
    CHECK (gm::renderReference (gm::defaultPatch(), { { 0, 60, 1.0f, 0.5f, -1 } }, fs, 1000, "/nowhere").empty());
}

TEST (flm190ab_gmsynth_engine_with_flms_data)
{
    using namespace lynx;
    namespace gm = gmsmobile;
    const auto root = flm190ab::realResources();
    if (root.empty())
        return;
    const int fs = 44100;
    const auto d = gm::defaultPatch();
    // the default patch (Sine, mix on osc A, choir 1, cutoff 1, Level 0 dB, centre): ≈ 0.25·w·sin per side before the
    // filter's small loss at the top of its table, at 261.63 Hz for key 60, the same on both sides
    const auto y = gm::renderReference (d, { { 0, 60, 1.0f, 0.5f, 30000 } }, fs, 44100, root);
    CHECK (y.size() == 2 * 44100);
    if (y.empty())
        return;
    const auto peak = flm190u::peakOf (y, 8000, 28000);
    CHECK (peak > 0.24f && peak < 0.255f); // 0.25 · (1 ± 0.03: the Osc Mod smoother rests at 0.01·w, a trace of AM)
    CHECK (std::fabs (flm190u::fundamental (y, 8000, 28000, fs) - 261.63) < 1.5);
    CHECK (std::fabs (flm190ab::rmsOf (y, 8000, 28000, 0) - flm190ab::rmsOf (y, 8000, 28000, 1)) < 1e-6);
    // instant attack (Level Attack 0): the first samples are already at full level
    CHECK (flm190u::peakOf (y, 0, 400) > 0.9f * peak);
    // release 0.2: the follower's time constant is (0.2 + 0.001) / 8.82 s, linear output → −8.686 dB per time constant
    {
        const double tau = 0.201 / 8.82, a = flm190ab::rmsOf (y, 30000 + 200, 30000 + 400, 0), b = flm190ab::rmsOf (y, 30000 + 200 + 1000, 30000 + 400 + 1000, 0);
        CHECK (std::fabs (std::log (a / b) - (1000.0 / fs) / tau) < 0.05);
    }
    // the voice ends (below 1e-4) and the module goes quiet
    CHECK (flm190u::peakOf (y, 44100 - 2000, 44100) == 0.0f);
    // linear velocity: half the velocity, half the level
    {
        const auto h = gm::renderReference (d, { { 0, 60, 0.5f, 0.5f, 30000 } }, fs, 30000, root);
        CHECK (std::fabs (flm190ab::rmsOf (h, 8000, 28000, 0) / flm190ab::rmsOf (y, 8000, 28000, 0) - 0.5) < 0.01);
    }
    // note pan: a linear balance (full right keeps the right side, silences the left)
    {
        const auto r = gm::renderReference (d, { { 0, 60, 1.0f, 1.0f, 30000 } }, fs, 30000, root);
        CHECK (flm190ab::rmsOf (r, 8000, 28000, 0) == 0.0 && std::fabs (flm190ab::rmsOf (r, 8000, 28000, 1) / flm190ab::rmsOf (y, 8000, 28000, 1) - 1.0) < 0.01);
    }
    // Level: 66·v − 48 dB (−6 dB at v = 42 / 66); Pan: the squared balance (v = 0.25 → right × 0.25, left × 1)
    {
        auto p = d;
        p.P[gm::param::level] = 42.0f / 66.0f;
        const auto q = gm::renderReference (p, { { 0, 60, 1.0f, 0.5f, 30000 } }, fs, 30000, root);
        CHECK (std::fabs (20.0 * std::log10 (flm190ab::rmsOf (q, 8000, 28000, 0) / flm190ab::rmsOf (y, 8000, 28000, 0)) + 6.0) < 0.02);
        p = d;
        p.P[gm::param::pan] = 0.25f;
        const auto s = gm::renderReference (p, { { 0, 60, 1.0f, 0.5f, 30000 } }, fs, 30000, root);
        CHECK (std::fabs (flm190ab::rmsOf (s, 8000, 28000, 1) / flm190ab::rmsOf (y, 8000, 28000, 1) - 0.25) < 0.005
               && std::fabs (flm190ab::rmsOf (s, 8000, 28000, 0) / flm190ab::rmsOf (y, 8000, 28000, 0) - 1.0) < 0.005);
    }
    // four voices: a fifth key takes the oldest one
    {
        gm::Player player;
        player.prepare (fs);
        player.setPatch (d, root);
        CHECK (player.resourcesLoaded() && player.idle());
        std::vector<float> l (512), r (512);
        for (int k = 0; k < 5; ++k)
        {
            player.note (60 + k, 0.8f);
            player.render (l.data(), r.data(), 512, 120.0, -1.0);
            CHECK (player.activeVoices() == std::min (k + 1, 4));
        }
        player.allNotesOff();
        for (int b = 0; b < 100; ++b)
            player.render (l.data(), r.data(), 512, 120.0, -1.0);
        CHECK (player.activeVoices() == 0 && player.idle());
    }
    // Transpose and Osc B: +12 semitones doubles the pitch; the mix at 1 plays osc B alone, 7 semitones up
    {
        auto p = d;
        p.P[gm::param::transpose] = 36.0f / 48.0f;
        const auto t = gm::renderReference (p, { { 0, 60, 1.0f, 0.5f, -1 } }, fs, 30000, root);
        CHECK (std::fabs (flm190u::fundamental (t, 8000, 28000, fs) - 523.25) < 4.0);
        p = d;
        p.P[gm::param::oscBMix] = 1.0f;
        p.P[gm::param::oscBPitch] = 31.0f / 48.0f + 0.001f;
        const auto b = gm::renderReference (p, { { 0, 60, 1.0f, 0.5f, -1 } }, fs, 30000, root);
        CHECK (std::fabs (flm190u::fundamental (b, 8000, 28000, fs) - 392.0) < 3.0);
    }
    // the filter: at cutoff 0.5 a saw loses its top; "BP" (eight poles) loses more than LP (four); HP keeps the top
    {
        auto p = d;
        p.waveA = p.waveB = 16;
        const auto full = gm::renderReference (p, { { 0, 48, 1.0f, 0.5f, -1 } }, fs, 30000, root);
        p.P[gm::param::cutoff] = 0.5f;
        const auto lp = gm::renderReference (p, { { 0, 48, 1.0f, 0.5f, -1 } }, fs, 30000, root);
        p.P[gm::param::filterKind] = 0.5f;
        const auto bp = gm::renderReference (p, { { 0, 48, 1.0f, 0.5f, -1 } }, fs, 30000, root);
        p.P[gm::param::filterKind] = 1.0f;
        const auto hp = gm::renderReference (p, { { 0, 48, 1.0f, 0.5f, -1 } }, fs, 30000, root);
        const auto diff = [] (const std::vector<float>& x) { // the level of the first difference = a measure of the top
            double a = 0.0;
            for (std::size_t i = 2 * 8000; i + 2 < 2 * 28000; i += 2)
                a += (double) (x[i + 2] - x[i]) * (double) (x[i + 2] - x[i]);
            return std::sqrt (a);
        };
        CHECK (diff (lp) < 0.5 * diff (full) && diff (bp) < 0.95 * diff (lp) && diff (hp) > diff (lp));
        CHECK (flm190ab::rmsOf (hp, 8000, 28000, 0) < flm190ab::rmsOf (lp, 8000, 28000, 0));
    }
    // a switched-on effect changes the sound, a switched-off one doesn't
    {
        auto p = d;
        p.waveA = p.waveB = 16;
        const auto dry = gm::renderReference (p, { { 0, 48, 1.0f, 0.5f, -1 } }, fs, 20000, root);
        for (const auto kind : { gm::fx::dist, gm::fx::phaser, gm::fx::lowPass, gm::fx::delay, gm::fx::reverb, gm::fx::pan })
        {
            auto e = p;
            e.P[gm::param::fxBase + gm::param::fxKind] = ((float) kind + 0.5f) / 9.0f;
            const auto off = gm::renderReference (e, { { 0, 48, 1.0f, 0.5f, -1 } }, fs, 20000, root);
            CHECK (off == dry);
            e.P[gm::param::fxBase + gm::param::fxEnable] = 1.0f;
            e.P[gm::param::fxBase + gm::param::fxX] = kind == gm::fx::pan ? 0.9f : 0.5f;
            const auto on = gm::renderReference (e, { { 0, 48, 1.0f, 0.5f, -1 } }, fs, 20000, root);
            bool finite = true;
            for (const auto v : on)
                finite = finite && std::isfinite (v) && std::fabs (v) < 4.0f;
            CHECK (finite && on != dry && flm190u::peakOf (on, 8000, 18000) > 0.001f);
        }
    }
}

TEST (flm190ab_gmsynth_plays_through_the_instrument_and_leaves_the_desktop_gms_alone)
{
    using namespace lynx;
    namespace gm = gmsmobile;
    // a desktop GMS channel is not FLM's, whatever its state
    {
        ChannelDef c;
        c.instrument = InstrumentKind::gms;
        CHECK (! gmsIsFlm (c));
        c.modelParams = { 0.0f };
        CHECK (! gmsIsFlm (c));
        c.modelParams = { 1.0f };
        CHECK (gmsIsFlm (c));
        c.instrument = InstrumentKind::minisynth;
        CHECK (! gmsIsFlm (c));
    }
    const auto root = flm190ab::realResources();
    if (root.empty())
        return;
    const int fs = 44100, total = 512 * 48;
    auto patch = gm::defaultPatch();
    patch.waveB = 16;
    patch.P[gm::param::oscBMix] = 0.8f;
    ChannelDef c;
    c.instrument = InstrumentKind::gms;
    c.modelParams = { 1.0f };
    c.pluginState = gm::writeState (patch);
    c.samplePath = root;
    auto inst = makeInstrument (InstrumentKind::gms);
    inst->prepare (fs);
    inst->configure (c, 120.0);
    std::vector<float> l ((std::size_t) total, 0.0f), r ((std::size_t) total, 0.0f);
    inst->noteOn (57, 0.6f);
    for (int at = 0; at < total; at += 512)
    {
        if (at == 512 * 16)
            inst->noteOff (57);
        inst->render (l.data() + at, r.data() + at, 512, 1.0f, 1.0f);
    }
    // the same notes through the module's reference renderer: the instrument adds nothing of the desktop host's
    const auto ref = gm::renderReference (patch, { { 0, 57, 0.6f, 0.5f, 512 * 16 } }, fs, total, root);
    CHECK (ref.size() == 2 * (std::size_t) total);
    if (ref.size() != 2 * (std::size_t) total)
        return;
    double err = 0.0, sum = 0.0;
    for (int i = 0; i < total; ++i)
    {
        err += std::pow ((double) l[(std::size_t) i] - ref[2 * (std::size_t) i], 2.0) + std::pow ((double) r[(std::size_t) i] - ref[2 * (std::size_t) i + 1], 2.0);
        sum += std::pow ((double) ref[2 * (std::size_t) i], 2.0) + std::pow ((double) ref[2 * (std::size_t) i + 1], 2.0);
    }
    CHECK (sum > 1.0 && err == 0.0);
    CHECK (inst->idle());
    // the channel's volume and pan: a gain and a linear balance on the module's output
    {
        auto half = makeInstrument (InstrumentKind::gms);
        half->prepare (fs);
        half->configure (c, 120.0);
        std::vector<float> hl (2048, 0.0f), hr (2048, 0.0f);
        half->noteOn (57, 0.6f);
        for (int at = 0; at < 2048; at += 512)
            half->render (hl.data() + at, hr.data() + at, 512, 0.5f, 0.5f);
        double e = 0.0;
        for (int i = 0; i < 2048; ++i)
            e = std::max (e, std::fabs ((double) hl[(std::size_t) i] - 0.5 * l[(std::size_t) i]));
        CHECK (e < 1e-6);
    }
    // the same instrument object back on a desktop state plays the desktop engine again (no sound without its waves)
    {
        ChannelDef desk;
        desk.instrument = InstrumentKind::gms;
        inst->configure (desk, 120.0);
        std::vector<float> dl (512, 0.0f), dr (512, 0.0f);
        inst->noteOn (60, 0.8f);
        inst->render (dl.data(), dr.data(), 512, 1.0f, 1.0f);
        CHECK (flm190u::peakOf (dl, 0, 256) == 0.0f);
    }
}
