// Round 190ee: FL Gross Beat's MIDI control of the slots and Smooth points, Pitcher's MIDI port, Edison's region
// trigger notes in the WAV (FL facts: /mnt/e/lynx-work/fled190ee/FL-FACTS.md; REA of Gross Beat_x64.dll and
// Edison_x64.dll, REA_rodada16 / 30 for Pitcher).

namespace fled190ee
{
// a 4-beat-bar sine through an effect, the song moving (FL's "playing"), 120 BPM, blocks of 512
inline std::vector<float> render (const lynx::EffectSlot& slot, const std::function<void (lynx::Effect&, int)>& beforeBlock, int blocks = 120)
{
    auto fx = lynx::makeEffect (slot);
    std::vector<float> out;
    if (fx == nullptr)
        return out;
    fx->prepare (44100.0);
    std::vector<float> l (512), r (512);
    const double bpm = 120.0, ticksPerSample = bpm / 60.0 * lynx::ppq / 44100.0;
    double ticks = 0.0;
    std::int64_t n = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i, ++n)
            l[(std::size_t) i] = r[(std::size_t) i] = 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (double) n / 44100.0)
                                                      + 0.2f * (float) std::sin (2.0 * 3.141592653589793 * 1250.0 * (double) n / 44100.0);
        fx->setSongPosition (ticks);
        fx->setPlaying (true);
        if (beforeBlock)
            beforeBlock (*fx, b);
        fx->process (l.data(), r.data(), 512, slot.params, bpm);
        out.insert (out.end(), l.begin(), l.end());
        ticks += 512.0 * ticksPerSample;
    }
    return out;
}

inline double diff (const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0.0;
    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        d = std::max (d, (double) std::abs (a[i] - b[i]));
    return a.size() == b.size() ? d : 1.0e9;
}

// FL's Pitcher state: "SSLF", size, "1RTP", 33 raw parameters, the 12-byte note mask, then parameter 33 (MIDI port)
inline std::vector<std::uint8_t> pitcherState (float port)
{
    std::vector<std::uint8_t> b { 'S', 'S', 'L', 'F', 0x98, 0, 0, 0, '1', 'R', 'T', 'P' };
    float p[33] {};
    p[0] = 1.0f;
    p[25] = 1.0f; // MIDI
    p[32] = 2.0f;
    for (const auto v : p)
    {
        const auto* x = reinterpret_cast<const std::uint8_t*> (&v);
        b.insert (b.end(), x, x + 4);
    }
    for (int k = 0; k < 12; ++k)
        b.push_back (1);
    const auto* x = reinterpret_cast<const std::uint8_t*> (&port);
    b.insert (b.end(), x, x + 4);
    return b;
}
} // namespace fled190ee

// ---- Gross Beat: "Trigger slots from MIDI notes" in FL's state (0x83f890 load / save, 0x83fcf0 constructor) ---------
TEST (gb190ee_midi_trigger_is_saved_in_version_9)
{
    using namespace lynx::grossbeat;
    // FL's bare instance: off, written as FL 2026 writes (version 9 + the byte)
    const auto bare = emptyState();
    CHECK (! bare.midiTrigger && bare.version == 9);
    const auto bytes = writeState (bare);
    CHECK (bytes.size() > 4 && bytes[0] == 3 && bytes[1] == 0 && bytes[2] == 9 && bytes[3] == 0 && bytes.back() == 0);
    const auto back = parseState (bytes.data(), bytes.size());
    CHECK (back.has_value() && ! back->midiTrigger && writeState (*back) == bytes);
    // on in a version-9 state
    auto on = bare;
    on.midiTrigger = true;
    const auto onBytes = writeState (on);
    CHECK (onBytes.back() == 1 && onBytes.size() == bytes.size());
    CHECK (parseState (onBytes.data(), onBytes.size())->midiTrigger);
    // every older state reads as on (FL sets the flag before the version test), and keeps its version-8 bytes
    const auto factory = factoryState();
    CHECK (factory.midiTrigger && factory.version == 8);
    const auto v8 = writeState (factory);
    CHECK (v8[2] == 8 && v8[3] == 0);
    const auto v8back = parseState (v8.data(), v8.size());
    CHECK (v8back.has_value() && v8back->midiTrigger && writeState (*v8back) == v8);
    // a version-8 state with the switch turned off is written as version 9
    auto off = *v8back;
    off.midiTrigger = false;
    const auto offBytes = writeState (off);
    CHECK (offBytes[2] == 9 && offBytes.size() == v8.size() + 1 && offBytes.back() == 0);
    CHECK (! parseState (offBytes.data(), offBytes.size())->midiTrigger);
    // a new LYNX Gross Beat (LYNX's bank in the role of FL's Default preset) is on
    CHECK (editableState (factoryContainer()).midiTrigger);
}

TEST (gb190ee_keys_select_slots)
{
    using namespace lynx::grossbeat;
    CHECK (slotForKey (60) == std::make_pair (0, 0) && slotForKey (95) == std::make_pair (0, 35));
    CHECK (slotForKey (24) == std::make_pair (1, 0) && slotForKey (59) == std::make_pair (1, 35));
    CHECK (slotForKey (23).first < 0 && slotForKey (96).first < 0 && slotForKey (0).first < 0 && slotForKey (127).first < 0);
}

// FL's MIDI handler 0x848330: Key-held slots stack up and go back on release
TEST (gb190ee_key_held_goes_back_on_release)
{
    using namespace lynx::grossbeat;
    MidiSlots midi;
    std::array<int, 2> current { 3, 0 };
    auto held = [] (int kind, int slot) { return kind == 0 && (slot == 5 || slot == 6); };
    auto pick = [&] (bool on, int key, int channel = 0) {
        const auto r = midi.note (on, channel, key, current, held);
        if (r.first >= 0)
            current[(std::size_t) r.first] = r.second;
        return r;
    };
    CHECK (pick (true, 65) == std::make_pair (0, 5));  // Key-held slot 5 (the stack keeps slot 3 under it)
    CHECK (pick (true, 66) == std::make_pair (0, 6));  // and 6 on top
    CHECK (pick (false, 66) == std::make_pair (0, 5)); // release 6: back to 5
    CHECK (pick (false, 65) == std::make_pair (0, 3)); // release 5: back to 3, the stack is empty again
    CHECK (pick (false, 65).first < 0);                // a release never pressed is ignored
    CHECK (pick (true, 62) == std::make_pair (0, 2));  // an ordinary slot: selected...
    CHECK (pick (false, 62).first < 0);                // ...and stays on release
    CHECK (pick (true, 30) == std::make_pair (1, 6));  // a volume key
    // releasing 5 first while 6 is held: 5 leaves the stack, 6 stays selected (the top is re-selected)
    current = { 3, 0 };
    pick (true, 65);
    pick (true, 66);
    CHECK (pick (false, 65) == std::make_pair (0, 6));
    CHECK (pick (false, 66) == std::make_pair (0, 3));
    // the same key on another MIDI channel is another held note
    pick (true, 65, 1);
    CHECK (pick (false, 65, 2).first < 0);
    CHECK (pick (false, 65, 1) == std::make_pair (0, 3));
}

// a note selects a slot exactly as the slot's parameter does; with the option off it does nothing
TEST (gb190ee_midi_note_selects_like_the_parameter)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::grossBeat);
    const auto none = fled190ee::render (slot, {});
    auto byParam = slot;
    byParam.params[0] = grossbeat::paramFromRaw (0, 7); // time slot 8 of LYNX's bank (half speed)
    const auto param = fled190ee::render (byParam, {});
    const auto midi = fled190ee::render (slot, [] (Effect& fx, int b) {
        if (b == 0)
            fx.midiNote (60 + 7, 100, 0);
    });
    CHECK (fled190ee::diff (param, none) > 1.0e-3); // (the slot is heard)
    CHECK (fled190ee::diff (midi, param) == 0.0);
    // a volume key
    auto byVolume = slot;
    byVolume.params[1] = grossbeat::paramFromRaw (1, 4);
    const auto volume = fled190ee::render (byVolume, {});
    const auto volumeMidi = fled190ee::render (slot, [] (Effect& fx, int b) {
        if (b == 0)
            fx.midiNote (24 + 4, 90, 3);
    });
    CHECK (fled190ee::diff (volume, none) > 1.0e-3 && fled190ee::diff (volumeMidi, volume) == 0.0);
    // "Trigger slots from MIDI notes" off: notes change nothing
    auto st = grossbeat::editableState (slot.state);
    st.midiTrigger = false;
    auto off = slot;
    off.state = grossbeat::withState (slot.state, st);
    const auto offPlain = fled190ee::render (off, {});
    const auto offMidi = fled190ee::render (off, [] (Effect& fx, int b) {
        if (b == 0)
            fx.midiNote (60 + 7, 100, 0);
    });
    CHECK (fled190ee::diff (offMidi, offPlain) == 0.0 && fled190ee::diff (offPlain, none) == 0.0);
}

// Smooth (type 10): the plugin's own preparation (0x784d60) and player (0x789a70) = FLEngine's law (flcurve)
TEST (gb190ee_smooth_points_are_curved)
{
    using namespace lynx;
    grossbeat::Slot slot;
    grossbeat::setPoints (slot, { { 0.0, 1.0f, 0.0f, 2 }, { 2.0, 0.0f, 0.0f, 10 }, { 4.0, 1.0f, 0.0f, 10 } });
    // 1 → 0 over beats 0…2: no Smooth neighbour before, a Smooth one after whose end level equals this start, so both
    // end slopes are flat: smoothstep, 3x² − 2x³
    const auto quarter = grossbeat::levelAt (slot, 0.5);
    CHECK (std::abs ((double) quarter - (1.0 - 0.15625)) < 1.0e-5);
    // the same as the shared law with its neighbours
    flcurve::SmoothSide before, after;
    after.smooth = true;
    after.dx = 2.0;
    after.y = 1.0;
    const auto seg = flcurve::prepareSmooth (0.0f, 2.0, 1.0, 0.0, before, after);
    for (const auto beat : { 0.3, 0.9, 1.5, 1.9 })
        CHECK (std::abs ((double) grossbeat::levelAt (slot, beat) - (1.0 - flcurve::evaluate (seg, beat / 2.0))) < 1.0e-5);
    // tension pulls it back to the straight line (|t| = 1: straight)
    grossbeat::Slot straight;
    grossbeat::setPoints (straight, { { 0.0, 1.0f, 0.0f, 2 }, { 2.0, 0.0f, 1.0f, 10 }, { 4.0, 1.0f, 0.0f, 10 } });
    CHECK (std::abs ((double) grossbeat::levelAt (straight, 0.5) - 0.75) < 1.0e-5);
    // a flat Smooth segment is straight (and stays flat)
    grossbeat::Slot flat;
    grossbeat::setPoints (flat, { { 0.0, 0.5f, 0.0f, 2 }, { 4.0, 0.5f, 0.3f, 10 } });
    CHECK (std::abs ((double) grossbeat::levelAt (flat, 2.0) - 0.5) < 1.0e-6);
    // the bank LYNX ships has no Smooth point, so its sound is untouched by this round
    const auto bank = grossbeat::factoryState();
    bool anySmooth = false;
    for (const auto& s : bank.slots)
        for (const auto& p : s.points)
            anySmooth = anySmooth || p.type == 10;
    CHECK (! anySmooth);
}

// FL's demo projects: no selected slot uses Smooth, so imported projects sound as before (FL Data present)
TEST (gb190ee_fl_demo_selected_slots_have_no_smooth)
{
    if (realFlpDir.empty())
        return;
    int states = 0, smoothSelected = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path (realFlpDir) / "Demo projects"))
        if (entry.path().extension() == ".flp")
        {
            std::ifstream in (entry.path(), std::ios::binary);
            const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
            try
            {
                const auto project = flp::parseProject (file);
                for (const auto& insert : project.mixer)
                    for (const auto& s : insert.slots)
                        if (s.plugin == "Gross Beat")
                            if (const auto st = lynx::grossbeat::parseState (s.state.data(), s.state.size()))
                            {
                                ++states;
                                CHECK (st->midiTrigger); // (version 8: on)
                                for (const auto k : { st->params[0], lynx::grossbeat::slotsPerKind + st->params[1] })
                                    for (const auto& p : st->slots[(std::size_t) k].points)
                                        smoothSelected += p.type == 10 ? 1 : 0;
                            }
            }
            catch (...)
            {
            }
        }
    CHECK (states > 0 && smoothSelected == 0);
}

// ---- Pitcher: FL's MIDI port (parameter 33) ------------------------------------------------------------------------
TEST (pitcher190ee_midi_port_parameters)
{
    using namespace lynx;
    const auto& info = effectInfo (EffectKind::pitchCorrect);
    CHECK (info.params.size() == 44 && info.params[42].name == std::string ("MIDI port") && info.params[43].name == std::string ("Port routing"));
    auto slot = makeEffectSlot (EffectKind::pitchCorrect);
    CHECK (slot.params[42] == 0.0f && slot.params[43] == 1.0f && effectMidiInPort (slot) == -1);
    slot.params[42] = 0.00638f; // the demo "Bien Duro": int(256 v) − 1 = 0
    CHECK (effectMidiInPort (slot) == 0);
    slot.params[42] = 0.0112f; // = 1
    CHECK (effectMidiInPort (slot) == 1);
    for (const auto port : { 0, 1, 7, 31, 200, 255 })
    {
        setEffectMidiInPort (slot, port);
        CHECK (effectMidiInPort (slot) == port);
    }
    setEffectMidiInPort (slot, -1);
    CHECK (effectMidiInPort (slot) == -1);
    // a project saved before (42 parameters) keeps the Channel Rack's held notes, listens to no port
    auto old = makeEffectSlot (EffectKind::pitchCorrect);
    old.params[42] = 0.0112f;
    upgradeLoadedEffectParams (old, 42);
    CHECK (old.params[43] == 0.0f && effectMidiInPort (old) == -1);
    auto saved = makeEffectSlot (EffectKind::pitchCorrect);
    upgradeLoadedEffectParams (saved, savedEffectParamCount (saved));
    CHECK (saved.params[43] == 1.0f);
    // giving an older Pitcher a port moves it to FL's routing
    setEffectMidiInPort (old, 3);
    CHECK (effectMidiInPort (old) == 3 && old.params[43] == 1.0f);
    // FL import: parameter 33 after the note mask
    const auto fl = effectFromFl ("Pitcher", fled190ee::pitcherState (0.0112f));
    CHECK (fl.has_value() && effectMidiInPort (*fl) == 1 && fl->params[34] == 1.0f);
    const auto noPort = effectFromFl ("Pitcher", fled190ee::pitcherState (0.0f));
    CHECK (noPort.has_value() && effectMidiInPort (*noPort) == -1);
}

TEST (pitcher190ee_notes_come_from_the_port)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::pitchCorrect);
    slot.params[34] = 1.0f; // MIDI mode
    // a voice between A4 and A#4: MIDI mode pulls it to the held note
    auto run = [] (const EffectSlot& s, const std::function<void (Effect&)>& feed) {
        auto fx = makeEffect (s);
        fx->prepare (44100.0);
        std::vector<float> out, l (512), r (512);
        std::int64_t n = 0;
        for (int b = 0; b < 60; ++b)
        {
            for (int i = 0; i < 512; ++i, ++n)
                l[(std::size_t) i] = r[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 452.0 * (double) n / 44100.0);
            feed (*fx);
            fx->process (l.data(), r.data(), 512, s.params, 120.0);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    static std::array<NoteMask, 4> held {};
    held = {};
    held[2][1] = 1ull << (69 - 64);
    const auto nothing = run (slot, [] (Effect&) {});
    const auto port = run (slot, [] (Effect& fx) { fx.midiNote (69, 100, 0); });
    const auto rackOnly = run (slot, [] (Effect& fx) { fx.setHeldNotes (held.data(), (int) held.size()); });
    CHECK (fled190ee::diff (port, nothing) > 1.0e-3); // the port's note is heard
    CHECK (fled190ee::diff (rackOnly, nothing) == 0.0); // the rack's held notes are not (FL: only the port)
    // an older project (Port routing off): the rack's held notes, as round 173d — the same correction
    auto old = slot;
    old.params[43] = 0.0f;
    const auto legacy = run (old, [] (Effect& fx) { fx.setHeldNotes (held.data(), (int) held.size()); });
    const auto legacyPort = run (old, [] (Effect& fx) { fx.midiNote (69, 100, 0); });
    CHECK (fled190ee::diff (legacy, port) == 0.0);
    CHECK (fled190ee::diff (legacyPort, nothing) == 0.0);
}

// ---- the MIDI Out channels reach Gross Beat and Pitcher slots on their port --------------------------------------------
TEST (rack190ee_midi_out_routes_to_effect_ports)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer.resize (4);
    song.channels.resize (1);
    song.channels[0] = {};
    song.channels[0].instrument = InstrumentKind::midiOut;
    midictl::MidiOutSettings s;
    s.port = 4;
    s.channel = 2;
    midictl::writeMidiOut (song.channels[0].pluginState, s);
    auto gb = makeEffectSlot (EffectKind::grossBeat);
    auto pitcher = makeEffectSlot (EffectKind::pitchCorrect);
    auto legacy = pitcher;
    legacy.params[42] = 0.02f; // port 4, but saved before the port routing
    legacy.params[43] = 0.0f;
    song.mixer[2].effects = { gb, pitcher, legacy };
    {
        auto state = RackState::build (song, nullptr, 48000.0);
        CHECK (! state->hasEffectMidiRoutes); // no port set yet
    }
    setEffectMidiInPort (song.mixer[2].effects[0], 4);
    setEffectMidiInPort (song.mixer[2].effects[1], 4);
    {
        auto state = RackState::build (song, nullptr, 48000.0);
        CHECK (state->hasEffectMidiRoutes && state->effectMidiRoutes[0].size() == 2);
        if (state->effectMidiRoutes[0].size() == 2)
        {
            CHECK (state->effectMidiRoutes[0][0].track == 2 && state->effectMidiRoutes[0][0].slot == 0 && state->effectMidiRoutes[0][0].port == 4);
            CHECK (state->effectMidiRoutes[0][1].slot == 1 && state->effectMidiRoutes[0][1].midiChannel == 2);
        }
    }
    setEffectMidiInPort (song.mixer[2].effects[0], 5); // another port: not this MIDI Out's
    {
        auto state = RackState::build (song, nullptr, 48000.0);
        CHECK (state->effectMidiRoutes[0].size() == 1);
    }
}

// ---- Edison: region trigger notes travel in the WAV ----------------------------------------------------------------------
TEST (edison190ee_trigger_notes_round_trip)
{
    using namespace lynx::audioedit;
    Document doc;
    doc.audio.sampleRate = 44100.0;
    doc.audio.channels = { std::vector<float> (4000, 0.1f) };
    doc.markers.push_back ({ 0, 1000, "Kick", "", 0.0, 36 });
    doc.markers.push_back ({ 1000, 2000, "Snare", "", 0.0, -1 });
    doc.markers.push_back ({ 2500, 2500, "Hit", "", 0.0, 60 });
    const auto bytes = encodeWav (doc, WavFormat::int16);
    Document back;
    CHECK (decodeWav (bytes.data(), bytes.size(), back));
    CHECK (back.markers.size() == 3);
    if (back.markers.size() == 3)
        CHECK (back.markers[0].keyNum == 36 && back.markers[1].keyNum == -1 && back.markers[2].keyNum == 60 && back.markers[0].name == "Kick");
    // without trigger notes the file is exactly what it was (no trigger chunk); with them, FL's "tlst" (round 190ez:
    // count + one 24-byte entry per region with a note)
    auto plain = doc;
    for (auto& m : plain.markers)
        m.keyNum = -1;
    const auto plainBytes = encodeWav (plain, WavFormat::int16);
    const std::string text (plainBytes.begin(), plainBytes.end());
    CHECK (text.find ("tlst") == std::string::npos && text.find ("lxkn") == std::string::npos && bytes.size() == plainBytes.size() + 8 + 4 + 2 * 24);
}

// Regions › Assign all to (0x86e370)
TEST (edison190ee_assign_all_to_layouts)
{
    using namespace lynx::audioedit;
    Document doc;
    doc.audio.channels = { std::vector<float> (100, 0.0f) };
    for (int i = 0; i < 6; ++i)
        doc.markers.push_back ({ i * 10, i * 10 + 5, "", "", 0.0, -1 });
    auto keys = [&] {
        std::vector<int> k;
        for (const auto& m : doc.markers)
            k.push_back (m.keyNum);
        return k;
    };
    assignTriggerNotes (doc, NoteLayout::wholeKeyboard);
    CHECK ((keys() == std::vector<int> { 60, 61, 62, 63, 64, 65 }));
    assignTriggerNotes (doc, NoteLayout::whiteNotes);
    CHECK ((keys() == std::vector<int> { 60, 62, 64, 65, 67, 69 }));
    assignTriggerNotes (doc, NoteLayout::blackNotes);
    CHECK ((keys() == std::vector<int> { 61, 63, 66, 68, 70, 73 }));
    assignTriggerNotes (doc, NoteLayout::c5);
    CHECK ((keys() == std::vector<int> (6, 60)));
    assignTriggerNotes (doc, NoteLayout::none);
    CHECK ((keys() == std::vector<int> (6, -1)));
    // many markers start lower (FL: 60 − 12·((n − 54) div 12) above 65), never above 124
    Document many = doc;
    many.markers.assign (90, Marker {});
    assignTriggerNotes (many, NoteLayout::wholeKeyboard);
    CHECK (many.markers[0].keyNum == 60 - 12 * ((90 - 54) / 12) && many.markers[89].keyNum == std::min (24 + 89, 124));
    Document lots = doc;
    lots.markers.assign (200, Marker {});
    assignTriggerNotes (lots, NoteLayout::wholeKeyboard);
    CHECK (lots.markers[0].keyNum == 0 && lots.markers[199].keyNum == 124);
}
