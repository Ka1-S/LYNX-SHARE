#pragma once

// The Channel Rack's filter (FL's "All" dropdown) with FL's two built-in views, without JUCE (tests/Sel168tTests.inc).
//
// FL evidence [DECOMP FLEngine_x64.dll, docs/analise/REA_rodada168t.md §3]: a channel whose type becomes "audio clip"
// (type 4) or "automation clip" (type 5) is moved into the filter group "Audio clips" / "Automation clips" (created
// when missing; FUN_00fff0b0), every other new channel goes to the group shown, or to "Unsorted" when the view is one
// of those two (FUN_01003d80); the .flp loader translates the two names (FUN_00f46490, event 0xE7). So in FL, picking
// "Unsorted" in the rack's dropdown hides the audio clip channels — what the YouTube review missed ("the channel rack
// stuff ... it's hard to tell what is what", 168d #10). FL's own projects carry these groups (all 80 demo projects
// have "Audio clips"), LYNX's own projects do not.
//
// LYNX offers the same two views for every project without touching the song: "Audio clips" = the audio clip
// channels (InstrumentKind::audioClip, or in a group named "Audio clips"), "Unsorted" = the other channels that are in
// no group (or in FL's "Unsorted" group). Groups named like these two are not listed again.

#include <string>

#include "core/Song.h"

namespace lynx::rackfilter
{
constexpr int all = -1;
constexpr int audioClips = -2;
constexpr int unsorted = -3;

inline bool isBuiltinName (const std::string& name)
{
    return name == "Audio clips" || name == "Unsorted";
}

inline const std::string* groupName (const Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return nullptr;
    const auto g = song.channels[(std::size_t) channel].group;
    return g >= 0 && g < (int) song.channelGroups.size() ? &song.channelGroups[(std::size_t) g] : nullptr;
}

inline bool isAudioClipChannel (const Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    if (song.channels[(std::size_t) channel].instrument == InstrumentKind::audioClip)
        return true;
    const auto* name = groupName (song, channel);
    return name != nullptr && *name == "Audio clips";
}

// Whether `channel` is listed under `filter` (all, audioClips, unsorted, or a group index).
inline bool shows (const Song& song, int channel, int filter)
{
    if (filter == all || channel < 0 || channel >= (int) song.channels.size())
        return true;
    if (filter == audioClips)
        return isAudioClipChannel (song, channel);
    if (filter == unsorted)
    {
        const auto* name = groupName (song, channel);
        return ! isAudioClipChannel (song, channel) && (name == nullptr || *name == "Unsorted");
    }
    return song.channels[(std::size_t) channel].group == filter;
}

// A valid filter for this song (a group index that no longer exists falls back to all).
inline int clamp (const Song& song, int filter)
{
    if (filter == audioClips || filter == unsorted)
        return filter;
    return filter >= 0 && filter < (int) song.channelGroups.size() ? filter : all;
}

// The group a new channel joins while `filter` is shown (FL: the group shown; none for the built-in views).
inline int groupForNewChannel (int filter)
{
    return filter >= 0 ? filter : -1;
}
} // namespace lynx::rackfilter
