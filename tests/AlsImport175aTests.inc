// --- Live set (.als) import, round 175a: XmlLite, gzip, the format helpers and the Song a set becomes. The sets here are
// written by the tests (only the elements the importer reads); no file of Live's is used.

#include "core/AlsImport.h"
#include "core/AutoWarp.h"
#include "core/WarpEdit.h"
#include "core/FlWrapper.h"
#include "core/Gzip.h"
#include "core/XmlLite.h"

namespace als175a
{
inline std::vector<std::uint8_t> bytesOf (const std::string& s) { return std::vector<std::uint8_t> (s.begin(), s.end()); }

inline std::string v (const char* name, const std::string& value) { return std::string ("<") + name + " Value=\"" + value + "\" />"; }

// a mixer with automatable volume / pan / speaker and `sends` send levels; ids from `id`
inline std::string mixer (int id, double volume, double pan, bool speaker, const std::vector<double>& sends = {}, const std::string& extra = {})
{
    std::string s = "<Mixer>";
    s += "<Volume><Manual Value=\"" + std::to_string (volume) + "\" /><AutomationTarget Id=\"" + std::to_string (id) + "\" /></Volume>";
    s += "<Pan><Manual Value=\"" + std::to_string (pan) + "\" /><AutomationTarget Id=\"" + std::to_string (id + 1) + "\" /></Pan>";
    s += std::string ("<Speaker><Manual Value=\"") + (speaker ? "true" : "false") + "\" /></Speaker>";
    s += "<Sends>";
    for (std::size_t k = 0; k < sends.size(); ++k)
        s += "<TrackSendHolder Id=\"" + std::to_string (k) + "\"><Send><Manual Value=\"" + std::to_string (sends[k]) +
             "\" /><AutomationTarget Id=\"" + std::to_string (id + 10 + (int) k) + "\" /></Send><EnabledByUser Value=\"true\" /></TrackSendHolder>";
    s += "</Sends>" + extra + "</Mixer>";
    return s;
}

inline std::string name (const std::string& n) { return "<Name>" + v ("EffectiveName", n) + v ("UserName", n) + "</Name>"; }

inline std::string midiNote (double time, double duration, int velocity)
{
    return "<MidiNoteEvent Time=\"" + std::to_string (time) + "\" Duration=\"" + std::to_string (duration) + "\" Velocity=\"" + std::to_string (velocity) +
           "\" OffVelocity=\"64\" IsEnabled=\"true\" />";
}

// kHs Gain's Uid fields as Live 12.4 saved them [LIVE probe, 2026-11-06]
constexpr std::int32_t khsFields[4] = { -1481959855, 7488716, 14899853, 7635833 };

inline std::string vst3Device (const std::string& processorHex)
{
    std::string uid = "<Uid>";
    for (int k = 0; k < 4; ++k)
        uid += v (("Fields." + std::to_string (k)).c_str(), std::to_string (khsFields[k]));
    uid += "</Uid>";
    return "<PluginDevice Id=\"0\"><On><Manual Value=\"true\" /></On><PluginDesc><Vst3PluginInfo Id=\"0\">" + v ("Name", "kHs Gain") +
           "<Preset><Vst3Preset Id=\"0\">" + uid + v ("DeviceType", "2") + "<ProcessorState>\n\t" + processorHex +
           "\n</ProcessorState><ControllerState /></Vst3Preset></Preset>" + uid + v ("DeviceType", "2") + "</Vst3PluginInfo></PluginDesc></PluginDevice>";
}

inline std::string vst2Device (const std::string& bufferHex)
{
    // 'FBCh' = 1178747752: the buffer is the plugin's chunk [LIVE probe]
    return "<PluginDevice Id=\"1\"><On><Manual Value=\"true\" /></On><PluginDesc><VstPluginInfo Id=\"0\">" + v ("Path", "C:/VST/kHs Gain.dll") +
           v ("PlugName", "kHs Gain") + v ("UniqueId", "1802725230") + v ("Category", "1") + "<Preset><VstPreset Id=\"0\">" + v ("Type", "1178747752") +
           v ("ProgramCount", "1") + v ("ParameterCount", "3") + v ("ProgramNumber", "0") + "<Buffer>" + bufferHex + "</Buffer>" +
           v ("ByteOrder", "2") + "</VstPreset></Preset></VstPluginInfo></PluginDesc></PluginDevice>";
}

// A set with a bit of everything the import maps: tempo 128, 3/4; a MIDI track (VST3 instrument, a 2-beat clip looped
// over 8 beats, a Session clip in scene 2); an audio track (a VST2 insert, a warped clip, routed to a group); a group;
// a return fed by the MIDI track's send; a locator; the arrangement loop; volume automation on the MIDI track.
inline std::string richSet (const std::string& liveVersion = "12.0_12117", bool live11 = false)
{
    const auto mainTag = live11 ? std::string ("MasterTrack") : std::string ("MainTrack");
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"" + liveVersion + "\" Creator=\"test\">\n<LiveSet>";
    s += "<Tracks>";
    // group (Id 30)
    s += "<GroupTrack Id=\"30\">" + name ("Band") + v ("Color", "14") + v ("TrackGroupId", "-1") + v ("TrackUnfolded", "false") +
         "<DeviceChain>" + mixer (300, 1.0, 0.0, true, { 0.000316 }) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") +
         "</AudioOutputRouting><DeviceChain><Devices /></DeviceChain></DeviceChain></GroupTrack>";
    // MIDI track (Id 10)
    s += "<MidiTrack Id=\"10\">" + name ("Keys") + v ("Color", "3") + v ("TrackGroupId", "-1") + "<DeviceChain>" +
         mixer (100, 0.5, -0.5, true, { 1.0 }) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting>" +
         "<MainSequencer><ClipTimeable><ArrangerAutomation><Events>" +
         "<MidiClip Id=\"0\" Time=\"4\">" + v ("CurrentStart", "4") + v ("CurrentEnd", "12") + v ("Name", "Riff") + v ("Color", "5") +
         "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", "2") + v ("StartRelative", "0") + v ("LoopOn", "true") + v ("OutMarker", "2") + "</Loop>" +
         "<Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + midiNote (0, 0.5, 100) + midiNote (1, 0.5, 64) + "</Notes>" + v ("MidiKey", "60") +
         "</KeyTrack></KeyTracks></Notes></MidiClip>" +
         "</Events></ArrangerAutomation></ClipTimeable>" +
         "<ClipSlotList><ClipSlot Id=\"0\"><ClipSlot><Value /></ClipSlot></ClipSlot><ClipSlot Id=\"1\"><ClipSlot><Value>" +
         "<MidiClip Id=\"1\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "4") + v ("Name", "Scene clip") +
         "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", "4") + v ("StartRelative", "0") + v ("LoopOn", "true") + "</Loop>" +
         "<Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + midiNote (0, 1, 90) + "</Notes>" + v ("MidiKey", "64") +
         "</KeyTrack></KeyTracks></Notes></MidiClip></Value></ClipSlot></ClipSlot></ClipSlotList></MainSequencer>" +
         "<DeviceChain><Devices>" + vst3Device ("010000000E020000504B0304") + "</Devices></DeviceChain></DeviceChain>" +
         "<AutomationEnvelopes><Envelopes><AutomationEnvelope Id=\"0\"><EnvelopeTarget>" + v ("PointeeId", "100") +
         "</EnvelopeTarget><Automation><Events><FloatEvent Id=\"1\" Time=\"-63072000\" Value=\"0.5\" /><FloatEvent Id=\"2\" Time=\"8\" Value=\"1\" />" +
         "</Events></Automation></AutomationEnvelope></Envelopes></AutomationEnvelopes></MidiTrack>";
    // audio track (Id 20), in the group
    s += "<AudioTrack Id=\"20\">" + name ("Vocal") + v ("Color", "20") + v ("TrackGroupId", "30") + "<DeviceChain>" +
         mixer (200, 1.0, 0.25, false, { 0.000316 }) + "<AudioOutputRouting>" + v ("Target", "AudioOut/GroupTrack") + "</AudioOutputRouting>" +
         "<MainSequencer><Sample><ArrangerAutomation><Events>" +
         "<AudioClip Id=\"0\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "8") + v ("Name", "Take") + v ("IsWarped", "true") +
         v ("WarpMode", "4") + v ("PitchCoarse", "2") + v ("SampleVolume", "0.5") +
         "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", "8") + v ("StartRelative", "0") + v ("LoopOn", "false") + v ("OutMarker", "8") + "</Loop>" +
         // 2 seconds of the sample over 8 beats (at 128 bpm 8 beats = 3.75 s): played 1.875× longer
         "<WarpMarkers><WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\" /><WarpMarker Id=\"1\" SecTime=\"2\" BeatTime=\"8\" /></WarpMarkers>" +
         "<SampleRef><FileRef>" + v ("RelativePathType", "1") + v ("RelativePath", "Samples/Imported/vocal.wav") + v ("Path", "D:/old/Samples/Imported/vocal.wav") +
         "</FileRef></SampleRef></AudioClip>" +
         "</Events></ArrangerAutomation></Sample></MainSequencer>" +
         "<DeviceChain><Devices>" + vst2Device ("504B0304AABB") + "<Eq8 Id=\"2\">" + v ("UserName", "") + "</Eq8></Devices></DeviceChain></DeviceChain></AudioTrack>";
    // return (Id 40)
    s += "<ReturnTrack Id=\"40\">" + name ("Reverb") + v ("Color", "40") + "<DeviceChain>" + mixer (400, 1.0, 0.0, true, { 0.000316 }) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices /></DeviceChain></DeviceChain></ReturnTrack>";
    s += "</Tracks>";
    s += "<" + mainTag + "><DeviceChain>" +
         mixer (900, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"128\" /><AutomationTarget Id=\"950\" /></Tempo><TimeSignature><Manual Value=\"200\" /></TimeSignature>") +
         "</DeviceChain><AutomationEnvelopes><Envelopes /></AutomationEnvelopes></" + mainTag + ">";
    s += "<Scenes><Scene Id=\"0\">" + v ("Name", "Intro") + "</Scene><Scene Id=\"1\">" + v ("Name", "Drop") + "</Scene></Scenes>";
    s += "<Locators><Locators><Locator Id=\"0\">" + v ("Time", "6") + v ("Name", "Verse") + "</Locator></Locators></Locators>";
    s += "<Transport>" + v ("LoopOn", "true") + v ("LoopStart", "4") + v ("LoopLength", "8") + "</Transport>";
    s += "</LiveSet>\n</Ableton>\n";
    return s;
}

inline lynx::AlsImportOptions options (lynx::FlpImportReport* report)
{
    lynx::AlsImportOptions o;
    o.common.projectFolder = "E:/sets/Song Project";
    o.common.fileExists = [] (const std::string& p) { return p == "E:/sets/Song Project/Samples/Imported/vocal.wav"; };
    o.common.report = report;
    o.sessionOnPlaylist = true; // 175a's placement (round 181c made the clip grid the default: Als181cTests.inc)
    return o;
}

inline const lynx::Marker* markerNamed (const lynx::Song& s, const std::string& n)
{
    for (const auto& m : s.markers())
        if (m.name == n)
            return &m;
    return nullptr;
}
} // namespace als175a

TEST (als175a_xml_reader)
{
    using namespace lynx::xml;
    Document d;
    CHECK (d.parse ("<?xml version=\"1.0\"?><!-- c --><A x=\"1 &amp; 2\"><B Value=\"3.5\" /><B Value=\"no\"/><C>t&lt;x<![CDATA[<raw>]]></C><D><E Value=\"true\"/></D></A>"));
    const auto a = d.root();
    CHECK (a.name() == "A");
    CHECK (a.attr ("x") == "1 & 2");
    CHECK (a.children ("B").size() == 2);
    CHECK (a.number ("B") == 3.5);
    CHECK (a.child ("C").text() == "t<x<raw>");
    CHECK (a.boolean ("D/E"));
    CHECK (a.find ("E").name() == "E");
    CHECK (! a.path ("D/Z"));
    CHECK (a.value ("Missing", "fb") == "fb");
    std::string err;
    CHECK (! d.parse ("<A><B></A>", &err) && ! err.empty());
    CHECK (! d.parse ("<A></A><B/>"));
    CHECK (escape ("a<\"&'>") == "a&lt;&quot;&amp;&apos;&gt;");
    CHECK (Document::decode ("&#65;&#x42;&#233;") == "AB\xC3\xA9");
}

TEST (als175a_gzip_round_trip)
{
    using namespace lynx;
    std::string text (200000, 'x');
    for (std::size_t i = 0; i < text.size(); ++i)
        text[i] = (char) ('a' + (i * 7919) % 26);
    const auto data = als175a::bytesOf (text);
    const auto packed = gzip::compressStored (data.data(), data.size());
    CHECK (gzip::isGzip (packed.data(), packed.size()));
    CHECK (! gzip::isGzip (data.data(), data.size()));
    const auto back = gzip::decompress (packed.data(), packed.size());
    CHECK (back && *back == data);
    auto broken = packed;
    broken[broken.size() / 2] ^= 0x55; // inside a stored block: the CRC catches it
    CHECK (! gzip::decompress (broken.data(), broken.size()));
    CHECK (gzip::crc32 ((const std::uint8_t*) "123456789", 9) == 0xCBF43926u);
}

TEST (als175a_format_helpers)
{
    using namespace lynx;
    int n = 0, d = 0;
    CHECK (alsTimeSignature (201, n, d) && n == 4 && d == 4);
    CHECK (alsTimeSignature (2, n, d) && n == 3 && d == 1 * 1); // 3/1 (code 2 = 3 / 2^0)
    CHECK (alsTimeSignature (302, n, d) && n == 6 && d == 8);
    CHECK (alsTimeSignatureCode (4, 4) == 201 && alsTimeSignatureCode (6, 8) == 302 && alsTimeSignatureCode (7, 16) == 402);
    CHECK (! alsTimeSignature (-1, n, d));

    // kHs Gain VST3: Live's Uid fields → class id → JUCE's normalised uniqueId 878ab867 (the plugin list's) [LIVE probe]
    const std::array<std::int32_t, 4> f { als175a::khsFields[0], als175a::khsFields[1], als175a::khsFields[2], als175a::khsFields[3] };
    const auto id = alsVst3ClassId (f);
    CHECK (alsVst3Fields (id) == f);
    const auto make = [] (std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t e) { return (a << 24) | (b << 16) | (c << 8) | e; };
    const std::uint32_t longs[] = { make (id[3], id[2], id[1], id[0]), make (id[5], id[4], id[7], id[6]), make (id[8], id[9], id[10], id[11]),
                                    make (id[12], id[13], id[14], id[15]) };
    std::uint32_t hash = 0;
    for (auto l : longs)
        hash = hash * 31 + l;
    CHECK (hash == 0x878ab867u);

    const std::vector<std::uint8_t> blob { 0x00, 0x01, 0xAB, 0xFF, 0x10 };
    CHECK (alsHexDecode (alsHexEncode (blob, 2)) == blob);
    CHECK (alsHexDecode ("0a 0B\n\t0c") == (std::vector<std::uint8_t> { 0x0A, 0x0B, 0x0C }));
    CHECK (alsHexDecode ("0g").empty() && alsHexDecode ("abc").empty());

    for (int i = 0; i < 70; ++i)
        CHECK (alsColourIndex (alsColour (i)) == i);
    CHECK (alsColour (-1) == 0 && alsColour (70) == 0);
}

TEST (als175a_detects_sets)
{
    using namespace lynx;
    const auto xmlBytes = als175a::bytesOf (als175a::richSet());
    const auto packed = gzip::compressStored (xmlBytes.data(), xmlBytes.size());
    CHECK (isAlsData (packed.data(), packed.size()));
    CHECK (isAlsData (xmlBytes.data(), xmlBytes.size()));
    const auto flp = als175a::bytesOf ("FLhd\x06\x00\x00\x00");
    CHECK (! isAlsData (flp.data(), flp.size()));
    std::string error;
    CHECK (! importAls (als175a::bytesOf ("<Other><LiveSet/></Other>"), {}, &error) && ! error.empty());
    CHECK (! importAls (als175a::bytesOf ("<Ableton MajorVersion=\"5\"><LiveSet>"), {}, &error));
    auto damaged = packed;
    damaged.resize (damaged.size() - 9);
    CHECK (! importAls (damaged, {}, &error));
}

TEST (als175a_imports_a_set)
{
    using namespace lynx;
    FlpImportReport report;
    const auto xmlBytes = als175a::bytesOf (als175a::richSet());
    const auto packed = gzip::compressStored (xmlBytes.data(), xmlBytes.size());
    std::string error;
    const auto song = importAls (packed, als175a::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
    {
        std::printf ("  import error: %s\n", error.c_str());
        return;
    }
    const auto& s = *song;
    CHECK (std::abs (s.bpm - 128.0) < 1e-9);
    CHECK (s.beatsPerBar == 3);

    // inserts: Master + Band, Keys, Vocal, Reverb (track order)
    CHECK (s.mixer.size() == 5);
    CHECK (s.mixer[1].name == "Band" && s.mixer[2].name == "Keys" && s.mixer[3].name == "Vocal" && s.mixer[4].name == "Reverb");
    CHECK (std::abs (s.mixer[2].volume - faderPosition (0.5f)) < 1e-4);
    CHECK (std::abs (s.mixer[2].pan + 0.5f) < 1e-6);
    CHECK (s.mixer[3].muted && ! s.mixer[2].muted);
    // Keys → Reverb at 0 dB; Vocal → Band (group) only, not to the Master
    CHECK (s.mixer[2].sends.size() == 1 && s.mixer[2].sends[0].target == 4 && std::abs (s.mixer[2].sends[0].level - 1.0f) < 1e-6);
    CHECK (! s.mixer[3].toMaster && s.mixer[3].sends.size() == 1 && s.mixer[3].sends[0].target == 1);
    // the VST2 insert (with its chunk) and the native EQ in the report
    CHECK (s.mixer[3].effects.size() == 1 && s.mixer[3].effects[0].plugin.foreign.format == ForeignPlugin::Format::vst2);
    if (s.mixer[3].effects.size() == 1)
    {
        const auto& p = s.mixer[3].effects[0].plugin.foreign;
        CHECK (p.vst2Id == 1802725230u && p.name == "kHs Gain");
        FlVst2State st;
        CHECK (parseFlVst2State (p.state, st) && st.chunk == (std::vector<std::uint8_t> { 0x50, 0x4B, 0x03, 0x04, 0xAA, 0xBB }));
    }
    CHECK (report.droppedEffects.count ("Eq8") == 1);

    // Keys: a VST3 instrument channel with its component state carried in FL's record layout
    const ChannelDef* keys = nullptr;
    for (const auto& c : s.channels)
        if (c.name == "Keys")
            keys = &c;
    CHECK (keys != nullptr && keys->instrument == InstrumentKind::plugin && keys->mixerTrack == 2);
    if (keys != nullptr)
    {
        const auto& p = keys->plugin.foreign;
        CHECK (p.format == ForeignPlugin::Format::vst3 && p.hasClassId);
        std::vector<std::uint8_t> component, controller;
        CHECK (splitFlVst3State (p.state, component, controller));
        CHECK (component == (std::vector<std::uint8_t> { 0x01, 0, 0, 0, 0x0E, 0x02, 0, 0, 0x50, 0x4B, 0x03, 0x04 }) && controller.empty());
    }

    // the looped 2-beat MIDI clip over 8 beats at beat 4: one pattern, 4 passes of 2 notes
    const Clip* riff = nullptr;
    for (const auto& c : s.playlist)
        if (c.kind == ClipKind::pattern && c.start == 4 * ppq)
            riff = &c;
    CHECK (riff != nullptr && riff->length == 8 * ppq);
    if (riff != nullptr)
    {
        const auto& p = s.patterns[(std::size_t) riff->pattern];
        CHECK (p.name == "Riff" && p.notes.size() == 8 && p.lengthTicks == 8 * ppq);
        CHECK (p.notes.size() == 8 && p.notes[0].start == 0 && p.notes[1].start == ppq && p.notes[2].start == 2 * ppq && p.notes[7].start == 7 * ppq);
        CHECK (p.notes.size() == 8 && std::abs (p.notes[1].velocity - 64.0f / 127.0f) < 1e-6 && p.notes[0].length == ppq / 2);
    }

    // the warped audio clip: the sample found next to the set (relocated), 1.875× longer, +2 semitones, gain 0.5
    const Clip* take = nullptr;
    for (const auto& c : s.playlist)
        if (c.kind == ClipKind::audio)
            take = &c;
    CHECK (take != nullptr);
    if (take != nullptr)
    {
        CHECK (take->start == 0 && take->length == 8 * ppq);
        CHECK (std::abs (take->stretch - 1.875) < 1e-9);
        CHECK (std::abs (take->pitch - 2.0f) < 1e-6 && std::abs (take->gain - 0.5f) < 1e-6);
        const auto& ch = s.channels[(std::size_t) take->channel];
        CHECK (ch.instrument == InstrumentKind::audioClip && ch.samplePath == "E:/sets/Song Project/Samples/Imported/vocal.wav");
        CHECK (ch.mixerTrack == 3 && ch.sampler.keepPitch && ch.sampler.stretchMethod == StretchMethod::complex);
        CHECK (std::abs (ch.volume - 10000.0f / 12800.0f) < 1e-6); // unity for a Playlist audio clip
    }
    CHECK (report.missingSamples == 0);

    // Session: scene 2's clip after the arrangement (ends at beat 12 → bar 5 in 3/4 = beat 12), with a marker per scene
    const auto* drop = als175a::markerNamed (s, "Drop");
    CHECK (drop != nullptr && drop->tick == 12 * ppq);
    CHECK (als175a::markerNamed (s, "Intro") == nullptr); // (scene 1 has no clip)
    bool sceneClip = false;
    for (const auto& c : s.playlist)
        sceneClip = sceneClip || (c.kind == ClipKind::pattern && c.start == 12 * ppq && c.length == 4 * ppq);
    CHECK (sceneClip);
    CHECK (report.approximated.size() >= 1);

    // locator, loop, groups
    const auto* verse = als175a::markerNamed (s, "Verse");
    CHECK (verse != nullptr && verse->tick == 6 * ppq);
    CHECK (s.loopStart == 4 * ppq && s.loopEnd == 12 * ppq);
    CHECK (s.playlistTracks.size() >= 3 && s.playlistTracks[0].collapsed && s.playlistTracks[2].grouped);

    // Keys' volume automation: an automation clip from 0 (the "before the song" value) to beat 8
    bool automated = false;
    for (const auto& a : s.automations)
        if (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == 2 && a.points.size() == 2)
            automated = a.points[1].tick == 8 * ppq && std::abs (a.points[0].value - faderPosition (0.5f)) < 1e-4 &&
                        std::abs (a.points[1].value - faderPosition (1.0f)) < 1e-4;
    CHECK (automated);

    // the plugin list for the targeted scan before an open
    const auto uses = alsForeignPlugins (packed);
    CHECK (uses.size() == 2);
}

TEST (als175a_reads_live11_sets)
{
    using namespace lynx;
    // Live 11 names the main track MasterTrack [BIN: the 11 → 12 upgrade table]
    const auto song = importAls (als175a::bytesOf (als175a::richSet ("11.0_11300", true)), als175a::options (nullptr));
    CHECK (song && std::abs (song->bpm - 128.0) < 1e-9 && song->beatsPerBar == 3 && song->mixer.size() == 5);
}

// Opt-in (LYNX_ALS_DIR=folder): every .als / .xml set below the folder is imported and summarised. Real sets (the ones
// Live ships, sets from public repositories) stay on the developer's disk; none is committed.
TEST (als175a_real_sets)
{
    using namespace lynx;
    const char* dir = std::getenv ("LYNX_ALS_DIR");
    if (dir == nullptr)
        return;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".als" && ext != ".xml")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        FlpImportReport report;
        AlsImportOptions o;
        o.common.projectFolder = entry.path().parent_path().string();
        o.common.fileExists = [] (const std::string& p) { return std::filesystem::exists (p); };
        o.common.report = &report;
        std::string error;
        const auto t0 = std::chrono::steady_clock::now();
        const auto song = importAls (bytes, o, &error);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        CHECK (song.has_value());
        if (! song)
        {
            std::printf ("  %s: ERROR %s\n", entry.path().filename().string().c_str(), error.c_str());
            continue;
        }
        int plugins = 0, notes = 0;
        for (const auto& c : song->channels)
            plugins += c.plugin.foreign.format != ForeignPlugin::Format::none;
        for (const auto& m : song->mixer)
            for (const auto& fx : m.effects)
                plugins += fx.plugin.foreign.format != ForeignPlugin::Format::none;
        for (const auto& p : song->patterns)
            notes += (int) p.notes.size();
        std::printf ("  %s: %.0f ms, bpm %.2f %d/x, %zu inserts, %zu channels, %zu patterns (%d notes), %zu clips, %zu automations, "
                     "%zu markers, %d plugins, %d samples missing, %zu dropped devices, %zu unsupported\n",
                     entry.path().filename().string().c_str(), ms, song->bpm, song->beatsPerBar, song->mixer.size(), song->channels.size(),
                     song->patterns.size(), notes, song->playlist.size(), song->automations.size(), song->markers().size(), plugins,
                     report.missingSamples, report.droppedEffects.size(), report.unsupported.size());
    }
}

// --- .als export (round 175a, N2): LYNX → .als → LYNX keeps what the format holds, for Live 12 and Live 11 sets

#include "core/AlsExport.h"
#include "flp/FlpWrite.h"

namespace als175a
{
// every pointee id unique and below NextPointeeId; SendsPre and each track's clip slots sized right [ALS_FORMAT §9]
inline bool loaderRulesHold (const std::string& xmlText, int returns, std::string& why)
{
    lynx::xml::Document d;
    if (! d.parse (xmlText))
    {
        why = "unparsable";
        return false;
    }
    std::set<std::string> ids;
    long maxId = 0;
    bool unique = true;
    std::function<void (const lynx::xml::Element&)> walk = [&] (const lynx::xml::Element& e) {
        const auto n = std::string (e.name());
        if (e.hasAttr ("Id"))
            maxId = std::max (maxId, std::strtol (e.attr ("Id").c_str(), nullptr, 10));
        const auto target = n == "Pointee" || (n.size() > 6 && n.compare (n.size() - 6, 6, "Target") == 0) || n.rfind ("ControllerTargets.", 0) == 0;
        if (target && e.hasAttr ("Id") && ! ids.insert (e.attr ("Id")).second)
            unique = false;
        // list items: ids unique among their siblings ("Non-unique list ids")
        std::set<std::string> siblings;
        for (const auto& c : e.children())
            if (c.hasAttr ("Id") && ! siblings.insert (std::string (c.name()) + "#" + c.attr ("Id")).second)
                unique = false;
        for (const auto& c : e.children())
            walk (c);
    };
    const auto set = d.root().child ("LiveSet");
    walk (set);
    if (! unique)
        why = "duplicate pointee id";
    else if (set.number ("NextPointeeId") <= (double) maxId)
        why = "NextPointeeId too low";
    else if ((int) set.child ("SendsPre").children().size() != returns)
        why = "SendsPre";
    else
    {
        const auto scenes = set.child ("Scenes").children ("Scene").size();
        for (const auto& t : set.child ("Tracks").children())
            for (const auto* seq : { "DeviceChain/MainSequencer/ClipSlotList", "DeviceChain/FreezeSequencer/ClipSlotList" })
                if (t.path (seq) && t.path (seq).children ("ClipSlot").size() != (t.name() == "GroupTrack" || t.name() == "ReturnTrack" ? 0 : scenes))
                    why = std::string ("clip slots of ") + std::string (t.name());
        for (const auto& g : set.child ("Tracks").children ("GroupTrack"))
            if (g.child ("Slots").children ("GroupTrackSlot").size() != scenes)
                why = "group track slots";
        if ((int) set.child ("Tracks").children ("ReturnTrack").size() != returns)
            why = "returns";
        // a return track's own sends are off, as Live 12 saves them (round 182b; Live 11's holders have no EnabledByUser)
        for (const auto& r : set.child ("Tracks").children ("ReturnTrack"))
            for (const auto& h : r.path ("DeviceChain/Mixer/Sends").children ("TrackSendHolder"))
            {
                if (! h.child ("EnabledByUser"))
                {
                    if (set.child ("MainTrack")) // Live 12 always writes it
                        why = "return track send without EnabledByUser";
                }
                else if (h.child ("EnabledByUser").attr ("Value") != "false")
                    why = "return track send enabled";
            }
    }
    return why.empty();
}
} // namespace als175a

TEST (als175a_export_round_trip)
{
    using namespace lynx;
    const auto original = importAls (als175a::bytesOf (als175a::richSet()), als175a::options (nullptr));
    CHECK (original.has_value());
    if (! original)
        return;
    for (const auto version : { AlsVersion::live12, AlsVersion::live11 })
    {
        AlsExportOptions o;
        o.version = version;
        o.sampleSeconds = [] (const std::string&) { return 2.0; };
        const auto out = exportAls (*original, o);
        CHECK (! out.bytes.empty() && gzip::isGzip (out.bytes.data(), out.bytes.size()));
        const auto live11 = version == AlsVersion::live11;
        CHECK (out.xml.find (live11 ? "<MasterTrack" : "<MainTrack") != std::string::npos);
        CHECK (out.xml.find (live11 ? "<MainTrack" : "<MasterTrack") == std::string::npos);
        CHECK (out.xml.find (live11 ? "MinorVersion=\"11." : "MinorVersion=\"12.") != std::string::npos);
        std::string why;
        // returns: Reverb. Band (the set's group track: an insert with no channel that Vocal routes into, not to the
        // Master) goes back as a group track holding Vocal
        CHECK (als175a::loaderRulesHold (out.xml, 1, why));
        CHECK (out.xml.find ("<GroupTrack ") != std::string::npos);
        if (! why.empty())
            std::printf ("  loader rule: %s\n", why.c_str());
        // every routing names a Live input / output, never a track path of the set a template came from: the Live 11 group
        // template's "p46251/p41230/p36996" input crashed Live 12.4 ("invalid map<K, T> key")
        for (auto at = out.xml.find ("<Target Value=\""); at != std::string::npos; at = out.xml.find ("<Target Value=\"", at + 1))
        {
            const auto value = out.xml.substr (at + 15, 8);
            CHECK (value.rfind ("AudioIn/", 0) == 0 || value.rfind ("AudioOut", 0) == 0 || value.rfind ("MidiIn/", 0) == 0 || value.rfind ("MidiOut/", 0) == 0);
            if (value[0] == 'p')
                std::printf ("  routing to a foreign track path: %s\n", out.xml.substr (at, 50).c_str());
        }
        // a clip's scale name is a setting (an index in Live 12, "Major"... in Live 11), never empty: Live 12.4 refuses
        // the set ("Unexpected value for int node")
        for (auto at = out.xml.find ("<ScaleInformation>"); at != std::string::npos; at = out.xml.find ("<ScaleInformation>", at + 1))
        {
            const auto name = out.xml.find ("<Name Value=\"", at);
            CHECK (name != std::string::npos && out.xml.compare (name, 15, "<Name Value=\"\"") != 0);
        }

        FlpImportReport report;
        auto opts = als175a::options (&report);
        opts.common.fileExists = [] (const std::string&) { return true; };
        const auto back = importAls (out.bytes, opts);
        CHECK (back.has_value());
        if (! back)
            continue;
        const auto& s = *back;
        CHECK (std::abs (s.bpm - 128.0) < 1e-9 && s.beatsPerBar == 3);
        // Keys: the VST3 instrument with the same class and component state; its 8 riff notes from beat 4
        const ChannelDef* keys = nullptr;
        for (const auto& c : s.channels)
            if (c.name == "Keys")
                keys = &c;
        CHECK (keys != nullptr && keys->instrument == InstrumentKind::plugin);
        if (keys != nullptr)
        {
            const ChannelDef* before = nullptr;
            for (const auto& c : original->channels)
                if (c.name == "Keys")
                    before = &c;
            CHECK (before != nullptr && keys->plugin.foreign.classId == before->plugin.foreign.classId);
            std::vector<std::uint8_t> a, b, ca, cb;
            CHECK (before != nullptr && splitFlVst3State (keys->plugin.foreign.state, a, ca) && splitFlVst3State (before->plugin.foreign.state, b, cb) && a == b);
            CHECK (std::abs (s.mixer[(std::size_t) keys->mixerTrack].volume - faderPosition (0.5f)) < 1e-3);
        }
        int riffNotes = 0;
        for (const auto& c : s.playlist)
            if (c.kind == ClipKind::pattern && c.start == 4 * ppq)
                riffNotes += (int) s.patterns[(std::size_t) c.pattern].notes.size();
        CHECK (riffNotes == 8);
        // the vocal: same sample, stretch, transpose and gain; its VST2 insert keeps the chunk
        const Clip* take = nullptr;
        for (const auto& c : s.playlist)
            if (c.kind == ClipKind::audio)
                take = &c;
        CHECK (take != nullptr);
        if (take != nullptr)
        {
            CHECK (std::abs (take->stretch - 1.875) < 1e-6 && std::abs (take->pitch - 2.0f) < 1e-6 && std::abs (take->gain - 0.5f) < 1e-6);
            CHECK (take->start == 0 && take->length == 8 * ppq);
            const auto& ch = s.channels[(std::size_t) take->channel];
            CHECK (ch.samplePath == "E:/sets/Song Project/Samples/Imported/vocal.wav");
            bool chunk = false;
            for (const auto& fx : s.mixer[(std::size_t) ch.mixerTrack].effects)
            {
                FlVst2State st;
                chunk = chunk || (fx.plugin.foreign.vst2Id == 1802725230u && parseFlVst2State (fx.plugin.foreign.state, st) && st.chunk.size() == 6);
            }
            CHECK (chunk);
            CHECK (! s.mixer[(std::size_t) ch.mixerTrack].toMaster); // (it fed the Band insert only)
        }
        CHECK (als175a::markerNamed (s, "Verse") != nullptr && als175a::markerNamed (s, "Verse")->tick == 6 * ppq);
        CHECK (s.loopStart == 4 * ppq && s.loopEnd == 12 * ppq);
        bool automated = false;
        for (const auto& a : s.automations)
            automated = automated || (a.target.kind == AutomationTargetKind::mixerVolume && a.points.size() == 2);
        CHECK (automated);
    }
}

TEST (als175a_flp_to_als)
{
    using namespace lynx;
    // an .flp (written by LYNX's .flp writer) → LYNX → .als → LYNX: the notes and the tempo arrive
    flp::Project p;
    p.tempo = 97.5;
    flp::Channel c;
    c.index = 0;
    c.type = flp::ChannelType::sampler;
    c.name = "Kick";
    c.samplePath = "C:/k.wav";
    p.channels.push_back (c);
    flp::Pattern pat;
    pat.index = 1;
    pat.name = "Beat";
    for (int k = 0; k < 4; ++k)
    {
        flp::Note n;
        n.position = 96 * k;
        n.length = 48;
        n.key = 60 + k;
        n.velocity = 100;
        pat.notes.push_back (n);
    }
    p.patterns.push_back (pat);
    flp::Arrangement a;
    a.name = "Main";
    flp::PlaylistItem item;
    item.position = 384;
    item.length = 384;
    item.index = 1;
    item.track = 0;
    a.items.push_back (item);
    p.arrangements.push_back (a);
    FlpImportOptions fo;
    fo.fileExists = [] (const std::string&) { return true; };
    const auto viaFlp = importFlp (flp::parseProject (flp::writeProject (p)), fo);
    const auto out = exportAls (viaFlp);
    std::string why;
    CHECK (als175a::loaderRulesHold (out.xml, 0, why));
    const auto back = importAls (out.bytes);
    CHECK (back.has_value());
    if (! back)
        return;
    CHECK (std::abs (back->bpm - 97.5) < 1e-9);
    std::vector<std::pair<std::int64_t, int>> before, after;
    for (const auto* song : { &viaFlp, &*back })
        for (const auto& clip : song->playlist)
            if (clip.kind == ClipKind::pattern)
                for (const auto& n : song->patterns[(std::size_t) clip.pattern].notes)
                    if (n.start >= clip.offset && n.start < clip.offset + clip.length)
                        (song == &viaFlp ? before : after).push_back ({ clip.start + n.start - clip.offset, n.key });
    std::sort (before.begin(), before.end());
    std::sort (after.begin(), after.end());
    CHECK (before.size() == 4 && before == after);
}

// --- slice N4: LYNX's Beats warp mode (Live's Beats, measured) --------------------------------------------------------

TEST (als175a_beats_warp_mode)
{
    // four 0.5 s pieces, each a 330 Hz tone that starts loud (an attack) and decays
    constexpr double rate = 44100.0;
    constexpr long piece = 22050, lead = 4410;
    lynx::SampleData in;
    in.sampleRate = rate;
    in.left.assign ((std::size_t) (lead + 4 * piece), 0.0f);
    for (long k = 0; k < 4; ++k)
        for (long i = 0; i < piece; ++i)
            in.left[(std::size_t) (lead + k * piece + i)] = (float) (0.8 * std::exp (-3.0 * (double) i / piece)
                                                                    * std::sin (2.0 * 3.141592653589793 * 330.0 * (double) i / rate));
    in.right = in.left;
    // the cuts: one per piece, at most 12 ms before its attack (the 10 ms before the steepest rise)
    const auto attacks = lynx::stretchdetail::beatsCuts (in.left.data(), (long) in.frames(), rate);
    CHECK (attacks.size() == 4);
    for (std::size_t k = 0; k < attacks.size() && k < 4; ++k)
        CHECK (attacks[k] <= lead + (long) k * piece && attacks[k] >= lead + (long) k * piece - (long) (rate * 0.012));

    // round 190gx: the Classic law (projects saved before 190gx) keeps 175a's behaviour exactly: cuts 10 ms before the
    // steepest rise, the reversed fill from the piece's end
    {
        lynx::StretchOptions classic;
        classic.modes.beatsLaw = lynx::WarpModeSettings::beatsLawClassic;
        const auto oldCuts = lynx::stretchdetail::beatsCuts (in.left.data(), (long) in.frames(), rate, false);
        CHECK (oldCuts.size() == 4);
        for (std::size_t k = 0; k < oldCuts.size() && k < attacks.size(); ++k)
            CHECK (oldCuts[k] == attacks[k] - (long) std::lround (rate * 0.010) || std::abs (oldCuts[k] - (attacks[k] - (long) std::lround (rate * 0.010))) <= 1);
        const auto slowClassic = lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats, 0.0, classic);
        int fwdOk = 0, backOk = 0;
        for (std::size_t k = 0; k + 1 < oldCuts.size(); ++k)
        {
            const auto a = oldCuts[k], length = oldCuts[k + 1] - a;
            const auto o0 = (long) std::llround ((double) a * 1.25);
            const auto fade = (long) std::lround (rate * 0.016);
            double fwd = 0.0, back = 0.0;
            for (long i = fade; i < length - fade; ++i)
                fwd = std::max (fwd, (double) std::abs (slowClassic->left[(std::size_t) (o0 + i)] - in.left[(std::size_t) (a + i)]));
            for (long j = 0; j < length / 4 - fade; ++j)
                back = std::max (back, (double) std::abs (slowClassic->left[(std::size_t) (o0 + length + j)] - in.left[(std::size_t) (a + length - 1 - j)]));
            fwdOk += fwd < 1e-6;
            backOk += back < 1e-6;
        }
        CHECK (fwdOk == 3);
        CHECK (backOk == 3);
        CHECK (slowClassic->left != lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats)->left); // (the two laws differ)
    }

    // slower (1.25×): each piece plays at its own speed from where the time map puts its attack, then backwards
    const auto slow = lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats);
    CHECK (std::abs ((double) slow->frames() - (double) in.frames() * 1.25) < 1.5);
    int exactForward = 0, exactBackward = 0;
    for (std::size_t k = 0; k + 1 < attacks.size(); ++k)
    {
        const auto a = attacks[k], length = attacks[k + 1] - a;
        const auto o0 = (long) std::llround ((double) a * 1.25);
        const auto fade = (long) std::lround (rate * 0.016);
        // (round 190gx, Live's W1 renders: the reversed reading turns 20 ms before the piece's end)
        const auto turn = length - (long) std::lround (rate * lynx::stretchdetail::beatsTurnSeconds);
        double fwd = 0.0, back = 0.0;
        for (long i = fade; i < turn; ++i)
            fwd = std::max (fwd, (double) std::abs (slow->left[(std::size_t) (o0 + i)] - in.left[(std::size_t) (a + i)]));
        for (long j = 0; j < length / 4 - fade; ++j)
            back = std::max (back, (double) std::abs (slow->left[(std::size_t) (o0 + turn + j)] - in.left[(std::size_t) (a + turn - 1 - j)]));
        exactForward += fwd < 1e-6;
        exactBackward += back < 1e-6;
    }
    CHECK (exactForward == 3);  // no stretching inside a piece: the original samples
    CHECK (exactBackward == 3); // the gap: the piece's end played backwards (from the turn)

    // faster (0.8×): each piece is cut where the next begins, still unstretched
    const auto fast = lynx::timeStretch (in, 0.8, lynx::StretchMethod::beats);
    CHECK (std::abs ((double) fast->frames() - (double) in.frames() * 0.8) < 1.5);
    int exactCut = 0;
    for (std::size_t k = 1; k + 1 < attacks.size(); ++k)
    {
        const auto a = attacks[k];
        const auto o0 = (long) std::llround ((double) a * 0.8);
        const auto d = (long) std::llround ((double) attacks[k + 1] * 0.8) - o0;
        double err = 0.0;
        for (long i = std::lround (rate * 0.016); i < d; ++i)
            err = std::max (err, (double) std::abs (fast->left[(std::size_t) (o0 + i)] - in.left[(std::size_t) (a + i)]));
        exactCut += err < 1e-6;
    }
    CHECK (exactCut == 2);

    // Tones, Texture, Complex: the spectral stretcher with their own presets, right length, level kept
    for (auto method : { lynx::StretchMethod::tones, lynx::StretchMethod::texture, lynx::StretchMethod::complex })
    {
        const auto out = lynx::timeStretch (in, 1.25, method);
        CHECK (std::abs ((double) out->frames() - (double) in.frames() * 1.25) < 1.5);
        double ein = 0.0, eout = 0.0;
        for (auto v : in.left)
            ein += (double) v * v;
        for (auto v : out->left)
            eout += (double) v * v;
        CHECK (std::abs (10.0 * std::log10 (eout / (ein * 1.25))) < 1.5);
    }
}

TEST (als175a_warped_clip_stretches_offline)
{
    // the same set with the file length recorded (Live always writes it): 96000 frames at 48 kHz = 2 s = 8 content beats.
    // The channel stretches the whole file to 8 beats with the clip's warp mode (Complex); the clip plays it unstretched
    using namespace lynx;
    auto xml = als175a::richSet();
    const std::string from = "</FileRef></SampleRef></AudioClip>";
    const auto at = xml.find (from);
    CHECK (at != std::string::npos);
    if (at == std::string::npos)
        return;
    xml.replace (at, from.size(), "</FileRef><DefaultDuration Value=\"96000\" /><DefaultSampleRate Value=\"48000\" /></SampleRef></AudioClip>");
    FlpImportReport report;
    const auto bytes = als175a::bytesOf (xml);
    const auto packed = gzip::compressStored (bytes.data(), bytes.size());
    const auto song = importAls (packed, als175a::options (&report), nullptr);
    CHECK (song.has_value());
    if (! song)
        return;
    const Clip* take = nullptr;
    for (const auto& c : song->playlist)
        if (c.kind == ClipKind::audio)
            take = &c;
    CHECK (take != nullptr);
    if (take == nullptr)
        return;
    CHECK (take->stretch == 1.0 && std::abs (take->pitch - 2.0f) < 1e-6 && take->offset == 0);
    const auto& ch = song->channels[(std::size_t) take->channel];
    CHECK (ch.sampler.keepPitch && ch.sampler.stretchMethod == StretchMethod::complex);
    CHECK (std::abs (ch.sampler.stretchBeats - 8.0f) < 1e-5);
}

TEST (als175a_beats_transpose_resamples_each_piece)
{
    // Beats with a transpose (Live: each piece read 2^(st/12) faster from its own start, at its own place on the grid).
    // Four 0.5 s pieces of a 330 Hz decaying tone, +12 st at 1×: every piece starts where it did, plays at 660 Hz (one
    // source frame every half output frame) and, done after half its length, fills the rest back and forth
    constexpr double rate = 44100.0;
    constexpr long piece = 22050, lead = 4410;
    lynx::SampleData in;
    in.sampleRate = rate;
    in.left.assign ((std::size_t) (lead + 4 * piece), 0.0f);
    for (long k = 0; k < 4; ++k)
        for (long i = 0; i < piece; ++i)
            in.left[(std::size_t) (lead + k * piece + i)] = (float) (0.8 * std::exp (-3.0 * (double) i / piece)
                                                                    * std::sin (2.0 * 3.141592653589793 * 330.0 * (double) i / rate));
    in.right = in.left;
    const auto attacks = lynx::stretchdetail::beatsCuts (in.left.data(), (long) in.frames(), rate);
    CHECK (attacks.size() == 4);

    // round 190gx: the Classic law keeps 175a's transposed fill (back and forth from the piece's end)
    {
        lynx::StretchOptions classic;
        classic.modes.beatsLaw = lynx::WarpModeSettings::beatsLawClassic;
        const auto oldCuts = lynx::stretchdetail::beatsCuts (in.left.data(), (long) in.frames(), rate, false);
        const auto upClassic = lynx::timeStretch (in, 1.0, lynx::StretchMethod::beats, 12.0, classic);
        const auto fade = (long) std::lround (rate * 0.016);
        int exactOld = 0, mirroredOld = 0;
        for (std::size_t k = 1; k + 1 < oldCuts.size(); ++k)
        {
            const auto a = oldCuts[k], length = oldCuts[k + 1] - a;
            double fwd = 0.0, back = 0.0;
            for (long i = fade; i < length / 2 - 2; ++i)
                fwd = std::max (fwd, (double) std::abs (upClassic->left[(std::size_t) (a + i)] - in.left[(std::size_t) (a + 2 * i)]));
            const auto played = (length - 1) / 2 + 1;
            for (long j = 0; j < played / 2 - fade; ++j)
                back = std::max (back, (double) std::abs (upClassic->left[(std::size_t) (a + played + j)] - upClassic->left[(std::size_t) (a + played - 1 - j)]));
            exactOld += fwd < 1e-5;
            mirroredOld += back < 1e-5;
        }
        CHECK (exactOld == 2);
        CHECK (mirroredOld == 2);
    }
    const auto up = lynx::timeStretch (in, 1.0, lynx::StretchMethod::beats, 12.0);
    CHECK (up->frames() == in.frames()); // the length follows the stretch only
    const auto fade = (long) std::lround (rate * 0.016);
    int exact = 0, mirrored = 0;
    for (std::size_t k = 1; k + 1 < attacks.size(); ++k)
    {
        const auto a = attacks[k], length = attacks[k + 1] - a;
        double fwd = 0.0, back = 0.0;
        const auto played = (length - 1) / 2 + 1;
        // (round 190gx: the reversed reading turns 20 ms before the piece's end, Live's W1 renders)
        const auto turn = played - (long) std::lround (rate * lynx::stretchdetail::beatsTurnSeconds);
        for (long i = fade; i < turn; ++i) // output frame i = source frame a + 2i (an even read: no interpolation)
            fwd = std::max (fwd, (double) std::abs (up->left[(std::size_t) (a + i)] - in.left[(std::size_t) (a + 2 * i)]));
        for (long j = 0; j < played / 2 - fade; ++j) // then backwards from the turn
            back = std::max (back, (double) std::abs (up->left[(std::size_t) (a + turn + j)] - up->left[(std::size_t) (a + turn - 1 - j)]));
        exact += fwd < 1e-5;
        mirrored += back < 1e-5;
    }
    CHECK (exact == 2);
    CHECK (mirrored == 2);

    // other methods ignore the transpose (they transpose in real time); 0 st is the plain Beats stretch
    const auto tones = lynx::timeStretch (in, 1.25, lynx::StretchMethod::tones, 12.0);
    const auto tonesPlain = lynx::timeStretch (in, 1.25, lynx::StretchMethod::tones);
    CHECK (tones->left == tonesPlain->left);
    const auto zero = lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats, 0.0);
    const auto plain = lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats);
    CHECK (zero->left == plain->left);
}

TEST (als175a_beats_clip_transpose_is_rendered_offline)
{
    // the rich set's clip, in Beats (WarpMode 0) with its +2 st and the file length recorded: the channel bakes the +2 st
    // into its stretched sample (stretchPitch) and the clip plays untransposed; exported again, the transpose is back
    using namespace lynx;
    auto xml = als175a::richSet();
    const std::string from = "</FileRef></SampleRef></AudioClip>";
    const auto at = xml.find (from);
    const std::string mode = "<WarpMode Value=\"4\" />";
    const auto atMode = xml.find (mode);
    CHECK (at != std::string::npos && atMode != std::string::npos);
    if (at == std::string::npos || atMode == std::string::npos)
        return;
    xml.replace (at, from.size(), "</FileRef><DefaultDuration Value=\"96000\" /><DefaultSampleRate Value=\"48000\" /></SampleRef></AudioClip>");
    xml.replace (xml.find (mode), mode.size(), "<WarpMode Value=\"0\" />");
    FlpImportReport report;
    const auto bytes = als175a::bytesOf (xml);
    const auto packed = gzip::compressStored (bytes.data(), bytes.size());
    const auto song = importAls (packed, als175a::options (&report), nullptr);
    CHECK (song.has_value());
    if (! song)
        return;
    const Clip* take = nullptr;
    for (const auto& c : song->playlist)
        if (c.kind == ClipKind::audio)
            take = &c;
    CHECK (take != nullptr);
    if (take == nullptr)
        return;
    const auto& ch = song->channels[(std::size_t) take->channel];
    CHECK (ch.sampler.stretchMethod == StretchMethod::beats && ch.sampler.keepPitch);
    CHECK (std::abs (ch.sampler.stretchPitch - 2.0f) < 1e-6);
    CHECK (take->pitch == 0.0f && take->stretch == 1.0);
    CHECK (std::abs (ch.sampler.stretchBeats - 8.0f) < 1e-5);

    // exported: PitchCoarse 2 again
    AlsExportOptions options;
    options.sampleSeconds = [] (const std::string&) { return 2.0; };
    const auto als = exportAls (*song, options);
    CHECK (als.xml.find ("<PitchCoarse Value=\"2\" />") != std::string::npos);
}

TEST (als175a_leaving_beats_moves_the_baked_transpose_to_the_pitch_knob)
{
    using namespace lynx;
    SamplerSettings s;
    s.keepPitch = true;
    s.stretchMethod = StretchMethod::beats;
    s.stretchPitch = 5.0f;
    s.pitch = 1.0f;
    CHECK (! s.moveBakedPitchToKnob()); // still Beats with Keep length: stays baked
    CHECK (s.stretchPitch == 5.0f && s.pitch == 1.0f);

    s.stretchMethod = StretchMethod::tones;
    CHECK (s.moveBakedPitchToKnob());
    CHECK (s.stretchPitch == 0.0f && s.pitch == 6.0f);
    CHECK (! s.moveBakedPitchToKnob()); // nothing left to move

    s.stretchMethod = StretchMethod::beats;
    s.stretchPitch = -3.0f;
    s.keepPitch = false; // Keep length off
    CHECK (s.moveBakedPitchToKnob());
    CHECK (s.stretchPitch == 0.0f && s.pitch == 3.0f);

    s.stretchPitch = 40.0f; // a transpose beyond the knob's range is limited to it
    CHECK (s.moveBakedPitchToKnob());
    CHECK (s.pitch == 24.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// Warp markers (round 175a, M1): a channel's markers bend the stretch inside the file

TEST (als175a_warp_map_from_markers)
{
    using namespace lynx;
    // markers in a straight line (one tempo): no map, the plain stretch
    CHECK (warpStretchMap ({ { 0.0, 0.0 }, { 1.0, 2.0 }, { 2.0, 4.0 } }, 96000.0, 48000.0, 192000.0).empty());
    CHECK (warpStretchMap ({ { 0.0, 0.0 } }, 96000.0, 48000.0, 192000.0).empty());
    // 1 s → 2 beats, then 1 s → 6 beats: the file (8 beats) fills 192000 frames, the bend at 48000 → 48000
    const std::vector<WarpPoint> bent { { 0.0, 0.0 }, { 1.0, 2.0 }, { 2.0, 8.0 } };
    const auto map = warpStretchMap (bent, 96000.0, 48000.0, 192000.0);
    CHECK (map.size() == 3);
    if (map.size() == 3)
    {
        CHECK (map[0] == (std::pair<double, double> { 0.0, 0.0 }));
        CHECK (std::abs (map[1].first - 48000.0) < 1e-6 && std::abs (map[1].second - 48000.0) < 1e-6);
        CHECK (std::abs (map[2].first - 96000.0) < 1e-6 && std::abs (map[2].second - 192000.0) < 1e-6);
    }
    CHECK (std::abs (stretchMapOutput (map, 72000.0) - 120000.0) < 1e-6);
    CHECK (std::abs (stretchMapSource (map, 120000.0) - 72000.0) < 1e-6);
    // markers past the file's ends (Live writes one after the last real one) and out of order still give the same map
    const auto wide = warpStretchMap ({ { 3.0, 14.0 }, { 1.0, 2.0 }, { -0.5, -1.0 } }, 96000.0, 48000.0, 192000.0);
    CHECK (wide.size() == 3);
    if (wide.size() == 3)
        CHECK (std::abs (wide[1].first - 48000.0) < 1e-6 && std::abs (wide[1].second - 48000.0) < 1e-6);
}

namespace als175a
{
// 2 s of silence with 20 ms 1 kHz bursts at the given seconds (44.1 kHz stereo)
inline lynx::SampleData bursts (const std::vector<double>& at)
{
    lynx::SampleData s;
    s.sampleRate = 44100.0;
    s.left.assign (88200, 0.0f);
    for (auto t : at)
        for (long i = 0; i < 882; ++i)
            s.left[(std::size_t) (std::lround (t * 44100.0) + i)] = (float) (0.7 * std::exp (-(double) i / 300.0) * std::sin (2.0 * 3.141592653589793 * 1000.0 * (double) i / 44100.0));
    s.right = s.left;
    return s;
}
// the frame where the 2 ms RMS envelope peaks within ±60 ms of `around`
inline long envelopePeak (const lynx::SampleData& s, long around)
{
    const long w = 88, from = std::max (0L, around - 2646), to = std::min ((long) s.frames() - w, around + 2646);
    long best = from;
    double bestE = -1.0;
    for (long i = from; i < to; i += 8)
    {
        double e = 0.0;
        for (long k = 0; k < w; ++k)
            e += (double) s.left[(std::size_t) (i + k)] * s.left[(std::size_t) (i + k)];
        if (e > bestE)
        {
            bestE = e;
            best = i;
        }
    }
    return best;
}
} // namespace als175a

TEST (als175a_warp_markers_place_the_audio)
{
    // bursts at 0.25, 0.75, 1.25 and 1.75 s; markers: the first second plays over 0.5 s, the second over 1.5 s (2 s in all).
    // Every warp mode puts each burst where the map says (within 8 ms of the peak the source's own burst has)
    using namespace lynx;
    const std::vector<double> at { 0.25, 0.75, 1.25, 1.75 };
    const auto in = als175a::bursts (at);
    const StretchMap map { { 0.0, 0.0 }, { 44100.0, 22050.0 }, { 88200.0, 88200.0 } };
    std::vector<long> sourcePeak;
    for (auto t : at)
        sourcePeak.push_back (als175a::envelopePeak (in, std::lround (t * 44100.0)));
    for (auto method : { StretchMethod::general, StretchMethod::transients, StretchMethod::mono, StretchMethod::beats, StretchMethod::tones,
                         StretchMethod::texture, StretchMethod::complex })
    {
        const auto out = timeStretchMapped (in, map, method);
        CHECK (out != nullptr && out->frames() == 88200 && out->isStereo());
        if (out == nullptr || out->frames() != 88200)
            continue;
        int placed = 0;
        for (std::size_t k = 0; k < at.size(); ++k)
        {
            const auto want = (long) std::llround (stretchMapOutput (map, (double) sourcePeak[k]));
            const auto got = als175a::envelopePeak (*out, want);
            placed += std::abs (got - want) <= 353; // 8 ms
        }
        CHECK (placed == 4);
        if (placed != 4)
            std::printf ("  warp markers: method %d placed %d of 4 bursts\n", (int) method, placed);
    }
    // fewer than 3 points: the plain stretch at the overall ratio (the same samples as timeStretch)
    const auto plain = timeStretchMapped (in, { { 0.0, 0.0 }, { 88200.0, 110250.0 } }, StretchMethod::complex);
    CHECK (plain->left == timeStretch (in, 1.25, StretchMethod::complex)->left);
}

TEST (als175a_warp_markers_import_and_export)
{
    // the rich set's clip with the file length recorded and markers that bend: 1 s → 2 beats, then 1 s → 6 beats. The
    // channel keeps them (8 beats in all), the clip starts at the file's start, and nothing is reported as approximated;
    // exported, the same three markers come back
    using namespace lynx;
    auto xml = als175a::richSet();
    const std::string from = "</FileRef></SampleRef></AudioClip>";
    const std::string markers = "<WarpMarkers><WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\" /><WarpMarker Id=\"1\" SecTime=\"2\" BeatTime=\"8\" /></WarpMarkers>";
    CHECK (xml.find (from) != std::string::npos && xml.find (markers) != std::string::npos);
    if (xml.find (from) == std::string::npos || xml.find (markers) == std::string::npos)
        return;
    xml.replace (xml.find (from), from.size(), "</FileRef><DefaultDuration Value=\"96000\" /><DefaultSampleRate Value=\"48000\" /></SampleRef></AudioClip>");
    xml.replace (xml.find (markers), markers.size(),
                 "<WarpMarkers><WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\" /><WarpMarker Id=\"1\" SecTime=\"1\" BeatTime=\"2\" />"
                 "<WarpMarker Id=\"2\" SecTime=\"2\" BeatTime=\"8\" /><WarpMarker Id=\"3\" SecTime=\"2.03125\" BeatTime=\"8.1875\" /></WarpMarkers>");
    FlpImportReport report;
    const auto bytes = als175a::bytesOf (xml);
    const auto song = importAls (gzip::compressStored (bytes.data(), bytes.size()), als175a::options (&report), nullptr);
    CHECK (song.has_value());
    if (! song)
        return;
    const Clip* take = nullptr;
    for (const auto& c : song->playlist)
        if (c.kind == ClipKind::audio)
            take = &c;
    CHECK (take != nullptr);
    if (take == nullptr)
        return;
    const auto& ch = song->channels[(std::size_t) take->channel];
    CHECK (ch.sampler.keepPitch && ch.sampler.stretchMethod == StretchMethod::complex);
    CHECK (std::abs (ch.sampler.stretchBeats - 8.0f) < 1e-5);
    CHECK (ch.sampler.warp.size() == 4);
    CHECK (take->stretch == 1.0 && take->offset == 0);
    CHECK (report.partialEffects.count ("Warp markers (one tempo per clip)") == 0);
    CHECK (! warpStretchMap (ch.sampler.warp, 96000.0, 48000.0, 96000.0 * 1.875 * 2.0).empty());

    AlsExportOptions options;
    options.sampleSeconds = [] (const std::string&) { return 2.0; };
    const auto als = exportAls (*song, options);
    CHECK (als.xml.find ("SecTime=\"1\" BeatTime=\"2\"") != std::string::npos);
    CHECK (als.xml.find ("SecTime=\"2\" BeatTime=\"8\"") != std::string::npos);

    // back into LYNX (LYNX writes no file length: the file's own, from sampleSeconds): the markers again
    auto again = als175a::options (nullptr);
    again.common.sampleSeconds = [] (const std::string&) { return 2.0; };
    again.common.fileExists = [] (const std::string&) { return true; };
    const auto back = importAls (als.bytes, again);
    CHECK (back.has_value());
    if (! back)
        return;
    int bent = 0;
    for (const auto& c : back->channels)
        bent += c.sampler.warp.size() >= 3 && std::abs (c.sampler.stretchBeats - 8.0f) < 1e-4;
    CHECK (bent == 1);
}

// ---------------------------------------------------------------------------------------------------------------
// Auto-warp (round 175a, M2)

namespace als175a
{
// a mono 44.1 kHz performance: a kick on every beat (louder on the bar), a hat between; `beatAt` gives each beat's time
inline lynx::SampleData performance (const std::vector<double>& beatAt, double seconds, double lead = 0.0)
{
    lynx::SampleData s;
    s.sampleRate = 44100.0;
    s.left.assign ((std::size_t) std::lround ((seconds + lead) * 44100.0), 0.0f);
    std::uint32_t noise = 7;
    for (std::size_t k = 0; k < beatAt.size(); ++k)
    {
        const auto at = (long) std::lround ((beatAt[k] + lead) * 44100.0);
        for (long i = 0; i < 3528 && at + i < (long) s.left.size(); ++i)
            s.left[(std::size_t) (at + i)] += (float) ((k % 4 == 0 ? 0.8 : 0.45) * std::exp (-(double) i / 530.0)
                                                       * std::sin (2.0 * 3.141592653589793 * (k % 2 == 0 ? 60.0 : 180.0) * (double) i / 44100.0));
        if (k + 1 < beatAt.size())
        {
            const auto hat = (long) std::lround ((0.5 * (beatAt[k] + beatAt[k + 1]) + lead) * 44100.0);
            for (long i = 0; i < 882 && hat + i < (long) s.left.size(); ++i)
            {
                noise = noise * 1664525u + 1013904223u;
                s.left[(std::size_t) (hat + i)] += (float) (0.15 * std::exp (-(double) i / 132.0) * ((double) (noise >> 8) / 8388608.0 - 1.0));
            }
        }
    }
    return s;
}
// where the markers put `beat` (piecewise linear, the end segments continued)
inline double markerTime (const std::vector<lynx::WarpPoint>& m, double beat)
{
    lynx::StretchMap map;
    for (const auto& p : m)
        map.push_back ({ p.beats, p.seconds });
    return lynx::stretchMapOutput (map, beat);
}
} // namespace als175a

TEST (als175a_auto_warp_tempo_in_names)
{
    using lynx::autowarp::bpmInName;
    CHECK (bpmInName ("Clapping Flamenco 120 bpm.aif") == 120.0);
    CHECK (bpmInName ("loop_128BPM.wav") == 128.0);
    CHECK (bpmInName ("Break-87.5-bpm") == 87.5);
    CHECK (bpmInName ("90bpm groove") == 90.0);
    CHECK (bpmInName ("Vocal Demo Lead Takes.aif") == 0.0);
    CHECK (bpmInName ("bpm only") == 0.0);
    CHECK (bpmInName ("take 2 bpm") == 0.0); // out of range
}

TEST (als175a_auto_warp_loops)
{
    using namespace lynx;
    // a 4 s loop of 8 beats named 120 bpm: one tempo, from the file's start to its last whole beat
    std::vector<double> beats;
    for (int k = 0; k < 8; ++k)
        beats.push_back (k * 0.5);
    const auto loop = als175a::performance (beats, 4.0);
    autowarp::Options named;
    named.knownBpm = 120.0;
    const auto a = autowarp::analyse (loop, named);
    CHECK (a.markers.size() == 2 && ! a.tracked && a.bpm == 120.0);
    if (a.markers.size() == 2)
        CHECK (a.markers[0] == (WarpPoint { 0.0, 0.0 }) && std::abs (a.markers[1].seconds - 4.0) < 1e-9 && a.markers[1].beats == 8.0);
    // no name: a power of two of beats nearest the song's tempo (4.2 s at a 120 bpm song: 8 beats, 114.29 bpm)
    const auto odd = als175a::performance (beats, 4.2);
    autowarp::Options unnamed;
    unnamed.songBpm = 120.0;
    const auto b = autowarp::analyse (odd, unnamed);
    CHECK (std::abs (b.bpm - 8.0 * 60.0 / 4.2) < 1e-6);
    CHECK (b.markers.size() == 2 && b.markers.back().beats == 8.0);
}

TEST (als175a_auto_warp_follows_a_drifting_performance)
{
    // 70 s that speed up from 118 to 124 bpm, after 1 s of silence: the markers start on the first kick and put every
    // beat within a few ms of where it is played (one tempo for the whole file would be a second off by the end)
    using namespace lynx;
    std::vector<double> beats;
    for (double t = 0.0; t < 70.0;)
    {
        beats.push_back (t);
        t += 60.0 / (118.0 + 6.0 * t / 70.0);
    }
    const auto s = als175a::performance (beats, 70.5, 1.0);
    const auto r = autowarp::analyse (s, {});
    CHECK (r.tracked && r.markers.size() > 20);
    CHECK (std::abs (r.firstBeat - 1.0) < 0.01);
    if (r.markers.size() < 2)
        return;
    std::vector<double> err;
    const auto last = r.markers.back().beats;
    for (std::size_t k = 0; k < beats.size() && (double) k <= last; ++k)
        err.push_back (std::abs (als175a::markerTime (r.markers, (double) k) - (beats[k] + 1.0)));
    std::sort (err.begin(), err.end());
    CHECK (err.size() > 100);
    if (err.size() > 100)
    {
        CHECK (err[err.size() / 2] < 0.005);
        CHECK (err[err.size() * 95 / 100] < 0.015);
        std::printf ("  auto-warp drift: median %.1f ms, 95%% %.1f ms\n", err[err.size() / 2] * 1000.0, err[err.size() * 95 / 100] * 1000.0);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Editing warp markers (round 175a, M3 core)

TEST (als175a_warp_marker_editing)
{
    using namespace lynx;
    SamplerSettings s;
    CHECK (! warpedit::ensureMarkers (s, 0.0, 120.0));
    CHECK (warpedit::ensureMarkers (s, 4.0, 120.0)); // a 4 s file at 120 bpm: 8 beats, start and end markers
    CHECK (s.warp.size() == 2 && s.warp[1] == (WarpPoint { 4.0, 8.0 }) && s.stretchBeats == 8.0f && s.keepPitch);

    // a marker at 1 s plays on beat 2; moved to beat 3 the first second stretches, the rest squeezes
    const auto i = warpedit::addMarker (s, 1.0);
    CHECK (i == 1 && s.warp.size() == 3 && std::abs (s.warp[1].beats - 2.0) < 1e-9);
    CHECK (warpedit::addMarker (s, 1.0005) == -1); // too close to it
    CHECK (std::abs (warpedit::moveMarker (s, 1, 3.0) - 3.0) < 1e-9);
    CHECK (std::abs (warpedit::beatAt (s.warp, 0.5) - 1.5) < 1e-9);
    // kept between its neighbours; the start never moves
    CHECK (std::abs (warpedit::moveMarker (s, 1, 9.0) - (8.0 - warpedit::minBeatGap)) < 1e-9);
    CHECK (warpedit::moveMarker (s, 0, 2.0) == 0.0 && s.warp[0].beats == 0.0);
    // the end marker sets the length in beats
    warpedit::moveMarker (s, 2, 10.0);
    CHECK (s.stretchBeats == 10.0f);
    // the end and the start can't be removed; the middle one can
    CHECK (! warpedit::removeMarker (s, 0) && ! warpedit::removeMarker (s, 2));
    CHECK (warpedit::removeMarker (s, 1) && s.warp.size() == 2);
    // the edited markers bend the render once they bend the tempo
    warpedit::addMarker (s, 2.0);
    warpedit::moveMarker (s, 1, 7.0);
    CHECK (! warpStretchMap (s.warp, 4.0 * 48000.0, 48000.0, 4.0 * 48000.0).empty());
    warpedit::clearMarkers (s);
    CHECK (s.warp.size() == 2);
}

TEST (als175a_warp_edits_one_clip_only)
{
    // two clips of one audio channel: editing the first one's markers gives it its own copy of the channel
    using namespace lynx;
    Song song;
    ChannelDef ch;
    ch.instrument = InstrumentKind::audioClip;
    ch.name = "Loop";
    ch.samplePath = "/x/loop.wav";
    song.channels.push_back (ch);
    for (int k = 0; k < 2; ++k)
    {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = 0;
        c.start = k * 3840;
        c.length = 3840;
        song.playlist.push_back (c);
    }
    const auto own = warpedit::channelForClipEdit (song, 0);
    CHECK (own == 1 && song.channels.size() == 2);
    CHECK (song.playlist[0].channel == 1 && song.playlist[1].channel == 0);
    // the second clip is now alone on its channel: edited in place
    CHECK (warpedit::channelForClipEdit (song, 1) == 0 && song.channels.size() == 2);
    CHECK (warpedit::channelForClipEdit (song, 5) == -1);
}

TEST (als175a_warp_markers_as_played)
{
    // markers 0 → 0, 1 s → 2, 2 s → 8 on a 2 s file: played as they are (8 beats); the inverse finds the file time
    using namespace lynx;
    SamplerSettings s;
    s.keepPitch = true;
    s.stretchBeats = 8.0f;
    s.warp = { { 0.0, 0.0 }, { 1.0, 2.0 }, { 2.0, 8.0 } };
    const auto p = warpedit::playedMarkers (s, 2.0);
    CHECK (p.size() == 3 && std::abs (p[1].beats - 2.0) < 1e-9);
    CHECK (std::abs (warpedit::secondsAt (p, 5.0) - 1.5) < 1e-9);
    // a straight line, or no keep-pitch stretch: nothing to show
    s.warp[1].beats = 4.0;
    CHECK (warpedit::playedMarkers (s, 2.0).empty());
    s.warp[1].beats = 2.0;
    s.keepPitch = false;
    CHECK (warpedit::playedMarkers (s, 2.0).empty());
    // beat 2 of a clip that starts at bar 2 (tick 3840) with its content from beat 1 (offset 960): tick 3840 + 960
    Clip c;
    c.start = 3840;
    c.offset = 960;
    CHECK (std::abs (warpedit::clipTickOfBeat (c, 2.0) - 4800.0) < 1e-9);
}

// Round 182b: Live 12.4 crashed loading a LYNX export of FL's "Africa's Calling" demo; its log listed samples that were
// "not a valid WAV file". FL ships raw Ogg Vorbis streams named .wav, and the export copied them as they were. Live picks
// the decoder by the extension, so every file whose content doesn't match its extension is written as a plain WAV.
TEST (als182b_raw_ogg_named_wav_goes_to_live_as_a_real_wav)
{
    using namespace lynx;
    const std::uint8_t rawOgg[] = { 'O', 'g', 'g', 'S', 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x91, 0x41, 0xf2, 0x21, 1, 0x1e, 1, 'v', 'o', 'r', 'b', 'i', 's' };
    const std::uint8_t pcmWav[] = { 'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0 };
    const std::uint8_t floatWav[] = { 'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 3, 0, 2, 0 };
    const std::uint8_t oggInWav[] = { 'R', 'I', 'F', 'F', 30, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 2, 0, 0, 0, 0x4F, 0x67 };
    const std::uint8_t flac[] = { 'f', 'L', 'a', 'C', 0, 0, 0, 34 };

    // the case Live crashed on: an Ogg stream named .wav (any case of the extension)
    CHECK (alsSampleNeedsWav ("wav", rawOgg, sizeof rawOgg));
    CHECK (alsSampleNeedsWav (".WAV", rawOgg, sizeof rawOgg));
    // as before: Ogg inside RIFF/WAVE, WavPack and unknown types are converted; plain PCM / float WAVs are not
    CHECK (alsSampleNeedsWav ("wav", oggInWav, sizeof oggInWav));
    CHECK (alsSampleNeedsWav ("wv", rawOgg, sizeof rawOgg));
    CHECK (! alsSampleNeedsWav ("wav", pcmWav, sizeof pcmWav));
    CHECK (! alsSampleNeedsWav ("wav", floatWav, sizeof floatWav));
    // types Live reads: kept when the content is what the name says
    CHECK (! alsSampleNeedsWav ("ogg", rawOgg, sizeof rawOgg));
    CHECK (! alsSampleNeedsWav ("flac", flac, sizeof flac));
    CHECK (alsSampleNeedsWav ("flac", rawOgg, sizeof rawOgg));
    CHECK (alsSampleNeedsWav ("ogg", pcmWav, sizeof pcmWav));
    // an empty or unreadable .wav is never passed through as a WAV
    CHECK (alsSampleNeedsWav ("wav", rawOgg, 0));

    // FL's own raw Ogg samples (FL Data folder, when present): detected and decodable, so the export can write a WAV
    if (realFlpDir.empty())
        return;
    const auto root = std::filesystem::path (realFlpDir).parent_path();
    for (const auto* file : { "Data/Patches/Misc/Used by demo projects/Cuebur & Thandi Draai - Africa's Calling/AP Clap Human 02.wav" })
    {
        const auto path = root / file;
        if (! std::filesystem::exists (path))
            continue;
        std::ifstream in (path, std::ios::binary);
        const std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char> (in), {} };
        CHECK (alsSampleNeedsWav (path.extension().string(), bytes.data(), std::min<std::size_t> (bytes.size(), 4096)));
        oggvorbis::Decoded decoded;
        CHECK (oggvorbis::decode (bytes.data(), bytes.size(), decoded) && decoded.channels > 0);
    }
}
