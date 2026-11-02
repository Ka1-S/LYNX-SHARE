#include "core/Workspace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace lynx::workspace
{
namespace
{
const char* const ids[panelCount] = { "playlist", "channelRack", "pianoRoll", "mixer", "browser" };

Rect clampInto (Rect r, int width, int height)
{
    r.w = std::clamp (r.w, std::min (minWidth, std::max (1, width)), std::max (1, width));
    r.h = std::clamp (r.h, std::min (minHeight, std::max (1, height)), std::max (1, height));
    r.x = std::clamp (r.x, 0, std::max (0, width - r.w));
    r.y = std::clamp (r.y, 0, std::max (0, height - r.h));
    return r;
}

// a panel counts as off-screen when less than this much of its title strip is reachable
bool isReachable (const Rect& r, int width, int height)
{
    constexpr int grab = 40;
    return r.right() >= grab && r.x <= width - grab && r.y >= 0 && r.y <= height - 20 && r.w > 0 && r.h > 0;
}
} // namespace

const char* panelId (Panel panel) noexcept
{
    return ids[(int) panel];
}

std::optional<Panel> panelFromId (const std::string& id)
{
    for (int i = 0; i < panelCount; ++i)
        if (id == ids[i])
            return (Panel) i;

    return std::nullopt;
}

Layout Layout::makeDefault (int width, int height)
{
    Layout l;
    l.width = std::max (1, width);
    l.height = std::max (1, height);
    const auto w = l.width, h = l.height;
    auto state = [] (Rect r, bool visible) {
        PanelState p;
        p.bounds = r;
        p.visible = visible;
        return p;
    };

    const auto browserWidth = std::clamp (w * 17 / 100, minWidth, 260);
    l[Panel::browser] = state ({ 0, 0, browserWidth, h }, true);
    l[Panel::playlist] = state ({ browserWidth, 0, w - browserWidth, h }, true);

    // The Channel Rack floats over the Playlist (FL's View › Arrange windows › Desktop (default): the Browser at the
    // left, the Playlist filling the rest, the rack a window above it; UI fits its size to the rows afterwards),
    // below the Playlist's toolbar row and to the right of its pattern picker and track names, so the arrangement
    // stays readable. On a small desktop (a laptop at 125/150 % scaling) it gets narrower (its steps scroll) rather
    // than covering the track names and the whole arrangement (round 167v); only when even rackMinWidth does not fit
    // there does it move left over the track names.
    const auto gap = 16;
    const auto besideHeaders = w - gap - (browserWidth + playlistHeaderWidth);
    const auto rackWidth = besideHeaders >= rackMinWidth ? std::min (besideHeaders, rackMaxWidth)
                                                         : std::min (w - browserWidth - 2 * gap, rackMinWidth);
    const auto rackHeight = std::min (h - 2 * gap, std::max (240, h * 55 / 100));
    const auto rackTop = std::min (40, h / 10);
    const auto rackLeft = std::max (browserWidth + gap, std::min (browserWidth + playlistHeaderWidth, w - gap - rackWidth));
    l[Panel::channelRack] = state ({ rackLeft, rackTop, rackWidth, std::min (rackHeight, h - rackTop - gap) }, true);

    // Hidden until called (F7, F9), with rects that tile when both are shown on a tall enough desktop: the
    // Mixer along the bottom, the Piano Roll above it. On a short desktop the Piano Roll keeps a usable height
    // and overlaps the Mixer, as floating windows do.
    const auto mixerHeight = std::min (h - 2 * gap, std::max (mixerMinHeight, h * 45 / 100));
    const auto mixerTop = h - mixerHeight - gap / 2;
    l[Panel::mixer] = state ({ browserWidth + gap / 2, mixerTop, w - browserWidth - gap, mixerHeight }, false);

    const auto rollTop = 2 * gap;
    const auto tiledRoll = mixerTop - gap / 2 - rollTop;
    const auto rollHeight = tiledRoll >= pianoRollMinHeight ? tiledRoll : std::max (minHeight, h * 58 / 100);
    l[Panel::pianoRoll] = state ({ browserWidth + 2 * gap, rollTop, w - browserWidth - 4 * gap, rollHeight }, false);

    for (auto& p : l.panels)
        p.bounds = clampInto (p.bounds, w, h);

    l.order = { Panel::playlist, Panel::browser, Panel::mixer, Panel::pianoRoll, Panel::channelRack };
    l.focused = Panel::channelRack;
    return l;
}

void Layout::relayoutDefault (int newWidth, int newHeight)
{
    const auto fresh = makeDefault (newWidth, newHeight);

    for (int i = 0; i < panelCount; ++i)
        panels[(std::size_t) i].bounds = fresh.panels[(std::size_t) i].bounds;

    width = fresh.width;
    height = fresh.height;
}

void Layout::bringToFront (Panel p)
{
    order.erase (std::remove (order.begin(), order.end(), p), order.end());
    order.push_back (p);
}

bool Layout::isFrontmost (Panel p) const
{
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if ((*this)[*it].visible && ! (*this)[*it].detached)
            return *it == p;

    return false;
}

void Layout::show (Panel p)
{
    (*this)[p].visible = true;
    bringToFront (p);
    focused = p;
}

void Layout::hide (Panel p)
{
    (*this)[p].visible = false;

    if (focused == p)
        for (auto it = order.rbegin(); it != order.rend(); ++it)
            if ((*this)[*it].visible)
            {
                focused = *it;
                break;
            }
}

bool Layout::toggle (Panel p)
{
    auto& state = (*this)[p];

    if (state.visible && focused == p)
    {
        hide (p);
        return false;
    }

    show (p);
    return true;
}

void Layout::toggleMaximised (Panel p)
{
    auto& state = (*this)[p];
    state.maximised = ! state.maximised;

    if (state.maximised)
        show (p);
}

void Layout::resize (int newWidth, int newHeight)
{
    newWidth = std::max (1, newWidth);
    newHeight = std::max (1, newHeight);
    const auto dockedPlaylist = width > 0 && browserDocksPlaylist();

    if (width > 0 && height > 0 && (newWidth != width || newHeight != height))
    {
        const auto sx = (double) newWidth / width, sy = (double) newHeight / height;

        for (auto& p : panels)
        {
            // edges that touched the desktop border keep touching it
            const auto r = p.bounds;
            auto x0 = (int) std::lround (r.x * sx), y0 = (int) std::lround (r.y * sy);
            auto x1 = r.right() >= width ? newWidth : (int) std::lround (r.right() * sx);
            auto y1 = r.bottom() >= height ? newHeight : (int) std::lround (r.bottom() * sy);
            p.bounds = { x0, y0, x1 - x0, y1 - y0 };
        }
    }

    width = newWidth;
    height = newHeight;

    for (auto& p : panels)
        p.bounds = clampInto (p.bounds, width, height);

    if (dockedPlaylist)
        redockPlaylist();
}

// The Browser at the desktop's left edge with the Playlist starting where it ends (the default arrangement):
// they stay side by side however the desktop is resized (the Browser's minimum width must not push it over the
// Playlist's pattern picker, round 167v).
bool Layout::browserDocksPlaylist() const
{
    const auto& b = (*this)[Panel::browser].bounds;
    const auto& p = (*this)[Panel::playlist].bounds;
    return b.x == 0 && p.x == b.right() && p.right() >= width;
}

void Layout::redockPlaylist()
{
    const auto& b = (*this)[Panel::browser].bounds;
    auto& p = (*this)[Panel::playlist].bounds;

    if (b.x != 0 || b.right() > width - minWidth)
        return;

    p.x = b.right();
    p.w = width - p.x;
}

int Layout::bringBackOffscreen()
{
    int moved = 0;

    for (auto& p : panels)
        if (! isReachable (p.bounds, width, height))
        {
            p.bounds = clampInto (p.bounds, width, height);
            ++moved;
        }

    return moved;
}

std::string Layout::serialise() const
{
    std::ostringstream out;
    out << "lynx-layout 1\n";
    char line[256];

    for (int i = 0; i < panelCount; ++i)
    {
        const auto& p = panels[(std::size_t) i];
        const auto fw = (double) std::max (1, width), fh = (double) std::max (1, height);
        std::snprintf (line, sizeof (line), "panel %s %d %d %d %.5f %.5f %.5f %.5f %d %d %d %d\n", ids[i], p.visible ? 1 : 0,
                       p.maximised ? 1 : 0, p.detached ? 1 : 0, p.bounds.x / fw, p.bounds.y / fh, p.bounds.w / fw, p.bounds.h / fh,
                       p.windowBounds.x, p.windowBounds.y, p.windowBounds.w, p.windowBounds.h);
        out << line;
    }

    out << "order";
    for (auto p : order)
        out << ' ' << ids[(int) p];
    out << "\nfocus " << ids[(int) focused] << "\n";
    return out.str();
}

std::optional<Layout> Layout::parse (const std::string& text, int width, int height)
{
    auto layout = makeDefault (width, height);
    std::istringstream in (text);
    std::string header;
    int version = 0;

    if (! (in >> header >> version) || header != "lynx-layout" || version != 1)
        return std::nullopt;

    std::string line;
    int panelsRead = 0;

    while (std::getline (in, line))
    {
        std::istringstream words (line);
        std::string kind;

        if (! (words >> kind))
            continue;

        if (kind == "panel")
        {
            std::string id;
            int visible = 0, maximised = 0, detached = 0;
            double x = 0, y = 0, w = 0, h = 0;
            Rect window;

            if (! (words >> id >> visible >> maximised >> detached >> x >> y >> w >> h >> window.x >> window.y >> window.w >> window.h))
                return std::nullopt;

            const auto panel = panelFromId (id);
            if (! panel || ! std::isfinite (x + y + w + h))
                return std::nullopt;

            auto& p = layout[*panel];
            p.visible = visible != 0;
            p.maximised = maximised != 0;
            p.detached = detached != 0;
            p.windowBounds = window;
            p.bounds = clampInto ({ (int) std::lround (x * layout.width), (int) std::lround (y * layout.height),
                                    (int) std::lround (w * layout.width), (int) std::lround (h * layout.height) },
                                  layout.width, layout.height);
            ++panelsRead;
        }
        else if (kind == "order")
        {
            std::vector<Panel> order;
            std::string id;

            while (words >> id)
                if (const auto p = panelFromId (id); p && std::find (order.begin(), order.end(), *p) == order.end())
                    order.push_back (*p);

            for (int i = 0; i < panelCount; ++i) // any missing panel goes to the back
                if (std::find (order.begin(), order.end(), (Panel) i) == order.end())
                    order.insert (order.begin(), (Panel) i);

            layout.order = order;
        }
        else if (kind == "focus")
        {
            std::string id;
            if (words >> id)
                if (const auto p = panelFromId (id))
                    layout.focused = *p;
        }
    }

    if (panelsRead == 0)
        return std::nullopt;

    // A layout saved by 0.1.21 or earlier could have the Browser (at the left edge) wider than where the Playlist
    // starts, covering its pattern picker (a minimum width applied while the window was still small, then scaled):
    // the Playlist starts where the Browser ends again.
    {
        const auto& b = layout[Panel::browser].bounds;
        const auto& p = layout[Panel::playlist].bounds;
        if (b.x == 0 && p.x > 0 && p.x < b.right() && p.right() >= layout.width)
            layout.redockPlaylist();
    }

    return layout;
}

Rect snap (Rect r, const std::vector<Rect>& others, int desktopWidth, int desktopHeight, int threshold, int edges)
{
    std::vector<int> xs { 0, desktopWidth }, ys { 0, desktopHeight };

    for (const auto& o : others)
    {
        // only edges the moving rect could meet (overlapping on the other axis, give or take the threshold)
        const auto overlapsVertically = r.y < o.bottom() + threshold && r.bottom() > o.y - threshold;
        const auto overlapsHorizontally = r.x < o.right() + threshold && r.right() > o.x - threshold;

        if (overlapsVertically)
        {
            xs.push_back (o.x);
            xs.push_back (o.right());
        }

        if (overlapsHorizontally)
        {
            ys.push_back (o.y);
            ys.push_back (o.bottom());
        }
    }

    auto nearest = [threshold] (int value, const std::vector<int>& lines, int& best) {
        int bestDistance = threshold + 1;
        for (auto line : lines)
            if (std::abs (line - value) < bestDistance)
            {
                bestDistance = std::abs (line - value);
                best = line;
            }
        return bestDistance <= threshold ? bestDistance : -1;
    };

    if (edges == 0) // move: the closer of the two edges on each axis wins
    {
        int a = 0, b = 0;
        const auto da = nearest (r.x, xs, a), db = nearest (r.right(), xs, b);
        if (da >= 0 && (db < 0 || da <= db))
            r.x = a;
        else if (db >= 0)
            r.x = b - r.w;

        const auto ta = nearest (r.y, ys, a), tb = nearest (r.bottom(), ys, b);
        if (ta >= 0 && (tb < 0 || ta <= tb))
            r.y = a;
        else if (tb >= 0)
            r.y = b - r.h;

        return r;
    }

    int line = 0;
    if ((edges & left) && nearest (r.x, xs, line) >= 0)
    {
        r.w += r.x - line;
        r.x = line;
    }
    if ((edges & right) && nearest (r.right(), xs, line) >= 0)
        r.w = line - r.x;
    if ((edges & top) && nearest (r.y, ys, line) >= 0)
    {
        r.h += r.y - line;
        r.y = line;
    }
    if ((edges & bottom) && nearest (r.bottom(), ys, line) >= 0)
        r.h = line - r.y;

    return r;
}
} // namespace lynx::workspace
