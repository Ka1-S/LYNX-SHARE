// Round 167k: FL's transport options after a jump, "Reset plugins on transport" for generators, the gap table keys
// (docs/analise/REA_rodada167k.md)
#define REA167K_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

namespace rea167k
{
// counts the transport resets it gets
struct CountingInstrument final : lynx::Instrument
{
    int resets = 0, panics = 0;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void panic() noexcept override { ++panics; }
    void transportReset() noexcept override { ++resets; }
    void render (float*, float*, int, float, float) noexcept override {}
};
} // namespace rea167k

TEST (rea167k_truncated_notes_chase_after_a_user_jump_not_the_loop_wrap)
{
    // FL's set-position (FUN_011e7a10 → FUN_011e78a0 → FUN_011e7470 with the catch-up flag): a note crossing the new
    // position plays after the user moves the position while playing; the loop's wrap is no set-position
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    REA167K_REQUIRE (song.channels.size() > 4 && ! song.patterns.empty());
    song.patterns[0].notes.clear();
    lynx::Note n;
    n.start = 0;
    n.length = 16 * lynx::ppq;
    n.key = 60;
    n.channel = 4; // the Synth
    song.patterns[0].notes.push_back (n);
    song.currentPattern = 0;
    const double rate = 48000.0, tps = lynx::ticksPerSample (song.bpm, rate);
    // plays 4 blocks from 0, jumps to beat 2 (a user jump when `user`), then measures 1.0 … 1.5 s after the jump
    auto tail = [&] (bool truncated, bool user) {
        auto state = lynx::RackState::build (song, nullptr, rate);
        lynx::RackRenderer renderer;
        renderer.prepare (rate, 512);
        unsigned jumps = 0;
        renderer.setTransportOptions (truncated, false, jumps);
        std::vector<float> l (512), r (512);
        double tick = 0.0;
        for (int b = 0; b < 4; ++b, tick += 512 * tps)
            renderer.process (state.get(), true, tick, 512, l.data(), r.data());
        tick = 2.0 * lynx::ppq;
        if (user)
            renderer.setTransportOptions (truncated, false, ++jumps);
        float m = 0.0f;
        for (int b = 0; b < (int) (1.5 * rate / 512); ++b, tick += 512 * tps)
        {
            renderer.process (state.get(), true, tick, 512, l.data(), r.data());
            if (b * 512 > rate)
                for (auto v : l)
                    m = std::max (m, std::abs (v));
        }
        return m;
    };
    CHECK (tail (true, true) > 1e-3f);   // chased: the note sounds on
    CHECK (tail (false, true) < 1e-5f);  // the option off: the jump released it
    CHECK (tail (true, false) < 1e-5f);  // a wrap (no user jump): released, not chased
}

TEST (rea167k_reset_plugins_on_transport_reaches_the_generators)
{
    // FL resets every plugin it registered (FUN_012b8620 → FUN_012b5210), generators too, when playback starts or the
    // position moves; nothing with the option off
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    REA167K_REQUIRE (song.channels.size() > 1);
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    auto counting = std::make_shared<rea167k::CountingInstrument>();
    state->instruments[1] = counting;
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
    renderer.setTransportOptions (true, false);
    renderer.process (state.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (counting->resets == 0);
    renderer.process (state.get(), false, 0.0, 256, l.data(), r.data());
    renderer.setTransportOptions (true, true);
    renderer.process (state.get(), true, 0.0, 256, l.data(), r.data()); // start
    CHECK (counting->resets == 1);
    renderer.process (state.get(), true, 256 * tps, 256, l.data(), r.data()); // plays on
    CHECK (counting->resets == 1);
    renderer.process (state.get(), true, 4.0 * lynx::ppq, 256, l.data(), r.data()); // jumped
    CHECK (counting->resets == 2);
}

TEST (rea167k_triangular_law_reaches_sampler_channels)
{
    // FL's render of FPC pads with the triangular law (k_gen_tri.flp): pan −0.5 keeps the left at the centre level and
    // halves the right (circular: 1.3066 / 0.5412); 3x Osc keeps its circular pan (LYNX's rack gains, unchanged)
    auto data = std::make_shared<lynx::SampleData>();
    data->left.assign (48000, 0.5f);
    auto level = [&] (bool triangular, float pan, float& left, float& right) {
        auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        REA167K_REQUIRE (sampler != nullptr);
        sampler->prepare (48000.0);
        sampler->setPanTriangular (triangular);
        sampler->noteOn (60, 1.0f);
        // the rack's circular gains for the channel pan (RackRenderer panGains, unity volume)
        const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
        std::vector<float> l (4096), r (4096);
        sampler->render (l.data(), r.data(), 4096, std::cos (angle) * 1.41421356f, std::sin (angle) * 1.41421356f);
        left = std::abs (l[2048]);
        right = std::abs (r[2048]);
    };
    float l0 = 0, r0 = 0, l = 0, r = 0;
    level (true, 0.0f, l0, r0);
    REA167K_REQUIRE (l0 > 1e-4f);
    level (true, -0.5f, l, r);
    CHECK (std::abs (l / l0 - 1.0f) < 1e-3f && std::abs (r / r0 - 0.5f) < 1e-3f);
    level (true, 0.75f, l, r);
    CHECK (std::abs (l / l0 - 0.25f) < 1e-3f && std::abs (r / r0 - 1.0f) < 1e-3f);
    level (false, -0.5f, l, r);
    CHECK (std::abs (l / l0 - 1.3066f) < 2e-3f && std::abs (r / r0 - 0.5412f) < 2e-3f);
}

TEST (rea167k_undo_backwards_key)
{
    // FL's Edit › "Undo backwards" Ctrl+Alt+Z (EditUndoBackwardsAction, shortcut 49242); Ctrl+Z stays "Undo"
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ 'Z', ctrl | alt }, Context::global, false) == "edit.undoBackwards");
    CHECK (map.find ({ 'Z', ctrl }, Context::global, false) == "edit.undo");
}
