#pragma once

#include <map>
#include <memory>
#include <mutex>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Instruments.h"
#include "core/TimeStretch.h"

namespace lynx
{
// Decodes audio files into SampleData, cached by path (one cache for the whole process).
// Any thread except the audio thread.
class SampleLibrary
{
public:
    SampleLibrary();

    std::shared_ptr<const SampleData> load (const juce::String& path);
    // The sample when something already holds it loaded (nullptr otherwise): never reads the file (round 149: the
    // stream prefetcher looked its clips up with load(), which decoded again every audio clip whose channel plays a
    // time-stretched copy, seconds on every edit of a song with many of them)
    std::shared_ptr<const SampleData> findLoaded (const juce::String& path);
    // The file changed on disk (the audio editor saved over it): the next load() reads it again.
    void forget (const juce::String& path);

    // The sample time-stretched without changing pitch (core/TimeStretch), cached by (path, ratio, method).
    // `map` (round 175a, warp markers): stretched along that warp map instead of at one ratio (its own cache entries).
    std::shared_ptr<const SampleData> loadStretched (const juce::String& path, double ratio, StretchMethod method = StretchMethod::general,
                                                    double semitones = 0.0, // semitones: Beats only (core/TimeStretch)
                                                    const StretchMap& map = {}, const StretchOptions& options = {});

    // A channel's precomputed span (its region after its precomputed effects and Trim, core/SamplePrecompute.h) stretched
    // by `ratio` (core/TimeStretch timeStretchSpan), cached in memory by (path, variantKey, ratio, method): FL stretches
    // that span to TIME, not the whole file (round 180d). Dropped with the file's other unused lengths.
    std::shared_ptr<const SampleData> loadStretchedSpan (const juce::String& path, const std::string& variantKey,
                                                        const std::shared_ptr<const SampleData>& span, double ratio,
                                                        StretchMethod method = StretchMethod::general, double semitones = 0.0,
                                                        const StretchOptions& options = {});

    // Above this, audio goes to a memory-mapped disk cache instead of staying entirely
    // in RAM ("disk streaming"). The cache is reused across sessions.
    static constexpr double streamAboveSeconds = 30.0;
    static juce::File cacheFolder();
    // The decoded copies (*.pcm) above `limitBytes`: the least recently used are deleted down to 3/4 of it (round 167e;
    // done once per session in the background). Returns the bytes left.
    static juce::int64 prunePcmCache (juce::int64 limitBytes);

    // Filter for FileChooser ("*.wav;*.flac;...")
    juce::String wildcard() const;

    // The audio files LYNX loads as samples (core/SampleFiles.h: wav, aif(f), flac, ogg, mp3 and FL's WavPack .wv).
    // Every drop target (Channel Rack, Playlist, Audio editor...) asks this, so they all take what the Browser lists.
    static bool isSampleFile (const juce::File& file);

    // The file's first `seconds` mixed to mono, read straight from the file: nothing is cached or kept (round 190as:
    // the Browser's "Find similar samples" analyses whole folders with it). False when the file can't be read.
    bool readMono (const juce::File& file, double seconds, std::vector<float>& mono, double& sampleRate);

private:
    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::File& file);
    std::shared_ptr<const SampleData> loadStreamed (const juce::File& file, juce::AudioFormatReader& reader);
    std::shared_ptr<const SampleData> decode (const juce::String& path);

    juce::AudioFormatManager formats;
};
// Brings in from disk the range [from, to) (seconds) of a mapped sample. Any thread except the audio thread.
void prefetchSample (const SampleData& data, double fromSeconds, double toSeconds);
} // namespace lynx
