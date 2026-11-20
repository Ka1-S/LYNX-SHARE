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

// --- round 190jr (BUILD-LIST 14, B21): the Extract stems options ----------------------------------------------------
// FL's "Extract stems from sample" form (TDEMUCSFORM, rea190bc/dfm): a tick per stem, Limit CPU usage, Safe mode, When
// done (mute the source clip / track / nothing), Group playlist tracks, Assign to new mixer tracks; plus the model
// (LYNX-own, BandLab's Splitter has 4 and 6 stems): Standard = HT-Demucs (4 stems), 6 stems = HT-Demucs 6s (adds
// guitar and piano), Fine-tuned = HT-Demucs ft (one fine-tuned model per stem: the best separation, one pass per stem).
// Every graph takes the same 7.8 s stereo segment ("mix") and returns 4 or 6 stereo stems ("stems").
inline constexpr int maxStems = 6;
inline constexpr std::array<const char*, maxStems> allStemNames { "drums", "bass", "instruments", "vocals", "guitar", "piano" };

enum class Model : std::uint8_t { standard, sixStems, fineTuned };
enum class WhenDone : std::uint8_t { muteClip, muteTrack, nothing }; // FL's EndActionSelect, in its order

struct Options
{
    std::array<bool, maxStems> stems { true, true, true, true, true, true }; // guitar / piano only with sixStems
    Model model = Model::standard;
    bool limitCpu = false;      // FL: "Limit separation to fewer CPU cores"
    bool safeMode = false;      // FL: "Use this for troubleshooting" (LYNX: no graph optimisation, one thread)
    WhenDone whenDone = WhenDone::muteClip;
    bool groupTracks = true;    // FL: "Group the playlist tracks for the separated stems" (under the source's track)
    bool newInserts = true;     // FL: "Assign each stem to a different mixer track"
    bool operator== (const Options&) const = default;
};

// One downloadable graph: pinned revision, size and hash (what the user agreed to is what runs).
struct ModelFile
{
    const char* file;
    const char* url;
    const char* sha256;
    std::int64_t bytes;
    int outputs; // stems the graph returns, in allStemNames order
};
inline const ModelFile& modelFile (Model model, int stem = 0) noexcept
{
    static const ModelFile standard { "htdemucs_fp16weights.onnx",
                                      "https://huggingface.co/adowu/htdemucs-onnx/resolve/e6b688a9a9f977e2efaba79d0f18b9b0cfc606ea/htdemucs_fp16weights.onnx",
                                      "d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a", 165612636, 4 };
    static const ModelFile six { "htdemucs_6s_fp16weights.onnx",
                                 "https://huggingface.co/StemSplitio/htdemucs-6s-onnx/resolve/49df9b6989cf2150840ea65b0bef77a2e471b678/htdemucs_6s_fp16weights.onnx",
                                 "7ce55792e2231c93fbf92de95f5fd5b3a5e6c89f7db690dfd693e8f1dce56869", 136428532, 6 };
    static const std::array<ModelFile, numStems> fine { {
        { "htdemucs_ft_drums_fp16weights.onnx",
          "https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/8616370ed541bc183dbe15fb0d54d5f49918f47e/htdemucs_ft_drums_fp16weights.onnx",
          "047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd", 165612636, 4 },
        { "htdemucs_ft_bass_fp16weights.onnx",
          "https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/8616370ed541bc183dbe15fb0d54d5f49918f47e/htdemucs_ft_bass_fp16weights.onnx",
          "b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961", 165612636, 4 },
        { "htdemucs_ft_other_fp16weights.onnx",
          "https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/8616370ed541bc183dbe15fb0d54d5f49918f47e/htdemucs_ft_other_fp16weights.onnx",
          "b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71", 165612636, 4 },
        { "htdemucs_ft_vocals_fp16weights.onnx",
          "https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/8616370ed541bc183dbe15fb0d54d5f49918f47e/htdemucs_ft_vocals_fp16weights.onnx",
          "0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce", 165612636, 4 } } };
    switch (model)
    {
        case Model::sixStems: return six;
        case Model::fineTuned: return fine[(std::size_t) std::clamp (stem, 0, numStems - 1)];
        case Model::standard: break;
    }
    return standard;
}

// The stems a model can give (4 or 6) and the ones the options ask for, in allStemNames order.
inline int stemsOf (Model model) noexcept { return model == Model::sixStems ? maxStems : numStems; }
inline std::vector<int> wantedStems (const Options& o)
{
    std::vector<int> out;
    for (int s = 0; s < stemsOf (o.model); ++s)
        if (o.stems[(std::size_t) s])
            out.push_back (s);
    return out;
}

// One separation pass of a run: the graph and the stems taken from it (the fine-tuned models: one pass per stem, each
// keeping only its own stem; the others: one pass giving every wanted stem).
struct Pass
{
    const ModelFile* model = nullptr;
    std::vector<int> take; // stem indices (allStemNames) taken from this graph's outputs
};
inline std::vector<Pass> passesFor (const Options& o)
{
    std::vector<Pass> out;
    const auto wanted = wantedStems (o);
    if (wanted.empty())
        return out;
    if (o.model == Model::fineTuned)
        for (const auto s : wanted)
            out.push_back ({ &modelFile (Model::fineTuned, s), { s } });
    else
        out.push_back ({ &modelFile (o.model), wanted });
    return out;
}

// The files a run needs and their total size (the download question).
inline std::int64_t downloadBytes (const std::vector<Pass>& passes, const std::function<bool (const ModelFile&)>& installed)
{
    std::int64_t total = 0;
    std::vector<const ModelFile*> seen;
    for (const auto& p : passes)
        if (std::find (seen.begin(), seen.end(), p.model) == seen.end())
        {
            seen.push_back (p.model);
            if (! installed || ! installed (*p.model))
                total += p.model->bytes;
        }
    return total;
}

// Options as text for the settings file ("stems=111100 model=0 cpu=0 safe=0 done=0 group=1 inserts=1") and back
// (unknown or missing keys keep their defaults).
inline std::string toText (const Options& o)
{
    std::string stems;
    for (const auto b : o.stems)
        stems += b ? '1' : '0';
    return "stems=" + stems + " model=" + std::to_string ((int) o.model) + " cpu=" + (o.limitCpu ? "1" : "0") + " safe=" + (o.safeMode ? "1" : "0")
           + " done=" + std::to_string ((int) o.whenDone) + " group=" + (o.groupTracks ? "1" : "0") + " inserts=" + (o.newInserts ? "1" : "0");
}
inline Options optionsFromText (const std::string& text)
{
    Options o;
    std::size_t at = 0;
    while (at < text.size())
    {
        const auto end = std::min (text.find (' ', at), text.size());
        const auto item = text.substr (at, end - at);
        at = end + 1;
        const auto eq = item.find ('=');
        if (eq == std::string::npos || eq + 1 >= item.size())
            continue;
        const auto key = item.substr (0, eq), value = item.substr (eq + 1);
        const auto on = value == "1";
        if (key == "stems")
        {
            for (std::size_t i = 0; i < o.stems.size() && i < value.size(); ++i)
                o.stems[i] = value[i] == '1';
        }
        else if (key == "model" && value.size() == 1 && value[0] >= '0' && value[0] <= '2')
            o.model = (Model) (value[0] - '0');
        else if (key == "done" && value.size() == 1 && value[0] >= '0' && value[0] <= '2')
            o.whenDone = (WhenDone) (value[0] - '0');
        else if (key == "cpu")
            o.limitCpu = on;
        else if (key == "safe")
            o.safeMode = on;
        else if (key == "group")
            o.groupTracks = on;
        else if (key == "inserts")
            o.newInserts = on;
    }
    return o;
}

// ONNX Runtime's worker threads (round 167x): the physical cores minus one, and never more than the logical CPUs minus
// two, so the audio callback and the UI always keep a core while a playing project is being separated.
inline int separationThreads (int physicalCores, int logicalCpus) noexcept
{
    return std::clamp (physicalCores - 1, 1, std::max (1, logicalCpus - 2));
}
// FL's "Limit CPU usage" (round 190jr): half of that, at least one; Safe mode runs on one thread
inline int separationThreads (int physicalCores, int logicalCpus, const Options& o) noexcept
{
    if (o.safeMode)
        return 1;
    const auto all = separationThreads (physicalCores, logicalCpus);
    return o.limitCpu ? std::max (1, all / 2) : all;
}

inline std::int64_t numSegments (std::int64_t frames) noexcept
{
    const auto stride = segmentFrames - overlapFrames;
    return frames <= segmentFrames ? 1 : 1 + (frames - segmentFrames + stride - 1) / stride;
}

// `model` turns one segment, in = [left N, right N], into out = [stem 0 left N, stem 0 right N, stem 1 left N, ...] and
// returns false to stop (an error or Cancel). `progress` gets 0..1 after each segment. False when the model stopped.
// separateInto: a graph with `outputs` stems into `stems` (stems[s][channel], s < outputs).
template <typename StemList, typename Graph>
bool separateInto (const std::vector<float>& left, const std::vector<float>& right, int outputs, Graph&& model, const std::function<void (double)>& progress,
                   StemList& stems)
{
    const auto frames = (std::int64_t) left.size();
    const auto n = segmentFrames;
    std::vector<float> in ((std::size_t) (2 * n)), out ((std::size_t) (outputs * 2 * n)), weight ((std::size_t) frames, 0.0f);
    for (int s = 0; s < outputs; ++s)
        for (auto& channel : stems[(std::size_t) s])
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
            for (int s = 0; s < outputs; ++s)
                for (int c = 0; c < 2; ++c)
                    stems[(std::size_t) s][(std::size_t) c][(std::size_t) (start + i)] += w * out[(std::size_t) ((s * 2 + c) * n + i)];
        }
        if (progress)
            progress ((double) (segment + 1) / (double) segments);
    }

    for (int s = 0; s < outputs; ++s)
        for (auto& channel : stems[(std::size_t) s])
            for (std::int64_t i = 0; i < frames; ++i)
                channel[(std::size_t) i] /= weight[(std::size_t) i];
    return true;
}

template <typename Graph>
bool separate (const std::vector<float>& left, const std::vector<float>& right, Graph&& model, const std::function<void (double)>& progress, Stems& stems)
{
    return separateInto (left, right, numStems, std::forward<Graph> (model), progress, stems);
}

// One new audio channel and clip per stem, on new tracks right under the source clip and aligned with it: the copied
// channel and clip keep its speed, pitch, fades and offset. Run inside AudioEngine::editSong (one undo step). False,
// with the song untouched, when `clipIndex` is no longer an audio clip of `sourcePath`.
struct StemFile
{
    std::string name; // "drums", "vocals"... (the channel is "<source> - <name>")
    std::string path;
};
struct Placement
{
    bool newInserts = true;               // each stem on an insert of its own (else the source channel's insert)
    bool groupTracks = false;             // the stems' tracks grouped under the source clip's track
    WhenDone whenDone = WhenDone::nothing;
};

// Round 190jr: the stems that were asked for, placed as the options say. False, with the song untouched, when
// `clipIndex` is no longer an audio clip of `sourcePath` or there is no stem.
inline bool addStemClips (Song& song, int clipIndex, const std::string& sourcePath, const std::vector<StemFile>& stems, const Placement& place)
{
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size() || stems.empty())
        return false;
    const auto source = song.playlist[(std::size_t) clipIndex];
    if (source.kind != ClipKind::audio || source.channel < 0 || source.channel >= (int) song.channels.size()
        || song.channels[(std::size_t) source.channel].samplePath != sourcePath)
        return false;

    const auto sourceChannel = song.channels[(std::size_t) source.channel];
    for (std::size_t s = 0; s < stems.size(); ++s)
    {
        const auto track = source.track + 1 + (int) s;
        noteedit::insertTrack (song, track);
        if (place.groupTracks && track < (int) song.playlistTracks.size())
            song.playlistTracks[(std::size_t) track].grouped = true;

        auto channel = sourceChannel;
        channel.name = sourceChannel.name + " - " + stems[s].name;
        channel.samplePath = stems[s].path;
        if (place.newInserts)
            channel.mixerTrack = song.claimFreeInsert();
        song.channels.push_back (channel);

        auto clip = source;
        clip.channel = (int) song.channels.size() - 1;
        clip.track = track;
        clip.uid = 0; // (a new clip: uids::normalize gives it its own)
        song.playlist.push_back (clip);
    }
    if (place.whenDone == WhenDone::muteClip)
        song.playlist[(std::size_t) clipIndex].muted = true;
    else if (place.whenDone == WhenDone::muteTrack && source.track >= 0 && source.track < (int) song.playlistTracks.size())
        song.playlistTracks[(std::size_t) source.track].muted = true;
    return true;
}

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
