#pragma once

// The end of an offline render after the audio graph, for one file (round 190gj, out of AudioEngine::renderOffline):
// overs clamped (unless kept), the peak, the dither, the PDC trim, the writer / capture, and the tail's end (half a
// second under −90 dB once the music is over). renderOffline runs one on the graph's output; the single-pass stems
// (engine/Exporter.h, StemPass) run one per file on the blocks that file's own render would have had, so every file is
// made by this same code.

#include <algorithm>
#include <cstdint>
#include <cstring>
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
        bool exact = false;            // round 190gh: the total is the length; the tail's silence does not end it
        bool keepOvers = false;        // false: samples above 0 dBFS are clamped to ±1 first
        int ditherBits = 0;            // > 0: TPDF dither for this bit depth
        bool ditherShaping = true;
        juce::AudioFormatWriter* output = nullptr;
        std::function<void (const float*, const float*, int)> capture; // the kept samples, as `output` gets them
        // Round 190gj: while the length is still to be found (an exact length known only once the mix has ended:
        // setTotal), the last `holdBack` samples are kept back from `output` / `capture`
        std::int64_t holdBack = 0;
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

    // The render's block [done, done + n) in `view` (two channels, changed in place). A block that goes past the total
    // (one render making several files: this file's own render would have ended inside it) is used up to the total.
    void block (juce::AudioBuffer<float>& view, std::int64_t done, int n)
    {
        n = (int) std::min<std::int64_t> (n, setup.totalSamples - done);
        if (n <= 0)
            return;
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
            if (setup.holdBack > 0 && ! totalKnown)
            {
                if (held.getNumSamples() < heldCount + n - skip)
                    held.setSize (2, (int) std::max<std::int64_t> (heldCount + n - skip, setup.holdBack + 2048), true);
                for (int ch = 0; ch < 2; ++ch)
                    held.copyFrom (ch, (int) heldCount, view, std::min (ch, view.getNumChannels() - 1), skip, n - skip);
                heldCount += n - skip;
                // what is surely before the end goes out now
                if (const auto out = (int) (heldCount - setup.holdBack); out > 0)
                {
                    write (held, 0, out);
                    for (int ch = 0; ch < 2; ++ch)
                        std::memmove (held.getWritePointer (ch), held.getReadPointer (ch, out), sizeof (float) * (std::size_t) (heldCount - out));
                    heldCount -= out;
                }
            }
            else
                write (view, skip, n - skip);
        }
        processedEnd = done + n;

        // in the tail: half a second of silence ends it
        if (done + n > setup.musicSamples && ! setup.exact)
        {
            quietSamples = blockPeak < silence ? quietSamples + n : 0;
            if (quietSamples > (std::int64_t) (setup.sampleRate * 0.5))
                endedInTail = ended = true;
        }
        if (done + n >= setup.totalSamples)
            ended = true;
    }

    // Round 190gj: the length is found (the end at render sample `total`, after the trim): what was kept back goes out
    // up to there. Before the first block, or with nothing kept back, it only sets the total.
    void setTotal (std::int64_t total)
    {
        setup.totalSamples = total;
        totalKnown = true;
        if (heldCount > 0)
        {
            // held = the samples just before processedEnd
            const auto heldFrom = processedEnd - heldCount;
            const auto keep = (int) std::clamp<std::int64_t> (total - heldFrom, 0, heldCount);
            if (keep > 0)
                write (held, 0, keep);
            heldCount = 0;
        }
        if (processedEnd >= total)
            ended = true;
    }

    // the samples after the trim that the stream took in (the file's length once it has ended)
    std::int64_t keptFrames() const noexcept { return std::max<std::int64_t> (0, std::min (processedEnd, setup.totalSamples) - setup.trim); }

    bool hasEnded() const noexcept { return ended; }
    bool endedBySilence() const noexcept { return endedInTail; } // (not by reaching the total)
    float getPeak() const noexcept { return peak; }
    double getWriteMs() const noexcept { return writeMs; }
    const Setup& getSetup() const noexcept { return setup; }

    static constexpr float silence = 3.0e-5f; // ~ -90 dB

private:
    void write (const juce::AudioBuffer<float>& from, int start, int count)
    {
        if (count <= 0)
            return;
        const auto beforeWrite = juce::Time::getMillisecondCounterHiRes();
        if (setup.output != nullptr)
            setup.output->writeFromAudioSampleBuffer (from, start, count);
        if (setup.capture)
            setup.capture (from.getReadPointer (0, start), from.getReadPointer (1, start), count);
        writeMs += juce::Time::getMillisecondCounterHiRes() - beforeWrite;
    }

    Setup setup;
    juce::AudioBuffer<float> held { 2, 0 };
    std::int64_t heldCount = 0, processedEnd = 0;
    bool totalKnown = false;
    std::optional<Dither> dither;
    float peak = 0.0f;
    std::int64_t quietSamples = 0;
    bool ended = false, endedInTail = false;
    double writeMs = 0.0;
};
} // namespace lynx
