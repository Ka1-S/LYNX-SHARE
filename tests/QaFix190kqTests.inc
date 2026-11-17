// --- round qafix190kq (QA of 0.1.30, release blockers): take lanes have no clip-grid / Session-mixer column (QA-2).

#include "core/Comping.h"

namespace qafix190kq
{
inline lynx::PlaylistTrack row (const char* name, bool grouped = false, int nest = 0)
{
    lynx::PlaylistTrack t;
    t.name = name;
    t.grouped = grouped;
    t.nest = nest;
    return t;
}
} // namespace qafix190kq

// QA-2: a lane under a comp track, an imported lane nested in a group (its header a plain folder), FL's ordinary groups
TEST (qafix190kq_take_lanes_are_told_from_tracks)
{
    using qafix190kq::row;
    lynx::Song s;
    s.playlistTracks.clear();
    s.playlistTracks.push_back (row ("Vox"));                   // 0: comp track
    s.playlistTracks.push_back (row ("Lane", true));            // 1: its lanes
    s.playlistTracks.push_back (row ("Lane", true));            // 2
    s.playlistTracks.push_back (row ("Drums"));                 // 3: FL group header
    s.playlistTracks.push_back (row ("Kick", true));            // 4: an ordinary group member (a real track)
    s.playlistTracks.push_back (row ("Bus"));                   // 5: Live group track
    s.playlistTracks.push_back (row ("Gtr", true));             // 6: a track inside the group, with a lane
    s.playlistTracks.push_back (row ("Gtr lane", true, 1));     // 7: its imported lane (one level deeper)
    s.playlistTracks.push_back (row ("Loose"));                 // 8: a lane flag on an ungrouped row: no lane any more
    s.playlistTracks[0].compTrack = true;
    s.playlistTracks[1].takeLane = s.playlistTracks[2].takeLane = true;
    s.playlistTracks[7].takeLane = true;
    s.playlistTracks[8].takeLane = true;
    const bool want[] { false, true, true, false, false, false, false, true, false };
    for (int t = 0; t < 9; ++t)
        CHECK (lynx::comp::isTakeLane (s, t) == want[t]);
    CHECK (! lynx::comp::isTakeLane (s, -1) && ! lynx::comp::isTakeLane (s, 9));
    // a comp made in LYNX ("Comp these takes"): lanes without the import's flag are lanes too
    s.playlistTracks[1].takeLane = s.playlistTracks[2].takeLane = false;
    CHECK (lynx::comp::isTakeLane (s, 1) && lynx::comp::isTakeLane (s, 2));
}

// QA-2: an imported Live set's lanes carry the flag; the tracks do not
TEST (qafix190kq_imported_take_lanes_are_flagged)
{
    using namespace als190ka;
    als190ka::Track t;
    t.clips = audioClip (0.0, 4.0, "E:/sets/take2.wav", true, 0.0, 4.0);
    auto text = set ({ t, als190ka::Track {} });
    const std::string lanes = "<TakeLanes><TakeLanes><TakeLane Id=\"0\">" + v ("Name", "Take 1") + "<ClipAutomation><Events>" +
                              audioClip (0.0, 4.0, "E:/sets/take1.wav", true, 0.0, 4.0) + "</Events></ClipAutomation></TakeLane>" +
                              "<TakeLane Id=\"1\">" + v ("Name", "") + "<ClipAutomation><Events>" + audioClip (0.0, 4.0, "E:/sets/take2.wav", true, 0.0, 4.0) +
                              "</Events></ClipAutomation></TakeLane></TakeLanes></TakeLanes>";
    text.insert (text.find ("<AutomationEnvelopes>"), lanes);
    lynx::FlpImportReport report;
    const auto s = load (text, &report, { "E:/sets/take1.wav", "E:/sets/take2.wav" });
    CHECK (s.has_value() && s->playlistTracks.size() == 4);
    if (! s || s->playlistTracks.size() != 4)
        return;
    CHECK (! s->playlistTracks[0].takeLane && s->playlistTracks[1].takeLane && s->playlistTracks[2].takeLane && ! s->playlistTracks[3].takeLane);
    CHECK (! lynx::comp::isTakeLane (*s, 0) && lynx::comp::isTakeLane (*s, 1) && lynx::comp::isTakeLane (*s, 2) && ! lynx::comp::isTakeLane (*s, 3));
}

// QA-9: a silent --export says why (Live's lesson sets without any clip ended in "result=ERROR" and no message)
#include "core/ExportSilence.h"

TEST (qafix190kq_silent_export_says_why)
{
    using lynx::exportsilence::reason;
    const auto starts = [] (const std::string& s, const char* head) { return s.rfind (head, 0) == 0; };
    lynx::Song empty;
    empty.playlist.clear();
    for (auto& p : empty.patterns)
        p.notes.clear();
    CHECK (starts (reason (empty, false), "nothing to render: the project holds no clip and no note"));
    // a Live set with tracks but no clip at all (the lesson sets' shape)
    lynx::FlpImportReport report;
    const auto set = als190ka::load (als190ka::set ({ als190ka::Track {}, als190ka::Track {} }), &report, {});
    CHECK (set.has_value());
    if (set)
        CHECK (starts (reason (*set, false), "nothing to render: the project holds no clip and no note"));
    // notes only in a pattern: the song is empty (the pattern export would play them)
    auto patternOnly = empty;
    if (patternOnly.patterns.empty())
        patternOnly.patterns.emplace_back();
    lynx::Note n;
    n.length = lynx::ppq;
    patternOnly.patterns[0].notes.push_back (n);
    CHECK (starts (reason (patternOnly, false), "nothing to render: the song (Playlist / arrangement) is empty; the project's notes"));
    CHECK (starts (reason (patternOnly, true), "the render is silent"));
    // clips only in the clip grid
    auto grid = empty;
    lynx::Clip c;
    c.length = lynx::ppq;
    lynx::launcher::setClip (grid, 0, 0, c);
    CHECK (starts (reason (grid, false), "nothing to render: the song (Playlist / arrangement) is empty; the project's clips are only in the clip grid"));
    // something in the song that plays nothing
    auto muted = empty;
    muted.playlist.push_back (c);
    CHECK (starts (reason (muted, false), "the render is silent"));
}

// QA-10: DirectWave's zone copies are shared between rack builds (the export's engine beside the project's held every
// zone twice); a new sample, or the other sample format, gets its own; nothing is kept once no one holds it
#include "core/DirectWave.h"

TEST (qafix190kq_directwave_zones_are_shared_between_builds)
{
    const auto make = [] (float l, float r, std::size_t n) {
        auto s = std::make_shared<lynx::SampleData>();
        s->sampleRate = 44100.0;
        s->left.assign (n, l);
        s->right.assign (n, r);
        return std::shared_ptr<const lynx::SampleData> (s);
    };
    const auto sample = make (0.25f, -0.25f, 1000);
    auto a = lynx::dw::zoneSamplesShared ("C:/x/zone.wav", sample, false);
    auto b = lynx::dw::zoneSamplesShared ("C:/x/zone.wav", sample, false);
    CHECK (a != nullptr && a == b);
    // the export engine decodes the file again (the rack keeps the zones, not the decoded sample): the same zones
    const auto decodedAgain = make (0.25f, -0.25f, 1000);
    CHECK (lynx::dw::zoneSamplesShared ("C:/x/zone.wav", decodedAgain, false) == a);
    // the other format, another file, or the same file changed on disk: zones of their own
    auto c = lynx::dw::zoneSamplesShared ("C:/x/zone.wav", sample, true);
    CHECK (c != nullptr && c != a && c->bits == 16 && a->bits == 32);
    CHECK (lynx::dw::zoneSamplesShared ("C:/x/other.wav", sample, false) != a);
    const auto changed = make (0.5f, -0.25f, 1000);
    const auto d = lynx::dw::zoneSamplesShared ("C:/x/zone.wav", changed, false);
    CHECK (d != nullptr && d != a && d->f32[0][256] == 0.5f);
    // the same content as the plain copy
    const auto plain = lynx::dw::zoneSamples (*sample, false);
    CHECK (plain != nullptr && plain->f32 == a->f32 && plain->frames == a->frames && plain->channels == a->channels);
    // nothing is kept once no build holds it
    std::weak_ptr<const lynx::dw::ZoneSamples> gone = c;
    c.reset();
    CHECK (gone.expired());
    CHECK (lynx::dw::zoneSamplesShared ("C:/x/zone.wav", nullptr, false) == nullptr);
}

// QA-6: the export's tail ends on a constant offset as on silence, not on sound or a slow decay
#include "core/TailSilence.h"

TEST (qafix190kq_tail_counts_dc_as_silence)
{
    using lynx::tailsilence::quietBlock;
    constexpr float silence = 3.0e-5f;
    std::vector<float> l (512), r (512);
    const float* ch[] { l.data(), r.data() };
    // Never Out's tail: -0.000225 / -0.000170 held (-73 dBFS of DC)
    std::fill (l.begin(), l.end(), -0.000225f);
    std::fill (r.begin(), r.end(), -0.000170f);
    CHECK (quietBlock (ch, 2, 512, silence));
    // digital silence, and noise under the level
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    CHECK (quietBlock (ch, 2, 512, silence));
    // a quiet tone (a reverb tail at -70 dBFS) is sound
    for (int i = 0; i < 512; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = 3.0e-4f * std::sin (0.05f * (float) i);
    CHECK (! quietBlock (ch, 2, 512, silence));
    // the same tone riding on DC is sound too
    for (auto& v : l)
        v += 0.01f;
    CHECK (! quietBlock (ch, 2, 512, silence));
    // DC on one side, a tone on the other: sound
    std::fill (l.begin(), l.end(), 0.01f);
    CHECK (! quietBlock (ch, 2, 512, silence));
    CHECK (quietBlock (ch, 2, 0, silence));
}

// QA-6 (the DC's source): a step on an audio clip channel plays the whole clip (round 190jd) and then ENDS — it held the
// sample's last value for ever (FL holds that DC only to a Playlist clip's own end): Never Out's hi-hat steps left a
// voice each, -73 dBFS of DC summed after the song
TEST (qafix190kq_clip_channel_step_ends_with_its_sample)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = r190io::rate;
    data->left.assign ((std::size_t) (0.5 * r190io::rate), 0.0f);
    for (std::size_t i = 0; i < data->left.size(); ++i)
        data->left[i] = 0.2f + 0.1f * (float) std::sin (6.283185307179586 * 440.0 * (double) i / r190io::rate); // ends on ~0.2: a DC tail
    data->right = data->left;
    const auto song = r190io::stepSong (lynx::InstrumentKind::audioClip, "dc");
    auto state = lynx::RackState::build (song, nullptr, r190io::rate, [data] (const std::string&) -> std::shared_ptr<const lynx::SampleData> { return data; });
    lynx::RackRenderer rr;
    rr.prepare (r190io::rate, 480);
    const auto total = (int) (1.9 * r190io::rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    double tick = 0.0;
    const auto tps = lynx::ticksPerSample (song.bpm, r190io::rate);
    for (int at = 0; at < total; at += 480)
    {
        const auto n = std::min (480, total - at);
        rr.process (state.get(), true, tick, n, l.data() + at, r.data() + at);
        tick += n * tps;
    }
    double during = 0.0, after = 0.0;
    for (int i = (int) (0.1 * r190io::rate); i < (int) (0.4 * r190io::rate); ++i)
        during = std::max (during, (double) std::abs (l[(std::size_t) i]));
    for (int i = (int) (0.7 * r190io::rate); i < total; ++i)
        after = std::max (after, (double) std::abs (l[(std::size_t) i]));
    std::printf ("qafix190kq: a clip channel step: peak %.4f while the sample plays, %.6f after it\n", during, after);
    CHECK (during > 0.05);  // (the whole clip plays: 190jd)
    CHECK (after < 1.0e-6); // (no DC after it)
}
