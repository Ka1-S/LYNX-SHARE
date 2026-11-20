// Round 190jf, B22: DAWproject import / export (core/DawProject.h) and its ZIP container (core/ZipLite.h). The open
// exchange format of Bitwig and PreSonus (MIT, github.com/bitwig/dawproject): a song goes out and comes back the same on
// what the format carries, the README's example project (Bitwig Studio 5.0) imports with what its XML holds, a
// hand-written project covers folders, sends, built-in devices, seconds, loops and the tempo map, broken files fail
// cleanly, and both directions list what they leave out.

#include "core/AlsImport.h"
#include "core/ClipLauncher.h"
#include "core/DawProject.h"
#include "core/Effects.h"
#include "core/FlCurve.h"
#include "core/FlWrapper.h"
#include "core/PluginPresets.h"
#include "core/TempoMarkers.h"
#include "core/XmlLite.h"
#include "core/ZipLite.h"

namespace dawproject190jf
{
#include "data/dawproject/BitwigReadmeProject.inc"

// A ZIP Python's zipfile wrote with DEFLATE (project.xml and notes/a.txt), to read method 8 and a foreign writer's layout
const std::uint8_t deflatedZip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xa2, 0x62, 0x70, 0x5d, 0x14, 0xd1, 0x2b, 0x24, 0x77, 0x00,
    0x00, 0x00, 0x9e, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x70, 0x72, 0x6f, 0x6a, 0x65, 0x63, 0x74, 0x2e, 0x78, 0x6d,
    0x6c, 0xb3, 0xb1, 0xaf, 0xc8, 0xcd, 0x51, 0x28, 0x4b, 0x2d, 0x2a, 0xce, 0xcc, 0xcf, 0xb3, 0x55, 0x32, 0xd4, 0x33, 0x50,
    0xb2, 0xb7, 0xe3, 0xb2, 0x09, 0x28, 0xca, 0xcf, 0x4a, 0x4d, 0x2e, 0x41, 0x95, 0xb0, 0xb3, 0x71, 0x2c, 0x28, 0xc8, 0xc9,
    0x4c, 0x4e, 0x2c, 0x01, 0x0a, 0x29, 0xe4, 0x25, 0xe6, 0xa6, 0xda, 0x2a, 0xb9, 0xa4, 0xa6, 0xe5, 0x24, 0x96, 0xa4, 0xa6,
    0x28, 0x21, 0x29, 0x55, 0xd2, 0xb7, 0xb3, 0x09, 0x29, 0x4a, 0xcc, 0x2b, 0x2e, 0xc8, 0x2f, 0x2a, 0x01, 0x32, 0x53, 0x73,
    0x0b, 0xf2, 0x15, 0x32, 0x53, 0x6c, 0x95, 0x4a, 0x94, 0x14, 0x4a, 0xf3, 0x32, 0x4b, 0x6c, 0x95, 0x92, 0x0a, 0x72, 0x81,
    0x1a, 0x12, 0x73, 0x4a, 0x81, 0x26, 0x58, 0x5a, 0x82, 0xd4, 0xeb, 0x23, 0x69, 0xd0, 0x87, 0xda, 0x6e, 0xc7, 0x05, 0x00,
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xa2, 0x62, 0x70, 0x5d, 0x57, 0x37, 0x62, 0x85, 0x0b, 0x00,
    0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x6e, 0x6f, 0x74, 0x65, 0x73, 0x2f, 0x61, 0x2e, 0x74, 0x78,
    0x74, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0xc0, 0x47, 0x02, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00,
    0x00, 0x00, 0x08, 0x00, 0xa2, 0x62, 0x70, 0x5d, 0x14, 0xd1, 0x2b, 0x24, 0x77, 0x00, 0x00, 0x00, 0x9e, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x70, 0x72,
    0x6f, 0x6a, 0x65, 0x63, 0x74, 0x2e, 0x78, 0x6d, 0x6c, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08,
    0x00, 0xa2, 0x62, 0x70, 0x5d, 0x57, 0x37, 0x62, 0x85, 0x0b, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0xa0, 0x00, 0x00, 0x00, 0x6e, 0x6f, 0x74, 0x65, 0x73,
    0x2f, 0x61, 0x2e, 0x74, 0x78, 0x74, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x72, 0x00,
    0x00, 0x00, 0xd4, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline std::vector<std::uint8_t> bytesOf (const std::string& s) { return { s.begin(), s.end() }; }

inline std::vector<std::uint8_t> zipOf (const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files)
{
    std::vector<lynx::zip::NewEntry> entries;
    for (const auto& [name, data] : files)
        entries.push_back ({ name, data });
    return lynx::zip::write (entries);
}

inline bool near (double a, double b, double eps = 1.0e-4) { return std::abs (a - b) <= eps; }

inline bool hasNote (const std::vector<std::string>& notes, const std::string& part)
{
    return std::any_of (notes.begin(), notes.end(), [&] (const std::string& n) { return n.find (part) != std::string::npos; });
}
inline bool reported (const std::map<std::string, int>& list, const std::string& part)
{
    return std::any_of (list.begin(), list.end(), [&] (const auto& kv) { return kv.first.find (part) != std::string::npos; });
}

inline const lynx::Automation* automationOf (const lynx::Song& s, lynx::AutomationTarget t)
{
    for (const auto& a : s.automations)
        if (a.target == t)
            return &a;
    return nullptr;
}

inline const lynx::Clip* clipAt (const lynx::Song& s, lynx::ClipKind kind, int track, std::int64_t start)
{
    for (const auto& c : s.playlist)
        if (c.kind == kind && c.track == track && c.start == start)
            return &c;
    return nullptr;
}

inline bool laneHas (const lynx::Song& s, int row, lynx::AutomationTarget t)
{
    if (row < 0 || row >= (int) s.playlistTracks.size())
        return false;
    for (const auto& l : s.playlistTracks[(std::size_t) row].lanes)
        if (l.automation >= 0 && l.automation < (int) s.automations.size() && s.automations[(std::size_t) l.automation].target == t)
            return true;
    return false;
}

// The rich song of the round trip, in the shape the import gives (one insert, channel and row per track)
struct Rich
{
    lynx::Song song;
    std::vector<std::uint8_t> vst3Component, vst3Controller, vst2State, wav;
    std::array<std::uint8_t, 16> vst3Class {};
};

inline Rich richSong()
{
    using namespace lynx;
    Rich r;
    auto& s = r.song;
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    s.playlistTracks.clear();
    s.mixer = Song::defaultMixer (0);
    s.mode = PlayMode::song;
    s.arrangements = { Arrangement { "Arrangement", {}, 0, 0, {} } };
    s.bpm = 128.0;
    s.beatsPerBar = 3;
    s.beatUnit = 4;
    s.mixer[0].volume = 0.7f;
    s.mixer[0].pan = 0.1f;
    s.info.title = "Night <Drive> & \"Co\"";
    s.info.author = "K's band";
    s.info.genre = "Synthwave";
    s.info.url = "https://example.org/?a=1&b=2";
    s.info.comments = "line one\nline two";

    // inserts 1 Lead, 2 Drums (the folder's bus), 3 Drum Loop, 4 Keys, 5 Reverb Return
    const auto insert = [&s] (const char* name, std::uint32_t colour) {
        MixerTrack m;
        m.name = name;
        m.colour = colour;
        s.mixer.push_back (m);
        return (int) s.mixer.size() - 1;
    };
    const auto lead = insert ("Lead", 0xFF112233), drums = insert ("Drums", 0xFF445566), loop = insert ("Drum Loop", 0xFF778899),
               keys = insert ("Keys", 0), ret = insert ("Reverb Return", 0xFFAABBCC);
    {
        auto& m = s.mixer[(std::size_t) lead];
        m.volume = 0.65f;
        m.pan = -0.25f;
        m.muted = true;
        auto fx = makeEffectSlot (EffectKind::reverb);
        fx.params[0] = 0.3f;
        fx.params[3] = 0.77f;
        fx.mix = 0.8f;
        fx.presetName = "Big \"Hall\"";
        m.effects.push_back (fx);
        m.sends.push_back ({ ret, 0.5f, true });
    }
    s.mixer[(std::size_t) drums].volume = 0.9f;
    {
        auto& m = s.mixer[(std::size_t) loop];
        m.toMaster = false;
        m.sends.push_back ({ drums, 1.0f });
        m.sends.push_back ({ ret, 0.25f });
        m.pan = 0.5f;
    }
    {
        auto& m = s.mixer[(std::size_t) keys];
        m.solo = true;
        EffectSlot fx;
        fx.kind = EffectKind::plugin;
        fx.plugin.name = "TestVerb3";
        fx.plugin.foreign.format = ForeignPlugin::Format::vst3;
        fx.plugin.foreign.name = "TestVerb3";
        r.vst3Class = alsVst3ClassId ({ 0x12345678, (std::int32_t) 0x9ABCDEF0, 0x0F1E2D3C, 0x4B5A6978 });
        fx.plugin.foreign.classId = r.vst3Class;
        fx.plugin.foreign.hasClassId = true;
        r.vst3Component = { 1, 2, 3, 4, 5, 0, 255 };
        r.vst3Controller = { 9, 8, 7 };
        fx.plugin.foreign.state = dawproject::flVst3State (r.vst3Component, r.vst3Controller);
        fx.enabled = false;
        m.effects.push_back (fx);
    }
    s.mixer[(std::size_t) ret].effects.push_back (makeEffectSlot (EffectKind::delay));
    s.mixer[(std::size_t) ret].volume = 0.5f;

    // channels: 0 Lead (LYNX's synth), 1 Keys (a VST2), 2 the loop's audio clip channel
    ChannelDef leadCh;
    leadCh.name = "Lead";
    leadCh.colour = 0xFF112233;
    leadCh.volume = 1.0f;
    leadCh.mixerTrack = lead;
    leadCh.instrument = InstrumentKind::synth;
    leadCh.synth.cutoff = 1234.5f;
    leadCh.synth.attack = 0.02f;
    leadCh.synth.resonance = 0.3f;
    s.channels.push_back (leadCh);
    ChannelDef keysCh;
    keysCh.name = "Keys";
    keysCh.volume = 1.0f;
    keysCh.mixerTrack = keys;
    keysCh.instrument = InstrumentKind::plugin;
    keysCh.plugin.name = "TestSynth";
    keysCh.plugin.foreign.format = ForeignPlugin::Format::vst2;
    keysCh.plugin.foreign.name = "TestSynth";
    keysCh.plugin.foreign.vst2Id = 0x54535931;
    FlVst2State st;
    st.flags = 8;
    st.currentProgram = 2;
    st.chunk = { 'c', 'h', 'u', 'n', 'k', 0, 1, 2, 3 };
    r.vst2State = writeFlVst2State (st);
    keysCh.plugin.foreign.state = r.vst2State;
    s.channels.push_back (keysCh);
    ChannelDef audio;
    audio.name = "Loop";
    audio.instrument = InstrumentKind::audioClip;
    audio.samplePath = "C:/loops/Loop 90.wav";
    audio.volume = audioClipUnityVolume;
    audio.mixerTrack = loop;
    audio.sampler.declickMode = audioClipDeclickMode;
    audio.sampler.warp = { { 0.0, 0.0 }, { 1.5, 3.0 }, { 3.0, 8.0 } };
    audio.sampler.stretchBeats = 8.0f;
    audio.sampler.keepPitch = true;
    s.channels.push_back (audio);
    r.wav = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 1, 2, 3 };

    // rows: 0 Lead, 1 Drums (folder with its bus), 2 Perc (a sub-folder without a channel), 3 Drum Loop, 4 Keys
    const auto row = [&s] (const char* name, std::uint32_t colour, bool grouped, int nest, PlaylistTrackMode mode, int mixer, int channel) {
        PlaylistTrack t;
        t.name = name;
        t.colour = colour;
        t.grouped = grouped;
        t.nest = nest;
        t.mode = mode;
        t.mixerTrack = mixer;
        t.channel = channel;
        s.playlistTracks.push_back (t);
    };
    row ("Lead", 0xFF112233, false, 0, PlaylistTrackMode::instrument, lead, 0);
    row ("Drums", 0xFF445566, false, 0, PlaylistTrackMode::audio, drums, -1);
    row ("Perc", 0, true, 0, PlaylistTrackMode::unassigned, -1, -1);
    row ("Drum Loop", 0xFF778899, true, 1, PlaylistTrackMode::audio, loop, -1);
    row ("Keys", 0, false, 0, PlaylistTrackMode::instrument, keys, 1);

    // patterns and clips
    Pattern a;
    a.name = "Lead A";
    a.colour = 0xFF00FF00;
    for (const auto& [start, length, key, vel, rel] : std::vector<std::tuple<int, int, int, float, float>> {
             { 0, 480, 60, 0.8f, 0.5f }, { 480, 240, 64, 0.6f, 0.25f }, { 1920, 960, 67, 1.0f, 0.5f } })
    {
        Note n;
        n.start = start;
        n.length = length;
        n.key = key;
        n.velocity = vel;
        n.release = rel;
        n.channel = 0;
        a.notes.push_back (n);
    }
    s.patterns.push_back (a);
    Pattern k;
    k.name = "Keys";
    for (const auto key : { 48, 55 })
    {
        Note n;
        n.start = 0;
        n.length = 3840;
        n.key = key;
        n.velocity = 0.7f;
        n.channel = 1;
        k.notes.push_back (n);
    }
    k.sortNotes();
    s.patterns.push_back (k);
    const auto clip = [&s] (ClipKind kind, int track, std::int64_t start, std::int64_t length, std::int64_t offset, int pattern, int channel) {
        Clip c;
        c.kind = kind;
        c.track = track;
        c.start = start;
        c.length = length;
        c.offset = offset;
        c.pattern = pattern;
        c.channel = channel;
        s.playlist.push_back (c);
        return &s.playlist.back();
    };
    clip (ClipKind::pattern, 0, 0, 3840, 0, 0, -1);
    clip (ClipKind::pattern, 0, 5760, 2880, 480, 0, -1);
    clip (ClipKind::pattern, 4, 0, 3840, 0, 1, -1);
    auto* ac = clip (ClipKind::audio, 3, 1920, 5760, 480, 0, 2);
    ac->fadeIn = 120;
    ac->fadeOut = 240;

    // automation lanes: the Lead insert's volume and its send, a parameter of its reverb; the Keys plug-in's parameter 7
    const auto lane = [&s] (int rowIndex, const char* name, AutomationTarget t, std::vector<std::tuple<int, float, int>> pts) {
        Automation au;
        au.name = name;
        au.target = t;
        for (const auto& [tick, v, mode] : pts)
        {
            AutomationPoint p;
            p.tick = tick;
            p.value = v;
            p.mode = (std::uint8_t) mode;
            au.points.push_back (p);
        }
        s.automations.push_back (au);
        s.playlistTracks[(std::size_t) rowIndex].lanes.push_back ({ (int) s.automations.size() - 1, 0 });
    };
    lane (0, "Lead Volume", { AutomationTargetKind::mixerVolume, lead, 0, 0 }, { { 0, 0.5f, flcurve::hold }, { 1920, 0.8f, flcurve::singleCurve }, { 3840, 0.6f, flcurve::hold } });
    lane (0, "Lead Send", { AutomationTargetKind::mixerSend, lead, ret, 0 }, { { 0, 0.3f, flcurve::singleCurve }, { 3840, 0.9f, flcurve::hold } });
    lane (0, "Lead Parameter 3", { AutomationTargetKind::effectParam, lead, 0, 3 }, { { 960, 0.1f, flcurve::hold }, { 2880, 0.95f, flcurve::hold } });
    lane (4, "Keys Parameter 7", { AutomationTargetKind::channelParam, 1, 0, 7 }, { { 0, 0.25f, flcurve::singleCurve }, { 7680, 0.75f, flcurve::hold } });

    // tempo markers (bar 5 at 140, a glide into 100 at bar 9), a 6/8 bar at bar 5, markers
    tempomarkers::add (s, 11520, 140.0);
    tempomarkers::add (s, 23040, 100.0, true);
    Marker sig;
    sig.tick = 11520;
    sig.numerator = 6;
    sig.denominator = 8;
    sig.action = Marker::timeSignature;
    s.addMarker (sig);
    Marker verse;
    verse.name = "Verse";
    s.addMarker (verse);
    Marker chorus;
    chorus.tick = 11520;
    chorus.name = "Chorus";
    s.addMarker (chorus);
    return r;
}
} // namespace dawproject190jf

TEST (dawproject190jf_zip_round_trip)
{
    using namespace lynx;
    using namespace dawproject190jf;
    std::vector<std::uint8_t> big (70000);
    for (std::size_t i = 0; i < big.size(); ++i)
        big[i] = (std::uint8_t) (i * 7 + (i >> 9));
    const auto bytes = zipOf ({ { "project.xml", bytesOf ("<Project/>") }, { "empty.bin", {} }, { "audio/Ação 1.wav", big }, { "plugins/a/b.fxp", { 1, 2, 3 } } });
    CHECK (zip::isZip (bytes.data(), bytes.size()));
    zip::Reader z;
    std::string err;
    CHECK (z.open (bytes.data(), bytes.size(), &err) && err.empty());
    CHECK (z.entries().size() == 4 && z.entries()[2].name == "audio/Ação 1.wav" && z.entries()[2].size == big.size() && z.entries()[2].method == 0);
    CHECK (z.extract ("project.xml") == bytesOf ("<Project/>"));
    CHECK (z.extract ("empty.bin").has_value() && z.extract ("empty.bin")->empty());
    CHECK (z.extract ("audio/Ação 1.wav") == big);
    // names: '\' and case and a leading "./" are forgiven; a missing one is an error
    CHECK (z.find ("PLUGINS\\a\\B.FXP") == 3 && z.find ("./project.xml") == 0 && z.find ("nope") == -1);
    CHECK (! z.extract ("nope", &err) && ! err.empty());
    // the same input always gives the same bytes (fixed dates)
    CHECK (zipOf ({ { "project.xml", bytesOf ("<Project/>") }, { "empty.bin", {} }, { "audio/Ação 1.wav", big }, { "plugins/a/b.fxp", { 1, 2, 3 } } }) == bytes);

    // DEFLATE (method 8) from another writer
    zip::Reader d;
    CHECK (d.open (deflatedZip, sizeof deflatedZip, &err));
    CHECK (d.entries().size() == 2 && d.entries()[0].method == 8);
    const auto text = d.extract ("notes/a.txt");
    CHECK (text && std::string (text->begin(), text->end()) == "hello hello hello hello hello hello");
    const auto project = d.extract ("project.xml");
    CHECK (project && std::string (project->begin(), project->end()).find ("<Application name=\"Deflated\"") != std::string::npos);

    // a flipped byte in an entry's data: the CRC catches it
    auto bad = bytes;
    bad[30 + std::string ("project.xml").size() + 2] ^= 0x20;
    zip::Reader b;
    CHECK (b.open (bad.data(), bad.size()));
    CHECK (! b.extract ("project.xml", &err) && err.find ("CRC") != std::string::npos);
    // truncated: no end record
    zip::Reader t;
    CHECK (! t.open (bytes.data(), bytes.size() / 2, &err) && ! err.empty());
    CHECK (! zip::isZip (bytes.data(), 10));
}

TEST (dawproject190jf_xml_escaping)
{
    using namespace lynx;
    using namespace dawproject190jf;
    CHECK (xml::escape ("a<b>&\"c'\n") == "a&lt;b&gt;&amp;&quot;c&apos;&#10;");
    auto r = richSong();
    r.song.playlistTracks[0].name = "Lead <1> & \"2\" 'x'";
    r.song.patterns[0].name = "A&B";
    const auto out = exportDawProject (r.song);
    CHECK (out.projectXml.find ("name=\"Lead &lt;1&gt; &amp; &quot;2&quot; &apos;x&apos;\"") != std::string::npos);
    CHECK (out.metadataXml.find ("<Title>Night &lt;Drive&gt; &amp; &quot;Co&quot;</Title>") != std::string::npos);
    CHECK (out.metadataXml.find ("line one&#10;line two") != std::string::npos);
    xml::Document doc;
    CHECK (doc.parse (out.projectXml) && doc.root().name() == "Project");
    CHECK (doc.root().path ("Structure/Track").attr ("name") == "Lead <1> & \"2\" 'x'");
    xml::Document meta;
    CHECK (meta.parse (out.metadataXml) && meta.root().child ("Comment").text() == "line one\nline two");
}

TEST (dawproject190jf_plugin_state_files)
{
    using namespace lynx;
    using namespace dawproject190jf;
    // VST3: .vstpreset both ways, the class id as the FUID's 32 hex digits
    const auto cls = alsVst3ClassId ({ 0x12345678, (std::int32_t) 0x9ABCDEF0, 0x0F1E2D3C, 0x4B5A6978 });
    CHECK (dawproject::vst3ClassIdText (cls) == "123456789ABCDEF00F1E2D3C4B5A6978");
    std::array<std::uint8_t, 16> back {};
    CHECK (dawproject::vst3ClassIdFromText ("{12345678-9ABC-DEF0-0F1E-2D3C4B5A6978}", back) && back == cls);
    CHECK (! dawproject::vst3ClassIdFromText ("1234", back));
    const std::vector<std::uint8_t> comp { 1, 2, 3, 4, 5 }, cont { 6, 7 };
    const auto preset = dawproject::writeVstPreset (cls, comp, cont);
    CHECK (preset.size() == 48 + 5 + 2 + 8 + 2 * 20 && std::memcmp (preset.data(), "VST3", 4) == 0);
    std::array<std::uint8_t, 16> readCls {};
    std::vector<std::uint8_t> c2, k2;
    CHECK (dawproject::readVstPreset (preset, readCls, c2, k2) && readCls == cls && c2 == comp && k2 == cont);
    CHECK (! dawproject::readVstPreset ({ 'V', 'S', 'T' }, readCls, c2, k2));
    // FL's VST3 record the plug-in resolver reads
    std::vector<std::uint8_t> c3, k3;
    CHECK (splitFlVst3State (dawproject::flVst3State (comp, cont), c3, k3) && c3 == comp && k3 == cont);

    // VST2: FL's record ↔ .fxb (a bank chunk), .fxp (a program chunk, FL's −6 layout), parameter programs
    FlVst2State bank;
    bank.flags = 8;
    bank.currentProgram = 3;
    bank.chunk = { 9, 9, 8 };
    const auto bankState = writeFlVst2State (bank);
    const auto fxb = dawproject::fxFromFlVst2State (bankState, 0x41424344);
    CHECK (fxb.size() > 12 && std::memcmp (fxb.data(), "CcnK", 4) == 0 && std::memcmp (fxb.data() + 8, "FBCh", 4) == 0);
    std::vector<std::uint8_t> state;
    std::uint32_t id = 0;
    CHECK (dawproject::flVst2StateFromFx (fxb, state, id) && id == 0x41424344 && state == bankState);
    // a program chunk (.fxp FPCh): FL's record has no program-chunk form in its current layout, so the chunk is carried as
    // the plug-in's chunk (the import reports it)
    pluginpresets::FxFile program;
    program.kind = pluginpresets::FxFile::Kind::chunkProgram;
    program.pluginId = 7;
    program.programName = "Init";
    program.chunk = { 1, 2 };
    const auto fxp = pluginpresets::writeFx (program);
    CHECK (std::memcmp (fxp.data() + 8, "FPCh", 4) == 0);
    FlVst2State parsed;
    CHECK (dawproject::flVst2StateFromFx (fxp, state, id) && id == 7 && parseFlVst2State (state, parsed) && parsed.chunk == program.chunk);
    FlVst2State params;
    params.params = { 0.25f, 0.5f, 1.0f };
    const auto fxck = dawproject::fxFromFlVst2State (writeFlVst2State (params), 7);
    CHECK (std::memcmp (fxck.data() + 8, "FxCk", 4) == 0);
    CHECK (dawproject::flVst2StateFromFx (fxck, state, id) && parseFlVst2State (state, parsed) && parsed.params == params.params && parsed.chunk.empty());
    CHECK (dawproject::fxFromFlVst2State ({}, 1).empty() && ! dawproject::flVst2StateFromFx ({ 1, 2, 3 }, state, id));

    // LYNX's own effect state text: every parameter's exact bits, the raw state, the names
    auto fx = makeEffectSlot (EffectKind::reverb);
    fx.params[5] = 0.123456789f;
    fx.params[100] = -0.0f;
    fx.mix = 0.3f;
    fx.enabled = false;
    fx.smartDisable = false;
    fx.state = { 0, 255, 16 };
    fx.samplePath = "C:\\x\\a\nb.wav";
    fx.presetName = "P=1";
    const auto restored = dawproject::effectFromStateText (dawproject::effectStateText (fx));
    CHECK (restored && restored->kind == EffectKind::reverb && restored->params == fx.params && restored->mix == 0.3f && ! restored->enabled
           && ! restored->smartDisable && restored->state == fx.state && restored->samplePath == fx.samplePath && restored->presetName == "P=1");
    CHECK (std::signbit (restored->params[100]));
    CHECK (! dawproject::effectFromStateText ("LYNX effect state 1\nkind=4\n")); // (a hosted plug-in is never a LYNX device)
    CHECK (! dawproject::effectFromStateText ("garbage"));

    // units
    CHECK (near (dawproject::gainOfVolume (-6.0, "decibel", 0), 0.501187) && near (dawproject::gainOfVolume (0.66, "linear", 2), 0.66));
    CHECK (near (dawproject::panOfValue (0.5, "normalized", 0, 1), 0.0) && near (dawproject::panOfValue (0.0, "normalized", 0, 1), -1.0));
    CHECK (near (dawproject::panOfValue (-0.5, "linear", -1, 1), -0.5) && near (dawproject::panOfValue (50, "percent", -100, 100), 0.5));
    CHECK (dawproject::colourOf ("#a2eabf") == 0xFFA2EABFu && dawproject::colourOf ("red") == 0 && dawproject::colourText (0xFFA2EABF) == "#a2eabf");
}

TEST (dawproject190jf_round_trip)
{
    using namespace lynx;
    using namespace dawproject190jf;
    auto r = richSong();
    const auto& a = r.song;
    DawProjectExportOptions eo;
    eo.readFile = [&r] (const std::string& path) { return pathFileName (path) == "Loop 90.wav" ? r.wav : std::vector<std::uint8_t> {}; };
    eo.sampleFormat = [] (const std::string&, double& seconds, int& rate, int& channels) {
        seconds = 3.0;
        rate = 48000;
        channels = 2;
        return true;
    };
    const auto out = exportDawProject (a, eo);
    CHECK (! out.bytes.empty() && isDawProjectData (out.bytes.data(), out.bytes.size()));
    // the container: project.xml, metadata.xml, the embedded audio and the plug-in states
    zip::Reader z;
    CHECK (z.open (out.bytes.data(), out.bytes.size()));
    CHECK (z.find ("project.xml") == 0 && z.find ("metadata.xml") == 1 && z.find ("audio/Loop 90.wav") >= 0);
    int states = 0;
    for (const auto& e : z.entries())
        states += e.name.rfind ("plugins/", 0) == 0 ? 1 : 0;
    CHECK (states == 5); // the synth, the reverb, the VST3, the delay, the VST2
    CHECK (out.projectXml.find ("<Vst3Plugin") != std::string::npos && out.projectXml.find ("deviceID=\"123456789ABCDEF00F1E2D3C4B5A6978\"") != std::string::npos);
    CHECK (out.projectXml.find ("deviceVendor=\"LYNX\"") != std::string::npos && out.projectXml.find ("role=\"submix\"") != std::string::npos);
    CHECK (out.projectXml.find ("role=\"effect\"") != std::string::npos && out.projectXml.find ("reference=\"") != std::string::npos);
    CHECK (dawProjectForeignPlugins (out.bytes).size() == 2);

    std::map<std::string, std::vector<std::uint8_t>> saved;
    FlpImportReport report;
    DawProjectImportOptions io;
    io.common.report = &report;
    io.common.saveEmbedded = [&saved] (const std::vector<std::uint8_t>& bytes, const std::string& name) {
        saved["/imported/" + name] = bytes;
        return "/imported/" + name;
    };
    std::string err;
    const auto back = importDawProject (out.bytes, io, &err);
    CHECK (back.has_value() && err.empty());
    if (! back)
        return;
    const auto& b = *back;

    // transport, meter, info
    CHECK (b.bpm == 128.0 && b.beatsPerBar == 3 && b.beatUnit == 4);
    CHECK (b.info.title == a.info.title && b.info.author == a.info.author && b.info.genre == a.info.genre && b.info.url == a.info.url
           && b.info.comments == a.info.comments);

    // tracks: names, colours, folder nesting, modes
    CHECK (b.playlistTracks.size() >= a.playlistTracks.size());
    for (std::size_t i = 0; i < a.playlistTracks.size() && i < b.playlistTracks.size(); ++i)
    {
        const auto& x = a.playlistTracks[i];
        const auto& y = b.playlistTracks[i];
        CHECK (x.name == y.name && x.colour == y.colour && x.grouped == y.grouped && x.nest == y.nest && x.mode == y.mode && x.mixerTrack == y.mixerTrack
               && x.channel == y.channel);
    }
    // mixer: names, colours, levels, mute / solo, routing, sends
    CHECK (b.mixer.size() == a.mixer.size());
    for (std::size_t i = 0; i < a.mixer.size() && i < b.mixer.size(); ++i)
    {
        const auto& x = a.mixer[i];
        const auto& y = b.mixer[i];
        CHECK (near (x.volume, y.volume) && near (x.pan, y.pan) && x.muted == y.muted && x.solo == y.solo && x.toMaster == y.toMaster);
        if (i > 0)
            CHECK (x.name == y.name && x.colour == y.colour);
        CHECK (x.sends.size() == y.sends.size());
        for (std::size_t k = 0; k < x.sends.size() && k < y.sends.size(); ++k)
            CHECK (x.sends[k].target == y.sends[k].target && near (x.sends[k].level, y.sends[k].level) && x.sends[k].pre == y.sends[k].pre);
        CHECK (x.effects.size() == y.effects.size());
    }
    // LYNX's own effects come back exactly; the VST3's state bytes and class id
    if (b.mixer.size() == a.mixer.size())
    {
        const auto& rev = b.mixer[1].effects.front();
        CHECK (rev.kind == EffectKind::reverb && rev.params == a.mixer[1].effects.front().params && rev.mix == 0.8f && rev.presetName == "Big \"Hall\"");
        CHECK (b.mixer[5].effects.front().kind == EffectKind::delay && b.mixer[5].effects.front().params == a.mixer[5].effects.front().params);
        const auto& v3 = b.mixer[4].effects.front();
        std::vector<std::uint8_t> comp, cont;
        CHECK (v3.kind == EffectKind::plugin && ! v3.enabled && v3.plugin.name == "TestVerb3" && v3.plugin.foreign.format == ForeignPlugin::Format::vst3);
        CHECK (v3.plugin.foreign.hasClassId && v3.plugin.foreign.classId == r.vst3Class);
        CHECK (splitFlVst3State (v3.plugin.foreign.state, comp, cont) && comp == r.vst3Component && cont == r.vst3Controller);
    }
    // channels: the native synth (its sound), the VST2 (its state, byte for byte), the audio clip channel with its warps
    CHECK (b.channels.size() == 3);
    if (b.channels.size() == 3)
    {
        CHECK (b.channels[0].name == "Lead" && b.channels[0].instrument == InstrumentKind::synth && b.channels[0].synth == a.channels[0].synth
               && b.channels[0].mixerTrack == 1 && b.channels[0].colour == 0xFF112233 && b.channels[0].volume == 1.0f);
        const auto& k = b.channels[1];
        CHECK (k.instrument == InstrumentKind::plugin && k.plugin.name == "TestSynth" && k.plugin.foreign.format == ForeignPlugin::Format::vst2
               && k.plugin.foreign.vst2Id == 0x54535931 && k.plugin.foreign.state == r.vst2State && k.mixerTrack == 4);
        const auto& au = b.channels[2];
        CHECK (au.instrument == InstrumentKind::audioClip && au.samplePath == "/imported/Loop 90.wav" && saved["/imported/Loop 90.wav"] == r.wav);
        CHECK (au.sampler.warp == a.channels[2].sampler.warp && au.sampler.stretchBeats == 8.0f && au.sampler.keepPitch && au.mixerTrack == 3);
        CHECK (au.name == "Loop" && au.volume == audioClipUnityVolume && au.sampler.declickMode == audioClipDeclickMode);
    }
    // notes and clips
    CHECK (b.patterns.size() == 2);
    if (b.patterns.size() == 2)
        for (std::size_t p = 0; p < 2; ++p)
        {
            CHECK (b.patterns[p].name == a.patterns[p].name && b.patterns[p].colour == a.patterns[p].colour);
            CHECK (b.patterns[p].notes.size() == a.patterns[p].notes.size());
            for (std::size_t n = 0; n < a.patterns[p].notes.size() && n < b.patterns[p].notes.size(); ++n)
            {
                const auto& x = a.patterns[p].notes[n];
                const auto& y = b.patterns[p].notes[n];
                CHECK (x.start == y.start && x.length == y.length && x.key == y.key && x.velocity == y.velocity && x.release == y.release && x.channel == y.channel);
            }
        }
    int clips = 0;
    for (const auto& c : b.playlist)
        clips += c.kind != ClipKind::automation ? 1 : 0;
    CHECK (clips == 4);
    for (const auto& c : a.playlist)
    {
        if (c.kind == ClipKind::automation)
            continue;
        const auto* y = clipAt (b, c.kind, c.track, c.start);
        CHECK (y != nullptr);
        if (y == nullptr)
            continue;
        CHECK (y->length == c.length && y->offset == c.offset && y->muted == c.muted);
        if (c.kind == ClipKind::pattern)
            CHECK (y->pattern == c.pattern);
        else
            CHECK (y->channel == c.channel && y->fadeIn == c.fadeIn && y->fadeOut == c.fadeOut && y->offsetFraction == 0.0f);
    }
    // automation: each lane's points, on the same rows
    for (const auto& x : a.automations)
    {
        if (x.target.kind == AutomationTargetKind::tempo)
            continue;
        const auto* y = automationOf (b, x.target);
        CHECK (y != nullptr);
        if (y == nullptr)
            continue;
        CHECK (y->name == x.name && y->points.size() == x.points.size());
        for (std::size_t i = 0; i < x.points.size() && i < y->points.size(); ++i)
            CHECK (y->points[i].tick == x.points[i].tick && near (y->points[i].value, x.points[i].value) && y->points[i].mode == x.points[i].mode);
    }
    CHECK (laneHas (b, 0, { AutomationTargetKind::mixerVolume, 1, 0, 0 }) && laneHas (b, 0, { AutomationTargetKind::mixerSend, 1, 5, 0 }));
    CHECK (laneHas (b, 0, { AutomationTargetKind::effectParam, 1, 0, 3 }) && laneHas (b, 4, { AutomationTargetKind::channelParam, 1, 0, 7 }));
    // the tempo map and the markers
    const auto ta = tempomarkers::list (a), tb = tempomarkers::list (b);
    CHECK (ta.size() == 2 && tb.size() == ta.size());
    for (std::size_t i = 0; i < ta.size() && i < tb.size(); ++i)
        CHECK (ta[i].tick == tb[i].tick && near (ta[i].bpm, tb[i].bpm, 0.01) && ta[i].glide == tb[i].glide);
    CHECK (near (tempomarkers::startTempo (b), 128.0, 0.01));
    // the tempo markers' clip comes back on the row it was on (its lane carries the tempo too)
    const auto* tca = tempomarkers::clipOf (a, tempomarkers::automationIndex (a));
    const auto* tcb = tempomarkers::clipOf (b, tempomarkers::automationIndex (b));
    CHECK (tca != nullptr && tcb != nullptr && tca->track == tcb->track && tcb->start == 0);
    CHECK (b.markers().size() == a.markers().size());
    for (const auto& m : a.markers())
        CHECK (std::any_of (b.markers().begin(), b.markers().end(), [&m] (const Marker& n) {
            return n.tick == m.tick && n.name == m.name && n.numerator == m.numerator && n.denominator == m.denominator && n.action == m.action;
        }));
    // nothing went missing on the way
    CHECK (report.unsupported.empty() && report.notReproduced.empty() && report.droppedEffects.empty() && report.missingSamples == 0);

    // and again: the second trip changes nothing more
    const auto again = exportDawProject (b, eo);
    const auto third = importDawProject (again.bytes, io, &err);
    CHECK (third && third->channels.size() == b.channels.size() && third->playlist.size() == b.playlist.size() && third->mixer.size() == b.mixer.size()
           && third->automations.size() == b.automations.size() && third->patterns.size() == b.patterns.size());
}

TEST (dawproject190jf_bitwig_readme_example)
{
    using namespace lynx;
    using namespace dawproject190jf;
    const auto xmlText = std::string (dawprojectReadmeExample);
    const std::vector<std::uint8_t> loop (64, 7);
    const auto file = zipOf ({ { "project.xml", bytesOf (xmlText) }, { "audio/Drumfunk3 170bpm.wav", loop } });
    CHECK (isDawProjectData (file.data(), file.size()));
    FlpImportReport report;
    DawProjectImportOptions io;
    io.common.report = &report;
    std::string savedName;
    io.common.saveEmbedded = [&] (const std::vector<std::uint8_t>& bytes, const std::string& name) {
        savedName = bytes == loop ? name : std::string();
        return "/imported/" + name;
    };
    std::string err;
    const auto song = importDawProject (file, io, &err);
    CHECK (song.has_value() && err.empty());
    if (! song)
        return;
    // what the XML holds: 3 tracks (one of them the master), 9 notes in one clip, one audio clip with 2 warps, 149 BPM 4/4
    CHECK (song->bpm == 149.0 && song->beatsPerBar == 4 && song->beatUnit == 4);
    CHECK (song->playlistTracks.size() == 2 && song->playlistTracks[0].name == "Bass" && song->playlistTracks[1].name == "Drumloop");
    CHECK (song->playlistTracks[0].colour == 0xFFA2EABFu && song->playlistTracks[0].mode == PlaylistTrackMode::instrument);
    CHECK (song->playlistTracks[1].mode == PlaylistTrackMode::audio && song->playlistTracks[1].mixerTrack == 2);
    CHECK (song->mixer.size() == 3 && song->mixer[1].name == "Bass" && song->mixer[2].name == "Drumloop");
    CHECK (near (song->mixer[1].volume, faderPosition (0.659140f)) && near (song->mixer[2].volume, faderPosition (0.177125f)) && near (song->mixer[1].pan, 0.0));
    CHECK (song->mixer[1].toMaster && song->mixer[2].toMaster && song->mixer[1].sends.empty());
    CHECK (song->channels.size() == 2);
    if (song->channels.size() == 2)
    {
        const auto& bass = song->channels[0];
        CHECK (bass.instrument == InstrumentKind::plugin && bass.plugin.foreign.format == ForeignPlugin::Format::clap
               && bass.plugin.foreign.clapId == "org.surge-synth-team.surge-xt" && bass.plugin.name == "Surge XT" && bass.mixerTrack == 1);
        const auto& audio = song->channels[1];
        CHECK (audio.instrument == InstrumentKind::audioClip && audio.samplePath == "/imported/Drumfunk3 170bpm.wav" && savedName == "Drumfunk3 170bpm.wav");
        CHECK (audio.sampler.warp.size() == 2 && near (audio.sampler.warp[1].seconds, 2.823541666666667, 1e-9) && near (audio.sampler.warp[1].beats, 8.00003433227539, 1e-9));
        CHECK (near (audio.sampler.stretchBeats, 8.00003433227539, 1e-4) && audio.sampler.keepPitch && audio.mixerTrack == 2);
        CHECK (audio.name == "Drumfunk3 170bpm");
    }
    CHECK (song->patterns.size() == 1 && song->patterns[0].notes.size() == 9);
    if (song->patterns.size() == 1 && song->patterns[0].notes.size() == 9)
    {
        const auto& n = song->patterns[0].notes;
        CHECK (n.front().start == 0 && n.front().length == 240 && n.front().key == 65 && near (n.front().velocity, 0.787402));
        CHECK (std::any_of (n.begin(), n.end(), [] (const Note& x) { return x.start == 1440 && x.length == 2400 && x.key == 53; }));
    }
    CHECK (song->playlist.size() == 2);
    const auto* pc = clipAt (*song, ClipKind::pattern, 0, 0);
    const auto* ac = clipAt (*song, ClipKind::audio, 1, 0);
    CHECK (pc != nullptr && pc->length == 7680 && pc->offset == 0);
    CHECK (ac != nullptr && ac->length == std::llround (8.00003433227539 * 960) && ac->offset == 0 && ac->fadeIn == 0);
    // the CLAP state file is not in this example: reported, the plug-in starts at its defaults
    CHECK (reported (report.notReproduced, "Plug-in state") && reported (report.converted, "Surge XT"));
    // without the audio file inside: reported, no clip
    FlpImportReport r2;
    io.common.report = &r2;
    const auto noAudio = importDawProject (zipOf ({ { "project.xml", bytesOf (xmlText) } }), io, &err);
    // (the clip stays, its channel without a sample, as the .flp import keeps a channel whose sample is missing)
    CHECK (noAudio && noAudio->playlist.size() == 2 && reported (r2.notReproduced, "Embedded audio"));
    CHECK (noAudio && noAudio->channels.size() == 2 && noAudio->channels[1].samplePath.empty() && noAudio->channels[1].name == "Drumfunk3 170bpm");
}

TEST (dawproject190jf_handmade_features)
{
    using namespace lynx;
    using namespace dawproject190jf;
    const std::string project = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Application name="Handmade" version="1"/>
  <Transport>
    <Tempo id="t" unit="bpm" value="120" min="20" max="666"/>
    <TimeSignature id="ts" numerator="7" denominator="8"/>
  </Transport>
  <Structure>
    <Track id="fold" name="Band" contentType="tracks" color="#ff0000">
      <Channel id="foldCh" role="submix" destination="mst">
        <Devices>
          <Compressor id="cmp" deviceName="Comp" deviceRole="audioFX">
            <Attack unit="seconds" value="0.01"/>
            <OutputGain unit="decibel" value="3"/>
            <Ratio unit="linear" value="4"/>
            <Release unit="seconds" value="0.2"/>
            <Threshold id="cmpT" unit="decibel" value="-20"/>
          </Compressor>
        </Devices>
        <Volume id="foldVol" unit="decibel" value="-6"/>
      </Channel>
      <Track id="gtr" name="Guitar" contentType="audio">
        <Channel id="gtrCh" role="regular" destination="foldCh">
          <Devices>
            <Equalizer deviceName="EQ" deviceRole="audioFX">
              <Band type="highPass" order="2"><Freq unit="hertz" value="80"/></Band>
              <Band type="bell"><Freq unit="hertz" value="2500"/><Gain unit="decibel" value="-3"/><Q unit="linear" value="1.4"/></Band>
              <Band type="bandPass"><Freq unit="hertz" value="500"/></Band>
            </Equalizer>
            <Vst2Plugin deviceID="1415534897" deviceName="FakeVerb" deviceRole="audioFX"><State path="plugins/fakeverb.fxp"/></Vst2Plugin>
            <BuiltinDevice deviceName="Mystery" deviceRole="audioFX"/>
          </Devices>
          <Pan id="gtrPan" unit="normalized" min="0" max="1" value="0.25"/>
          <Sends><Send destination="fxCh" type="pre"><Volume unit="linear" value="0.5"/></Send></Sends>
          <Volume unit="linear" value="1"/>
        </Channel>
      </Track>
      <Track id="syn" name="Synth" contentType="notes">
        <Channel id="synCh" role="regular" destination="foldCh">
          <Devices><Device deviceName="Some Arp" deviceRole="noteFX"/></Devices>
        </Channel>
      </Track>
    </Track>
    <Track id="fx" name="Hall" contentType="audio">
      <Channel id="fxCh" role="effect" destination="mst"><Volume unit="linear" value="0.5"/></Channel>
    </Track>
    <Track id="m" name="Main" contentType="audio notes"><Channel id="mst" role="master"><Volume unit="linear" value="1"/></Channel></Track>
  </Structure>
  <Arrangement id="arr">
    <Lanes timeUnit="beats">
      <Lanes track="syn">
        <Clips>
          <Clip time="4" duration="6" playStart="1" loopStart="0" loopEnd="2" name="Riff">
            <Notes>
              <Note time="0" duration="0.5" channel="0" key="60" vel="0.5" rel="0.5"/>
              <Note time="1" duration="0.5" channel="0" key="62" vel="0.5" rel="0.5"/>
              <Note time="1.5" duration="0.25" channel="1" key="64" vel="0.5" rel="0.5"/>
            </Notes>
          </Clip>
        </Clips>
      </Lanes>
      <Lanes track="gtr" timeUnit="seconds">
        <Clips>
          <Clip time="1" duration="2" contentTimeUnit="seconds" playStart="0.5" fadeTimeUnit="seconds" fadeInTime="0.25">
            <Audio channels="2" sampleRate="48000" duration="4"><File path="takes/gtr.wav" external="true"/></Audio>
          </Clip>
        </Clips>
        <Points unit="normalized"><Target parameter="gtrPan"/><RealPoint time="0" value="0.5" interpolation="linear"/><RealPoint time="2" value="1"/></Points>
      </Lanes>
      <Lanes track="fold">
        <Points><Target parameter="foldVol"/><RealPoint time="0" value="-6"/><RealPoint time="8" value="0"/></Points>
        <Points><Target parameter="cmpT"/><RealPoint time="0" value="-20"/></Points>
      </Lanes>
      <Lanes track="m"><Points><Target expression="pitchBend" channel="0"/><RealPoint time="0" value="0"/></Points></Lanes>
    </Lanes>
    <Markers><Marker time="0" name="Intro" color="#00ff00"/><Marker time="16" name="Drop"/></Markers>
    <TempoAutomation unit="bpm"><Target parameter="t"/><RealPoint time="0" value="120"/><RealPoint time="8" value="120" interpolation="linear"/><RealPoint time="16" value="180"/></TempoAutomation>
    <TimeSignatureAutomation><Target parameter="ts"/><TimeSignaturePoint time="14" numerator="4" denominator="4"/></TimeSignatureAutomation>
  </Arrangement>
  <Scenes>
    <Scene name="A" color="#0000ff"><Lanes>
      <ClipSlot track="syn" hasStop="true"><Clip time="0" duration="2" playStart="1" loopStart="0" loopEnd="2" name="Loop A"><Notes><Note time="0" duration="1" channel="0" key="72"/></Notes></Clip></ClipSlot>
      <ClipSlot track="gtr" hasStop="false"/>
    </Lanes></Scene>
    <Scene name="B"><Lanes/></Scene>
  </Scenes>
</Project>
)";
    const std::string metadata = "<MetaData><Title>Hand</Title><Artist>Me</Artist><Album>X</Album><Comment>c</Comment></MetaData>";
    pluginpresets::FxFile fx;
    fx.kind = pluginpresets::FxFile::Kind::chunkProgram;
    fx.pluginId = 1415534897;
    fx.programName = "Room";
    fx.chunk = { 5, 6, 7 };
    const auto file = zipOf ({ { "project.xml", bytesOf (project) }, { "metadata.xml", bytesOf (metadata) }, { "plugins/fakeverb.fxp", pluginpresets::writeFx (fx) } });
    FlpImportReport report;
    DawProjectImportOptions io;
    io.common.report = &report;
    io.common.projectFolder = "/proj";
    io.common.fileExists = [] (const std::string& p) { return p == "/proj/takes/gtr.wav"; };
    std::string err;
    const auto song = importDawProject (file, io, &err);
    CHECK (song.has_value());
    if (! song)
        return;
    const auto& s = *song;
    // structure: the folder and its bus, two members, the effect return without a row, the master
    // (row 3: the tempo markers' clip, on a row of its own at the end: the project names no row for it)
    CHECK (s.playlistTracks.size() == 4 && s.mixer.size() == 5);
    if (s.playlistTracks.size() != 4 || s.mixer.size() != 5)
        return;
    CHECK (s.playlistTracks[0].name == "Band" && ! s.playlistTracks[0].grouped && s.playlistTracks[0].mode == PlaylistTrackMode::audio && s.playlistTracks[0].mixerTrack == 1);
    CHECK (s.playlistTracks[1].name == "Guitar" && s.playlistTracks[1].grouped && s.playlistTracks[1].nest == 0 && s.playlistTracks[1].mixerTrack == 2);
    CHECK (s.playlistTracks[2].name == "Synth" && s.playlistTracks[2].grouped && s.playlistTracks[2].mode == PlaylistTrackMode::instrument);
    CHECK (s.mixer[1].colour == 0xFFFF0000u && near (s.mixer[1].volume, faderPosition (0.501187f)) && s.mixer[1].toMaster);
    CHECK (! s.mixer[2].toMaster && s.mixer[2].sends.size() == 2 && s.mixer[2].sends[0].target == 1 && s.mixer[2].sends[0].level == 1.0f);
    CHECK (s.mixer[2].sends[1].target == 4 && s.mixer[2].sends[1].pre && near (s.mixer[2].sends[1].level, 0.5));
    CHECK (near (s.mixer[2].pan, -0.5) && s.mixer[4].name == "Hall" && near (s.mixer[4].volume, faderPosition (0.5f)));
    CHECK (! s.mixer[3].toMaster && s.mixer[3].sends.size() == 1 && s.mixer[3].sends[0].target == 1);
    // devices: the compressor → Crest, the EQ → Facet (the band-pass left out), the VST2 with its program chunk
    CHECK (s.mixer[1].effects.size() == 1 && s.mixer[1].effects[0].kind == EffectKind::crestCompressor);
    CHECK (s.mixer[2].effects.size() == 2 && s.mixer[2].effects[0].kind == EffectKind::facetEq && s.mixer[2].effects[1].kind == EffectKind::plugin);
    if (s.mixer[2].effects.size() == 2)
    {
        const auto& v2 = s.mixer[2].effects[1].plugin.foreign;
        FlVst2State st;
        CHECK (v2.format == ForeignPlugin::Format::vst2 && v2.vst2Id == 1415534897u && parseFlVst2State (v2.state, st) && st.chunk == fx.chunk);
        CHECK (reported (report.approximated, "program chunk"));
    }
    CHECK (reported (report.droppedEffects, "Mystery") && reported (report.approximated, "bandPass") && reported (report.notReproduced, "Note effect: Some Arp"));
    CHECK (reported (report.unsupported, "Notes track without an instrument") && s.channels.size() == 2 && s.channels[0].instrument == InstrumentKind::synth);
    // the looped notes clip: four passes over the loop, one pattern
    CHECK (s.patterns.size() == 2 && s.patterns[0].name == "Riff" && s.patterns[0].notes.size() == 3 && reported (report.approximated, "MIDI channels"));
    std::vector<std::tuple<std::int64_t, std::int64_t, std::int64_t>> passes;
    for (const auto& c : s.playlist)
        if (c.kind == ClipKind::pattern)
            passes.push_back ({ c.start, c.length, c.offset });
    CHECK ((passes == std::vector<std::tuple<std::int64_t, std::int64_t, std::int64_t>> { { 3840, 960, 960 }, { 4800, 1920, 0 }, { 6720, 1920, 0 }, { 8640, 960, 0 } }));
    // the guitar's audio, in seconds: 1 s → beat 2 at 120 BPM, its offset 0.5 s, its fade a quarter second; found next to the project
    const auto* g = clipAt (s, ClipKind::audio, 1, 1920);
    CHECK (g != nullptr && g->length == 3840 && g->offset == 960 && g->fadeIn == 480);
    CHECK (s.channels[1].samplePath == "/proj/takes/gtr.wav" && s.channels[1].sampler.warp.empty() && report.missingSamples == 0 && report.relocatedSamples == 0);
    // automation: the bus volume (dB) and the guitar's pan (seconds); the compressor's threshold and the pitch bend reported
    const auto* vol = automationOf (s, { AutomationTargetKind::mixerVolume, 1, 0, 0 });
    CHECK (vol != nullptr && vol->points.size() == 2 && vol->points[1].tick == 7680 && near (vol->points[1].value, faderPosition (1.0f)) && laneHas (s, 0, vol->target));
    const auto* pan = automationOf (s, { AutomationTargetKind::mixerPan, 2, 0, 0 });
    CHECK (pan != nullptr && pan->points.size() == 2 && pan->points[0].mode == flcurve::singleCurve && pan->points[1].tick == 3840 && near (pan->points[1].value, 1.0));
    CHECK (reported (report.notReproduced, "built-in device's Threshold") && reported (report.notReproduced, "pitchBend"));
    // tempo map, meter and its change, markers, scenes, metadata
    const auto* tempoClip = tempomarkers::clipOf (s, tempomarkers::automationIndex (s));
    CHECK (tempoClip != nullptr && tempoClip->track == 3 && s.playlistTracks[3].name == "Tempo" && ! s.playlistTracks[3].grouped);
    const auto marks = tempomarkers::list (s);
    CHECK (marks.size() == 2 && marks[0].tick == 7680 && ! marks[0].glide && marks[1].tick == 15360 && marks[1].glide && near (marks[1].bpm, 180.0, 0.01));
    CHECK (s.beatsPerBar == 7 && s.beatUnit == 8);
    CHECK (std::any_of (s.markers().begin(), s.markers().end(), [] (const Marker& m) { return m.tick == 13440 && m.numerator == 4 && m.action == Marker::timeSignature; }));
    CHECK (std::any_of (s.markers().begin(), s.markers().end(), [] (const Marker& m) { return m.tick == 15360 && m.name == "Drop"; }));
    CHECK (reported (report.notReproduced, "Marker colours"));
    // the scenes → the clip grid: scene A's clip on the Synth row, its loop with the Start marker a beat in; scene B empty
    CHECK (s.clipGrid.has_value() && s.patterns.size() == 2);
    if (s.clipGrid)
    {
        const auto& g = *s.clipGrid;
        CHECK (g.scenes.size() >= 2 && g.scenes[0].name == "A" && g.scenes[0].colour == 0xFF0000FFu && g.scenes[1].name == "B");
        const auto at = std::find_if (g.slots.begin(), g.slots.end(), [] (const ClipGridSlot& x) { return x.name == "Loop A"; });
        CHECK (at != g.slots.end() && at->scene == 0 && at->loop && at->startMarker == 960);
        const auto clip = std::find_if (g.clips.begin(), g.clips.end(), [&] (const Clip& c) { return c.gridSlot == (int) (at - g.slots.begin()); });
        CHECK (clip != g.clips.end() && clip->track == 2 && clip->pattern == 1 && clip->offset == 0 && clip->length == 1920);
        CHECK (std::any_of (g.slots.begin(), g.slots.end(), [] (const ClipGridSlot& x) { return x.noStop && x.scene == 0; }));
    }
    CHECK (s.info.title == "Hand" && s.info.author == "Me" && s.info.comments == "c" && reported (report.notReproduced, "Project info: Album"));
    // a missing external file: in the Missing files list; the clip stays, its channel without a sample until it is located
    FlpImportReport r2;
    io.common.report = &r2;
    io.common.fileExists = [] (const std::string&) { return false; };
    const auto missing = importDawProject (file, io, &err);
    CHECK (missing && r2.missingSamples == 1 && r2.missingFiles.size() == 1 && r2.missingFiles[0].path == "takes/gtr.wav");
    CHECK (missing && clipAt (*missing, ClipKind::audio, 1, 1920) != nullptr && missing->channels.size() == 2 && missing->channels[1].samplePath.empty());
    // located: the import again with the relocation finds it
    io.common.relocations["takes/gtr.wav"] = "/elsewhere/gtr.wav";
    io.common.fileExists = [] (const std::string& p) { return p == "/elsewhere/gtr.wav"; };
    const auto located = importDawProject (file, io, &err);
    CHECK (located && located->channels.size() == 2 && located->channels[1].samplePath == "/elsewhere/gtr.wav");
}

TEST (dawproject190jf_robustness)
{
    using namespace lynx;
    using namespace dawproject190jf;
    std::string err;
    const auto fails = [&err] (const std::vector<std::uint8_t>& bytes) {
        err.clear();
        return ! importDawProject (bytes, {}, &err).has_value() && ! err.empty();
    };
    CHECK (fails ({}));
    CHECK (fails (bytesOf ("this is not a zip file at all, just text")));
    CHECK (fails (zipOf ({ { "metadata.xml", bytesOf ("<MetaData/>") } }))); // no project.xml
    CHECK (fails (zipOf ({ { "project.xml", bytesOf ("<Project><Structure></Project>") } }))); // malformed
    CHECK (fails (zipOf ({ { "project.xml", bytesOf ("<Ableton/>") } })));                    // not a project
    const auto good = zipOf ({ { "project.xml", bytesOf (dawprojectReadmeExample) } });
    CHECK (importDawProject (good, {}, &err).has_value());
    CHECK (fails ({ good.begin(), good.begin() + (std::ptrdiff_t) good.size() / 2 })); // truncated
    CHECK (! isDawProjectData (good.data(), good.size() / 2) && ! isDawProjectData (nullptr, 0));
    // every byte flipped in turn (a sample of them): an error or a song, never a crash
    int songs = 0, errors = 0;
    for (std::size_t i = 0; i < good.size(); i += 7)
    {
        auto bad = good;
        bad[i] ^= 0x5A;
        const auto s = importDawProject (bad, {}, &err);
        (s ? songs : errors)++;
    }
    CHECK (songs + errors > 100 && errors > 0);
    // odd but well-formed content: no times, huge loops, references to nothing, a clip inside nothing
    const std::string odd = R"(<Project version="1.0"><Application name="x" version="1"/>
      <Structure><Track id="a" contentType="notes"><Channel id="c" role="regular" destination="nowhere"/></Track></Structure>
      <Arrangement><Lanes><Lanes track="a"><Clips>
        <Clip time="0" duration="100000" loopStart="0" loopEnd="0.001"><Notes><Note key="999" time="-5" duration="1" channel="0"/></Notes></Clip>
        <Clip time="1" duration="1" reference="none"/>
        <Clip time="2" duration="1"/>
      </Clips><Points><Target parameter="missing"/><RealPoint time="0" value="1"/></Points></Lanes><Lanes track="zzz"><Clips><Clip time="0" duration="1"><Notes/></Clip></Clips></Lanes></Lanes></Arrangement>
    </Project>)";
    FlpImportReport report;
    DawProjectImportOptions io;
    io.common.report = &report;
    const auto s = importDawProject (zipOf ({ { "project.xml", bytesOf (odd) } }), io, &err);
    CHECK (s && s->playlist.size() <= 4096 && ! s->patterns.empty());
    CHECK (s && std::all_of (s->patterns.front().notes.begin(), s->patterns.front().notes.end(), [] (const Note& n) { return n.key <= 127 && n.start >= 0; }));
    CHECK (reported (report.notReproduced, "missing content") && reported (report.approximated, "unknown or looping destination"));
}

TEST (dawproject190jf_what_does_not_map)
{
    using namespace lynx;
    using namespace dawproject190jf;
    auto r = richSong();
    auto& s = r.song;
    s.patterns[0].notes[0].pan = 0.5f;
    s.patterns[0].notes[1].muted = true;
    s.playlist[3].gain = 0.5f;
    s.loopStart = 0;
    s.loopEnd = 3840;
    s.vcas.push_back ({});
    launcher::ensureGrid (s);
    s.arrangements.push_back (Arrangement { "B", {}, 0, 0, {} });
    s.mixer[2].masterLevel = 0.5f;
    s.channels[0].arp.direction = 1;
    s.automations[0].points[1].tension = 0.4f;
    Automation chVol;
    chVol.target = { AutomationTargetKind::channelVolume, 0, 0, 0 };
    chVol.points = { { 0, 0.5f } };
    s.automations.push_back (chVol);
    s.playlistTracks[0].lanes.push_back ({ (int) s.automations.size() - 1, 0 });
    const auto out = exportDawProject (s); // (no readFile: the audio is referenced where it is)
    CHECK (! out.bytes.empty() && out.projectXml.find ("external=\"true\"") != std::string::npos);
    for (const auto* part : { "Note pan", "Muted notes", "Audio clip gain", "song's loop", "VCA faders", "clip grid", "other than the current",
                              "Insert-to-Master", "arpeggiators", "Curved automation", "has no parameter for", "length was not known" })
    {
        const auto found = hasNote (out.notes, part);
        CHECK (found);
        if (! found)
            std::printf ("    (missing note: %s)\n", part);
    }
    // a song with nothing in it still makes a valid project: the master and the transport
    Song empty;
    const auto e = exportDawProject (empty);
    const auto back = importDawProject (e.bytes);
    CHECK (back && back->mixer.size() == 1 && back->playlistTracks.empty() && back->patterns.size() == 1);
}

TEST (dawproject190jf_fl_layout)
{
    // An .flp's usual layout (round 190jf e2e on FL's demo "Thief"): unassigned rows with one channel's pattern clips, rows
    // of automation clips, named empty rows, audio clips whose samples are not on this computer (no path), a pattern not
    // on the Playlist. The rows, their names and folders survive; each channel's notes go to its row's track.
    using namespace lynx;
    using namespace dawproject190jf;
    Song s;
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    s.playlistTracks.clear();
    s.mixer = Song::defaultMixer (4);
    s.mode = PlayMode::song;
    for (const auto* name : { "Bass", "Drum Loop" })
    {
        ChannelDef c;
        c.name = name;
        c.mixerTrack = 1;
        s.channels.push_back (c);
    }
    s.channels[1].instrument = InstrumentKind::audioClip; // its sample was not found: no path
    s.channels[1].mixerTrack = 2;
    const auto row = [&s] (const char* name, bool grouped) {
        PlaylistTrack t;
        t.name = name;
        t.grouped = grouped;
        s.playlistTracks.push_back (t);
    };
    row ("BASSES", false);
    row ("Bass", true);
    row ("Bass Cutoff", true);
    row ("Empty but named", true);
    row ("Loop", false);
    Pattern p;
    p.name = "Bass line";
    Note n;
    n.channel = 0;
    p.notes.push_back (n);
    s.patterns.push_back (p);
    s.patterns.push_back (p); // never placed
    Clip pc;
    pc.kind = ClipKind::pattern;
    pc.track = 1;
    pc.length = 3840;
    s.playlist.push_back (pc);
    Automation a;
    a.name = "Bass volume";
    a.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
    a.points = { { 0, 0.2f }, { 1920, 0.7f } };
    s.automations.push_back (a);
    Clip ac;
    ac.kind = ClipKind::automation;
    ac.automation = 0;
    ac.track = 2;
    ac.start = 960;
    ac.length = 1920;
    s.playlist.push_back (ac);
    Clip au;
    au.kind = ClipKind::audio;
    au.channel = 1;
    au.track = 4;
    au.start = 0;
    au.length = 7680;
    s.playlist.push_back (au);
    for (int k = 1; k <= 40; ++k) // (many clips of one file: the file is looked for once)
    {
        au.start = k * 7680;
        s.playlist.push_back (au);
    }

    const auto out = exportDawProject (s);
    CHECK (hasNote (out.notes, "missing here") && hasNote (out.notes, "not placed on the Playlist"));
    FlpImportReport report;
    DawProjectImportOptions io;
    io.common.report = &report;
    int probes = 0;
    io.common.fileExists = [&probes] (const std::string&) { ++probes; return false; };
    const auto b = importDawProject (out.bytes, io);
    CHECK (b.has_value() && probes == 1);
    if (! b)
        return;
    CHECK (b->playlistTracks.size() == 5);
    if (b->playlistTracks.size() != 5)
        return;
    const char* names[] { "BASSES", "Bass", "Bass Cutoff", "Empty but named", "Loop" };
    for (int i = 0; i < 5; ++i)
        CHECK (b->playlistTracks[(std::size_t) i].name == names[i] && b->playlistTracks[(std::size_t) i].grouped == s.playlistTracks[(std::size_t) i].grouped);
    // the channel took its row; the automation sits in its own row's lane (song time: the clip's points moved by its start)
    CHECK (b->playlistTracks[1].mode == PlaylistTrackMode::instrument && clipAt (*b, ClipKind::pattern, 1, 0) != nullptr);
    const auto* vol = automationOf (*b, { AutomationTargetKind::mixerVolume, 1, 0, 0 });
    CHECK (vol != nullptr && laneHas (*b, 2, vol->target) && vol->points.size() == 2 && vol->points[0].tick == 960 && vol->points[1].tick == 2880);
    // the audio clip: kept, its file listed as missing under the channel's name
    const auto* clip = clipAt (*b, ClipKind::audio, 4, 0);
    CHECK (clip != nullptr && clip->length == 7680 && report.missingSamples == 1 && report.missingFiles.size() == 1 && report.missingFiles[0].path == "Drum Loop.wav");
    CHECK (std::count_if (b->playlist.begin(), b->playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::audio; }) == 41);
}
