#pragma once

// Round 177d: what a recording in progress looks like before it is placed (the live display while recording).
// MusicTech's FL Studio 2025 review: "MIDI is not shown in the Playlist while recording" and audio recording is
// "unintuitive". LYNX draws the take as it grows: the notes still held (NoteRecorder::preview) in the Piano roll and
// the Playlist, and every audio take as a growing clip with its waveform on the Playlist track it will land on.
//
// Nothing here runs unless a take records: the peaks are written by the recording thread (never the audio thread) as it
// drains the take to disk, and read by the UI while it paints.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace lynx::liverec
{
// The waveform outline of an audio take being recorded: the largest |sample| over every `framesPerPeak` frames, all
// channels together. Writer: the recording thread (it may allocate; it is not the audio thread). Reader: the UI.
class LivePeaks
{
public:
    static constexpr int framesPerPeak = 256;

    void append (const float* const* data, int channels, int n)
    {
        if (data == nullptr || channels <= 0 || n <= 0)
            return;
        const std::lock_guard<std::mutex> guard (lock);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < channels; ++c)
                if (data[c] != nullptr)
                    pending = std::max (pending, std::abs (data[c][i]));
            if (++pendingFrames == framesPerPeak)
            {
                peaks.push_back (pending);
                pending = 0.0f;
                pendingFrames = 0;
            }
        }
        frames += n;
    }

    // frames appended so far
    std::int64_t size() const
    {
        const std::lock_guard<std::mutex> guard (lock);
        return frames;
    }

    // `count` values: the largest |sample| over [firstFrame + k * framesPerValue, firstFrame + (k + 1) * framesPerValue)
    // (0 where nothing was recorded). One lock for the whole column set.
    void read (double firstFrame, double framesPerValue, int count, float* out) const
    {
        if (out == nullptr || count <= 0)
            return;
        const std::lock_guard<std::mutex> guard (lock);
        const auto available = (std::int64_t) peaks.size();
        for (int k = 0; k < count; ++k)
        {
            const auto from = (firstFrame + k * framesPerValue) / framesPerPeak;
            const auto to = (firstFrame + (k + 1) * framesPerValue) / framesPerPeak;
            auto a = (std::int64_t) std::floor (from), b = (std::int64_t) std::ceil (to);
            a = std::clamp<std::int64_t> (a, 0, available);
            b = std::clamp<std::int64_t> (std::max (b, a + 1), 0, available);
            auto v = 0.0f;
            for (auto i = a; i < b; ++i)
                v = std::max (v, peaks[(std::size_t) i]);
            if (pendingFrames > 0 && from < (double) available + 1.0 && to > (double) available)
                v = std::max (v, pending); // the newest frames, not a whole peak yet
            out[k] = v;
        }
    }

private:
    mutable std::mutex lock;
    std::vector<float> peaks;
    float pending = 0.0f;
    int pendingFrames = 0;
    std::int64_t frames = 0;
};

// Where the part of an audio take that is being recorded shows on the song timeline. `startTick` = the transport
// position of the take's first sample (as the audio callback saw it), `frames` = frames recorded, `ticksPerSample` at
// the take's tempo, `latency` = the compensation in samples (takes::Latency::total()), `lastWrap` = the take frame at
// which the transport last jumped back to `loopStart` (loop recording; < 0: never). The same mapping as takes::place
// and takes::loopPasses: frame f sounds at startTick + (f − latency) · ticksPerSample; in a loop pass, the frame
// `lastWrap + latency` is the loop's start. Only the current pass shows (the earlier ones become take lanes at the stop).
struct AudioSpan
{
    double start = 0.0;          // song tick of the span's first frame
    double length = 0.0;         // ticks
    double firstFrame = 0.0;     // take frame at `start`
    double ticksPerSample = 0.0;
    bool valid() const noexcept { return length > 0.0; }
};

inline AudioSpan audioSpan (double startTick, std::int64_t frames, double ticksPerSample, int latency, std::int64_t lastWrap,
                            std::int64_t loopStart) noexcept
{
    AudioSpan s;
    s.ticksPerSample = ticksPerSample;
    if (startTick < 0.0 || frames <= 0 || ticksPerSample <= 0.0)
        return s;
    if (lastWrap >= 0)
    {
        s.firstFrame = (double) (lastWrap + std::max (0, latency));
        s.start = (double) loopStart;
    }
    else
    {
        s.firstFrame = 0.0;
        s.start = startTick - (double) latency * ticksPerSample;
    }
    s.length = std::max (0.0, ((double) frames - s.firstFrame) * ticksPerSample);
    if (s.start < 0.0) // (a take that starts before the song: what lies before 0 is not shown, as it is not placed)
    {
        s.firstFrame += -s.start / ticksPerSample;
        s.length = std::max (0.0, s.length + s.start);
        s.start = 0.0;
    }
    return s;
}
} // namespace lynx::liverec
