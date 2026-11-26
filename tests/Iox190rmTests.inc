// Round 190rm (0.1.32 import / export sweep): the DAWproject export's fixes (core/DawProject.h).

TEST (iox190rm_dawproject_audio_format_never_zero)
{
    // FL's Ogg-in-WAV samples: JUCE's readers could not open them, the app's reader left 0 Hz / 0 channels / 0 s, and the
    // export wrote <Audio channels="0" sampleRate="0">: 401 of Thief's 1,351 clips. Now a reader that fails gives the
    // documented 44100 Hz / 2 channels, and a reader's zeros are never written.
    using namespace lynx;
    Song s;
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    s.playlistTracks.clear();
    s.mixer = Song::defaultMixer (2);
    s.mode = PlayMode::song;
    ChannelDef a;
    a.name = "Vox";
    a.instrument = InstrumentKind::audioClip;
    a.samplePath = "/samples/vox.wav";
    a.mixerTrack = 1;
    s.channels.push_back (a);
    a.name = "Riser";
    a.samplePath = "/samples/riser.wv";
    s.channels.push_back (a);
    PlaylistTrack t;
    t.name = "Audio";
    s.playlistTracks.push_back (t);
    for (int ch = 0; ch < 2; ++ch)
    {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = ch;
        c.track = 0;
        c.start = ch * 3840;
        c.length = 3840;
        s.playlist.push_back (c);
    }
    DawProjectExportOptions o;
    o.sampleFormat = [] (const std::string&, double& seconds, int& rate, int& channels) {
        seconds = 0.0, rate = 0, channels = 0; // (what the app's reader did)
        return false;
    };
    std::map<std::string, std::string> asked;
    o.readFile = [] (const std::string& path) { return std::vector<std::uint8_t> (path.begin(), path.end()); };
    o.embeddedName = [&asked] (const std::string& path) {
        const auto name = path.substr (path.rfind ('/') + 1);
        return asked[path] = name.substr (0, name.rfind ('.')) + ".wav"; // (converted to WAV)
    };
    const auto r = exportDawProject (s, o);
    CHECK (! r.bytes.empty());
    CHECK (r.projectXml.find ("channels=\"0\"") == std::string::npos);
    CHECK (r.projectXml.find ("sampleRate=\"0\"") == std::string::npos);
    CHECK (r.projectXml.find ("channels=\"2\" sampleRate=\"44100\"") != std::string::npos);
    // the embedded names: what embeddedName gave (a .wv decoded to WAV goes in as "riser.wav")
    CHECK (asked.size() == 2);
    CHECK (r.projectXml.find ("path=\"audio/riser.wav\"") != std::string::npos);
    CHECK (r.projectXml.find ("path=\"audio/vox.wav\"") != std::string::npos);
    CHECK (r.projectXml.find (".wv\"") == std::string::npos);
    zip::Reader z;
    CHECK (z.open (r.bytes.data(), r.bytes.size()));
    CHECK (z.find ("audio/riser.wav") >= 0 && z.find ("audio/vox.wav") >= 0);
    // a reader that knows the format: its values
    o.sampleFormat = [] (const std::string&, double& seconds, int& rate, int& channels) {
        seconds = 2.0, rate = 48000, channels = 1;
        return true;
    };
    const auto known = exportDawProject (s, o);
    CHECK (known.projectXml.find ("channels=\"1\" sampleRate=\"48000\" duration=\"2\"") != std::string::npos);
    // without embeddedName: the file's own name, as before
    o.embeddedName = nullptr;
    const auto plain = exportDawProject (s, o);
    CHECK (plain.projectXml.find ("path=\"audio/riser.wv\"") != std::string::npos);
}

TEST (iox190rm_dawproject_audio_clip_level_and_pitch)
{
    // Thief → DAWproject → LYNX was +3.3 dB louder on its audio clips: the clip gain (DAWproject has none) and the sample
    // channel's volume were left out, and a resampling channel's pitch (+6 st on 741 of 1,351 clips) played the file at
    // its own speed and pitch. Now a level every clip of a row shares goes into its track's Volume, and the pitch is the
    // speed it is (a "repitch" warp); the round trip plays the same.
    using namespace lynx;
    Song s;
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    s.playlistTracks.clear();
    s.mixer = Song::defaultMixer (3);
    s.mode = PlayMode::song;
    s.bpm = 120.0;
    ChannelDef a;
    a.name = "Riser";
    a.instrument = InstrumentKind::audioClip;
    a.samplePath = "/samples/riser.wav";
    a.mixerTrack = 1;
    a.volume = 0.6f;
    a.sampler.pitch = 12.0f; // (resamples: twice as fast)
    s.channels.push_back (a);
    a.name = "Loop";
    a.samplePath = "/samples/loop.wav";
    a.mixerTrack = 2;
    a.volume = audioClipUnityVolume;
    a.sampler.pitch = 0.0f;
    s.channels.push_back (a);
    for (const auto* name : { "Riser", "Loop" })
    {
        PlaylistTrack t;
        t.name = name;
        s.playlistTracks.push_back (t);
    }
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = 0;
    c.track = 0;
    c.length = 1920;
    c.gain = 0.5f;
    s.playlist.push_back (c);
    c.start = 3840; // (a second clip of the row, the same level)
    s.playlist.push_back (c);
    c.channel = 1;
    c.track = 1;
    c.start = 0;
    c.gain = 0.25f;
    s.playlist.push_back (c);
    c.start = 1920;
    c.gain = 1.0f; // (a row whose clips differ: said in the notes)
    s.playlist.push_back (c);
    DawProjectExportOptions o;
    o.sampleFormat = [] (const std::string&, double& seconds, int& rate, int& channels) {
        seconds = 4.0, rate = 44100, channels = 2;
        return true;
    };
    const auto r = exportDawProject (s, o);
    CHECK (! r.bytes.empty());
    // the Riser row: 0.5 × channelGain (0.6) / channelGain (unity) on its track's Volume
    const auto want = 0.5 * channelGain (0.6f) / channelGain (audioClipUnityVolume) * faderGain (s.mixer[1].volume);
    const auto riser = r.projectXml.find ("name=\"Riser\"");
    CHECK (riser != std::string::npos);
    const auto vol = r.projectXml.find ("<Volume", riser);
    CHECK (vol != std::string::npos);
    const auto at = r.projectXml.find ("value=\"", vol);
    const auto value = std::atof (r.projectXml.c_str() + at + 7);
    CHECK (std::abs (value - want) < 1.0e-4);
    // the pitch: 4 s of file are 8 beats at 120 BPM, twice as fast 4 beats ("repitch"); the Loop plays at its own speed
    CHECK (r.projectXml.find ("algorithm=\"repitch\"") != std::string::npos);
    CHECK (r.projectXml.find ("<Warp time=\"4\" contentTime=\"4\"/>") != std::string::npos);
    CHECK (r.projectXml.find ("<Warp time=\"8\" contentTime=\"4\"/>") == std::string::npos);
    bool levelsNote = false;
    for (const auto& n : r.notes)
        levelsNote = levelsNote || n.find ("different levels") != std::string::npos;
    CHECK (levelsNote);
    // and back: the Riser channel plays its 4 s in 4 beats (twice as fast), its insert at the folded level
    DawProjectImportOptions io;
    io.common.fileExists = [] (const std::string&) { return true; };
    const auto back = importDawProject (r.bytes, io);
    CHECK (back.has_value());
    if (back)
    {
        bool found = false;
        for (const auto& ch : back->channels)
            if (ch.samplePath == "/samples/riser.wav")
            {
                found = true;
                CHECK (std::abs (ch.sampler.stretchBeats - 4.0f) < 1.0e-4f && ! ch.sampler.keepPitch);
                CHECK (ch.mixerTrack > 0 && ch.mixerTrack < (int) back->mixer.size());
                if (ch.mixerTrack > 0 && ch.mixerTrack < (int) back->mixer.size())
                    CHECK (std::abs (faderGain (back->mixer[(std::size_t) ch.mixerTrack].volume) - want) < 2.0e-3);
            }
        CHECK (found);
    }
}

TEST (iox190rm_flp_export_plugin_song_start_values_on_the_plugins_range)
{
    // Thief → LYNX → .flp → FL 2026 rendered full-scale DC from the first sample: the Fruity Balance volume's song-start
    // value (event 0xD8, raw 256 of 0..320 = 0.8) went back as 0.8 x 65536 = 52429 (205 times the level). The song-start
    // value of a plugin parameter is the plugin's own integer (flRawRange), as the import reads it.
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer.resize (std::max<std::size_t> (song.mixer.size(), 3));
    auto balance = makeEffectSlot (EffectKind::fruityBalance);
    balance.flOrigin.plugin = "Fruity Balance"; // (as an FL import keeps it: FL's own state, written back as it was)
    balance.flOrigin.state = { 0, 0, 0, 0, 0, 1, 0, 0 };
    balance.flOrigin.fingerprint = flpexport::fingerprint (balance);
    song.mixer[2].effects = { balance };
    const AutomationTarget volume { AutomationTargetKind::effectParam, 2, 0, 1 };
    song.setInitialValue (volume, 0.8f);
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    bool found = false;
    for (const auto& c : project.initControls)
        if (c.id.isMixer() && c.id.track() == 2 && c.id.param() == flp::ControlId::pluginParamBase + 1)
        {
            found = true;
            CHECK (c.value == 256);
        }
    CHECK (found);
    // and back: the import reads it as the same 0.8
    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (project, io);
    bool again = false;
    for (const auto& v : back.initialValues)
        if (v.target.kind == AutomationTargetKind::effectParam && v.target.param == 1)
            again = std::abs (v.value - 0.8f) < 1.0e-3f;
    CHECK (again);
}
