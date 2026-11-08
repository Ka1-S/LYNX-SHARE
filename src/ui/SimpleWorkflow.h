#pragma once

// The Simple workflow (View › Layout style, round 184a; design: /mnt/e/lynx-work/simple184a/DESIGN.md): one window with
// the tracks, the timeline and, in later slices, the New track chooser, a dock for the selected track and the Finish
// dock. This object owns those parts. MainComponent makes it the first time the style is chosen and keeps it for the
// session, so in Pattern and Clip-launch workflow nothing of it exists (the selftest checks `instances` stays 0).

#include <atomic>

namespace lynx
{
class SimpleWorkflow
{
public:
    SimpleWorkflow() { instances.fetch_add (1, std::memory_order_relaxed); }
    ~SimpleWorkflow() = default;
    SimpleWorkflow (const SimpleWorkflow&) = delete;
    SimpleWorkflow& operator= (const SimpleWorkflow&) = delete;

    // how many were ever made in this process (the "nothing exists in Pattern workflow" check)
    static inline std::atomic<int> instances { 0 };
};
} // namespace lynx
