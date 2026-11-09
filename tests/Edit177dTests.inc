// Round 177d, FL pain points (research-web-2.md §3; docs/analise/REA_rodada177d.md §2).
// Slice 177d-4: 25 song-bar ruler, 35 instruments-only stems.
#include "core/EditorPolish.h"

TEST (pain177d_song_bars_ruler_origin)
{
    lynx::Song s;
    s.patterns.resize (2);
    CHECK (! lynx::polish::patternSongOrigin (s, 1).has_value()); // not on the Playlist: pattern bars
    lynx::Clip late, early, muted;
    late.pattern = early.pattern = muted.pattern = 1;
    late.start = 8 * 3840;
    early.start = 4 * 3840;
    early.offset = 3840; // starts one bar into the pattern
    muted.start = 0;
    muted.muted = true;
    s.playlist = { late, early, muted };
    CHECK (lynx::polish::patternSongOrigin (s, 1) == 3 * 3840); // the first unmuted clip, minus its offset
    CHECK (lynx::polish::songBarNumber (3 * 3840, 0, 3840) == 4);
    // an offset past the song's start: the origin is negative but real, and the bars before bar 1 have no number
    s.playlist = { early };
    s.playlist[0].start = 0;
    CHECK (lynx::polish::patternSongOrigin (s, 1) == -3840);
    CHECK (lynx::polish::songBarNumber (-3840, 0, 3840) == 0);
    CHECK (lynx::polish::songBarNumber (-3840, 1, 3840) == 1);
}

TEST (pain177d_instruments_only_channel_stems)
{
    lynx::Song s;
    s.channels.resize (2);
    s.mixer.resize (4);
    s.channels[0].mixerTrack = 1;
    s.channels[1].mixerTrack = 0;
    s.mixer[1].toMaster = false; // insert 1 goes only to the bus on insert 2, and sends to the reverb on 3
    s.mixer[1].sends = { lynx::MixerSend { 2, 1.0f }, lynx::MixerSend { 3, 0.5f } };
    const auto wet = lynx::exportopts::makeChannelStemSong (s, 0);
    CHECK (wet.channels[1].muted && ! wet.channels[0].muted);
    CHECK (wet.mixer[1].sends.size() == 2 && ! wet.mixer[1].toMaster); // the mixer stays as it is
    const auto dry = lynx::exportopts::makeChannelStemSong (s, 0, true);
    CHECK (dry.channels[1].muted);
    CHECK (dry.mixer[1].sends.empty() && dry.mixer[1].toMaster);
    CHECK (s.mixer[1].sends.size() == 2); // the song itself is untouched
    // a channel on the Master: nothing to strip
    const auto master = lynx::exportopts::makeChannelStemSong (s, 1, true);
    CHECK (master.mixer[1].sends.size() == 2);
}
