// Round 190qj (0.1.32): issue #265 and the contributor PRs #261-#266.
//
// Issue #265 ("still having routing problems", 0.1.30): a pattern clip on a Playlist track that is linked to mixer insert 3
// kept playing in Song mode while the track's header M (insert 3's mute) was lit. FL 2026: a channel routed to a muted
// insert is silent in Song mode, and so it is in LYNX (first test). The video's case is the second: the pattern placed on the
// linked audio track plays its channels where they are routed (another insert), as in FL, so muting insert 3 does not reach
// it. The linked header's M now mutes the Playlist track too when the track holds such clips (PlaylistComponent), and its
// hint names the other inserts (autolink::otherInsertsOnTrack).

#include "core/PlaylistOps.h"
#include "core/ThemePalette.h"

namespace i190qj
{
// a Sampler "Kick" on insert `channelInsert` playing a 0.25 DC sample for 2 s (4 beats at 120), its pattern clip on
// Playlist track 3 (index 2), which is linked to insert `link` (Track mode › Audio track); Song mode
inline lynx::Song routed (r186b::Files& files, int channelInsert, int link)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = r186b::rate;
    d->left.assign ((std::size_t) (4.0 * r186b::rate), 0.25f);
    d->right = d->left;
    files.map["dc"] = d;

    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    lynx::ChannelDef c;
    c.name = "Kick";
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "dc";
    c.mixerTrack = channelInsert;
    s.channels = { c };
    lynx::Pattern p;
    lynx::Note n;
    n.channel = 0;
    n.length = 4 * lynx::ppq;
    p.notes = { n };
    s.patterns = { p };
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::pattern;
    clip.pattern = 0;
    clip.track = 2;
    clip.length = 4 * lynx::ppq;
    s.playlist = { clip };
    if (s.playlistTracks.size() < 4)
        s.playlistTracks.resize (4);
    for (auto& t : s.playlistTracks)
        lynx::trackmodes::unassign (s, (int) (&t - s.playlistTracks.data()));
    CHECK (lynx::trackmodes::linkToMixerTrack (s, 2, link));
    return s;
}

inline double level (const lynx::Song& s, r186b::Files& files)
{
    return r186b::render (s, files, 1.5).rmsDb (0.2, 1.2);
}
} // namespace i190qj

TEST (issue265_a_channel_on_a_muted_insert_is_silent_in_song_mode)
{
    r186b::Files files;
    auto s = i190qj::routed (files, 3, 3);
    CHECK (i190qj::level (s, files) > -40.0);
    CHECK (lynx::autolink::otherInsertsOnTrack (s, 2, 3).empty()); // (everything on the track plays through its insert)
    s.mixer[3].muted = true; // FL 2026: silent
    CHECK (i190qj::level (s, files) < -120.0);
    s.mixer[3].muted = false;
    s.mixer[3].volume = 0.0f; // the fader down: silent too
    CHECK (i190qj::level (s, files) < -120.0);
}

TEST (issue265_a_pattern_on_a_linked_track_that_plays_through_another_insert)
{
    r186b::Files files;
    // the video's case: the pattern's channel on insert 4, the track it sits on linked to insert 3
    auto s = i190qj::routed (files, 4, 3);
    CHECK (lynx::autolink::otherInsertsOnTrack (s, 2, 3) == std::vector<int> { 4 });
    s.mixer[3].muted = true; // insert 3's mute alone does not reach it (FL: the channel plays where it is routed)
    CHECK (i190qj::level (s, files) > -40.0);
    s.playlistTracks[2].muted = true; // what the header's M now also does for such a track: silent
    CHECK (i190qj::level (s, files) < -120.0);
    // a channel on the Master counts as another insert too; muted notes' channels too (they are the channel's)
    s.channels[0].mixerTrack = 0;
    CHECK (lynx::autolink::otherInsertsOnTrack (s, 2, 3) == std::vector<int> { 0 });
    // clips on other tracks don't count
    s.playlist[0].track = 1;
    CHECK (lynx::autolink::otherInsertsOnTrack (s, 2, 3).empty());
}

TEST (issue265_the_linked_header_mutes_the_track_too)
{
    // PlaylistComponent wires it (a real click needs the GUI): the source keeps the rule
    using namespace i18ntest;
    const auto src = readFile ("src/ui/PlaylistComponent.cpp");
    const auto at = src.find ("issue #265 (round 190qj): clips on this track that play through other inserts");
    CHECK (at != std::string::npos);
    CHECK (src.find ("s.playlistTracks[(std::size_t) track].muted = mute;", at) != std::string::npos);
}

// Issue #238 follow-up (round 190qj, LYNX extra; the reporter's Material-style idea): the theme presets give tracks without a
// colour of their own a muted shade of their accent instead of FL's grey; LYNX's default look keeps FL's grey; an option
// keeps the grey everywhere; the Tracks dot overrides both.
TEST (issue238_presets_give_tracks_a_muted_accent)
{
    using namespace lynx::themes;
    const auto hsv = [] (std::uint32_t c) {
        const auto r = ((c >> 16) & 0xff) / 255.0, g = ((c >> 8) & 0xff) / 255.0, b = (c & 0xff) / 255.0;
        const auto hi = std::max ({ r, g, b }), lo = std::min ({ r, g, b });
        return std::pair<double, double> { hi > 0.0 ? (hi - lo) / hi : 0.0, hi };
    };
    Spec lynxLook;
    CHECK (defaultTrackColour (lynxLook) == flTrackGrey); // (the FL-like default look stays as it is)
    const auto greyV = hsv (flTrackGrey).second;
    int tinted = 0;
    for (const auto& preset : builtInThemes())
    {
        const auto c = defaultTrackColour (preset);
        if (preset.accent == defaultAccent)
        {
            CHECK (c == flTrackGrey);
            continue;
        }
        ++tinted;
        const auto [s, v] = hsv (c);
        CHECK (c != flTrackGrey && s <= 0.36 && std::abs (v - greyV - 0.03) < 0.01);
        CHECK (contrastRatio (0xffffffffu, c) >= 4.5); // (white track names stay readable)
        // the accent's hue: the strongest channel is the accent's (a greyish accent stays greyish)
        if (hsv (preset.accent).first > 0.3)
            CHECK (contrastRatio (c, flTrackGrey) < 1.3 && mutedAccent (preset.accent) == c);
        auto off = preset;
        off.tracksFromAccent = false; // the option: FL's grey
        CHECK (defaultTrackColour (off) == flTrackGrey);
        auto dot = preset;
        dot.dots[(std::size_t) Dot::tracks] = 0xff102030; // the Tracks dot wins
        CHECK (defaultTrackColour (dot) == 0xff102030u);
    }
    CHECK (tinted == (int) builtInThemes().size() - 1);
    // Amber's muted shade is warm, Ocean's cool
    const auto amber = mutedAccent (0xfff0a020), ocean = mutedAccent (0xff3d8bfd);
    CHECK (((amber >> 16) & 0xff) > (amber & 0xff) && (ocean & 0xff) > ((ocean >> 16) & 0xff));
}
