// --- Round 190fm: grooves beyond the clip grid (core/Groove.h): a groove on Playlist pattern clips (played while
// scheduling, Commit, Extract, grid <-> Playlist), the Live set's GroovePool and clip grooves in and out (.als), and the
// "Classic swing" grooves of LYNX's own bank. The sets here are written by the tests (only the elements the importer
// reads), except the read-only check of a set Live ships (skipped where that file is absent). The .lynx round trip of a
// Playlist clip's groove is in the app's --selftest and --grooveshot190fm (the project file needs JUCE).

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/ClipLauncher.h"
#include "core/Groove.h"
#include "core/Scheduler.h"
#include "core/SessionOps.h"
#include "core/XmlLite.h"

namespace groove190fm
{
using namespace lynx;
constexpr std::int64_t bar = 4 * ppq;

struct Heard
{
    double tick;
    int key;
    float velocity;
    int type;
    bool operator== (const Heard&) const = default;
};

// The whole Playlist, block by block through scheduleSong (as the engine plays it)
inline std::vector<Heard> play (const Song& song, int block = 512)
{
    const auto tps = ticksPerSample (song.bpm, 48000.0);
    const auto index = makeScheduleIndex (song);
    std::vector<Heard> out;
    EventBuffer ev;
    const auto total = (std::int64_t) std::llround ((double) index.songLength / tps);
    for (std::int64_t at = 0; at < total; at += block)
    {
        ev.clear();
        scheduleSong (song, (double) at * tps, (int) std::min<std::int64_t> (block, total - at), tps, ev, at == 0, &index, false);
        for (const auto& e : ev)
            out.push_back ({ ((double) at + e.sampleOffset) * tps, e.key, e.velocity, (int) e.type });
    }
    return out;
}

inline std::vector<double> onsOf (const std::vector<Heard>& heard, int key)
{
    std::vector<double> out;
    for (const auto& h : heard)
        if (h.type == (int) NoteEvent::Type::on && h.key == key)
            out.push_back (h.tick);
    return out;
}

// One channel, pattern 0 = four 16ths from tick 0 (keys 60…63, velocity 0.8) in a one-bar pattern; two Playlist clips
// of it: bar 1 (track 0) and bar 3 (track 1)
inline Song song()
{
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.channels.resize (1);
    s.playlistTracks.resize (3);
    Pattern p;
    p.name = "Hats";
    for (int i = 0; i < 4; ++i)
    {
        Note n;
        n.key = 60 + i;
        n.start = i * 240;
        n.length = 100;
        n.velocity = 0.8f;
        p.notes.push_back (n);
    }
    p.lengthTicks = bar;
    s.patterns.push_back (p);
    for (const auto& [track, at] : { std::pair { 0, (std::int64_t) 0 }, std::pair { 1, 2 * bar } })
    {
        Clip c;
        c.pattern = 0;
        c.track = track;
        c.start = at;
        c.length = bar;
        s.playlist.push_back (c);
    }
    return s;
}

// every off-beat 16th `late` ticks late and at `soft` of the loudest
inline Groove offbeat (int late, float soft = 0.5f)
{
    Groove g;
    g.name = "Off-beat";
    g.base = GrooveBase::sixteenth;
    g.length = bar;
    for (int i = 0; i < 16; ++i)
        g.points.push_back ({ i * 240 + (i % 2 == 1 ? late : 0), i % 2 == 1 ? soft : 1.0f });
    return g;
}

inline std::string v (const char* name, const std::string& value) { return std::string ("<") + name + " Value=\"" + value + "\" />"; }

// A <Groove> element as a set's pool holds it [SCHEMA order] with a one-bar 8th template: off-beats `late` beats late
inline std::string grooveElement (int id, const std::string& name, double late, int grid, double timing, double velocity, double quantize = 0, double random = 0)
{
    std::string notes;
    for (int i = 0; i < 8; ++i)
        notes += "<MidiNoteEvent Time=\"" + std::to_string (i * 0.5 + (i % 2 == 1 ? late : 0.0)) + "\" Duration=\"0.0625\" Velocity=\"" +
                 (i % 2 == 1 ? "64" : "127") + "\" OffVelocity=\"64\" NoteId=\"" + std::to_string (i + 1) + "\" />";
    return "<Groove Id=\"" + std::to_string (id) + "\">" + v ("LomId", "0") + v ("Name", name) + "<Clip><Value><MidiClip Id=\"0\" Time=\"0\">" +
           v ("CurrentStart", "0") + v ("CurrentEnd", "4") + "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", "4") + v ("LoopOn", "true") +
           "</Loop><Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + notes + "</Notes>" + v ("MidiKey", "36") +
           "</KeyTrack></KeyTracks></Notes></MidiClip></Value></Clip>" + v ("Grid", std::to_string (grid)) +
           v ("QuantizationAmount", std::to_string (quantize)) + v ("TimingAmount", std::to_string (timing)) +
           v ("RandomAmount", std::to_string (random)) + v ("VelocityAmount", std::to_string (velocity)) + v ("Annotation", "") +
           v ("Selection", "false") + "<SourceContext />" + "</Groove>";
}

inline std::string midiClip (int id, double at, double length, int grooveId, const std::string& name)
{
    return "<MidiClip Id=\"" + std::to_string (id) + "\" Time=\"" + std::to_string (at) + "\">" + v ("CurrentStart", std::to_string (at)) +
           v ("CurrentEnd", std::to_string (at + length)) + v ("Name", name) + "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", std::to_string (length)) +
           v ("StartRelative", "0") + v ("LoopOn", "false") + v ("OutMarker", std::to_string (length)) + "</Loop>" +
           "<GrooveSettings>" + v ("GrooveId", std::to_string (grooveId)) + "</GrooveSettings>" +
           "<Notes><KeyTracks><KeyTrack Id=\"0\"><Notes><MidiNoteEvent Time=\"0.5\" Duration=\"0.25\" Velocity=\"100\" OffVelocity=\"64\" />"
           "</Notes>" + v ("MidiKey", "60") + "</KeyTrack></KeyTracks></Notes></MidiClip>";
}

inline std::string slot (int id, const std::string& clip)
{
    return "<ClipSlot Id=\"" + std::to_string (id) + "\"><ClipSlot><Value>" + clip + "</Value></ClipSlot>" + v ("HasStop", "true") + "</ClipSlot>";
}

// A set with a GroovePool of two grooves (Ids 7 and 2, in that order), DefaultGrooveId `def`, the global amount
// `amount` %, and a MIDI track: arrangement clips with GrooveId 7, −1 and 5 (not in the pool), a Session clip with 2
inline std::string grooveSet (int def = -1, double amount = 80)
{
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12402\" Creator=\"test\">\n<LiveSet><Tracks>";
    s += "<MidiTrack Id=\"10\"><Name>" + v ("EffectiveName", "Drums") + v ("UserName", "Drums") + "</Name>" + v ("TrackGroupId", "-1") +
         "<DeviceChain><Mixer><Volume><Manual Value=\"1\" /><AutomationTarget Id=\"100\" /></Volume><Pan><Manual Value=\"0\" /><AutomationTarget Id=\"101\" /></Pan>"
         "<Speaker><Manual Value=\"true\" /></Speaker><Sends /></Mixer><AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting>" +
         "<MainSequencer><ClipTimeable><ArrangerAutomation><Events>" + midiClip (0, 0, 4, 7, "Grooved") + midiClip (1, 4, 4, -1, "Straight") +
         midiClip (2, 8, 4, 5, "Lost") + "</Events></ArrangerAutomation></ClipTimeable><ClipSlotList>" + slot (0, midiClip (3, 0, 4, 2, "Session")) +
         "</ClipSlotList></MainSequencer><DeviceChain><Devices /></DeviceChain></DeviceChain></MidiTrack>";
    s += "</Tracks><MainTrack><DeviceChain><Mixer><Volume><Manual Value=\"1\" /></Volume><Pan><Manual Value=\"0\" /></Pan>"
         "<Tempo><Manual Value=\"120\" /></Tempo><TimeSignature><Manual Value=\"201\" /></TimeSignature><GlobalGrooveAmount>" +
         v ("LomId", "0") + v ("Manual", std::to_string (amount)) + "</GlobalGrooveAmount></Mixer></DeviceChain></MainTrack>";
    s += "<Scenes><Scene Id=\"0\">" + v ("Name", "") + "</Scene></Scenes>";
    s += "<GroovePool>" + v ("LomId", "0") + "<Grooves>" + grooveElement (7, "Late eighths", 0.04, 1, 100, 30) +
         grooveElement (2, "Pushed eighths", -0.03, 1, 80, -50, 25, 10) + "</Grooves>" + v ("DefaultGrooveId", std::to_string (def)) + "</GroovePool>";
    s += "</LiveSet>\n</Ableton>\n";
    return s;
}

inline AlsImportOptions options (FlpImportReport* report = nullptr)
{
    AlsImportOptions o;
    o.common.projectFolder = "E:/sets";
    o.common.fileExists = [] (const std::string&) { return true; };
    o.common.report = report;
    return o;
}

inline std::vector<std::uint8_t> bytesOf (const std::string& s) { return std::vector<std::uint8_t> (s.begin(), s.end()); }

inline const Clip* playlistClipNamed (const Song& s, const std::string& name)
{
    for (const auto& c : s.playlist)
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) s.patterns.size() && s.patterns[(std::size_t) c.pattern].name == name)
            return &c;
    return nullptr;
}
} // namespace groove190fm

// --- Playlist clips play through their groove --------------------------------------------------------------------

TEST (groove190fm_playlist_clip_plays_through_its_groove_without_changing_its_notes)
{
    using namespace lynx;
    using namespace groove190fm;
    auto s = song();
    auto g = offbeat (60);
    g.velocity = 1.0f;
    const auto id = groove::add (s.grooves, g);
    CHECK (groove::setPlaylistClipGroove (s, { 0 }, id));
    CHECK (! groove::setPlaylistClipGroove (s, { 0 }, 99)); // no such groove
    CHECK (! groove::setPlaylistClipGroove (s, { 7 }, id)); // no such clip
    CHECK (s.playlist[0].groove == id && s.playlist[1].groove == 0);
    CHECK (groove::users (s, id) == 1);
    CHECK (groove::ofClip (s, s.playlist[0]) != nullptr && groove::ofClip (s, s.playlist[1]) == nullptr);
    const auto written = s.patterns[0].notes;
    const auto heard = play (s);
    const auto on61 = onsOf (heard, 61), on63 = onsOf (heard, 63), on60 = onsOf (heard, 60);
    CHECK (on61.size() == 2 && on63.size() == 2 && on60.size() == 2);
    if (on61.size() == 2 && on63.size() == 2 && on60.size() == 2)
    {
        CHECK (std::abs (on60[0] - 0.0) < 1.0 && std::abs (on61[0] - 300.0) < 1.0 && std::abs (on63[0] - 780.0) < 1.0); // grooved
        CHECK (std::abs (on61[1] - (2.0 * bar + 240.0)) < 1.0 && std::abs (on63[1] - (2.0 * bar + 720.0)) < 1.0);  // the plain clip
    }
    for (const auto& h : heard)
        if (h.type == (int) NoteEvent::Type::on)
        {
            const auto grooved = h.tick < (double) bar;
            const auto odd = h.key % 2 == 1;
            CHECK (std::abs (h.velocity - (grooved && odd ? 0.4f : 0.8f)) < 1.0e-6f);
        }
    CHECK (s.patterns[0].notes == written);

    // the pool's amount scales it, as on the grid
    s.grooves.amount = 0.5f;
    const auto half = onsOf (play (s), 61);
    CHECK (! half.empty() && std::abs (half[0] - 270.0) < 1.0);
    s.grooves.amount = 1.0f;

    // removed from the pool: the clip plays straight again
    CHECK (groove::remove (s, id));
    CHECK (s.playlist[0].groove == 0);
    const auto straight = onsOf (play (s), 61);
    CHECK (! straight.empty() && std::abs (straight[0] - 240.0) < 1.0);
}

TEST (groove190fm_clips_without_a_groove_play_bit_identically)
{
    using namespace lynx;
    using namespace groove190fm;
    const auto s = song();
    const auto plain = play (s);
    auto pooled = s;
    groove::add (pooled.grooves, offbeat (90));
    groove::add (pooled.grooves, offbeat (-40));
    CHECK (play (pooled) == plain); // a pool no Playlist clip uses changes nothing
    CHECK (! plain.empty());
    // the model's defaults: a clip saved before this round has no groove; Clip keeps its size (the field sits in the
    // padding after keyShift, at the end)
    CHECK (Clip {}.groove == 0);
    CHECK (offsetof (Clip, groove) > offsetof (Clip, keyShift) && offsetof (Clip, groove) + sizeof (std::uint32_t) == sizeof (Clip));
    // a groove id on an audio clip does nothing (grooves play on pattern clips)
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.groove = 1;
    CHECK (groove::ofClip (pooled, audio) == nullptr);
    CHECK (! groove::setPlaylistClipGroove (pooled, {}, 1));
}

TEST (groove190fm_commit_and_extract_on_playlist_clips)
{
    using namespace lynx;
    using namespace groove190fm;
    auto s = song();
    auto g = offbeat (60);
    g.velocity = 1.0f;
    const auto id = groove::add (s.grooves, g);
    CHECK (! groove::commitPlaylistClip (s, 0)); // no groove yet
    groove::setPlaylistClipGroove (s, { 0 }, id);
    const auto before = play (s);
    const auto patterns = s.patterns.size();
    CHECK (groove::commitPlaylistClip (s, 0));
    // the pattern is shared with the second clip: the first got its own copy, the second plays as before
    CHECK (s.patterns.size() == patterns + 1 && s.playlist[0].pattern == (int) patterns && s.playlist[1].pattern == 0);
    CHECK (s.playlist[0].groove == 0);
    for (const auto& n : s.patterns[patterns].notes)
    {
        const auto odd = n.key % 2 == 1;
        CHECK (n.start == (n.key - 60) * 240 + (odd ? 60 : 0) && std::abs (n.velocity - (odd ? 0.4f : 0.8f)) < 1.0e-6f);
    }
    // it sounds as it did with the groove on
    const auto after = play (s);
    CHECK (after.size() == before.size());
    for (std::size_t i = 0; i < after.size() && i < before.size(); ++i)
        CHECK (std::abs (after[i].tick - before[i].tick) < 0.5 && after[i].key == before[i].key && after[i].velocity == before[i].velocity);
    // a pattern only this clip plays is written in place
    groove::setPlaylistClipGroove (s, { 0 }, id);
    CHECK (groove::commitPlaylistClip (s, 0) && s.patterns.size() == patterns + 1);
    CHECK (! groove::commitPlaylistClip (s, 9));

    // Extract from what the clip plays: from its offset on, over its length
    auto t = song();
    t.patterns[0].notes[1].start = 300; // the second 16th 60 late
    t.playlist[1].offset = 240;         // the second clip starts at the second 16th
    t.playlist[1].length = bar - 240;
    const auto whole = groove::extractFromClip (t, t.playlist[0], GrooveBase::sixteenth, "Mine");
    CHECK (whole.name == "Mine" && whole.points.size() == 4 && whole.length == bar);
    CHECK (groove::apply (whole, 1.0f, 240, 0.8f, 0).start == 300);
    const auto cut = groove::extractFromClip (t, t.playlist[1], GrooveBase::sixteenth, "Cut");
    CHECK (cut.points.size() == 3 && ! cut.points.empty() && cut.points[0].tick == 60); // (the note at 300 is 60 into the clip)
    Clip audio;
    audio.kind = ClipKind::audio;
    CHECK (groove::extractFromClip (t, audio, GrooveBase::sixteenth, "x").points.empty());
}

TEST (groove190fm_groove_goes_with_a_clip_between_grid_and_playlist)
{
    using namespace lynx;
    using namespace groove190fm;
    auto s = song();
    const auto id = groove::add (s.grooves, offbeat (60));
    // Playlist → grid: the clip's groove becomes its slot's; the grid clip itself carries none
    s.playlist[0].groove = id;
    CHECK (session::fromPlaylist (s, 0, 0, 0, false));
    const auto* slot = launcher::settingsAt (*s.clipGrid, 0, 0);
    const auto* clip = launcher::clipAt (*s.clipGrid, 0, 0);
    CHECK (slot != nullptr && slot->groove == id && clip != nullptr && clip->groove == 0);
    CHECK (clip != nullptr && groove::ofClip (s, *clip) != nullptr);
    // grid → Playlist: the slot's groove comes along
    const auto count = s.playlist.size();
    CHECK (session::toPlaylist (s, 0, 0, 2, 4 * bar));
    CHECK (s.playlist.size() == count + 1 && s.playlist.back().groove == id && s.playlist.back().gridSlot == -1);
    CHECK (groove::users (s, id) == 3); // the grid clip and two Playlist clips
    // Commit on the grid leaves the Playlist's clips alone
    CHECK (groove::commit (s, 0, 0));
    CHECK (launcher::settingsAt (*s.clipGrid, 0, 0)->groove == 0 && s.playlist[0].groove == id && s.playlist.back().groove == id);
}

// --- .als: the GroovePool and the clips' grooves ----------------------------------------------------------------

TEST (groove190fm_als_import_reads_the_pool_and_clip_grooves)
{
    using namespace lynx;
    using namespace groove190fm;
    FlpImportReport report;
    const auto s = importAls (bytesOf (grooveSet (7, 80)), options (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    CHECK (s->grooves.grooves.size() == 2);
    CHECK (std::abs (s->grooves.amount - 0.8f) < 1.0e-6f); // GlobalGrooveAmount 80 %
    if (s->grooves.grooves.size() == 2)
    {
        const auto& late = s->grooves.grooves[0];
        const auto& pushed = s->grooves.grooves[1];
        CHECK (late.name == "Late eighths" && late.base == GrooveBase::eighth && late.length == bar);
        CHECK (std::abs (late.timing - 1.0f) < 1.0e-6f && std::abs (late.velocity - 0.3f) < 1.0e-6f && late.quantize == 0.0f && late.random == 0.0f);
        CHECK (late.points.size() == 8 && late.points.size() > 1 && late.points[1].tick == 480 + 38); // 0.04 beats = 38.4 ticks
        CHECK (std::abs (pushed.velocity + 0.5f) < 1.0e-6f && std::abs (pushed.quantize - 0.25f) < 1.0e-6f && std::abs (pushed.random - 0.1f) < 1.0e-6f
               && std::abs (pushed.timing - 0.8f) < 1.0e-6f);
        CHECK (pushed.points.size() > 1 && pushed.points[1].tick == 480 - 29); // −0.03 beats = −28.8 ticks
        // the arrangement clips: GrooveId 7 → the first groove; −1 and an id not in the pool → none
        const auto* grooved = playlistClipNamed (*s, "Grooved");
        const auto* straight = playlistClipNamed (*s, "Straight");
        const auto* lost = playlistClipNamed (*s, "Lost");
        CHECK (grooved != nullptr && straight != nullptr && lost != nullptr);
        if (grooved != nullptr && straight != nullptr && lost != nullptr)
        {
            CHECK (grooved->groove == late.id && straight->groove == 0 && lost->groove == 0);
            CHECK (groove::ofClip (*s, *grooved) == &late);
        }
        // the Session clip: GrooveId 2 → its slot's groove
        CHECK (s->clipGrid.has_value());
        if (s->clipGrid)
        {
            auto grid = *s->clipGrid;
            const auto* slot = launcher::settingsAt (grid, 0, 0);
            const auto* clip = launcher::clipAt (grid, 0, 0);
            CHECK (slot != nullptr && clip != nullptr && slot->groove == pushed.id && clip->groove == 0);
        }
    }
    CHECK (report.unsupported.count ("Clip groove that is not in the set's Groove Pool (left out)") == 1);
    CHECK (report.unsupported.count ("Default groove for new clips (LYNX's new clips start without a groove)") == 1);
    CHECK (report.approximated.count ("Grooves (LYNX's groove timing and accents; Live's exact law needs a live check)") == 1);

    // no default groove, the global amount above Live's 130 %: kept at 130, reported
    FlpImportReport second;
    const auto t = importAls (bytesOf (grooveSet (-1, 131.25)), options (&second));
    CHECK (t.has_value() && std::abs (t->grooves.amount - 1.3f) < 1.0e-6f);
    CHECK (second.unsupported.count ("Default groove for new clips (LYNX's new clips start without a groove)") == 0);
    CHECK (second.approximated.count ("Global groove amount above 130 % (kept at 130 %)") == 1);
}

TEST (groove190fm_als_export_writes_the_pool_and_round_trips)
{
    using namespace lynx;
    using namespace groove190fm;
    auto s = song();
    auto g = offbeat (60, 0.5f);
    g.name = "Mine & yours";
    g.base = GrooveBase::sixteenth;
    g.quantize = 0.25f;
    g.timing = 0.8f;
    g.random = 0.1f;
    g.velocity = -0.5f;
    groove::add (s.grooves, offbeat (30));     // index 0 in Live
    const auto id = groove::add (s.grooves, g); // index 1
    s.grooves.amount = 1.2f;
    s.playlist[0].groove = id;
    CHECK (launcher::setClip (s, 0, 0, s.playlist[1]) >= 0); // a grid clip of the same pattern…
    groove::setClipGroove (s, 0, 0, s.grooves.grooves[0].id); // …through the first groove
    for (const auto version : { AlsVersion::live12, AlsVersion::live11 })
    {
        AlsExportOptions o;
        o.version = version;
        const auto out = exportAls (s, o);
        xml::Document doc;
        CHECK (doc.parse (out.xml));
        const auto set = doc.root().child ("LiveSet");
        const auto pool = set.child ("GroovePool");
        const auto grooves = pool.path ("Grooves").children ("Groove");
        CHECK (grooves.size() == 2);
        if (grooves.size() == 2)
        {
            // the members in the schema's order [SCHEMA 2742-2754]
            std::vector<std::string> names;
            for (const auto& m : grooves[1].children())
                names.push_back (std::string (m.name()));
            const std::vector<std::string> schema { "LomId", "Name", "Clip", "Grid", "QuantizationAmount", "TimingAmount", "RandomAmount",
                                                    "VelocityAmount", "Annotation", "Selection", "SourceContext" };
            CHECK (names == schema);
            CHECK (grooves[0].attr ("Id") == "0" && grooves[1].attr ("Id") == "1");
            CHECK (grooves[1].value ("Name") == "Mine & yours" && grooves[1].value ("Grid") == "3");
            CHECK (grooves[1].value ("QuantizationAmount") == "25" && grooves[1].value ("TimingAmount") == "80" && grooves[1].value ("RandomAmount") == "10"
                   && grooves[1].value ("VelocityAmount") == "-50");
            CHECK (grooves[1].path ("Clip/Value/MidiClip/Loop/LoopOn").attr ("Value") == "true");
        }
        CHECK (pool.child ("DefaultGrooveId") ? pool.value ("DefaultGrooveId") == "-1" : version == AlsVersion::live11);
        const auto amount = set.path (version == AlsVersion::live11 ? "MasterTrack/DeviceChain/Mixer/GlobalGrooveAmount" : "MainTrack/DeviceChain/Mixer/GlobalGrooveAmount");
        CHECK (amount.value ("Manual") == "120");
        // the clips: the Playlist clip with a groove → GrooveId 1, the plain one −1, the Session clip 0
        std::vector<std::string> arranged;
        std::string session;
        for (const auto& t : set.child ("Tracks").children ("MidiTrack"))
        {
            for (const auto& c : t.path ("DeviceChain/MainSequencer/ClipTimeable/ArrangerAutomation/Events").children ("MidiClip"))
                arranged.push_back (c.value ("GrooveSettings/GrooveId"));
            for (const auto& sl : t.path ("DeviceChain/MainSequencer/ClipSlotList").children ("ClipSlot"))
                for (const auto& c : sl.path ("ClipSlot/Value").children ("MidiClip"))
                    session = c.value ("GrooveSettings/GrooveId");
        }
        CHECK ((arranged == std::vector<std::string> { "1", "-1" }));
        CHECK (session == "0");

        // and back: the same pool, the same clips' grooves
        const auto back = importAls (out.bytes, options());
        CHECK (back.has_value());
        if (! back)
            continue;
        CHECK (back->grooves.grooves.size() == 2 && std::abs (back->grooves.amount - 1.2f) < 1.0e-6f);
        if (back->grooves.grooves.size() == 2)
        {
            const auto& b = back->grooves.grooves[1];
            CHECK (b.name == g.name && b.base == g.base && b.length == g.length && b.quantize == g.quantize && b.timing == g.timing
                   && b.random == g.random && b.velocity == g.velocity);
            CHECK (b.points.size() == g.points.size());
            for (std::size_t i = 0; i < b.points.size() && i < g.points.size(); ++i)
                CHECK (b.points[i].tick == g.points[i].tick && std::abs (b.points[i].velocity - g.points[i].velocity) < 0.5f / 127.0f);
            CHECK (! back->playlist.empty() && back->playlist[0].groove == b.id);
            CHECK (back->playlist.size() > 1 && back->playlist[1].groove == 0);
            auto grid = back->clipGrid ? *back->clipGrid : ClipGrid {};
            const auto* slot = back->clipGrid ? launcher::settingsAt (grid, 0, 0) : nullptr;
            CHECK (slot != nullptr && slot->groove == back->grooves.grooves[0].id);
        }
    }
    // a project without grooves: an empty pool, the global amount 100 %
    const auto plain = exportAls (song());
    CHECK (plain.xml.find ("<Grooves />") != std::string::npos);
    CHECK (plain.xml.find ("<GrooveId Value=\"0\"") == std::string::npos);
}

TEST (groove190fm_shipped_live_set_groove_is_read)
{
    using namespace lynx;
    using namespace groove190fm;
    // Live's own demo set (read-only, on this machine only): its pool holds one groove that four clips use
    const std::filesystem::path file = "/mnt/e/lynx-work/ableton/xml/APC40_Demo.xml";
    if (! std::filesystem::exists (file))
    {
        std::printf ("  (skipped: %s is not here)\n", file.string().c_str());
        return;
    }
    std::ifstream in (file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    xml::Document doc;
    CHECK (doc.parse (text.str()));
    FlpImportReport report;
    const auto s = importAlsXml (doc, options (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    CHECK (s->grooves.grooves.size() == 1);
    if (s->grooves.grooves.size() != 1)
        return;
    const auto& g = s->grooves.grooves[0];
    CHECK (g.name == "SP1200 8 Swing-54");
    CHECK (g.base == GrooveBase::eighth && g.length == bar && g.timing == 1.0f && std::abs (g.velocity - 0.3f) < 1.0e-6f && g.quantize == 0.0f);
    CHECK (g.points.size() == 8); // one per 8th of its loop (0…4 beats)
    if (g.points.size() == 8)
        CHECK (std::abs ((double) g.points[1].tick - 0.5417 * ppq) < 2.0); // the second 8th at 54 % of the pair
    CHECK (std::abs (s->grooves.amount - 1.0f) < 1.0e-6f);
    CHECK (groove::users (*s, g.id) == 4); // the four clips with GrooveId 0
    std::printf ("  APC40 demo: groove \"%s\", %d clips\n", g.name.c_str(), groove::users (*s, g.id));
}

// --- LYNX's own Classic swing --------------------------------------------------------------------------------------

TEST (groove190fm_classic_swing_grooves_follow_the_published_steps)
{
    using namespace lynx;
    const auto& bank = groove::factoryBank();
    const auto& steps = groove::classicSwingSteps();
    CHECK ((steps == std::vector<int> { 50, 54, 58, 62, 63, 66, 67, 71, 75 }));
    std::vector<const Groove*> classic;
    for (const auto& e : bank)
        if (std::string (e.folder) == "Classic swing")
            classic.push_back (&e.groove);
    CHECK (classic.size() == 3 * steps.size());
    for (const auto* g : classic)
    {
        CHECK (g->name.size() > 2 && g->name.substr (g->name.size() - 2) == " %");
        CHECK (g->quantize == 1.0f && g->timing == 1.0f && g->velocity == 0.0f && g->random == 0.0f);
        // LYNX's own names: no machine or maker in them
        for (const auto* mark : { "MPC", "SP", "Akai", "E-mu", "Emu", "Linn", "Roland", "TR-", "Logic", "Notator" })
            CHECK (g->name.find (mark) == std::string::npos);
    }
    const auto named = [&] (const std::string& n) -> const Groove* {
        for (const auto* g : classic)
            if (g->name == n)
                return g;
        return nullptr;
    };
    // the second 16th of every pair `percent` % through the pair (480 ticks): 58 % → 278, 71 % → 341; notes are put on the
    // grid first, so a late or early hit lands on the swung place too
    const auto* s58 = named ("Swing 16ths 58 %");
    const auto* s71 = named ("Swing 16ths 71 %");
    const auto* s50 = named ("Swing 8ths 50 %");
    const auto* s54 = named ("Swing 8ths 54 %");
    const auto* s75 = named ("Swing 32nds 75 %");
    CHECK (s58 != nullptr && s71 != nullptr && s50 != nullptr && s54 != nullptr && s75 != nullptr);
    if (s58 != nullptr && s71 != nullptr && s50 != nullptr && s54 != nullptr && s75 != nullptr)
    {
        CHECK (groove::apply (*s58, 1.0f, 240, 0.8f, 0).start == 278);
        CHECK (groove::apply (*s58, 1.0f, 250, 0.8f, 0).start == 278); // (quantised first)
        CHECK (groove::apply (*s58, 1.0f, 480, 0.8f, 0).start == 480);
        CHECK (groove::apply (*s71, 1.0f, 720, 0.8f, 0).start == 480 + 341);
        CHECK (groove::apply (*s50, 1.0f, 500, 0.8f, 0).start == 480); // 50 %: the plain grid
        CHECK (groove::apply (*s54, 1.0f, 480, 0.8f, 0).start == 518);  // 960 × 0.54
        CHECK (groove::apply (*s75, 1.0f, 120, 0.8f, 0).start == 180);  // 240 × 0.75
        CHECK (s58->base == GrooveBase::sixteenth && s54->base == GrooveBase::eighth && s75->base == GrooveBase::thirtySecond);
    }
}
