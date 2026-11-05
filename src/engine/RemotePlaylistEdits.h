#pragma once

// The local control adapter's Playlist transaction. No UI/audio access: build and
// validate a copy, then let the message-thread adapter commit it as one undo step.
#include "core/SessionIds.h"
#include "core/Song.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lynx::remote_playlist
{
constexpr std::int64_t maxTick = 1000000000;
constexpr int maxTrack = 499;
constexpr std::size_t maxClips = 100000;

struct Patch
{
    std::optional<int> track;
    std::optional<std::int64_t> start, length, offset, fadeIn, fadeOut;
    std::optional<float> offsetFraction, gain, pan, pitch;
    std::optional<double> stretch;
    std::optional<bool> muted, reversed;

    bool empty() const noexcept
    {
        return ! track && ! start && ! length && ! offset && ! fadeIn && ! fadeOut && ! offsetFraction
            && ! gain && ! pan && ! pitch && ! stretch && ! muted && ! reversed;
    }
    bool hasAudioFields() const noexcept
    {
        return fadeIn || fadeOut || offsetFraction || gain || pan || pitch || stretch || reversed;
    }
};

struct Edit
{
    enum class Action { add, update, duplicate, remove } action = Action::update;
    std::uint64_t clipId = 0;
    ClipKind kind = ClipKind::pattern;
    int source = -1;
    Patch patch;
};

struct Transaction
{
    Song song;
    std::vector<std::uint64_t> created;
    std::size_t updated = 0, removed = 0;
};

[[noreturn]] inline void fail (const char* why) { throw std::runtime_error (why); }

inline void writableTrack (const Song& song, int track)
{
    if (track < 0 || track > maxTrack)
        fail ("Playlist track must be between 0 and 499");
    if (track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].lockedToContent)
        fail ("Remote does not edit Playlist tracks locked to content; unlock the track first");
}

inline void validateClip (const Song& song, const Clip& clip)
{
    writableTrack (song, clip.track);
    if (clip.start < 0 || clip.start > maxTick || clip.length < 1 || clip.length > maxTick
        || clip.start > maxTick - clip.length || clip.offset < 0 || clip.offset > maxTick
        || clip.offset > maxTick - clip.length)
        fail ("Clip timing exceeds the supported nonnegative tick range");
    switch (clip.kind)
    {
        case ClipKind::pattern:
            if (clip.pattern < 0 || clip.pattern >= (int) song.patterns.size()) fail ("Pattern source is unavailable");
            break;
        case ClipKind::audio:
            if (clip.channel < 0 || clip.channel >= (int) song.channels.size()
                || song.channels[(std::size_t) clip.channel].instrument != InstrumentKind::audioClip)
                fail ("Audio source must be an existing audio-clip channel");
            break;
        case ClipKind::automation:
            if (clip.automation < 0 || clip.automation >= (int) song.automations.size()) fail ("Automation source is unavailable");
            break;
        default: fail ("Unsupported Playlist clip kind");
    }
    if (clip.kind == ClipKind::audio)
    {
        if (clip.fadeIn < 0 || clip.fadeOut < 0 || clip.fadeIn > clip.length || clip.fadeOut > clip.length
            || ! std::isfinite (clip.offsetFraction) || clip.offsetFraction < -0.5f || clip.offsetFraction > 0.5f
            || ! std::isfinite (clip.gain) || clip.gain < 0 || clip.gain > 64
            || ! std::isfinite (clip.pan) || clip.pan < -1 || clip.pan > 1
            || ! std::isfinite (clip.pitch) || clip.pitch < -12 || clip.pitch > 12
            || ! std::isfinite (clip.stretch) || clip.stretch < 0.01 || clip.stretch > 100)
            fail ("Audio clip settings exceed their supported ranges");
    }
}

inline void applyPatch (Clip& clip, const Patch& patch)
{
    if (clip.kind != ClipKind::audio && patch.hasAudioFields())
        fail ("Audio-only properties require an audio clip");
    if (patch.track) clip.track = *patch.track;
    if (patch.start) clip.start = *patch.start;
    if (patch.length) clip.length = *patch.length;
    if (patch.offset) clip.offset = *patch.offset;
    if (patch.fadeIn) clip.fadeIn = *patch.fadeIn;
    if (patch.fadeOut) clip.fadeOut = *patch.fadeOut;
    if (patch.offsetFraction) clip.offsetFraction = *patch.offsetFraction;
    if (patch.gain) clip.gain = *patch.gain;
    if (patch.pan) clip.pan = *patch.pan;
    if (patch.pitch) clip.pitch = *patch.pitch;
    if (patch.stretch) clip.stretch = *patch.stretch;
    if (patch.muted) clip.muted = *patch.muted;
    if (patch.reversed) clip.reversed = *patch.reversed;
}

inline Transaction apply (const Song& original, const std::vector<Edit>& edits)
{
    if (edits.empty() || edits.size() > 128)
        fail ("Playlist transaction needs between 1 and 128 edits");
    Transaction result { original, {}, 0, 0 };
    auto& song = result.song;
    for (const auto& edit : edits)
    {
        auto found = song.playlist.end();
        if (edit.action != Edit::Action::add)
        {
            if (edit.clipId == 0) fail ("An existing nonzero clipId is required");
            found = std::find_if (song.playlist.begin(), song.playlist.end(), [&] (const Clip& clip) { return clip.uid == edit.clipId; });
            if (found == song.playlist.end()) fail ("Playlist clipId is unavailable in the current arrangement");
            writableTrack (song, found->track);
        }
        if (edit.action == Edit::Action::remove)
        {
            if (! edit.patch.empty()) fail ("Remove cannot change clip properties");
            song.playlist.erase (found);
            ++result.removed;
            continue;
        }
        if (edit.action == Edit::Action::update)
        {
            if (edit.patch.empty()) fail ("Update requires at least one changed clip property");
            auto clip = *found;
            applyPatch (clip, edit.patch);
            validateClip (song, clip);
            *found = clip;
            ++result.updated;
            continue;
        }
        if (song.playlist.size() >= maxClips) fail ("Playlist clip limit reached");
        Clip clip;
        if (edit.action == Edit::Action::duplicate)
            clip = *found; // preserve all source/offset/audio fields, including fields not exposed for editing
        else
        {
            if (! edit.patch.track || ! edit.patch.start || ! edit.patch.length)
                fail ("Add requires track, start and length");
            clip.kind = edit.kind;
            switch (edit.kind)
            {
                case ClipKind::pattern: clip.pattern = edit.source; break;
                case ClipKind::audio: clip.channel = edit.source; break;
                case ClipKind::automation: clip.automation = edit.source; break;
                default: fail ("Unsupported Playlist clip kind");
            }
        }
        applyPatch (clip, edit.patch);
        validateClip (song, clip);
        clip.uid = uids::next (song);
        if (clip.uid == 0) fail ("Cannot allocate a Playlist clip identity");
        song.playlist.push_back (clip);
        result.created.push_back (clip.uid);
    }
    return result;
}
} // namespace lynx::remote_playlist
