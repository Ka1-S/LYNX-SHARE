// Round 171c (discussion #10, masoudnp: "make smoother movement to volume knob when play"): a fader drag reaches the
// playing snapshot through its live levels, without a rebuild, and the gain still glides FL's 0.001 per sample.

namespace
{
// insert 2 monitors a DC input of 0.25 on both sides; nothing else plays
struct Fader171cRig
{
    lynx::Song song = [] {
        auto s = lynx::Song::makeDefault();
        s.channels.clear();
        s.playlist.clear();
        s.mixer = lynx::Song::defaultMixer (3);
        s.mixer[2].monitor = lynx::InputMonitor::on;
        s.mixer[2].inputChannel = 0;
        return s;
    }();
    lynx::RackRenderer renderer;
    lynx::LiveInput input;
    std::vector<float> a = std::vector<float> (256, 0.25f), l = std::vector<float> (256), r = std::vector<float> (256);

    Fader171cRig()
    {
        renderer.prepare (48000.0, 512);
        renderer.setLiveInput (&input);
        input.channels[0] = a.data();
        input.channels[1] = a.data();
        input.numChannels = 2;
    }
    void block (const lynx::RackState& state) { renderer.process (&state, false, 0.0, 256, l.data(), r.data()); }
};
} // namespace

TEST (fader171c_live_levels_follow_the_song)
{
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (4);
    lynx::LiveLevels live (song);
    CHECK (live.matches (song) && live.mixerVolume.size() == 4 && live.channelVolume.size() == song.channels.size());
    CHECK (live.mixerVolume[2].load() == song.mixer[2].volume);

    song.mixer[2].volume = 0.3f;
    song.mixer[3].pan = -0.5f;
    song.mixer[1].stereo = 0.25f;
    if (! song.channels.empty())
        song.channels[0].volume = 0.1f, song.channels[0].pan = 0.75f;
    live.store (song);
    CHECK (live.mixerVolume[2].load() == 0.3f && live.mixerPan[3].load() == -0.5f && live.mixerStereo[1].load() == 0.25f);
    CHECK (song.channels.empty() || (live.channelVolume[0].load() == 0.1f && live.channelPan[0].load() == 0.75f));

    // another layout: not matched, nothing written
    song.mixer.push_back (song.mixer[1]);
    song.mixer[2].volume = 0.9f;
    CHECK (! live.matches (song));
    live.store (song);
    CHECK (live.mixerVolume[2].load() == 0.3f);

    // every built snapshot carries them
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (state->live != nullptr && state->live->matches (song) && state->live->mixerVolume[2].load() == 0.9f);
}

TEST (fader171c_a_live_fader_move_glides_without_a_rebuild)
{
    Fader171cRig rig;
    auto state = lynx::RackState::build (rig.song, nullptr, 48000.0);
    rig.block (*state);
    CHECK (std::abs (rig.l[200] - 0.25f) < 1e-4f); // unity fader

    // the fader dragged down to −12 dB-ish: the same snapshot plays it
    const auto position = 0.45f;
    state->live->mixerVolume[2].store (position);
    const auto target = lynx::mixerMatrix (rig.song.mixer[2], position, 0.0f, rig.song.panTriangular);
    CHECK (target.ll < 0.5f);

    float previous = rig.l[255];
    float worstStep = 0.0f;
    for (int b = 0; b < 8; ++b) // 2048 samples: past the glide (≈ 0.65 / 0.001 samples)
    {
        rig.block (*state);
        for (int i = 0; i < 256; ++i)
        {
            worstStep = std::max (worstStep, std::abs (rig.l[(std::size_t) i] - previous));
            previous = rig.l[(std::size_t) i];
        }
    }
    // FL's glide (REA_rodada85 §3): at most 0.001 of gain per sample, i.e. 0.00025 on this 0.25 input: no click
    CHECK (worstStep <= 0.001f * 0.25f + 1e-6f);
    CHECK (std::abs (rig.l[200] - 0.25f * target.ll) < 1e-4f);

    // pan and separation come live too
    state->live->mixerPan[2].store (1.0f);
    for (int b = 0; b < 8; ++b)
        rig.block (*state);
    CHECK (rig.l[200] < 0.01f && rig.r[200] > rig.l[200] + 0.05f); // hard right
    state->live->mixerPan[2].store (0.0f);
    state->live->mixerStereo[2].store (1.0f); // mono: the same here (L = R)
    for (int b = 0; b < 8; ++b)
        rig.block (*state);
    CHECK (std::abs (rig.l[200] - rig.r[200]) < 1e-5f);

    // the Master's fader too
    state->live->mixerVolume[0].store (0.0f);
    for (int b = 0; b < 8; ++b)
        rig.block (*state);
    CHECK (std::abs (rig.l[200]) < 1e-6f);
}

TEST (fader171c_rebuilt_snapshot_keeps_the_dragged_value)
{
    // the publish after the drag builds from the Song, which holds the same value: no jump at the swap
    Fader171cRig rig;
    auto state = lynx::RackState::build (rig.song, nullptr, 48000.0);
    rig.song.mixer[2].volume = 0.5f;
    state->live->store (rig.song);
    for (int b = 0; b < 8; ++b)
        rig.block (*state);
    const auto before = rig.l[255];
    auto next = lynx::RackState::build (rig.song, state.get(), 48000.0);
    rig.block (*next);
    CHECK (std::abs (rig.l[0] - before) < 1e-6f && std::abs (rig.l[200] - before) < 1e-6f);
}

TEST (fader171c_no_live_levels_uses_the_song)
{
    // a snapshot without live levels (older callers) plays its Song's values
    Fader171cRig rig;
    rig.song.mixer[2].volume = 0.4f;
    auto built = lynx::RackState::build (rig.song, nullptr, 48000.0);
    auto copy = std::make_unique<lynx::RackState> (*built);
    copy->live = nullptr;
    for (int b = 0; b < 8; ++b)
        rig.block (*copy);
    const auto target = lynx::mixerMatrix (rig.song.mixer[2], 0.4f, 0.0f, rig.song.panTriangular);
    CHECK (std::abs (rig.l[200] - 0.25f * target.ll) < 1e-4f);
}
