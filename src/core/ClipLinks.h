#pragma once

// Round 190rt (issue #310): an existing automation clip linked to a control, as FL 2026 does it — the control's
// "Link to controller..." window lists every clip under "Internal controller › Automation clips"; Accept with "Remove
// conflicts" on (FL's default) makes the clip drive that control alone (a re-target: FL's clip keeps only the new
// target link), with it off the control joins the clip's target links (checked in FL 2026, round 190rt). The clip keeps
// its points either way. A clip several controls follow is LYNX's Automation::links (round 190gw), 1:1 links here.

#include <algorithm>
#include <vector>

#include "core/Song.h"

namespace lynx::cliplinks
{
// a clip LYNX can link a control to: a positional clip (not a live controller link), not the tempo (LYNX's tempo map
// reads the tempo clips; a tempo clip cannot drive other controls) and not the ruler's tempo markers
inline bool linkable (const Automation& a) noexcept
{
    return ! a.isLive() && ! a.tempoMarkers && a.target.kind != AutomationTargetKind::tempo;
}

inline bool drives (const Automation& a, const AutomationTarget& t) noexcept
{
    if (a.isLive())
        return false;
    if (a.links.empty())
        return a.target == t && t.kind != AutomationTargetKind::none;
    return std::any_of (a.links.begin(), a.links.end(), [&t] (const Automation::Link& l) { return l.target == t; });
}

// the first clip that drives `t`, or -1
inline int clipDriving (const Song& song, const AutomationTarget& t) noexcept
{
    for (int i = 0; i < (int) song.automations.size(); ++i)
        if (drives (song.automations[(std::size_t) i], t))
            return i;
    return -1;
}

// `t` onto clip `index`: alone with `replace`, else beside its other targets. False when nothing changed.
inline bool link (Song& song, int index, const AutomationTarget& t, bool replace)
{
    if (index < 0 || index >= (int) song.automations.size() || t.kind == AutomationTargetKind::none
        || t.kind == AutomationTargetKind::tempo)
        return false;
    auto& a = song.automations[(std::size_t) index];
    if (! linkable (a))
        return false;
    if (replace || a.target.kind == AutomationTargetKind::none)
    {
        if (a.links.empty() && a.target == t)
            return false;
        a.links.clear();
        a.target = t;
        return true;
    }
    if (drives (a, t))
        return false;
    if (a.links.empty())
    {
        Automation::Link first;
        first.target = a.target;
        a.links.push_back (std::move (first));
    }
    Automation::Link added;
    added.target = t;
    a.links.push_back (std::move (added));
    a.target = a.links.front().target; // (Automation::links: `target` is the first link's)
    return true;
}

// `t` out of clip `index`'s targets (the clip keeps its points; without a target it drives nothing). One 1:1 link left
// becomes the plain clip again. False when the clip did not drive `t`.
inline bool unlink (Song& song, int index, const AutomationTarget& t)
{
    if (index < 0 || index >= (int) song.automations.size())
        return false;
    auto& a = song.automations[(std::size_t) index];
    if (! drives (a, t))
        return false;
    if (a.links.empty())
    {
        a.target = {};
        return true;
    }
    a.links.erase (std::remove_if (a.links.begin(), a.links.end(), [&t] (const Automation::Link& l) { return l.target == t; }),
                   a.links.end());
    if (a.links.empty())
        a.target = {};
    else
    {
        a.target = a.links.front().target;
        if (a.links.size() == 1 && a.links.front().formula.empty() && a.links.front().scale.empty())
            a.links.clear();
    }
    return true;
}
} // namespace lynx::cliplinks
