// --- Export: one file per marker section, presets, "same as last time" (core/ExportOptions.h) - round 190ap ---

namespace exportsections190ap
{
// an 8-bar song (one pattern clip of 8 bars) with markers Intro (bar 1), Verse (bar 3), a time signature at bar 4 (not
// a section), a nameless marker at bar 5 and Drop at bar 7
inline lynx::Song song (bool markerAtStart = true)
{
    auto s = lynx::Song::makeDefault();
    const auto bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.mode = lynx::PlayMode::song;
    s.playlist.clear();
    lynx::Clip clip;
    clip.pattern = 0;
    clip.start = 0;
    clip.length = 8 * bar;
    s.playlist.push_back (clip);
    const auto add = [&s, bar] (int atBar, const char* name, int action = lynx::Marker::none, int numerator = 0) {
        lynx::Marker m;
        m.tick = (atBar - 1) * bar;
        m.name = name;
        m.action = action;
        m.numerator = numerator;
        s.addMarker (m);
    };
    if (markerAtStart)
        add (1, "Intro");
    add (3, "Verse");
    add (4, "3/4", lynx::Marker::timeSignature, 3);
    add (5, "");
    add (7, "Drop: A/B?");
    add (7, "Second marker at the drop");
    add (20, "After the end");
    return s;
}
} // namespace exportsections190ap

TEST (exportsections190ap_markers_cut_the_song)
{
    using namespace lynx::exportopts;
    const auto song = exportsections190ap::song();
    const auto bar = (std::int64_t) lynx::ppq * song.beatsPerBar;
    CHECK (song.songLength() == 8 * bar);
    const auto sections = markerSections (song);
    CHECK (sections.size() == 4);
    if (sections.size() == 4)
    {
        CHECK (sections[0] == (Section { "Intro", 0, 2 * bar }));
        CHECK (sections[1] == (Section { "Verse", 2 * bar, 4 * bar }));
        CHECK (sections[2].name.rfind ("Marker ", 0) == 0 && sections[2].start == 4 * bar && sections[2].end == 6 * bar);
        CHECK (sections[3] == (Section { "Drop: A/B?", 6 * bar, 8 * bar }));
        // the sections tile the song: nothing is left out and nothing is rendered twice
        std::int64_t at = 0;
        for (const auto& s : sections)
        {
            CHECK (s.start == at && s.end > s.start);
            at = s.end;
        }
        CHECK (at == song.songLength());
    }
    // what lies before the first marker is its own section
    const auto late = markerSections (exportsections190ap::song (false));
    CHECK (late.size() == 4 && late[0] == (Section { "Start", 0, 2 * bar }) && late[1].name == "Verse");
    // no markers: nothing to split by
    auto plain = song;
    plain.markers().clear();
    CHECK (markerSections (plain).empty());
    CHECK (markerSections (lynx::Song {}).empty());
}

TEST (exportsections190ap_each_section_renders_as_a_selection)
{
    using namespace lynx::exportopts;
    const auto song = exportsections190ap::song();
    const auto bar = (std::int64_t) lynx::ppq * song.beatsPerBar;
    Options o;
    o.sections = true;
    o.loops = 3;
    CHECK (exportsSections (song, o));
    const auto each = sectionOptions (o);
    CHECK (each.range == Range::selection && each.loops == 1 && ! each.sections);
    std::int64_t total = 0;
    for (const auto& section : markerSections (song))
    {
        const auto part = makeSectionSong (song, section);
        CHECK (hasSelection (part) && part.loopStart == section.start && part.loopEnd == section.end);
        std::int64_t music = 0, loop = 0;
        const auto render = makeRenderSong (part, each, 0, music, loop);
        CHECK (music == section.end - section.start && loop == music);
        CHECK (render.songLength() == music);
        total += music;
        CHECK (part.playlist == song.playlist && part.channels == song.channels); // only the selection differs
    }
    CHECK (total == 8 * bar);

    // not split: the option off (the default), another range, the Master export, or a song without markers
    CHECK (! Options().sections && ! exportsSections (song, Options()));
    auto pattern = o;
    pattern.range = Range::pattern;
    CHECK (! exportsSections (song, pattern));
    auto master = o;
    master.master = true;
    CHECK (! exportsSections (song, master));
    auto plain = song;
    plain.markers().clear();
    CHECK (! exportsSections (plain, o));
}

TEST (exportsections190ap_file_names_and_counts)
{
    using namespace lynx::exportopts;
    CHECK (sectionBaseName ("Song", 1, 4, "Intro") == "Song - 01 Intro");
    CHECK (sectionBaseName ("Song", 4, 4, "Drop: A/B?") == "Song - 04 Drop_ A_B_");
    CHECK (sectionBaseName ("Song", 12, 12, "  Outro. ") == "Song - 12 Outro");
    CHECK (sectionBaseName ("Song", 7, 120, "x") == "Song - 007 x");
    CHECK (sectionBaseName ("Song", 2, 3, "") == "Song - 02");
    // with stems, a section's stems add theirs: "Song - 02 Verse - 3 Bass.wav"
    CHECK (stemFileName (sectionBaseName ("Song", 2, 4, "Verse"), 3, "Bass", ".wav") == "Song - 02 Verse - 3 Bass.wav");

    const auto song = exportsections190ap::song();
    Options o;
    CHECK (fileCount (song, o) == 1);
    o.sections = true;
    CHECK (fileCount (song, o) == 4);
    o.moreFormats = 1u << (int) Format::mp3;
    CHECK (fileCount (song, o) == 8); // WAV and MP3 of each
    o.moreFormats = 0;
    o.stems = Stems::mixerTracks;
    o.stemsIncludeMaster = true;
    const auto perSection = (int) stemTracks (song).size() + 1;
    CHECK (fileCount (song, o) == 4 * perSection);
    o.sections = false;
    CHECK (fileCount (song, o) == perSection);
}

TEST (exportsections190ap_options_text_keeps_the_new_switch_and_old_files_read)
{
    using namespace lynx::exportopts;
    Options o;
    o.sections = true;
    o.stems = Stems::channels;
    CHECK (fromText (toText (o)) == o);
    CHECK (fromText (toText (Options())) == Options());
    // a settings file written before this round has no "sections" line: off
    const auto old = "format=1\nmp3Kbps=192\nstems=1\n";
    CHECK (! fromText (old).sections && fromText (old).format == Format::mp3 && fromText (old).mp3Kbps == 192);
}

TEST (exportsections190ap_presets_and_same_as_last_time)
{
    using namespace lynx::exportopts;
    CHECK (cleanPresetName ("  Stems for\tmixing \n") == "Stems for mixing");
    CHECK (cleanPresetName ("[MP3 demo]") == "MP3 demo");
    CHECK (cleanPresetName (" [ ] ").empty() && cleanPresetName ("").empty());
    CHECK (cleanPresetName (std::string (100, 'x')).size() == 60);

    Options stems;
    stems.stems = Stems::mixerTracks;
    stems.wavBits = 32;
    stems.sections = true;
    Options demo;
    demo.format = Format::mp3;
    demo.mp3Kbps = 192;
    demo.normalize = true;
    demo.master = true; // (never part of a preset)
    std::vector<Preset> presets;
    presets = withPreset (presets, { "Stems for mixing", stems });
    presets = withPreset (presets, { "MP3 demo", demo });
    presets = withPreset (presets, { "   ", demo }); // no name: not added
    CHECK (presets.size() == 2 && presets[0].name == "MP3 demo" && presets[1].name == "Stems for mixing"); // by name
    CHECK (! presets[0].options.master && presets[0].options.mp3Kbps == 192 && presets[1].options == stems);
    // saving under a name that exists (any case) replaces it
    Options demo2 = demo;
    demo2.mp3Kbps = 320;
    demo2.master = false;
    presets = withPreset (presets, { "mp3 DEMO", demo2 });
    CHECK (presets.size() == 2 && presets[0].name == "mp3 DEMO" && presets[0].options.mp3Kbps == 320);

    const auto text = presetsToText (presets);
    CHECK (text.find ("[mp3 DEMO]\n") == 0 && text.find ("[Stems for mixing]\n") != std::string::npos);
    CHECK (presetsFromText (text) == presets);
    CHECK (presetsFromText (presetsToText (presetsFromText (text))) == presets);
    CHECK (presetsFromText ("").empty() && presetsFromText ("format=1\nno header\n").empty());
    // Windows line ends and a broken value: the preset still reads, with the default for that value
    const auto loose = presetsFromText ("[A]\r\nformat=3\r\nflacLevel=99\r\n\r\n[B]\r\nformat=banana\r\n");
    CHECK (loose.size() == 2 && loose[0].name == "A" && loose[0].options.format == Format::flac && loose[0].options.flacLevel == 8
           && loose[1].options.format == Format::wav);

    presets = withoutPreset (presets, "MP3 Demo");
    CHECK (presets.size() == 1 && presets[0].name == "Stems for mixing");
    CHECK (withoutPreset (presets, "nope").size() == 1);

    // "same as last time": the options and the file of the last export; the same folder, this project's name
    LastExport last;
    last.options = demo2;
    last.file = "C:\\Music\\Exports = final\\Old song.mp3";
    const auto back = lastExportFromText (lastExportToText (last));
    CHECK (back == last);
    CHECK (sameAsLastFile (last, "New song") == "C:\\Music\\Exports = final\\New song.mp3");
    last.file = "/home/me/out/Old.mp3";
    CHECK (sameAsLastFile (last, "A/B: mix?") == "/home/me/out/A_B_ mix_.mp3");
    CHECK (sameAsLastFile (last, "").empty());
    CHECK (sameAsLastFile (LastExport(), "Song").empty() && lastExportFromText ("").file.empty());
    CHECK (lastExportFromText ("") == LastExport());
}
