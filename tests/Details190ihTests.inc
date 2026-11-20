// Round 190ih: behaviour details from the 190if audit (PLAN R2, R3, R4, R7, R9, R12, R14).

// R7 (DP-68): the Envelope Controller publishes the last key and velocity it received (outputs 8 / 9) for the
// editor's Keyboard / Velocity mapping line, as FL's manual describes ("the vertical white line ... the last note/key
// received"); before any note both are -1, and outputs 0 … 7 are unchanged
TEST (details190ih_envelope_controller_last_key_and_velocity)
{
    lynx::EnvelopeControllerData d;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::controller;
    c.controllerState = lynx::writeEnvelopeController (d);
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::controller);
    inst->prepare (48000.0);
    inst->configure (c, 120.0);
    std::vector<float> l (480), r (480);
    inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
    CHECK (inst->controlOutput (8) == -1.0f);
    CHECK (inst->controlOutput (9) == -1.0f);
    const auto before = inst->controlOutput (0);
    inst->noteOn (64, 0.75f);
    CHECK (inst->controlOutput (8) == 64.0f);
    CHECK (inst->controlOutput (9) == 0.75f);
    inst->noteOff (64);
    inst->noteOn (36, 0.25f);
    CHECK (inst->controlOutput (8) == 36.0f); // the last note wins
    CHECK (inst->controlOutput (9) == 0.25f);
    inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
    CHECK (std::isfinite (before) && std::isfinite (inst->controlOutput (0)));
}

// R2 (DP-1/2/3, DC-1/2): Gross Beat publishes its play state for the editor's locator and slot grids. FL's manual:
// the locator is "visible only when the host is playing"; "Green rectangle = Queued for playback. Green filled =
// Playing". With POS SYNC 4 beats the envelope's position is the song's beat within the 4-beat bar.
TEST (details190ih_gross_beat_publishes_its_play_position)
{
    using namespace lynx::grossbeat;
    const auto fresh = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    slot.state = fresh.state;
    slot.params[0] = paramFromRaw (0, 0);
    slot.params[1] = paramFromRaw (1, 0);
    auto fx = lynx::makeEffect (slot);
    CHECK (fx != nullptr);
    if (fx == nullptr)
        return;
    fx->prepare (48000.0);
    const auto tps = 120.0 / 60.0 * lynx::ppq / 48000.0; // ticks per sample at 120 BPM
    std::vector<float> l (480), r (480);
    std::size_t done = 0;
    auto block = [&] (bool moving) {
        std::fill (l.begin(), l.end(), 0.5f);
        std::fill (r.begin(), r.end(), 0.5f);
        fx->setSongPosition ((double) done * tps);
        fx->process (l.data(), r.data(), 480, slot.params, 120.0);
        if (moving)
            done += 480;
    };
    CHECK (fx->controlOutput (0) == 0.0f); // nothing published before it plays
    double worst = 0.0;
    for (int b = 0; b < 600; ++b) // 6 s = 12 beats: three passes of the 4-beat envelope
    {
        block (true);
        const auto beats = (double) done * tps / lynx::ppq;
        const auto expect = std::fmod (beats, 4.0) / 4.0;
        auto d = std::abs ((double) fx->controlOutput (1) - expect);
        d = std::min (d, 1.0 - d); // (at the wrap 0.999 and 0.0 are neighbours)
        if (b > 0)
            worst = std::max (worst, d);
        CHECK (fx->controlOutput (0) == 1.0f);
        CHECK (fx->controlOutput (3) == 1.0f && fx->controlOutput (4) == 1.0f); // slot 0 of each grid plays (+1)
    }
    CHECK (worst < 0.01); // within 1 % of the bar (the attack compensation runs a hair ahead)
    // the volume envelope moves with the same position sync
    CHECK (std::abs (fx->controlOutput (2) - fx->controlOutput (1)) < 0.01f);

    // a click on another time slot with a trigger sync (the factory's 6th time slot: 4 beats) queues it until then;
    // then it plays (a slot whose TRIG SYNC is off would switch at once)
    for (int b = 0; b < 50; ++b) // (one beat on: off the bar line, where the sync would switch at once)
        block (true);
    slot.params[0] = paramFromRaw (0, 5);
    block (true);
    CHECK (fx->controlOutput (5) == 6.0f); // queued: slot 5
    CHECK (fx->controlOutput (3) == 1.0f); // still playing slot 0
    for (int b = 0; b < 200 && fx->controlOutput (5) != 0.0f; ++b)
        block (true);
    CHECK (fx->controlOutput (5) == 0.0f);
    CHECK (fx->controlOutput (3) == 6.0f);

    // stopped (the song position holds): not playing, so the editor draws no locator
    block (false);
    block (false);
    CHECK (fx->controlOutput (0) == 0.0f);
}

// R4 (DC-8, DE-6): the Channel rack's activity light — bright at note-on, dimmer while held, dark after the release
// (FL capture details190if shots/fl-rack-selector-typingkey-before-during-after.png); a note shorter than a frame
// still lights it; Animations off = steady on / off
TEST (details190ih_rack_activity_light_lights_dims_and_fades)
{
    using namespace lynx::activity;
    Light a;
    CHECK (step (a, 5, false, true) == 0.0f); // the first look learns the counter: opening the rack lights nothing
    CHECK (step (a, 6, true, true) == peak);  // note-on
    float last = peak;
    for (int f = 0; f < 30; ++f)              // held: dims towards `held`, never below
    {
        const auto v = step (a, 6, true, true);
        CHECK (v <= last && v >= held);
        last = v;
    }
    CHECK (std::abs (last - held) < 0.01f);
    int frames = 0;
    while (step (a, 6, false, true) > 0.0f && frames < 60) // released: fades out
        ++frames;
    CHECK (frames > 0 && frames < 10);
    CHECK (step (a, 7, false, true) == peak); // a note shorter than a frame: the counter moved
    CHECK (step (a, 7, false, true) < peak);
    // Animations off: steady
    Light b;
    step (b, 0, false, false);
    CHECK (step (b, 1, true, false) == peak);
    CHECK (step (b, 1, true, false) == peak);
    CHECK (step (b, 1, false, false) == 0.0f);
    CHECK (step (b, 2, false, false) == peak); // the one frame of a short note
    CHECK (step (b, 2, false, false) == 0.0f);
    CHECK (shade (0.0f) == 0 && shade (peak) == 24);
}

// R4: the engine counts the blocks that struck a note on a channel, from any source (here a live note, as the typing
// keyboard and MIDI send them, while the transport is stopped); a held note strikes once
TEST (details190ih_engine_counts_note_strikes_per_channel)
{
    layer164::Rig rig (1); // channel 0 = a Layer, channel 1 = a plugin
    CHECK (rig.renderer.noteStrikes (1) == 0u);
    rig.play (1, 60);
    const auto once = rig.renderer.noteStrikes (1);
    CHECK (once == 1u);
    rig.play (1, 64, false); // a note-off of a key not held: no strike
    CHECK (rig.renderer.noteStrikes (1) == once);
    rig.play (1, 67);
    CHECK (rig.renderer.noteStrikes (1) == once + 1u);
    CHECK (rig.renderer.noteStrikes (0) == 0u); // the other channel stayed dark
    CHECK (rig.renderer.noteStrikes (-1) == 0u && rig.renderer.noteStrikes (100000) == 0u);
}

// R3 (DF-137): FL's Alt+1 … Alt+0 open the ten most recent projects; no other action has those chords
TEST (details190ih_recent_projects_keys)
{
    using namespace lynx::keys;
    for (int n = 1; n <= 10; ++n)
    {
        const auto* a = findAction ("file.recent" + std::to_string (n));
        CHECK (a != nullptr);
        if (a == nullptr)
            continue;
        const Chord chord { n == 10 ? '0' : '0' + n, alt };
        CHECK (a->context == Context::global && a->defaults.size() == 1 && a->defaults[0] == chord);
        for (const auto& other : actions())
            if (std::string (other.id) != a->id)
                for (const auto& c : other.defaults)
                    CHECK (! (c == chord));
    }
}

// R3 (DC-9): the engine keeps the last typing-keyboard / MIDI note for the hint bar ("Note on: C4 / velocity: 100");
// a note-off shows its note-on's velocity, as FL's hint did (details190if shots/fl-hint-noteon.png)
TEST (details190ih_last_live_note_packing)
{
    using namespace lynx::livenote;
    const auto a = pack (0, 60, 100, true);
    CHECK (a != 0u && key (a) == 60 && velocity (a) == 100 && on (a));
    const auto b = pack (a, 60, 100, false); // the same key released: a new word (the serial moved)
    CHECK (b != a && key (b) == 60 && velocity (b) == 100 && ! on (b));
    const auto c = pack (0xffff0000u, 200, -5, true); // clamped; the serial wraps past 0 to 1
    CHECK ((c >> 16) == 1u && key (c) == 127 && velocity (c) == 0);
}

// R12 (owner decision L1, DL-C): Live's keys act only in the Clip-launch layout; Pattern and Simple keep LYNX's keys
TEST (details190ih_live_keys_only_in_the_clip_launch_layout)
{
    using namespace lynx::keys;
    using lynx::livekeys::Command;
    using lynx::layoutstyle::Style;
    const struct { Chord chord; Command command; } live[] {
        { { tab, none }, Command::toggleView },          { { 'O', none }, Command::metronome },
        { { 'o', none }, Command::metronome },           { { f9, none }, Command::arrangementRecord },
        { { f9, ctrl | shift }, Command::sessionRecord }, { { f10, none }, Command::backToArrangement },
        { { 'M', none }, Command::computerKeyboard },    { { 'B', ctrl | alt }, Command::browser },
        { { 'F', ctrl | shift }, Command::follow },      { { 'J', ctrl }, Command::consolidate },
    };
    for (const auto& k : live)
    {
        CHECK (lynx::livekeys::commandFor (k.chord, Style::clipLaunch) == k.command);
        CHECK (lynx::livekeys::commandFor (k.chord, Style::pattern) == Command::none);
        CHECK (lynx::livekeys::commandFor (k.chord, Style::simple) == Command::none);
        CHECK (std::string (lynx::livekeys::menuKey (k.command, Style::clipLaunch)).size() > 1);
        CHECK (std::string (lynx::livekeys::menuKey (k.command, Style::pattern)).empty());
    }
    // the FL keys these chords have in the Pattern layout are still in the key map (they are not changed)
    CHECK (findAction ("transport.metronome") != nullptr && findAction ("transport.metronome")->defaults[0] == (Chord { 'M', ctrl }));
    CHECK (findAction ("view.typingPiano") != nullptr && findAction ("view.typingPiano")->defaults[0] == (Chord { 'T', ctrl }));
    // nearby chords stay free of Live's meaning
    CHECK (lynx::livekeys::commandFor ({ tab, shift }, Style::clipLaunch) == Command::none); // (Shift+Tab: detail view switch)
    CHECK (lynx::livekeys::commandFor ({ 'M', ctrl }, Style::clipLaunch) == Command::none);  // (Ctrl+M: MIDI Map Mode)
    CHECK (lynx::livekeys::commandFor ({ 'O', ctrl }, Style::clipLaunch) == Command::none);
}

// R9 (DP-16, owner decision PL5): Parametric EQ 2's momentary band solo — while a band is soloed only its region
// passes (FL manual: "holding (Shift) and clicking the Band Tokens will momentarily solo the Band"), for LYNX's own law
// and for a slot running FL's engine; ending the solo gives the plain EQ back
namespace details190ih
{
inline double rmsThrough (lynx::Effect& fx, const lynx::EffectParams& params, double hz)
{
    std::vector<float> l (512), r (512);
    double sum = 0.0;
    int count = 0;
    for (int b = 0, n = 0; b < 60; ++b)
    {
        for (int i = 0; i < 512; ++i, ++n)
            l[(std::size_t) i] = r[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * hz * n / 48000.0);
        fx.process (l.data(), r.data(), 512, params, 120.0);
        if (b >= 20) // (past the filters' settling and any latency)
            for (const auto v : l)
                sum += (double) v * v, ++count;
    }
    return std::sqrt (sum / std::max (1, count));
}
} // namespace details190ih

TEST (details190ih_peq2_band_solo_passes_only_that_band)
{
    for (const float law : { 0.0f, 0.5f }) // LYNX's own law, FL's Parametric EQ 2 engine (core/ParametricEqFl.h)
    {
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::parametricEq);
        slot.params[(std::size_t) lynx::peqfl::lawParam] = law;
        slot.params[(std::size_t) lynx::peq::param (2, 1)] = lynx::peq::hzValue (5000.0);
        auto fx = lynx::makeEffect (slot);
        CHECK (fx != nullptr);
        if (fx == nullptr)
            continue;
        fx->prepare (48000.0);
        const auto lowPlain = details190ih::rmsThrough (*fx, slot.params, 100.0);
        lynx::peq::setSoloBand (fx.get(), 2);
        CHECK (lynx::peq::soloBand (fx.get()) == 2);
        const auto lowSolo = details190ih::rmsThrough (*fx, slot.params, 100.0);
        const auto highSolo = details190ih::rmsThrough (*fx, slot.params, 5000.0);
        CHECK (lowPlain > 0.05);
        CHECK (lowSolo < lowPlain * 0.15); // the rest of the spectrum is gone
        CHECK (highSolo > 0.05);           // the band's own region passes
        lynx::peq::setSoloBand (fx.get(), -1);
        CHECK (lynx::peq::soloBand (fx.get()) == -1);
        CHECK (std::abs (details190ih::rmsThrough (*fx, slot.params, 100.0) - lowPlain) < 0.02);
    }
    // anything else than a Parametric EQ ignores it
    auto other = lynx::makeEffect (lynx::makeEffectSlot (lynx::EffectKind::grossBeat));
    lynx::peq::setSoloBand (other.get(), 1);
    CHECK (lynx::peq::soloBand (other.get()) == -1);
}

// R8 part (DP-92 / DP-93, owner decision PL4): FL's Presets › Randomize (random values for the published settings
// only) and Hybridize (a preset brings a random half of the published settings)
TEST (details190ih_presets_randomize_and_hybridize)
{
    lynx::EffectParams p {};
    p.fill (0.25f);
    lynx::presetmix::randomize (p, 10, 12345u);
    int changed = 0;
    for (int i = 0; i < 10; ++i)
    {
        CHECK (p[(std::size_t) i] >= 0.0f && p[(std::size_t) i] < 1.0f);
        changed += p[(std::size_t) i] != 0.25f ? 1 : 0;
    }
    CHECK (changed >= 9);
    CHECK (p[10] == 0.25f && p[(std::size_t) lynx::maxEffectParams - 1] == 0.25f); // the rest is left alone
    auto again = lynx::EffectParams {};
    again.fill (0.25f);
    lynx::presetmix::randomize (again, 10, 12345u);
    CHECK (again == p); // deterministic for a seed

    lynx::EffectParams before {}, preset {};
    before.fill (0.0f);
    preset.fill (1.0f);
    const auto mixed = lynx::presetmix::hybridize (before, preset, 100, 777u);
    int fromPreset = 0;
    for (int i = 0; i < 100; ++i)
        fromPreset += mixed[(std::size_t) i] == 1.0f ? 1 : 0;
    CHECK (fromPreset > 30 && fromPreset < 70); // about half
    CHECK (mixed[100] == 1.0f);                 // past the published ones: the preset's
}
