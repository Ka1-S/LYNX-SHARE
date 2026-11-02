#pragma once

// The automatable parameters of an engine that keeps its knobs inside the plugin's saved state (round 168n): the
// designed editors, the automation clips (AutomationTargetKind::channelParam with `index`) and LYNX's preset banks
// all name a parameter by the engine's own index. The value of an automation clip (0…1) maps linearly to [low, high]
// (rounded for stepped parameters: switches, menus, counts).

#include <string>
#include <vector>

namespace lynx
{
struct EngineParam
{
    int index = 0;           // the engine's own parameter number (Toxic Biohazard P[i], PoiZone P[i], …)
    std::string name;        // unique within the engine, English ("Osc 1 attack"); the knob's name and the clip's name
    float low = 0.0f, high = 1.0f; // raw range stored in the state
    bool stepped = false;    // whole numbers only

    float normalised (float raw) const noexcept
    {
        return high > low ? (raw <= low ? 0.0f : raw >= high ? 1.0f : (raw - low) / (high - low)) : 0.0f;
    }
    float raw (float value01) const noexcept
    {
        const auto v = value01 <= 0.0f ? 0.0f : value01 >= 1.0f ? 1.0f : value01;
        const auto r = low + v * (high - low);
        return stepped ? (float) (int) (r + (r >= 0.0f ? 0.5f : -0.5f)) : r;
    }
};

// The parameter of `index` in a table sorted by index (nullptr = none)
inline const EngineParam* findEngineParam (const std::vector<EngineParam>& table, int index) noexcept
{
    std::size_t lo = 0, hi = table.size();
    while (lo < hi)
    {
        const auto mid = (lo + hi) / 2;
        if (table[mid].index < index)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < table.size() && table[lo].index == index ? &table[lo] : nullptr;
}
} // namespace lynx

#include <array>
#include <atomic>
#include <utility>

namespace lynx
{
// Parameter changes from the UI thread to the audio thread without rebuilding the engine (a knob turned while notes
// play keeps them playing, as in FL). One producer (the UI thread's configure), one consumer (the audio thread).
// push() fails when full: the caller then rebuilds the engine instead.
class LiveParamQueue
{
public:
    bool push (int index, float value) noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        const auto next = (t + 1) % capacity;
        if (next == head.load (std::memory_order_acquire))
            return false;
        items[t] = { index, value };
        tail.store (next, std::memory_order_release);
        return true;
    }
    std::size_t space() const noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed), h = head.load (std::memory_order_acquire);
        return capacity - 1 - (t + capacity - h) % capacity;
    }
    template <typename Apply>
    void drain (Apply&& apply) noexcept
    {
        auto h = head.load (std::memory_order_relaxed);
        const auto t = tail.load (std::memory_order_acquire);
        while (h != t)
        {
            apply (items[h].first, items[h].second);
            h = (h + 1) % capacity;
        }
        head.store (h, std::memory_order_release);
    }
    void clear() noexcept { head.store (tail.load (std::memory_order_acquire), std::memory_order_release); }

private:
    static constexpr std::size_t capacity = 1024;
    std::array<std::pair<int, float>, capacity> items {};
    std::atomic<std::size_t> head { 0 }, tail { 0 };
};
} // namespace lynx
