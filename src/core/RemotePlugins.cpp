#include "core/RemotePlugins.h"

#include <algorithm>
#include <cmath>

#include "core/StandIns.h"

namespace lynx::remoteplug
{
namespace
{
// at most `max` bytes, never cutting a UTF-8 sequence, control characters out
void cutText (std::string& s, std::size_t max = maxText)
{
    s.erase (std::remove_if (s.begin(), s.end(), [] (char c) { return (unsigned char) c < 0x20 || c == 0x7f; }), s.end());
    if (s.size() <= max)
        return;
    auto end = max;
    while (end > 0 && ((unsigned char) s[end] & 0xC0) == 0x80)
        --end;
    s.resize (end);
}

float unit (float v) noexcept
{
    return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;
}

template <typename SongT, typename Fn>
void visit (SongT& song, const Fn& fn)
{
    for (auto& c : song.channels)
        if (c.instrument == InstrumentKind::plugin)
            fn (c.plugin, Place { { live::Kind::channel, c.uid, 0 }, -1 });
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (song.mixer[t].effects[s].kind == EffectKind::plugin)
                fn (song.mixer[t].effects[s].plugin, Place { { live::Kind::insert, (std::uint64_t) t, 0 }, (int) s });
}

template <typename SongT>
auto* refOf (SongT& song, const Place& place)
{
    decltype (&song.channels.front().plugin) found = nullptr;
    if (place.part.kind == live::Kind::channel && place.slot < 0)
    {
        for (auto& c : song.channels)
            if (c.uid == place.part.a && c.uid != 0 && c.instrument == InstrumentKind::plugin)
                found = &c.plugin;
    }
    else if (place.part.kind == live::Kind::insert && place.part.a < song.mixer.size() && place.slot >= 0)
    {
        auto& effects = song.mixer[(std::size_t) place.part.a].effects;
        if ((std::size_t) place.slot < effects.size() && effects[(std::size_t) place.slot].kind == EffectKind::plugin)
            found = &effects[(std::size_t) place.slot].plugin;
    }
    return found;
}
} // namespace

void clean (Shared& shared)
{
    cutText (shared.id, 512);
    cutText (shared.name);
    cutText (shared.vendor);
    cutText (shared.format, 16);
    if (shared.params.size() > maxParams)
        shared.params.resize (maxParams);
    std::set<int> seen;
    std::vector<Param> kept;
    kept.reserve (shared.params.size());
    for (auto& p : shared.params)
    {
        if (p.index < 0 || p.index >= 1 << 20 || ! seen.insert (p.index).second)
            continue;
        cutText (p.name);
        cutText (p.unit, 16);
        cutText (p.low, 32);
        cutText (p.high, 32);
        p.def = unit (p.def);
        p.steps = p.steps < 2 ? 0 : std::min (p.steps, 1 << 16);
        if (p.steps == 0 || p.steps > maxSteps || (int) p.stepNames.size() != p.steps)
            p.stepNames.clear();
        for (auto& n : p.stepNames)
            cutText (n, 32);
        kept.push_back (std::move (p));
    }
    shared.params = std::move (kept);
}

void clean (std::vector<Shared>& list)
{
    if (list.size() > maxShared)
        list.resize (maxShared);
    std::set<std::string> seen;
    std::vector<Shared> kept;
    for (auto& s : list)
    {
        clean (s);
        if (! s.id.empty() && seen.insert (s.id).second)
            kept.push_back (std::move (s));
    }
    list = std::move (kept);
}

const Shared* find (const std::vector<Shared>& list, const std::string& id) noexcept
{
    for (const auto& s : list)
        if (s.id == id)
            return &s;
    return nullptr;
}

float valueOf (const PluginRef& ref, int index, float fallback) noexcept
{
    for (const auto& [i, v] : ref.values)
        if (i == index)
            return v;
    return fallback;
}

bool setValue (PluginRef& ref, int index, float value)
{
    if (index < 0 || index >= 1 << 20)
        return false;
    value = unit (value);
    auto it = std::lower_bound (ref.values.begin(), ref.values.end(), index, [] (const auto& p, int i) { return p.first < i; });
    if (it != ref.values.end() && it->first == index)
    {
        if (it->second == value)
            return false;
        it->second = value;
        return true;
    }
    if (ref.values.size() >= maxParams)
        return false;
    ref.values.insert (it, { index, value });
    return true;
}

void cleanValues (PluginRef& ref)
{
    std::map<int, float> byIndex;
    for (const auto& [i, v] : ref.values)
        if (i >= 0 && i < 1 << 20 && byIndex.size() < maxParams)
            byIndex[i] = unit (v); // (the last one of an index wins)
    ref.values.assign (byIndex.begin(), byIndex.end());
}

PluginRef makeRef (const Shared& shared, std::uint32_t playedBy)
{
    PluginRef ref;
    ref.id = shared.id;
    ref.name = shared.name;
    ref.playedBy = playedBy;
    return ref;
}

void forEachRef (Song& song, const std::function<void (PluginRef&, const Place&)>& fn)
{
    visit (song, fn);
}

void forEachRef (const Song& song, const std::function<void (const PluginRef&, const Place&)>& fn)
{
    visit (song, fn);
}

PluginRef* refAt (Song& song, const Place& place)
{
    return refOf (song, place);
}

const PluginRef* refAt (const Song& song, const Place& place)
{
    return refOf (song, place);
}

std::vector<Use> uses (const Song& song, std::uint32_t by)
{
    std::vector<Use> out;
    forEachRef (song, [&] (const PluginRef& ref, const Place& place) {
        if (by != 0 && ref.playedBy == by)
            out.push_back ({ place, live::ownerOf (place.part, song), ref.id });
    });
    return out;
}

void sanitize (Song& carrier, std::uint32_t hostTag)
{
    forEachRef (carrier, [&] (PluginRef& ref, const Place&) {
        if (ref.playedBy == 0)
        {
            ref.values.clear(); // (values only mean something on a hosted plugin)
            return;
        }
        ref.playedBy = hostTag;
        ref.state.clear();
        ref.foreign = {};
        cleanValues (ref);
    });
}

int valueEdits (const Song& confirmed, const live::Patch& patch)
{
    int edits = 0;
    std::set<live::Key> changed (patch.changed.begin(), patch.changed.end());
    forEachRef (patch.carrier, [&] (const PluginRef& ref, const Place& place) {
        if (ref.playedBy == 0 || changed.count (place.part) == 0)
            return;
        const auto* old = refAt (confirmed, place);
        if (old == nullptr || old->id != ref.id)
        {
            edits += (int) ref.values.size(); // (a new hosted plugin: what it starts with)
            return;
        }
        for (const auto& [i, v] : ref.values)
            if (valueOf (*old, i, -1.0f) != v)
                ++edits;
    });
    return edits;
}

bool Limiter::allow (std::uint32_t tag, double nowMs, int edits)
{
    if (edits <= 0)
        return true;
    auto& b = buckets[tag];
    if (! b.used)
        b = { capacity, nowMs, true };
    b.tokens = std::min (capacity, b.tokens + std::max (0.0, nowMs - b.atMs) / 1000.0 * rate);
    b.atMs = nowMs;
    if ((double) edits > b.tokens)
        return false;
    b.tokens -= (double) edits;
    return true;
}

void Outbox::set (int index, float value, double)
{
    wanted[index] = { unit (value), -1.0, 0 };
}

std::optional<float> Outbox::showing (int index) const
{
    if (auto it = wanted.find (index); it != wanted.end())
        return it->second.value;
    return std::nullopt;
}

std::vector<std::pair<int, float>> Outbox::due (const PluginRef& ref, double nowMs)
{
    std::vector<std::pair<int, float>> out;
    for (auto it = wanted.begin(); it != wanted.end();)
    {
        auto& e = it->second;
        const bool arrived = e.writtenMs >= 0.0 && valueOf (ref, it->first, -1.0f) == e.value;
        // (arrived and not undone for a while: done; tried enough: the session's value stands)
        if ((arrived && nowMs - e.writtenMs >= retry * 2.0) || (! arrived && e.tries >= maxTries && nowMs - e.writtenMs >= retry))
            it = wanted.erase (it);
        else
            ++it;
    }
    if (nowMs - lastWriteMs < interval)
        return out;
    for (auto& [index, e] : wanted)
    {
        if (valueOf (ref, index, -1.0f) == e.value)
            continue;
        if (e.writtenMs >= 0.0 && nowMs - e.writtenMs < retry)
            continue; // (written a moment ago: the host's answer may still be on its way)
        out.push_back ({ index, e.value });
        e.writtenMs = nowMs;
        ++e.tries;
    }
    if (! out.empty())
        lastWriteMs = nowMs;
    return out;
}

std::vector<live::Key> toRender (const Song& song, const std::set<live::Key>& needed, std::uint32_t me,
                                 const std::function<bool (const PluginRef&)>& missing, const std::function<bool (const std::string& id)>& shared)
{
    std::vector<live::Key> out;
    if (me == 0)
        return out;
    const auto here = live::unplayable (song, missing);
    for (const auto& k : needed)
    {
        if (here.count (k) > 0 || live::ownerOf (k, song) == me)
            continue; // (a plugin of it is not here, or it is this participant's own: live::toRender)
        std::vector<const PluginRef*> refs;
        if (k.kind == live::Kind::channel)
        {
            for (const auto& c : song.channels)
                if (c.uid == k.a && c.uid != 0)
                    standins::pluginsOf (c, refs);
        }
        else if (k.kind == live::Kind::insert && k.a > 0 && k.a < song.mixer.size())
            standins::pluginsOf (song.mixer[(std::size_t) k.a], refs);
        bool mine = false, allShared = true;
        for (const auto* r : refs)
            if (r->playedBy == me)
            {
                mine = true;
                allShared = allShared && shared (r->id);
            }
        if (mine && allShared)
            out.push_back (k);
    }
    return out;
}

void endSession (Song& song, bool values)
{
    forEachRef (song, [&] (PluginRef& ref, const Place&) {
        ref.playedBy = 0;
        if (values)
            ref.values.clear();
    });
}
} // namespace lynx::remoteplug
