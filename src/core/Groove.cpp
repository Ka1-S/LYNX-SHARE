#include "core/Groove.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "core/ClipLauncher.h"
#include "core/NoteChance.h"
#include "core/XmlLite.h"

namespace lynx::groove
{
const char* baseName (GrooveBase base) noexcept
{
    switch (base)
    {
        case GrooveBase::quarter: return "1/4";
        case GrooveBase::eighth: return "1/8";
        case GrooveBase::eighthTriplet: return "1/8T";
        case GrooveBase::sixteenth: return "1/16";
        case GrooveBase::sixteenthTriplet: return "1/16T";
        case GrooveBase::thirtySecond: return "1/32";
    }
    return "1/16";
}

// --- the pool --------------------------------------------------------------------------------------------------------
const Groove* find (const GroovePool& pool, std::uint32_t id) noexcept
{
    if (id == 0)
        return nullptr;
    for (const auto& g : pool.grooves)
        if (g.id == id)
            return &g;
    return nullptr;
}

Groove* find (GroovePool& pool, std::uint32_t id) noexcept
{
    return const_cast<Groove*> (find (std::as_const (pool), id));
}

namespace
{
void tidy (Groove& g)
{
    g.base = (GrooveBase) std::clamp ((int) g.base, 0, (int) GrooveBase::thirtySecond);
    g.quantize = std::clamp (g.quantize, 0.0f, 1.0f);
    g.timing = std::clamp (g.timing, 0.0f, 1.0f);
    g.random = std::clamp (g.random, 0.0f, 1.0f);
    g.velocity = std::clamp (g.velocity, -1.0f, 1.0f);
    g.length = std::max<std::int64_t> (baseTicks (g.base), g.length);
    for (auto& p : g.points)
    {
        p.tick = std::clamp<std::int64_t> (p.tick, 0, g.length - 1);
        p.velocity = std::clamp (p.velocity, 0.0f, 1.0f);
    }
    std::stable_sort (g.points.begin(), g.points.end(), [] (const GroovePoint& a, const GroovePoint& b) { return a.tick < b.tick; });
}

// The template's point of the Base slot at grid position `grid` (already inside the template's length): the nearest
// point less than half a slot before it and at most half a slot after it, also across the template's wrap.
struct Slot
{
    bool found = false;
    double offset = 0.0;
    float accent = 1.0f;
};

Slot slotAt (const Groove& g, std::int64_t grid) noexcept
{
    Slot out;
    if (g.points.empty())
        return out;
    const auto half = (double) baseTicks (g.base) / 2.0;
    const auto length = (double) g.length;
    auto best = half + 1.0;
    const auto consider = [&] (const GroovePoint& p, double shift) {
        const auto d = (double) p.tick + shift - (double) grid;
        if (d > -half && d <= half && std::abs (d) < best)
        {
            best = std::abs (d);
            out = { true, d, p.velocity };
        }
    };
    const auto it = std::lower_bound (g.points.begin(), g.points.end(), grid, [] (const GroovePoint& p, std::int64_t t) { return p.tick < t; });
    // the points around the grid position (those of one slot are next to each other), and the template's ends
    for (auto i = it; i != g.points.end() && (double) i->tick - (double) grid <= half; ++i)
        consider (*i, 0.0);
    for (auto i = it; i != g.points.begin();)
    {
        --i;
        if ((double) grid - (double) i->tick >= half)
            break;
        consider (*i, 0.0);
    }
    if ((double) grid < half)
        for (auto i = g.points.end(); i != g.points.begin();)
        {
            --i;
            if (length - (double) i->tick >= half)
                break;
            consider (*i, -length);
        }
    if (length - (double) grid <= half)
        for (auto i = g.points.begin(); i != g.points.end() && (double) i->tick <= half; ++i)
            consider (*i, length);
    return out;
}

struct Range
{
    float low = 1.0f, high = 1.0f;
};
Range accentRange (const Groove& g) noexcept
{
    Range r { 1.0f, 0.0f };
    for (const auto& p : g.points)
    {
        r.low = std::min (r.low, p.velocity);
        r.high = std::max (r.high, p.velocity);
    }
    if (g.points.empty() || r.high <= 0.0f)
        return {};
    return r;
}

constexpr double randomSpan (std::int64_t base) noexcept { return (double) base / 4.0; } // ± at Random 100 %
} // namespace

std::uint32_t add (GroovePool& pool, Groove groove)
{
    std::uint32_t id = 1;
    for (const auto& g : pool.grooves)
        id = std::max (id, g.id + 1);
    groove.id = id;
    tidy (groove);
    pool.grooves.push_back (std::move (groove));
    return id;
}

bool remove (Song& song, std::uint32_t id)
{
    auto& list = song.grooves.grooves;
    const auto it = std::find_if (list.begin(), list.end(), [id] (const Groove& g) { return g.id == id; });
    if (id == 0 || it == list.end())
        return false;
    list.erase (it);
    if (song.clipGrid)
        for (auto& s : song.clipGrid->slots)
            if (s.groove == id)
                s.groove = 0;
    return true;
}

int users (const Song& song, std::uint32_t id) noexcept
{
    auto n = 0;
    if (id != 0 && song.clipGrid)
        for (const auto& c : song.clipGrid->clips)
            if (c.length > 0 && c.gridSlot >= 0 && c.gridSlot < (int) song.clipGrid->slots.size()
                && song.clipGrid->slots[(std::size_t) c.gridSlot].groove == id)
                ++n;
    return n;
}

const Groove* ofClip (const Song& song, const Clip& clip) noexcept
{
    if (clip.gridSlot < 0 || ! song.clipGrid || clip.gridSlot >= (int) song.clipGrid->slots.size() || song.grooves.grooves.empty())
        return nullptr;
    return find (song.grooves, song.clipGrid->slots[(std::size_t) clip.gridSlot].groove);
}

bool setClipGroove (Song& song, int track, int scene, std::uint32_t id)
{
    if (! song.clipGrid || (id != 0 && find (song.grooves, id) == nullptr))
        return false;
    auto* slot = launcher::settingsAt (*song.clipGrid, track, scene);
    if (slot == nullptr)
        return false;
    slot->groove = id;
    return true;
}

// --- the maths -------------------------------------------------------------------------------------------------------
Played apply (const Groove& g, float amount, std::int64_t start, float velocity, std::uint32_t bits) noexcept
{
    const auto base = (double) baseTicks (g.base);
    const auto scale = (double) std::clamp (amount, 0.0f, maxAmount);
    auto at = (double) start;

    // 1. straight quantisation to the Base
    const auto nearest = std::floor (at / base + 0.5) * base;
    at += (nearest - at) * (double) g.quantize;

    // 2. the template: the note takes the offset its Base slot has in the template
    const auto grid = (std::int64_t) std::llround (std::floor (at / base + 0.5) * base);
    const auto length = std::max<std::int64_t> (1, g.length);
    const auto slot = slotAt (g, ((grid % length) + length) % length);
    if (slot.found)
        at += slot.offset * (double) g.timing * scale;

    // 3. random timing
    if (g.random > 0.0f && scale > 0.0)
        at += ((double) bits / 4294967296.0 * 2.0 - 1.0) * randomSpan (baseTicks (g.base)) * (double) g.random * scale;

    Played out;
    out.start = std::max<std::int64_t> (0, (std::int64_t) std::llround (at));
    out.velocity = velocity;

    // 4. accents: the template's velocity against its loudest note; a negative amount mirrors the accents
    if (slot.found && g.velocity != 0.0f && scale > 0.0)
    {
        const auto range = accentRange (g);
        const auto level = (double) slot.accent / (double) range.high;
        const auto mirrored = ((double) range.low + (double) range.high - (double) slot.accent) / (double) range.high;
        const auto a = std::min (1.0, std::abs ((double) g.velocity) * scale);
        const auto factor = 1.0 + a * ((g.velocity > 0.0f ? level : mirrored) - 1.0);
        out.velocity = (float) std::clamp ((double) velocity * factor, 1.0 / 127.0, 1.0);
    }
    return out;
}

std::int64_t maxShift (const Groove& g, float amount) noexcept
{
    const auto base = (double) baseTicks (g.base);
    const auto scale = (double) std::clamp (amount, 0.0f, maxAmount);
    return (std::int64_t) std::ceil (base / 2.0 * (double) g.quantize + base / 2.0 * (double) g.timing * scale
                                     + randomSpan (baseTicks (g.base)) * (double) g.random * scale)
           + 1;
}

std::uint32_t randomBits (std::uint32_t seed, std::size_t noteIndex, std::int64_t pass) noexcept
{
    return (std::uint32_t) (chance::mix (chance::context (seed, 0x67726F6F7665ull + (std::uint64_t) noteIndex, pass)) >> 32);
}

bool commit (Song& song, int track, int scene)
{
    if (! song.clipGrid)
        return false;
    auto& grid = *song.clipGrid;
    const auto* clip = launcher::clipAt (grid, track, scene);
    if (clip == nullptr || clip->kind != ClipKind::pattern || clip->pattern < 0 || clip->pattern >= (int) song.patterns.size())
        return false;
    const auto* g = ofClip (song, *clip);
    if (g == nullptr)
        return false;
    const auto groove = *g;
    auto pattern = clip->pattern;

    // a pattern other clips play too: this clip gets its own copy
    auto uses = 0;
    song.forEachClipList ([&] (std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern == pattern && c.length > 0)
                ++uses;
    });
    if (uses > 1)
    {
        auto copy = song.patterns[(std::size_t) pattern];
        copy.uid = 0;
        song.patterns.push_back (std::move (copy));
        pattern = (int) song.patterns.size() - 1;
        for (auto& c : grid.clips)
            if (&c == clip)
                c.pattern = pattern;
    }

    auto& notes = song.patterns[(std::size_t) pattern].notes;
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto played = apply (groove, song.grooves.amount, notes[i].start, notes[i].velocity, randomBits (song.chanceSeed, i, 0));
        notes[i].start = played.start;
        notes[i].velocity = played.velocity;
    }
    song.patterns[(std::size_t) pattern].sortNotes();
    if (auto* slot = launcher::settingsAt (grid, track, scene))
        slot->groove = 0;
    return true;
}

Groove extract (const Pattern& pattern, std::int64_t length, GrooveBase base, std::string name)
{
    Groove g;
    g.name = std::move (name);
    g.base = base;
    const auto step = baseTicks (base);
    g.length = std::max (step, (length + step - 1) / step * step);
    const auto slots = (std::size_t) (g.length / step);
    std::vector<const Note*> loudest (slots, nullptr);
    for (const auto& n : pattern.notes)
    {
        if (n.muted || n.start < 0)
            continue;
        const auto slot = (std::size_t) (((n.start + step / 2) / step) % (std::int64_t) slots);
        if (n.start >= g.length + step / 2)
            continue;
        if (loudest[slot] == nullptr || n.velocity > loudest[slot]->velocity)
            loudest[slot] = &n;
    }
    for (std::size_t s = 0; s < slots; ++s)
        if (const auto* n = loudest[s])
        {
            // (a note just before the template's end belongs to the first slot: half a slot early at most)
            auto tick = n->start;
            if (s == 0 && tick > g.length / 2)
                tick -= g.length;
            g.points.push_back ({ ((tick % g.length) + g.length) % g.length, n->velocity });
        }
    tidy (g);
    return g;
}

// --- LYNX's own grooves ----------------------------------------------------------------------------------------------
namespace
{
Groove named (std::string name, GrooveBase base, std::int64_t length)
{
    Groove g;
    g.name = std::move (name);
    g.base = base;
    g.length = length;
    return g;
}

// Swing: of every pair of Base notes the second one sits `percent` % of the way through the pair (50 = straight).
Groove swing (const char* what, GrooveBase base, int percent)
{
    const auto step = baseTicks (base);
    auto g = named (std::string ("Swing ") + what + " " + std::to_string (percent), base, 4 * ppq);
    for (std::int64_t t = 0, i = 0; t < g.length; t += step, ++i)
        g.points.push_back ({ i % 2 == 0 ? t : t - step + (std::int64_t) std::llround (2.0 * (double) step * percent / 100.0), 1.0f });
    return g;
}

// A feel: every Base slot of a bar gets the offset and accent `shape` gives for its index.
template <typename Shape>
Groove shaped (const char* name, GrooveBase base, std::int64_t length, Shape shape)
{
    const auto step = baseTicks (base);
    auto g = named (name, base, length);
    for (std::int64_t t = 0, i = 0; t < length; t += step, ++i)
    {
        const std::pair<int, float> p = shape ((int) i);
        g.points.push_back ({ ((t + p.first) % length + length) % length, p.second });
    }
    return g;
}

// A fixed "loose hand": the same small offsets and accents every time (a hash of the slot, no random source).
std::pair<int, float> loose (int i, int ticks, float depth)
{
    const auto h = chance::mix (0x4C594E58ull + (std::uint64_t) i * 7919u);
    const auto offset = (int) std::lround ((chance::unit (h) * 2.0 - 1.0) * ticks);
    const auto accent = 1.0f - depth * (float) chance::unit (chance::mix (h));
    return { i % 4 == 0 ? offset / 3 : offset, accent };
}

std::vector<BankEntry> makeBank()
{
    std::vector<BankEntry> bank;
    const auto bar = (std::int64_t) 4 * ppq;
    for (const auto percent : { 54, 58, 62, 66, 70, 75 })
        bank.push_back ({ "Swing", swing ("8ths", GrooveBase::eighth, percent) });
    for (const auto percent : { 54, 58, 62, 66, 70, 75 })
        bank.push_back ({ "Swing", swing ("16ths", GrooveBase::sixteenth, percent) });
    for (const auto percent : { 54, 58, 62, 66, 70, 75 })
        bank.push_back ({ "Swing", swing ("32nds", GrooveBase::thirtySecond, percent) });

    bank.push_back ({ "Feel", shaped ("Laid back 16ths", GrooveBase::sixteenth, bar, [] (int i) { return std::pair { i % 2 == 1 ? 26 : (i % 4 == 2 ? 12 : 0), i % 4 == 0 ? 1.0f : 0.82f }; }) });
    bank.push_back ({ "Feel", shaped ("Pushed 16ths", GrooveBase::sixteenth, bar, [] (int i) { return std::pair { i % 2 == 1 ? -22 : (i % 4 == 2 ? -10 : 0), i % 4 == 0 ? 1.0f : 0.85f }; }) });
    bank.push_back ({ "Feel", shaped ("Late backbeat", GrooveBase::quarter, bar, [] (int i) { return std::pair { i % 2 == 1 ? 34 : 0, i % 2 == 1 ? 1.0f : 0.9f }; }) });
    bank.push_back ({ "Feel", shaped ("Early backbeat", GrooveBase::quarter, bar, [] (int i) { return std::pair { i % 2 == 1 ? -28 : 0, i % 2 == 1 ? 1.0f : 0.9f }; }) });
    bank.push_back ({ "Feel", shaped ("Loose 8ths", GrooveBase::eighth, 2 * bar, [] (int i) { return loose (i, 30, 0.25f); }) });
    bank.push_back ({ "Feel", shaped ("Loose 16ths", GrooveBase::sixteenth, 2 * bar, [] (int i) { return loose (i + 100, 22, 0.3f); }) });
    bank.push_back ({ "Feel", shaped ("Wobbly 16ths", GrooveBase::sixteenth, 2 * bar, [] (int i) { return loose (i + 500, 48, 0.4f); }) });

    const auto accent = [&] (const char* name, GrooveBase base, std::int64_t length, auto shape) {
        auto g = shaped (name, base, length, [shape] (int i) { return std::pair { 0, (float) shape (i) }; });
        g.velocity = 1.0f; // an accent groove does its work through Velocity
        bank.push_back ({ "Accent", std::move (g) });
    };
    accent ("Accent beats", GrooveBase::sixteenth, bar, [] (int i) { return i % 4 == 0 ? 1.0 : 0.6; });
    accent ("Accent off-beats", GrooveBase::eighth, bar, [] (int i) { return i % 2 == 1 ? 1.0 : 0.6; });
    accent ("Accent two and four", GrooveBase::quarter, bar, [] (int i) { return i % 2 == 1 ? 1.0 : 0.7; });
    accent ("Accent one", GrooveBase::quarter, bar, [] (int i) { return i == 0 ? 1.0 : 0.72; });
    accent ("Rolling 16ths", GrooveBase::sixteenth, bar, [] (int i) { return 0.7 + 0.3 * std::cos ((double) (i % 4) * 1.5707963267948966); });
    accent ("Rise over one bar", GrooveBase::sixteenth, bar, [] (int i) { return 0.4 + 0.6 * (double) i / 15.0; });
    accent ("Fall over one bar", GrooveBase::sixteenth, bar, [] (int i) { return 1.0 - 0.6 * (double) i / 15.0; });
    accent ("Rise over four bars", GrooveBase::eighth, 4 * bar, [] (int i) { return 0.35 + 0.65 * (double) i / 31.0; });

    for (const auto base : { GrooveBase::quarter, GrooveBase::eighth, GrooveBase::eighthTriplet, GrooveBase::sixteenth,
                             GrooveBase::sixteenthTriplet, GrooveBase::thirtySecond })
    {
        auto g = shaped ("", base, bar, [] (int) { return std::pair { 0, 1.0f }; });
        g.name = std::string ("Straight ") + baseName (base);
        g.quantize = 1.0f; // the plain grids quantise; their template moves nothing
        g.timing = 0.0f;
        bank.push_back ({ "Utility", std::move (g) });
    }
    for (auto& e : bank)
        tidy (e.groove);
    return bank;
}
} // namespace

const std::vector<BankEntry>& factoryBank()
{
    static const auto bank = makeBank();
    return bank;
}

// --- a groove file of the user's -------------------------------------------------------------------------------------
bool parseAgr (std::string text, Groove& out, std::string* error)
{
    const auto fail = [&] (const char* why) {
        if (error != nullptr)
            *error = why;
        return false;
    };
    xml::Document doc;
    std::string parseError;
    if (! doc.parse (std::move (text), &parseError))
        return fail ("not XML");
    const auto root = doc.root();
    const auto node = root.name() == "Groove" ? root : root.find ("Groove");
    if (! node)
        return fail ("no groove in the file");
    const auto clip = node.path ("Clip/Value/MidiClip");
    if (! clip)
        return fail ("the groove has no template");

    Groove g;
    g.name = node.value ("Name");
    g.base = (GrooveBase) std::clamp ((int) node.number ("Grid", 3.0), 0, (int) GrooveBase::thirtySecond);
    g.quantize = (float) (node.number ("QuantizationAmount", 0.0) / 100.0);
    g.timing = (float) (node.number ("TimingAmount", 100.0) / 100.0);
    g.random = (float) (node.number ("RandomAmount", 0.0) / 100.0);
    g.velocity = (float) (node.number ("VelocityAmount", 0.0) / 100.0);

    // the template: the clip's notes between its loop points, in beats
    const auto from = clip.number ("Loop/LoopStart", 0.0);
    auto to = clip.number ("Loop/LoopEnd", 0.0);
    if (to <= from)
        to = from + 4.0;
    g.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((to - from) * ppq));
    for (const auto& keyTrack : clip.path ("Notes/KeyTracks").children ("KeyTrack"))
        for (const auto& n : keyTrack.path ("Notes").children ("MidiNoteEvent"))
        {
            if (n.hasAttr ("IsEnabled") && n.attr ("IsEnabled") == "false")
                continue;
            const auto time = std::strtod (n.attr ("Time").c_str(), nullptr) - from;
            const auto tick = (std::int64_t) std::llround (time * ppq);
            if (tick < 0 || tick >= g.length)
                continue;
            g.points.push_back ({ tick, (float) std::clamp (std::strtod (n.attr ("Velocity").c_str(), nullptr) / 127.0, 0.0, 1.0) });
        }
    if (g.points.empty())
        return fail ("the groove's template has no notes");
    tidy (g);
    // several notes in one Base slot (a chord in the template): the loudest speaks for the slot
    const auto step = baseTicks (g.base);
    std::vector<GroovePoint> kept;
    for (const auto& p : g.points)
    {
        const auto slot = (p.tick + step / 2) / step;
        if (! kept.empty() && (kept.back().tick + step / 2) / step == slot)
        {
            if (p.velocity > kept.back().velocity)
                kept.back() = p;
            continue;
        }
        kept.push_back (p);
    }
    g.points = std::move (kept);
    out = std::move (g);
    return true;
}
} // namespace lynx::groove
