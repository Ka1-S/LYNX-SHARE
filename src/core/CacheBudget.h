#pragma once
// A size budget for LYNX's disk caches (round 167e): past the limit the least recently used entries go, oldest first,
// down to 3/4 of the limit (the stretched copies since round 165i, the decoded PCM copies since 167e).
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace lynx::cachebudget
{
struct Entry
{
    std::int64_t bytes = 0;
    std::int64_t lastUsed = 0; // any monotonic time (the file's modification time in ms)
};

// The entries to delete (indices into `entries`, oldest first); none while the total is within the limit.
inline std::vector<std::size_t> toPrune (const std::vector<Entry>& entries, std::int64_t limitBytes)
{
    std::int64_t total = 0;
    for (const auto& e : entries)
        total += e.bytes;
    if (total <= limitBytes)
        return {};
    std::vector<std::size_t> order (entries.size());
    std::iota (order.begin(), order.end(), std::size_t { 0 });
    std::stable_sort (order.begin(), order.end(), [&] (std::size_t a, std::size_t b) { return entries[a].lastUsed < entries[b].lastUsed; });
    std::vector<std::size_t> out;
    for (const auto i : order)
    {
        if (total <= limitBytes / 4 * 3)
            break;
        out.push_back (i);
        total -= entries[i].bytes;
    }
    return out;
}
} // namespace lynx::cachebudget
