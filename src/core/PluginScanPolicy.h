#pragma once

// How long the plugin scan waits for the child process that describes one plugin file (engine/PluginHost.cpp).
// A plugin file can hold many plugins: a Waves "WaveShell" VST3 declares hundreds of classes, and describing each one
// means creating and initialising it. A fixed 30 s limit blacklisted whole Waves bundles as "hung" (REA_rodada107 §2),
// so every Waves plugin of a project fell back to a substitute. The child now reports how many classes the file has
// before describing them, and the limit grows with that number.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace lynx::scanpolicy
{
constexpr int baseTimeoutMs = 30000;          // a file with one plugin (or whose class count is not known)
constexpr int perClassMs = 2000;              // each further class
constexpr int maxTimeoutMs = 45 * 60 * 1000;  // never wait longer than this for one file

constexpr int timeoutMs (int classes) noexcept
{
    if (classes <= 1)
        return baseTimeoutMs;
    const auto extra = (long long) (classes - 1) * perClassMs;
    return (int) std::min<long long> ((long long) baseTimeoutMs + extra, maxTimeoutMs);
}

// What the LV2 folders hold: each bundle and the date of its newest .ttl, in any order the disk lists them. An LV2 plugin
// has no file of its own to compare (JUCE's pluginNeedsRescanning is always true for LV2), so this text stands for all.
inline std::string lv2Stamp (std::vector<std::pair<std::string, long long>> bundles)
{
    std::sort (bundles.begin(), bundles.end());
    std::string stamp;
    for (const auto& [path, newestTtl] : bundles)
        stamp += path + "|" + std::to_string (newestTtl) + "\n";
    return stamp;
}
} // namespace lynx::scanpolicy
