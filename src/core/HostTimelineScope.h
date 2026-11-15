#pragma once

// Round 190gq (render-ahead, core/RenderAhead.h): the play head hostTimeline() (core/Instruments.h) gives on this
// thread. A renderer that renders ahead of the song position sets its own around its units, so the hosted plugins it
// runs see the position they are rendering; nullptr = the shared one (the song position). Defined in Instruments.cpp.

namespace lynx
{
struct HostTimeline;

// returns the previous one
HostTimeline* swapThreadHostTimeline (HostTimeline* timeline) noexcept;

struct ScopedHostTimeline
{
    explicit ScopedHostTimeline (HostTimeline* timeline) noexcept
        : active (timeline != nullptr), previous (active ? swapThreadHostTimeline (timeline) : nullptr)
    {
    }
    ~ScopedHostTimeline()
    {
        if (active)
            swapThreadHostTimeline (previous);
    }
    ScopedHostTimeline (const ScopedHostTimeline&) = delete;
    ScopedHostTimeline& operator= (const ScopedHostTimeline&) = delete;
    const bool active;
    HostTimeline* const previous;
};
} // namespace lynx
