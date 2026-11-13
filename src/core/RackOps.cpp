#include "core/RackOps.h"

#include "core/MidiControls.h"
#include "core/PatternBlocks.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <functional>
#include <numeric>

namespace lynx
{
const std::array<std::uint32_t, 12>& lynxPalette()
{
    static const std::array<std::uint32_t, 12> palette { 0xffd94a4fu, 0xffe07a3fu, 0xffd9b23cu, 0xff8fbf45u, 0xff3fae6au, 0xff35a8a0u,
                                                         0xff3d8fd1u, 0xff5b6ee0u, 0xff8a5cd6u, 0xffc25bb0u, 0xffd9668cu, 0xff7d8794u };
    return palette;
}

std::uint32_t randomPaletteColour (std::uint32_t current, std::uint32_t seed)
{
    const auto& palette = lynxPalette();
    // xorshift so consecutive seeds don't walk the palette in order
    auto x = seed * 2654435761u + 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    auto index = x % (std::uint32_t) palette.size();

    if (palette[index] == current)
        index = (index + 1 + (x >> 8) % (std::uint32_t) (palette.size() - 1)) % (std::uint32_t) palette.size();

    return palette[index];
}

// --- patterns ----------------------------------------------------------------------------------

bool isPatternEmpty (const Pattern& pattern) noexcept
{
    return pattern.notes.empty();
}

int stepPattern (const Song& song, int delta) noexcept
{
    if (song.patterns.empty())
        return 0;

    return std::clamp (song.currentPattern + delta, 0, (int) song.patterns.size() - 1);
}

int findNextEmptyPattern (const Song& song, int from) noexcept
{
    const auto count = (int) song.patterns.size();

    for (int i = 1; i < count; ++i)
    {
        const auto index = ((from + i) % count + count) % count;

        if (isPatternEmpty (song.patterns[(std::size_t) index]))
            return index;
    }

    return -1;
}

std::string newPatternName (const Song& song)
{
    for (int n = 1;; ++n)
    {
        const auto name = "Pattern " + std::to_string (n);

        if (std::none_of (song.patterns.begin(), song.patterns.end(), [&] (const Pattern& p) { return p.name == name; }))
            return name;
    }
}

int gotoNextEmptyPattern (Song& song)
{
    // the current pattern counts when it is empty and nothing else is
    auto index = findNextEmptyPattern (song, song.currentPattern);

    if (index < 0)
    {
        if (! song.patterns.empty() && isPatternEmpty (song.activePattern()))
            return song.currentPattern;

        Pattern p;
        p.name = newPatternName (song);
        song.patterns.push_back (std::move (p));
        index = (int) song.patterns.size() - 1;
    }

    song.currentPattern = index;
    return index;
}

int gotoFirstEmptyPattern (Song& song)
{
    for (int i = 0; i < (int) song.patterns.size(); ++i)
        if (isPatternEmpty (song.patterns[(std::size_t) i]))
        {
            song.currentPattern = i;
            return i;
        }

    Pattern p;
    p.name = newPatternName (song);
    song.patterns.push_back (std::move (p));
    song.currentPattern = (int) song.patterns.size() - 1;
    return song.currentPattern;
}

namespace
{
// Shifts pattern clip references at or after `index` by `delta`.
void shiftPatternClips (Song& song, int index, int delta)
{
    song.forEachClipList ([index, delta] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= index)
                c.pattern += delta;
    });
    patternblocks::shiftPatterns (song, index, delta); // (round 170f)
}
} // namespace

int insertPattern (Song& song, int index, const std::string& name)
{
    index = std::clamp (index, 0, (int) song.patterns.size());
    shiftPatternClips (song, index, 1);
    Pattern p;
    p.name = name;
    song.patterns.insert (song.patterns.begin() + index, std::move (p));
    song.currentPattern = index;
    return index;
}

int splitPatternByChannel (Song& song, int index)
{
    if (index < 0 || index >= (int) song.patterns.size())
        return 0;

    const auto source = song.patterns[(std::size_t) index];
    std::vector<int> channels;

    for (const auto& n : source.notes)
        if (std::find (channels.begin(), channels.end(), n.channel) == channels.end())
            channels.push_back (n.channel);

    std::sort (channels.begin(), channels.end());
    std::vector<Pattern> created;

    for (auto channel : channels)
    {
        Pattern p;
        const auto channelName = channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].name
                                                                                         : "Channel " + std::to_string (channel + 1);
        p.name = source.name + " - " + channelName;
        p.lengthTicks = source.lengthTicks != 0 ? source.lengthTicks : source.effectiveLength (song.meter());
        p.colour = channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].colour : source.colour;

        for (const auto& n : source.notes)
            if (n.channel == channel)
                p.notes.push_back (n);
        p.loopOn = source.loopOn; // (the channel keeps its loop point)
        p.loopMode = source.loopMode;
        for (const auto& l : source.channelLoops)
            if (l.channel == channel)
                p.channelLoops.push_back (l);

        created.push_back (std::move (p));
    }

    if (created.empty())
        return 0;

    shiftPatternClips (song, index + 1, (int) created.size());
    song.patterns.insert (song.patterns.begin() + index + 1, created.begin(), created.end());

    if (song.currentPattern > index)
        song.currentPattern += (int) created.size();

    return (int) created.size();
}

namespace
{
std::string nextCopyName (const std::string& base, const std::function<bool (const std::string&)>& taken)
{
    for (int n = 2;; ++n)
    {
        const auto name = base + " #" + std::to_string (n);
        if (! taken (name))
            return name;
    }
}
} // namespace

int clonePattern (Song& song, int index)
{
    if (index < 0 || index >= (int) song.patterns.size())
        return -1;

    auto copy = song.patterns[(std::size_t) index];
    copy.name = nextCopyName (copy.name, [&song] (const std::string& name) {
        return std::any_of (song.patterns.begin(), song.patterns.end(), [&] (const Pattern& p) { return p.name == name; });
    });
    shiftPatternClips (song, index + 1, 1);
    song.patterns.insert (song.patterns.begin() + index + 1, std::move (copy));
    song.currentPattern = index + 1;
    return index + 1;
}

bool movePattern (Song& song, int index, int delta)
{
    const auto other = index + (delta < 0 ? -1 : 1);

    if (delta == 0 || index < 0 || index >= (int) song.patterns.size() || other < 0 || other >= (int) song.patterns.size())
        return false;

    std::swap (song.patterns[(std::size_t) index], song.patterns[(std::size_t) other]);
    song.forEachClipList ([index, other] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::pattern)
                c.pattern = c.pattern == index ? other : c.pattern == other ? index : c.pattern;
    });
    patternblocks::swapPatterns (song, index, other); // (round 170f)

    if (song.currentPattern == index)
        song.currentPattern = other;
    else if (song.currentPattern == other)
        song.currentPattern = index;

    return true;
}

// --- pattern hygiene (round 177d) ------------------------------------------------------------------

std::vector<int> unusedPatterns (const Song& song)
{
    std::vector<char> used (song.patterns.size(), 0);
    song.forEachClipList ([&used] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) used.size())
                used[(std::size_t) c.pattern] = 1;
    });
    song.forEachBlockList ([&used] (const std::vector<PatternBlock>& list) {
        for (const auto& b : list)
            if (b.pattern >= 0 && b.pattern < (int) used.size())
                used[(std::size_t) b.pattern] = 1;
    });
    for (const auto& c : song.channels) // a Loop Starter channel plays from its pattern
        if (c.loopStarterPattern >= 0 && c.loopStarterPattern < (int) used.size())
            used[(std::size_t) c.loopStarterPattern] = 1;
    std::vector<int> out;
    for (int p = 0; p < (int) used.size(); ++p)
        if (! used[(std::size_t) p])
            out.push_back (p);
    return out;
}

int deletePatterns (Song& song, std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    int removed = 0;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) // (from the last: the earlier indices stay valid)
        if (*it >= 0 && *it < (int) song.patterns.size() && song.patterns.size() > 1)
        {
            song.removePattern (*it);
            ++removed;
        }
    return removed;
}

void reorderPatterns (Song& song, const std::vector<int>& order)
{
    const auto n = (int) song.patterns.size();
    if ((int) order.size() != n)
        return;
    std::vector<int> where ((std::size_t) n, -1); // old index -> new index
    for (int k = 0; k < n; ++k)
    {
        const auto old = order[(std::size_t) k];
        if (old < 0 || old >= n || where[(std::size_t) old] >= 0)
            return; // not a permutation
        where[(std::size_t) old] = k;
    }
    std::vector<Pattern> moved;
    moved.reserve ((std::size_t) n);
    for (const auto old : order)
        moved.push_back (std::move (song.patterns[(std::size_t) old]));
    song.patterns = std::move (moved);
    const auto remap = [&where, n] (int p) { return p >= 0 && p < n ? where[(std::size_t) p] : p; };
    song.forEachClipList ([&remap] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::pattern)
                c.pattern = remap (c.pattern);
    });
    song.forEachBlockList ([&remap, n] (std::vector<PatternBlock>& list) {
        for (auto& b : list)
            b.pattern = remap (b.pattern);
        patternblocks::normalize (list, n);
    });
    for (auto& c : song.channels)
        if (c.loopStarterPattern >= 0)
            c.loopStarterPattern = remap (c.loopStarterPattern);
    song.currentPattern = remap (song.currentPattern);
}

namespace
{
std::string lowered (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

// a pattern's colour as the Picker shows it (automatic colours follow the index): hue, then lightness
std::pair<int, int> colourKey (std::uint32_t argb)
{
    const auto r = (int) ((argb >> 16) & 0xff), g = (int) ((argb >> 8) & 0xff), b = (int) (argb & 0xff);
    const auto hi = std::max ({ r, g, b }), lo = std::min ({ r, g, b });
    int hue = 0;
    if (hi != lo)
    {
        const auto d = (double) (hi - lo);
        double h = hi == r ? (g - b) / d : hi == g ? 2.0 + (b - r) / d : 4.0 + (r - g) / d;
        hue = (int) std::lround (h * 60.0);
        if (hue < 0)
            hue += 360;
    }
    return { hi == lo ? 360 + 1 : hue, hi + lo }; // (greys after the colours)
}

bool sameContent (const Pattern& a, const Pattern& b)
{
    return a.notes == b.notes && a.lengthTicks == b.lengthTicks && a.events == b.events && a.loopOn == b.loopOn && a.loopMode == b.loopMode
           && a.channelLoops == b.channelLoops && a.markers == b.markers && a.loopStarterGenre == b.loopStarterGenre;
}
} // namespace

void sortPatterns (Song& song, PatternSort by)
{
    std::vector<int> order (song.patterns.size());
    for (int i = 0; i < (int) order.size(); ++i)
        order[(std::size_t) i] = i;
    if (by == PatternSort::name)
    {
        std::vector<std::string> keys;
        for (const auto& p : song.patterns)
            keys.push_back (lowered (p.name));
        std::stable_sort (order.begin(), order.end(), [&keys] (int a, int b) { return keys[(std::size_t) a] < keys[(std::size_t) b]; });
    }
    else
    {
        // (a pattern without its own colour: the automatic one; those keep their order, after the coloured ones)
        std::stable_sort (order.begin(), order.end(), [&song] (int a, int b) {
            const auto ca = song.patterns[(std::size_t) a].colour, cb = song.patterns[(std::size_t) b].colour;
            if ((ca == 0) != (cb == 0))
                return cb == 0;
            return ca != 0 && colourKey (ca) < colourKey (cb);
        });
    }
    reorderPatterns (song, order);
}

std::vector<std::vector<int>> identicalPatterns (const Song& song)
{
    std::vector<std::vector<int>> groups;
    std::vector<char> grouped (song.patterns.size(), 0);
    for (int a = 0; a < (int) song.patterns.size(); ++a)
    {
        if (grouped[(std::size_t) a] || (song.patterns[(std::size_t) a].notes.empty() && song.patterns[(std::size_t) a].events.empty()))
            continue; // (empty patterns are not duplicates of each other: they are where new parts get written)
        std::vector<int> group { a };
        for (int b = a + 1; b < (int) song.patterns.size(); ++b)
            if (! grouped[(std::size_t) b] && sameContent (song.patterns[(std::size_t) a], song.patterns[(std::size_t) b]))
            {
                group.push_back (b);
                grouped[(std::size_t) b] = 1;
            }
        if (group.size() > 1)
            groups.push_back (std::move (group));
    }
    return groups;
}

int mergeIdenticalPatterns (Song& song)
{
    const auto groups = identicalPatterns (song);
    if (groups.empty())
        return 0;
    std::vector<int> target (song.patterns.size());
    for (int i = 0; i < (int) target.size(); ++i)
        target[(std::size_t) i] = i;
    std::vector<int> duplicates;
    for (const auto& g : groups)
        for (std::size_t k = 1; k < g.size(); ++k)
        {
            target[(std::size_t) g[k]] = g.front();
            duplicates.push_back (g[k]);
        }
    song.forEachClipList ([&target] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) target.size())
                c.pattern = target[(std::size_t) c.pattern];
    });
    const auto count = (int) target.size();
    song.forEachBlockList ([&target, count] (std::vector<PatternBlock>& list) {
        for (auto& b : list)
            if (b.pattern >= 0 && b.pattern < (int) target.size())
                b.pattern = target[(std::size_t) b.pattern];
        patternblocks::normalize (list, count);
    });
    for (auto& c : song.channels)
        if (c.loopStarterPattern >= 0 && c.loopStarterPattern < count)
            c.loopStarterPattern = target[(std::size_t) c.loopStarterPattern];
    if (song.currentPattern >= 0 && song.currentPattern < count)
        song.currentPattern = target[(std::size_t) song.currentPattern];
    return deletePatterns (song, duplicates);
}

// --- step sequencer ----------------------------------------------------------------------------

int patternSteps (const Song& song, const Pattern& pattern) noexcept
{
    return std::clamp ((int) (pattern.effectiveLength (song.meter()) / ticksPerStep), 16, 512);
}

bool showsNotePreview (const Pattern& pattern, int channel) noexcept
{
    return std::any_of (pattern.notes.begin(), pattern.notes.end(), [channel] (const Note& n) {
        return n.channel == channel && (n.start % ticksPerStep != 0 || n.length > ticksPerStep || n.key != 60 || n.slide || n.porta);
    });
}

bool showsNotePreview (const Pattern& pattern, int channel, bool pianoRollView) noexcept
{
    return pianoRollView || showsNotePreview (pattern, channel);
}

std::pair<int, int> channelKeyRange (const Pattern& pattern, int channel) noexcept
{
    auto low = 128, high = -1;

    for (const auto& n : pattern.notes)
        if (n.channel == channel)
        {
            low = std::min (low, n.key);
            high = std::max (high, n.key);
        }

    return high < 0 ? std::pair { 60, 60 } : std::pair { low, high };
}

std::vector<bool> euclideanRhythm (int pulses, int steps, int rotation)
{
    if (steps <= 0)
        return {};

    pulses = std::clamp (pulses, 0, steps);
    std::vector<bool> out ((std::size_t) steps, false);

    // step i is an onset when the running count floor(i * pulses / steps) goes up there: the onsets
    // are spread as evenly as integers allow, and the first step is always one
    for (int i = 0; i < steps; ++i)
        out[(std::size_t) i] = pulses > 0 && (i * pulses) % steps < pulses;

    rotation = ((rotation % steps) + steps) % steps;
    std::rotate (out.rbegin(), out.rbegin() + rotation, out.rend());
    return out;
}

void fillEverySteps (Pattern& pattern, int channel, int every, int steps, int offset)
{
    every = std::max (1, every);

    for (int s = std::max (0, offset); s < steps; s += every)
        if (! pattern.hasStep (channel, s))
            pattern.toggleStep (channel, s);
}

std::int64_t lengthWithStep (std::int64_t lengthTicks, int step, std::int64_t ticksPerStep, std::int64_t barTicks) noexcept
{
    const auto end = (std::int64_t) (step + 1) * ticksPerStep;
    if (lengthTicks <= 0 || step < 0 || end <= lengthTicks || barTicks <= 0)
        return lengthTicks;
    return (end + barTicks - 1) / barTicks * barTicks;
}

void setStepRhythm (Pattern& pattern, int channel, const std::vector<bool>& rhythm, int steps)
{
    const auto end = (std::int64_t) steps * ticksPerStep;
    auto& notes = pattern.notes;
    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [&] (const Note& n) { return n.channel == channel && n.start < end && n.start % ticksPerStep == 0; }),
                 notes.end());

    if (! rhythm.empty())
        for (int s = 0; s < steps; ++s)
            if (rhythm[(std::size_t) s % rhythm.size()])
            {
                Note n;
                n.start = (std::int64_t) s * ticksPerStep;
                n.channel = channel;
                notes.push_back (n);
            }

    pattern.sortNotes();
}

void rotateSteps (Pattern& pattern, int channel, int delta, int steps)
{
    if (steps <= 0)
        return;

    const auto length = (std::int64_t) steps * ticksPerStep;
    const auto shift = (std::int64_t) delta * ticksPerStep;

    for (auto& n : pattern.notes)
        if (n.channel == channel && n.start < length)
            n.start = ((n.start + shift) % length + length) % length;

    pattern.sortNotes();
}

bool hasPianoRollNotes (const Pattern& pattern) noexcept
{
    return std::any_of (pattern.notes.begin(), pattern.notes.end(), [] (const Note& n) {
        return n.start % ticksPerStep != 0 || n.key != 60 || n.length > ticksPerStep;
    });
}

std::vector<int> notesInStep (const Pattern& pattern, int channel, int step)
{
    const auto from = (std::int64_t) step * ticksPerStep;
    std::vector<int> out;

    for (int i = 0; i < (int) pattern.notes.size(); ++i)
    {
        const auto& n = pattern.notes[(std::size_t) i];
        if (n.channel == channel && n.start >= from && n.start < from + ticksPerStep)
            out.push_back (i);
    }

    return out;
}

// --- channel organisation ----------------------------------------------------------------------

std::vector<int> channelSortOrder (const Song& song, ChannelSort by)
{
    std::vector<int> order (song.channels.size());
    std::iota (order.begin(), order.end(), 0);

    auto lower = [] (std::string s) {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);
        return s;
    };

    // colour order: by hue, uncoloured channels last
    auto hue = [] (std::uint32_t argb) {
        if (argb == 0)
            return 1000.0;
        const auto r = ((argb >> 16) & 0xff) / 255.0, g = ((argb >> 8) & 0xff) / 255.0, b = (argb & 0xff) / 255.0;
        const auto mx = std::max ({ r, g, b }), mn = std::min ({ r, g, b }), d = mx - mn;
        if (d <= 1e-9)
            return 400.0 + mx; // greys after the colours
        double h = mx == r ? std::fmod ((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0;
        h = h < 0.0 ? h * 60.0 + 360.0 : h * 60.0;
        return h >= 345.0 ? h - 360.0 : h; // reds just below 360 sort with the other reds, first
    };

    const auto& c = song.channels;
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) {
        const auto& x = c[(std::size_t) a];
        const auto& y = c[(std::size_t) b];
        switch (by)
        {
            case ChannelSort::name: return lower (x.name) < lower (y.name);
            case ChannelSort::colour: return hue (x.colour) < hue (y.colour);
            case ChannelSort::insert: return (x.mixerTrack <= 0 ? 1 << 20 : x.mixerTrack) < (y.mixerTrack <= 0 ? 1 << 20 : y.mixerTrack);
            case ChannelSort::type: return (int) x.instrument < (int) y.instrument;
        }
        return false;
    });

    return order;
}

std::vector<int> unusedChannels (const Song& song)
{
    std::vector<char> used (song.channels.size(), 0);
    auto mark = [&used] (int channel) {
        if (channel >= 0 && channel < (int) used.size())
            used[(std::size_t) channel] = 1;
    };

    for (const auto& p : song.patterns)
        for (const auto& n : p.notes)
            mark (n.channel);

    auto markClips = [&mark] (const std::vector<Clip>& clips) {
        for (const auto& clip : clips)
            if (clip.kind == ClipKind::audio)
                mark (clip.channel);
    };
    markClips (song.playlist);
    for (const auto& a : song.arrangements)
        markClips (a.clips);

    for (const auto& a : song.automations)
        mark (a.source.channel);

    // a used Layer's children play its notes (round 164)
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        if (used[c] && song.channels[c].instrument == InstrumentKind::layer)
            for (const auto k : song.channels[c].layer.children)
                mark (k);

    std::vector<int> out;
    for (int i = 0; i < (int) used.size(); ++i)
        if (! used[(std::size_t) i])
            out.push_back (i);

    return out;
}

// --- channel selection -------------------------------------------------------------------------

bool ChannelSelection::contains (int row) const noexcept
{
    return std::binary_search (rows.begin(), rows.end(), row);
}

void ChannelSelection::selectOnly (int row)
{
    rows = { row };
    anchor = row;
}

void ChannelSelection::toggle (int row)
{
    if (const auto it = std::lower_bound (rows.begin(), rows.end(), row); it != rows.end() && *it == row)
    {
        if (rows.size() > 1) // one channel always stays selected (it is the keyboard/MIDI target)
            rows.erase (it);
    }
    else
        rows.insert (it, row);

    anchor = row;
}

void ChannelSelection::extendTo (int row)
{
    rows.clear();

    for (int r = std::min (anchor, row); r <= std::max (anchor, row); ++r)
        rows.push_back (r);
}

void ChannelSelection::selectAll (int count)
{
    rows.resize ((std::size_t) std::max (0, count));
    std::iota (rows.begin(), rows.end(), 0);
}

void ChannelSelection::clamp (int count)
{
    rows.erase (std::remove_if (rows.begin(), rows.end(), [count] (int r) { return r < 0 || r >= count; }), rows.end());

    if (rows.empty() && count > 0)
        rows = { std::clamp (anchor, 0, count - 1) };

    anchor = std::clamp (anchor, 0, std::max (0, count - 1));
}

// --- channel operations ------------------------------------------------------------------------

void permuteChannels (Song& song, const std::vector<int>& newOrder)
{
    const auto count = (int) song.channels.size();

    if ((int) newOrder.size() != count)
        return;

    std::vector<int> newIndexOf ((std::size_t) count, -1); // old -> new

    for (int i = 0; i < count; ++i)
    {
        const auto old = newOrder[(std::size_t) i];

        if (old < 0 || old >= count || newIndexOf[(std::size_t) old] >= 0)
            return; // not a permutation

        newIndexOf[(std::size_t) old] = i;
    }

    auto map = [&] (int channel) { return channel >= 0 && channel < count ? newIndexOf[(std::size_t) channel] : channel; };

    std::vector<ChannelDef> reordered;
    reordered.reserve ((std::size_t) count);

    for (auto old : newOrder)
        reordered.push_back (std::move (song.channels[(std::size_t) old]));

    song.channels = std::move (reordered);

    for (auto& p : song.patterns)
    {
        for (auto& n : p.notes)
            n.channel = map (n.channel);
        for (auto& l : p.channelLoops) // the Channel Rack's loop points (round 165f-rack)
            l.channel = map (l.channel);

        p.sortNotes();
    }

    song.forEachClipList ([&] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::audio)
                c.channel = map (c.channel);
    });

    song.forEachTarget ([&] (AutomationTarget& t) {
        if (t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
            || t.kind == AutomationTargetKind::channelParam || t.kind == AutomationTargetKind::channelAdjust)
            t.index = map (t.index);
    });

    for (auto& a : song.automations)
        if (a.source.channel >= 0)
            a.source.channel = map (a.source.channel);

    for (auto& c : song.channels) // Layer children (round 164)
        for (auto& k : c.layer.children)
            k = map (k);
}

bool moveChannels (Song& song, std::vector<int>& selection, int delta)
{
    const auto count = (int) song.channels.size();
    std::sort (selection.begin(), selection.end());
    selection.erase (std::unique (selection.begin(), selection.end()), selection.end());
    selection.erase (std::remove_if (selection.begin(), selection.end(), [count] (int r) { return r < 0 || r >= count; }), selection.end());

    if (selection.empty() || delta == 0 || (delta < 0 && selection.front() == 0) || (delta > 0 && selection.back() == count - 1))
        return false;

    std::vector<int> order ((std::size_t) count);
    std::iota (order.begin(), order.end(), 0);
    std::vector<char> selected ((std::size_t) count, 0);

    for (auto r : selection)
        selected[(std::size_t) r] = 1;

    // bubble each selected row one step, as a block (rows already blocked stay)
    if (delta < 0)
    {
        for (int i = 1; i < count; ++i)
            if (selected[(std::size_t) i] && ! selected[(std::size_t) i - 1])
            {
                std::swap (order[(std::size_t) i], order[(std::size_t) i - 1]);
                std::swap (selected[(std::size_t) i], selected[(std::size_t) i - 1]);
            }
    }
    else
    {
        for (int i = count - 2; i >= 0; --i)
            if (selected[(std::size_t) i] && ! selected[(std::size_t) i + 1])
            {
                std::swap (order[(std::size_t) i], order[(std::size_t) i + 1]);
                std::swap (selected[(std::size_t) i], selected[(std::size_t) i + 1]);
            }
    }

    permuteChannels (song, order);
    selection.clear();

    for (int i = 0; i < count; ++i)
        if (selected[(std::size_t) i])
            selection.push_back (i);

    return true;
}

int addChannelRouted (Song& song, ChannelDef channel, int insert)
{
    if (insert >= 0 && insert < (int) song.mixer.size())
    {
        // a generator dropped on a mixer strip or one of its effect slots (FL 2026, round 167u shots): the channel's
        // name is made unique ("3x Osc #2") and an insert that still has its default name takes it
        const auto taken = [&song] (const std::string& name) {
            return std::any_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) { return c.name == name; });
        };
        if (! channel.name.empty() && taken (channel.name))
            channel.name = nextCopyName (channel.name, taken);

        auto& track = song.mixer[(std::size_t) insert].name;
        if (! channel.name.empty() && (track.empty() || track == (insert == 0 ? std::string ("Master") : "Insert " + std::to_string (insert))))
            track = channel.name;

        channel.mixerTrack = insert;
    }
    else
        channel.mixerTrack = song.claimFreeInsert();

    song.channels.push_back (std::move (channel));
    return (int) song.channels.size() - 1;
}

int insertChannel (Song& song, int index, ChannelDef channel)
{
    const auto count = (int) song.channels.size();
    index = std::clamp (index, 0, count);
    song.channels.push_back (std::move (channel));

    // move the new last channel up to `index`
    std::vector<int> order;
    order.reserve ((std::size_t) count + 1);

    for (int i = 0; i < index; ++i)
        order.push_back (i);

    order.push_back (count);

    for (int i = index; i < count; ++i)
        order.push_back (i);

    permuteChannels (song, order);
    return index;
}

int cloneChannel (Song& song, int index)
{
    if (index < 0 || index >= (int) song.channels.size())
        return -1;

    auto copy = song.channels[(std::size_t) index];
    copy.freeze = {}; // (round 190ay: the clone plays its own instrument; the frozen audio is the original's)
    const auto base = copy.name;

    for (int n = 2;; ++n)
    {
        const auto name = base + " #" + std::to_string (n);

        if (std::none_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) { return c.name == name; }))
        {
            copy.name = name;
            break;
        }
    }

    return insertChannel (song, index + 1, std::move (copy));
}

int removeChannels (Song& song, std::vector<int> channels)
{
    std::sort (channels.begin(), channels.end());
    channels.erase (std::unique (channels.begin(), channels.end()), channels.end());
    int removed = 0;

    for (auto it = channels.rbegin(); it != channels.rend(); ++it)
        if (*it >= 0 && *it < (int) song.channels.size())
        {
            song.removeChannel (*it);
            ++removed;
        }

    return removed;
}

bool isInsertFree (const Song& song, int track) noexcept
{
    if (track <= 0 || track >= (int) song.mixer.size())
        return false;

    if (std::any_of (song.channels.begin(), song.channels.end(), [track] (const ChannelDef& c) { return c.mixerTrack == track; }))
        return false;

    if (! song.mixer[(std::size_t) track].effects.empty())
        return false;

    for (int t = 1; t < (int) song.mixer.size(); ++t)
        if (t != track && song.routes (t, track))
            return false;

    return true;
}

std::vector<int> sendChannelsToFreeInserts (Song& song, const std::vector<int>& channels)
{
    std::vector<int> result;

    if (song.mixer.empty())
        song.mixer = Song::defaultMixer (0);

    for (auto channel : channels)
    {
        if (channel < 0 || channel >= (int) song.channels.size())
        {
            result.push_back (0);
            continue;
        }

        int track = 0;

        for (int t = 1; t < (int) song.mixer.size() && track == 0; ++t)
            if (isInsertFree (song, t))
                track = t;

        if (track == 0 && (int) song.mixer.size() < maxMixerTracks)
        {
            song.mixer.emplace_back();
            track = (int) song.mixer.size() - 1;
        }

        if (track > 0)
        {
            auto& c = song.channels[(std::size_t) channel];
            c.mixerTrack = track;
            song.mixer[(std::size_t) track].name = c.name;
            song.mixer[(std::size_t) track].colour = c.colour;
        }

        result.push_back (track);
    }

    return result;
}

// --- Channel settings "Misc" tab ------------------------------------------------------------------------------

// FL's Misc › Polyphony exists on every generator channel (REA_rodada143 B2) and FL's channel layer applies it before
// the generator (REA_rodada168l): every LYNX channel that plays notes into a sound has it. The channels that make no
// sound of their own (controllers, MIDI Out, Layer, VFX, Dashboard, Voltage, Fruity Dance, Wasp's placeholder, the
// video player) and the audio clip channels have nothing to make mono.
bool channelHasMono (const ChannelDef& c) noexcept
{
    switch (c.instrument)
    {
        case InstrumentKind::audioClip:
        case InstrumentKind::controller:
        case InstrumentKind::midiOut:
        case InstrumentKind::layer:
        case InstrumentKind::dashboard:
        case InstrumentKind::voltage:
        case InstrumentKind::vfx:
        case InstrumentKind::videoplayer:
        case InstrumentKind::fruitydance:
        case InstrumentKind::wasp:
            return false;
        default:
            return true;
    }
}

bool channelHasPortamento (const ChannelDef& c) noexcept
{
    return channelHasMono (c);
}

bool channelMono (const ChannelDef& c) noexcept
{
    return c.instrument == InstrumentKind::synth ? c.synth.mono : channelHasMono (c) && c.misc.mono;
}

void setChannelMono (ChannelDef& c, bool on) noexcept
{
    // (every copy: the LYNX Synth's own, and the one FL's engines that take the channel's mono read, Sytrus / Ogun /
    // Fruit Kick's max polyphony)
    c.misc.mono = on;
    c.synth.mono = on;
}

bool channelPortamento (const ChannelDef& c) noexcept
{
    if (c.instrument == InstrumentKind::synth)
        return c.synth.porta;
    if (c.instrument == InstrumentKind::sampler)
        return c.sampler.porta;
    return channelHasPortamento (c) && c.misc.porta;
}

float channelPortamentoBeats (const ChannelDef& c) noexcept
{
    if (c.instrument == InstrumentKind::synth)
        return c.synth.portaBeats;
    if (c.instrument == InstrumentKind::sampler)
        return c.sampler.portaBeats;
    return c.misc.portaBeats;
}

void setChannelPortamento (ChannelDef& c, bool on, float beats) noexcept
{
    beats = std::clamp (beats, 0.0f, 4.0f);
    c.misc.porta = c.synth.porta = c.sampler.porta = on;
    c.misc.portaBeats = c.synth.portaBeats = c.sampler.portaBeats = beats;
}

bool channelCutsItself (const ChannelDef& c) noexcept
{
    // issue #68: one state for every "Cut itself" control (rack menu, sample panel, Misc page). FL has one: its "Cut self"
    // sets Cut and By to the same group (FLEngine_x64 CutItselfBtnClick 0xF67C60 → 0xFEAAF0: [0x27C] = a group,
    // [0x280] = [0x27C]); LYNX's sampler flag (projects saved by the sample panel before 0.1.24) counts too
    // (round 190fe: an FLM cut group shares Cut and By but spares the channel that plays — not "cut itself")
    return (c.cutGroup != 0 && c.cutGroup == c.cutBy && ! c.cutOthersOnly) || c.sampler.cut;
}

void setChannelCutsItself (ChannelDef& c, int channel, bool on) noexcept
{
    c.cutGroup = c.cutBy = on ? 1000 + channel : 0;
    c.cutOthersOnly = c.cutAtNoteEnd = false; // (round 190fe)
    c.sampler.cut = on; // (the sampler's own quick cut of its voices goes with it, as an imported FL "cut itself")
}

std::string cutGroupText (int group, int channel)
{
    if (group <= 0)
        return "--";
    if (group == 1000 + channel)
        return "self";
    return std::to_string (group);
}

std::string stepPositionText (std::int64_t tick, Meter meter)
{
    tick = std::max<std::int64_t> (0, tick);
    const auto barTicks = meter.bar();
    const auto bar = tick / barTicks + 1;
    const auto inBar = tick % barTicks;
    const auto step = inBar / ticksPerStep + 1;
    const auto flTick = (inBar % ticksPerStep) * 24 / ticksPerStep; // FL's 96 PPQ: 24 ticks per step
    char text[32];
    std::snprintf (text, sizeof (text), "%lld:%02lld:%02lld", (long long) bar, (long long) step, (long long) flTick);
    return text;
}

bool replaceGenerator (Song& song, int index, ChannelDef generator)
{
    if (index < 0 || index >= (int) song.channels.size())
        return false;
    auto& c = song.channels[(std::size_t) index];
    generator.mixerTrack = c.mixerTrack;
    generator.volume = c.volume;
    generator.pan = c.pan;
    generator.muted = c.muted;
    generator.group = c.group;
    generator.swingMix = c.swingMix;
    if (c.colour != 0)
        generator.colour = c.colour;
    c = std::move (generator);
    return true;
}
// --- Channel Rack Cut / Copy / Paste (REA round 170a §1) -------------------------------------------------------

ChannelStepsClipboard copyChannelSteps (const Pattern& pattern, const std::vector<int>& selected, int channelCount)
{
    ChannelStepsClipboard clip;
    int first = channelCount;
    for (auto c : selected)
        if (c >= 0 && c < channelCount)
            first = std::min (first, c);
    if (first >= channelCount)
        return clip;

    clip.slots.resize ((std::size_t) (channelCount - first));
    for (auto c : selected)
        if (c >= first && c < channelCount)
            clip.slots[(std::size_t) (c - first)].copied = true;

    for (const auto& n : pattern.notes)
        if (n.channel >= first && n.channel < channelCount && clip.slots[(std::size_t) (n.channel - first)].copied)
        {
            auto copy = n;
            copy.channel = 0;
            clip.slots[(std::size_t) (n.channel - first)].notes.push_back (copy);
        }

    for (const auto& l : pattern.channelLoops)
        if (l.channel >= first && l.channel < channelCount && clip.slots[(std::size_t) (l.channel - first)].copied)
        {
            auto& slot = clip.slots[(std::size_t) (l.channel - first)];
            slot.loops = true;
            slot.loopSetting = l.setting;
        }
    return clip;
}

bool clearChannelSteps (Pattern& pattern, const std::vector<int>& selected)
{
    const auto before = pattern.notes.size();
    pattern.notes.erase (std::remove_if (pattern.notes.begin(), pattern.notes.end(),
                                         [&selected] (const Note& n) { return std::find (selected.begin(), selected.end(), n.channel) != selected.end(); }),
                         pattern.notes.end());
    return pattern.notes.size() != before;
}

bool pasteChannelSteps (Pattern& pattern, const ChannelStepsClipboard& clipboard, const std::vector<int>& selected, int channelCount)
{
    int first = channelCount;
    for (auto c : selected)
        if (c >= 0 && c < channelCount)
            first = std::min (first, c);
    if (clipboard.empty() || first >= channelCount)
        return false;

    std::vector<int> targets;
    for (int k = 0; k < (int) clipboard.slots.size() && first + k < channelCount; ++k)
        if (clipboard.slots[(std::size_t) k].copied)
            targets.push_back (first + k);
    clearChannelSteps (pattern, targets);

    for (auto channel : targets)
    {
        const auto& slot = clipboard.slots[(std::size_t) (channel - first)];
        for (auto n : slot.notes)
        {
            n.channel = channel;
            pattern.notes.push_back (n);
        }
        auto& loops = pattern.channelLoops;
        loops.erase (std::remove_if (loops.begin(), loops.end(), [channel] (const ChannelLoop& l) { return l.channel == channel; }), loops.end());
        if (slot.loops)
            loops.push_back ({ channel, slot.loopSetting });
    }
    pattern.sortNotes();
    return true;
}

int stepPatternCreating (Song& song, int delta)
{
    if (song.patterns.empty())
        return 0;
    const auto last = (int) song.patterns.size() - 1;
    const auto target = song.currentPattern + delta;
    if (delta > 0 && target > last && ! isPatternEmpty (song.patterns[(std::size_t) last]))
    {
        Pattern p;
        p.name = newPatternName (song);
        song.patterns.push_back (std::move (p));
        return last + 1;
    }
    return std::clamp (target, 0, last);
}

int audioClipsAmong (const Song& song, const std::vector<int>& channels)
{
    int count = 0;
    for (const auto c : channels)
        if (c >= 0 && c < (int) song.channels.size() && song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
            ++count;
    return count;
}

int setDeclickOfAudioClips (Song& song, const std::vector<int>& channels, int mode)
{
    int count = 0;
    for (const auto c : channels)
        if (c >= 0 && c < (int) song.channels.size() && song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
        {
            song.channels[(std::size_t) c].sampler.declickMode = std::clamp (mode, 0, 5);
            ++count;
        }
    return count;
}

int setStretchOfAudioClips (Song& song, const std::vector<int>& channels, bool keepPitch, StretchMethod method)
{
    int count = 0;
    for (const auto c : channels)
        if (c >= 0 && c < (int) song.channels.size() && song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
        {
            auto& s = song.channels[(std::size_t) c].sampler;
            s.keepPitch = keepPitch;
            if (keepPitch)
                s.stretchMethod = method;
            s.moveBakedPitchToKnob(); // (outside Beats nothing renders a baked pitch; round 175a)
            ++count;
        }
    return count;
}

void prepareForMidiExport (Song& song)
{
    constexpr int drums = 9; // (MIDI channel 10)
    int next = 0;
    for (auto& c : song.channels)
    {
        // FL converts every channel but automation clips (seen in FL 2026, round 181b: Layer and Envelope
        // Controller became MIDI Out, the automation clip stayed and took no MIDI channel). LYNX's automation clips
        // aren't channels, so every channel converts.
        midictl::MidiOutSettings settings;
        if (c.plugin.name == "FPC" || c.instrument == InstrumentKind::fpc)
        {
            settings.channel = drums;
        }
        else
        {
            settings.channel = next;
            next = (next + 1) & 15;
            if (next == drums)
                ++next;
        }
        c.instrument = InstrumentKind::midiOut;
        c.plugin = {};
        c.pluginState.clear();
        midictl::writeMidiOut (c.pluginState, settings);
    }
}
} // namespace lynx
