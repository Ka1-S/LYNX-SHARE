// Round 190am: the exact rule of Mixer menu > "Show only used inserts" (mixerops::hiddenUnusedInserts).

#include "core/MixerOps.h"

TEST (extras190am_show_only_used_inserts_rule)
{
    using namespace lynx;
    auto song = Song::makeDefault(); // channels play into inserts 1..5
    const auto count = (int) song.mixer.size();
    CHECK (count >= 16);
    const auto shown = [&song] (int t, const std::set<int>& keep = {}) { return ! mixerops::hiddenUnusedInserts (song, keep)[(std::size_t) t]; };

    // one flag per insert and one for the "+" button, which never hides; the Master never hides
    CHECK ((int) mixerops::hiddenUnusedInserts (song, {}).size() == count + 1);
    CHECK (shown (0) && shown (count));
    // a channel routed to it: stays. Nothing at all: hidden
    for (int t = 1; t <= 5; ++t)
        CHECK (shown (t));
    for (int t = 6; t < count; ++t)
        CHECK (! shown (t));

    // the selected insert (and whatever else the mixer asks to keep) always stays
    CHECK (shown (7, { 7 }) && ! shown (8, { 7 }) && shown (8, { 7, 8 }));

    // anything that can receive audio stays: a send into it ...
    song.mixer[1].sends.push_back ({ 9, 1.0f });
    CHECK (shown (9) && ! shown (10));
    // ... an audio input ...
    song.mixer[10].inputChannel = 0;
    CHECK (shown (10));
    // ... a Playlist track linked to it ...
    CHECK (! shown (11));
    song.playlistTracks.resize (std::max<std::size_t> (song.playlistTracks.size(), 1));
    song.playlistTracks[0].mixerTrack = 11;
    CHECK (shown (11));
    // ... an effect's sidechain input or extra output ...
    CHECK (! song.mixer[0].effects.empty());
    song.mixer[2].effects.push_back (song.mixer[0].effects[0]);
    song.mixer[2].effects.back().sidechain = 12;
    CHECK (shown (12));
    // ... and a channel routed there later
    CHECK (! shown (13));
    song.channels[0].mixerTrack = 13;
    CHECK (shown (13));

    // an effect on it, or an output that no longer goes to the Master: stays
    CHECK (! shown (14) && ! shown (15));
    song.mixer[14].effects.push_back (song.mixer[0].effects[0]);
    song.mixer[15].toMaster = false;
    CHECK (shown (14) && shown (15));

    // the option only looks: nothing in the song changed
    const auto before = song.mixer.size();
    (void) mixerops::hiddenUnusedInserts (song, { 3 });
    CHECK (song.mixer.size() == before);

    // a song without a mixer: only the "+" flag
    Song empty;
    empty.mixer.clear();
    CHECK (mixerops::hiddenUnusedInserts (empty, { 0, 4 }) == std::vector<bool> { false });
}
