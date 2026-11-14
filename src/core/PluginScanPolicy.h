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

// Round 190fw: the limit above is for a child that sits still. Image-Line's "FL Studio VSTi" boots the whole FL
// engine when it is created (about 9 s warm; a first or cold start, with FL's files read from disk, went past 30 s and
// both FL plugins were blacklisted for good), and a plugin may show a licence or first-run window during the scan that
// waits for the user. The child now reports what it is doing (its CPU time, the bytes it read, its page faults and
// whether it shows a window) and the parent only gives up on a child that has done nothing for `stallMs`, or that has
// been busy past `busyCapMs`. A child that hangs on a lock or an invisible dialog still ends at the old limit.
struct ChildActivity
{
    long long cpuMs = 0;      // user + kernel CPU time so far
    long long readBytes = 0;  // bytes read from files so far
    long long faults = 0;     // page faults so far (a cold DLL is read through them, not through reads)
    int windows = 0;          // visible top-level windows it shows now
};

constexpr int stallMs = baseTimeoutMs;          // nothing done for this long: hung
constexpr int busyCapMs = 5 * 60 * 1000;        // working (or waiting on its window) never longer than this
constexpr int busyCpuPercent = 5;               // CPU time per interval that counts as work
constexpr long long busyReadBytes = 1 << 20;    // or this much read in one interval
constexpr long long busyFaults = 256;           // or this many page faults in one interval

// whether the child did something between two reports `intervalMs` apart
constexpr bool childWorked (const ChildActivity& before, const ChildActivity& now, int intervalMs) noexcept
{
    return now.windows > 0 || (now.cpuMs - before.cpuMs) * 100 >= (long long) busyCpuPercent * std::max (1, intervalMs)
        || now.readBytes - before.readBytes >= busyReadBytes || now.faults - before.faults >= busyFaults;
}

// One scan child, from the parent's side: the limit for its file (timeoutMs) and the time it last did something.
class ChildWatch
{
public:
    explicit ChildWatch (int baseMs = baseTimeoutMs, int stall = stallMs, int cap = busyCapMs) noexcept
        : base (baseMs), stallLimit (stall), capLimit (cap) {}

    // the number of plugins in the file, once the child has said it
    void setClasses (int count) noexcept { classes = count; }

    // a report of the child at `elapsedMs` after its start
    void report (int elapsedMs, const ChildActivity& now) noexcept
    {
        if (seen && childWorked (last, now, elapsedMs - lastAt))
            lastWork = elapsedMs;
        last = now;
        lastAt = elapsedMs;
        seen = true;
    }

    int limitMs() const noexcept
    {
        const auto extra = classes > 1 ? std::min<long long> ((long long) (classes - 1) * perClassMs, maxTimeoutMs) : 0;
        return (int) std::min<long long> ((long long) base + extra, maxTimeoutMs);
    }

    // true once the child should be killed
    bool expired (int elapsedMs) const noexcept
    {
        const auto limit = limitMs();
        if (elapsedMs < limit)
            return false;
        // past the limit: only a child that still works (and not past the cap) gets more time
        return lastWork < 0 || elapsedMs - lastWork >= stallLimit || elapsedMs >= std::max (limit, capLimit);
    }

    // why it was killed, for scan-failures.log
    const char* why (int elapsedMs) const noexcept
    {
        if (lastWork >= 0 && elapsedMs - lastWork < stallLimit)
            return "still busy at the limit";
        return lastWork >= 0 ? "stopped working" : "no sign of work";
    }

    int lastWorkMs() const noexcept { return lastWork; }

private:
    int base, stallLimit, capLimit;
    int classes = 0;
    ChildActivity last {};
    int lastAt = 0;
    int lastWork = -1;
    bool seen = false;
};

// The format a file the scan could not describe belongs to, for the Plugin Manager's "Failed to load" rows (round
// 190fw: a VST2 .dll was shown as "VST3"). The names are the hosts' format names (JUCE's, and "VST" for VST2).
inline std::string formatOfFile (const std::string& path)
{
    auto lower = path;
    for (auto& c : lower)
        c = (char) ((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    while (! lower.empty() && (lower.back() == '/' || lower.back() == '\\'))
        lower.pop_back();
    const auto ends = [&lower] (const std::string& tail) {
        return lower.size() >= tail.size() && lower.compare (lower.size() - tail.size(), tail.size(), tail) == 0;
    };
    if (lower.rfind ("lv2-folders:", 0) == 0 || ends (".lv2") || (lower.find ("://") != std::string::npos) || lower.rfind ("urn:", 0) == 0)
        return "LV2";
    if (ends (".clap"))
        return "CLAP";
    if (ends (".vst3"))
        return "VST3";
    if (ends (".dll") || ends (".so") || ends (".vst"))
        return "VST";
    if (ends (".component"))
        return "AudioUnit";
    return "VST3";
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
