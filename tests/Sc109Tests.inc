// Issue #109: a hosted plugin's side input is on only while its slot has a sidechain source (Waves' RCompressor listened
// to a silent side bus and never compressed). The rack build tells each plugin slot's effect after every build.

namespace sc109
{
struct SideSpy final : lynx::Effect
{
    std::vector<bool> calls;
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const lynx::EffectParams&, double) noexcept override {}
    void setSideInputActive (bool on) override { calls.push_back (on); }
};
} // namespace sc109

TEST (sc109_plugin_side_input_follows_the_slots_sidechain_source)
{
    auto s = lynx::Song::makeDefault();
    CHECK (s.mixer.size() > 3);
    lynx::EffectSlot slot;
    slot.kind = lynx::EffectKind::plugin;
    slot.plugin.id = "VST3-Compressor";
    s.mixer[1].effects.clear();
    s.mixer[1].effects.push_back (slot);

    std::vector<std::shared_ptr<sc109::SideSpy>> made;
    lynx::RackState::Factories f;
    f.pluginEffects = [&] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Effect> {
        made.push_back (std::make_shared<sc109::SideSpy>());
        return made.back();
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, f);
    CHECK (made.size() == 1 && made[0]->calls == std::vector<bool> { false }); // no source: off

    s.mixer[1].effects[0].sidechain = 3;
    state = lynx::RackState::build (s, state.get(), 48000.0, f);
    CHECK (made.size() == 1); // the running plugin stays (its state too)
    CHECK (made[0]->calls == (std::vector<bool> { false, true }));

    s.mixer[1].effects[0].sidechain = 1; // itself: not a source
    state = lynx::RackState::build (s, state.get(), 48000.0, f);
    s.mixer[1].effects[0].sidechain = -1;
    state = lynx::RackState::build (s, state.get(), 48000.0, f);
    CHECK (made.size() == 1 && made[0]->calls == (std::vector<bool> { false, true, false, false }));
}
