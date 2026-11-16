// Round 190jl (Discord #help, 0.1.29): a MIDI Out channel sent nothing to a hardware synth. FL's Options › MIDI ›
// Output "Port": the MIDI Out channels (and plugin wrapper output ports) on a device's port play that device. The rack
// keeps what they play, with its sample, for the engine's MIDI output thread (engine/MidiSyncIo.h).

namespace hw190jl
{
struct Out
{
    std::int64_t at = 0; // absolute sample
    int port = 0, source = 0;
    std::vector<std::uint8_t> bytes;
};

lynx::ChannelDef midiOutChannel (int midiChannel, int port, int program = 0)
{
    lynx::midiout::State st;
    st.channel = midiChannel;
    st.port = port;
    st.program = program;
    lynx::ChannelDef c;
    c.name = "MIDI Out";
    c.instrument = lynx::InstrumentKind::midiOut;
    c.pluginState = lynx::midiout::write (st);
    return c;
}

lynx::Song songWith (const lynx::ChannelDef& channel)
{
    lynx::Song s;
    s.bpm = 120.0;
    s.mixer = lynx::Song::defaultMixer (2);
    s.channels.push_back (channel);
    s.patterns.emplace_back();
    s.patterns[0].lengthTicks = lynx::ppq * 4;
    s.mode = lynx::PlayMode::pattern;
    return s;
}

std::array<std::uint64_t, 4> ports (std::initializer_list<int> list)
{
    std::array<std::uint64_t, 4> bits {};
    for (const auto p : list)
        bits[(std::size_t) p >> 6] |= std::uint64_t { 1 } << (p & 63);
    return bits;
}

// `blocks` blocks of 256 at 48 kHz from tick `start`; what the rack kept for the hardware outputs
std::vector<Out> run (lynx::RackRenderer& r, const lynx::RackState& state, bool playing, int blocks, std::int64_t& sample, double& tick,
                      lynx::LiveEventQueue* live = nullptr)
{
    std::vector<Out> out;
    std::vector<float> l (256), rr (256);
    std::array<lynx::RackRenderer::HardwareMidiEvent, lynx::RackRenderer::hardwareMidiCapacity> taken {};
    const auto tps = 120.0 * lynx::ppq / (60.0 * 48000.0);
    for (int b = 0; b < blocks; ++b)
    {
        r.process (&state, playing, tick, 256, l.data(), rr.data(), live);
        const auto n = r.takeHardwareMidi (taken.data(), (int) taken.size());
        for (int k = 0; k < n; ++k)
            out.push_back ({ sample + taken[(std::size_t) k].sample, taken[(std::size_t) k].port, taken[(std::size_t) k].source,
                             { taken[(std::size_t) k].bytes.begin(), taken[(std::size_t) k].bytes.begin() + taken[(std::size_t) k].size } });
        sample += 256;
        if (playing)
            tick += 256 * tps;
    }
    return out;
}

std::vector<Out> notesOf (const std::vector<Out>& all)
{
    std::vector<Out> notes;
    for (const auto& o : all)
        if (! o.bytes.empty() && (o.bytes[0] & 0xE0) == 0x80)
            notes.push_back (o);
    return notes;
}
} // namespace hw190jl

TEST (midiout190jl_pattern_notes_reach_the_hardware_port_to_the_sample)
{
    auto s = hw190jl::songWith (hw190jl::midiOutChannel (3, 1, 5));
    lynx::Note n;
    n.channel = 0;
    n.key = 62;
    n.start = lynx::ppq / 4;  // 6000 samples at 120 BPM / 48 kHz
    n.length = lynx::ppq / 2; // ends at 18000
    n.velocity = 0.78f;
    s.patterns[0].notes.push_back (n);
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    CHECK (state->hardwareMidiChannels == std::vector<int> { 0 } && state->hardwareMidiPort[0] == 1);

    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    r.setHardwareMidiPorts (hw190jl::ports ({ 1 }));
    std::int64_t sample = 0;
    double tick = 0.0;
    const auto all = hw190jl::run (r, *state, true, 80, sample, tick);
    const auto notes = hw190jl::notesOf (all);
    CHECK (notes.size() == 2);
    if (notes.size() == 2)
    {
        // MIDI channel 4 (index 3), key 62, round (127 × 0.78) = 99; the off at the note's end
        CHECK (notes[0].bytes == (std::vector<std::uint8_t> { 0x93, 62, 99 }) && std::abs (notes[0].at - 6000) <= 1 && notes[0].port == 1
               && notes[0].source == 0);
        CHECK (notes[1].bytes == (std::vector<std::uint8_t> { 0x83, 62, 0 }) && std::abs (notes[1].at - 18000) <= 1 && notes[1].port == 1);
    }
    // FL's reset when playback starts: the program change (patch 5 = program 4) on its MIDI channel, at the start
    CHECK (std::any_of (all.begin(), all.end(), [] (const hw190jl::Out& o) { return o.bytes == std::vector<std::uint8_t> { 0xC3, 4 } && o.at == 0; }));
    // time order
    CHECK (std::is_sorted (all.begin(), all.end(), [] (const hw190jl::Out& a, const hw190jl::Out& b) { return a.at < b.at; }));
}

TEST (midiout190jl_only_a_port_an_output_device_has_is_sent)
{
    auto s = hw190jl::songWith (hw190jl::midiOutChannel (0, 1));
    lynx::Note n;
    n.channel = 0;
    n.start = 0;
    n.length = lynx::ppq;
    s.patterns[0].notes.push_back (n);
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    for (const auto& set : { hw190jl::ports ({ 2 }), hw190jl::ports ({ 0, 3, 200 }), hw190jl::ports ({}) })
    {
        lynx::RackRenderer r;
        r.prepare (48000.0, 256);
        r.setHardwareMidiPorts (set);
        std::int64_t sample = 0;
        double tick = 0.0;
        CHECK (hw190jl::run (r, *state, true, 20, sample, tick).empty()); // (another port, or none: nothing)
    }
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    r.setHardwareMidiPorts (hw190jl::ports ({ 1, 200 }));
    std::int64_t sample = 0;
    double tick = 0.0;
    const auto notes = hw190jl::notesOf (hw190jl::run (r, *state, true, 20, sample, tick));
    CHECK (notes.size() == 1 && notes[0].bytes[0] == 0x90 && notes[0].at == 0);

    // a muted MIDI Out channel sends no notes
    s.channels[0].muted = true;
    auto muted = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::RackRenderer rm;
    rm.prepare (48000.0, 256);
    rm.setHardwareMidiPorts (hw190jl::ports ({ 1 }));
    sample = 0;
    tick = 0.0;
    CHECK (hw190jl::notesOf (hw190jl::run (rm, *muted, true, 20, sample, tick)).empty());
}

TEST (midiout190jl_stop_ends_the_notes_at_the_device)
{
    auto s = hw190jl::songWith (hw190jl::midiOutChannel (2, 7));
    lynx::Note n;
    n.channel = 0;
    n.key = 48;
    n.start = 0;
    n.length = lynx::ppq * 2;
    s.patterns[0].notes.push_back (n);
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    r.setHardwareMidiPorts (hw190jl::ports ({ 7 }));
    std::int64_t sample = 0;
    double tick = 0.0;
    auto playing = hw190jl::notesOf (hw190jl::run (r, *state, true, 10, sample, tick));
    CHECK (playing.size() == 1 && playing[0].bytes == (std::vector<std::uint8_t> { 0x92, 48, 99 }));
    const auto stopped = hw190jl::notesOf (hw190jl::run (r, *state, false, 2, sample, tick));
    CHECK (stopped.size() == 1 && stopped[0].bytes == (std::vector<std::uint8_t> { 0x82, 48, 0 }) && stopped[0].at == 10 * 256);
    // nothing more after that (the note was ended once)
    CHECK (hw190jl::notesOf (hw190jl::run (r, *state, false, 4, sample, tick)).empty());
}

TEST (midiout190jl_live_input_keeps_its_source_and_controllers_follow)
{
    auto s = hw190jl::songWith (hw190jl::midiOutChannel (5, 1));
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    r.setHardwareMidiPorts (hw190jl::ports ({ 1 }));
    lynx::LiveEventQueue live;
    std::int64_t sample = 0;
    double tick = 0.0;
    hw190jl::run (r, *state, false, 1, sample, tick, &live); // (configured once)

    lynx::NoteEvent on;
    on.channel = 0;
    on.key = 64;
    on.velocity = 100.0f / 127.0f;
    on.source = 3; // MIDI input 3 plays the MIDI Out channel
    live.push (on);
    lynx::NoteEvent cc;
    cc.type = lynx::NoteEvent::Type::control;
    cc.channel = 0;
    cc.key = 74;
    cc.velocity = 64.0f / 127.0f;
    cc.source = 3;
    live.push (cc);
    lynx::NoteEvent off = on;
    off.type = lynx::NoteEvent::Type::off;
    off.velocity = 0.0f;
    const auto first = hw190jl::run (r, *state, false, 1, sample, tick, &live);
    live.push (off);
    const auto second = hw190jl::run (r, *state, false, 1, sample, tick, &live);
    // the live velocity comes out as it came in (round (127 v)), on the MIDI Out's channel 6, with its input
    CHECK (std::any_of (first.begin(), first.end(), [] (const hw190jl::Out& o) {
        return o.bytes == std::vector<std::uint8_t> { 0x95, 64, 100 } && o.source == 3 && o.port == 1;
    }));
    CHECK (std::any_of (first.begin(), first.end(), [] (const hw190jl::Out& o) {
        return o.bytes == std::vector<std::uint8_t> { 0xB5, 74, 64 } && o.source == 3;
    }));
    CHECK (second.size() == 1 && second[0].bytes == (std::vector<std::uint8_t> { 0x85, 64, 0 }) && second[0].source == 3);

    // a song note afterwards carries no source (the device gets it even from its own input's port)
    lynx::Note n;
    n.channel = 0;
    n.start = 0;
    n.length = lynx::ppq;
    s.patterns[0].notes.push_back (n);
    auto withNote = lynx::RackState::build (s, nullptr, 48000.0);
    tick = 0.0;
    const auto song = hw190jl::notesOf (hw190jl::run (r, *withNote, true, 2, sample, tick));
    CHECK (! song.empty() && song[0].source == 0);
}

TEST (midiout190jl_output_port_saved_and_old_prefs_load)
{
    lynx::prefs::MidiPrefs p;
    lynx::prefs::MidiOutputPrefs o;
    o.name = "M-VAVE FM-1 MIDI 1";
    o.masterSync = true;
    o.port = 1;
    p.setOutput (o);
    lynx::prefs::MidiOutputPrefs portOnly;
    portOnly.name = "Synth";
    portOnly.port = 12;
    p.setOutput (portOnly);
    const auto text = p.toText();
    CHECK (text.find ("outputport=1|M-VAVE FM-1 MIDI 1") != std::string::npos && text.find ("outputport=12|Synth") != std::string::npos);
    const auto back = lynx::prefs::MidiPrefs::fromText (text);
    CHECK (back.output ("M-VAVE FM-1 MIDI 1").port == 1 && back.output ("M-VAVE FM-1 MIDI 1").masterSync);
    CHECK (back.output ("Synth").port == 12 && ! back.output ("Synth").masterSync);
    CHECK (back == p);
    // 0.1.29's file (no port line): none, as before
    const auto old = lynx::prefs::MidiPrefs::fromText ("velocity=linear\noutput=1|clock|0|0|M-VAVE FM-1 MIDI 1\n");
    CHECK (old.output ("M-VAVE FM-1 MIDI 1").port == -1 && old.output ("M-VAVE FM-1 MIDI 1").masterSync);
    CHECK (lynx::prefs::MidiOutputPrefs {}.port == -1);
}

TEST (midiout190jl_an_output_takes_its_inputs_port_as_fl)
{
    // FL (REA midiout190jl, FLEngine 0x117cdb0 / 0xfd9430): an output with no port of its own takes the port of the input
    // that is the same device; its own port ≥ 0 wins
    lynx::prefs::MidiPrefs p;
    lynx::prefs::MidiInputPrefs fm1;
    fm1.name = "M-VAVE FM-1 MIDI 1"; // (Linux: one name for both directions, the Discord report's setup)
    fm1.port = 1;
    p.setInput (fm1);
    lynx::prefs::MidiInputPrefs keys;
    keys.name = "Keystation MIDI In";
    keys.port = 7;
    p.setInput (keys);
    CHECK (p.outputPort ("M-VAVE FM-1 MIDI 1") == 1 && p.inheritedOutputPort ("M-VAVE FM-1 MIDI 1") == 1);
    CHECK (p.outputPort ("Keystation MIDI Out") == 7);
    CHECK (p.outputPort ("Other synth") == -1);
    lynx::prefs::MidiOutputPrefs own;
    own.name = "M-VAVE FM-1 MIDI 1";
    own.port = 3;
    p.setOutput (own);
    CHECK (p.outputPort ("M-VAVE FM-1 MIDI 1") == 3 && p.inheritedOutputPort ("M-VAVE FM-1 MIDI 1") == 1);
    own.port = -1; // (back to none: the input's port again, as FL's saved value only wins when it is ≥ 0)
    p.setOutput (own);
    CHECK (p.outputPort ("M-VAVE FM-1 MIDI 1") == 1);
}
