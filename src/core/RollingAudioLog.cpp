#include "core/RollingAudioLog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <thread>

namespace lynx
{
void RollingAudioLog::stopWriter() noexcept
{
    // pairs with pushBlock: either it sees writable == false, or this sees writing == true and waits for that one block
    writable.store (false, std::memory_order_seq_cst);
    while (writing.load (std::memory_order_seq_cst))
        std::this_thread::yield();
}

void RollingAudioLog::startWriter() noexcept
{
    writable.store (samples != nullptr, std::memory_order_seq_cst);
}

void RollingAudioLog::pushBlock (const float* l, const float* r, int n) noexcept
{
    writing.store (true, std::memory_order_seq_cst);
    if (writable.load (std::memory_order_seq_cst) && l != nullptr && n > 0)
    {
        if (r == nullptr)
            r = l;

        // both channels within the level? stops at the first sample that isn't (at once, while music plays)
        auto silent = true;
        for (int i = 0; i < n && silent; ++i)
            silent = std::abs (l[i]) <= silenceLevel && std::abs (r[i]) <= silenceLevel;

        silentFrames = silent ? silentFrames + n : 0;
        if (silentFrames <= silenceLimit)
        {
            // a block longer than the whole buffer: only its end can stay
            const auto count = std::min<std::size_t> ((std::size_t) n, capacity);
            const auto skip = (std::size_t) n - count;
            auto* out = samples.get();
            auto at = head;
            for (std::size_t i = 0; i < count; ++i)
            {
                out[at * 2] = l[skip + i];
                out[at * 2 + 1] = r[skip + i];
                if (++at == capacity)
                    at = 0;
            }
            head = at;
        }
    }
    writing.store (false, std::memory_order_seq_cst);
}

void RollingAudioLog::reallocate (std::size_t frames, bool keep)
{
    // (the new buffer is made and cleared before the writer stops, so the pause is only the copy)
    std::unique_ptr<float[]> fresh;
    if (frames > 0)
        fresh.reset (new (std::nothrow) float[frames * 2]());

    stopWriter();
    auto old = std::move (samples);
    const auto oldCapacity = capacity;
    const auto oldHead = head;

    samples = std::move (fresh);
    capacity = samples != nullptr ? frames : 0;
    head = 0;
    silentFrames = 0;
    silenceLimit = (std::int64_t) (rate * silenceHoldSeconds);

    if (keep && old != nullptr && samples != nullptr)
    {
        // the newest frames, in time order; an unfilled start stays zeros and a dump trims it
        const auto kept = std::min (oldCapacity, capacity);
        for (std::size_t i = 0; i < kept; ++i)
        {
            const auto from = (oldHead + oldCapacity - kept + i) % oldCapacity;
            samples[i * 2] = old[from * 2];
            samples[i * 2 + 1] = old[from * 2 + 1];
        }
        head = kept % capacity;
    }
    startWriter();
    // `old` goes here, off the audio thread
}

void RollingAudioLog::setEnabled (bool on)
{
    const std::lock_guard<std::mutex> lock (control);
    if (on == enabled)
        return;
    enabled = on;
    reallocate (on && rate > 0.0 ? (std::size_t) (seconds * rate) : 0, false);
}

void RollingAudioLog::setLengthSeconds (int newSeconds)
{
    const std::lock_guard<std::mutex> lock (control);
    newSeconds = std::max (1, newSeconds);
    if (newSeconds == seconds)
        return;
    seconds = newSeconds;
    if (enabled && rate > 0.0)
        reallocate ((std::size_t) (seconds * rate), true);
}

void RollingAudioLog::setSampleRate (double newRate)
{
    const std::lock_guard<std::mutex> lock (control);
    if (newRate <= 0.0 || newRate == rate)
        return;
    rate = newRate;
    if (enabled)
        reallocate ((std::size_t) (seconds * rate), false);
}

bool RollingAudioLog::isEnabled() const
{
    const std::lock_guard<std::mutex> lock (control);
    return enabled;
}

int RollingAudioLog::lengthSeconds() const
{
    const std::lock_guard<std::mutex> lock (control);
    return seconds;
}

double RollingAudioLog::sampleRate() const
{
    const std::lock_guard<std::mutex> lock (control);
    return rate;
}

std::size_t RollingAudioLog::framesCapacity() const
{
    const std::lock_guard<std::mutex> lock (control);
    return capacity;
}

std::size_t RollingAudioLog::bytesInUse() const
{
    return framesCapacity() * 2 * sizeof (float);
}

bool RollingAudioLog::dump (Dump& out)
{
    const std::lock_guard<std::mutex> lock (control);
    out = {};
    if (samples == nullptr)
        return false;

    // made before the writer stops; the pause is only the two copies
    std::vector<float> copy (capacity * 2);
    stopWriter();
    const auto oldest = head;
    std::memcpy (copy.data(), samples.get() + oldest * 2, (capacity - oldest) * 2 * sizeof (float));
    std::memcpy (copy.data() + (capacity - oldest) * 2, samples.get(), oldest * 2 * sizeof (float));
    startWriter();

    auto isZero = [&copy] (std::size_t frame) { return copy[frame * 2] == 0.0f && copy[frame * 2 + 1] == 0.0f; };
    std::size_t first = 0, last = capacity; // [first, last)
    while (first + 1 < last && isZero (first))
        ++first;
    while (last - 1 > first && isZero (last - 1))
        --last;

    auto audible = false;
    for (auto frame = first; frame < last && ! audible; ++frame)
        audible = std::abs (copy[frame * 2]) > silenceLevel || std::abs (copy[frame * 2 + 1]) > silenceLevel;
    if (! audible)
        return false;

    out.left.resize (last - first);
    out.right.resize (last - first);
    for (auto frame = first; frame < last; ++frame)
    {
        out.left[frame - first] = copy[frame * 2];
        out.right[frame - first] = copy[frame * 2 + 1];
    }
    out.rate = rate;
    return true;
}
} // namespace lynx
