// Round 167i (issue #17): FL's MIDI Out — its state in every version FL wrote, the messages its knobs, patch and reset
// send, FL's presets, "Import MIDI data" with MIDI Out channels (docs/analise/REA_rodada167i.md).

#include "core/MidiOut.h"
#include "core/MidiImport.h"

namespace mo167
{
using lynx::midiout::pack;

lynx::midiout::Control cc (int number, int min = 0, int max = 127)
{
    lynx::midiout::Control c;
    c.number = number;
    c.active = true;
    c.min = min;
    c.max = max;
    return c;
}

std::vector<std::uint32_t> messages (const lynx::midiout::State& s, int i, int value, int& held)
{
    std::array<std::uint32_t, 8> out {};
    const auto n = lynx::midiout::controlMessages (s, i, value, held, out.data());
    return { out.begin(), out.begin() + n };
}

std::vector<std::uint32_t> take (lynx::Instrument& in)
{
    std::vector<std::uint32_t> out (lynx::midiout::maxResetMessages + 8);
    const auto n = in.takeMidiMessages (out.data(), (int) out.size());
    out.resize ((std::size_t) std::max (0, n));
    return out;
}

bool has (const std::vector<std::uint32_t>& m, std::uint32_t x) { return std::find (m.begin(), m.end(), x) != m.end(); }

lynx::ChannelDef channelWith (const lynx::midiout::State& s)
{
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::midiOut;
    c.pluginState = lynx::midiout::write (s);
    return c;
}
} // namespace mo167

TEST (midi_out_state_round_trips_and_reads_old_layouts)
{
    using namespace lynx::midiout;
    State s;
    s.channel = 3;
    s.bankMsb = 2;
    s.bankLsb = -1;
    s.page = 1;
    s.port = 7;
    s.sendPanVolPitch = false;
    s.mapNoteColour = true;
    s.program = 42;
    s.values[10] = 99;
    s.controls[10] = mo167::cc (74);
    s.controls[10].name = "Cutoff";
    s.controls[10].shortName = "Cut";
    s.controls[11] = mo167::cc (300, 0, 16383);
    s.controls[11].type = Type::nrpn;
    s.controls[11].active = false; // (FL: bit 31 of the number)
    s.controls[11].channel = 9;
    s.pageNames[1] = "Filter";
    s.resetFlags = resetPan | resetCutNotes;
    const auto bytes = write (s);
    const auto back = parse (bytes);
    CHECK (back.has_value() && *back == s);
    // FL's writer: version 7, the header, 73 values, only the non-default controls, the page names, inverted options
    std::int32_t version = 0, number = 0;
    std::memcpy (&version, bytes.data(), 4);
    CHECK (version == 7 && bytes[28] == 0 && bytes[29] == 1);
    const auto records = 30 + 73 * 4;
    CHECK (bytes[(std::size_t) records] == 10);
    std::memcpy (&number, bytes.data() + records + 1 + 24 + 7 + 4 + 1, 4); // the second record's number
    CHECK (bytes[(std::size_t) records + 1 + 24 + 7 + 4] == 11 && (std::uint32_t) number == (300u | 0x80000000u));

    // version 1 (20 header bytes, 16-byte records, no option bytes, no reset word): FL's "Default.fst"
    std::vector<std::uint8_t> v1;
    auto i32 = [&] (std::int32_t v) {
        for (int k = 0; k < 4; ++k)
            v1.push_back ((std::uint8_t) ((std::uint32_t) v >> (8 * k)));
    };
    i32 (1), i32 (9), i32 (-1), i32 (-1), i32 (-1), i32 (0); // version, channel 10, reserved, bank, bank, page
    i32 (0); // program
    for (int k = 0; k < 72; ++k)
        i32 (k == 0 ? 64 : 0);
    v1.push_back (0);
    i32 (1), i32 (0), i32 (0), i32 (127); // CC 1, active
    v1.push_back (3), v1.insert (v1.end(), { 'M', 'o', 'd' }), v1.push_back (1), v1.push_back ('M');
    v1.push_back (0xff);
    const auto old = parse (v1);
    CHECK (old.has_value() && old->channel == 9 && old->port == 0 && old->sendPanVolPitch && ! old->mapNoteColour);
    CHECK (old->values[0] == 64 && old->controls[0].sends() && old->controls[0].number == 1 && old->controls[0].name == "Mod");
    CHECK (old->controls[0].noteOff == 0 && old->controls[0].channel == -1 && old->pageNames[0] == "Page 1");
    CHECK (old->resetFlags == resetAll); // (before version 6 the options follow "Send pan, volume and pitch")
    CHECK (! parse ({ 1, 2, 3 }).has_value() && stateOf ({}) == State {});
}

TEST (midi_out_reads_every_fl_preset)
{
    using namespace lynx::midiout;
    const auto dir = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "MIDI Out";
    if (realFlpDir.empty() || ! std::filesystem::is_directory (dir))
        return;
    int read = 0, named = 0;
    for (const auto& e : std::filesystem::directory_iterator (dir))
    {
        if (e.path().extension() != ".fst")
            continue;
        const auto file = readFile (e.path());
        const auto blob = stateFromFst ({ file.begin(), file.end() });
        const auto s = parse (blob);
        CHECK (s.has_value());
        if (! s)
            continue;
        ++read;
        named += std::any_of (s->controls.begin(), s->controls.end(), [] (const Control& c) { return c.assigned() && ! c.name.empty(); });
        // LYNX's writer (FL's version 7) keeps everything FL's preset says
        auto expected = *s;
        expected.version = 7;
        CHECK (parse (write (*s)) == expected);
        // and its .fst reads back the same
        CHECK (parse (stateFromFst (writeFst (*s))) == expected);
    }
    CHECK (read >= 40 && named >= 30);
}

TEST (midi_out_import_track_state_is_fls_miditrack_fst)
{
    using namespace lynx::midiout;
    const auto s = importTrackState();
    CHECK (importParamForCc (1) == 1 && importParamForCc (93) == 6 && importParamForCc (64) == 10 && importParamForCc (69) == 15);
    CHECK (importParamForCc (7) == -1 && importParamForCc (10) == -1 && importParamForCc (74) == -1);
    for (int cc : { 1, 2, 4, 5, 91, 93, 64, 65, 66, 67, 68, 69 })
    {
        const auto& c = s.controls[(std::size_t) (importParamForCc (cc) - 1)];
        CHECK (c.number == cc && ! c.active && ! c.name.empty());
    }
    const auto file = std::filesystem::path (realFlpDir) / "System" / "MIDITrack.fst";
    if (realFlpDir.empty() || ! std::filesystem::exists (file))
        return;
    const auto bytes = readFile (file);
    const auto fl = parse (stateFromFst ({ bytes.begin(), bytes.end() }));
    CHECK (fl.has_value());
    if (! fl)
        return;
    CHECK (fl->program == s.program && fl->pageNames == s.pageNames);
    for (int i = 0; i < controlCount; ++i)
    {
        const auto& a = fl->controls[(std::size_t) i];
        const auto& b = s.controls[(std::size_t) i];
        CHECK (a.number == b.number && a.active == b.active && a.name == b.name && a.shortName == b.shortName && a.type == b.type);
    }
}

TEST (midi_out_control_messages_are_fls)
{
    using namespace lynx::midiout;
    State s;
    s.channel = 2;
    int held = -1;
    s.controls[0] = mo167::cc (74);
    CHECK (mo167::messages (s, 0, 100, held) == std::vector<std::uint32_t> { pack (0xB2, 74, 100) });
    CHECK (mo167::messages (s, 0, 300, held) == std::vector<std::uint32_t> { pack (0xB2, 74, 127) }); // (clamped to the range)
    s.controls[0].active = false;
    CHECK (mo167::messages (s, 0, 10, held).empty());
    // 14-bit CC: MSB on the number, LSB on number + 32
    s.controls[1] = mo167::cc (1, 0, 16383);
    CHECK (mo167::messages (s, 1, 8193, held) == (std::vector<std::uint32_t> { pack (0xB2, 1, 64), pack (0xB2, 33, 1) }));
    // RPN / NRPN with the channel override
    s.controls[2] = mo167::cc (0x0102);
    s.controls[2].type = Type::rpn;
    s.controls[2].channel = 15;
    CHECK (mo167::messages (s, 2, 5, held) == (std::vector<std::uint32_t> { pack (0xBF, 101, 2), pack (0xBF, 100, 2), pack (0xBF, 6, 5) }));
    s.controls[3] = mo167::cc (7, 0, 16383);
    s.controls[3].type = Type::nrpn;
    CHECK (mo167::messages (s, 3, 129, held)
           == (std::vector<std::uint32_t> { pack (0xB2, 99, 0), pack (0xB2, 98, 7), pack (0xB2, 6, 1), pack (0xB2, 38, 1) }));
    s.controls[4] = mo167::cc (0);
    s.controls[4].type = Type::aftertouch;
    CHECK (mo167::messages (s, 4, 33, held) == std::vector<std::uint32_t> { pack (0xD2, 33) });
    // Note: the value is the key, the number the velocity; the note-off value only ends it
    s.controls[5] = mo167::cc (90);
    s.controls[5].type = Type::note;
    s.controls[5].noteOff = 0;
    held = -1;
    CHECK (mo167::messages (s, 5, 60, held) == std::vector<std::uint32_t> { pack (0x92, 60, 90) } && held == 60);
    CHECK (mo167::messages (s, 5, 62, held) == (std::vector<std::uint32_t> { pack (0x82, 60, 90), pack (0x92, 62, 90) }));
    CHECK (mo167::messages (s, 5, 0, held) == std::vector<std::uint32_t> { pack (0x82, 62, 90) } && held == -1);
    CHECK (label (s.controls[0]) == "CC 74" && label (s.controls[4]) == "Aftertouch" && label (Control {}).empty());
    CHECK (description (s.controls[2]) == "RPN 258 Chan #16");
}

TEST (midi_out_reset_sends_fls_order)
{
    using namespace lynx::midiout;
    State s;
    s.channel = 0;
    s.bankMsb = 1;
    s.bankLsb = 2;
    s.program = 5;
    s.controls[0] = mo167::cc (74);
    s.values[0] = 20;
    s.controls[1] = mo167::cc (71);
    s.controls[1].active = false;
    std::array<int, controlCount> held;
    held.fill (-1);
    std::vector<Message> out (maxResetMessages);
    auto n = resetMessages (s, 64, 100, 8192, held, out.data());
    out.resize ((std::size_t) n);
    CHECK ((out
            == std::vector<Message> { pack (0xB0, 101, 0), pack (0xB0, 100, 0), pack (0xB0, 6, 12), pack (0xB0, 0, 1), pack (0xB0, 32, 2),
                                      pack (0xB0, 10, 64), pack (0xB0, 7, 100), pack (0xE0, 0, 64), pack (0xC0, 4), pack (0xB0, 74, 20) }));
    // the options off: only the bank and the active controls
    s.resetFlags = 0;
    out.assign (maxResetMessages, 0);
    n = resetMessages (s, 64, 100, 8192, held, out.data());
    out.resize ((std::size_t) n);
    CHECK ((out == std::vector<Message> { pack (0xB0, 0, 1), pack (0xB0, 32, 2), pack (0xB0, 74, 20) }));
    CHECK (resetAllControllers (s) == pack (0xB0, 121, 0));
    CHECK (panValue (0.0f) == 64 && volumeValue (0.78125f) == 100 && bendValue (0.0f) == 8192 && bendValue (1200.0f) == 16383);
}

TEST (midi_out_instrument_sends_knobs_automation_and_reactivates)
{
    using namespace lynx::midiout;
    State s;
    s.channel = 1;
    s.resetFlags = 0;
    s.controls[0] = mo167::cc (74);
    s.controls[1] = mo167::cc (71);
    s.controls[1].active = false;
    auto in = makeInstrument();
    auto channel = mo167::channelWith (s);
    in->configure (channel, 48000.0);
    const auto first = mo167::take (*in); // (a new MIDI Out sends its setup: CC 74 = 0)
    CHECK (mo167::has (first, pack (0xB1, 74, 0)) && ! mo167::has (first, pack (0xB1, 71, 0)));
    CHECK (mo167::take (*in).empty());

    // the editor turns the knob: the song's value goes out
    s.values[0] = 50;
    channel = mo167::channelWith (s);
    in->configure (channel, 48000.0);
    CHECK (mo167::take (*in) == std::vector<std::uint32_t> { pack (0xB1, 74, 50) });

    // automation of the deactivated control (parameter 2) switches it on (FL)
    in->setParameter (2, 1.0f);
    CHECK (mo167::take (*in) == std::vector<std::uint32_t> { pack (0xB1, 71, 127) });
    // the patch parameter
    s.resetFlags = resetProgram;
    channel = mo167::channelWith (s);
    in->configure (channel, 48000.0);
    mo167::take (*in);
    in->setParameter (programParam, normalFromProgram (10));
    CHECK (mo167::take (*in) == std::vector<std::uint32_t> { pack (0xC1, 9) });

    // the Reset button: CC 121 first, then the reset
    requestReset (*in, true);
    const auto reset = mo167::take (*in);
    CHECK (! reset.empty() && reset[0] == pack (0xB1, 121, 0) && mo167::has (reset, pack (0xB1, 74, 50)));

    // Song's automation value: read and set like FL's parameters (a value on a deactivated control activates it)
    lynx::Song song;
    s.controls[2] = mo167::cc (10, 0, 100);
    s.controls[2].active = false;
    song.channels.push_back (mo167::channelWith (s));
    const lynx::AutomationTarget t { lynx::AutomationTargetKind::channelParam, 0, 0, 3 };
    CHECK (song.targetValue (t) == 0.0f && song.setTargetValue (t, 0.5f));
    const auto after = stateOf (song.channels[0].pluginState);
    CHECK (after.values[2] == 50 && after.controls[2].active && std::abs (song.targetValue (t) - 0.5f) < 1e-6f);
}

TEST (midi_import_with_midi_out_channels_fills_fls_lanes)
{
    using namespace lynx::midiimport;
    ParsedMidi midi;
    midi.trackNames = { "Lead" };
    auto ev = [&] (std::int64_t tick, std::uint8_t status, std::uint8_t d1, std::uint8_t d2) { midi.events.push_back ({ 0, tick, status, d1, d2 }); };
    ev (0, 0xC3, 4, 0);       // program 5
    ev (0, 0xB3, 7, 100);     // volume
    ev (0, 0xB3, 10, 64);     // pan
    ev (0, 0xB3, 1, 90);      // mod wheel → knob 1
    ev (0, 0xB3, 64, 127);    // damper → knob 10
    ev (0, 0xB3, 74, 50);     // (dropped, as FL)
    ev (0, 0xE3, 0, 0x50);    // pitch bend
    ev (0, 0x93, 60, 100);
    ev (96, 0x83, 60, 0);
    Options o;
    o.midiOut = true;
    lynx::Song song;
    std::string error;
    CHECK (importInto (song, midi, o, "MIDI", error));
    const auto c = (int) song.channels.size() - 1;
    CHECK (song.channels[(std::size_t) c].instrument == lynx::InstrumentKind::midiOut);
    const auto st = lynx::midiout::stateOf (song.channels[(std::size_t) c].pluginState);
    CHECK (st.channel == 3 && st.pageNames[1] == "Boolean controllers");
    const auto& lanes = song.patterns.back().events;
    auto lane = [&] (lynx::AutomationTargetKind kind, int param) -> const lynx::PatternEvents* {
        for (const auto& l : lanes)
            if (l.target.kind == kind && l.target.index == c && (kind != lynx::AutomationTargetKind::channelParam || l.target.param == param))
                return &l;
        return nullptr;
    };
    const auto* vol = lane (lynx::AutomationTargetKind::channelVolume, 0);
    const auto* pan = lane (lynx::AutomationTargetKind::channelPan, 0);
    const auto* mod = lane (lynx::AutomationTargetKind::channelParam, 1);
    const auto* damper = lane (lynx::AutomationTargetKind::channelParam, 10);
    const auto* program = lane (lynx::AutomationTargetKind::channelParam, lynx::midiout::programParam);
    const auto* bend = lane (lynx::AutomationTargetKind::channelParam, lynx::midictl::pitchBendBase + 3);
    CHECK (vol && pan && mod && damper && program && bend);
    if (vol && pan && mod && damper && program && bend)
    {
        CHECK (std::abs (vol->events[0].value - 10000.0f / 12800.0f) < 1e-6f && std::abs (pan->events[0].value - 6400.0f / 12800.0f) < 1e-6f);
        CHECK (std::abs (mod->events[0].value - 90.0f / 127.0f) < 1e-6f && damper->events[0].value == 1.0f);
        CHECK (lynx::midiout::programFromNormal (program->events[0].value) == 5);
        CHECK (std::abs (bend->events[0].value - (float) (0x50 << 7) / 16383.0f) < 1e-6f);
    }
    CHECK (lanes.size() == 6); // (CC 74 is not imported)
}
