// Round 133 (issue #3): MIDI CC / aftertouch / pitch bend as automatable "parameters" of hosted plugins (FL's
// wrapper), built-in controllers (Envelope / Formula), MIDI Out routing (docs/analise/REA_rodada133.md).

#include "core/MidiControls.h"
#include "core/FormulaController.h"

namespace feat133
{
// An instrument that records what the renderer asks of it.
struct Recorder final : lynx::Instrument
{
    std::vector<std::pair<int, float>> params;
    std::vector<std::pair<int, int>> midiNotes; // key, channel
    int plainNotes = 0;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override { ++plainNotes; }
    void noteOff (int) noexcept override {}
    void noteOnMidi (int key, float, int ch) noexcept override { midiNotes.push_back ({ key, ch }); }
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    void setParameter (int index, float value) noexcept override { params.push_back ({ index, value }); }
    void midiMessage (const std::uint8_t* b, int n) noexcept override { messages.emplace_back (b, b + n); }
    std::vector<std::vector<std::uint8_t>> messages;
};
} // namespace feat133

TEST (midi_controls_indices_names_and_messages)
{
    using namespace lynx::midictl;
    CHECK (isCc (0x1040) && ! isPressure (0x1040) && isMidiParam (0x1040));
    CHECK (isPressure (0x1080) && isPressure (0x108F) && isPitchBend (0x1090) && ! isMidiParam (0x10A0) && ! isMidiParam (0xFFF));
    CHECK (paramName (ccBase + 64) == "MIDI CC #64 (Damper pedal (sustain))");
    CHECK (paramName (ccBase + 3) == "MIDI CC #3");
    CHECK (paramName (pressureBase + 15) == "MIDI Channel 16 Aftertouch");
    CHECK (paramName (pitchBendBase) == "MIDI Channel 1 Pitch Bend");
    CHECK (quantise (ccBase + 1, 1.0f) == 127 && quantise (ccBase + 1, 0.5f) == 64 && quantise (ccBase, -3.0f) == 0);
    CHECK (quantise (pitchBendBase, 0.5f) == 8192 && quantise (pitchBendBase, 1.0f) == 16383);
    CHECK (std::abs (defaultValue (ccBase + 7) - 100.0f / 127.0f) < 1e-6f && defaultValue (pitchBendBase + 3) == 0.5f && defaultValue (ccBase + 64) == 0.0f);

    std::uint8_t m[3] {};
    CHECK (message (ccBase + 64, 127, 2, m) == 3 && m[0] == 0xB2 && m[1] == 64 && m[2] == 127);
    CHECK (message (pressureBase + 4, 90, 0, m) == 2 && m[0] == 0xD4 && m[1] == 90);
    CHECK (message (pitchBendBase + 1, 16383, 0, m) == 3 && m[0] == 0xE1 && m[1] == 127 && m[2] == 127);
    CHECK (message (12, 5, 0, m) == 0);
}

TEST (midi_controls_state_sends_each_change_once)
{
    using namespace lynx::midictl;
    State s;
    std::vector<std::vector<std::uint8_t>> out;
    auto flush = [&] { s.flush (0, [&] (const std::uint8_t* b, int n) { out.emplace_back (b, b + n); }); };

    CHECK (! s.set (5, 0.3f)); // a plugin parameter, not MIDI
    CHECK (s.value (ccBase + 11) == 1.0f); // untouched: the rest value
    CHECK (s.set (ccBase + 64, 1.0f) && s.set (ccBase + 1, 0.5f));
    flush();
    CHECK (out.size() == 2 && out[0][0] == 0xB0 && out[0][1] == 1 && out[0][2] == 64 && out[1][1] == 64 && out[1][2] == 127);
    out.clear();
    s.set (ccBase + 64, 0.999f); // same 7-bit value: nothing new
    flush();
    CHECK (out.empty());
    s.set (ccBase + 64, 0.0f);
    s.set (pitchBendBase, 0.75f);
    flush();
    CHECK (out.size() == 2 && out[0][2] == 0 && out[1][0] == 0xE0);
    CHECK (std::abs (s.value (pitchBendBase) - 0.75f) < 1e-3f);
}

TEST (midi_cc_automation_reaches_the_plugin_instrument)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin.id = "VST3-Piano-1";
    s.channels.push_back (c);
    s.patterns.emplace_back();
    s.mode = lynx::PlayMode::song;
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::channelParam, 0, 0, lynx::midictl::ccBase + 64 };
    a.points = { { 0, 1.0f, 0.0f }, { lynx::ppq * 4, 1.0f, 0.0f } };
    s.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = lynx::ppq * 4;
    s.playlist.push_back (clip);

    auto recorder = std::make_shared<feat133::Recorder>();
    lynx::RackState::Factories f;
    f.pluginInstruments = [&] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Instrument> { return recorder; };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, f);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256);
    r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), nullptr);
    const auto sent = std::any_of (recorder->params.begin(), recorder->params.end(),
                                   [] (const auto& p) { return p.first == lynx::midictl::ccBase + 64 && p.second == 1.0f; });
    CHECK (sent);
}

TEST (flp_wrapper_midi_cc_automation_keeps_its_index)
{
    // FL addresses MIDI CC n of a wrapped plugin as parameter 0x1000 + n (controlID 0x8000 + 0x1000 + n)
    const std::map<int, int> channels { { 3, 0 } };
    const auto t = lynx::automationTargetFor ({ (3u << 16) | 0x9040u }, channels, 4);
    CHECK (t.kind == lynx::AutomationTargetKind::channelParam && t.index == 0 && t.param == lynx::midictl::ccBase + 64);
    const auto p = lynx::automationTargetFor ({ (3u << 16) | 0x9081u }, channels, 4);
    CHECK (p.param == lynx::midictl::pressureBase + 1);
}

namespace feat133
{
double eval (const std::string& f, double a = 0, double b = 0, double c = 0, double songTime = 0)
{
    const auto p = lynx::formula::Program::compile (f);
    if (p == nullptr)
        return -999.0;
    lynx::formula::Inputs in;
    in.a = a;
    in.b = b;
    in.c = c;
    in.songTime = songTime;
    return p->evaluate (in);
}
bool near (double x, double y) { return std::abs (x - y) < 1e-9; }
} // namespace feat133

TEST (formula_controller_language_follows_fl)
{
    using feat133::eval;
    using feat133::near;
    CHECK (near (eval ("a+b+c", 0.1, 0.2, 0.3), 0.6));
    CHECK (near (eval ("2+3*4"), 14.0) && near (eval ("(2+3)*4"), 20.0));
    CHECK (near (eval ("2^3^2"), 64.0));        // left to right
    CHECK (near (eval ("-2^2"), 4.0));          // unary minus binds tighter than ^ (FL's precedence 10 vs 20)
    CHECK (near (eval ("1+2=3"), 1.0) && near (eval ("1+2<>3"), 0.0)); // comparisons below + -
    CHECK (near (eval ("not 0.4"), 1.0) && near (eval ("not 0.5"), 1.0) && near (eval ("not 0.6"), 0.0)); // Round, ties to even
    CHECK (near (eval ("1 and 0 or 1"), 1.0) && near (eval ("3 xor 1"), 2.0) && near (eval ("1 shl 3"), 8.0));
    CHECK (near (eval ("7 div 2"), 3.0) && near (eval ("7 mod 4"), 3.0) && near (eval ("5.5 fmod 2"), 1.5));
    CHECK (near (eval ("50%"), 0.5) && near (eval ("3!"), 6.0) && near (eval ("4!/2"), 12.0));
    CHECK (near (eval ("Round(2.5)"), 2.0) && near (eval ("round(3.5)"), 4.0) && near (eval ("Int(-1.5)"), -1.0) && near (eval ("Frac(1.25)"), 0.25));
    CHECK (near (eval ("If(0.6,2,3)"), 2.0) && near (eval ("Case(1,2,3)"), 2.0) && near (eval ("Case(0.9,2,3)"), 3.0));
    CHECK (near (eval ("IfL(1,2)+IfG(1,2)*10+IfLE(2,2)*100+IfGE(1,2)*1000+IfE(2,2)*10000"), 10101.0));
    CHECK (near (eval ("Inter(0.5,0.6,0.4)"), 1.0) && near (eval ("Internoswap(0.5,0.6,0.4)"), 0.0) && near (eval ("Inter(0.5,0.4,0.6)"), 1.0));
    CHECK (near (eval ("Map(0.5,2,4)"), 3.0) && near (eval ("Min(Max(a,b),c)", 0.9, 0.1, 0.5), 0.5));
    CHECK (near (eval ("LogN(2,8)"), 3.0) && near (eval ("Sqr(3)+Sqrt(16)"), 13.0) && near (eval ("Neg(2)"), -2.0) && near (eval ("Sum(1,2)"), 3.0));
    CHECK (near (eval ("Sec(0)"), 1.0) && near (eval ("ArcTg(1)*4"), 3.14159265358979323846) && near (eval ("pi"), 3.14159265358979323846));
    CHECK (near (eval ("Fold(0.3)"), 0.3) && near (eval ("Fold(1.2)"), 0.8) && near (eval ("Fold(-0.25)"), 0.25));
    CHECK (near (eval ("Warp(1.3,0)"), 0.3 + 0 * 1.0) || std::abs (eval ("Warp(1.3,0)") - 0.3) < 1e-12);
    CHECK (near (eval ("Tension(0.5,0)"), 0.5) && eval ("Tension(0.5,10)") > 0.5 && eval ("Tension(0.5,-10)") < 0.5);
    CHECK (near (eval ("Tension(0,10)"), 0.0) && std::abs (eval ("Tension(1,10)") - 1.0) < 1e-6);
    CHECK (near (eval ("Frac(SongTime)", 0, 0, 0, 2.25), 0.25) && near (eval ("SONGTIME()", 0, 0, 0, 3.0), 3.0)); // case-insensitive
    // FL's presets
    CHECK (near (eval ("a+Sin(SongTime*Pi*2*c)*(b-0.5)", 0.5, 1.0, 0.5, 0.5), 0.5 + std::sin (3.14159265358979323846 * 0.5) * 0.5));
    CHECK (near (eval ("Round(a*(b*16+1))/(b*16+1)", 0.5, 18944.0 / 65536.0, 0.5), std::nearbyint (0.5 * (18944.0 / 65536.0 * 16 + 1)) / (18944.0 / 65536.0 * 16 + 1)));
    const auto s1 = eval ("SeededRand(3)"), s2 = eval ("SeededRand(3)"), s3 = eval ("SeededRand(4)");
    CHECK (s1 == s2 && s1 != s3 && s1 >= 0.0 && s1 < 1.0);
    // errors
    std::string error;
    CHECK (lynx::formula::Program::compile ("a+(b", &error) == nullptr && error == "Missing )");
    CHECK (lynx::formula::Program::compile ("foo(1)", &error) == nullptr && error.find ("foo") != std::string::npos);
    CHECK (lynx::formula::Program::compile ("a+", &error) == nullptr);
    CHECK (lynx::formula::Program::compile ("", &error) == nullptr);
}

TEST (formula_controller_state_import_and_output)
{
    // FL's "Default.fst" state: version 2, a b c = 0, "a+b+c", an RTF description
    std::vector<std::uint8_t> st { 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 'a', '+', 'b', '+', 'c', 0, 0, 0, 0 };
    const auto parsed = lynx::formula::parseState (st);
    CHECK (parsed && parsed->version == 2 && parsed->formula == "a+b+c" && parsed->a == 0.0f);

    lynx::formula::State mine;
    mine.a = 0.5f;
    mine.b = 1.0f;
    mine.formula = "a*b+c";
    const auto bytes = lynx::formula::writeState (mine);
    const auto back = lynx::formula::parseState (bytes);
    CHECK (back && back->formula == "a*b+c" && back->a == 0.5f && back->b == 1.0f && back->c == 0.0f);

    const auto slot = lynx::effectFromFl ("Fruity Formula Controller", bytes, 120.0);
    CHECK (slot && slot->kind == lynx::EffectKind::formulaController && slot->params[0] == 0.5f && slot->params[1] == 1.0f);
    CHECK (std::string (lynx::effectInfo (lynx::EffectKind::formulaController).name) == "Formula Controller");

    auto fx = lynx::makeEffect (*slot);
    fx->prepare (48000.0);
    std::vector<float> l (64, 0.25f), r (64, -0.25f);
    auto p = slot->params;
    p[2] = 0.2f;
    fx->process (l.data(), r.data(), 64, p, 120.0);
    CHECK (std::abs (fx->controlOutput (0) - 0.7f) < 1e-6f);
    CHECK (l[10] == 0.25f && r[10] == -0.25f); // the audio passes through
    p[0] = 1.0f;
    fx->process (l.data(), r.data(), 64, p, 120.0);
    CHECK (fx->controlOutput (0) == 1.0f); // clipped to 0..1

    // the song position in quarter notes
    lynx::EffectSlot saw = lynx::makeEffectSlot (lynx::EffectKind::formulaController);
    lynx::formula::State sawState;
    sawState.formula = "Frac(SongTime)";
    saw.state = lynx::formula::writeState (sawState);
    auto sawFx = lynx::makeEffect (saw);
    sawFx->setSongPosition (lynx::ppq * 2.5);
    sawFx->process (l.data(), r.data(), 64, saw.params, 120.0);
    CHECK (std::abs (sawFx->controlOutput (0) - 0.5f) < 1e-6f);
}

TEST (midi_out_routes_notes_and_controls_to_the_plugin_on_its_port)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    lynx::ChannelDef out;
    out.instrument = lynx::InstrumentKind::midiOut;
    lynx::midictl::MidiOutSettings settings;
    settings.channel = 4;
    settings.port = 2;
    settings.program = 5;
    lynx::midictl::writeMidiOut (out.pluginState, settings);
    CHECK (lynx::midictl::parseMidiOut (out.pluginState).channel == 4 && lynx::midictl::parseMidiOut (out.pluginState).port == 2);
    s.channels.push_back (out);
    lynx::ChannelDef piano;
    piano.instrument = lynx::InstrumentKind::plugin;
    piano.plugin.id = "VST3-Piano-1";
    piano.plugin.midiInPort = 2;
    s.channels.push_back (piano);
    lynx::ChannelDef other = piano;
    other.plugin.midiInPort = 5; // another port: not reached
    s.channels.push_back (other);
    s.patterns.emplace_back();

    // the MIDI Out's CC 1 automated
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::channelParam, 0, 0, lynx::midictl::ccBase + 1 };
    a.points = { { 0, 0.5f, 0.0f }, { lynx::ppq * 4, 0.5f, 0.0f } };
    s.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = lynx::ppq * 4;
    s.playlist.push_back (clip);
    s.mode = lynx::PlayMode::song;

    std::vector<std::shared_ptr<feat133::Recorder>> made;
    lynx::RackState::Factories f;
    f.pluginInstruments = [&] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Instrument> {
        made.push_back (std::make_shared<feat133::Recorder>());
        return made.back();
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, f);
    CHECK (made.size() == 2 && state->midiTargets[0] == std::vector<int> { 1 });

    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = 0; // played on the MIDI Out channel
    on.key = 60;
    on.velocity = 0.8f;
    live.push (on);
    std::vector<float> l (256), rr (256);
    r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), &live);
    CHECK (made[0]->midiNotes.size() == 1 && made[0]->midiNotes[0].first == 60 && made[0]->midiNotes[0].second == 4);
    CHECK (made[1]->midiNotes.empty() && made[0]->plainNotes == 0);
    const auto cc = std::any_of (made[0]->params.begin(), made[0]->params.end(),
                                 [] (const auto& p) { return p.first == lynx::midictl::ccBase + 1; });
    CHECK (cc && made[1]->params.empty());
    // FL's reset when playback starts (REA_rodada167i §1.4), on the MIDI Out's channel: the pitch bend range (RPN 0 =
    // 12 semitones), then the program
    const auto& m0 = made[0]->messages;
    const auto at = [&] (const std::vector<std::uint8_t>& m) { return std::find (m0.begin(), m0.end(), m) - m0.begin(); };
    CHECK (at ({ 0xB4, 101, 0 }) == 0 && at ({ 0xB4, 100, 0 }) == 1 && at ({ 0xB4, 6, 12 }) == 2);
    CHECK (at ({ 0xC4, 4 }) < (std::ptrdiff_t) m0.size() && at ({ 0xC4, 4 }) > 2 && made[1]->messages.empty());

    // a pattern note of the MIDI Out channel, scheduled while playing
    s.mode = lynx::PlayMode::pattern;
    lynx::Note n;
    n.channel = 0;
    n.key = 62;
    n.start = 0;
    n.length = lynx::ppq;
    s.patterns[0].notes.push_back (n);
    s.patterns[0].lengthTicks = lynx::ppq * 4;
    made.clear();
    auto patternState = lynx::RackState::build (s, nullptr, 48000.0, f);
    lynx::RackRenderer r2;
    r2.prepare (48000.0, 256);
    r2.process (patternState.get(), true, 0.0, 256, l.data(), rr.data(), nullptr);
    CHECK (made.size() == 2 && made[0]->midiNotes.size() == 1 && made[0]->midiNotes[0].first == 62 && made[0]->midiNotes[0].second == 4);
    CHECK (made[1]->midiNotes.empty());
}

TEST (flp_demos_formula_controllers_import_and_compile)
{
    if (realFlpDir.empty())
        return;
    int slots = 0, compiled = 0, links = 0;
    // top-level slots and Patcher children (the demos use them inside Patchers, REA_rodada14 §7.2)
    std::function<void (const lynx::EffectSlot&)> visit = [&] (const lynx::EffectSlot& fx) {
        if (fx.kind == lynx::EffectKind::formulaController)
        {
            ++slots;
            const auto text = lynx::formula::formulaOf (fx.state);
            std::string error;
            if (lynx::formula::Program::compile (text, &error) != nullptr)
                ++compiled;
            else
                std::printf ("  formula not compiled: '%s' (%s)\n", text.c_str(), error.c_str());
        }
        for (const auto& b : fx.branches)
            for (const auto& child : b.chain)
                visit (child);
        links += (int) fx.links.size();
    };
    for (const auto* name : { "Mehran Abbasi - With You.flp", "Astes - Bien Duro.flp" })
    {
        const auto path = std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs" / name;
        if (! std::filesystem::exists (path))
            continue;
        std::ifstream in (path, std::ios::binary);
        const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
        const auto song = lynx::importFlp (flp::parseProject (file));
        for (const auto& track : song.mixer)
            for (const auto& fx : track.effects)
                visit (fx);
    }
    std::printf ("  Formula Controller in the demos: %d slots, %d compiled, %d container links\n", slots, compiled, links);
    CHECK (compiled == slots);
}
