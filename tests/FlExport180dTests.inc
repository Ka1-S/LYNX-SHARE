// --- Round 180d: Export to FL Studio, automation clip trims (idea by Tookolld) ------------------------------------------

#include <map>

TEST (render180d_export_trims_automation_clips_shorter_than_their_content)
{
    // FL reads -1 as "the whole content", whatever the item's length says: a short clip of a long automation made
    // Thief 208 bars long. A clip showing all its content stays -1, as in FL's own files.
    using namespace lynx;
    auto song = Song::makeDefault();
    Automation a;
    a.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    a.points = { { 0, 0.5f, 0.0f }, { ppq * 100, 0.5f, 0.0f } };
    song.automations.push_back (a);
    const auto index = (int) song.automations.size() - 1;
    auto addClip = [&] (std::int64_t start, std::int64_t length, std::int64_t offset) {
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = index;
        c.track = 3;
        c.start = start;
        c.length = length;
        c.offset = offset;
        song.playlist.push_back (c);
    };
    song.playlist.clear();
    addClip (0, ppq * 100, 0);        // all of it
    addClip (ppq * 200, ppq * 4, 0);  // its first 4 beats
    addClip (ppq * 300, ppq * 4, ppq * 8); // 4 beats from beat 8

    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (! project.arrangements.empty());
    std::map<double, std::pair<float, float>> byBeat;
    const auto fileBeat = (double) project.header.ppq;
    for (const auto& item : project.arrangements[0].items)
        if (item.kind == flp::PlaylistItem::Kind::channelClip)
            byBeat[item.position / fileBeat] = { item.startOffset, item.endOffset };
    CHECK (byBeat.size() == 3);
    CHECK (byBeat[0.0].first == -1.0f && byBeat[0.0].second == -1.0f);
    CHECK (byBeat[200.0].first == 0.0f && byBeat[200.0].second == 4.0f);
    CHECK (byBeat[300.0].first == 8.0f && byBeat[300.0].second == 12.0f);
}

TEST (render180d_export_writes_fl_stretch_modes)
{
    using namespace lynx;
    auto writtenMode = [] (const SamplerSettings& sampler) {
        auto song = Song::makeDefault();
        song.channels.resize (1);
        song.channels[0].instrument = InstrumentKind::sampler;
        song.channels[0].samplePath = "C:/x.wav";
        song.channels[0].sampler = sampler;
        flpexport::Options options;
        flpexport::Report report;
        const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
        return project.channels.at (0).stretchMode;
    };

    // an imported mode goes back as it came, with or without a TIME, and for each of FL's general modes
    for (const auto mode : { -2, -1, 1, 5, 6, 2, 3, 7 })
    {
        SamplerSettings s;
        applyFlStretchPanel (s, mode, 0, 0, 0.0f, 0.0, 120.0);
        CHECK (s.keepPitch && s.flStretchMode == mode);
        CHECK (writtenMode (s) == mode);
        s.stretchBeats = 4.0f;
        CHECK (writtenMode (s) == mode);
    }

    // made in LYNX: General is FL's Stretch (−1, not Stretch Pro's −2); no keepPitch is Resample
    SamplerSettings lynxMade;
    lynxMade.keepPitch = true;
    lynxMade.stretchBeats = 4.0f;
    CHECK (writtenMode (lynxMade) == -1);
    lynxMade.stretchMethod = StretchMethod::mono;
    CHECK (writtenMode (lynxMade) == 2);
    lynxMade.stretchMethod = StretchMethod::transients;
    CHECK (writtenMode (lynxMade) == 7);
    lynxMade.keepPitch = false;
    CHECK (writtenMode (lynxMade) == 0);

    // the channel's method changed after the import: the method wins over the remembered mode
    SamplerSettings changed;
    applyFlStretchPanel (changed, 5, 0, 0, 0.0f, 0.0, 120.0);
    changed.stretchMethod = StretchMethod::mono;
    CHECK (writtenMode (changed) == 2);

    // FL's Resample stays Resample, whatever an earlier mode was
    SamplerSettings resample;
    applyFlStretchPanel (resample, 0, 700, 0, 0.0f, 0.0, 120.0);
    CHECK (! resample.keepPitch && resample.flStretchMode == 0 && writtenMode (resample) == 0);
}

TEST (render180d_export_puts_fl_stretch_pitch_back_on_its_knob)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.resize (1);
    auto& def = song.channels[0];
    def.instrument = InstrumentKind::sampler;
    def.samplePath = "C:/x.wav";
    def.misc.rootNote = 60;
    applyFlStretchPanel (def.sampler, -1, 700, 0, 0.0f, 0.0, 120.0);
    def.sampler.pitch += 3.0f; // the header's own part
    flpexport::Options options;
    flpexport::Report report;
    auto written = flp::parseProject (flpexport::exportFlp (song, options, report)).channels.at (0);
    CHECK (written.stretchPitch == 700 && written.pitchCents == 300);

    // Resample ignores that knob in FL: all of the pitch goes in the header
    def.sampler.keepPitch = false;
    written = flp::parseProject (flpexport::exportFlp (song, options, report)).channels.at (0);
    CHECK (written.stretchPitch == 0 && written.pitchCents == 1000);
}

TEST (render180d_export_keeps_a_channels_fl_plugin_flags)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.resize (1);
    song.channels[0].instrument = InstrumentKind::sampler;
    song.channels[0].samplePath = "C:/x.wav";
    flpexport::Options options;
    flpexport::Report report;
    const auto written = [&] { return flp::parseProject (flpexport::exportFlp (song, options, report)).channels.at (0).wrapperFlags; };
    const auto own = written();
    CHECK (own > 0);
    song.channels[0].flWrapperFlags = 0x170;
    CHECK (written() == 0x170);
}
