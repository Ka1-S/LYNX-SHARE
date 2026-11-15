// --- REA round 134: FL Studio technologies (recordings, wrapper flags, processing options) ---

TEST (rea134_recording_names_like_fl)
{
    using namespace lynx::safety;
    // FL: "<project>_<yyyy-mm-dd hh-mm-ss>_<insert name>.wav" (FLEngine FUN_012b4e70)
    CHECK (recordingName ("Night Drive", 2026, 2, 1, 21, 59, 7, "Insert 9") == "Night Drive_2026-02-01 21-59-07_Insert 9");
    CHECK (recordingName ("", 2026, 10, 23, 8, 5, 0, "Vocal") == "untitled_2026-10-23 08-05-00_Vocal");
    CHECK (recordingName ("Song", 2026, 10, 23, 8, 5, 0, "") == "Song_2026-10-23 08-05-00");
    CHECK (std::string (recordedSubfolder) == "Audio/Recorded");
}

TEST (rea134_unused_recordings)
{
    using namespace lynx::safety;
    CHECK (samePath ("C:\\Music\\a.wav", "c:/music/A.WAV") && ! samePath ("a.wav", "b.wav") && ! samePath ("a.wav", "a.wav2"));
    const std::vector<std::string> recorded { "/p/Audio/Recorded/s_1.wav", "/p/Audio/Recorded/s_2.wav", "/p/Audio/Recorded/s_1.wav", "" };
    const auto unused = unusedRecordings (recorded, { "/p/Audio/Recorded/s_2.wav", "/samples/kick.wav" });
    CHECK (unused.size() == 1 && unused[0] == "/p/Audio/Recorded/s_1.wav");
    CHECK (unusedRecordings (recorded, {}).size() == 2);

    // the audio clip a take became counts as a reference
    lynx::Song song;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "/p/Audio/Recorded/s_1.wav";
    song.channels.push_back (c);
    CHECK (unusedRecordings (recorded, referencedSamples (song)).size() == 1 && unusedRecordings (recorded, referencedSamples (song))[0] == "/p/Audio/Recorded/s_2.wav");
}

TEST (rea134_allow_threaded_processing_off_pins_to_audio_thread)
{
    // FL's per-plugin "Allow threaded processing" (Fruity Wrapper menu "WantTPMenu", REA round 134)
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (16);
    for (int i = 5; i < 32; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = i % 2 ? lynx::InstrumentKind::synth : lynx::InstrumentKind::hat;
        c.mixerTrack = 1 + i % 12;
        c.volume = 0.3f;
        song.channels.push_back (c);
        for (int step = i % 4; step < 16; step += 3)
            song.patterns[0].notes.push_back ({ (std::int64_t) step * lynx::ticksPerStep, 200, i, 48 + i, 0.7f });
    }
    song.patterns[0].sortNotes();
    lynx::ChannelDef p;
    p.instrument = lynx::InstrumentKind::plugin;
    p.plugin.name = "Missing";
    p.plugin.threaded = false;
    p.mixerTrack = 4;
    song.channels.push_back (p);
    song.setRoute (3, 7, true, 0.6f);
    auto fx = lynx::makeEffectSlot (lynx::EffectKind::plugin);
    fx.plugin.threaded = false;
    song.mixer[3].effects.push_back (fx);
    song.mixer[3].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::reverb));

    auto seqState = lynx::RackState::build (song, nullptr, 48000.0);
    auto parState = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (parState->parallel);
    CHECK (parState->audioThreadChannels.size() == 1 && parState->audioThreadChannels[0] == (int) song.channels.size() - 1);
    CHECK (parState->threadedChannels.size() + 1 == song.channels.size());
    bool pinned3 = false;
    for (const auto& layer : parState->audioThreadLayers)
        for (auto t : layer)
            pinned3 = pinned3 || t == 3;
    CHECK (pinned3);

    lynx::WorkerPool pool (3);
    lynx::RackRenderer seq, par;
    seq.prepare (48000.0, 512);
    par.prepare (48000.0, 512);
    par.setWorkerPool (&pool);
    std::vector<float> l1 (512), r1 (512), l2 (512), r2 (512);
    const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
    bool same = true;
    for (int block = 0; block < 100; ++block)
    {
        seq.process (seqState.get(), true, block * 512 * tps, 512, l1.data(), r1.data());
        par.process (parState.get(), true, block * 512 * tps, 512, l2.data(), r2.data());
        same = same && l1 == l2 && r1 == r2;
    }
    CHECK (same);

    // saved in the project only when off (ProjectFile), part of the reference's identity
    lynx::PluginRef a, b;
    b.threaded = false;
    CHECK (! (a == b));
}

TEST (rea134_browser_search_index_dirty_folders)
{
    // a fake disk: folder -> (time, files, subfolders)
    struct F { std::int64_t time; std::vector<std::string> files, subs; };
    std::map<std::string, F> disk { { "/lib", { 1, { "kick 808.wav", "readme.txt" }, { "Drums", "Vox" } } },
                                    { "/lib/Drums", { 1, { "snare.wav", "kick hard.wav" }, {} } },
                                    { "/lib/Vox", { 1, { "hook.wav" }, { "Old" } } },
                                    { "/lib/Vox/Old", { 1, { "kick vox.wav" }, {} } } };
    int lists = 0;
    lynx::search::FileSystem fs;
    fs.folderTime = [&] (const std::string& f) { auto i = disk.find (f); return i == disk.end() ? (std::int64_t) -1 : i->second.time; };
    fs.list = [&] (const std::string& f) {
        ++lists;
        lynx::search::FolderListing l;
        if (auto i = disk.find (f); i != disk.end())
            l.exists = true, l.time = i->second.time, l.files = i->second.files, l.subfolders = i->second.subs;
        return l;
    };
    lynx::search::Index index;
    CHECK (index.refresh ("/lib", fs) == 4 && index.folderCount() == 4 && index.fileCount() == 6);
    bool truncated = false;
    auto hits = index.find ("kick", { "/lib" }, 100, truncated);
    CHECK (hits.size() == 3 && ! truncated);
    CHECK (index.find ("kick", { "/lib/Vox" }, 100, truncated).size() == 1);
    CHECK (index.find ("kick", { "/lib" }, 2, truncated).size() == 2 && truncated);

    // nothing changed: no folder is listed again
    CHECK (index.refresh ("/lib", fs) == 0);
    // a file added in Drums (its time changes): only Drums is listed again
    disk["/lib/Drums"].files.push_back ("kick soft.wav");
    disk["/lib/Drums"].time = 2;
    CHECK (index.refresh ("/lib", fs) == 1 && index.find ("kick", { "/lib" }, 100, truncated).size() == 4);
    // a folder removed: it and its subfolder leave the index
    disk.erase ("/lib/Vox/Old");
    disk["/lib/Vox"].subs.clear();
    disk["/lib/Vox"].time = 3;
    CHECK (index.refresh ("/lib", fs) == 1 && ! index.contains ("/lib/Vox/Old") && index.find ("kick", { "/lib" }, 100, truncated).size() == 3);

    // saved and read back
    lynx::search::Index copy;
    CHECK (copy.fromText (index.toText()) && copy.folderCount() == index.folderCount() && copy.fileCount() == index.fileCount());
    CHECK (copy.refresh ("/lib", fs) == 0);
    CHECK (! copy.fromText ("something else"));
}

TEST (rea134_hq_for_all_plugins_export_option)
{
    // FL's render window option "HQ for all plugins" (FLEngine FUN_00eec630: bit 2 of the render flags), on by default
    lynx::exportopts::Options o;
    CHECK (o.hqAllPlugins);
    o.hqAllPlugins = false;
    CHECK (! lynx::exportopts::fromText (lynx::exportopts::toText (o)).hqAllPlugins);
    CHECK (lynx::exportopts::fromText ("").hqAllPlugins);
    CHECK (! lynx::hqForAllPlugins());
    lynx::setHqForAllPlugins (true);
    CHECK (lynx::hqForAllPlugins());
    lynx::setHqForAllPlugins (false);
}

TEST (rea134_mixer_track_allow_threaded_processing)
{
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (8);
    for (int i = 0; i < 30; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::synth;
        c.mixerTrack = 1 + i % 6;
        song.channels.push_back (c);
    }
    song.mixer[2].threaded = false;
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    bool pinned = false, elsewhere = false;
    for (std::size_t d = 0; d < state->audioThreadLayers.size(); ++d)
    {
        for (auto t : state->audioThreadLayers[d])
            pinned = pinned || t == 2;
        for (auto t : state->threadedLayers[d])
            elsewhere = elsewhere || t == 2;
    }
    CHECK (pinned && ! elsewhere);
}

namespace
{
// a Fruity Wrapper 0xD5 blob: u32 version + {u32 id, u64 size, data} records
std::vector<std::uint8_t> rea134WrapperBlob (std::uint32_t version, std::uint32_t flags, std::uint32_t fixedOptions)
{
    std::vector<std::uint8_t> b;
    auto u32 = [&b] (std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((std::uint8_t) (v >> (8 * i))); };
    auto record = [&] (std::uint32_t id, const std::vector<std::uint8_t>& data) {
        u32 (id);
        u32 ((std::uint32_t) data.size());
        u32 (0);
        b.insert (b.end(), data.begin(), data.end());
    };
    u32 (version);
    std::vector<std::uint8_t> options (25, 0);
    for (int i = 0; i < 4; ++i)
    {
        options[(std::size_t) (9 + i)] = (std::uint8_t) (flags >> (8 * i));
        options[(std::size_t) (13 + i)] = (std::uint8_t) (fixedOptions >> (8 * i));
    }
    record (0x02, options);
    record (0x32, std::vector<std::uint8_t> (16, 0)); // type 0 = VST2
    record (0x36, { 'S', 'y', 'n', 't', 'h' });
    return b;
}
} // namespace

TEST (rea134_fl_wrapper_flags_from_flp)
{
    // FL's defaults (most saved plugins: 0xA4007D8C): no fixed buffers, threaded, not bridged
    lynx::ForeignPlugin f;
    CHECK (lynx::parseFlWrapper (rea134WrapperBlob (12, 0xA4007D8Cu, 0), f) && f.hasWrapperOptions && f.wrapperFlags == 0xA4007D8Cu);
    lynx::PluginRef ref;
    lynx::applyFlWrapperOptions (f, ref);
    CHECK (! ref.fixedBuffers && ! ref.maxSizeBuffers && ref.threaded && ! ref.separateProcess);

    // fixed size buffers + "Process maximum size buffers", threaded off, bridged
    CHECK (lynx::parseFlWrapper (rea134WrapperBlob (12, (0xA4007D8Cu | 0x2u | 0x40000u) & ~0x1000u, 0x1u), f));
    lynx::PluginRef ref2;
    lynx::applyFlWrapperOptions (f, ref2);
    CHECK (ref2.fixedBuffers && ref2.maxSizeBuffers && ! ref2.threaded && ref2.separateProcess);

    // 0x400000 = the defaults; old versions keep "threaded" / "bridged" untouched
    CHECK (lynx::parseFlWrapper (rea134WrapperBlob (12, 0x400002u, 1u), f));
    lynx::PluginRef ref3;
    lynx::applyFlWrapperOptions (f, ref3);
    CHECK (! ref3.fixedBuffers && ref3.threaded);
    CHECK (lynx::parseFlWrapper (rea134WrapperBlob (6, 0x2u, 0u), f));
    lynx::PluginRef ref4;
    lynx::applyFlWrapperOptions (f, ref4);
    CHECK (ref4.fixedBuffers && ref4.threaded && ! ref4.separateProcess);

    // FL's block sizes (Fruity Wrapper FUN_00870670): 2 ms rounded up to 4 samples, or the device buffer
    CHECK (lynx::flFixedBlockSize (44100.0, 512, false) == 88 && lynx::flFixedBlockSize (48000.0, 512, false) == 96);
    CHECK (lynx::flFixedBlockSize (96000.0, 256, false) == 192 && lynx::flFixedBlockSize (48000.0, 256, true) == 256);
}

TEST (rea134_export_window_length_and_disk_space)
{
    // FL's render window: "Length: 144 bars  Total time: 3'57''  Disk space: 38.76 MB" (Thief, 150 BPM, 16-bit WAV)
    using namespace lynx::exportopts;
    CHECK (std::abs (lengthInBars (230.4, 150.0, lynx::Meter (4, 4)) - 144.0) < 1e-9);
    Options o;
    o.format = Format::wav;
    o.wavBits = 16;
    o.sampleRate = 44100.0;
    const auto bytes = estimatedBytes (o, 230.4);
    CHECK (std::abs ((double) bytes / (1024.0 * 1024.0) - 38.76) < 0.05); // FL's figure, in MiB
    o.format = Format::mp3;
    o.mp3Kbps = 320;
    CHECK (estimatedBytes (o, 10.0) == 400000);
    CHECK (estimatedBytes (o, 0.0) == 0);
    o.showWhenDone = true;
    CHECK (fromText (toText (o)).showWhenDone && ! fromText ("").showWhenDone);
}
