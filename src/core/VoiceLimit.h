#pragma once

// A channel's "Max polyphony" (FL's Channel settings › Polyphony › Max, flp event 0xDD; REA round 139): with a limit,
// a new note beyond it releases the oldest sounding note first (FL: "the excess releases the oldest voices"). The
// renderer keeps one VoiceOrder per channel. JUCE-free and real-time safe (fixed storage, no allocation).

#include <array>
#include <cstdint>

namespace lynx
{
class VoiceOrder
{
public:
    // Before starting `key`: the key to release first (the oldest note) when `limit` (> 0) voices already sound,
    // else -1. The new note becomes the newest one (a key played again moves to the end).
    int noteOn (int key, int limit) noexcept
    {
        if (key < 0 || key > 127)
            return -1;
        remove (key);
        int victim = -1;
        if (limit > 0 && count >= limit)
        {
            victim = keys[0];
            removeAt (0);
        }
        keys[(std::size_t) count++] = (std::int8_t) key;
        return victim;
    }

    void noteOff (int key) noexcept { remove (key); }
    void clear() noexcept { count = 0; }
    int sounding() const noexcept { return count; }
    int oldest() const noexcept { return count > 0 ? keys[0] : -1; }

private:
    void remove (int key) noexcept
    {
        for (int i = 0; i < count; ++i)
            if (keys[(std::size_t) i] == key)
            {
                removeAt (i);
                return;
            }
    }

    void removeAt (int i) noexcept
    {
        for (int k = i + 1; k < count; ++k)
            keys[(std::size_t) (k - 1)] = keys[(std::size_t) k];
        --count;
    }

    std::array<std::int8_t, 128> keys {};
    int count = 0;
};

// The limit the renderer applies to a channel: 0 = none. FL's limiter does not run on a Mono channel (FLEngine
// 0xd293d0: only with the Mono bit off, round 168l): its notes share one voice anyway (core/ChannelPoly.h).
inline int voiceLimitFor (int maxPolyphony, bool mono) noexcept
{
    if (maxPolyphony <= 0 || mono)
        return 0;
    return maxPolyphony;
}
} // namespace lynx
