#pragma once

// "Extract stems from sample" (issue #27), the part without JUCE: HT-Demucs over a whole file by overlapping
// segments, and the one undo step that puts the stems on new Playlist tracks. The model itself runs in ui/StemExtract.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/NoteEdit.h"
#include "core/Song.h"

namespace lynx::stemsplit
{
inline constexpr double sampleRate = 44100.0;
inline constexpr std::int64_t segmentFrames = 343980; // the exported graph's fixed input (7.8 s)
inline constexpr std::int64_t overlapFrames = segmentFrames / 4;
inline constexpr int numStems = 4;
inline constexpr std::array<const char*, numStems> stemNames { "drums", "bass", "instruments", "vocals" }; // the graph's order ("other" = instruments)

using Stems = std::array<std::array<std::vector<float>, 2>, numStems>;

// ONNX Runtime's worker threads (round 167x): the physical cores minus one, and never more than the logical CPUs minus
// two, so the audio callback and the UI always keep a core while a playing project is being separated.
inline int separationThreads (int physicalCores, int logicalCpus) noexcept
{
    return std::clamp (physicalCores - 1, 1, std::max (1, logicalCpus - 2));
}

inline std::int64_t numSegments (std::int64_t frames) noexcept
{
    const auto stride = segmentFrames - overlapFrames;
    return frames <= segmentFrames ? 1 : 1 + (frames - segmentFrames + stride - 1) / stride;
}

// `model` turns one segment, in = [left N, right N], into out = [stem 0 left N, stem 0 right N, stem 1 left N, ...] and
// returns false to stop (an error or Cancel). `progress` gets 0..1 after each segment. False when the model stopped.
template <typename Model>
bool separate (const std::vector<float>& left, const std::vector<float>& right, Model&& model, const std::function<void (double)>& progress, Stems& stems)
{
    const auto frames = (std::int64_t) left.size();
    const auto n = segmentFrames;
    std::vector<float> in ((std::size_t) (2 * n)), out ((std::size_t) (numStems * 2 * n)), weight ((std::size_t) frames, 0.0f);
    for (auto& stem : stems)
        for (auto& channel : stem)
            channel.assign ((std::size_t) frames, 0.0f);

    const auto segments = numSegments (frames);
    for (std::int64_t segment = 0; segment < segments; ++segment)
    {
        const auto start = segment * (n - overlapFrames);
        const auto count = std::min (n, frames - start);
        std::fill (in.begin(), in.end(), 0.0f); // (the last segment is padded with silence)
        std::copy_n (left.begin() + start, count, in.begin());
        std::copy_n (right.begin() + start, count, in.begin() + n);
        if (! model (in.data(), out.data()))
            return false;

        for (std::int64_t i = 0; i < count; ++i)
        {
            // a fade over the overlap that is never 0, so the first and last frames of the file keep their weight
            const auto w = (float) std::min ((double) std::min (i, n - 1 - i) + 1.0, (double) overlapFrames + 1.0) / (float) (overlapFrames + 1);
            weight[(std::size_t) (start + i)] += w;
            for (int s = 0; s < numStems; ++s)
                for (int c = 0; c < 2; ++c)
                    stems[(std::size_t) s][(std::size_t) c][(std::size_t) (start + i)] += w * out[(std::size_t) ((s * 2 + c) * n + i)];
        }
        if (progress)
            progress ((double) (segment + 1) / (double) segments);
    }

    for (auto& stem : stems)
        for (auto& channel : stem)
            for (std::int64_t i = 0; i < frames; ++i)
                channel[(std::size_t) i] /= weight[(std::size_t) i];
    return true;
}

// One new audio channel and clip per stem, on new tracks right under the source clip and aligned with it: the copied
// channel and clip keep its speed, pitch, fades and offset. Run inside AudioEngine::editSong (one undo step). False,
// with the song untouched, when `clipIndex` is no longer an audio clip of `sourcePath`.
inline bool addStemClips (Song& song, int clipIndex, const std::string& sourcePath, const std::array<std::string, numStems>& paths)
{
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return false;
    const auto source = song.playlist[(std::size_t) clipIndex];
    if (source.kind != ClipKind::audio || source.channel < 0 || source.channel >= (int) song.channels.size()
        || song.channels[(std::size_t) source.channel].samplePath != sourcePath)
        return false;

    const auto sourceChannel = song.channels[(std::size_t) source.channel];
    for (int s = 0; s < numStems; ++s)
    {
        const auto track = source.track + 1 + s;
        noteedit::insertTrack (song, track);

        auto channel = sourceChannel;
        channel.name = sourceChannel.name + " - " + stemNames[(std::size_t) s];
        channel.samplePath = paths[(std::size_t) s];
        channel.mixerTrack = song.claimFreeInsert();
        song.channels.push_back (channel);

        auto clip = source;
        clip.channel = (int) song.channels.size() - 1;
        clip.track = track;
        song.playlist.push_back (clip);
    }
    return true;
}
} // namespace lynx::stemsplit
