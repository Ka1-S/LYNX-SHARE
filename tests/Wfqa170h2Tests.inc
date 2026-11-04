// Round 170h2 (user decisions 2026-11-04, docs/analise/REA_rodada170h.md): Ctrl+H = FL's Stop sound, the Undo
// history on Ctrl+Alt+H; the default new project has empty steps

// FL: Tools › Macros › Stop sound = Ctrl+H (DFM MacroStopSoundAction ShortCut 16456); Ctrl+Alt+H is free in FL
// (FLEngine DFM shortcut list) and in LYNX
TEST (wfqa170h2_ctrl_h_stops_sound_ctrl_alt_h_history)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ 'H', ctrl }, Context::global, false) == "tools.stopSound");
    CHECK (map.find ({ 'H', ctrl }, Context::pianoRoll, true) == "tools.stopSound");
    CHECK (map.find ({ 'H', (std::uint8_t) (ctrl | alt) }, Context::global, false) == "edit.history");
    CHECK (map.actionsWith ({ 'H', (std::uint8_t) (ctrl | alt) }, Context::global).size() == 1);
    CHECK (findAction ("tools.stopSound") != nullptr);
}

namespace
{
// Plays a note on the Synth (channel 4, insert 5 with a long-feedback Delay), lets go, then sends the panic
// (always) and FL's Stop sound (when `stop`); returns the energy of what comes out 0.5 s … 2 s later
double wfqa170h2TailAfter (bool stop)
{
    auto song = lynx::Song::makeDefault();
    song.patterns[0].notes.clear();
    song.mixer[0].effects.clear();
    auto delay = lynx::makeEffectSlot (lynx::EffectKind::delay);
    delay.params[1] = 0.85f; // feedback: echoes for seconds
    delay.params[2] = 0.6f;
    song.mixer[5].effects = { delay };
    const double rate = 48000.0;
    auto state = lynx::RackState::build (song, nullptr, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, 512);
    lynx::LiveEventQueue live;
    std::vector<float> l (512), r (512);
    auto block = [&] { renderer.process (state.get(), false, 0.0, 512, l.data(), r.data(), &live); };

    lynx::NoteEvent on;
    on.type = lynx::NoteEvent::Type::on;
    on.channel = 4;
    on.key = 60;
    on.velocity = 0.9f;
    live.push (on);
    for (int i = 0; i < 20; ++i)
        block();
    auto off = on;
    off.type = lynx::NoteEvent::Type::off;
    live.push (off);
    block();

    lynx::NoteEvent panic;
    panic.type = lynx::NoteEvent::Type::choke;
    panic.channel = -1;
    live.push (panic);
    if (stop)
    {
        auto reset = panic;
        reset.channel = lynx::RackRenderer::stopSoundChannel;
        live.push (reset);
    }
    for (int i = 0; i < 47; ++i) // ~0.5 s: the synth's own release is over
        block();
    double energy = 0.0;
    for (int i = 0; i < 141; ++i) // ~1.5 s
    {
        block();
        for (int k = 0; k < 512; ++k)
            energy += (double) l[(std::size_t) k] * l[(std::size_t) k] + (double) r[(std::size_t) k] * r[(std::size_t) k];
    }
    return energy;
}
} // namespace

// FL's Stop sound (MacroStopSoundActionExecute → FUN_011c8d10 → FUN_011c8a40 (1, 1, 1) → FUN_012b5210: every
// registered plugin reset): the delay's echoes stop too, the plain panic (all notes off) leaves them ringing
TEST (wfqa170h2_stop_sound_clears_effect_tails)
{
    const auto panicOnly = wfqa170h2TailAfter (false);
    const auto stopped = wfqa170h2TailAfter (true);
    CHECK (panicOnly > 1e-3);
    CHECK (stopped < panicOnly * 1e-4);
}

// FL's default template has the channels with EMPTY steps (driven FL, fl-02); LYNX's default new project now too —
// same channels, inserts and Master limiter as before; the beat stays in the "Basic beat" template
TEST (wfqa170h2_default_new_project_has_empty_steps)
{
    const auto song = lynx::makeNewProject();
    const auto reference = lynx::Song::makeDefault();
    CHECK (song.channels.size() == reference.channels.size());
    for (std::size_t c = 0; c < song.channels.size() && c < reference.channels.size(); ++c)
        CHECK (song.channels[c].name == reference.channels[c].name && song.channels[c].instrument == reference.channels[c].instrument);
    CHECK (! song.patterns.empty() && song.patterns[0].notes.empty());
    CHECK (! song.mixer.empty() && ! song.mixer[0].effects.empty()); // the Master limiter
    const auto beat = lynx::makeNewProject (1); // "Basic beat"
    CHECK (! beat.patterns[0].notes.empty());
}
