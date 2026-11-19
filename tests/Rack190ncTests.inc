// Round 190nc (0.1.31): Live's Solo / Cue (DL-110, from Live 12 manual 18.6: the Cue mode itself was not captured, the
// owner's device has one output pair) and the Macro Map pick-then-Map flow of DL-95.

#include "core/SessionMixer.h"

// The Cue Out must be an output other than the Main Out's; with one shared pair the switch stays on Solo (190mv B1)
TEST (rack190nc_cue_needs_a_separate_output)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (4);
    song.clipGrid.emplace();
    auto& cue = song.clipGrid->cue;
    CHECK (cue == SessionCue {} && ! cue.cueMode && cue.out == outputDefault && std::abs (cue.volume - 0.85f) < 1e-6f);
    // Cue Out "as Main" (Live's default 1/2 with Main Out 1/2): not separate, whatever the device
    CHECK (! sessionmixer::cueOutputSeparate (song, 2) && ! sessionmixer::cueOutputSeparate (song, 8));
    cue.cueMode = true;
    CHECK (! sessionmixer::cueActive (song, 8));
    // Cue Out 1 (mono) with Main Out 1/2: they share output 1 (captured: the switch does not toggle)
    cue.out = 0, cue.outMode = OutputMode::monoSum;
    CHECK (! sessionmixer::cueOutputSeparate (song, 2));
    // Cue Out 3/4: separate on a 4-output device, not on a 2-output one (it has no 3/4)
    cue.out = 2, cue.outMode = OutputMode::stereo;
    CHECK (sessionmixer::cueOutputSeparate (song, 4) && ! sessionmixer::cueOutputSeparate (song, 2) && ! sessionmixer::cueOutputSeparate (song, 3));
    CHECK (sessionmixer::cueActive (song, 4));
    cue.cueMode = false;
    CHECK (! sessionmixer::cueActive (song, 4)); // (the switch on Solo)
    cue.cueMode = true;
    // Main Out moved to 3/4: Cue Out 3/4 now shares it; Cue Out 1 (mono) is separate again
    song.mixer[0].outputChannel = 2;
    CHECK (! sessionmixer::cueOutputSeparate (song, 4));
    cue.out = 0, cue.outMode = OutputMode::monoLeft;
    CHECK (sessionmixer::cueOutputSeparate (song, 4));
    // a song without a clip grid has Live's defaults (no cue)
    Song plain;
    CHECK (! sessionmixer::cueActive (plain, 8) && sessionmixer::cueOf (plain).out == outputDefault);
    // the boxes write outputs as Live does: "1/2", "3", "3/4"
    CHECK (sessionmixer::outputBoxText (outputDefault, OutputMode::stereo) == "1/2");
    CHECK (sessionmixer::outputBoxText (2, OutputMode::monoSum) == "3" && sessionmixer::outputBoxText (2, OutputMode::stereo) == "3/4");
    // Cue Volume on Live's volume taper: 0.85 = 0 dB, 1 = +6 dB, 0 = silence
    CHECK (std::abs (sessionmixer::cueGain (0.85f) - 1.0f) < 1e-4f);
    CHECK (std::abs (20.0f * std::log10 (sessionmixer::cueGain (1.0f)) - 6.0f) < 0.05f);
    CHECK (sessionmixer::cueGain (0.0f) == 0.0f && sessionmixer::cueGain (0.5f) < sessionmixer::cueGain (0.7f));
}

// The Cue switches behave as the Solo switches: exclusive, Ctrl-click adds; they never touch the solos
TEST (rack190nc_cue_switches)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (5);
    song.mixer[3].solo = true;
    sessionmixer::cue (song, 1, true);
    CHECK (song.mixer[1].cue && ! song.mixer[2].cue);
    sessionmixer::cue (song, 2, true); // exclusive: 2 alone
    CHECK (! song.mixer[1].cue && song.mixer[2].cue);
    sessionmixer::cue (song, 4, false); // Ctrl: added
    CHECK (song.mixer[2].cue && song.mixer[4].cue);
    sessionmixer::cue (song, 4, false);
    CHECK (! song.mixer[4].cue);
    sessionmixer::cue (song, 2, true); // the only one pressed again: off
    CHECK (! song.mixer[2].cue);
    CHECK (song.mixer[3].solo && ! song.mixer[0].cue); // (solos untouched; the Main has no Cue switch)
    sessionmixer::cue (song, 0, true);
    sessionmixer::cue (song, 9, true);
    CHECK (! song.mixer[0].cue);
}

// A cued track's output reaches the Cue Out (lined up with Main, at the Cue Volume) and still the Main; nothing goes
// there while Cue mode is off or the Cue Out shares the Main Out
TEST (rack190nc_cue_routes_render)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (4);
    song.mixer[2].monitor = InputMonitor::on;
    song.mixer[2].inputChannel = 0; // stereo input 1-2: L 0.25, R -0.5
    song.mixer[2].cue = true;
    song.clipGrid.emplace();
    song.clipGrid->cue.out = 2; // Cue Out 3/4
    auto off = RackState::build (song, nullptr, 48000.0);
    CHECK (std::none_of (off->hwRoutes.begin(), off->hwRoutes.end(), [] (const RackState::HwRoute& r) { return r.cue; })); // (Solo mode)
    song.clipGrid->cue.cueMode = true;
    auto state = RackState::build (song, off.get(), 48000.0);
    CHECK (state->hwRoutes.size() == 1 && state->hwRoutes[0].cue && state->hwRoutes[0].track == 2 && state->hwRoutes[0].channel == 2);

    RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    LiveInput input;
    HardwareOutput hw;
    renderer.setLiveInput (&input);
    renderer.setHardwareOutput (&hw);
    std::vector<float> a (256, 0.25f), b (256, -0.5f), l (256), r (256);
    input.channels[0] = a.data();
    input.channels[1] = b.data();
    input.numChannels = 2;
    std::vector<std::vector<float>> outs (4, std::vector<float> (256, 0.0f));
    std::array<float*, 4> ptrs {};
    for (int k = 0; k < 4; ++k)
        ptrs[(std::size_t) k] = outs[(std::size_t) k].data();
    auto slotOf = [] (int ch) { return ch < 4 ? ch : -1; };
    hw.active = true;
    hw.cueGain = 0.5f; // (the engine sets it from the Cue Volume)
    renderer.process (state.get(), false, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[100] - 0.25f) < 1e-3f && std::abs (r[100] + 0.5f) < 1e-3f); // still heard at Main
    hw.drainInto (ptrs.data(), 4, slotOf, 256);
    CHECK (std::abs (outs[2][100] - 0.125f) < 1e-3f && std::abs (outs[3][100] + 0.25f) < 1e-3f); // the Cue Out, at the Cue Volume
    CHECK (outs[0][100] == 0.0f && outs[1][100] == 0.0f);

    // the Cue Out moved onto the Main Out's pair: no cue route
    song.clipGrid->cue.out = outputDefault;
    auto shared = RackState::build (song, state.get(), 48000.0);
    CHECK (shared->hwRoutes.empty());
    // a track with a hardware output target and a Cue switch: both routes
    song.clipGrid->cue.out = 2;
    song.mixer[2].outputChannel = 3;
    song.mixer[2].outputMode = OutputMode::monoSum;
    auto both = RackState::build (song, shared.get(), 48000.0);
    CHECK (both->hwRoutes.size() == 2 && ! both->hwRoutes[0].cue && both->hwRoutes[1].cue);
    hw.clear();
}

// Live's Macro Map: a picked parameter given to a macro is mapped once (a parameter belongs to one macro), full range
TEST (rack190nc_pick_then_map)
{
    using namespace lynx;
    auto doc = rack::makeRack (RackDoc::Kind::audioEffect);
    PatchNode fx;
    fx.effect = makeEffectSlot (EffectKind::delay);
    const auto node = rack::addDevice (doc, 0, fx);
    CHECK (node >= 0);
    RackMapping picked;
    picked.node = node, picked.param = 1;
    CHECK (rack::map (doc, 2, picked));
    int k = -1;
    CHECK (rack::macroOf (*rack::rackOf (doc), picked, &k) == 2 && k == 0);
    CHECK (rack::map (doc, 0, picked)); // the same parameter onto another macro moves it there
    CHECK (rack::macroOf (*rack::rackOf (doc), picked) == 0 && rack::rackOf (doc)->macros[2].mappings.empty());
    const auto& m = rack::rackOf (doc)->macros[0].mappings.front();
    CHECK (m.min == 0.0f && m.max == 1.0f);
    rack::setMacro (doc, 0, 127.0f);
    CHECK (std::abs (doc.nodes[(std::size_t) node].effect.params[1] - 1.0f) < 1e-5f);
}
