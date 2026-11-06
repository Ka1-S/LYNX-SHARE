// --- Live set (.als) import, round 175a: XmlLite, gzip, the format helpers and the Song a set becomes. The sets here are
// written by the tests (only the elements the importer reads); no file of Live's is used.

#include "core/AlsImport.h"
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
        CHECK (ch.mixerTrack == 3 && ch.sampler.keepPitch && ch.sampler.stretchMethod == StretchMethod::general);
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
