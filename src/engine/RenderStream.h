#pragma once

// The end of an offline render after the audio graph, for one file (round 190gj, out of AudioEngine::renderOffline):
// overs clamped (unless kept), the peak, the dither, the PDC trim, the writer / capture, and the tail's end (half a
// second under −90 dB once the music is over). renderOffline runs one on the graph's output; the single-pass stems
// (engine/Exporter.h, StemPass) run one per file on the blocks that file's own render would have had, so every file is
// made by this same code.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Dither.h"
#include "core/SoftClip.h"

namespace lynx
{
class RenderStream
{
public:
    struct Setup
    {
        std::int64_t trim = 0;         // the start's samples left out (the delay compensation's latency)
        std::int64_t musicSamples = 0; // where the music ends (trim included): the tail starts there
        std::int64_t totalSamples = 0; // the most the render lasts (trim, music and the longest tail)
        double sampleRate = 44100.0;
        bool keepOvers = false;        // false: samples above 0 dBFS are clamped to ±1 first
        int ditherBits = 0;            // > 0: TPDF dither for this bit depth
        bool ditherShaping = true;
        juce::AudioFormatWriter* output = nullptr;
        std::function<void (const float*, const float*, int)> capture; // the kept samples, as `output` gets them
    };

    explicit RenderStream (Setup s) : setup (std::move (s))
    {
        if (setup.ditherBits > 0)
            dither.emplace (setup.ditherBits, setup.ditherShaping);
    }

    // The size of the block at render sample `done` (blocks of `blockSize` until the end), 0 once the stream is over.
    int blockAt (std::int64_t done, int blockSize) const noexcept
    {
        return ended || done >= setup.totalSamples ? 0 : (int) std::min<std::int64_t> (blockSize, setup.totalSamples - done);
    }

    // The render's block [done, done + n) in `view` (two channels, changed in place).
    void block (juce::AudioBuffer<float>& view, std::int64_t done, int n)
    {
        if (! setup.keepOvers) // float WAV and the buffered export (normalize) keep what goes above 0 dBFS, like FL
            for (int ch = 0; ch < view.getNumChannels(); ++ch)
                clipOvers (view.getWritePointer (ch), n);

        const auto blockPeak = view.getMagnitude (0, n);
        peak = std::max (peak, blockPeak);

        if (dither)
            for (int ch = 0; ch < view.getNumChannels(); ++ch)
                dither->process (view.getWritePointer (ch), n, ch);

        // PDC: the start is the latency's silence; the song's end arrives `trim` samples later
        if ((setup.output != nullptr || setup.capture) && done + n > setup.trim)
        {
            const auto skip = (int) std::max<std::int64_t> (0, setup.trim - done);
            const auto beforeWrite = juce::Time::getMillisecondCounterHiRes();
            if (setup.output != nullptr)
                setup.output->writeFromAudioSampleBuffer (view, skip, n - skip);
            if (setup.capture)
                setup.capture (view.getReadPointer (0, skip), view.getReadPointer (1, skip), n - skip);
            writeMs += juce::Time::getMillisecondCounterHiRes() - beforeWrite;
        }

        // in the tail: half a second of silence ends it
        if (done + n > setup.musicSamples)
        {
            quietSamples = blockPeak < silence ? quietSamples + n : 0;
            if (quietSamples > (std::int64_t) (setup.sampleRate * 0.5))
                endedInTail = ended = true;
        }
        if (done + n >= setup.totalSamples)
            ended = true;
    }

    bool hasEnded() const noexcept { return ended; }
    bool endedBySilence() const noexcept { return endedInTail; } // (not by reaching the total)
    float getPeak() const noexcept { return peak; }
    double getWriteMs() const noexcept { return writeMs; }
    const Setup& getSetup() const noexcept { return setup; }

    static constexpr float silence = 3.0e-5f; // ~ -90 dB

private:
    Setup setup;
    std::optional<Dither> dither;
    float peak = 0.0f;
    std::int64_t quietSamples = 0;
    bool ended = false, endedInTail = false;
    double writeMs = 0.0;
};
} // namespace lynx
