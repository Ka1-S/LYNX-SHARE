#pragma once

// Playlist clip operations behind the clip menu (Make unique, Merge, Reverse, Fit to tempo, Select all of a
// source) and the audio clip's time mapping. JUCE-free, header-only; each edits a Song and is run inside
// AudioEngine::editSong, so it is one undo step.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <limits>
#include <vector>

#include "core/SamplePrecompute.h"
#include "core/Song.h"

namespace lynx::clipops
{
// The part of its file an audio clip's channel plays (its Trim; a clip plays the whole region): a clip's offsets count from
// its first frame, and TIME stretches its length (FL: FUN_00ff9d70, REA round 180d). Pass its `frames` as `fileFrames`
// below and add `first` to a frame drawn from them, so that drawing, clip lengths and playback agree.
inline precompute::PlayedSpan clipSpan (const ChannelDef& channel, const std::shared_ptr<const SampleData>& data)
{
    return precompute::playedSpan (data, channel.sampler, channel.instrument == InstrumentKind::audioClip);
}

// Audio frames of the file per Playlist tick for an audio clip's channel: the file's own speed, or, when the
// channel is stretched (stretchBeats > 0, with or without keeping the pitch), the played span (clipSpan) spread over
// that many beats.
inline double framesPerTick (const SamplerSettings& sampler, double fileFrames, double fileRate, double bpm) noexcept
{
    if (sampler.stretchBeats > 0.0f && fileFrames > 0.0)
        return fileFrames / ((double) sampler.stretchBeats * ppq);
    return 60.0 / (std::max (1.0, bpm) * ppq) * fileRate;
}

// Length of the whole file in ticks (as the Playlist plays it).
inline std::int64_t fileTicks (const SamplerSettings& sampler, double fileFrames, double fileRate, double bpm) noexcept
{
    const auto perTick = framesPerTick (sampler, fileFrames, fileRate, bpm);
    return perTick > 0.0 ? (std::int64_t) std::llround (fileFrames / perTick) : 0;
}

// How much faster an audio clip's channel reads its file because of its pitch (round 163): 2^(semitones/12) for the
// pitch knob plus Misc › Fine tune when it resamples; 1 with "keep pitch" (the pitch shifter keeps the length).
inline double clipSpeed (const ChannelDef& channel) noexcept
{
    if (channel.sampler.keepPitch)
        return 1.0;
    const auto semitones = (double) std::clamp (channel.sampler.pitch, -24.0f, 24.0f) + (double) channel.misc.fineTune / 100.0;
    return std::exp2 (semitones / 12.0);
}

// framesPerTick / fileTicks of an audio clip's channel, its pitch included (what the Playlist shows and plays).
inline double framesPerTick (const ChannelDef& channel, double fileFrames, double fileRate, double bpm) noexcept
{
    return framesPerTick (channel.sampler, fileFrames, fileRate, bpm) * clipSpeed (channel);
}

inline std::int64_t fileTicks (const ChannelDef& channel, double fileFrames, double fileRate, double bpm) noexcept
{
    const auto perTick = framesPerTick (channel, fileFrames, fileRate, bpm);
    return perTick > 0.0 ? (std::int64_t) std::llround (fileFrames / perTick) : 0;
}

// An audio clip's speed with FL 2026's own clip pitch (round 167k, keys 7/8): the clip pitch is a shift that keeps the
// length (FL render: a 2 s clip pitched +3 still lasts 2 s on a channel that resamples), so only the channel's counts;
// FL's clip stretch (round 167l) slows it down by that factor, in every time-stretch mode
inline double clipSpeed (const ChannelDef& channel, const Clip& clip) noexcept
{
    return clipSpeed (channel) / std::clamp (clip.stretch, 0.01, 100.0);
}

inline double framesPerTick (const ChannelDef& channel, const Clip& clip, double fileFrames, double fileRate, double bpm) noexcept
{
    return framesPerTick (channel.sampler, fileFrames, fileRate, bpm) * clipSpeed (channel, clip);
}

inline std::int64_t fileTicks (const ChannelDef& channel, const Clip& clip, double fileFrames, double fileRate, double bpm) noexcept
{
    const auto perTick = framesPerTick (channel, clip, fileFrames, fileRate, bpm);
    return perTick > 0.0 ? (std::int64_t) std::llround (fileFrames / perTick) : 0;
}

// The clip reads its file backwards: the channel's Reverse, flipped again by the clip's own reverse (key 9).
inline bool playsBackwards (const ChannelDef& channel, const Clip& clip) noexcept
{
    return channel.sampler.reverse != clip.reversed;
}

// Keys 7 / 8 of FL's Playlist (EEPitchSelectedAudioClipsDown/UpAction → FUN_00e2d7b0): every selected audio clip's
// pitch moves one semitone, clamped to -12..+12; the clips keep their place and length. False when none is audio.
inline bool pitchAudioClips (Song& song, const std::vector<int>& clips, int semitones) noexcept
{
    auto any = false;
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
        {
            auto& c = song.playlist[(std::size_t) i];
            c.pitch = std::clamp (c.pitch + (float) semitones, -12.0f, 12.0f);
            any = true;
        }
    return any;
}

// Key 9 (EEReverseSelectedAudioClipsAction → FUN_00e2db70 → FUN_01065520): toggles each selected audio clip's
// reverse; the part of the file it shows mirrors (start' = file − min(end, file), clamped at 0), so the clip plays the
// same audio backwards. `wholeFileTicks(i)` gives clip i's file length in ticks (≤ 0: unknown, the offset stays).
template <typename FileTicks>
inline bool reverseAudioClips (Song& song, const std::vector<int>& clips, FileTicks&& wholeFileTicks)
{
    auto any = false;
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
        {
            const auto file = (std::int64_t) wholeFileTicks (i);
            auto& c = song.playlist[(std::size_t) i];
            c.reversed = ! c.reversed;
            if (file > 0)
            {
                const auto played = std::min (c.length, std::max<std::int64_t> (0, file - c.offset));
                c.offset = std::max<std::int64_t> (0, file - (c.offset + played));
                c.offsetFraction = 0.0f;
            }
            any = true;
        }
    return any;
}

// Scales every clip of audio channel `channel` (offset, length, fades) after its file's length in ticks changed from
// `before` to `after` (a stretch or a resampling pitch), so each keeps showing and playing the same audio.
inline void rescaleClipsOf (Song& song, int channel, std::int64_t before, std::int64_t after) noexcept
{
    if (before <= 0 || after <= 0 || before == after)
        return;
    const auto k = (double) after / (double) before;
    for (auto& c : song.playlist)
        if (c.kind == ClipKind::audio && c.channel == channel)
        {
            const auto exact = ((double) c.offset + (double) c.offsetFraction) * k;
            c.offset = (std::int64_t) std::llround (exact);
            c.offsetFraction = (float) (exact - (double) c.offset);
            c.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) c.length * k));
            c.fadeIn = (std::int64_t) std::llround ((double) c.fadeIn * k);
            c.fadeOut = (std::int64_t) std::llround ((double) c.fadeOut * k);
        }
}

// The "Pitch..." command (round 163) on a sample channel or an audio clip's channel: the pitch knob gets `semitones`
// plus `cents` (fractions of a semitone; the fine tune is left alone), and `keepLength` chooses the stretch engine
// (pitch only) or resampling (the speed follows, like a tape). An audio channel's clips follow its new length when it
// resamples. `fileFrames`/`fileRate` = its file (0 when unknown: the clips are left as they are).
inline void setChannelPitch (Song& song, int channel, double semitones, double cents, bool keepLength, double fileFrames = 0.0,
                             double fileRate = 0.0)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return;
    auto& ch = song.channels[(std::size_t) channel];
    const auto audio = ch.instrument == InstrumentKind::audioClip;
    const auto before = audio && fileFrames > 0.0 ? fileTicks (ch, fileFrames, fileRate, song.bpm) : 0;
    ch.sampler.pitch = (float) std::clamp (semitones + cents / 100.0, -24.0, 24.0);
    ch.sampler.keepPitch = keepLength;
    if (audio && fileFrames > 0.0)
        rescaleClipsOf (song, channel, before, fileTicks (ch, fileFrames, fileRate, song.bpm));
}

// "Fit to tempo": the number of beats a sample of `seconds` spans, so that at the project's `bpm` it keeps in time.
// Issue #32 #7: this used to assume a power-of-two number of beats (1, 2, 4 ... 256) and round to it, so a 3-bar or
// 6-beat loop and a loop with a reverb tail came out of tempo. Now, as FL's TIME knob does ("%s (embedded)",
// "%s (estimated)", FL strings r168s): the WAV's ACID beat count when it has one (`embeddedBeats`); else the sample's
// own tempo (`sourceBpm`, detected) taken in the octave nearest the project tempo (the least stretch: a 90 BPM loop in
// a 120 project stays 90, at 140 it is read as 180), the exact count (a tail stays a fraction of a beat); else (no
// pulse) the power-of-two count nearest its length at the project tempo.
inline float fitBeats (double seconds, double bpm, float embeddedBeats = 0.0f, double sourceBpm = 0.0) noexcept
{
    if (embeddedBeats > 0.0f)
        return embeddedBeats;
    bpm = std::max (1.0, bpm);
    if (! (seconds > 0.0))
        return 0.0f;
    if (sourceBpm > 0.0)
    {
        while (sourceBpm < bpm / std::sqrt (2.0))
            sourceBpm *= 2.0;
        while (sourceBpm >= bpm * std::sqrt (2.0))
            sourceBpm *= 0.5;
        return (float) std::clamp (seconds * sourceBpm / 60.0, 0.01, 1024.0);
    }
    const auto beats = seconds * bpm / 60.0; // no tempo known (no pulse found): a power-of-two loop, as before
    return (float) std::pow (2.0, std::clamp (std::round (std::log2 (beats)), 0.0, 8.0));
}

// FL's TIME knob presets (its right-click menu, r168s c_f20): "1 beat, 2 beats, 1 bar, 2 bars, 3 bars, 4 bars" in beats.
// (round 190fg: in quarter notes, a beat and a bar of the song's meter)
inline std::vector<float> timePresetBeats (Meter meter)
{
    const auto beat = (float) meter.beat() / (float) ppq, bar = (float) meter.bar() / (float) ppq;
    return { beat, 2.0f * beat, bar, 2.0f * bar, 3.0f * bar, 4.0f * bar };
}

// Clips (indices into song.playlist) that use the same source as `clip`: the same pattern, the same audio
// channel or the same automation.
inline std::vector<int> sameSource (const Song& song, int clip)
{
    std::vector<int> out;
    if (clip < 0 || clip >= (int) song.playlist.size())
        return out;
    const auto& c = song.playlist[(std::size_t) clip];

    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& o = song.playlist[(std::size_t) i];
        if (o.kind != c.kind)
            continue;
        if ((c.kind == ClipKind::pattern && o.pattern == c.pattern) || (c.kind == ClipKind::audio && o.channel == c.channel)
            || (c.kind == ClipKind::automation && o.automation == c.automation))
            out.push_back (i);
    }
    return out;
}

// FL's Playlist Select > Overlapping / Stacked (REA round 153, behaviour from its manual): per track, in time order.
// Overlapping: clips that start before an earlier clip of the track ends (every layer except the lowest).
// Stacked: clips lying completely inside another clip of the track (exact duplicates: all but the first), so
// deleting them leaves what is heard unchanged.
inline std::vector<int> overlappingClips (const Song& song)
{
    std::vector<int> order ((std::size_t) song.playlist.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;
    std::stable_sort (order.begin(), order.end(), [&song] (int a, int b) {
        const auto& ca = song.playlist[(std::size_t) a];
        const auto& cb = song.playlist[(std::size_t) b];
        return ca.track != cb.track ? ca.track < cb.track : ca.start < cb.start;
    });
    std::vector<int> out;
    int track = -1;
    std::int64_t reach = 0;
    for (auto i : order)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.track != track)
        {
            track = c.track;
            reach = c.end();
            continue;
        }
        if (c.start < reach)
            out.push_back (i);
        reach = std::max (reach, c.end());
    }
    std::sort (out.begin(), out.end());
    return out;
}

// FL's automatic crossfades (round 166e, FL 2026 measured in docs/analise/REA_rodada166e.md §1): with the project's
// "Automatic crossfades" on, where an audio clip starts inside the previous audio clip of its track and ends after
// it, the two crossfade over the whole overlap (the earlier one fades out, the later one fades in). Clips lying
// completely inside another get none. Recomputed after every edit (AudioEngine::editSong / setSong); off = none.
inline void updateCrossfades (std::vector<Clip>& clips, bool on)
{
    for (auto& c : clips)
        c.crossfadeIn = c.crossfadeOut = 0;
    if (! on)
        return;

    std::vector<int> order;
    order.reserve (clips.size());
    for (std::size_t i = 0; i < clips.size(); ++i)
        if (clips[i].kind == ClipKind::audio && clips[i].length > 0)
            order.push_back ((int) i);
    std::stable_sort (order.begin(), order.end(), [&clips] (int a, int b) {
        const auto& ca = clips[(std::size_t) a];
        const auto& cb = clips[(std::size_t) b];
        return ca.track != cb.track ? ca.track < cb.track : ca.start < cb.start;
    });
    for (std::size_t k = 1; k < order.size(); ++k)
    {
        auto& a = clips[(std::size_t) order[k - 1]];
        auto& b = clips[(std::size_t) order[k]];
        if (a.track != b.track || b.start <= a.start || b.start >= a.end() || b.end() <= a.end())
            continue;
        if ((a.fadeCurve & 2) != 0 && (b.fadeCurve & 1) != 0) // a comp crossfade (round 177c) keeps its own curve
            continue;
        const auto overlap = a.end() - b.start;
        a.crossfadeOut = std::max (a.crossfadeOut, overlap);
        b.crossfadeIn = std::max (b.crossfadeIn, overlap);
    }
}

inline void updateCrossfades (Song& song)
{
    song.forEachClipList ([on = song.autoCrossfades] (std::vector<Clip>& clips) { updateCrossfades (clips, on); });
}

// Issue #87 (round 178a): an audio clip that does not follow the tempo (its channel's Time stretch is off,
// stretchBeats == 0: FL's default Resample with no Time set) plays its audio at the file's own speed, so it lasts the
// same seconds whatever the tempo. FL keeps such a clip's region in milliseconds (the .flp's playlist item start / end
// offsets, REA_rodada110 §1 and REA_rodada178a §1) and resizes the clip when the project tempo changes: a lower tempo
// makes it shorter in bars, a higher one longer. LYNX keeps clips in ticks, so a tempo change rescales those clips'
// length, start offset and fades by new / old tempo (their position stays). Clips whose channel follows the tempo
// (stretchBeats > 0) keep their length in beats, as in FL. Every arrangement's clips follow. Call it for a change of the
// project tempo itself (the tempo display, tap tempo, the project settings), not for tempo automation.
inline void followTempo (Song& song, double newBpm)
{
    const auto oldBpm = song.bpm;
    song.bpm = newBpm;
    if (! (oldBpm > 0.0) || ! (newBpm > 0.0) || oldBpm == newBpm)
        return;
    const auto factor = newBpm / oldBpm;
    const auto scaled = [factor] (std::int64_t ticks) { return (std::int64_t) std::llround ((double) ticks * factor); };
    song.forEachClipList ([&] (std::vector<Clip>& clips) {
        for (auto& c : clips)
        {
            if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size()
                || song.channels[(std::size_t) c.channel].sampler.stretchBeats > 0.0f)
                continue;
            c.length = std::max<std::int64_t> (1, scaled (c.length));
            const auto offset = ((double) c.offset + (double) c.offsetFraction) * factor;
            c.offset = std::max<std::int64_t> (0, (std::int64_t) std::llround (offset));
            c.offsetFraction = c.offset > 0 ? (float) (offset - (double) c.offset) : 0.0f;
            c.fadeIn = std::clamp<std::int64_t> (scaled (c.fadeIn), 0, c.length);
            c.fadeOut = std::clamp<std::int64_t> (scaled (c.fadeOut), 0, c.length - c.fadeIn);
        }
    });
}

// FL's crossfade curve (round 166e): an S (raised cosine) whose fade-in and fade-out add up to 1 at every point.
inline float crossfadeCurve (float x) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    return 0.5f - 0.5f * std::cos (3.14159265358979f * x);
}

// FL's fade tension on a fade's curve y (0..1), as its voices apply it (REA round 180d, FUN_006774b0 / FUN_00977870 / FUN_00677540):
// the raw value from the file, rounded to 0.01; 0 leaves the curve alone. A 200 ms fade-out at -812.933 reproduces FL's render
// within 0.012 RMS.
inline float fadeTensionCurve (float y, float tension) noexcept
{
    const auto t = std::round ((double) tension * 100.0) * 0.01;
    if (t == 0.0)
        return y;
    const auto a = std::abs (t);
    const auto range = std::log (a + 1.0);
    return t > 0.0 ? (float) (1.0 - (std::exp ((1.0 - (double) y) * range) - 1.0) / a)
                   : (float) ((std::exp ((double) y * range) - 1.0) / a);
}

// A comp crossfade (round 177c): sin(x·π/2) — the fade-in and the fade-out (the same curve, mirrored) keep constant
// power (sin² + cos² = 1), the right sum for two different takes (uncorrelated) where an S curve would dip ~3 dB.
inline float equalPowerCurve (float x) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    return std::sin (1.57079632679490f * x);
}

// FL's audio clip gain handle (round 166e §2): −96.1 dB (shown "-INF") … +36 dB, 0.05 dB per pixel of drag.
constexpr double clipGainMinDb = -96.1, clipGainMaxDb = 36.0, clipGainDbPerPixel = 0.05;
inline double clipGainToDb (float gain) noexcept
{
    return gain <= 0.0f ? clipGainMinDb : std::clamp (20.0 * std::log10 ((double) gain), clipGainMinDb, clipGainMaxDb);
}
inline float clipGainFromDb (double db) noexcept
{
    return (float) std::pow (10.0, std::clamp (db, clipGainMinDb, clipGainMaxDb) / 20.0);
}

inline std::vector<int> stackedClips (const Song& song)
{
    std::vector<int> out;
    const auto& list = song.playlist;
    for (int a = 0; a < (int) list.size(); ++a)
        for (int b = 0; b < (int) list.size(); ++b)
        {
            const auto& c = list[(std::size_t) a];
            const auto& o = list[(std::size_t) b];
            if (a == b || o.track != c.track || o.start > c.start || o.end() < c.end())
                continue;
            if (! (o.start == c.start && o.end() == c.end()) || b < a)
            {
                out.push_back (a);
                break;
            }
        }
    return out;
}

inline std::vector<int> mutedClips (const Song& song)
{
    std::vector<int> out;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (song.playlist[(std::size_t) i].muted)
            out.push_back (i);
    return out;
}

// FL's Alt+M / Shift+Alt+M in the Playlist (EEMuteAction / EEUnmuteAction → FUN_00e3cc70, undo "mute" / "unmute";
// issue #53): sets (not toggles) the clip mute of the selected clips, or of every clip when nothing is selected
// (FUN_01061e80 walks the selected items, else all of them). With a time selection only the clips that start inside
// it, ends included (FUN_00e3cb60 against the range FUN_00e87aa0 sets from the editor's time selection). Returns how
// many clips changed.
inline int setClipsMuted (Song& song, const std::vector<int>& selection, bool mute, std::int64_t from = 0,
                          std::int64_t to = std::numeric_limits<std::int64_t>::max())
{
    int changed = 0;
    auto apply = [&] (int i) {
        if (i < 0 || i >= (int) song.playlist.size())
            return;
        auto& c = song.playlist[(std::size_t) i];
        if (c.start < from || c.start > to)
            return;
        if (c.muted != mute)
        {
            c.muted = mute;
            ++changed;
        }
    };
    if (selection.empty())
        for (int i = 0; i < (int) song.playlist.size(); ++i)
            apply (i);
    else
        for (const auto i : selection)
            apply (i);
    return changed;
}

// Audio "Make unique": the clips get their own copy of the audio channel (so reversing, stretching or renaming
// them leaves the other clips of that sample alone). Returns the new channel, or -1.
inline int makeAudioUnique (Song& song, const std::vector<int>& clips)
{
    int source = -1;
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
        {
            source = song.playlist[(std::size_t) i].channel;
            break;
        }
    if (source < 0 || source >= (int) song.channels.size())
        return -1;

    auto copy = song.channels[(std::size_t) source];
    // "Name #2", "Name #3"...: the first free number
    const auto base = copy.name;
    for (int n = 2;; ++n)
    {
        const auto candidate = base + " #" + std::to_string (n);
        if (std::none_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) { return c.name == candidate; }))
        {
            copy.name = candidate;
            break;
        }
    }
    song.channels.push_back (std::move (copy));
    const auto channel = (int) song.channels.size() - 1;

    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio
            && song.playlist[(std::size_t) i].channel == source)
            song.playlist[(std::size_t) i].channel = channel;
    return channel;
}

// Merge: clips of the same source on the same track that continue each other (each starts where the previous
// ends, and its offset continues the previous one's) become one clip. Fades at the joins are dropped; the
// first clip's fade-in and the last one's fade-out are kept. Gains and mutes must agree. Returns true if
// anything merged.
inline bool canMerge (const Song& song, const std::vector<int>& clips)
{
    if (clips.size() < 2)
        return false;
    std::vector<Clip> list;
    for (auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            return false;
        list.push_back (song.playlist[(std::size_t) i]);
    }
    std::sort (list.begin(), list.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    const auto& f = list.front();
    if (f.kind == ClipKind::automation)
        return false;

    for (std::size_t i = 1; i < list.size(); ++i)
    {
        const auto& a = list[i - 1];
        const auto& b = list[i];
        if (b.kind != f.kind || b.track != f.track || b.muted != f.muted || std::abs (b.gain - f.gain) > 1.0e-6f
            || (f.kind == ClipKind::pattern && b.pattern != f.pattern) || (f.kind == ClipKind::audio && b.channel != f.channel))
            return false;
        if (b.start != a.end() || b.offset != a.offset + a.length)
            return false;
    }
    return true;
}

inline bool mergeClips (Song& song, std::vector<int> clips)
{
    if (! canMerge (song, clips))
        return false;
    std::sort (clips.begin(), clips.end(), [&song] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });
    auto merged = song.playlist[(std::size_t) clips.front()];
    const auto& last = song.playlist[(std::size_t) clips.back()];
    merged.length = last.end() - merged.start;
    merged.fadeOut = last.fadeOut;

    auto doomed = clips;
    std::sort (doomed.rbegin(), doomed.rend());
    for (auto i : doomed)
        song.playlist.erase (song.playlist.begin() + i);
    song.playlist.push_back (merged);
    return true;
}

// FL's track menu "Merge clips" (REA round 143): on `track`, every run of clips that continue each other (same
// source, as canMerge) becomes one clip. Returns how many clips disappeared.
inline int mergeTrackClips (Song& song, int track)
{
    int removed = 0;
    for (bool again = true; again;)
    {
        again = false;
        std::vector<int> onTrack;
        for (int i = 0; i < (int) song.playlist.size(); ++i)
            if (song.playlist[(std::size_t) i].track == track)
                onTrack.push_back (i);
        std::sort (onTrack.begin(), onTrack.end(), [&song] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });
        for (std::size_t i = 0; i + 1 < onTrack.size() && ! again; ++i)
        {
            std::vector<int> run { onTrack[i] };
            for (auto j = i + 1; j < onTrack.size() && canMerge (song, { run.back(), onTrack[j] }); ++j)
                run.push_back (onTrack[j]);
            if (run.size() > 1 && mergeClips (song, run))
            {
                removed += (int) run.size() - 1;
                again = true;
            }
        }
    }
    return removed;
}

// FL's clip menu "Select source pattern / channel" (REA round 143): the clips (of that kind) play another source.
inline void setClipSource (Song& song, const std::vector<int>& clips, ClipKind kind, int source)
{
    for (auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size() || song.playlist[(std::size_t) i].kind != kind)
            continue;
        auto& c = song.playlist[(std::size_t) i];
        if (kind == ClipKind::pattern && source >= 0 && source < (int) song.patterns.size())
            c.pattern = source;
        else if (kind == ClipKind::audio && source >= 0 && source < (int) song.channels.size())
            c.channel = source;
        else if (kind == ClipKind::automation && source >= 0 && source < (int) song.automations.size())
            c.automation = source;
    }
}

// Reverse an audio clip: the channel plays its file backwards (all clips of that channel, as in FL; "Make
// unique" first to reverse one clip alone). The clip keeps showing the same stretch of time: its offset is
// mirrored so the part that was heard is still the part that plays (backwards).
inline void reverseAudio (Song& song, int clip, std::int64_t wholeFileTicks)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return;
    const auto channel = song.playlist[(std::size_t) clip].channel;
    if (song.playlist[(std::size_t) clip].kind != ClipKind::audio || channel < 0 || channel >= (int) song.channels.size())
        return;

    auto& sampler = song.channels[(std::size_t) channel].sampler;
    sampler.reverse = ! sampler.reverse;

    if (wholeFileTicks <= 0)
        return;
    for (auto& c : song.playlist)
        if (c.kind == ClipKind::audio && c.channel == channel)
        {
            const auto played = std::min (c.length, std::max<std::int64_t> (0, wholeFileTicks - c.offset));
            c.offset = std::max<std::int64_t> (0, wholeFileTicks - (c.offset + played));
        }
}
// Playlist › Edit › Shift left / right (Shift+Left / Shift+Right; EEShiftL/RAction Tag -1 / 1 → FUN_00eb32a0 →
// FUN_00e51730 (editor, Tag, 0), round 189e): the selected clips move by `delta` ticks (one snap step; with Alt held
// one pixelStepTicks, round 190ag), held so that none starts before the song start: the whole selection stops together
// and keeps its spacing. Clips that land on other clips simply overlap them, as after a drag. Returns the move applied.
inline std::int64_t shiftClipsInTime (std::vector<Clip>& playlist, const std::vector<int>& selection, std::int64_t delta)
{
    for (auto i : selection)
        if (i >= 0 && i < (int) playlist.size())
            delta = std::max (delta, -playlist[(std::size_t) i].start);
    for (auto i : selection)
        if (i >= 0 && i < (int) playlist.size())
            playlist[(std::size_t) i].start += delta;
    return delta;
}

// FL's fine step (round 190ag, issue #147; FUN_00e51730 with Alt held, and the Shift+wheel nudge): one pixel's worth of
// ticks at the current zoom (the editor's ticks-per-pixel, rounded), never less than one tick.
inline std::int64_t pixelStepTicks (double pixelsPerTick)
{
    return pixelsPerTick > 0.0 ? std::max<std::int64_t> (1, (std::int64_t) std::llround (1.0 / pixelsPerTick)) : 1;
}

// After a keyboard shift FL keeps the edge that leads the move in view (FUN_00e51730 -> FUN_00e7e290 with 1/32 of the
// view as the margin, round 190ag): `edge` is the earliest start (moving earlier) or the latest end (moving later) of
// the moved clips. When it is closer than one margin to the side of the view it moves towards (or past it), the view
// scrolls so that the edge sits three margins inside that side. Returns the new scroll position in ticks.
inline double scrollToShowEdge (double scrollTicks, double viewPixels, double pixelsPerTick, std::int64_t edge, bool movingLater)
{
    if (viewPixels <= 0.0 || pixelsPerTick <= 0.0)
        return scrollTicks;
    const auto margin = std::max (1.0, std::round (viewPixels / 32.0));
    const auto ticksPerPixel = 1.0 / pixelsPerTick;
    if (movingLater)
        return (double) edge > scrollTicks + (viewPixels - margin) * ticksPerPixel
                   ? std::max (0.0, (double) edge - (viewPixels - 3.0 * margin) * ticksPerPixel) : scrollTicks;
    return (double) edge < scrollTicks + margin * ticksPerPixel ? std::max (0.0, (double) edge - 3.0 * margin * ticksPerPixel) : scrollTicks;
}

// Playlist › Edit › "Nudge clips with mouse wheel" (Shift+wheel, FUN_00e82270, round 189g; checked in FL 2026: the clips
// stay, their content moves, "playlist shift clip"): the content of the chosen clips moves `delta` ticks earlier (their
// offset grows; negative = later), held so that no offset goes below 0. Returns the shift applied.
inline std::int64_t shiftClipContent (std::vector<Clip>& playlist, const std::vector<int>& clips, std::int64_t delta)
{
    for (auto i : clips)
        if (i >= 0 && i < (int) playlist.size())
            delta = std::max (delta, -playlist[(std::size_t) i].offset);
    for (auto i : clips)
        if (i >= 0 && i < (int) playlist.size())
            playlist[(std::size_t) i].offset += delta;
    return delta;
}

// Playlist › Edit › Shift up / down (Shift+Up / Shift+Down; EEShiftU/DAction Tag 1 / -1 → FUN_00eb3300 → FUN_00e51730
// (editor, 0, Tag); checked live in FL 2026, round 189e shot f11: every selected clip moves one track down): the
// selected clips move `tracks` tracks (negative = up), held inside tracks 1..500. Returns the move applied.
inline int shiftClipsTrack (std::vector<Clip>& playlist, const std::vector<int>& selection, int tracks)
{
    for (auto i : selection)
        if (i >= 0 && i < (int) playlist.size())
            tracks = std::clamp (tracks, -playlist[(std::size_t) i].track, 499 - playlist[(std::size_t) i].track);
    for (auto i : selection)
        if (i >= 0 && i < (int) playlist.size())
            playlist[(std::size_t) i].track += tracks;
    return tracks;
}

// Playlist › Select › "Select by selected source" (Shift+C; EESelBySourceAction → FUN_00eb1da0, Playlist only, round 189e):
// every clip of the current clip source `source` (the Picker's: a pattern, an audio-clip channel or an automation); an
// audio clip of another audio-clip channel of the same file counts too (FL compares the channels' sample). Empty when there
// is none (FL: "No clips selected|There are no clips associated with the currently selected source clip.").
inline std::vector<int> clipsOfSource (const Song& song, const Clip& source)
{
    std::vector<int> out;
    const auto samplePath = [&song] (int channel) -> const std::string* {
        if (channel < 0 || channel >= (int) song.channels.size() || song.channels[(std::size_t) channel].instrument != InstrumentKind::audioClip)
            return nullptr;
        return &song.channels[(std::size_t) channel].samplePath;
    };
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& o = song.playlist[(std::size_t) i];
        if (o.kind != source.kind)
            continue;
        auto same = false;
        if (source.kind == ClipKind::pattern)
            same = o.pattern == source.pattern;
        else if (source.kind == ClipKind::automation)
            same = o.automation == source.automation;
        else if (source.kind == ClipKind::audio)
        {
            const auto* a = samplePath (o.channel);
            const auto* b = samplePath (source.channel);
            same = o.channel == source.channel || (a != nullptr && b != nullptr && ! a->empty() && *a == *b);
        }
        if (same)
            out.push_back (i);
    }
    return out;
}

// Playlist › Select › "Invert selection" (Shift+I; EEInvertSelAction → FUN_00eb1ae0): every clip not selected now.
inline std::vector<int> invertedSelection (int clipCount, const std::vector<int>& selection)
{
    std::vector<bool> chosen ((std::size_t) std::max (0, clipCount), false);
    for (auto i : selection)
        if (i >= 0 && i < clipCount)
            chosen[(std::size_t) i] = true;
    std::vector<int> out;
    for (int i = 0; i < clipCount; ++i)
        if (! chosen[(std::size_t) i])
            out.push_back (i);
    return out;
}
// Make unique as sample (round 189l): the clip now plays `channel`, a file holding what it played, from its start; its own
// pitch, stretch and reverse are in the file, so they are reset
inline void resetRenderedClip (Clip& c, int channel) noexcept
{
    c.channel = channel;
    c.offset = 0;
    c.offsetFraction = 0.0f;
    c.pitch = 0.0f;
    c.stretch = 1.0;
    c.reversed = false;
}

// --- FL's per-clip edge declicker (round 189i, REA FLEngine_x64.dll) --------------------------------------------------------
// The fade handle menu (PLFadeHandleMenu, PLFadeHandleMenuClick 0xea9040 -> FUN_00d02e00 / FUN_00d01bc0) stores, per side,
// "Manual" (fade flags bit 0 / 1, FUN_00cfbf60: turning it off also zeroes that side's fade) and the Declicker choice
// (flags >> 2 & 7 / >> 14 & 7, FUN_00cfc020 via FUN_00d03100): Automatic 0, Enabled 2 ("use channel declicker"),
// Disabled 3 ("bypass declicker"); 1 is FL's own "Automatic, joined seamlessly" (FUN_00d011a0 resets 0 / 1 and sets 1
// where two clips continue each other). FL /R renders (round 189l) play 1 as Automatic: the channel's declicker is
// skipped only on a side that is Disabled, manual or has a fade.
enum DeclickChoice
{
    declickAutomatic = 0,
    declickJoined = 1,
    declickEnabled = 2,
    declickDisabled = 3
};

inline int declickChoice (const Clip& c, int side) noexcept
{
    return (int) ((c.flFadeFlags >> (side == 0 ? 2 : 14)) & 7u);
}

inline void setDeclickChoice (Clip& c, int side, int choice) noexcept
{
    const auto shift = side == 0 ? 2u : 14u;
    c.flFadeFlags = (c.flFadeFlags & ~(7u << shift)) | (((std::uint32_t) choice & 7u) << shift);
}

inline bool manualFade (const Clip& c, int side) noexcept
{
    return (c.flFadeFlags & (side == 0 ? 1u : 2u)) != 0;
}

// FL's Manual item (FUN_00cfbf60): on / off per side; off also zeroes that side's fade (FL keeps only automatic
// crossfades on a side that isn't manual)
inline void setManualFade (Clip& c, int side, bool on) noexcept
{
    const auto bit = side == 0 ? 1u : 2u;
    c.flFadeFlags = on ? (c.flFadeFlags | bit) : (c.flFadeFlags & ~bit);
    if (! on)
        (side == 0 ? c.fadeIn : c.fadeOut) = 0;
}


// --- FL's Create crossfade (fade handle menu Tag 100, round 189n; FLEngine 2026 [DECOMP] FUN_00d02580 -> per clip
// FUN_00d02410, neighbour FUN_00d02310 / FUN_00cf9930, FUN_00cfd680 sets the fades, FUN_00cfaf80 clamps them; menu
// state FUN_00cfc310 / FUN_00d023d0) ----

// FL's FUN_00cf7580: `a` and `b` already crossfade — neither side manual, a's fade out == b's fade in, and a's fade out
// starts where b starts. (Two touching clips without fades pass it too, as in FL.)
inline bool flCrossfadePair (const Clip& a, const Clip& b) noexcept
{
    return ! manualFade (a, 1) && ! manualFade (b, 0) && a.fadeOut == b.fadeIn && a.end() - a.fadeOut == b.start;
}

// The clip that `index` would crossfade with on `side` (0 = before it / its fade in, 1 = after it / its fade out), or -1:
// the nearest clip of the same track, in start order, that overlaps it or already crossfades with it (FL's search with
// flags 2: any clip kind; FL's array is sorted by start). Before: every earlier clip is tried; after: the search stops
// at the first clip that starts past this one's end.
inline int crossfadeNeighbour (const std::vector<Clip>& clips, int index, int side)
{
    if (index < 0 || index >= (int) clips.size())
        return -1;
    const auto& c = clips[(std::size_t) index];
    const auto before = [&clips] (int x, int y) {
        const auto& a = clips[(std::size_t) x];
        const auto& b = clips[(std::size_t) y];
        return a.start != b.start ? a.start < b.start : x < y;
    };
    const auto end = [] (const Clip& k) { return k.start + std::max<std::int64_t> (1, k.length); };
    if (side == 0)
    {
        int best = -1; // the latest-starting earlier clip that overlaps or crossfades
        for (int i = 0; i < (int) clips.size(); ++i)
        {
            const auto& o = clips[(std::size_t) i];
            if (i == index || o.track != c.track || ! before (i, index) || (best >= 0 && before (i, best)))
                continue;
            if (c.start < end (o) || flCrossfadePair (o, c))
                best = i;
        }
        return best;
    }
    // after: walk the later clips nearest first, up to the first that starts past this clip's end
    std::vector<int> later;
    for (int i = 0; i < (int) clips.size(); ++i)
        if (i != index && clips[(std::size_t) i].track == c.track && before (index, i))
            later.push_back (i);
    std::sort (later.begin(), later.end(), before);
    for (auto i : later)
    {
        const auto& o = clips[(std::size_t) i];
        if (end (c) < o.start)
            break;
        if (o.start < end (c) || flCrossfadePair (c, o))
            return i;
    }
    return -1;
}

// FL's Create crossfade on one pair, `a` the earlier clip: when they overlap by 0 < overlap <= the shorter clip, a's fade
// out and b's fade in become the overlap and stop being manual. Each clip keeps its other fade: a's fade in comes first
// (FUN_00cfaf80 mode 1), b's fade out comes first (mode 2). Pattern clips get no fades. True if it changed them.
inline bool createCrossfade (Clip& a, Clip& b) noexcept
{
    const auto overlap = a.start + std::max<std::int64_t> (1, a.length) - b.start;
    if (overlap <= 0 || overlap > std::min (a.length, b.length))
        return false;
    a.flFadeFlags &= ~2u; // FUN_01065840 (a, 0): a's fade out is no longer manual
    b.flFadeFlags &= ~1u; // FUN_01065830 (b, 0)
    if (a.kind == ClipKind::pattern || b.kind == ClipKind::pattern)
        return false;
    a.fadeOut = overlap;
    a.fadeIn = std::min (a.length, a.fadeIn);
    a.fadeOut = std::max<std::int64_t> (0, std::min (a.fadeOut, a.length - a.fadeIn));
    b.fadeIn = overlap;
    b.fadeOut = std::min (b.length, b.fadeOut);
    b.fadeIn = std::max<std::int64_t> (0, std::min (b.fadeIn, b.length - b.fadeOut));
    return true;
}

// FL's Create crossfade from a fade handle of `clicked` on `side`. A clicked clip that is selected: every selected clip
// crossfades with the clip after it, or else with the one before it. Not selected: only the clicked clip, with its
// neighbour on that side. Returns how many pairs changed.
inline int createCrossfades (std::vector<Clip>& clips, int clicked, int side, const std::vector<int>& selection)
{
    if (clicked < 0 || clicked >= (int) clips.size())
        return 0;
    const auto selected = std::find (selection.begin(), selection.end(), clicked) != selection.end();
    int changed = 0;
    const auto pair = [&] (int a, int b) {
        if (a >= 0 && b >= 0 && createCrossfade (clips[(std::size_t) a], clips[(std::size_t) b]))
            ++changed;
    };
    if (! selected)
    {
        const auto other = crossfadeNeighbour (clips, clicked, side);
        side == 0 ? pair (other, clicked) : pair (clicked, other);
        return changed;
    }
    for (auto i : selection)
    {
        if (i < 0 || i >= (int) clips.size())
            continue;
        if (const auto next = crossfadeNeighbour (clips, i, 1); next >= 0)
            pair (i, next);
        else
            pair (crossfadeNeighbour (clips, i, 0), i);
    }
    return changed;
}

// The fade handle menu's "Create crossfade" is enabled (FUN_00d023d0) when one of the menu's clips has a neighbour on
// that side that is as selected as it is (both selected or both not). Side -1 = no handle (FL's handle kind 0, the audio
// clip menu): FUN_00d02310 takes the neighbour before, else the one after.
inline bool canCreateCrossfade (const std::vector<Clip>& clips, const std::vector<int>& targets, int side, const std::vector<int>& selection)
{
    const auto isSelected = [&selection] (int i) { return std::find (selection.begin(), selection.end(), i) != selection.end(); };
    for (auto i : targets)
    {
        auto other = crossfadeNeighbour (clips, i, side < 0 ? 0 : side);
        if (side < 0 && other < 0)
            other = crossfadeNeighbour (clips, i, 1);
        if (other >= 0 && isSelected (other) == isSelected (i))
            return true;
    }
    return false;
}

// FL's audio clip menu › Fades › "Crossfade selected" (Tag 100, round 189o; [DECOMP] EEAUFadesComputeCrossfadesMenuClick
// FUN_012447a0 -> FUN_00d01bc0 action 0x15 = the same FUN_00d02580 as Create crossfade, with no handle). FL shows the
// item (FUN_01236e30 -> FUN_00cfc9b0, FUN_00840720 = Visible) only when the clicked clip is selected and one selected
// clip has a selected neighbour (before it, else after it). It then crossfades every selected clip with the clip after
// it, or else with the one before it (createCrossfades on a selected clip).
inline bool showCrossfadeSelected (const std::vector<Clip>& clips, int clicked, const std::vector<int>& selection)
{
    if (std::find (selection.begin(), selection.end(), clicked) == selection.end())
        return false;
    std::vector<int> targets;
    for (auto i : selection)
        if (i >= 0 && i < (int) clips.size())
            targets.push_back (i);
    return canCreateCrossfade (clips, targets, -1, selection);
}

// --- FL's Normalize on the gain handle (Tags 110 / 111, round 189n; [DECOMP] FUN_00d029b0) ----
// Normalize (and "Normalize selection individually"): each audio clip's gain = 1 / its peak, kept in the gain handle's
// range -96.1 ... +36 dB (FUN_01064dc0, FUN_010657d0). The peak is the largest |sample| of every channel of the file over
// the part the clip plays (FUN_01064c60 / FUN_00692980). A silent clip keeps its gain.
inline constexpr float normalizeGainMin = 1.5667499610572122e-05f, normalizeGainMax = 63.095699310302734f;
inline float normalizeGain (float peak, float current) noexcept
{
    return peak == 0.0f ? current : std::clamp (1.0f / peak, normalizeGainMin, normalizeGainMax);
}
// "Normalize selection as a group" (FUN_00d026f0): every selected audio clip of the Playlist gets the same gain, 1 / the
// largest of their peaks, with no range limit; all silent = no change.
inline float groupNormalizeGain (const std::vector<float>& peaks) noexcept
{
    float top = 0.0f;
    for (auto p : peaks)
        top = std::max (top, p);
    return top == 0.0f ? 0.0f : 1.0f / top;
}

// FL's audio clip menu › Automate › Panning / Volume (EEAUAutoPanMenuClick 0x1244900, Tag 0 / 1; round 190dx,
// /mnt/e/lynx-work/flpl190dx/FL-FACTS.md). FL makes an automation clip of a parameter of the clip's channel and puts it
// where the audio clip is: same track, same start, same length, a flat line at the parameter's current value from the
// clip's start to its end. FL's parameters are the channel's Misc › "Levels adjustment" pan offset / volume multiplier
// (channel parameters 0x10 / 0x11; round 190dz: LYNX's AutomationTargetKind::channelAdjust, the same parameters), so the
// main Pan / Volume knobs stay free. FL's Volume clip sets its range top to unity (half of the multiplier's 0…200 %
// travel) and the line at that top; LYNX's clips have no range of their own: the line sits at min(value, 0.5) (round
// 190ey, see automateAudioClip).
enum class AutomateWhat : std::uint8_t { panning, volume };

inline AutomationTarget automateTarget (const Song& song, int clip, AutomateWhat what)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return {};
    const auto& c = song.playlist[(std::size_t) clip];
    if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size())
        return {};
    AutomationTarget t;
    t.kind = AutomationTargetKind::channelAdjust;
    t.index = c.channel;
    t.param = what == AutomateWhat::panning ? leveladjust::pan : leveladjust::volume;
    return t;
}

// The new clip's name (live check 190ew PL-1 / PL-2): "<channel> - Volume multiplier", and " #2", " #3"... when clips of
// that target already exist (FL: "Amount Too - Bass - Volume multiplier #2")
inline std::string automateName (const Song& song, const AutomationTarget& target, const std::string& base)
{
    int same = 0;
    for (const auto& a : song.automations)
        if (a.target == target && ! a.isLive())
            ++same;
    return same == 0 ? base : base + " #" + std::to_string (same + 1);
}

// Adds that automation clip (named `name`); returns the new automation's index, −1 when `clip` is no audio clip.
// (Live check 190ew PL-1: FL makes the new clip the Playlist's current clip, the UI's part.) Live check 190ew PL-2 (shot
// PL-2-automate-volume-147pct-clip-editor.png): FL's Volume clip plays unity whatever the Levels VOL is above it (its
// range top set to unity, the line at that top: a 147 % multiplier is not kept); below unity the line is at the value.
// LYNX's clips have no range of their own, so the line sits at min(value, unity) = min(value, 0.5) of the 0…200 % travel.

inline int automateAudioClip (Song& song, int clip, AutomateWhat what, const std::string& name)
{
    const auto target = automateTarget (song, clip, what);
    if (target.kind == AutomationTargetKind::none)
        return -1;
    const auto audio = song.playlist[(std::size_t) clip];
    auto value = std::clamp (song.targetValue (target), 0.0f, 1.0f);
    if (what == AutomateWhat::volume)
        value = std::min (value, 0.5f); // (FL: unity at most, live check 190ew PL-2)
    const auto length = std::max<std::int64_t> (1, audio.length);

    Automation a;
    a.name = name;
    a.target = target;
    a.points = { { 0, value, 0.0f }, { length, value, 0.0f } };
    song.automations.push_back (std::move (a));

    Clip made;
    made.kind = ClipKind::automation;
    made.automation = (int) song.automations.size() - 1;
    made.track = audio.track;
    made.start = audio.start;
    made.length = length;
    song.playlist.push_back (made);
    return made.automation;
}

} // namespace lynx::clipops
