// Round 190oo (design review 2): the owner's option decisions that have rules in core.

#include "core/LynxExtras.h"
#include "core/PlaylistOps.h"
#include "core/Templates.h"

// CS O-2: a new Clip-launch project links Track N to insert N (Live's new set: a strip on every track), off by default
TEST (design190oo_cliplaunch_strips_option)
{
    using namespace lynx;
    NewProjectDefaults d;
    CHECK (! d.clipLaunchStrips && d.toText().find ("newClipLaunchStrips=0") != std::string::npos);
    CHECK (NewProjectDefaults::ownsKey ("newClipLaunchStrips"));
    NewProjectDefaults back;
    back.readKey ("newClipLaunchStrips", "1");
    CHECK (back.clipLaunchStrips);
    d.clipLaunchStrips = true;
    CHECK (d.toText().find ("newClipLaunchStrips=1") != std::string::npos);
    bool listed = false;
    for (const auto& item : extras::items)
        listed = listed || (item.id == "project.clipLaunchStrips" && item.settingsKey == "ui-options.txt:newClipLaunchStrips");
    CHECK (listed);

    auto song = makeTemplate (0);
    applyNewProjectDefaults (song, NewProjectDefaults {});
    const auto before = song;
    const auto linked = linkClipLaunchStrips (song, 16);
    CHECK (linked == 16);
    CHECK (song.linkTracksToMixer);
    for (int t = 0; t < 16; ++t)
    {
        CHECK (autolink::linkedInsertOf (song, t) == t + 1);
        if (t < (int) song.channels.size()) // (the template's channels play through inserts 1..5: their instrument tracks)
        {
            CHECK (song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::instrument);
            CHECK (song.playlistTracks[(std::size_t) t].channel == t);
            CHECK (song.playlistTracks[(std::size_t) t].name == song.channels[(std::size_t) t].name);
        }
        else
            CHECK (song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::audio);
    }
    // nothing is routed differently: the same channels on the same inserts, the same number of inserts
    CHECK (song.mixer.size() == before.mixer.size());
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        CHECK (song.channels[c].mixerTrack == before.channels[c].mixerTrack);
    // a second call links nothing more; fewer inserts than tracks: only as many as there are inserts
    CHECK (linkClipLaunchStrips (song, 16) == 0);
    auto small = makeTemplate (0);
    NewProjectDefaults four;
    four.inserts = 4;
    applyNewProjectDefaults (small, four);
    CHECK (linkClipLaunchStrips (small, 16) == 4);
    CHECK (autolink::linkedInsertOf (small, 4) == -1);
}

// O-1 and O-2 are listed as the other extras are (O-2 is a default, so it is not an extra)
TEST (design190oo_extras_listed)
{
    using namespace lynx;
    bool shade = false;
    for (const auto& item : extras::items)
        shade = shade || (item.id == "playlist.shadeFadedAudio" && item.settingsKey == "ui-options.txt:shadeFadedAudio");
    CHECK (shade);
}
