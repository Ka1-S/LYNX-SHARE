// --- Round 177a slice 3: VCA faders (LYNX's addition; FL Studio has none) ----------------------------------------------

#include "core/MixerOps.h"

TEST (mixer177a3_vca_gains_add_in_db_nest_and_follow_moves)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (8);
    auto db = [] (float gain) { return 20.0 * std::log10 ((double) gain); };
    auto position = [] (double dbValue) { return faderPosition ((float) std::pow (10.0, dbValue / 20.0)); };

    CHECK (mixerops::vcaGain (song, 2) == 1.0f && ! mixerops::vcaMuted (song, 2)); // no VCA: nothing

    const auto drums = mixerops::createVca (song, { 3, 2, 2, 0 }, "Drums"); // (the Master and duplicates are left out)
    CHECK (drums == 0 && song.vcas[0].name == "Drums" && (song.vcas[0].members == std::vector<int> { 2, 3 }));
    CHECK (std::abs (mixerops::vcaGain (song, 2) - 1.0f) < 1e-6f); // a new VCA is at unity

    song.vcas[0].level = position (-6.0);
    CHECK (std::abs (db (mixerops::vcaGain (song, 2)) + 6.0) < 0.01 && mixerops::vcaGain (song, 4) == 1.0f);

    // a second VCA on insert 3 and a parent VCA over the drums: the dB add up
    const auto all = mixerops::createVca (song, { 3, 4 }, "All");
    song.vcas[(std::size_t) all].level = position (3.0);
    const auto master = mixerops::createVca (song, {}, "Master VCA");
    song.vcas[0].parent = master;
    song.vcas[(std::size_t) master].level = position (-2.0);
    CHECK (std::abs (db (mixerops::vcaGain (song, 2)) - (-6.0 - 2.0)) < 0.02);
    CHECK (std::abs (db (mixerops::vcaGain (song, 3)) - (-6.0 + 3.0 - 2.0)) < 0.02);
    CHECK (std::abs (db (mixerops::vcaGain (song, 4)) - 3.0) < 0.02);
    CHECK ((mixerops::vcaReach (song, master) == std::vector<int> { 2, 3 }));

    // mute: through the parent too
    song.vcas[(std::size_t) master].muted = true;
    CHECK (mixerops::vcaMuted (song, 2) && mixerops::vcaMuted (song, 3) && ! mixerops::vcaMuted (song, 4));
    song.vcas[(std::size_t) master].muted = false;

    // a parent cycle does not hang
    song.vcas[(std::size_t) master].parent = 0;
    CHECK (mixerops::vcasOf (song, 2).size() == 2);
    song.vcas[(std::size_t) master].parent = -1;

    // members follow moves; a deleted insert leaves the VCA
    mixerops::swapTracks (song, 2, 6);
    CHECK ((song.vcas[0].members == std::vector<int> { 3, 6 }));
    CHECK (mixerops::deleteTrack (song, 3));
    CHECK ((song.vcas[0].members == std::vector<int> { 5 }) && (song.vcas[(std::size_t) all].members == std::vector<int> { 3 }));

    // assign / remove
    CHECK (mixerops::assignToVca (song, { 1, 5 }, 0) == 1 && (song.vcas[0].members == std::vector<int> { 1, 5 }));
    CHECK (mixerops::removeFromVca (song, { 5, 7 }, 0) == 1 && (song.vcas[0].members == std::vector<int> { 1 }));
}

TEST (mixer177a3_merge_and_delete_vca)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (6);
    const auto v = mixerops::createVca (song, { 2, 3 }, "Bus");
    song.vcas[0].level = faderPosition (0.5f); // -6 dB
    song.mixer[3].volume = faderPosition (0.5f);
    Automation a;
    a.target = { AutomationTargetKind::mixerVolume, 2, 0, 0 };
    a.points = { { 0, unityFader, 0.0f }, { 96, faderPosition (0.25f), 0.0f } };
    Automation level;
    level.target = { AutomationTargetKind::vcaLevel, 0, 0, 0 };
    song.automations = { a, level };

    const auto before2 = faderGain (song.mixer[2].volume) * mixerops::vcaGain (song, 2);
    const auto before3 = faderGain (song.mixer[3].volume) * mixerops::vcaGain (song, 3);
    CHECK (mixerops::mergeVcaIntoMembers (song, v));
    CHECK (song.vcas[0].level == unityFader && mixerops::vcaGain (song, 2) == 1.0f);
    CHECK (std::abs (faderGain (song.mixer[2].volume) - before2) < 1e-4f && std::abs (faderGain (song.mixer[3].volume) - before3) < 1e-4f);
    CHECK (std::abs (faderGain (song.automations[0].points[0].value) - 0.5f) < 1e-4f);
    CHECK (std::abs (faderGain (song.automations[0].points[1].value) - 0.125f) < 1e-4f);
    CHECK (! mixerops::mergeVcaIntoMembers (song, v)); // at unity: nothing to merge

    // the VCA level is an automation target with a value
    song.vcas[0].level = 0.3f;
    CHECK (song.targetValue (level.target) == 0.3f);
    CHECK (song.setTargetValue (level.target, 0.6f) && song.vcas[0].level == 0.6f);

    // deleting: children lose their parent, automation its target
    mixerops::createVca (song, { 4 }, "");
    CHECK (song.vcas[1].name == "VCA 2");
    song.vcas[1].parent = 0;
    CHECK (mixerops::deleteVca (song, 0));
    CHECK (song.vcas.size() == 1 && song.vcas[0].parent == -1 && song.automations[1].target.kind == AutomationTargetKind::none);
}

TEST (mixer177a3_vca_scales_its_members_in_the_render)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (4);
    for (int t : { 2, 3 }) // two inserts playing their audio input (0.25 each) into the Master
    {
        song.mixer[(std::size_t) t].monitor = InputMonitor::on;
        song.mixer[(std::size_t) t].inputChannel = 0;
    }
    RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    LiveInput input;
    renderer.setLiveInput (&input);
    std::vector<float> a (256, 0.25f), l (256), r (256);
    input.channels[0] = a.data();
    input.channels[1] = a.data();
    input.numChannels = 2;
    auto run = [&] (const RackState* state) {
        for (int i = 0; i < 12; ++i) // past the fader ramp
            renderer.process (state, false, 0.0, 256, l.data(), r.data());
        return l[100];
    };

    auto plain = RackState::build (song, nullptr, 48000.0);
    CHECK (plain->vcaChains.empty()); // no VCA: nothing to do per block
    CHECK (std::abs (run (plain.get()) - 0.5f) < 1e-3f);

    // a VCA at -6 dB on insert 2: only insert 2 drops 6 dB
    mixerops::createVca (song, { 2 }, "Lead");
    song.vcas[0].level = faderPosition (std::pow (10.0f, -6.0f / 20.0f));
    auto half = RackState::build (song, plain.get(), 48000.0);
    const auto expected = 0.25f * std::pow (10.0f, -6.0f / 20.0f) + 0.25f;
    CHECK (std::abs (run (half.get()) - expected) < 2e-3f);

    // a live drag (AudioEngine::editLevels) reaches the gain through the snapshot's atomics, without a rebuild
    song.vcas[0].level = unityFader;
    half->live->store (song);
    CHECK (std::abs (run (half.get()) - 0.5f) < 2e-3f);

    // muting the VCA mutes its member only
    song.vcas[0].muted = true;
    auto muted = RackState::build (song, half.get(), 48000.0);
    CHECK (std::abs (run (muted.get()) - 0.25f) < 2e-3f);
}
