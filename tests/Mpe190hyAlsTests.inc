// --- Round 190hy (issue #108): MPE stage 2 in .als — Live's per-note expression (PerNoteEventStore) in and out -------
// Format as Live 12 Suite's "Patience" demo set writes it [DATA]: PerNoteEventList{@NoteId, @CC −2 / 74 / −1}/Events/
// PerNoteEvent{@TimeOffset (beats from the note's start), @Value [, @CurveControl1X/1Y/2X/2Y]}; core/NoteCurves.h.

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/NoteCurves.h"

namespace mpe190hy
{
using lynx::NoteCurveLane;

// one bar, a clip of pattern 0 on the Playlist (the default song's first channel)
inline lynx::Song songWith (const std::vector<lynx::Note>& notes)
{
    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::song;
    auto& p = s.patterns[0];
    p.notes = notes;
    for (auto& n : p.notes)
        n.channel = 0;
    p.lengthTicks = 4 * lynx::ppq;
    p.sortNotes();
    s.playlist.clear();
    lynx::Clip c;
    c.pattern = 0;
    c.length = 4 * lynx::ppq;
    s.playlist.push_back (c);
    return s;
}

inline lynx::Note note (std::int64_t start, std::int64_t length, int key)
{
    lynx::Note n;
    n.start = start;
    n.length = length;
    n.key = key;
    n.velocity = 100.0f / 127.0f;
    return n;
}

inline std::vector<lynx::Note> notesOf (const lynx::Song& s)
{
    std::vector<lynx::Note> got;
    for (const auto& p : s.patterns)
        got.insert (got.end(), p.notes.begin(), p.notes.end());
    std::sort (got.begin(), got.end(), [] (const lynx::Note& a, const lynx::Note& b) { return a.key < b.key; });
    return got;
}

inline std::optional<lynx::Song> importText (const std::string& xml)
{
    std::string error;
    return lynx::importAls (std::vector<std::uint8_t> (xml.begin(), xml.end()), {}, &error);
}

// the exported set's PerNoteEventStore element as text
inline std::string storeOf (const std::string& xml)
{
    const auto from = xml.find ("<PerNoteEventStore>"), to = xml.find ("</PerNoteEventStore>");
    return from == std::string::npos || to == std::string::npos ? std::string() : xml.substr (from, to - from);
}

inline bool within (float a, float b, float tolerance) { return std::abs (a - b) <= tolerance; }

inline bool sameLanes (const lynx::NoteCurves& a, const lynx::NoteCurves& b, float tickTolerance, float pitchTolerance, float unitTolerance)
{
    for (int i = 0; i < lynx::noteCurveLaneCount; ++i)
    {
        const auto& p = a.lanes[(std::size_t) i];
        const auto& q = b.lanes[(std::size_t) i];
        if (p.size() != q.size())
            return false;
        for (std::size_t k = 0; k < p.size(); ++k)
            if (! within (p[k].tick, q[k].tick, tickTolerance) || ! within (p[k].value, q[k].value, i == 0 ? pitchTolerance : unitTolerance))
                return false;
    }
    return true;
}
} // namespace mpe190hy

TEST (mpe190hy_als_import_reads_live_per_note_lists)
{
    using namespace mpe190hy;
    // a one-note clip exported by LYNX (NoteId 1), with Live's lists put into its empty store: pitch with a Bézier
    // segment (and controls on the last event, which shape nothing), slide, pressure, and a list of a note that is not there
    const auto out = lynx::exportAls (songWith ({ note (0, 4 * lynx::ppq, 60) }), {});
    auto xml = out.xml;
    const auto at = xml.find ("<EventLists />", xml.find ("<PerNoteEventStore>"));
    CHECK (at != std::string::npos);
    if (at == std::string::npos)
        return;
    xml.replace (at, std::string ("<EventLists />").size(),
                 "<EventLists>"
                 "<PerNoteEventList Id=\"108\" NoteId=\"1\" CC=\"-1\"><Events><PerNoteEvent TimeOffset=\"0.5\" Value=\"0\" />"
                 "<PerNoteEvent TimeOffset=\"1.5\" Value=\"127\" /></Events></PerNoteEventList>"
                 "<PerNoteEventList Id=\"107\" NoteId=\"1\" CC=\"74\"><Events><PerNoteEvent TimeOffset=\"0\" Value=\"64\" />"
                 "<PerNoteEvent TimeOffset=\"2\" Value=\"127\" /></Events></PerNoteEventList>"
                 "<PerNoteEventList Id=\"106\" NoteId=\"1\" CC=\"-2\"><Events>"
                 "<PerNoteEvent TimeOffset=\"0\" Value=\"0\" CurveControl1X=\"0.25\" CurveControl1Y=\"0\" CurveControl2X=\"0.75\" CurveControl2Y=\"1\" />"
                 "<PerNoteEvent TimeOffset=\"1\" Value=\"-1706.5625\" />"
                 "<PerNoteEvent TimeOffset=\"2\" Value=\"1194.59375\" CurveControl1X=\"0.5\" CurveControl1Y=\"1\" CurveControl2X=\"0.5\" CurveControl2Y=\"1\" />"
                 "</Events></PerNoteEventList>"
                 "<PerNoteEventList Id=\"105\" NoteId=\"99\" CC=\"74\"><Events><PerNoteEvent TimeOffset=\"0\" Value=\"0\" /></Events></PerNoteEventList>"
                 "</EventLists>");
    const auto back = importText (xml);
    CHECK (back.has_value());
    if (! back)
        return;
    const auto got = notesOf (*back);
    CHECK (got.size() == 1);
    if (got.size() != 1)
        return;
    const auto& n = got[0];
    CHECK (n.curves.has());
    CHECK (n.finePitch == 0.0f);
    const auto& c = *n.curves;
    const auto q = (double) lynx::ppq;
    // pitch: −10 st at beat 1, +7 st at beat 2 (Live's 8191.5 / 48 per semitone), held after the last event
    CHECK (within (c.valueAt (NoteCurveLane::pitch, 0.0, 99.0f), 0.0f, 0.01f));
    CHECK (within (c.valueAt (NoteCurveLane::pitch, q, 99.0f), -1000.0f, 0.01f));
    CHECK (within (c.valueAt (NoteCurveLane::pitch, 2.0 * q, 99.0f), 700.0f, 0.01f));
    CHECK (within (c.valueAt (NoteCurveLane::pitch, 1.5 * q, 99.0f), -150.0f, 0.01f)); // (a linear segment)
    CHECK (within (c.valueAt (NoteCurveLane::pitch, 3.5 * q, 99.0f), 700.0f, 0.01f));
    CHECK (c.lane (NoteCurveLane::pitch).back().tick == (float) (2.0 * q));
    // the Bézier segment: at s = 1/4, x = 0.2265625 and y = 0.15625 → −156.25 cents at tick 217.5 (a line: −226.6)
    const auto bent = c.valueAt (NoteCurveLane::pitch, 0.2265625 * q, 99.0f);
    std::printf ("  bezier pitch at 0.2266 beat: %.2f cents\n", (double) bent);
    CHECK (within (bent, -156.25f, 1.5f));
    CHECK (c.lane (NoteCurveLane::pitch).size() > 3);
    // slide: CC 74 at 64 = 0.5, 127 = 1; halfway (95.5) = 0.75 by the Mod X law
    CHECK (within (c.valueAt (NoteCurveLane::timbre, 0.0, 0.0f), 0.5f, 1e-5f));
    CHECK (within (c.valueAt (NoteCurveLane::timbre, q, 0.0f), 0.75f, 1e-4f));
    CHECK (within (c.valueAt (NoteCurveLane::timbre, 2.0 * q, 0.0f), 1.0f, 1e-5f));
    // pressure: 0 at beat 0.5 (held before it), 1 at beat 1.5
    CHECK (c.lane (NoteCurveLane::pressure).size() == 2);
    CHECK (within (c.lane (NoteCurveLane::pressure).front().tick, 480.0f, 1e-3f));
    CHECK (within (c.valueAt (NoteCurveLane::pressure, 0.0, 9.0f), 0.0f, 1e-6f));
    CHECK (within (c.valueAt (NoteCurveLane::pressure, q, 9.0f), 0.5f, 1e-4f));
    CHECK (within (c.valueAt (NoteCurveLane::pressure, 1.5 * q, 9.0f), 1.0f, 1e-6f));
}

TEST (mpe190hy_als_export_round_trips_note_curves)
{
    using namespace mpe190hy;
    auto a = note (0, 2 * lynx::ppq, 60);
    lynx::NoteCurves ca;
    ca.lane (NoteCurveLane::pitch) = { { 0.0f, 0.0f }, { 240.0f, -350.0f }, { 960.0f, 1200.0f }, { 1500.0f, 1100.0f } };
    ca.lane (NoteCurveLane::timbre) = { { 0.0f, 0.2f }, { 480.0f, 0.9f }, { 1200.0f, 0.5f } };
    ca.lane (NoteCurveLane::pressure) = { { 120.0f, 0.1f }, { 600.0f, 1.0f }, { 1800.0f, 0.3f } };
    a.curves = ca;
    // a note with fine pitch and a pitch bend: Live has no fine pitch per note, so the bend carries it
    auto b = note (2 * lynx::ppq, lynx::ppq, 64);
    b.finePitch = 25.0f;
    lynx::NoteCurves cb;
    cb.lane (NoteCurveLane::pitch) = { { 0.0f, 0.0f }, { 480.0f, 100.0f } };
    b.curves = cb;
    for (const auto version : { lynx::AlsVersion::live12, lynx::AlsVersion::live11 })
    {
        lynx::AlsExportOptions o;
        o.version = version;
        const auto out = lynx::exportAls (songWith ({ a, b }), o);
        const auto store = storeOf (out.xml);
        CHECK (store.find ("CC=\"-2\"") != std::string::npos);
        CHECK (store.find ("CC=\"74\"") != std::string::npos);
        CHECK (store.find ("CC=\"-1\"") != std::string::npos);
        CHECK (store.find ("CurveControl") == std::string::npos); // (linear points only)
        CHECK (store.find ("TimeOffset=\"0.25\" Value=\"-597.296875\"") != std::string::npos); // −350 cents at a 16th
        std::string error;
        const auto back = lynx::importAls (out.bytes, {}, &error);
        CHECK (back.has_value());
        if (! back)
            continue;
        const auto got = notesOf (*back);
        CHECK (got.size() == 2);
        if (got.size() != 2)
            continue;
        CHECK (got[0].curves.has() && got[1].curves.has());
        CHECK (sameLanes (*got[0].curves, ca, 1e-3f, 0.01f, 1e-5f));
        lynx::NoteCurves cbLive;
        cbLive.lane (NoteCurveLane::pitch) = { { 0.0f, 25.0f }, { 480.0f, 125.0f } };
        CHECK (got[1].finePitch == 0.0f);
        CHECK (sameLanes (*got[1].curves, cbLive, 1e-3f, 0.01f, 1e-5f));
    }
}

TEST (mpe190hy_als_note_without_curves_keeps_the_empty_store)
{
    using namespace mpe190hy;
    auto plain = note (0, lynx::ppq, 60);
    plain.finePitch = 30.0f; // (fine pitch alone stays unexported, as before this round)
    for (const auto version : { lynx::AlsVersion::live12, lynx::AlsVersion::live11 })
    {
        lynx::AlsExportOptions o;
        o.version = version;
        const auto out = lynx::exportAls (songWith ({ plain, note (lynx::ppq, lynx::ppq, 62) }), o);
        CHECK (out.xml.find ("PerNoteEventList") == std::string::npos);
        const auto store = storeOf (out.xml);
        CHECK (! store.empty() && store.find ("<EventLists />") != std::string::npos);
        const auto back = lynx::importAls (out.bytes, {});
        CHECK (back.has_value());
        if (back)
            for (const auto& n : notesOf (*back))
                CHECK (! n.curves.has());
    }
}

TEST (mpe190hy_als_note_ids_map_lists_to_their_notes)
{
    using namespace mpe190hy;
    // two notes on different keys: NoteIds go key by key (60 → 1, 67 → 2), each list names its own note
    auto lo = note (0, lynx::ppq, 60), hi = note (0, lynx::ppq, 67);
    lynx::NoteCurves cl, ch;
    cl.lane (NoteCurveLane::timbre) = { { 0.0f, 0.1f }, { 480.0f, 0.3f } };
    ch.lane (NoteCurveLane::pressure) = { { 0.0f, 0.8f }, { 720.0f, 0.2f } };
    lo.curves = cl;
    hi.curves = ch;
    const auto out = lynx::exportAls (songWith ({ hi, lo }), {});
    const auto store = storeOf (out.xml);
    CHECK (store.find ("NoteId=\"1\" CC=\"74\"") != std::string::npos);
    CHECK (store.find ("NoteId=\"2\" CC=\"-1\"") != std::string::npos);
    CHECK (store.find ("NoteId=\"1\" CC=\"-1\"") == std::string::npos);
    CHECK (store.find ("NoteId=\"2\" CC=\"74\"") == std::string::npos);
    const auto back = lynx::importAls (out.bytes, {});
    CHECK (back.has_value());
    if (! back)
        return;
    const auto got = notesOf (*back);
    CHECK (got.size() == 2);
    if (got.size() != 2)
        return;
    CHECK (got[0].key == 60 && sameLanes (*got[0].curves, cl, 1e-3f, 0.01f, 1e-5f));
    CHECK (got[1].key == 67 && sameLanes (*got[1].curves, ch, 1e-3f, 0.01f, 1e-5f));
    // the lists swapped by hand: the curves follow the NoteIds, not the order
    auto xml = out.xml;
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>> { { "NoteId=\"1\" CC", "NoteId=\"X\" CC" },
                                                                                       { "NoteId=\"2\" CC", "NoteId=\"1\" CC" },
                                                                                       { "NoteId=\"X\" CC", "NoteId=\"2\" CC" } })
        if (const auto at = xml.find (from); at != std::string::npos)
            xml.replace (at, from.size(), to);
    const auto swapped = importText (xml);
    CHECK (swapped.has_value());
    if (! swapped)
        return;
    const auto sw = notesOf (*swapped);
    CHECK (sw.size() == 2);
    if (sw.size() == 2)
    {
        CHECK (sameLanes (*sw[0].curves, ch, 1e-3f, 0.01f, 1e-5f));
        CHECK (sameLanes (*sw[1].curves, cl, 1e-3f, 0.01f, 1e-5f));
    }
}

TEST (mpe190hy_als_plugin_mpe_mode_round_trips)
{
    using namespace mpe190hy;
    for (const auto mpe : { true, false })
    {
        auto s = songWith ({ note (0, lynx::ppq, 60) });
        auto& c = s.channels[0];
        c.instrument = lynx::InstrumentKind::plugin;
        c.plugin.name = "MpeSynth";
        c.plugin.foreign.format = lynx::ForeignPlugin::Format::vst3;
        c.plugin.foreign.name = "MpeSynth";
        for (std::size_t k = 0; k < c.plugin.foreign.classId.size(); ++k)
            c.plugin.foreign.classId[k] = (std::uint8_t) (k + 1);
        c.plugin.foreign.hasClassId = true;
        c.plugin.mpe = mpe;
        const auto out = lynx::exportAls (s, {});
        CHECK (out.xml.find (mpe ? "<MpeEnabled Value=\"true\" />" : "<MpeEnabled Value=\"false\" />") != std::string::npos);
        const auto back = lynx::importAls (out.bytes, {});
        CHECK (back.has_value());
        if (! back)
            continue;
        const auto it = std::find_if (back->channels.begin(), back->channels.end(),
                                      [] (const lynx::ChannelDef& ch) { return ch.instrument == lynx::InstrumentKind::plugin && ch.plugin.name == "MpeSynth"; });
        CHECK (it != back->channels.end());
        if (it != back->channels.end())
            CHECK (it->plugin.mpe == mpe);
    }
}
