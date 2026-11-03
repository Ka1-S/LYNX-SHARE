// Round 168x (issue #41, Carlrune20): "Arming a mixer insert and recording captures only the dry input. There's no way
// to record the signal after the insert's effects". FL has the option: the insert's "Record" positions (External input
// only / External and mixer input / Post effects / Post EQ / Post level and panning / Post track; Post EQ is FL 2026's
// default). The renderer taps the insert at that point for the take. Evidence: docs/analise/REA_rodada168x.md §2.

namespace
{
struct Rec168Taps
{
    lynx::Song song = lynx::Song::makeDefault();
    lynx::RackRenderer renderer;
    lynx::LiveInput input;
    std::vector<float> a = std::vector<float> (256, 0.25f), b = std::vector<float> (256, 0.25f), l = std::vector<float> (256),
                       r = std::vector<float> (256);

    Rec168Taps()
    {
        song.channels.clear();
        song.playlist.clear();
        song.mixer = lynx::Song::defaultMixer (4);
        auto& mt = song.mixer[2];
        mt.inputChannel = 0; // In 1 - In 2 (a DC of 0.25: easy to follow through the insert)
        mt.monitor = lynx::InputMonitor::automatic;
        auto gain = lynx::makeEffectSlot (lynx::EffectKind::utility);
        gain.params[0] = 0.625f; // +6 dB
        mt.effects.push_back (gain);
        mt.eq.gainDb[0] = 6.0f;  // the built-in EQ's low shelf: +6 dB at DC
        mt.volume = lynx::faderPosition (0.5f); // −6 dB
        renderer.prepare (48000.0, 512);
        renderer.setLiveInput (&input);
        input.channels[0] = a.data();
        input.channels[1] = b.data();
        input.numChannels = 2;
        input.armed[2].store (true);
        input.recording.store (true);
        const lynx::RecordSource points[] { lynx::RecordSource::externalAndMixer, lynx::RecordSource::postEffects, lynx::RecordSource::postEq,
                                            lynx::RecordSource::postLevel, lynx::RecordSource::postTrack };
        for (int k = 0; k < 5; ++k)
        {
            input.taps[(std::size_t) k].point.store ((int) points[k]);
            input.taps[(std::size_t) k].track.store (2);
        }
        input.tapCount.store (5);
    }

    // a few blocks (the fader glides, the EQ settles), then sample 200 of the last block at each tap
    std::array<float, 5> run (const lynx::RackState* state)
    {
        for (int block = 0; block < 12; ++block)
        {
            for (auto& tap : input.taps)
                for (auto& d : tap.data)
                    std::fill (d.begin(), d.begin() + 256, 0.0f); // AudioEngine clears them before each block
            renderer.process (state, true, 0.0, 256, l.data(), r.data());
        }
        std::array<float, 5> out {};
        for (int k = 0; k < 5; ++k)
            out[(std::size_t) k] = input.taps[(std::size_t) k].data[0][200];
        return out;
    }
};

bool rec168Near (float a, float b) { return std::abs (a - b) < 0.02f * std::max (0.05f, std::abs (b)); }
} // namespace

TEST (rec168_fl_default_is_post_eq)
{
    // FL 2026's new inserts record Post EQ (its demo projects save 0xA5 = 3 on every insert)
    lynx::MixerTrack track;
    CHECK (track.recordSource == lynx::RecordSource::postEq && ! track.recordDryToo);
    CHECK ((int) lynx::RecordSource::externalInput == 0 && (int) lynx::RecordSource::postTrack == 5); // FL's byte values
    flp::Project project;
    CHECK (project.mixer.empty());
    flp::MixerInsert insert;
    CHECK (insert.recordSource == -1); // no 0xA5 event: LYNX keeps the default
}

TEST (rec168_taps_follow_fl_insert_order)
{
    Rec168Taps t;
    auto state = lynx::RackState::build (t.song, nullptr, 48000.0);
    const auto v = t.run (state.get());
    // input 0.25 → slots (+6 dB) → EQ (+6 dB at DC) → fader (half) → mute
    CHECK (rec168Near (v[0], 0.25f));        // External and mixer input: the insert's input, before the slots
    CHECK (rec168Near (v[1], 0.25f * 1.995f)); // Post effects
    CHECK (rec168Near (v[2], 0.25f * 1.995f * 1.995f)); // Post EQ (FL's default)
    CHECK (v[3] > v[2] * 0.45f && v[3] < v[2] * 0.55f); // Post level and panning (−6 dB, the pan law at the centre)
    CHECK (rec168Near (v[4], v[3]));        // Post track: the same while the insert plays
    CHECK (rec168Near (t.l[200], v[4]));    // ... which is what reaches the Master

    // muted: Post track goes silent, the positions before the mute keep recording (the effects still run)
    t.song.mixer[2].muted = true;
    auto muted = lynx::RackState::build (t.song, state.get(), 48000.0);
    const auto m = t.run (muted.get());
    CHECK (std::abs (m[4]) < 1e-6f && std::abs (t.l[200]) < 1e-6f);
    CHECK (rec168Near (m[1], v[1]) && rec168Near (m[2], v[2]) && rec168Near (m[3], v[3]));

    // no taps: the renderer does no tap work (the buffers stay as they were)
    t.input.tapCount.store (0);
    t.input.taps[1].data[0][200] = 7.0f;
    t.renderer.process (muted.get(), true, 0.0, 256, t.l.data(), t.r.data());
    CHECK (t.input.taps[1].data[0][200] == 7.0f);
}

TEST (rec168_wet_take_latency_is_the_slots_latency)
{
    // a take tapped after the slots arrives late by their latency (the input itself is not delayed by the plan's
    // alignment); the manual track latency delays nothing inside the insert
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.mixer = lynx::Song::defaultMixer (4);
    song.mixer[2].latencyMs = 10.0f;
    std::vector<std::vector<int>> slots (4);
    slots[2] = { 64, 128 };
    slots[1] = { 500 };
    song.mixer[1].sends.push_back ({ 2, 1.0f });
    const auto plan = lynx::computePdc (song, {}, slots, 48000.0);
    CHECK (lynx::insertEffectsLatency (plan, song, 2, 48000.0) == 192);
    CHECK (lynx::insertEffectsLatency (plan, song, 1, 48000.0) == 500);
    CHECK (lynx::insertEffectsLatency (plan, song, 3, 48000.0) == 0);
    CHECK (lynx::insertEffectsLatency (plan, song, 99, 48000.0) == 0);
}
