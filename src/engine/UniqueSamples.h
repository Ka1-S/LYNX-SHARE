#pragma once

// FL's "Make unique as sample" (Playlist › Edit and the audio clip menu; round 189g, off the message thread since 190ri): what
// each audio clip plays is written as "<sample> - Part_<n>.wav" (an earlier " - Part_" suffix dropped first; FL 2026: one file
// per clip, also for clips that play the same), then the clip plays a new audio clip channel with that file. The plan is made
// and applied on the message thread; the render works on its own copy of the song. A cancelled render leaves nothing behind.

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/ClipEnvelope.h"
#include "core/ClipOps.h"
#include "core/QuickRender.h"
#include "core/RackRenderer.h"
#include "engine/SampleLibrary.h"
#include "platform/Paths.h"
#include "engine/SafeWrite.h" // (rollup 0.1.32 step 6)

namespace lynx::uniquesamples
{
struct Part
{
    int clip = -1;
    Clip was;          // the clip as planned (apply skips a clip that is another one by then)
    juce::File source; // its channel's sample
    juce::File file;   // where it goes (none: the next free "<sample> - Part_<n>.wav" in `folder`)
    juce::File folder;
    bool written = false;
};

struct Plan
{
    Song song; // what the render reads
    double rate = 44100.0;
    std::vector<Part> parts;
};

// The project's data folder for this clip's file, else the sample's own folder. Only folders that exist on this computer: an
// imported project's "C:\Users\...\" is no place on Linux or macOS (it became a folder of that very name in the working
// directory), nor is "/Users/..." on Windows.
inline juce::File folderFor (const Song& song, const juce::File& source)
{
    if (const auto data = platform::localFile (song.dataFolder); data.isDirectory())
        if (const auto samples = data.getChildFile ("Samples"); samples.isDirectory() || samples.createDirectory().wasOk())
            return samples;
    return source.getParentDirectory();
}

inline juce::String baseName (const juce::File& source)
{
    auto base = source.getFileNameWithoutExtension();
    if (const auto at = base.lastIndexOf (" - Part_"); at > 0)
        base = base.substring (0, at);
    return base;
}

inline juce::File nextFree (const juce::File& folder, const juce::String& base, int& n)
{
    for (;; ++n)
        if (const auto out = folder.getChildFile (base + " - Part_" + juce::String (n) + ".wav"); ! out.exists())
            return out;
}

inline Plan makePlan (const Song& song, double rate, const std::vector<int>& clips)
{
    Plan plan;
    plan.rate = rate;
    for (const auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size())
            continue;
        const auto source = platform::localFile (song.channels[(std::size_t) c.channel].samplePath);
        if (source == juce::File())
            continue;
        Part p;
        p.clip = i;
        p.was = c;
        p.source = source;
        p.folder = folderFor (song, source);
        plan.parts.push_back (std::move (p));
    }
    if (! plan.parts.empty())
        plan.song = song;
    return plan;
}

inline bool writeWav (const ClipAudio& audio, const juce::File& file)
{
    const auto frames = (int) audio.left.size();
    const auto stereo = audio.left != audio.right;
    juce::AudioBuffer<float> buffer (stereo ? 2 : 1, frames);
    buffer.copyFrom (0, 0, audio.left.data(), frames);
    if (stereo)
        buffer.copyFrom (1, 0, audio.right.data(), frames);
    // (rollup 0.1.32 step 6: round 190rm's checked writer)
    juce::String why;
    return safewrite::writeWithHandedStream (file, [&] (std::unique_ptr<juce::OutputStream>& stream) {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), audio.sampleRate, (unsigned int) buffer.getNumChannels(), 32, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release(); // (the writer owns it now)
        return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }, why);
}

// Any thread but the audio thread. `progress (0..1)` after each clip; false = cancelled: every file made is deleted and nothing
// is left to apply. A part that can't be rendered or written is left out.
inline bool render (Plan& plan, const std::function<bool (double)>& progress)
{
    SampleLibrary library; // (its cache is the process's: what the project already decoded is found)
    RackState::Factories factories;
    factories.samples = [&library] (const std::string& path) { return library.load (juce::String::fromUTF8 (path.c_str())); };
    factories.stretchedSamples = [&library] (const std::string& path, double ratio, StretchMethod method, double semitones, const StretchMap& map,
                                             const StretchOptions& options) {
        return library.loadStretched (juce::String::fromUTF8 (path.c_str()), ratio, method, semitones, map, options);
    };
    factories.stretchedSpans = [&library] (const std::string& path, const std::string& variant, const std::shared_ptr<const SampleData>& span, double ratio,
                                           StretchMethod method, double semitones, const StretchOptions& options) {
        return library.loadStretchedSpan (juce::String::fromUTF8 (path.c_str()), variant, span, ratio, method, semitones, options);
    };

    std::vector<juce::File> made;
    std::map<juce::String, int> next;                                                                   // folder + base -> the next number to try
    std::map<std::tuple<int, std::int64_t, float, std::int64_t, float, double, bool>, juce::File> same; // a clip that plays what an earlier one plays
    for (std::size_t i = 0; i < plan.parts.size(); ++i)
    {
        auto& p = plan.parts[i];
        if (! progress ((double) i / (double) plan.parts.size()))
        {
            for (const auto& f : made)
                f.deleteFile();
            for (auto& part : plan.parts)
                part.written = false;
            return false;
        }
        if (p.file == juce::File())
        {
            const auto base = baseName (p.source);
            auto& n = next.try_emplace (p.folder.getFullPathName() + "\n" + base, 1).first->second;
            p.file = nextFree (p.folder, base, n);
        }
        const auto& c = p.was;
        const auto key = std::make_tuple (c.channel, c.offset, c.offsetFraction, c.length, c.pitch, c.stretch, c.reversed);
        const auto existed = p.file.exists(); // (a file the user chose to replace)
        if (const auto it = same.find (key); it != same.end())
            p.written = it->second.copyFileTo (p.file);
        else
        {
            const auto audio = lynx::renderClipAudio (plan.song, (std::size_t) p.clip, plan.rate, factories);
            p.written = ! audio.left.empty() && (! existed || p.file.deleteFile()) && writeWav (audio, p.file);
            if (p.written)
                same[key] = p.file;
        }
        if (p.written && ! existed)
            made.push_back (p.file);
    }
    progress (1.0);
    return true;
}

// One edit of the song: call it inside one editSong. Returns how many clips changed.
inline int apply (Song& song, const Plan& plan)
{
    int changed = 0;
    for (const auto& p : plan.parts)
    {
        if (! p.written || p.clip >= (int) song.playlist.size())
            continue;
        auto& clip = song.playlist[(std::size_t) p.clip];
        if (clip.kind != ClipKind::audio || clip.channel != p.was.channel || clip.start != p.was.start || clip.track != p.was.track
            || clip.channel < 0 || clip.channel >= (int) song.channels.size())
            continue;
        auto copy = song.channels[(std::size_t) clip.channel];
        copy.samplePath = p.file.getFullPathName().toStdString();
        copy.name = quickrender::uniqueChannelName (song, p.file.getFileNameWithoutExtension().toStdString());
        song.channels.push_back (std::move (copy));
        clipenv::shift (song, clip, -((double) clip.offset + (double) clip.offsetFraction)); // (round 190gi: its envelopes stay on their audio)
        clipops::resetRenderedClip (clip, (int) song.channels.size() - 1);
        ++changed;
    }
    return changed;
}
} // namespace lynx::uniquesamples
