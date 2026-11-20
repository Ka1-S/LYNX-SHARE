// --- Round 190jc: the Simple layout's BandLab / FL Studio Mobile look (design190ia slices 5, 14, 15, 22, 24).
// The core parts: the rows the Simple Playlist shows (its real tracks, then the drop lane's row: no placeholder rows),
// each track kind's type icon, and the tour card's place beside the control it points at (core/FirstSongTour.h).

#include "core/FirstSongTour.h"
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

TEST (simple190jc_tour_card_goes_beside_its_ring)
{
    using namespace lynx::tour;
    const Box window { 12, 12, 1896, 1056 };
    // "+ Add track" at the top left (design190ia SG-18: the card sat at (1527, 115), ~1 330 px away): below it, 14 px off
    const Box addTrack { 71, 69, 122, 42 };
    const auto p = placeCard (addTrack, window, 380, 184, 14);
    CHECK (p.side == Side::below);
    CHECK (p.card.y == addTrack.bottom() + 14 && p.card.x == addTrack.x);
    CHECK (! p.card.intersects (addTrack));

    // a ring at the bottom: above it; near the right edge: slid left to stay inside
    const Box low { 1800, 1000, 60, 40 };
    const auto q = placeCard (low, window, 380, 184, 14);
    CHECK (q.side == Side::above && q.card.bottom() == low.y - 14 && q.card.right() <= window.right());

    // the space below is kept clear (the Finish dock): the next side that fits
    const Box dock { 1000, 0, 920, 1080 };
    const auto r = placeCard (Box { 600, 300, 100, 30 }, window, 380, 184, 14, dock);
    CHECK (r.side != Side::none && ! r.card.intersects (dock) && r.card.inside (window));

    // no ring, or no room anywhere: none (the caller keeps its corner)
    CHECK (placeCard (Box {}, window, 380, 184, 14).side == Side::none);
    CHECK (placeCard (Box { 0, 0, 1920, 1080 }, window, 380, 184, 14).side == Side::none);
}

TEST (simple190jc_step_five_rings_the_drop_lane)
{
    using namespace lynx::tour;
    const auto t = targets (loop);
    CHECK (t.size() == 3 && t[1] == "simple.dropLane"); // (the voice tile while the chooser is up, else the lane)
}
