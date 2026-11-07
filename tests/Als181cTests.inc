// --- Live set (.als) Session view ↔ LYNX's clip grid, round 181c. The sets are written by the tests (only the elements
// the importer reads); the launch numbers are the ones read in Live 12's Clip View (docs/analise/REA_rodada181a.md).

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/ClipLauncher.h"

namespace als181c
{
using als175a::midiNote;
using als175a::v;

inline std::string follow (int a, int b, int chanceA, int chanceB, int jumpA, int jumpB, bool linked, int iterations, double time, bool on)
{
    return "<FollowAction>" + v ("FollowTime", std::to_string (time)) + v ("IsLinked", linked ? "true" : "false") +
           v ("LoopIterations", std::to_string (iterations)) + v ("FollowActionA", std::to_string (a)) + v ("FollowActionB", std::to_string (b)) +
           v ("FollowChanceA", std::to_string (chanceA)) + v ("FollowChanceB", std::to_string (chanceB)) + v ("JumpIndexA", std::to_string (jumpA)) +
           v ("JumpIndexB", std::to_string (jumpB)) + v ("FollowActionEnabled", on ? "true" : "false") + "</FollowAction>";
}

inline std::string loop (double start, double end, bool on, double startRelative = 0)
{
    return "<Loop>" + v ("LoopStart", std::to_string (start)) + v ("LoopEnd", std::to_string (end)) + v ("StartRelative", std::to_string (startRelative)) +
           v ("LoopOn", on ? "true" : "false") + v ("OutMarker", std::to_string (end)) + "</Loop>";
}

inline std::string slot (int id, const std::string& clip, bool hasStop = true)
{
    return "<ClipSlot Id=\"" + std::to_string (id) + "\"><ClipSlot><Value>" + clip + "</Value></ClipSlot>" + v ("HasStop", hasStop ? "true" : "false") + "</ClipSlot>";
}

// Tempo 120, 4/4, GlobalQuantisation 9 (1/8). A MIDI track "Keys": scene 1 a looping toggle clip (1/8, legato, follow
// actions on), scene 2 empty without its stop button, scene 3 a one-shot gate clip. An audio track "Beat": scene 1 a
// looping repeat clip. Scenes: Intro (tempo 140), Gap, Out (3/4). With `clips` false every slot is empty.
inline std::string sessionSet (bool clips = true)
{
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" Creator=\"test\">\n<LiveSet>";
    s += "<Tracks>";
    const auto a = clips ? "<MidiClip Id=\"0\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "4") + v ("Name", "Riff A") + v ("Color", "5") +
                               v ("LaunchMode", "2") + v ("LaunchQuantisation", "10") + v ("Legato", "true") + loop (1, 5, true) +
                               follow (4, 9, 70, 30, 0, 2, false, 3, 8, true) + "<Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + midiNote (1, 0.5, 100) +
                               "</Notes>" + v ("MidiKey", "60") + "</KeyTrack><KeyTrack Id=\"1\"><Notes>" + midiNote (4.5, 0.25, 80) + "</Notes>" +
                               v ("MidiKey", "62") + "</KeyTrack></KeyTracks></Notes></MidiClip>"
                         : std::string();
    const auto b = clips ? "<MidiClip Id=\"1\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "2") + v ("Name", "Hit B") + v ("LaunchMode", "1") +
                               v ("LaunchQuantisation", "0") + v ("Legato", "false") + loop (0, 2, false) +
                               "<Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + midiNote (0, 1, 90) + "</Notes>" + v ("MidiKey", "48") +
                               "</KeyTrack></KeyTracks></Notes></MidiClip>"
                         : std::string();
    s += "<MidiTrack Id=\"10\">" + als175a::name ("Keys") + v ("Color", "3") + v ("TrackGroupId", "-1") + "<DeviceChain>" +
         als175a::mixer (100, 1.0, 0.0, true) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting>" +
         "<MainSequencer><ClipTimeable><ArrangerAutomation><Events /></ArrangerAutomation></ClipTimeable><ClipSlotList>" + slot (0, a) +
         slot (1, "", false) + slot (2, b) + "</ClipSlotList></MainSequencer><DeviceChain><Devices /></DeviceChain></DeviceChain></MidiTrack>";
    const auto beat = clips ? "<AudioClip Id=\"2\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "4") + v ("Name", "Beat loop") +
                                  v ("IsWarped", "true") + v ("WarpMode", "0") + v ("LaunchMode", "3") + v ("LaunchQuantisation", "5") + loop (0, 4, true) +
                                  "<WarpMarkers><WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\" /><WarpMarker Id=\"1\" SecTime=\"2\" BeatTime=\"4\" /></WarpMarkers>" +
                                  "<SampleRef><FileRef>" + v ("RelativePathType", "0") + v ("Path", "E:/sets/beat.wav") + "</FileRef></SampleRef></AudioClip>"
                            : std::string();
    s += "<AudioTrack Id=\"20\">" + als175a::name ("Beat") + v ("Color", "20") + v ("TrackGroupId", "-1") + "<DeviceChain>" +
         als175a::mixer (200, 1.0, 0.0, true) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting>" +
         "<MainSequencer><Sample><ArrangerAutomation><Events /></ArrangerAutomation></Sample><ClipSlotList>" + slot (0, beat) + slot (1, "") + slot (2, "") +
         "</ClipSlotList></MainSequencer><DeviceChain><Devices /></DeviceChain></DeviceChain></AudioTrack>";
    s += "</Tracks>";
    s += "<MainTrack><DeviceChain>" +
         als175a::mixer (900, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"120\" /><AutomationTarget Id=\"950\" /></Tempo><TimeSignature><Manual Value=\"201\" /></TimeSignature>") +
         "</DeviceChain><AutomationEnvelopes><Envelopes /></AutomationEnvelopes></MainTrack>";
    s += "<Scenes><Scene Id=\"0\">" + v ("Name", "Intro") + v ("Tempo", "140") + v ("IsTempoEnabled", "true") + "</Scene><Scene Id=\"1\">" + v ("Name", "Gap") +
         "</Scene><Scene Id=\"2\">" + v ("Name", "Out") + v ("TimeSignatureId", "200") + v ("IsTimeSignatureEnabled", "true") + "</Scene></Scenes>";
    s += v ("GlobalQuantisation", "9");
    s += "</LiveSet>\n</Ableton>\n";
    return s;
}

inline lynx::AlsImportOptions options (lynx::FlpImportReport* report = nullptr)
{
    lynx::AlsImportOptions o;
    o.common.projectFolder = "E:/sets";
    o.common.fileExists = [] (const std::string&) { return true; };
    o.common.report = report;
    return o;
}

// The grid clip whose slot is named `n` (nullptr: none), and its settings
inline const lynx::Clip* clipNamed (const lynx::Song& s, const std::string& n, const lynx::ClipGridSlot** settings = nullptr)
{
    if (! s.clipGrid)
        return nullptr;
    for (const auto& c : s.clipGrid->clips)
        if (c.gridSlot >= 0 && c.gridSlot < (int) s.clipGrid->slots.size() && s.clipGrid->slots[(std::size_t) c.gridSlot].name == n)
        {
            if (settings != nullptr)
                *settings = &s.clipGrid->slots[(std::size_t) c.gridSlot];
            return &c;
        }
    return nullptr;
}

// Checks a song holds sessionSet's grid (straight from the set, or after a trip through the export)
inline void checkGrid (const lynx::Song& s)
{
    using namespace lynx;
    CHECK (s.clipGrid.has_value());
    if (! s.clipGrid)
        return;
    const auto& g = *s.clipGrid;
    CHECK (g.scenes.size() == 3);
    if (g.scenes.size() == 3)
    {
        CHECK (g.scenes[0].name == "Intro" && std::abs (g.scenes[0].tempo - 140.0) < 1e-9 && g.scenes[0].beatsPerBar == 0);
        CHECK (g.scenes[1].name == "Gap" && g.scenes[1].tempo == 0.0);
        CHECK (g.scenes[2].name == "Out" && g.scenes[2].beatsPerBar == 3);
    }
    CHECK (g.globalQuant == LaunchQuant::eighth); // GlobalQuantisation 9 = 1/8 [LIVE]

    const ClipGridSlot* a = nullptr;
    const auto* riff = clipNamed (s, "Riff A", &a);
    CHECK (riff != nullptr && a != nullptr);
    if (riff != nullptr && a != nullptr)
    {
        CHECK (a->scene == 0 && riff->kind == ClipKind::pattern && riff->start == 0);
        CHECK (riff->length == 4 * ppq); // the loop region 1…5
        CHECK (a->mode == LaunchMode::toggle && a->quant == LaunchQuant::eighth && a->legato && a->loop);
        CHECK (a->follow.enabled && a->follow.a == FollowKind::next && a->follow.b == FollowKind::jump);
        CHECK (a->follow.chanceA == 70 && a->follow.chanceB == 30 && a->follow.jumpB == 2);
        CHECK (! a->follow.linked && a->follow.iterations == 3 && a->follow.ticks == 8 * ppq);
        const auto& p = s.patterns[(std::size_t) riff->pattern];
        bool c4 = false, d4 = false;
        for (const auto& n : p.notes)
        {
            c4 = c4 || (n.key == 60 && n.start - riff->offset == 0);
            d4 = d4 || (n.key == 62 && n.start - riff->offset == 7 * ppq / 2);
        }
        CHECK (c4 && d4);
        CHECK (! launcher::hasStopButton (g, riff->track, 1)); // scene 2's empty slot has no stop button
        CHECK (launcher::clipAt (g, riff->track, 1) == nullptr);
    }
    const ClipGridSlot* b = nullptr;
    const auto* hit = clipNamed (s, "Hit B", &b);
    CHECK (hit != nullptr && b != nullptr);
    if (hit != nullptr && b != nullptr)
    {
        CHECK (b->scene == 2 && hit->length == 2 * ppq && ! b->loop);
        CHECK (b->mode == LaunchMode::gate && b->quant == LaunchQuant::global && ! b->legato && ! b->follow.enabled);
        CHECK (riff == nullptr || hit->track == riff->track);
    }
    const ClipGridSlot* l = nullptr;
    const auto* beat = clipNamed (s, "Beat loop", &l);
    CHECK (beat != nullptr && l != nullptr);
    if (beat != nullptr && l != nullptr)
    {
        CHECK (beat->kind == ClipKind::audio && l->scene == 0 && beat->length == 4 * ppq);
        CHECK (l->mode == LaunchMode::repeat && l->quant == LaunchQuant::bar && l->loop);
        CHECK (riff == nullptr || beat->track != riff->track);
    }
}
} // namespace als181c

TEST (als181c_session_clips_fill_the_grid)
{
    using namespace lynx;
    FlpImportReport report;
    const auto song = importAls (als175a::bytesOf (als181c::sessionSet()), als181c::options (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    als181c::checkGrid (*song);
    CHECK (song->playlist.empty()); // nothing on the Playlist: the arrangement is empty
    CHECK (song->markers().empty());
}

TEST (als181c_session_clips_on_the_playlist_stay_an_option)
{
    using namespace lynx;
    auto o = als181c::options();
    o.sessionOnPlaylist = true;
    const auto song = importAls (als175a::bytesOf (als181c::sessionSet()), o);
    CHECK (song.has_value());
    if (! song)
        return;
    CHECK (! song->clipGrid.has_value());
    CHECK (! song->playlist.empty());
    CHECK (als175a::markerNamed (*song, "Intro") != nullptr && als175a::markerNamed (*song, "Out") != nullptr);
}

TEST (als181c_a_set_without_session_clips_adds_no_grid)
{
    using namespace lynx;
    const auto song = importAls (als175a::bytesOf (als181c::sessionSet (false)), als181c::options());
    CHECK (song.has_value() && ! song->clipGrid.has_value());
}

TEST (als181c_start_marker_inside_the_loop_is_reported)
{
    using namespace lynx;
    auto set = als181c::sessionSet();
    const auto at = set.find ("<StartRelative Value=\"0.000000\" />");
    CHECK (at != std::string::npos);
    set.replace (at, std::string ("<StartRelative Value=\"0.000000\" />").size(), "<StartRelative Value=\"1\" />");
    FlpImportReport report;
    const auto song = importAls (als175a::bytesOf (set), als181c::options (&report));
    CHECK (song.has_value() && als181c::clipNamed (*song, "Riff A") != nullptr);
    CHECK (report.approximated.count ("Session clip whose start marker is inside its loop (it starts at the loop start)") == 1);
}

TEST (als181c_grid_round_trips_through_live_session_view)
{
    using namespace lynx;
    const auto song = importAls (als175a::bytesOf (als181c::sessionSet()), als181c::options());
    CHECK (song.has_value());
    if (! song)
        return;
    for (const auto version : { AlsVersion::live12, AlsVersion::live11 })
    {
        AlsExportOptions o;
        o.version = version;
        const auto out = exportAls (*song, o);
        CHECK (out.xml.find ("<GlobalQuantisation Value=\"9\"") != std::string::npos);
        CHECK (out.xml.find ("<HasStop Value=\"false\"") != std::string::npos);
        // every track: one clip slot per scene [LIVE: §9 rule 6]
        xml::Document doc;
        CHECK (doc.parse (out.xml));
        const auto set = doc.root().child ("LiveSet");
        CHECK (set.child ("Scenes").children ("Scene").size() == 3);
        for (const auto& t : set.child ("Tracks").children())
            if (t.name() == "MidiTrack" || t.name() == "AudioTrack")
                CHECK (t.path ("DeviceChain/MainSequencer/ClipSlotList").children ("ClipSlot").size() == 3);
        const auto back = importAls (out.bytes, als181c::options());
        CHECK (back.has_value());
        if (back)
            als181c::checkGrid (*back);
    }
}

TEST (als181c_a_lynx_grid_exports_to_session_view)
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    ChannelDef keys;
    keys.name = "Keys";
    s.channels = { keys };
    Pattern p;
    p.name = "Riff";
    for (const auto& [key, at] : { std::pair<int, std::int64_t> { 60, 0 }, { 64, 2 * ppq } })
    {
        Note n;
        n.key = key;
        n.start = at;
        n.length = ppq;
        p.notes.push_back (n);
    }
    p.lengthTicks = 4 * ppq;
    s.patterns = { p };
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 0;
    c.length = 4 * ppq;
    CHECK (launcher::setClip (s, 0, 1, c) >= 0);
    auto* slot = launcher::settingsAt (*s.clipGrid, 0, 1);
    CHECK (slot != nullptr);
    if (slot == nullptr)
        return;
    slot->name = "Riff";
    slot->mode = LaunchMode::repeat;
    slot->quant = LaunchQuant::sixteenthTriplet;
    slot->loop = false;
    slot->follow.enabled = true;
    slot->follow.a = FollowKind::other;
    slot->follow.b = FollowKind::jump;
    slot->follow.jumpB = 0;
    slot->follow.chanceA = 40;
    slot->follow.chanceB = 60;
    s.clipGrid->scenes[1].name = "Main";
    s.clipGrid->scenes[1].tempo = 96.0;
    s.clipGrid->globalQuant = LaunchQuant::bars2;
    const auto out = exportAls (s);
    CHECK (out.xml.find ("<GlobalQuantisation Value=\"3\"") != std::string::npos); // 2 Bars: clip number 4 − 1
    const auto back = importAls (out.bytes, als181c::options());
    CHECK (back.has_value());
    if (! back)
        return;
    const ClipGridSlot* r = nullptr;
    const auto* clip = als181c::clipNamed (*back, "Riff", &r);
    CHECK (clip != nullptr && r != nullptr);
    if (clip == nullptr || r == nullptr)
        return;
    CHECK (r->scene == 1 && clip->length == 4 * ppq);
    CHECK (r->mode == LaunchMode::repeat && r->quant == LaunchQuant::sixteenthTriplet && ! r->loop);
    CHECK (r->follow.enabled && r->follow.a == FollowKind::other && r->follow.b == FollowKind::jump && r->follow.jumpB == 0);
    CHECK (r->follow.chanceA == 40 && r->follow.chanceB == 60);
    CHECK (back->clipGrid->globalQuant == LaunchQuant::bars2);
    CHECK (back->clipGrid->scenes.size() == 2 && back->clipGrid->scenes[1].name == "Main" && back->clipGrid->scenes[1].tempo == 96.0);
    CHECK (back->playlist.empty()); // a grid-only song puts nothing in the arrangement
}
