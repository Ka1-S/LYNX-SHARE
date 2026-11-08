// --- Round 183a: the Detail View of the Clip-launch workflow. The chain a Playlist track shows (core/DetailChain.h).

#include "core/ClipLauncher.h"
#include "core/DetailChain.h"

namespace detail183a
{
using namespace lynx;

// Channels 0..3 on inserts 1, 1, 2, 3; pattern p holds one note on channel p; pattern 4 holds notes on channels 0 and 1;
// pattern 5 on channels 0 and 2.
inline Song chainSong()
{
    Song s;
    s.channels.resize (4);
    s.mixer.resize (5);
    const int inserts[] { 1, 1, 2, 3 };
    for (int c = 0; c < 4; ++c)
        s.channels[(std::size_t) c].mixerTrack = inserts[c];
    for (int p = 0; p < 6; ++p)
    {
        auto& pattern = s.patterns.emplace_back();
        const std::vector<int> on = p < 4 ? std::vector<int> { p } : p == 4 ? std::vector<int> { 0, 1 } : std::vector<int> { 0, 2 };
        for (const auto ch : on)
        {
            Note n;
            n.channel = ch;
            pattern.notes.push_back (n);
        }
    }
    s.playlistTracks.resize (6);
    return s;
}

inline Clip clipOn (int track, int pattern)
{
    Clip c;
    c.track = track;
    c.pattern = pattern;
    c.length = 4 * ppq;
    return c;
}
} // namespace detail183a

TEST (detail183a_chain_of_a_track_from_its_clips)
{
    using namespace lynx;
    auto s = detail183a::chainSong();
    s.playlist.push_back (detail183a::clipOn (0, 3)); // one channel: the instrument and its insert
    s.playlist.push_back (detail183a::clipOn (1, 4)); // two channels on one insert: the insert alone
    s.playlist.push_back (detail183a::clipOn (2, 5)); // two inserts: nothing chosen, the lists offered

    const auto one = detailchain::forTrack (s, 0);
    CHECK (one.channel == 3 && one.insert == 3 && ! one.fromTrackMode);
    const auto shared = detailchain::forTrack (s, 1);
    CHECK (shared.channel == -1 && shared.insert == 1);
    CHECK ((shared.channels == std::vector<int> { 0, 1 }));
    const auto split = detailchain::forTrack (s, 2);
    CHECK (split.empty());
    CHECK ((split.channels == std::vector<int> { 0, 2 }) && (split.inserts == std::vector<int> { 1, 2 }));
    CHECK (detailchain::forTrack (s, 3).empty()); // an empty track
    CHECK (detailchain::forTrack (s, -1).empty());
    CHECK (detailchain::forTrack (s, 99).empty()); // past the last track: nothing, no crash

    // grid clips count as the track's clips too; a length-0 stop mark does not
    launcher::setClip (s, 3, 0, detail183a::clipOn (3, 2));
    const auto grid = detailchain::forTrack (s, 3);
    CHECK (grid.channel == 2 && grid.insert == 2);
    Clip stopMark = detail183a::clipOn (4, 0);
    stopMark.length = 0;
    s.playlist.push_back (stopMark);
    CHECK (detailchain::forTrack (s, 4).empty());

    // an audio clip: its channel
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.track = 5;
    audio.channel = 1;
    audio.length = ppq;
    s.playlist.push_back (audio);
    const auto a = detailchain::forTrack (s, 5);
    CHECK (a.channel == 1 && a.insert == 1);
}

TEST (detail183a_track_mode_decides_before_the_clips)
{
    using namespace lynx;
    auto s = detail183a::chainSong();
    s.playlist.push_back (detail183a::clipOn (0, 4));
    // FL's instrument track: its channel, then the channel's insert
    s.playlistTracks[0].mode = PlaylistTrackMode::instrument;
    s.playlistTracks[0].channel = 2;
    s.playlistTracks[0].mixerTrack = 4;
    auto c = detailchain::forTrack (s, 0);
    CHECK (c.channel == 2 && c.insert == 2 && c.fromTrackMode);
    CHECK ((c.channels == std::vector<int> { 0, 1 })); // the clips are still listed for the picker
    // a channel routed past the mixer's end falls back to the track's linked insert
    s.channels[2].mixerTrack = 50;
    c = detailchain::forTrack (s, 0);
    CHECK (c.channel == 2 && c.insert == 4);
    // FL's audio track: its linked insert, no instrument
    s.playlistTracks[1].mode = PlaylistTrackMode::audio;
    s.playlistTracks[1].mixerTrack = 3;
    c = detailchain::forTrack (s, 1);
    CHECK (c.channel == -1 && c.insert == 3 && c.fromTrackMode);
    // an audio track linked to nothing: the clips decide
    s.playlistTracks[1].mixerTrack = -1;
    s.playlist.push_back (detail183a::clipOn (1, 0));
    c = detailchain::forTrack (s, 1);
    CHECK (c.channel == 0 && c.insert == 1 && ! c.fromTrackMode);
}

TEST (detail183a_master_and_an_empty_mixer)
{
    using namespace lynx;
    auto s = detail183a::chainSong();
    s.channels[0].mixerTrack = 0; // straight to Master: the Master's chain
    s.playlist.push_back (detail183a::clipOn (0, 0));
    auto c = detailchain::forTrack (s, 0);
    CHECK (c.channel == 0 && c.insert == 0);
    s.mixer.clear(); // no mixer at all: the instrument alone
    c = detailchain::forTrack (s, 0);
    CHECK (c.channel == 0 && c.insert == -1 && c.inserts.empty());
}

namespace detail183a
{
// insert 1 with effects of the given kinds at the given rows (-1 = right after the previous)
inline Song insertWith (const std::vector<int>& rows)
{
    Song s;
    s.mixer.resize (2);
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        EffectSlot fx;
        fx.kind = EffectKind::eq;
        fx.params[0] = (float) i / 10.0f; // (tells them apart)
        fx.row = rows[i];
        s.mixer[1].effects.push_back (fx);
    }
    return s;
}
inline std::vector<float> order (const Song& s)
{
    std::vector<float> out;
    for (const auto& fx : s.mixer[1].effects)
        out.push_back (fx.params[0]);
    return out;
}
inline EffectSlot marker()
{
    EffectSlot fx;
    fx.kind = EffectKind::eq;
    fx.params[0] = 0.99f;
    return fx;
}
} // namespace detail183a

TEST (detail183a_drop_between_cards_keeps_the_slot_places)
{
    using namespace lynx;
    // a free row between the neighbours: taken, nobody moves
    auto s = detail183a::insertWith ({ 0, 4, 7 });
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 1) == 1);
    CHECK ((detail183a::order (s) == std::vector<float> { 0.0f, 0.99f, 0.1f, 0.2f }));
    CHECK ((mixerops::effectRows (s.mixer[1].effects) == std::vector<int> { 0, 1, 4, 7 }));

    // packed neighbours: the ones after move down a row
    s = detail183a::insertWith ({ 0, 1, 2 });
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 1) == 1);
    CHECK ((detail183a::order (s) == std::vector<float> { 0.0f, 0.99f, 0.1f, 0.2f }));
    CHECK ((mixerops::effectRows (s.mixer[1].effects) == std::vector<int> { 0, 1, 2, 3 }));

    // first place with the first row free / taken
    s = detail183a::insertWith ({ 3, 5 });
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 0) == 0);
    CHECK ((mixerops::effectRows (s.mixer[1].effects) == std::vector<int> { 2, 3, 5 }));
    s = detail183a::insertWith ({ 0, 1 });
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 0) == 0);
    CHECK ((detail183a::order (s) == std::vector<float> { 0.99f, 0.0f, 0.1f }));

    // after the last, which sits in slot 10: the ones before move up
    s = detail183a::insertWith ({ 0, 8, 9 });
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 3) == 3);
    CHECK ((detail183a::order (s) == std::vector<float> { 0.0f, 0.1f, 0.2f, 0.99f }));
    CHECK ((mixerops::effectRows (s.mixer[1].effects) == std::vector<int> { 0, 7, 8, 9 }));

    // the middle of a chain packed at both ends: packed from the top
    s = detail183a::insertWith ({ 0, 1, 2, 3, 6, 7, 8, 9 });
    s.mixer[1].effects[4].row = 6; // (rows 4 and 5 are free, but not where the drop is)
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 2) == 2);
    CHECK ((detail183a::order (s) == std::vector<float> { 0.0f, 0.1f, 0.99f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f }));
    CHECK (s.mixer[1].effects.size() == 9);

    // nine effects: the tenth goes in anywhere; then the insert is full
    s = detail183a::insertWith ({ 0, 1, 2, 3, 4, 5, 6, 7, 8 });
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 4) == 4);
    CHECK (s.mixer[1].effects.size() == 10);
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 0) == -1);
    CHECK (detailchain::insertEffectAt (s, 5, detail183a::marker(), 0) == -1); // (no such insert)

    // an empty insert
    s = detail183a::insertWith ({});
    CHECK (detailchain::insertEffectAt (s, 1, detail183a::marker(), 7) == 0);
}

TEST (detail183a_drag_a_card_before_another)
{
    using namespace lynx;
    auto s = detail183a::insertWith ({ 0, 2, 5 });
    // automation of the first effect follows it
    s.automations.emplace_back();
    s.automations.back().target = AutomationTarget { AutomationTargetKind::effectParam, 1, 0, 0 };
    CHECK (detailchain::moveEffectBefore (s, 1, 0, 3) == 2); // the first after the last
    CHECK ((detail183a::order (s) == std::vector<float> { 0.1f, 0.2f, 0.0f }));
    CHECK (s.automations.back().target.slot == 2);
    CHECK ((mixerops::effectRows (s.mixer[1].effects) == std::vector<int> { 0, 2, 5 })); // (the rows stay in use)
    CHECK (detailchain::moveEffectBefore (s, 1, 2, 0) == 0); // back to the front
    CHECK ((detail183a::order (s) == std::vector<float> { 0.0f, 0.1f, 0.2f }));
    CHECK (detailchain::moveEffectBefore (s, 1, 1, 1) == -1); // onto itself: nothing
    CHECK (detailchain::moveEffectBefore (s, 1, 1, 2) == -1); // before the next: the same place
    CHECK (detailchain::moveEffectBefore (s, 1, 7, 0) == -1);
}

// The detail view's keys (the reference DAW's View menu, Live 12.4 [SHOT] d05): free before, no conflict, every focus.
TEST (detail183a_keys_are_free_and_reach_the_detail_view)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    for (auto focus : { Context::global, Context::channelRack, Context::pianoRoll, Context::playlist, Context::mixer, Context::browser })
    {
        CHECK (map.find ({ 'L', ctrl | alt }, focus, false) == "view.detailToggle");
        CHECK (map.find ({ '3', ctrl | alt }, focus, false) == "view.detailClip");
        CHECK (map.find ({ '4', ctrl | alt }, focus, false) == "view.detailDevices");
        CHECK (map.find ({ tab, shift }, focus, false) == "view.detailSwitch");
    }
    CHECK (map.find ({ tab, shift }, Context::pianoRoll, true) == "view.detailSwitch"); // (the typing piano doesn't take it)
    CHECK (map.find ({ tab }, Context::global, false).empty()); // plain Tab keeps no binding
}
