// --- GarageBand (.band) import, round 190ic (issue #184; core/BandFormat.h, core/BandImport.h). The songs here are
// written by the tests, record by record as the reader's notes describe them; no GarageBand project is committed. Real
// projects are imported only by the opt-in test at the end (LYNX_BAND_DIR).

#include "core/BandFormat.h"
#include "core/BandImport.h"
#include "core/ProjectKey.h"  // (rollup 0.1.30: the CMake test build needs these two itself)
#include "core/SimpleTracks.h"

namespace band190ic
{
using Bytes = std::vector<std::uint8_t>;

inline void put32 (Bytes& b, std::size_t at, std::uint32_t v)
{
    if (b.size() < at + 4)
        b.resize (at + 4, 0);
    for (int k = 0; k < 4; ++k)
        b[at + (std::size_t) k] = (std::uint8_t) (v >> (8 * k));
}
inline void put16 (Bytes& b, std::size_t at, std::uint16_t v)
{
    if (b.size() < at + 2)
        b.resize (at + 2, 0);
    b[at] = (std::uint8_t) v;
    b[at + 1] = (std::uint8_t) (v >> 8);
}
inline void putText (Bytes& b, std::size_t at, const std::string& s)
{
    if (b.size() < at + s.size())
        b.resize (at + s.size(), 0);
    std::memcpy (b.data() + at, s.data(), s.size());
}

struct Song
{
    std::vector<Bytes> records;

    // one record: tag (as stored), class, idx, ordinal and its payload
    void add (const char* tag, std::uint16_t classId, std::uint32_t idx, const Bytes& payload, std::uint32_t ordinal = 0)
    {
        Bytes r (36, 0);
        std::memcpy (r.data(), tag, 4);
        put16 (r, 6, classId);
        put32 (r, 8, idx);
        put32 (r, 0x0C, ordinal);
        put16 (r, 0x16, 2);
        put32 (r, 0x1C, (std::uint32_t) payload.size());
        r.insert (r.end(), payload.begin(), payload.end());
        records.push_back (std::move (r));
    }
    Bytes bytes() const
    {
        Bytes b { 0x23, 0x47, 0xC0, 0xAB, 0xAF, 0x06, 3, 0, 4, 0, 0, 0, 1, 0, 8, 0 };
        b.resize (24, 0);
        for (const auto& r : records)
            b.insert (b.end(), r.begin(), r.end());
        put32 (b, 0x10, (std::uint32_t) (b.size() - 24));
        return b;
    }
};

inline Bytes tail() { return { 0xF1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 0, 0, 0, 0, 0 }; }

inline Bytes placement (bool audio, int track, std::int64_t tick, std::uint32_t child, std::uint32_t file = 0, std::uint32_t k = 0)
{
    Bytes e (0x50, 0);
    e[0] = audio ? 0x24 : 0x20;
    put32 (e, 4, (std::uint32_t) (34560 + tick));
    put16 (e, 0x14, (std::uint16_t) track);
    e[0x17] = 0x89;
    e[0x37] = 0x8A;
    put32 (e, 0x20, audio ? 0xFFFFFFFFu : child * 4);
    put32 (e, 0x28, k);
    put32 (e, 0x2C, file * 4);
    return e;
}

inline Bytes noteEvent (std::int64_t tick, int key, int velocity, std::int64_t length)
{
    Bytes e (32, 0);
    e[0] = 0x90;
    put32 (e, 4, (std::uint32_t) (38400 + tick));
    e[0x0B] = (std::uint8_t) velocity;
    e[0x0C] = (std::uint8_t) key;
    e[0x0F] = 1;
    e[0x10] = 0x40;
    e[0x17] = 0x89;
    put32 (e, 0x1C, (std::uint32_t) length);
    return e;
}

inline Bytes cat (std::initializer_list<Bytes> parts)
{
    Bytes b;
    for (const auto& p : parts)
        b.insert (b.end(), p.begin(), p.end());
    return b;
}

// a sequence's name record (qeSM): name @raw 0x34, the region's length 0x3C after the (even) name end
inline Bytes sequenceName (const std::string& name, std::int64_t length)
{
    Bytes p (0x10, 0);
    put16 (p, 0x10, (std::uint16_t) name.size());
    putText (p, 0x12, name);
    const auto end = 0x12 + name.size() + (name.size() & 1);
    put32 (p, end + 0x3C, (std::uint32_t) length);
    p.resize (std::max<std::size_t> (p.size(), end + 0x100), 0);
    return p;
}

inline Bytes environment (const std::string& name)
{
    Bytes p (0xA0, 0);
    put16 (p, 0x5A, 4);
    put16 (p, 0x9E, (std::uint16_t) name.size());
    putText (p, 0xA0, name);
    p.resize (p.size() + 32, 0);
    return p;
}

inline Bytes strip (std::uint8_t type, int ordinal, const std::string& name, int fader, int pan)
{
    Bytes p (0x90, 0);
    p[4] = type;
    put16 (p, 6, (std::uint16_t) ordinal);
    put16 (p, 0x0A, 0x1235);
    putText (p, 0x3C, " " + name);
    p[0x55] = (std::uint8_t) fader;
    p[0x59] = (std::uint8_t) pan;
    p[0x77] = (std::uint8_t) fader;
    return p;
}

inline Bytes trackRow (std::uint32_t slot, bool muted)
{
    Bytes p (0x30, 0);
    p[0] = 1;
    p[2] = 0x14;
    p[3] = muted ? 1 : 0;
    put32 (p, 8, slot);
    return p;
}

// A small GarageBand for iOS song at 96 BPM in 3/4: an instrument track "Grand Piano" (fader 90, pan hard right) with a
// two-note region at bar 2 (one more note hidden past its end), a muted audio track "Audio 1" (fader 45) with a region
// of "Audio Recorder#01.aif" trimmed by 44100 samples, and a drum track "Live Rock Kit" with a kick and a snare.
inline Bytes song()
{
    Song s;
    Bytes gnos (0x400, 0);
    put32 (gnos, 0x3A6 - 36, 960000);
    put16 (gnos, 0x11E, 44100 - 15660);
    s.add ("gnoS", 3, 0xFFFFFFFFu, gnos);
    // tempo map (a change to 120 BPM at bar 5 of 3/4) and signature
    Bytes tempo = cat ({ Bytes (32, 0), Bytes (32, 0), tail() });
    tempo[0] = 0x60;
    put32 (tempo, 4, 38400);
    put32 (tempo, 0x10, 960000);
    tempo[32] = 0x60;
    put32 (tempo, 36, 38400 + 4 * 3 * 960);
    put32 (tempo, 32 + 0x10, 1200000);
    s.add ("qSvE", 3, 0, tempo);
    Bytes signature (80, 0);
    signature[0x0B] = 2;
    signature[0x0C] = 3;
    s.add ("qSvE", 1, 0, cat ({ signature, tail() }));
    // environment and mixer strips
    s.add ("ivnE", 0x14, 72u << 16, environment ("Audio 1"));
    s.add ("ivnE", 0x14, 104u << 16, environment ("Inst 1"));
    s.add ("ivnE", 0x14, 108u << 16, environment ("Inst 2"));
    s.add ("ivnE", 0x14, 92u << 16, environment ("Output"));
    s.add ("OCuA", 0x10, 0x240000, strip (0x40, 0, "Audio 1", 45, 64));
    s.add ("OCuA", 0x10, 0x240000, strip (0x43, 0, "Inst 1", 90, 127));
    s.add ("OCuA", 0x10, 0x240000, strip (0x43, 1, "Inst 2", 100, 0));
    // arrange rows: Inst 1, Audio 1 (muted), Inst 2, Output
    auto row = [&s] (int index, std::uint32_t slot, bool muted) {
        s.add ("karT", 0x04, 0x40000, trackRow (slot, muted));
        put16 (s.records.back(), 0x12, (std::uint16_t) index); // (the row's place in the arrangement)
    };
    row (0, 104, false);
    row (1, 72, true);
    row (2, 108, false);
    row (3, 92, false);
    // sequences: 1 the arrangement, 2 the piano region, 3 the drum region, 4 an automation holder
    s.add ("qeSM", 0x17, 1u << 18, sequenceName ("Root Folder", 0));
    s.add ("qeSM", 0x17, 2u << 18, sequenceName ("Grand Piano", 2 * 3 * 960));
    s.add ("qeSM", 0x17, 3u << 18, sequenceName ("Live Rock Kit", 3 * 960));
    s.add ("qeSM", 0x17, 4u << 18, sequenceName ("*Automation", 0));
    s.add ("qSvE", 0x17, 1u << 18,
           cat ({ placement (false, 1, 3 * 960, 2), placement (true, 2, 960, 0, 5, 0), placement (false, 3, 0, 3), tail() }));
    s.add ("qSvE", 0x17, 2u << 18,
           cat ({ noteEvent (0, 60, 100, 960), noteEvent (960, 64, 64, 480), noteEvent (7 * 960, 67, 90, 480), tail() }));
    s.add ("qSvE", 0x17, 3u << 18, cat ({ noteEvent (0, 36, 120, 240), noteEvent (960, 38, 100, 240), tail() }));
    s.add ("qSvE", 0x17, 5u << 18, cat ({ placement (false, 1, 0, 4), tail() })); // (automation: not the arrangement)
    // the audio region (file 5, region 0) and its file
    Bytes region (0x4C, 0);
    put32 (region, 0x06, 44100);
    put32 (region, 0x16, 88200);
    put16 (region, 0x4A, 14);
    putText (region, 0x4C, "Audio Recorder");
    region.resize (region.size() + 126, 0);
    s.add ("gRuA", 0x0B, 5u << 18, region, 0);
    Bytes file (0x0A, 0);
    const std::string name = "Audio Recorder#01.aif";
    put16 (file, 8, (std::uint16_t) name.size());
    for (std::size_t k = 0; k < name.size(); ++k)
        put16 (file, 0x0A + 2 * k, (std::uint16_t) name[k]);
    file.resize (file.size() + 64, 0);
    s.add ("lFuA", 0x0A, 5u << 18, file);
    return s.bytes();
}

inline std::string base64 (const Bytes& b)
{
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < b.size(); i += 3)
    {
        const std::uint32_t v = (std::uint32_t) b[i] << 16 | (i + 1 < b.size() ? (std::uint32_t) b[i + 1] << 8 : 0u) | (i + 2 < b.size() ? b[i + 2] : 0u);
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += i + 1 < b.size() ? abc[(v >> 6) & 63] : '=';
        out += i + 2 < b.size() ? abc[v & 63] : '=';
        if (i % 57 == 54)
            out += "\n\t\t";
    }
    return out;
}

// GarageBand for iOS's projectData: a keyed archive whose logic model holds the song as NSData
inline Bytes keyedArchive (const Bytes& songBytes)
{
    const auto text = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                   "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n<dict>\n\t<key>$archiver</key>\n"
                                   "\t<string>NSKeyedArchiver</string>\n\t<key>$objects</key>\n\t<array>\n\t\t<string>$null</string>\n\t\t<dict>\n"
                                   "\t\t\t<key>NS.data</key>\n\t\t\t<data>\n\t\t")
                      + base64 ({ 1, 2, 3, 4 }) + "\n\t\t\t</data>\n\t\t</dict>\n\t\t<dict>\n\t\t\t<key>NS.data</key>\n\t\t\t<data>\n\t\t"
                      + base64 (songBytes) + "\n\t\t\t</data>\n\t\t</dict>\n\t</array>\n\t<key>$version</key>\n\t<integer>100000</integer>\n</dict>\n</plist>\n";
    return Bytes (text.begin(), text.end());
}

inline Bytes metadata()
{
    const std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
                             "\t<key>com_apple_garageband_metadata_songKey</key>\n\t<string>A</string>\n"
                             "\t<key>com_apple_garageband_metadata_songGender</key>\n\t<string>minor</string>\n"
                             "\t<key>com_apple_garageband_metadata_songSignatureNominator</key>\n\t<integer>3</integer>\n"
                             "\t<key>com_apple_garageband_metadata_songSignatureDeNominator</key>\n\t<integer>4</integer>\n"
                             "\t<key>com_apple_garageband_metadata_songTitle</key>\n\t<string>My Song 3</string>\n"
                             "\t<key>com_apple_garageband_metadata_songTempo</key>\n\t<integer>96</integer>\n</dict>\n</plist>\n";
    return Bytes (text.begin(), text.end());
}

// GarageBand 10 for Mac's MetaData.plist is a binary plist: { SongKey = "Eb", SongGenderKey = "major", SampleRate = 48000 }
inline Bytes binaryMetadata()
{
    Bytes b { 'b', 'p', 'l', 'i', 's', 't', '0', '0' };
    std::vector<std::size_t> offsets;
    auto object = [&] (std::initializer_list<std::uint8_t> bytes) {
        offsets.push_back (b.size());
        b.insert (b.end(), bytes);
    };
    object ({ 0xD3, 1, 2, 3, 4, 5, 6 });                              // 0: dict of 3 (keys 1..3, values 4..6)
    object ({ 0x57, 'S', 'o', 'n', 'g', 'K', 'e', 'y' });             // 1
    object ({ 0x5D, 'S', 'o', 'n', 'g', 'G', 'e', 'n', 'd', 'e', 'r', 'K', 'e', 'y' }); // 2
    object ({ 0x5A, 'S', 'a', 'm', 'p', 'l', 'e', 'R', 'a', 't', 'e' }); // 3
    object ({ 0x52, 'E', 'b' });                                     // 4
    object ({ 0x55, 'm', 'a', 'j', 'o', 'r' });                      // 5
    object ({ 0x11, 0xBB, 0x80 });                                   // 6: 48000
    const auto table = b.size();
    for (const auto o : offsets)
        b.push_back ((std::uint8_t) o);
    Bytes trailer (32, 0);
    trailer[6] = 1; // offset size
    trailer[7] = 1; // ref size
    trailer[15] = (std::uint8_t) offsets.size();
    trailer[23] = 0; // top object
    trailer[31] = (std::uint8_t) table;
    b.insert (b.end(), trailer.begin(), trailer.end());
    return b;
}

inline lynx::BandImportOptions options (lynx::FlpImportReport* report, std::map<std::string, Bytes>* files)
{
    lynx::BandImportOptions o;
    o.packageFolder = "/songs/Jingle.band";
    o.common.report = report;
    o.common.fileExists = [files] (const std::string& p) { return files->count (p) > 0; };
    o.common.readFile = [files] (const std::string& p) {
        const auto it = files->find (p);
        return it == files->end() ? Bytes() : it->second;
    };
    return o;
}
} // namespace band190ic

TEST (band190ic_reads_the_song)
{
    using namespace lynx;
    const auto bytes = band190ic::song();
    CHECK (band::isSongData (bytes.data(), bytes.size()));
    std::string error;
    const auto p = band::parseSong (bytes, &error);
    CHECK (p.has_value());
    if (! p)
        return;
    CHECK (std::abs (p->bpm - 96.0) < 1e-9 && p->numerator == 3 && p->denominator == 4 && p->sampleRate == 44100.0);
    CHECK (p->tempoChanges.size() == 1 && p->tempoChanges[0].first == 4 * 3 * 960 && std::abs (p->tempoChanges[0].second - 120.0) < 1e-9);
    // three tracks (the Output row is not one), in arrange order
    CHECK (p->tracks.size() == 3);
    if (p->tracks.size() != 3)
        return;
    const auto& piano = p->tracks[0];
    CHECK (piano.name == "Grand Piano" && ! piano.audio && piano.volume == 90 && piano.pan == 127 && piano.volumeKnown && ! piano.muted);
    CHECK (piano.regions.size() == 1 && piano.regions[0].start == 3 * 960 && piano.regions[0].length == 6 * 960);
    CHECK (piano.regions[0].notes.size() == 2 && piano.regions[0].hiddenNotes == 1);
    if (piano.regions[0].notes.size() == 2)
        CHECK (piano.regions[0].notes[1].tick == 960 && piano.regions[0].notes[1].key == 64 && piano.regions[0].notes[1].velocity == 64
               && piano.regions[0].notes[1].length == 480);
    const auto& audio = p->tracks[1];
    CHECK (audio.audio && audio.muted && audio.volume == 45 && audio.name == "Audio Recorder");
    CHECK (audio.regions.size() == 1 && audio.regions[0].file == "Audio Recorder#01.aif" && audio.regions[0].trimSamples == 44100
           && audio.regions[0].lengthSamples == 88200 && audio.regions[0].start == 960);
    const auto& kit = p->tracks[2];
    CHECK (kit.name == "Live Rock Kit" && kit.volume == 100 && kit.pan == 0 && kit.regions.size() == 1 && kit.regions[0].notes.size() == 2);
}

TEST (band190ic_projectdata_wrappers_and_legacy)
{
    using namespace lynx;
    const auto bytes = band190ic::song();
    // raw (GarageBand 10's Alternatives/000/ProjectData) and inside the iOS keyed archive
    CHECK (band::songFromProjectData (bytes) == bytes);
    CHECK (band::songFromProjectData (band190ic::keyedArchive (bytes)) == bytes);
    // GarageBand 6–11's older layout (the same magic, no record run): refused with a reason, never guessed at
    auto legacy = bytes;
    band190ic::put32 (legacy, 0x10, 4);
    std::string error;
    CHECK (! band::songFromProjectData (legacy, &error) && error.find ("GarageBand 6") != std::string::npos);
    CHECK (! band::songFromProjectData (band190ic::keyedArchive (legacy), &error) && error.find ("GarageBand 6") != std::string::npos);
    CHECK (! band::parseSong (legacy, &error));
    // not a project at all; a damaged record run
    CHECK (! band::songFromProjectData ({ 1, 2, 3 }, &error));
    auto cut = bytes;
    cut.resize (cut.size() - 7);
    band190ic::put32 (cut, 0x10, (std::uint32_t) (cut.size() - 24));
    CHECK (! band::parseSong (cut, &error));
    // every truncation of the song is refused or read without a crash
    for (std::size_t n = 0; n < bytes.size(); n += 97)
    {
        band190ic::Bytes part (bytes.begin(), bytes.begin() + (std::ptrdiff_t) n);
        (void) band::parseSong (part);
        (void) band::songFromProjectData (band190ic::keyedArchive (part));
    }
}

TEST (band190ic_metadata_plists)
{
    using namespace lynx;
    band::Project p;
    band::applyMetadata (band190ic::metadata(), p);
    CHECK (p.keyRoot == 9 && p.keyMinor && p.numerator == 3 && p.denominator == 4);
    band::Project mac;
    band::applyMetadata (band190ic::binaryMetadata(), mac);
    CHECK (mac.keyRoot == 3 && ! mac.keyMinor && mac.sampleRate == 48000.0);
    CHECK (band::keyRootOf ("F#") == 6 && band::keyRootOf ("Bb") == 10 && band::keyRootOf ("H") == -1);
}

TEST (band190ic_imports_simple_tracks)
{
    using namespace lynx;
    std::map<std::string, band190ic::Bytes> files;
    files["/songs/Jingle.band/projectData"] = band190ic::keyedArchive (band190ic::song());
    files["/songs/Jingle.band/Output/metadata.plist"] = band190ic::metadata();
    files["/songs/Jingle.band/Media/Audio Recorder#01.aif"] = { 0 };
    FlpImportReport report;
    std::string error;
    const auto song = importBandPackage (band190ic::options (&report, &files), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    CHECK (song->info.title == "Jingle" && std::abs (song->bpm - 96.0) < 1e-9 && song->beatsPerBar == 3);
    const auto key = projectkey::get (*song);
    CHECK (key && key->root == 9 && projectkey::isMinor (*key));
    // one Simple track each: an instrument, an audio track, drums
    CHECK (simpletracks::kindOf (*song, 0) == simpletracks::Kind::instrument && simpletracks::kindOf (*song, 1) == simpletracks::Kind::audio
           && simpletracks::kindOf (*song, 2) == simpletracks::Kind::drums);
    CHECK (song->playlistTracks[0].name == "Grand Piano" && song->playlistTracks[1].muted && ! song->playlistTracks[0].muted);
    // the piano's region: a pattern clip at bar 2 of 3/4, its two notes on the track's synth (the Bell preset)
    const Clip* pianoClip = nullptr;
    const Clip* audioClip = nullptr;
    int drumClips = 0;
    for (const auto& c : song->playlist)
    {
        if (c.track == 0 && c.kind == ClipKind::pattern)
            pianoClip = &c;
        if (c.track == 1 && c.kind == ClipKind::audio)
            audioClip = &c;
        drumClips += c.track == 2 && c.kind == ClipKind::pattern;
    }
    CHECK (pianoClip != nullptr && drumClips == 1);
    if (pianoClip != nullptr)
    {
        CHECK (pianoClip->start == 3 * 960 && pianoClip->length == 6 * 960);
        const auto& p = song->patterns[(std::size_t) pianoClip->pattern];
        CHECK (p.notes.size() == 2 && p.lengthTicks == 6 * 960);
        if (p.notes.size() == 2)
        {
            const auto& c = song->channels[(std::size_t) p.notes[0].channel];
            CHECK (c.instrument == InstrumentKind::synth && c.mixerTrack == simpletracks::insertOf (*song, 0));
            CHECK (std::abs (p.notes[1].velocity - 64.0f / 127.0f) < 1e-6f && p.notes[1].key == 64);
        }
    }
    // the audio region: its file from Media, trimmed by one second (96 BPM: 1.6 beats), two seconds long
    CHECK (audioClip != nullptr);
    if (audioClip != nullptr)
    {
        const auto& c = song->channels[(std::size_t) audioClip->channel];
        CHECK (c.instrument == InstrumentKind::audioClip && c.samplePath == "/songs/Jingle.band/Media/Audio Recorder#01.aif");
        CHECK (audioClip->start == 960 && audioClip->offset == 1536 && audioClip->length == 3072);
    }
    // the faders: 0 dB, the audio track's 45 (−12 dB), pan hard right on the piano and hard left on the kit
    const auto& pianoInsert = song->mixer[(std::size_t) simpletracks::insertOf (*song, 0)];
    const auto& audioInsert = song->mixer[(std::size_t) simpletracks::insertOf (*song, 1)];
    const auto& kitInsert = song->mixer[(std::size_t) simpletracks::insertOf (*song, 2)];
    CHECK (std::abs (pianoInsert.volume - faderPosition (1.0f)) < 1e-5f && pianoInsert.pan == 1.0f);
    CHECK (std::abs (audioInsert.volume - faderPosition (0.25f)) < 1e-5f && kitInsert.pan == -1.0f);
    // the kit's kick and snare on the built-in drums
    bool kick = false, snare = false;
    for (const auto& p : song->patterns)
        for (const auto& n : p.notes)
        {
            const auto kind = song->channels[(std::size_t) n.channel].instrument;
            kick = kick || kind == InstrumentKind::kick;
            snare = snare || kind == InstrumentKind::snare;
        }
    CHECK (kick && snare);
    // the report: what was approximated and what wasn't converted
    CHECK (report.approximated.count ("GarageBand instrument \"Grand Piano\" (LYNX's built-in synth, Bell preset)") == 1);
    CHECK (report.notReproduced.count ("GarageBand: tempo changes (the song plays at its first tempo)") == 1 && report.missingSamples == 0);

    // a missing file is listed; a located one (the Missing files window) is used on the next import
    files.erase ("/songs/Jingle.band/Media/Audio Recorder#01.aif");
    report = {};
    (void) importBandPackage (band190ic::options (&report, &files));
    CHECK (report.missingSamples == 1 && report.missingFiles.size() == 1
           && report.missingFiles[0].path == "/songs/Jingle.band/Media/Audio Recorder#01.aif");
    auto o = band190ic::options (&report, &files);
    o.common.relocations["/songs/Jingle.band/Media/Audio Recorder#01.aif"] = "/found/take.aif";
    report = {};
    const auto again = importBandPackage (o);
    CHECK (again && report.missingSamples == 0);
    if (again)
        CHECK (std::any_of (again->channels.begin(), again->channels.end(), [] (const ChannelDef& c) { return c.samplePath == "/found/take.aif"; }));
}

TEST (band190ic_instrument_names)
{
    using namespace lynx;
    CHECK (bandPresetFor ("Classical Grand") == "Bell" && bandPresetFor ("Hip Hop Sub Bass") == "808 Bass" && bandPresetFor ("Fingerstyle Bass") == "Reese Bass");
    CHECK (bandPresetFor ("String Ensemble") == "Pad" && bandPresetFor ("Steel String Acoustic Guitar") == "Pluck");
    CHECK (bandPresetFor ("Playful Melody") == "Lead" && bandPresetFor ("???") == "Default");
    CHECK (bandDrumKitName ("Live Rock Kit") && bandDrumKitName ("House Drum Machine") && ! bandDrumKitName ("Classical Grand"));
    CHECK (bandDrumFor (36) == InstrumentKind::kick && bandDrumFor (38) == InstrumentKind::snare && bandDrumFor (39) == InstrumentKind::clap
           && bandDrumFor (42) == InstrumentKind::hat);
}

// Opt-in (LYNX_BAND_DIR=folder): every .band below the folder is imported and summarised. Real projects stay on the
// developer's disk; none is committed.
TEST (band190ic_real_projects)
{
    using namespace lynx;
    const char* dir = std::getenv ("LYNX_BAND_DIR");
    if (dir == nullptr)
        return;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir))
    {
        if (! entry.is_directory() || entry.path().extension() != ".band")
            continue;
        FlpImportReport report;
        BandImportOptions o;
        o.packageFolder = entry.path().string();
        o.common.report = &report;
        o.common.fileExists = [] (const std::string& p) { return std::filesystem::exists (p); };
        o.common.readFile = [] (const std::string& p) {
            std::ifstream file (p, std::ios::binary);
            return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char>());
        };
        std::string error;
        const auto song = importBandPackage (o, &error);
        if (! song)
        {
            std::printf ("  %s: not imported: %s\n", entry.path().filename().string().c_str(), error.c_str());
            continue;
        }
        int notes = 0, audioClips = 0, patternClips = 0;
        for (const auto& p : song->patterns)
            notes += (int) p.notes.size();
        for (const auto& c : song->playlist)
        {
            audioClips += c.kind == ClipKind::audio;
            patternClips += c.kind == ClipKind::pattern;
        }
        std::printf ("  %s: bpm %.2f %d/%d, %d tracks, %d pattern clips (%d notes), %d audio clips, %d files missing, %zu unsupported kinds\n",
                     entry.path().filename().string().c_str(), song->bpm, song->beatsPerBar, song->beatUnit,
                     simpletracks::nextEmptyTrack (*song), patternClips, notes, audioClips, report.missingSamples, report.unsupported.size() + report.notReproduced.size());
    }
}
