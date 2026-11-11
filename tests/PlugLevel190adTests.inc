// Round 190ad: hosted plugins of an FL project played 3 dB under FL (Surge XT VST3, FL 2026 renders, REA_rodada190ad.md).
// Fruity Wrapper scales the plugin's output by the host's pan kernel of (channel pan, 2 × the channel volume curve):
// √2 × the curve at the centre, 2 × on the side panned to; LYNX's own channel law is 1 × at the centre. Projects
// imported from .flp follow FL (Song::flPluginLevels); LYNX's own projects keep their law.
namespace plug190ad
{
// a fake hosted plugin: keeps the gains the rack hands it
struct GainSpy final : lynx::Instrument
{
    float gl = -1.0f, gr = -1.0f;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float l, float r) noexcept override { gl = l; gr = r; }
};

// the gains the rack hands a hosted plugin channel, under FL's levels or LYNX's own law
std::pair<float, float> gains (bool flLevels, float volume, float pan, bool triangular = false)
{
    using namespace lynx;
    Song song;
    song.mixer = Song::defaultMixer (2);
    song.flPluginLevels = flLevels;
    song.panTriangular = triangular;
    ChannelDef c;
    c.instrument = InstrumentKind::plugin;
    c.plugin.id = "spy";
    c.volume = volume;
    c.pan = pan;
    song.channels.push_back (c);
    auto& p = song.patterns.emplace_back();
    p.notes = { { 0, 960, 0, 60 } };
    auto spy = std::make_shared<GainSpy>();
    RackState::Factories f;
    f.pluginInstruments = [spy] (const PluginRef&, double) { return std::shared_ptr<Instrument> (spy); };
    const auto state = RackState::build (song, nullptr, 44100.0, f);
    if (state == nullptr)
        return { -1.0f, -1.0f };
    RackRenderer r;
    r.prepare (44100.0, 256);
    std::vector<float> l (256), rr (256);
    r.process (state.get(), true, 0.0, 256, l.data(), rr.data());
    return { spy->gl, spy->gr };
}

bool close (float a, float b, float tolerance = 2.0e-6f) { return std::abs (a - b) <= tolerance; }
float db (float ratio) { return 20.0f * std::log10 (ratio); }
} // namespace plug190ad

TEST (plug190ad_lynx_projects_keep_their_plugin_level)
{
    using namespace lynx;
    // LYNX's own law: the curve at the centre, √2 × on the side panned to — unchanged for every project saved before
    for (const auto v : { 0.25f, 0.5f, 0.78f, 1.0f })
    {
        const auto [l, r] = plug190ad::gains (false, v, 0.0f);
        const auto angle = 0.25f * 3.14159265f;
        CHECK (plug190ad::close (l, channelGain (v) * std::cos (angle) * 1.41421356f, 1.0e-7f)); // (the old panGains)
        CHECK (plug190ad::close (r, channelGain (v) * std::sin (angle) * 1.41421356f, 1.0e-7f));
    }
    const auto [l, r] = plug190ad::gains (false, 0.78f, -1.0f);
    CHECK (plug190ad::close (l, channelGain (0.78f) * 1.41421356f));
    CHECK (std::abs (r) < 1.0e-6f);
    // the triangular law never reached a plugin channel under LYNX's own law
    CHECK (plug190ad::gains (false, 0.78f, -0.5f, true) == plug190ad::gains (false, 0.78f, -0.5f, false));
}

TEST (plug190ad_fl_projects_play_hosted_plugins_at_the_wrapper_level)
{
    using namespace lynx;
    const float sqrt2 = 1.41421356f;
    // centre: √2 × the curve, +3.01 dB over LYNX's own law at every volume (FL's renders: +3.0 dB at 25 / 50 / 78 / 100 %)
    for (const auto v : { 0.25f, 0.5f, 0.78f, 1.0f })
    {
        const auto [l, r] = plug190ad::gains (true, v, 0.0f);
        CHECK (plug190ad::close (l, channelGain (v) * sqrt2));
        CHECK (plug190ad::close (r, channelGain (v) * sqrt2));
        const auto own = plug190ad::gains (false, v, 0.0f);
        CHECK (std::abs (plug190ad::db (l / own.first) - 3.0103f) < 0.001f);
    }
    // FL's default channel (78 %) leaves a plugin at −2.2 dB, where LYNX's own law leaves it at −5.2 dB
    CHECK (std::abs (plug190ad::db (plug190ad::gains (true, 0.78f, 0.0f).first) - -2.20f) < 0.01f);
    CHECK (std::abs (plug190ad::db (plug190ad::gains (false, 0.78f, 0.0f).first) - -5.21f) < 0.01f);

    // pan: FL's kernel of (pan, 2 × the curve): the side panned to reaches 2 ×, the other 0
    const auto g = channelGain (0.78f);
    {
        const auto [l, r] = plug190ad::gains (true, 0.78f, -1.0f);
        CHECK (plug190ad::close (l, 2.0f * g));
        CHECK (std::abs (r) < 1.0e-6f);
    }
    {
        const auto [l, r] = plug190ad::gains (true, 0.78f, 1.0f);
        CHECK (plug190ad::close (r, 2.0f * g));
        CHECK (std::abs (l) < 1.0e-6f);
    }
    {
        const auto [l, r] = plug190ad::gains (true, 0.78f, -0.5f);
        CHECK (plug190ad::close (l, 2.0f * g * std::cos (3.14159265f / 8.0f)));
        CHECK (plug190ad::close (r, 2.0f * g * std::sin (3.14159265f / 8.0f)));
    }
    // the wrapper's pan is an integer −64…64 (FL's renders: R / L = 1.0249 at 1.5 / 64, 1.0503 at 2.5 / 64, 1 at 0.4 / 64)
    {
        CHECK (plug190ad::gains (true, 0.78f, 1.5f / 64.0f) == plug190ad::gains (true, 0.78f, 1.0f / 64.0f));
        CHECK (plug190ad::gains (true, 0.78f, 2.5f / 64.0f) == plug190ad::gains (true, 0.78f, 2.0f / 64.0f));
        CHECK (plug190ad::gains (true, 0.78f, 0.4f / 64.0f) == plug190ad::gains (true, 0.78f, 0.0f));
        CHECK (plug190ad::gains (true, 0.78f, -1.5f / 64.0f) == plug190ad::gains (true, 0.78f, -1.0f / 64.0f));
        const auto [l, r] = plug190ad::gains (true, 0.78f, 1.5f / 64.0f);
        CHECK (std::abs (r / l - 1.0249f) < 0.0002f);
        const auto [l2, r2] = plug190ad::gains (true, 0.78f, 2.5f / 64.0f);
        CHECK (std::abs (r2 / l2 - 1.0503f) < 0.0002f);
        const auto [l3, r3] = plug190ad::gains (true, 0.78f, -0.5f);
        CHECK (std::abs (r3 / l3 - 0.4142f) < 0.0002f); // (FL: 0.4142 at half left)
    }
    // the project's triangular law: the louder side keeps the centre level, the other falls linearly
    // (FL's render at half left: L at the centre's level, R / L = 0.5000)
    {
        const auto [l, r] = plug190ad::gains (true, 0.78f, -0.5f, true);
        CHECK (plug190ad::close (l, g * sqrt2));
        CHECK (plug190ad::close (r, g * sqrt2 * 0.5f));
        const auto centre = plug190ad::gains (true, 0.78f, 0.0f, true);
        CHECK (plug190ad::close (centre.first, g * sqrt2) && plug190ad::close (centre.second, g * sqrt2));
    }
}

TEST (plug190ad_only_hosted_plugins_follow_the_wrapper_level)
{
    using namespace lynx;
    // the same song with FL's levels on and off renders the same when it has no hosted plugin (LYNX's synth here)
    std::vector<float> out[2];
    for (int fl = 0; fl < 2; ++fl)
    {
        Song song;
        song.mixer = Song::defaultMixer (2);
        song.flPluginLevels = fl == 1;
        ChannelDef c;
        c.instrument = InstrumentKind::synth;
        c.pan = -0.3f;
        song.channels.push_back (c);
        auto& p = song.patterns.emplace_back();
        p.notes = { { 0, 960, 0, 57 } };
        const auto state = RackState::build (song, nullptr, 44100.0, RackState::Factories {});
        CHECK (state != nullptr);
        if (state == nullptr)
            return;
        RackRenderer r;
        r.prepare (44100.0, 256);
        std::vector<float> l (256), rr (256);
        const auto tps = ticksPerSample (song.bpm, 44100.0);
        for (int b = 0; b < 16; ++b)
        {
            r.process (state.get(), true, b * 256 * tps, 256, l.data(), rr.data());
            out[fl].insert (out[fl].end(), l.begin(), l.end());
            out[fl].insert (out[fl].end(), rr.begin(), rr.end());
        }
    }
    CHECK (out[0] == out[1]);
    CHECK (std::any_of (out[0].begin(), out[0].end(), [] (float x) { return x != 0.0f; }));
}

TEST (plug190ad_new_projects_take_fl_plugin_levels)
{
    using namespace lynx;
    // File › New and every built-in template: FL's levels (none of them holds a hosted plugin)
    CHECK (makeNewProject().flPluginLevels);
    for (int i = 0; i < (int) templateNames().size(); ++i)
    {
        const auto song = makeNewProject (i);
        CHECK (song.flPluginLevels);
        CHECK (std::none_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::plugin; }));
    }
    // what a project saved before reads as (no key): LYNX's own law
    CHECK (! Song::makeDefault().flPluginLevels);
    CHECK (! Song {}.flPluginLevels);
    // a user's template: without a hosted plugin it takes FL's levels; with one made under LYNX's law it keeps them
    auto plain = Song::makeDefault();
    CHECK (adoptFlPluginLevels (plain) && plain.flPluginLevels);
    auto withPlugin = Song::makeDefault();
    ChannelDef c;
    c.instrument = InstrumentKind::plugin;
    c.plugin.id = "x";
    withPlugin.channels.push_back (c);
    CHECK (! adoptFlPluginLevels (withPlugin) && ! withPlugin.flPluginLevels);
    withPlugin.flPluginLevels = true; // (a template saved from now on: already FL's)
    CHECK (adoptFlPluginLevels (withPlugin));
}

TEST (plug190ad_wrapper_velocity_is_round_128)
{
    using lynx::hostpitch::flWrapperVelocity;
    // an .flp note's velocity byte b is b / 128 in LYNX: FL's wrapper sends b itself (127 at most)
    for (int b = 1; b <= 128; ++b)
        CHECK (flWrapperVelocity ((float) b / 128.0f) == std::min (b, 127));
    CHECK (flWrapperVelocity (100.0f / 128.0f) == 100); // (LYNX's own law, round(127 v), sends 99)
    CHECK ((int) std::lround (100.0f / 128.0f * 127.0f) == 99);
    CHECK (flWrapperVelocity (0.5f / 128.0f) == 1);  // Round(0.5) = 0 in FL; a note on stays a note on
    CHECK (flWrapperVelocity (2.5f / 128.0f) == 2);  // ties to even, as FL's Round
    CHECK (flWrapperVelocity (3.5f / 128.0f) == 4);
    CHECK (flWrapperVelocity (0.0f) == 1);
    CHECK (flWrapperVelocity (2.0f) == 127);
}

TEST (plug190ad_flp_import_and_export_keep_the_plugin_level)
{
    using namespace lynx;
    const auto written = [] (const Song& s) {
        flpexport::Options options;
        flpexport::Report report;
        return flp::parseProject (flpexport::exportFlp (s, options, report));
    };
    const auto back = [] (const flp::Project& p) {
        FlpImportOptions o;
        o.fileExists = [] (const std::string&) { return true; };
        return importFlp (p, o);
    };
    auto s = Song::makeDefault();
    CHECK (! s.flPluginLevels); // LYNX's own projects: their own law
    s.channels.resize (2);
    auto& c = s.channels[0];
    c.instrument = InstrumentKind::plugin;
    c.plugin.name = "Keys";
    c.plugin.foreign.format = ForeignPlugin::Format::vst2;
    c.plugin.foreign.vst2Id = 0x4B455953;
    c.plugin.foreign.name = "Keys";
    c.volume = 0.78f;
    s.channels[1].instrument = InstrumentKind::synth;
    s.channels[1].volume = 0.6f;

    // a project with FL's levels: the knob goes out and comes back as it is
    s.flPluginLevels = true;
    {
        const auto again = back (written (s));
        CHECK (again.flPluginLevels);
        CHECK (again.channels.size() >= 2);
        if (again.channels.size() >= 2)
        {
            // (the import keeps the wrapper's plugin as `foreign`; the app makes the channel a hosted plugin when it
            // finds that plugin installed)
            CHECK (again.channels[0].plugin.foreign.format == ForeignPlugin::Format::vst2 && again.channels[0].plugin.foreign.vst2Id == 0x4B455953u);
            CHECK (std::abs (again.channels[0].volume - 0.78f) < 1.0e-4f);
            CHECK (std::abs (again.channels[1].volume - 0.6f) < 1.0e-4f);
        }
    }
    // LYNX's own law: FL plays the plugin √2 louder at the same knob, so the knob written is the one that sounds
    // the same in FL (and in LYNX when the .flp comes back); other channels keep theirs
    s.flPluginLevels = false;
    {
        const auto again = back (written (s));
        CHECK (again.flPluginLevels);
        CHECK (again.channels.size() >= 2);
        if (again.channels.size() >= 2)
        {
            CHECK (again.channels[0].volume < 0.78f);
            CHECK (std::abs (channelGain (again.channels[0].volume) * 1.41421356f - channelGain (0.78f)) < 2.0e-4f);
            CHECK (std::abs (again.channels[1].volume - 0.6f) < 1.0e-4f);
        }
    }
}

TEST (plug190ad_a_patcher_channel_is_not_compensated)
{
    using namespace lynx;
    // FL's levels apply to the rack's own hosted plugin channels only: what a Patcher hosts keeps the Patcher's level
    // (the channel is a Patcher, not a wrapper; nothing inside it reads the project's flag), so it is never raised
    // twice — the same render with the flag on and off
    std::vector<float> out[2];
    for (int fl = 0; fl < 2; ++fl)
    {
        Song song;
        song.mixer = Song::defaultMixer (2);
        song.flPluginLevels = fl == 1;
        ChannelDef c;
        c.instrument = InstrumentKind::patcher;
        c.pan = 0.25f;
        c.patcher.nodes = { patcher162::generatorNode (InstrumentKind::synth) };
        c.patcher.wires = { patcher162::midi (patchFromHost, 0, 0, 0), patcher141::audio (0, patchToHost) };
        song.channels.push_back (c);
        auto& p = song.patterns.emplace_back();
        p.notes = { { 0, 960, 0, 57 } };
        const auto state = RackState::build (song, nullptr, 44100.0);
        CHECK (state != nullptr);
        if (state == nullptr)
            return;
        RackRenderer r;
        r.prepare (44100.0, 256);
        std::vector<float> l (256), rr (256);
        const auto tps = ticksPerSample (song.bpm, 44100.0);
        for (int b = 0; b < 16; ++b)
        {
            r.process (state.get(), true, b * 256 * tps, 256, l.data(), rr.data());
            out[fl].insert (out[fl].end(), l.begin(), l.end());
            out[fl].insert (out[fl].end(), rr.begin(), rr.end());
        }
    }
    CHECK (out[0] == out[1]);
    CHECK (std::any_of (out[0].begin(), out[0].end(), [] (float x) { return x != 0.0f; }));
}
