#pragma once

// Round 190on (research190oe P10): stacked controller lanes under the Piano roll. FL Studio 2026 shows one event lane (its
// "Control" chooser; users ask for more: complaint F13 of research190oe), so in the Pattern layout this is a LYNX extra, off
// (Settings › LYNX extras); the Clip-launch layout has it on, as Live 12's clip editor shows several envelope lanes at once.
// A "+" beside the bottom lane's chooser adds a lane above it, each added lane has its own chooser (a note property or a
// pattern event lane) and an "×"; the line between two lanes drags their heights. The set is kept in ui-options.txt.
// No JUCE.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::stackedlanes
{
constexpr int maxAdded = 4;      // lanes above the bottom one (five in all)
constexpr int minHeight = 24;    // a lane never gets shorter (its chooser's row)
constexpr float minWeight = 0.2f, maxWeight = 5.0f;

struct Lane
{
    int property = 1;                         // a note property (the Piano roll's lane ids 1 … 8) when `target` is empty
    std::optional<AutomationTarget> target;   // a pattern event lane
    float weight = 1.0f;                      // its share of the event editor's height
    bool operator== (const Lane& o) const noexcept { return property == o.property && target == o.target && weight == o.weight; }
};

// Whether the "+" is offered: always in the Clip-launch layout, in the others when the extra is on.
constexpr bool enabled (bool clipLaunch, bool extraOn) noexcept { return clipLaunch || extraOn; }

// The heights of `weights.size()` lanes (the added ones, top to bottom, then the bottom lane) sharing `total` pixels with
// `gap` pixels between two lanes: in proportion to the weights, each at least minHeight while the room allows; they
// always add up to `total` minus the gaps (the last lane takes what rounding leaves).
inline std::vector<int> heights (const std::vector<float>& weights, int total, int gap = 1)
{
    std::vector<int> out (weights.size(), 0);
    if (weights.empty())
        return out;
    const auto room = std::max (0, total - gap * ((int) weights.size() - 1));
    double sum = 0.0;
    for (const auto w : weights)
        sum += std::clamp ((double) w, (double) minWeight, (double) maxWeight);
    const auto floorEach = room >= minHeight * (int) weights.size() ? minHeight : room / (int) weights.size();
    const auto spare = room - floorEach * (int) weights.size();
    int used = 0;
    for (std::size_t i = 0; i < weights.size(); ++i)
    {
        const auto share = std::clamp ((double) weights[i], (double) minWeight, (double) maxWeight) / sum;
        out[i] = floorEach + (int) std::lround (spare * share);
        used += out[i];
    }
    out.back() += room - used;
    return out;
}

// The line between lane `k` and lane `k + 1` dragged by `dy` pixels (down: lane k grows) in a pane `total` pixels tall
// (no gaps): the two weights change so that lane k's height follows the mouse, the others stay, neither of the two goes
// under minHeight.
inline void dragBoundary (std::vector<float>& weights, int total, int k, int dy)
{
    if (k < 0 || k + 1 >= (int) weights.size())
        return;
    const auto n = (int) weights.size();
    for (auto& w : weights)
        w = std::clamp (w, minWeight, maxWeight);
    const auto current = heights (weights, total, 0);
    const auto floorEach = total >= minHeight * n ? minHeight : total / n;
    const auto spare = total - floorEach * n;
    if (spare <= 0)
        return;
    double sum = 0.0;
    for (const auto w : weights)
        sum += w;
    const auto a = current[(std::size_t) k], b = current[(std::size_t) k + 1];
    const auto newA = std::clamp (a + dy, minHeight, std::max (minHeight, a + b - minHeight));
    const auto pair = weights[(std::size_t) k] + weights[(std::size_t) k + 1]; // (the sum stays: the other lanes keep their heights)
    const auto wantA = (float) ((double) (newA - floorEach) * sum / (double) spare);
    weights[(std::size_t) k] = std::clamp (wantA, minWeight, std::min (maxWeight, pair - minWeight));
    weights[(std::size_t) k + 1] = pair - weights[(std::size_t) k];
}

// ui-options.txt's line: "p1:1.00 e3.2.0.5:0.80" — p<property> or e<kind>.<index>.<slot>.<param>, then the weight; the
// bottom lane's weight last as "b:<weight>".
inline std::string toText (const std::vector<Lane>& added, float bottomWeight)
{
    std::ostringstream out;
    for (const auto& l : added)
    {
        if (l.target)
            out << 'e' << (int) l.target->kind << '.' << l.target->index << '.' << l.target->slot << '.' << l.target->param;
        else
            out << 'p' << l.property;
        out << ':' << std::round (l.weight * 100.0f) / 100.0f << ' ';
    }
    out << "b:" << std::round (bottomWeight * 100.0f) / 100.0f;
    return out.str();
}

inline std::vector<Lane> fromText (const std::string& text, float* bottomWeight = nullptr, int lastProperty = 8)
{
    std::vector<Lane> out;
    std::istringstream in (text);
    std::string token;
    while (in >> token)
    {
        const auto colon = token.find (':');
        if (colon == std::string::npos || colon == 0)
            continue;
        const auto weight = std::clamp ((float) std::atof (token.c_str() + colon + 1), minWeight, maxWeight);
        const auto head = token.substr (0, colon);
        if (head == "b")
        {
            if (bottomWeight != nullptr)
                *bottomWeight = weight;
            continue;
        }
        Lane l;
        l.weight = weight;
        if (head[0] == 'p')
        {
            l.property = std::atoi (head.c_str() + 1);
            if (l.property < 1 || l.property > lastProperty)
                continue;
        }
        else if (head[0] == 'e')
        {
            int v[4] = {};
            std::istringstream parts (head.substr (1));
            std::string part;
            int n = 0;
            while (n < 4 && std::getline (parts, part, '.'))
                v[n++] = std::atoi (part.c_str());
            if (n != 4 || v[0] <= 0)
                continue;
            l.target = AutomationTarget { (AutomationTargetKind) v[0], v[1], v[2], v[3] };
        }
        else
            continue;
        if ((int) out.size() < maxAdded)
            out.push_back (l);
    }
    return out;
}
} // namespace lynx::stackedlanes
