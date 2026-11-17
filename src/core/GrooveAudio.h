#pragma once

// Round 190kc (0.1.30, Live's groove pool on audio clips; the Clip-launch layout). Live's manual, chapter "Using
// Grooves": "The timing and volume information from any audio or MIDI clip can be extracted to create a new groove ...
// via the Extract Groove command in the clip's context menu. Grooves created by extracting will only consider the
// material in the playing portion of the clip" (14.2.1); "Pressing the Commit button ... For audio clips, this creates
// Warp Markers at the appropriate positions in the clip. After pressing Commit, the clip's Groove chooser selection is
// automatically set to None" (14.1.2); "In audio clips, grooves work by adjusting the clip's warping behavior, and thus
// only work on clips with Warp enabled" (14). LYNX finds an audio clip's attacks as the audio editor's Quantize audio
// does (clipwarp::attacks); each attack is a "note" of the clip at its place, its level the attack's peak.
//
// Header only (the message thread: Extract, Commit). Not on the audio thread.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/ClipLauncher.h"
#include "core/ClipWarp.h"
#include "core/Groove.h"
#include "core/WarpEdit.h"

namespace lynx::grooveaudio
{
struct Hit
{
    double seconds = 0.0;     // in the file
    std::int64_t tick = 0;    // from the clip's start (its playing portion)
    float level = 0.0f;       // the attack's peak, the loudest attack of the clip = 1
};

// The beats of the file at `seconds`: the channel's warp markers when it is warped, else the file at `bpm`.
inline double contentBeat (const SamplerSettings& s, double seconds, double bpm)
{
    if (s.keepPitch && s.warp.size() >= 2)
        return warpedit::beatAt (s.warp, seconds) - warpedit::beatAt (s.warp, 0.0);
    return seconds * std::max (1.0, bpm) / 60.0;
}

// The attacks in the clip's playing portion, in order.
inline std::vector<Hit> hitsOf (const Song& song, const Clip& clip, const SampleData& sample)
{
    std::vector<Hit> out;
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size() || clip.length <= 0 || ! (sample.sampleRate > 0.0))
        return out;
    const auto& s = song.channels[(std::size_t) clip.channel].sampler;
    const auto stretch = std::clamp (clip.stretch, 0.01, 100.0);
    float loudest = 0.0f;
    for (const auto t : clipwarp::attacks (sample))
    {
        const auto tick = (std::int64_t) std::llround (contentBeat (s, t, song.bpm) * ppq * stretch - ((double) clip.offset + (double) clip.offsetFraction));
        if (tick < 0 || tick >= clip.length)
            continue;
        // the attack's level: its peak in the 30 ms after it
        const auto from = (std::size_t) std::max (0.0, t * sample.sampleRate);
        const auto to = std::min (sample.frames(), from + (std::size_t) (0.03 * sample.sampleRate));
        const auto* l = sample.leftData();
        const auto* r = sample.rightData(); // (the left channel again for a mono file)
        float peak = 0.0f;
        for (auto i = from; i < to; ++i)
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        out.push_back ({ t, tick, peak });
        loudest = std::max (loudest, peak);
    }
    for (auto& h : out)
        h.level = loudest > 0.0f ? std::clamp (h.level / loudest, 1.0f / 127.0f, 1.0f) : 1.0f;
    return out;
}

// Extract Groove from an audio clip: one point per occupied Base slot (the loudest attack's place and level), over
// the clip's length, as from a MIDI clip's notes (groove::extract).
inline Groove extract (const Song& song, const Clip& clip, const SampleData& sample, GrooveBase base, std::string name)
{
    Pattern hits;
    for (const auto& h : hitsOf (song, clip, sample))
    {
        Note n;
        n.start = h.tick;
        n.velocity = h.level;
        hits.notes.push_back (n);
    }
    return groove::extract (hits, clip.length, base, std::move (name));
}

// Commit on the audio clip of grid slot (track, scene): a warp marker at each attack in its playing portion, on the beat
// the groove plays it on (timing, quantise and the random part as of pass 0, the way MIDI notes are committed); the
// groove leaves the slot. The channel gets its own copy first when other clips play it, so they keep their timing. A
// clip that was not warped is warped from here on (Live: grooves need Warp). Returns the markers placed; -1 when the
// slot has no audio clip with a groove.
inline int commit (Song& song, int track, int scene, const SampleData& sample)
{
    if (! song.clipGrid)
        return -1;
    auto& grid = *song.clipGrid;
    const auto* found = launcher::clipAt (grid, track, scene);
    if (found == nullptr || found->kind != ClipKind::audio || found->channel < 0 || found->channel >= (int) song.channels.size())
        return -1;
    const auto* g = groove::ofClip (song, *found);
    if (g == nullptr)
        return -1;
    const auto groove = *g;
    const auto hits = hitsOf (song, *found, sample);
    Clip* clip = nullptr;
    for (auto& c : grid.clips)
        if (&c == found)
            clip = &c;
    if (clip == nullptr)
        return -1;
    // the channel to write: its own, or a copy when another clip (grid or arrangement) plays it
    int uses = 0;
    song.forEachClipList ([&] (std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::audio && c.channel == clip->channel)
                ++uses;
    });
    if (uses > 1)
    {
        auto copy = song.channels[(std::size_t) clip->channel];
        copy.name += " (groove)";
        song.channels.push_back (std::move (copy));
        clip = nullptr;
        for (auto& c : grid.clips) // (the vector of channels grew; the grid's clips did not move)
            if (c.track == track && c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size() && grid.slots[(std::size_t) c.gridSlot].scene == scene)
                clip = &c;
        if (clip == nullptr)
            return -1;
        clip->channel = (int) song.channels.size() - 1;
    }
    auto& s = song.channels[(std::size_t) clip->channel].sampler;
    const auto seconds = clipwarp::fileSeconds (sample);
    if (! warpedit::ensureMarkers (s, seconds, song.bpm))
        return -1;
    s.keepPitch = true;
    const auto stretch = std::clamp (clip->stretch, 0.01, 100.0);
    const auto offset = (double) clip->offset + (double) clip->offsetFraction;
    const auto b0 = warpedit::beatAt (s.warp, 0.0);
    int placed = 0;
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const auto played = groove::apply (groove, song.grooves.amount, hits[i].tick, hits[i].level, groove::randomBits (song.chanceSeed, i, 0));
        const auto beat = b0 + ((double) played.start + offset) / (ppq * stretch);
        const auto index = warpedit::addMarker (s, hits[i].seconds);
        if (index < 0)
            continue;
        warpedit::moveMarker (s, index, beat, seconds);
        ++placed;
    }
    warpedit::syncLength (s, seconds);
    if (auto* slot = launcher::settingsAt (grid, track, scene))
        slot->groove = 0;
    return placed;
}
} // namespace lynx::grooveaudio
