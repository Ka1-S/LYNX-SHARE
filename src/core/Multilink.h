#pragma once
// FL's "Multilink to controllers" (Options › Switches, Ctrl+J; FL manual "Automation & Recording › linking", FLEngine
// OptionsToggleMultiLinkAction; round 177b-4): while it is on, every control the user moves is queued in that order;
// then the controllers moved on the hardware link to them in the same order, one controller (CC and MIDI channel) per
// target. The session ends by itself when there are as many controllers as targets.
#include "core/Song.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace lynx
{
struct MultilinkSession
{
    struct Link
    {
        int cc = 0, channel = 0;
        AutomationTarget target;
    };

    std::vector<AutomationTarget> targets; // in the order they were moved
    std::vector<std::pair<int, int>> used; // (cc, channel) already linked in this session
    int linked = 0;
    bool collecting = true; // false after the switch's "Link to controller" / "Override global links": only controllers now
    bool global = false;    // "Override global links": the links go to the settings, for every project

    // a moved control: queued once (moving it again keeps its place)
    bool add (const AutomationTarget& t)
    {
        if (! collecting || t.kind == AutomationTargetKind::none || std::find (targets.begin(), targets.end(), t) != targets.end())
            return false;
        targets.push_back (t);
        return true;
    }

    // a controller message: the next target, the first time this controller moves; nothing otherwise
    std::optional<Link> controller (int cc, int channel)
    {
        if (linked >= (int) targets.size())
            return std::nullopt;
        const std::pair<int, int> key { cc, channel };
        if (std::find (used.begin(), used.end(), key) != used.end())
            return std::nullopt;
        used.push_back (key);
        return Link { cc, channel, targets[(std::size_t) linked++] };
    }

    bool complete() const noexcept { return ! targets.empty() && linked >= (int) targets.size(); }
};
} // namespace lynx
