#pragma once

// FL's wrapper option "Use fixed size buffers" (REA_rodada118 Part B): the plugin is always called with the same
// block length, whatever the host's chunk sizes are (LYNX splits its blocks at every event, so without this a plugin
// sees variable, sometimes tiny, blocks). Input is collected until a whole block is there; the processed block is
// played back while the next one fills, so the output is the input delayed by exactly `block` samples — reported to
// PDC as extra latency. JUCE-free; real-time safe after construction (no allocation in `run`).

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace lynx
{
// The block FL's wrapper uses (Fruity Wrapper FUN_00870670, REA round 134): "Use fixed size buffers" alone = 2 ms
// of audio rounded up to a multiple of 4 samples (88 at 44.1 kHz, 96 at 48 kHz); with "Process maximum size buffers"
// the host's maximum block (LYNX: the audio device's buffer) - more latency.
inline int flFixedBlockSize (double sampleRate, int deviceBlock, bool maximumSize) noexcept
{
    if (maximumSize)
        return std::max (16, deviceBlock);
    const auto twoMs = (int) (sampleRate * 2.0 / 1000.0 + 0.5);
    return std::max (16, (twoMs + 3) & ~3);
}

class FixedBlock
{
public:
    FixedBlock (int blockSize, int channels)
        : block (std::max (1, blockSize)), numChannels (std::max (1, channels)),
          input ((std::size_t) (block * numChannels), 0.0f), output ((std::size_t) (block * numChannels), 0.0f)
    {
        for (int c = 0; c < numChannels; ++c)
            pointers.push_back (input.data() + (std::size_t) (c * block));
    }

    int blockSize() const noexcept { return block; }
    int latency() const noexcept { return block; }
    // where the next input sample goes inside the block being collected (MIDI offsets of events arriving now)
    int position() const noexcept { return pos; }

    // `io[c]` (c < channels given at construction, `n` samples each) is replaced with the delayed output. `hasInput`
    // false: blocks start silent (instruments). `process (float* const* channels, int block)` works in place on a
    // whole block. Round 190ll (issue #199): a `process (channels, block, end)` also gets where that block's input ended
    // in this call (samples into `io`): the block began at `end - block`, which is negative when it began in an earlier
    // call — where a hosted plugin's play head must say the block starts, so that its blocks follow each other in time.
    template <typename Process>
    void run (float* const* io, int n, bool hasInput, Process&& process) noexcept
    {
        for (int done = 0; done < n;)
        {
            const auto count = std::min (n - done, block - pos);

            for (int c = 0; c < numChannels; ++c)
            {
                auto* in = input.data() + (std::size_t) (c * block + pos);
                auto* out = output.data() + (std::size_t) (c * block + pos);
                auto* x = io[c] + done;

                for (int i = 0; i < count; ++i)
                {
                    const auto v = x[i];
                    x[i] = out[i];
                    in[i] = hasInput ? v : 0.0f;
                }
            }

            pos += count;
            done += count;

            if (pos == block)
            {
                if constexpr (std::is_invocable_v<Process&, float* const*, int, int>)
                    process (pointers.data(), block, done);
                else
                    process (pointers.data(), block);
                std::copy (input.begin(), input.end(), output.begin());
                pos = 0;
            }
        }
    }

    void clear() noexcept
    {
        std::fill (input.begin(), input.end(), 0.0f);
        std::fill (output.begin(), output.end(), 0.0f);
        pos = 0;
    }

private:
    int block, numChannels, pos = 0;
    std::vector<float> input, output;
    std::vector<float*> pointers;
};
} // namespace lynx
