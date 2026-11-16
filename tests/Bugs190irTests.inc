// Round 190ir: issue #189 (text size), the Discord recording report (takes on new tracks / new inserts).
// (#188, a minimised window that kept the main window's clicks, is a JUCE/X11 fix: LYNX --minimizeclicktest.)

#include "core/FontMetrics.h"
#include "core/TextSize.h"

#include <fstream>
#include <iterator>

namespace bugs190ir
{
// a font file with only the tables FontMetrics reads (head, hhea, OS/2), Open Sans's values
inline std::vector<unsigned char> tinyFont (int upem, int ascent, int descent, bool useTypo)
{
    std::vector<unsigned char> d (12 + 3 * 16, 0);
    const auto put16 = [&d] (std::size_t at, int v) { d[at] = (unsigned char) ((unsigned) v >> 8); d[at + 1] = (unsigned char) v; };
    const auto put32 = [&d] (std::size_t at, std::uint32_t v) {
        for (int k = 0; k < 4; ++k)
            d[at + (std::size_t) k] = (unsigned char) (v >> (24 - 8 * k));
    };
    put16 (4, 3);
    const char* tags[] { "head", "hhea", "OS/2" };
    const std::size_t lengths[] { 54, 36, 96 };
    for (int i = 0; i < 3; ++i)
    {
        const auto entry = 12 + 16 * (std::size_t) i;
        std::memcpy (d.data() + entry, tags[i], 4);
        put32 (entry + 8, (std::uint32_t) d.size());
        put32 (entry + 12, (std::uint32_t) lengths[i]);
        d.resize (d.size() + lengths[i], 0);
    }
    const auto head = 12 + 48, hhea = head + 54, os2 = hhea + 36;
    put16 (head + 18, upem);
    put16 (hhea + 4, ascent);
    put16 (hhea + 6, -descent);
    put16 (os2 + 62, useTypo ? 0x80 : 0);
    put16 (os2 + 68, ascent);
    put16 (os2 + 70, -descent);
    put16 (os2 + 74, ascent);
    put16 (os2 + 76, descent);
    return d;
}

inline lynx::Song recordingSong (int inserts)
{
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.playlistTracks.clear();
    song.mixer = lynx::Song::defaultMixer (inserts);
    return song;
}

inline int clipsOn (const lynx::Song& song, int track)
{
    return (int) std::count_if (song.playlist.begin(), song.playlist.end(), [track] (const lynx::Clip& c) { return c.track == track; });
}
} // namespace bugs190ir

TEST (bugs190ir_font_metrics_set_the_extent_and_keep_the_ratio)
{
    using namespace lynx::fontmetrics;
    const auto font = bugs190ir::tinyFont (2048, 2189, 600, true); // Open Sans
    const auto before = read (font.data(), font.size());
    CHECK (before.has_value() && before->unitsPerEm == 2048 && before->hheaAscent == 2189 && before->hheaDescent == -600 && before->useTypoMetrics);
    CHECK (before.has_value() && std::abs (before->extent() - 1.3618) < 0.001);
    const auto scaled = withVerticalExtent (font.data(), font.size(), dejaVuSansExtent);
    CHECK (scaled.size() == font.size());
    const auto after = read (scaled.data(), scaled.size());
    CHECK (after.has_value() && std::abs (after->extent() - dejaVuSansExtent) < 0.001);
    if (after.has_value())
    {
        // 2384 units in all, split as 2189 : 600 (1871 + 513); typo and win alike
        CHECK (after->hheaAscent - after->hheaDescent == 2384 && after->hheaAscent == 1871);
        CHECK (after->typoAscent == after->hheaAscent && after->typoDescent == after->hheaDescent);
        CHECK (after->winAscent == 1871 && after->winDescent == 513);
        CHECK (after->unitsPerEm == 2048 && after->useTypoMetrics);
    }
    // the letters grow by the extents' ratio: 1.362 / 1.164 = +17 % at the same JUCE height
    CHECK (before.has_value() && after.has_value() && std::abs (before->extent() / after->extent() - 1.17) < 0.01);
    // not a font: nothing
    const std::vector<unsigned char> junk (40, 7);
    CHECK (! read (junk.data(), junk.size()).has_value() && withVerticalExtent (junk.data(), junk.size(), 1.0).empty());
    CHECK (withVerticalExtent (font.data(), 20, 1.0).empty()); // (cut short)
}

TEST (bugs190ir_bundled_open_sans_is_read)
{
    // the font LYNX brings (resources/fonts): Open Sans 2189 / -600 of 2048 with USE_TYPO_METRICS, as measured
    std::ifstream in ("resources/fonts/OpenSans-Regular.ttf", std::ios::binary);
    const std::vector<unsigned char> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (data.empty())
        return; // (run from elsewhere than the repository root)
    const auto v = lynx::fontmetrics::read (data.data(), data.size());
    CHECK (v.has_value() && v->unitsPerEm == 2048 && v->hheaAscent == 2189 && v->hheaDescent == -600 && v->useTypoMetrics);
    const auto scaled = lynx::fontmetrics::withVerticalExtent (data.data(), data.size(), lynx::fontmetrics::dejaVuSansExtent);
    const auto s = lynx::fontmetrics::read (scaled.data(), scaled.size());
    CHECK (s.has_value() && s->hheaAscent == 1871 && s->hheaDescent == -513 && s->typoAscent == 1871 && s->typoDescent == -513);
    // (its win metrics are taller, 2302 + 651: scaled to the same 2384 units in their own ratio)
    CHECK (v.has_value() && v->winAscent == 2302 && v->winDescent == 651 && s.has_value() && s->winAscent + s->winDescent == 2384 && s->winAscent == 1858);
    // only the six metric fields changed (12 bytes at most)
    int changed = 0;
    for (std::size_t i = 0; i < data.size() && i < scaled.size(); ++i)
        changed += data[i] != scaled[i] ? 1 : 0;
    CHECK (scaled.size() == data.size() && changed > 0 && changed <= 12);
}

TEST (bugs190ir_text_size_per_role)
{
    using namespace lynx::textsize;
    CHECK (forRole (100, Role::body) == 100 && forRole (100, Role::caption) == 100);
    CHECK (forRole (150, Role::body) == 150 && forRole (150, Role::small) == 150 && forRole (150, Role::title) == 150);
    // the roles in boxes of a fixed size stop at 125 %
    CHECK (forRole (150, Role::caption) == 125 && forRole (150, Role::display) == 125 && forRole (125, Role::display) == 125);
    CHECK (forRole (110, Role::display) == 110 && forRole (90, Role::caption) == 90);
    CHECK (forRole (300, Role::body) == 150 && forRole (10, Role::body) == 90); // clamped
    CHECK (std::abs (scaled (13.0f, 125, Role::body) - 16.25f) < 0.001f && std::abs (scaled (20.0f, 150, Role::display) - 25.0f) < 0.001f);
    CHECK (parse ("125") == 125 && parse ("125\n") == 125 && parse (" 110 %") == 110 && parse ("") == 100 && parse ("big") == 100);
    CHECK (parse ("400") == 150 && parse ("50") == 90 && parse ("99999999") == 100);
    for (const auto c : choices)
        CHECK (c >= minimum && c <= maximum);
}

TEST (bugs190ir_takes_land_where_fl_puts_them)
{
    using namespace lynx;
    using namespace lynx::takes;
    auto song = bugs190ir::recordingSong (8);
    const auto inserts = song.mixer.size();
    // a beat on Track 1, bars 1-2 (a pattern clip)
    Clip beat;
    beat.kind = ClipKind::pattern;
    beat.track = 0;
    beat.start = 0;
    beat.length = 2 * 4 * ppq;
    song.playlist.push_back (beat);
    // Insert 1 armed, FL's default "Post EQ": the take plays through the Master, no audio track is linked
    CHECK (playbackInsert (song, 1, RecordSource::postEq) == 0);
    const auto bar = (std::int64_t) 4 * ppq;
    // take 1 over bars 3-4: Track 1 is free there (FL: "the first Playlist Track where it won't overlap")
    placeTakeClips (song, { { "/rec/Insert 1_Take1.wav", "Insert 1_Take1", 0, 1 } }, { { 2 * bar, 2 * bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 0);
    // take 2 over the same bars: the next free track (FL as well)
    placeTakeClips (song, { { "/rec/Insert 1_Take2.wav", "Insert 1_Take2", 0, 1 } }, { { 2 * bar, 2 * bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 1);
    // take 3 later in the song: back on Track 1 (LYNX 0.1.29: a third new track, "after the last one used")
    placeTakeClips (song, { { "/rec/Insert 1_Take3.wav", "Insert 1_Take3", 0, 1 } }, { { 8 * bar, 2 * bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 0);
    // FL makes an audio clip channel per take; no take made an insert
    CHECK (song.channels.size() == 3 && song.mixer.size() == inserts);
    CHECK (std::all_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.mixerTrack == 0; }));
    // a track linked to an instrument or another insert is not taken
    song.playlistTracks.resize (3);
    song.playlistTracks[2].mode = PlaylistTrackMode::audio;
    song.playlistTracks[2].mixerTrack = 5;
    CHECK (takeTrack (song, 1, 2 * bar, 4 * bar) == 3);
}

TEST (bugs190ir_linked_audio_track_takes_go_to_its_sub_tracks)
{
    using namespace lynx;
    using namespace lynx::takes;
    auto song = bugs190ir::recordingSong (8);
    const auto inserts = song.mixer.size();
    const auto bar = (std::int64_t) 4 * ppq;
    song.playlistTracks.resize (4);
    auto& vox = song.playlistTracks[1];
    vox.mode = PlaylistTrackMode::audio;
    vox.mixerTrack = 3;
    vox.name = "Vox";
    // something on Track 3 (index 2) that must move down, not become a take lane
    Clip other;
    other.kind = ClipKind::pattern;
    other.track = 2;
    other.start = 0;
    other.length = bar;
    song.playlist.push_back (other);
    // insert 3 records with its audio track linked: the clip plays through 3, on the Vox track
    CHECK (playbackInsert (song, 3, RecordSource::postEq) == 3);
    placeTakeClips (song, { { "/rec/Vox_Take1.wav", "Vox_Take1", 3, 3 } }, { { 0, 2 * bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 1);
    // the same bars again: a sub-track under Vox (FL: "sub-tracks to this parent"), the track below moved down
    placeTakeClips (song, { { "/rec/Vox_Take2.wav", "Vox_Take2", 3, 3 } }, { { 0, 2 * bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 2);
    CHECK (song.playlistTracks.size() >= 3 && song.playlistTracks[2].grouped && song.playlistTracks[2].name == "Vox"
           && song.playlistTracks[2].mode == PlaylistTrackMode::unassigned);
    CHECK (song.playlist.front().kind == ClipKind::pattern && song.playlist.front().track == 3);
    // and again: the next sub-track
    placeTakeClips (song, { { "/rec/Vox_Take3.wav", "Vox_Take3", 3, 3 } }, { { 0, 2 * bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 3 && song.playlistTracks[3].grouped && song.playlist.front().track == 4);
    // a take that fits on the Vox track itself goes there
    placeTakeClips (song, { { "/rec/Vox_Take4.wav", "Vox_Take4", 3, 3 } }, { { 4 * bar, bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 1);
    // a take that fits on the first sub-track goes there (no new one)
    placeTakeClips (song, { { "/rec/Vox_Take5.wav", "Vox_Take5", 3, 3 } }, { { 4 * bar, bar, 0, 0.0f } });
    CHECK (song.playlist.back().track == 2 && bugs190ir::clipsOn (song, 2) == 2);
    // every take on one insert: no insert added, one clip channel each (FL), all through insert 3
    CHECK (song.mixer.size() == inserts && song.channels.size() == 5);
    CHECK (std::all_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.mixerTrack == 3; }));
    CHECK (song.playlistTracks[1].mode == PlaylistTrackMode::audio && song.playlistTracks[1].mixerTrack == 3);
}
