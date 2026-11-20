// Round details7_190mp (0.1.30, details pass 7): FL's Half speed, Auto name clips, Restretch all.

// FL's tempo menu "Half speed" (capture 190mp A4): the transport moves at half the rate, an override too; the song's
// tempo is not touched
TEST (details190mp_half_speed_transport)
{
    lynx::Transport t;
    t.sampleRate.store (48000.0);
    t.bpm.store (150.0);
    const auto normal = t.ticksPerSample();
    t.playSpeed.store (0.5);
    CHECK (std::abs (t.ticksPerSample() * 2.0 - normal) < 1.0e-12);
    CHECK (t.bpm.load() == 150.0);
    t.linkBpm.store (120.0); // an override is halved as well
    CHECK (std::abs (t.ticksPerSample() - 60.0 * lynx::ppq / (60.0 * 48000.0)) < 1.0e-12);
    t.linkBpm.store (0.0);
    t.pinnedBpm.store (75.0); // a pinned tempo is the renderer's, already halved
    CHECK (std::abs (t.ticksPerSample() - 75.0 * lynx::ppq / (60.0 * 48000.0)) < 1.0e-12);
}

// FL's Auto name clips (REA FUN_012ef5f0, capture 190mp A1): each source of the track's clips, in start order, takes the
// track's name ("Name", "Name #2"…) and colour; a leading '-' is dropped; a nameless track changes nothing
TEST (details190mp_auto_name_clips)
{
    using namespace lynx;
    Song song;
    song.channels.resize (2);
    song.channels[0].name = "Main Vox";
    song.channels[0].instrument = InstrumentKind::audioClip;
    song.channels[1].name = "Ad lib";
    song.channels[1].instrument = InstrumentKind::audioClip;
    song.patterns.resize (2);
    song.patterns[0].name = "Pattern 1";
    song.patterns[1].name = "Pattern 2";
    song.automations.resize (1);
    song.automations[0].name = "Filter";
    song.playlistTracks.resize (3);
    song.playlistTracks[0].name = "-Thief Vox";
    song.playlistTracks[0].colour = 0xff336699;
    song.playlistTracks[0].icon = 7;
    auto clip = [&song] (ClipKind kind, int source, int track, std::int64_t start) {
        Clip c;
        c.kind = kind;
        c.track = track;
        c.start = start;
        c.length = 960;
        if (kind == ClipKind::audio)
            c.channel = source;
        else if (kind == ClipKind::pattern)
            c.pattern = source;
        else
            c.automation = source;
        song.playlist.push_back (c);
    };
    clip (ClipKind::audio, 1, 0, 3840); // starts later: second
    clip (ClipKind::audio, 0, 0, 0);
    clip (ClipKind::audio, 0, 0, 1920); // same source: once
    clip (ClipKind::pattern, 1, 0, 7680);
    clip (ClipKind::automation, 0, 1, 0); // another track
    CHECK (pltracks::autoNameClips (song, 0) == 3);
    CHECK (song.channels[0].name == "Thief Vox");
    CHECK (song.channels[1].name == "Thief Vox #2");
    CHECK (song.patterns[1].name == "Thief Vox #3");
    CHECK (song.patterns[0].name == "Pattern 1");
    CHECK (song.channels[0].colour == 0xff336699 && song.patterns[1].colour == 0xff336699);
    CHECK (song.channels[1].icon == 7);
    CHECK (song.automations[0].name == "Filter");
    CHECK (pltracks::autoNameClips (song, 1) == 0); // no name: nothing
    song.playlistTracks[1].name = "Sweep";
    CHECK (pltracks::autoNameClips (song, 1) == 1 && song.automations[0].name == "Sweep");
    CHECK (pltracks::autoNameClips (song, 2) == 0 && pltracks::autoNameClips (song, 9) == 0);
}

// FL's Restretch all (REA FUN_00fe7510): the channels stretched ahead of time are those LYNX renders a stretched copy for
TEST (details190mp_restretchable_channels)
{
    using namespace lynx;
    Song song;
    song.channels.resize (4);
    for (auto& c : song.channels)
    {
        c.instrument = InstrumentKind::sampler;
        c.samplePath = "/x.wav";
    }
    CHECK (restretchableChannels (song) == 0);
    song.channels[0].sampler.keepPitch = true;
    song.channels[0].sampler.stretchBeats = 8.0f;
    song.channels[1].sampler.stretchBeats = 8.0f; // Resample: follows in real time
    song.channels[2].instrument = InstrumentKind::audioClip;
    song.channels[2].sampler.keepPitch = true;
    song.channels[2].sampler.stretchBeats = 4.0f;
    song.channels[3].sampler.keepPitch = true; // no length: not stretched
    CHECK (restretchableChannels (song) == 2);
}
