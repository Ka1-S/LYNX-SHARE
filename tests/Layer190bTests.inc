// Round 190b, issue #141: what is played into a Layer channel besides notes (the sustain pedal CC 64, the wheels, the
// other controllers) goes to its children, the same way a channel played directly gets it.
namespace l190b
{
// a fake hosted plugin: records the MIDI messages and the controller changes it gets
struct ControlSpy final : lynx::Instrument
{
    std::vector<std::vector<std::uint8_t>> got;
    std::vector<std::pair<int, float>> controls;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    void midiMessage (const std::uint8_t* bytes, int size) noexcept override { got.emplace_back (bytes, bytes + size); }
    void controlChange (int cc, float value) noexcept override { controls.emplace_back (cc, value); }
};

// channel 0 = a Layer over channels 1 and 2; channel 3 = a plugin outside the Layer; `setup` may change the song
struct Rig
{
    lynx::Song song;
    std::vector<std::shared_ptr<ControlSpy>> spies; // [channel]
    std::unique_ptr<const lynx::RackState> state;
    lynx::RackRenderer renderer;
    lynx::LiveEventQueue live;

    explicit Rig (std::function<void (lynx::Song&)> setup = {})
    {
        song.mixer = lynx::Song::defaultMixer (2);
        lynx::ChannelDef l;
        l.name = "Layer";
        l.instrument = lynx::InstrumentKind::layer;
        l.layer.children = { 1, 2 };
        song.channels.push_back (l);
        for (int k = 1; k <= 3; ++k)
        {
            lynx::ChannelDef c;
            c.instrument = lynx::InstrumentKind::plugin;
            c.plugin.id = "spy" + std::to_string (k);
            song.channels.push_back (c);
        }
        song.patterns.emplace_back();
        if (setup)
            setup (song);
        spies.resize (song.channels.size());
        lynx::RackState::Factories f;
        f.pluginInstruments = [this] (const lynx::PluginRef& ref, double) {
            const auto k = (std::size_t) std::stoi (ref.id.substr (3));
            spies[k] = std::make_shared<ControlSpy>();
            return std::shared_ptr<lynx::Instrument> (spies[k]);
        };
        state = lynx::RackState::build (song, nullptr, 44100.0, f);
        renderer.prepare (44100.0, 256);
    }

    void control (int channel, int controller, float value)
    {
        lynx::NoteEvent e;
        e.type = lynx::NoteEvent::Type::control;
        e.channel = channel;
        e.key = controller;
        e.velocity = value;
        live.push (e);
    }

    void run()
    {
        std::vector<float> l (256), r (256);
        renderer.process (state.get(), false, 0.0, 256, l.data(), r.data(), &live, nullptr);
    }
};
} // namespace l190b

TEST (l190b_layer_passes_the_sustain_pedal_to_its_children)
{
    using Bytes = std::vector<std::uint8_t>;
    l190b::Rig rig;
    CHECK (rig.spies[1] != nullptr && rig.spies[2] != nullptr && rig.spies[3] != nullptr);
    if (! (rig.spies[1] != nullptr && rig.spies[2] != nullptr && rig.spies[3] != nullptr))
        return;

    // pedal down, then up, played into the Layer
    rig.control (0, 64, 1.0f);
    rig.run();
    for (int c = 1; c <= 2; ++c)
    {
        CHECK (rig.spies[(std::size_t) c]->got.size() == 1);
        if (rig.spies[(std::size_t) c]->got.size() == 1)
            CHECK ((rig.spies[(std::size_t) c]->got[0] == Bytes { 0xB0, 64, 127 }));
        CHECK (rig.spies[(std::size_t) c]->controls.size() == 1);
    }
    CHECK (rig.spies[3]->got.empty()); // (not one of the Layer's children)
    rig.control (0, 64, 0.0f);
    rig.run();
    CHECK (rig.spies[1]->got.size() == 2);
    CHECK (rig.spies[2]->got.size() == 2);
    CHECK (rig.spies[3]->got.empty());

    // the same pedal played straight into a channel: the Layer's children got exactly that
    rig.control (3, 64, 1.0f);
    rig.control (3, 64, 0.0f);
    rig.run();
    CHECK (rig.spies[3]->got == rig.spies[1]->got);
    CHECK (rig.spies[3]->got == rig.spies[2]->got);
    CHECK (rig.spies[3]->controls == rig.spies[1]->controls);
    if (rig.spies[1]->got.size() == 2)
        CHECK ((rig.spies[1]->got[1] == Bytes { 0xB0, 64, 0 }));
}

TEST (l190b_layer_passes_wheels_and_controllers_to_its_children)
{
    using Bytes = std::vector<std::uint8_t>;
    l190b::Rig rig;
    CHECK (rig.spies[1] != nullptr && rig.spies[2] != nullptr);
    if (! (rig.spies[1] != nullptr && rig.spies[2] != nullptr))
        return;
    rig.control (0, lynx::NoteEvent::controlPitchBend, 1.0f);
    rig.control (0, 1, 64.0f / 127.0f);                       // mod wheel
    rig.control (0, lynx::NoteEvent::controlPressure, 1.0f);
    rig.control (0, 66, 1.0f);                                // sostenuto
    rig.run();
    const std::vector<Bytes> expected { { 0xE0, 127, 127 }, { 0xB0, 1, 64 }, { 0xD0, 127 }, { 0xB0, 66, 127 } };
    CHECK (rig.spies[1]->got == expected);
    CHECK (rig.spies[2]->got == expected);
    CHECK (rig.spies[3]->got.empty());
    CHECK (std::abs (rig.spies[1]->wheelCents() - 200.0f) < 1.0e-3f); // (the default pitch range: 2 semitones)
}

TEST (l190b_layer_controls_skip_itself_and_nested_layers)
{
    // children: itself, another Layer (FL's Layers don't nest), an index past the rack, and one real child
    l190b::Rig rig ([] (lynx::Song& s) {
        s.channels[2].instrument = lynx::InstrumentKind::layer;
        s.channels[2].layer.children = { 0, 3 };
        s.channels[0].layer.children = { 0, 2, 99, -1, 1 };
    });
    CHECK (rig.spies[1] != nullptr && rig.spies[3] != nullptr);
    if (! (rig.spies[1] != nullptr && rig.spies[3] != nullptr))
        return;
    rig.control (0, 64, 1.0f);
    rig.run();
    CHECK (rig.spies[1]->got.size() == 1);
    CHECK (rig.spies[3]->got.empty()); // (the nested Layer's child: not reached through the outer Layer)
}
