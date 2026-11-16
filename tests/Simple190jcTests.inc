// --- Round 190jc: the Simple layout's BandLab / FL Studio Mobile look (design190ia slices 5, 14, 15, 22, 24).
// The core parts: the rows the Simple Playlist shows (its real tracks, then the drop lane's row: no placeholder rows),
// each track kind's type icon, and the tour card's place beside the control it points at (core/FirstSongTour.h).

#include "core/SimpleTracks.h"

namespace simple190jc
{
using namespace lynx;

inline Song empty()
{
    Song s;
    s.mixer = Song::defaultMixer();
    return s;
}
} // namespace simple190jc

TEST (simple190jc_rows_shown_are_the_real_tracks_and_the_drop_lane)
{
    using namespace lynx;
    auto s = simple190jc::empty();
    CHECK (simpletracks::rowsShown (s, 16) == 1); // an empty song: the drop lane alone

    simpletracks::Request voice;
    voice.kind = simpletracks::Kind::voice;
    CHECK (simpletracks::addTrack (s, voice).ok());
    CHECK (simpletracks::addTrack (s, voice).ok());
    CHECK (simpletracks::rowsShown (s, 16) == 3); // two tracks + the lane

    // a clip far down (a project made elsewhere): every row up to it shows, the lane is the first free one (a gap)
    Clip c;
    c.kind = ClipKind::pattern;
    c.track = 6;
    s.playlist.push_back (c);
    CHECK (simpletracks::nextEmptyTrack (s) == 2);
    CHECK (simpletracks::rowsShown (s, 16) == 7);
    CHECK (simpletracks::rowsShown (s, 4) == 4); // never more than the Playlist's rows
}

TEST (simple190jc_every_kind_has_a_type_icon)
{
    using namespace lynx;
    using K = simpletracks::Kind;
    for (auto k : { K::voice, K::instrument, K::drums, K::sampler, K::guitar, K::bass, K::audio, K::pads })
        CHECK (simpletracks::iconOf (k) != trackicons::Icon::none);
    CHECK (simpletracks::iconOf (K::voice) == trackicons::Icon::microphone);
    CHECK (simpletracks::iconOf (K::drums) == trackicons::Icon::drumMachine);
    // the icon is drawn, not stored: a new track keeps FL's "no icon" (its .flp and the Pattern layout are unchanged)
    auto s = simple190jc::empty();
    simpletracks::Request q;
    q.kind = K::voice;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok() && s.playlistTracks[(std::size_t) r.track].icon == 0);
}
