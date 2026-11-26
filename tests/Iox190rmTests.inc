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

