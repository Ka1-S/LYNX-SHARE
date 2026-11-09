// Issue #125: an automation clip kept a control linked to a hardware knob from moving, also with the song stopped.
// FL 2026 [EXEC round 185b, a channel volume knob with an automation clip over the whole song, moved with the mouse]:
// stopped, the moved value stays (the clip does not pull it back); playback start puts the clip's value back; moved while
// playing (a flat clip and a 0->1 ramp), the value stays for the rest of that playback; stopping keeps it.
// LYNX: AudioEngine::noteTweaked releases the target (RackRenderer::setReleasedSlots); the renderer forgets the released
// slots when the transport starts.

TEST (auto125_moved_control_leaves_its_automation_until_playback_starts)
{
    auto song = lynx::Song::makeDefault();
    song.mode = lynx::PlayMode::song;
    song.channels.clear();
    song.playlist.clear();
    song.automations.clear();
    song.mixer = lynx::Song::defaultMixer (2);
    song.mixer[0].inputChannel = 0; // a constant through the Master's input
    song.mixer[0].monitor = lynx::InputMonitor::on; // (heard stopped as well as playing)
    const lynx::AutomationTarget target { lynx::AutomationTargetKind::mixerVolume, 0 };
    lynx::Automation a;
    a.target = target;
    a.points = { { 0, 0.0f, 0.0f }, { 4 * lynx::ppq, 0.0f, 0.0f } }; // the Master's fader automated down to silence
    song.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = 64 * lynx::ppq;
    song.playlist.push_back (clip);
    std::vector<int> slots;
    lynx::RackState::slotsOfTarget (song, target, slots);

    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    lynx::LiveInput input;
    renderer.setLiveInput (&input);
    std::vector<float> in (512, 0.25f), l (512), r (512);
    input.channels[0] = in.data();
    input.channels[1] = in.data();
    input.numChannels = 2;
    double tick = 0.0;
    auto run = [&] (bool playing, int blocks) {
        for (int b = 0; b < blocks; ++b)
        {
            renderer.process (state.get(), playing, tick, 512, l.data(), r.data());
            if (playing)
                tick += 64.0;
        }
        return std::abs (l[511]);
    };

    CHECK (run (false, 8) < 1e-4f); // stopped, untouched: the clip's value at the position (as FL shows on opening)
    renderer.setReleasedSlots (slots); // moved while stopped (the knob, a linked controller)
    CHECK (renderer.isSlotOverridden (0));
    CHECK (run (false, 16) > 0.05f); // the moved value stays while stopped
    CHECK (run (true, 8) < 1e-4f);   // playback starts: the clip drives it again
    CHECK (! renderer.isSlotOverridden (0));

    renderer.setReleasedSlots (slots); // moved while playing
    CHECK (run (true, 16) > 0.05f);    // the clip no longer pulls it back during this playback
    CHECK (run (false, 8) > 0.05f);    // stopping keeps the moved value
    CHECK (renderer.isSlotOverridden (0));
    CHECK (run (true, 8) < 1e-4f);     // the next start: the automation again
}
