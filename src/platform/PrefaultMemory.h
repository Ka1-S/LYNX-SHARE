#pragma once

// Round 190ok: a hosted plugin that touches memory for the first time on the audio thread takes a page fault there
// (190of's --pluginbench: LSP's multiband compressors / gates, 88-234 faults in 3 s of playing, each one a trip into
// the kernel on the audio thread). On Linux the anonymous memory a plugin mapped while it loaded and prepared can be
// faulted in ahead, off the audio thread, without touching its contents: madvise (MADV_POPULATE_WRITE, Linux 5.14+)
// populates the page tables as a write would, and the plugin's first real write later costs nothing. Only private
// anonymous read-write mappings that appeared during the load are taken (not files, not thread stacks: a stack sits
// right above its guard page), each up to 32 MiB and 64 MiB in all per plugin; an older kernel answers EINVAL and
// nothing happens. Elsewhere these are no-ops. Header-only.

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#if defined(__linux__)
 #include <cerrno>
 #include <cstdio>
 #include <cstring>
 #include <sys/mman.h>
 #ifndef MADV_POPULATE_WRITE
  #define MADV_POPULATE_WRITE 23
 #endif
#endif

namespace lynx::prefault
{
struct Region
{
    std::uintptr_t start = 0, end = 0;
    bool anonymousWritable = false; // rw-p, no file, not [stack]
    bool guard = false;             // ---p (a stack's guard page)
};

// The process's mappings now (Linux: /proc/self/maps; elsewhere: none).
inline std::vector<Region> mappings()
{
    std::vector<Region> list;
#if defined(__linux__)
    if (auto* f = std::fopen ("/proc/self/maps", "r"))
    {
        char line[512];
        while (std::fgets (line, sizeof (line), f) != nullptr)
        {
            unsigned long a = 0, b = 0;
            char perms[8] {};
            int pathAt = 0;
            if (std::sscanf (line, "%lx-%lx %7s %*s %*s %*s %n", &a, &b, perms, &pathAt) < 3)
                continue;
            const char* path = line + pathAt;
            while (*path == ' ')
                ++path;
            const bool noFile = *path == '\n' || *path == '\0' || std::strncmp (path, "[heap]", 6) == 0;
            Region r;
            r.start = a;
            r.end = b;
            r.anonymousWritable = std::strcmp (perms, "rw-p") == 0 && noFile;
            r.guard = std::strcmp (perms, "---p") == 0;
            list.push_back (r);
        }
        std::fclose (f);
    }
#endif
    return list;
}

struct Result
{
    std::size_t regions = 0, bytes = 0;
    bool supported = true;
};

// Populates the anonymous read-write memory that is new since `before` (or grew: the new part of a region), up to the
// caps. Off the audio thread (it may take a few ms for tens of MB).
inline Result populateNew (const std::vector<Region>& before, std::size_t maxRegionBytes = 32u << 20, std::size_t maxTotalBytes = 64u << 20)
{
    Result out;
#if defined(__linux__)
    const auto now = mappings();
    std::set<std::pair<std::uintptr_t, std::uintptr_t>> old;
    for (const auto& r : before)
        if (r.anonymousWritable)
            old.insert ({ r.start, r.end });
    for (std::size_t k = 0; k < now.size(); ++k)
    {
        const auto& r = now[k];
        if (! r.anonymousWritable || old.count ({ r.start, r.end }) != 0)
            continue;
        if (k > 0 && now[k - 1].guard && now[k - 1].end == r.start)
            continue; // a thread's stack (its guard page right below it)
        // a region that only grew (the heap, an arena): its new part
        auto start = r.start;
        for (const auto& o : before)
            if (o.anonymousWritable && o.start == r.start && o.end < r.end)
                start = o.end;
        const auto size = (std::size_t) (r.end - start);
        if (size == 0 || size > maxRegionBytes || out.bytes + size > maxTotalBytes)
            continue;
        if (madvise ((void*) start, size, MADV_POPULATE_WRITE) == 0)
        {
            ++out.regions;
            out.bytes += size;
        }
        else if (errno == EINVAL)
        {
            out.supported = false; // (a kernel before 5.14)
            break;
        }
    }
#else
    (void) before, (void) maxRegionBytes, (void) maxTotalBytes;
    out.supported = false;
#endif
    return out;
}
} // namespace lynx::prefault
