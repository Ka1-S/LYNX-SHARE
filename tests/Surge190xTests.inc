// Round 190x, issue #143: Surge XT channels of a project made in FL Studio were silent in LYNX. FL's default template
// puts every new channel in cut group 1, cut by 1 ("cut itself", 0x84 = 0x10001); FL never cuts a wrapped plugin
// with it (Surge XT VST3 renders the same with and without it, REA_rodada190x), but LYNX choked the plugin right after
// each note on at the same sample, so every note ended as it started.
namespace s190x
{
// a fake hosted plugin: records what the rack asks of it, in order
struct CallSpy final : lynx::Instrument
{
    std::vector<std::string> calls;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override { calls.push_back ("on"); }
    void noteOff (int) noexcept override { calls.push_back ("off"); }
    void allNotesOff() noexcept override { calls.push_back ("all off"); }
    void cut() noexcept override { calls.push_back ("cut"); }
    void render (float*, float*, int, float, float) noexcept override {}
};
} // namespace s190x

TEST (s190x_cut_itself_never_chokes_a_hosted_plugin)
{
    using namespace lynx;
    Song song;
    song.mixer = Song::defaultMixer (2);
    for (int k = 0; k < 2; ++k) // 0 = a hosted plugin, 1 = LYNX's synth: both "cut itself", as FL's template sets them
    {
        ChannelDef c;
        c.instrument = k == 0 ? InstrumentKind::plugin : InstrumentKind::synth;
        c.plugin.id = k == 0 ? "spy" : "";
        c.cutGroup = c.cutBy = 1;
        song.channels.push_back (c);
    }
    auto& p = song.patterns.emplace_back();
    p.notes = { { 0, 1920, 0, 48 }, { 480, 240, 0, 60 }, { 960, 240, 1, 60 } }; // a long note, a short one over it

    auto spy = std::make_shared<s190x::CallSpy>();
    RackState::Factories f;
    f.pluginInstruments = [spy] (const PluginRef&, double) { return std::shared_ptr<Instrument> (spy); };
    const auto state = RackState::build (song, nullptr, 44100.0, f);
    CHECK (state != nullptr);
    if (state == nullptr)
        return;
    // the synth is still cut by its group (its own notes and the plugin's); the plugin is never a target
    CHECK ((state->cutTargets[0] == std::vector<int> { 1 }));
    CHECK ((state->cutTargets[1] == std::vector<int> { 1 }));

    RackRenderer r;
    r.prepare (44100.0, 256);
    std::vector<float> l (256), rr (256);
    const auto tps = ticksPerSample (song.bpm, 44100.0);
    for (double tick = 0.0; tick < 1500.0; tick += 256 * tps)
        r.process (state.get(), true, tick, 256, l.data(), rr.data());

    // both notes reached the plugin, and nothing stopped them on the way (FL: the long note keeps sounding)
    const auto& calls = spy->calls;
    const auto first = std::find (calls.begin(), calls.end(), std::string ("on"));
    CHECK (std::count (calls.begin(), calls.end(), std::string ("on")) == 2);
    CHECK (std::count (first, calls.end(), std::string ("cut")) == 0);
    CHECK (std::count (first, calls.end(), std::string ("all off")) == 0);
    CHECK (std::count (first, calls.end(), std::string ("off")) == 1); // the short note's own end
}
