// Round 190qm: PR #268 (DERALKERAL) "a track switched off goes dark", checked against FL 2026 (Thief, shots/fl/fl3-*):
// FL greys a switched-off Playlist track's clips (drawn as muted clips: the veil and the X) and its header, and leaves its
// lane as it is. The clips' look is FL's (always, Pattern layout); the PR's row veil is a LYNX extra, on by default (owner
// rule 2026-11-23), off = FL-exact. The pixels are checked by --workflowshot190iu (7b, 7c); these read the list and sources.

#include "core/LynxExtras.h"

TEST (pr268_darken_switched_off_tracks_is_a_lynx_extra)
{
    using namespace lynx;
    int found = 0;
    for (const auto& item : extras::items)
        if (item.id == "playlist.darkenMutedTracks")
        {
            ++found;
            CHECK (item.settingsKey == "ui-options.txt:darkenMutedTracks");
            CHECK (item.title.find ("(LYNX)") != std::string_view::npos);
            CHECK (item.note.find ("FL Studio") != std::string_view::npos); // (says what FL does, and how to get it back)
        }
    CHECK (found == 1);
}

TEST (pr268_switched_off_track_clips_drawn_as_muted_clips)
{
    const auto src = issue269::sourceRoot(); // (Issue269_190qmTests.inc)
    if (src.empty())
        return;
    const auto root = src / "ui";
    std::ifstream cppIn (root / "PlaylistComponent.cpp"), hIn (root / "PlaylistComponent.h");
    if (! cppIn || ! hIn)
        return;
    std::stringstream cppText, hText;
    cppText << cppIn.rdbuf();
    hText << hIn.rdbuf();
    const auto cpp = cppText.str(), h = hText.str();
    // FL's part: a clip on a switched-off track counts as muted for its look (Pattern layout)
    CHECK (cpp.find ("bool shownMuted (const Song& song, const Clip& c) { return c.muted || (flClipLook() && switchedOff (song, c.track)); }")
           != std::string::npos);
    // ... except a take lane, muted only to stay silent (Live's TakeLanes, a comp's lanes): its look is its own
    CHECK (cpp.find ("bool switchedOff (const Song& song, int track) { return song.isTrackMuted (track) && ! comp::isTakeLane (song, track); }")
           != std::string::npos);
    CHECK (cpp.find ("if (! shownMuted (song, clip))") != std::string::npos); // (the veil and the X)
    // the extra: on by default, and the row veil asks it
    CHECK (h.find ("static inline bool darkenMutedTracks = true;") != std::string::npos);
    CHECK (cpp.find ("if (! darkenMutedTracks || ! flClipLook())") != std::string::npos);
}
