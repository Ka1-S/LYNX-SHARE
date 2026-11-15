#include "core/AudioCheck.h"

#include <algorithm>

namespace lynx::audiocheck
{
Verdict verdict (bool deviceOpen, float peak, bool heard) noexcept
{
    if (! deviceOpen)
        return Verdict::noDevice;
    if (heard)
        return Verdict::works;
    return peak >= reachedPeak ? Verdict::reachedNotHeard : Verdict::notReached;
}

namespace
{
// lower = tried first
int rank (const std::string& os, const std::string& type)
{
    static const std::vector<std::string> windowsOrder { "Windows Audio", "DirectSound", "ASIO", "Windows Audio (Exclusive Mode)",
                                                    "Windows Audio (Low Latency Mode)" };
    static const std::vector<std::string> linuxOrder { "PipeWire", "ALSA", "JACK" };
    static const std::vector<std::string> macosOrder { "CoreAudio" };
    const auto& order = os == "windows" ? windowsOrder : os == "linux" ? linuxOrder : macosOrder;
    const auto it = std::find (order.begin(), order.end(), type);
    return it != order.end() ? (int) (it - order.begin()) : (int) order.size();
}
} // namespace

std::vector<Choice> alternatives (const std::string& os, const Choice& current,
                                  const std::vector<std::pair<std::string, std::vector<std::string>>>& available, int maxCount)
{
    std::vector<Choice> out;
    auto add = [&] (const Choice& c) {
        if ((int) out.size() < maxCount && ! (c == current) && ! c.name.empty() && std::find (out.begin(), out.end(), c) == out.end())
            out.push_back (c);
    };
    // the current type's other devices (at most two, so other drivers still get a place)
    int same = 0;
    for (const auto& [type, names] : available)
        if (type == current.type)
            for (const auto& n : names)
                if (same < 2 && ! (Choice { type, n } == current))
                {
                    add ({ type, n });
                    ++same;
                }
    // then each other type's first (default) device, in the system's order
    std::vector<std::pair<std::string, std::vector<std::string>>> others;
    for (const auto& t : available)
        if (t.first != current.type && ! t.second.empty())
            others.push_back (t);
    std::stable_sort (others.begin(), others.end(), [&os] (const auto& a, const auto& b) { return rank (os, a.first) < rank (os, b.first); });
    for (const auto& [type, names] : others)
        add ({ type, names.front() });
    // room left: more of the current type
    for (const auto& [type, names] : available)
        if (type == current.type)
            for (const auto& n : names)
                add ({ type, n });
    return out;
}
} // namespace lynx::audiocheck
