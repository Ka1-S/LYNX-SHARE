// Round 190jm (issue #184, the reporter's GarageBand for iOS project): GarageBand for iOS 2.3+ region events (the
// region-settings event 0x32 and extended controllers with extension words, which stopped the reader before any note),
// the channel strips' plugin records (patch name and category, the Sampler's instrument file) and the user's own
// Sampler instruments (.exs + recordings in Media/Sampler). Synthetic bytes only, built with round 190ic's helpers; no
// GarageBand file is committed.

namespace band190jm
{
using band190ic::Bytes;
using band190ic::cat;
using band190ic::put16;
using band190ic::put32;
using band190ic::putText;

// GarageBand for iOS 2.3+'s region-settings event: 32 bytes, its second word marked 0x88 @+0x17
inline Bytes settingsEvent()
{
    Bytes e (32, 0);
    e[0] = 0x32;
    put32 (e, 4, 38400);
    e[0x0C] = 7;
    e[0x17] = 0x88;
    return e;
}

// an extension word (byte +7 = 0xA3 / 0xA4 / 0xA7)
inline Bytes extension (std::uint8_t mark)
{
    Bytes e (16, 0);
    e[0] = 0x11;
    e[7] = mark;
    return e;
}

// an extended controller / pressure event (byte +1 = 0x40) with one extension word
inline Bytes extendedController (std::uint8_t status, std::int64_t tick)
{
    Bytes e (16, 0);
    e[0] = status;
    e[1] = 0x40;
    put32 (e, 4, (std::uint32_t) (38400 + tick));
    e[0x0F] = 1;
    return cat ({ e, extension (0xA4) });
}

// an extended note (byte +1 = 0x40) with two extension words
inline Bytes extendedNote (std::int64_t tick, int key, int velocity, std::int64_t length)
{
    auto n = band190ic::noteEvent (tick, key, velocity, length);
    n[1] = 0x40;
    return cat ({ n, extension (0xA3), extension (0xA4) });
}

// a strip's plugin record (UCuA): slot 2 = the instrument (setting file @pay+0x0E, plugin name @raw+0x9C), slot 0x10 =
// the patch (file @pay+0x10, category @pay+0x50)
inline void plugin (band190ic::Song& s, int strip, int slot, const std::string& first, const std::string& second)
{
    Bytes p (0x100, 0);
    put16 (p, 0x0A, 0x1235);
    if (slot == 2)
    {
        putText (p, 0x0E, first);
        putText (p, 0x9C - 36, second);
    }
    else
    {
        putText (p, 0x10, first);
        putText (p, 0x50, second);
    }
    s.add ("UCuA", 0x0E, 0x240000, p, (std::uint32_t) strip << 16);
    put16 (s.records.back(), 0x12, (std::uint16_t) slot);
}

// Three instrument tracks: "Inst 1" plays a Sampler instrument the user made ("Voice.exs"), its region starting with the
// settings event, an extended controller, an extended note, a plain note and an extended pressure event; "Inst 2" is a
// "Flexi" patch of the Drums category (no drum word in its name) with a kick and a snare; "Inst 3" ("Warm Keys") has no
// region.
inline Bytes song()
{
    band190ic::Song s;
    Bytes gnos (0x400, 0);
    put32 (gnos, 0x3A6 - 36, 1100000);
    put16 (gnos, 0x11E, 44100 - 15660);
    s.add ("gnoS", 3, 0xFFFFFFFFu, gnos);
    s.add ("ivnE", 0x14, 104u << 16, band190ic::environment ("Inst 1"));
    s.add ("ivnE", 0x14, 108u << 16, band190ic::environment ("Inst 2"));
    s.add ("ivnE", 0x14, 112u << 16, band190ic::environment ("Inst 3"));
    s.add ("OCuA", 0x10, 0x240000, band190ic::strip (0x43, 0, "Inst 1", 127, 64), 7u << 16);
    plugin (s, 7, 2, "Voice.exs", "Sampler");
    plugin (s, 7, 0x10, "Sampler.cst", "Sampler");
    s.add ("OCuA", 0x10, 0x240000, band190ic::strip (0x43, 1, "Inst 2", 90, 64), 8u << 16);
    plugin (s, 8, 2, "Flexi Kit.exs", "Sampler");
    plugin (s, 8, 0x10, "Flexi.patch", "Drums");
    s.add ("OCuA", 0x10, 0x240000, band190ic::strip (0x43, 2, "Inst 3", 90, 64), 9u << 16);
    plugin (s, 9, 2, "", "Alchemy");
    plugin (s, 9, 0x10, "Warm Keys.cst", "Pianos and Keyboards");
    auto row = [&s] (int index, std::uint32_t slot) {
        s.add ("karT", 0x04, 0x40000, band190ic::trackRow (slot, false));
        put16 (s.records.back(), 0x12, (std::uint16_t) index);
    };
    row (0, 104);
    row (1, 108);
    row (2, 112);
    s.add ("qeSM", 0x17, 1u << 18, band190ic::sequenceName ("Root Folder", 0));
    s.add ("qeSM", 0x17, 2u << 18, band190ic::sequenceName ("Voice", 6 * 960));
    s.add ("qeSM", 0x17, 3u << 18, band190ic::sequenceName ("Flexi", 3 * 960));
    s.add ("qSvE", 0x17, 1u << 18, cat ({ band190ic::placement (false, 1, 0, 2), band190ic::placement (false, 2, 0, 3), band190ic::tail() }));
    s.add ("qSvE", 0x17, 2u << 18,
           cat ({ settingsEvent(), extendedController (0xB0, 0), extendedNote (0, 50, 100, 960), band190ic::noteEvent (960, 52, 80, 480),
                  extendedController (0xD3, 1200), band190ic::tail() }));
    s.add ("qSvE", 0x17, 3u << 18, cat ({ settingsEvent(), band190ic::noteEvent (0, 36, 120, 240), extendedNote (960, 38, 100, 240), band190ic::tail() }));
    return s.bytes();
}

// a Sampler instrument file (.exs, little-endian): the instrument, one zone (root key 48, −6 cents, keys 40..80) and its sample
inline Bytes exs (const std::string& sample)
{
    auto chunk = [] (std::uint8_t type, const std::string& name, Bytes data) {
        Bytes c (84, 0);
        c[0] = 1;
        c[1] = 1;
        c[3] = type;
        put32 (c, 4, (std::uint32_t) data.size());
        putText (c, 16, "TBOS");
        putText (c, 20, name);
        return cat ({ c, data });
    };
    Bytes zone (148, 0);
    zone[1] = 48;
    zone[2] = (std::uint8_t) (std::int8_t) -6;
    zone[6] = 40;
    zone[7] = 80;
    zone[10] = 127;
    put32 (zone, 0x5C, 0);
    return cat ({ chunk (0, "Voice.exs", Bytes (88, 0)), chunk (1, "Zone #1", zone), chunk (3, sample, Bytes (600, 0)) });
}
} // namespace band190jm

TEST (band190jm_reads_ios23_regions_and_plugins)
{
    using namespace lynx;
    const auto p = band::parseSong (band190jm::song());
    CHECK (p.has_value() && p->tracks.size() == 3);
    if (! p || p->tracks.size() != 3)
        return;
    const auto& voice = p->tracks[0];
    CHECK (voice.regions.size() == 1);
    if (voice.regions.size() == 1)
    {
        const auto& r = voice.regions[0];
        CHECK (! r.patternContent && r.notes.size() == 2 && r.controllerEvents == 2);
        if (r.notes.size() == 2)
            CHECK (r.notes[0].key == 50 && r.notes[0].length == 960 && r.notes[1].tick == 960 && r.notes[1].key == 52 && r.notes[1].velocity == 80);
    }
    CHECK (voice.unit == "Sampler" && voice.samplerInstrument == "Voice.exs" && voice.patch == "Sampler" && voice.category == "Sampler");
    const auto& kit = p->tracks[1];
    CHECK (kit.name == "Flexi" && kit.category == "Drums" && kit.patch == "Flexi" && bandDrumTrack (kit) && ! bandDrumKitName (kit.name));
    CHECK (kit.regions.size() == 1 && kit.regions[0].notes.size() == 2 && ! kit.regions[0].patternContent);
    // a track without regions takes its patch's name; an Alchemy instrument has no Sampler file
    CHECK (p->tracks[2].name == "Warm Keys" && p->tracks[2].unit == "Alchemy" && p->tracks[2].samplerInstrument.empty() && ! bandDrumTrack (p->tracks[2]));

    // the .exs reader
    const auto zones = band::readExs (band190jm::exs ("Voice.wav"));
    CHECK (zones.has_value() && zones->size() == 1);
    if (zones && zones->size() == 1)
        CHECK ((*zones)[0].sample == "Voice.wav" && (*zones)[0].rootKey == 48 && (*zones)[0].fineTune == -6 && (*zones)[0].lowKey == 40
               && (*zones)[0].highKey == 80 && (*zones)[0].highVelocity == 127);
    auto broken = band190jm::exs ("Voice.wav");
    broken.pop_back();
    CHECK (! band::readExs (broken).has_value() && ! band::readExs ({ 1, 2, 3 }).has_value());
}

TEST (band190jm_imports_the_users_sampler)
{
    using namespace lynx;
    std::map<std::string, band190ic::Bytes> files;
    files["/songs/Jingle.band/projectData"] = band190ic::keyedArchive (band190jm::song());
    files["/songs/Jingle.band/Media/Sampler/Sampler Instruments/Voice.exs"] = band190jm::exs ("Voice.wav");
    files["/songs/Jingle.band/Media/Sampler/Sampler Files/Voice.wav"] = { 0 };
    FlpImportReport report;
    std::string error;
    const auto song = importBandPackage (band190ic::options (&report, &files), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    const ChannelDef* sampler = nullptr;
    int kicks = 0, synths = 0;
    for (const auto& c : song->channels)
    {
        if (c.instrument == InstrumentKind::sampler)
            sampler = &c;
        kicks += c.instrument == InstrumentKind::kick;
        synths += c.instrument == InstrumentKind::synth;
    }
    CHECK (sampler != nullptr && kicks == 1 && synths == 1); // (Voice: the user's sampler; Flexi: drums; Warm Keys: synth)
    if (sampler != nullptr)
    {
        CHECK (sampler->samplePath == "/songs/Jingle.band/Media/Sampler/Sampler Files/Voice.wav" && sampler->zones.size() == 1);
        if (sampler->zones.size() == 1)
            CHECK (sampler->zones[0].rootKey == 48 && std::abs (sampler->zones[0].tune + 0.06f) < 1e-6f && sampler->zones[0].lowKey == 40
                   && sampler->zones[0].highKey == 80);
    }
    int notes = 0;
    for (const auto& pt : song->patterns)
        notes += (int) pt.notes.size();
    CHECK (notes == 4 && report.missingSamples == 0);

    // the recording missing: listed, and the track plays the built-in synth
    files.erase ("/songs/Jingle.band/Media/Sampler/Sampler Files/Voice.wav");
    FlpImportReport again;
    const auto fallback = importBandPackage (band190ic::options (&again, &files));
    CHECK (fallback.has_value() && again.missingSamples == 1 && again.missingFiles.size() == 1);
    if (fallback)
        CHECK (std::none_of (fallback->channels.begin(), fallback->channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::sampler; }));
}
