#pragma once

// The Audio logger's memory (FL 2026's Tools › Audio logger, measured in docs/analise/REA_rodada181b.md; LYNX's own
// implementation): the last 1, 2, 5 or 10 minutes of the Master, kept in RAM, nothing on disk until a dump.
//
// Real-time rules:
//  - the whole buffer is allocated by the control side (UI / device start), never by the audio thread;
//  - off costs one relaxed atomic load per call and no memory (the buffer is freed);
//  - on costs a copy of the block plus a scan that stops at the first sample that isn't silent;
//  - the audio thread never waits and takes no lock: while the control side swaps or copies the buffer (a few ms), the
//    blocks in between are simply not logged.
//
// Memory: 8 bytes per frame (stereo 32-bit float), that is seconds * rate * 8 bytes:
//                44.1 kHz    48 kHz     96 kHz
//     1 minute     20 MiB     22 MiB     44 MiB
//     2 minutes    40 MiB     44 MiB     88 MiB
//     5 minutes   101 MiB    110 MiB    220 MiB
//    10 minutes   202 MiB    220 MiB    440 MiB
// A dump needs the same amount again for as long as it takes to write the file.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace lynx
{
class RollingAudioLog
{
public:
    enum class Position
    {
        preEffects,  // the Master's input, before its effects
        postEffects  // after the effects and the track EQ, before the fader
    };

    static constexpr int defaultSeconds = 60;
    // A block counts as silent while every sample stays within this level ...
    static constexpr float silenceLevel = 1.0e-6f;
    // ... and after this long of it nothing more is stored, so a pause doesn't push the music out of the buffer
    static constexpr int silenceHoldSeconds = 8;

    RollingAudioLog() = default;
    RollingAudioLog (const RollingAudioLog&) = delete;
    RollingAudioLog& operator= (const RollingAudioLog&) = delete;

    // --- control side (any non-audio thread; serialised by a mutex the audio thread never touches) ---
    // Nothing is allocated until it is enabled AND the rate is known.
    void setEnabled (bool on);
    // Keeps the newest audio that fits the new length.
    void setLengthSeconds (int seconds);
    // Audio at the old rate would play wrong: another rate starts empty.
    void setSampleRate (double rate);
    void setPosition (Position p) noexcept { position.store (p, std::memory_order_relaxed); }

    bool isEnabled() const;
    int lengthSeconds() const;
    Position getPosition() const noexcept { return position.load (std::memory_order_relaxed); }
    double sampleRate() const;
    std::size_t bytesInUse() const;
    std::size_t framesCapacity() const;

    struct Dump
    {
        std::vector<float> left, right;
        double rate = 0.0;
    };
    // The contents, oldest first, without the exact zeros at both ends. The log is not cleared and keeps logging.
    // False when there is nothing above the silence level ("the logger is empty").
    bool dump (Dump& out);

    // --- audio thread ---
    // `r` may equal `l` (mono). `at` says where in the Master's chain the caller stands.
    void push (Position at, const float* l, const float* r, int n) noexcept
    {
        if (! writable.load (std::memory_order_relaxed) || position.load (std::memory_order_relaxed) != at)
            return;
        pushBlock (l, r, n);
    }

private:
    void pushBlock (const float* l, const float* r, int n) noexcept;
    void reallocate (std::size_t frames, bool keep);
    void stopWriter() noexcept;
    void startWriter() noexcept;

    std::atomic<bool> writable { false };
    std::atomic<bool> writing { false };
    std::atomic<Position> position { Position::postEffects };

    // owned by the writer while `writable`, by the control side otherwise
    std::unique_ptr<float[]> samples; // interleaved left / right
    std::size_t capacity = 0;         // frames
    std::size_t head = 0;             // the frame written next
    std::int64_t silentFrames = 0;
    std::int64_t silenceLimit = 0;

    mutable std::mutex control;
    bool enabled = false;
    int seconds = defaultSeconds;
    double rate = 0.0;
};
} // namespace lynx
