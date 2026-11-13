#pragma once

// Round 190df (WHAT-LYNX-LACKS items 22 and 30): the FL facts behind three small FL pieces, kept free of UI so the
// tests can check them. Facts from static study of FLEngine_x64.dll (FL Studio 2026 study install), handler addresses
// in /mnt/e/lynx-work/flctl190df/FL-FACTS.md; facts in our own words.
//  - mlmenu: FL's multilink menu (MLWheelMenu, right-click on the toolbar's Multilink button): its entries for a
//    state, its caption, and the value laws of Randomize and Humanize.
//  - samplehistory: FL's "Sample history" (the Load button of a sampler channel: right-click lists it, the mouse
//    wheel steps through it).
//  - chenv: FL's channel envelope presets (.CHENV files: one articulator record of 17 whole numbers).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "core/FlVoiceSettings.h"
#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx
{
namespace mlmenu
{
// What an entry does (FL's MLParam_* items)
enum class Action
{
    none,
    editEvents,         // Edit events
    initSong,           // Init song with these positions
    initPattern,        // Init pattern with these positions
    createClips,        // Create automation clips
    linkToControllers,  // Link to controllers...
    overrideGlobal,     // Override global links...
    randomize,          // Randomize
    humanize,           // Humanize
    cancel              // Cancel
};

struct Entry
{
    enum class Kind
    {
        caption, // a section caption (FL's "-Text" items)
        item,
        separator
    };
    Kind kind = Kind::item;
    std::string text; // English; the UI translates it ("%d" is filled in by `captionFor`)
    Action action = Action::none;
    bool enabled = true;
};

// FL's first caption [MLWheelMenuPopup 0x1234ae0]: multilink on = "%d parameter selected" / "%d parameters selected"
// (the queued controls); off = the last tweaked parameters: none "No parameter selected", one "Last tweaked
// parameter", two "Last 2 tweaked parameters".
inline std::string captionFor (bool multilinkOn, int count)
{
    if (count <= 0)
        return TR_NOOP ("No parameter selected");
    if (! multilinkOn)
        return count == 1 ? TR_NOOP ("Last tweaked parameter") : TR_NOOP ("Last 2 tweaked parameters");
    return std::to_string (count) + (count < 2 ? " parameter selected" : " parameters selected");
}

// The menu in FL's order [form TFruityLoopsMainForm.MLWheelMenu; live shot link190bt-09a]. Every item is enabled
// when there is at least one parameter; Cancel and the line above it show only while multilink is on.
inline std::vector<Entry> build (bool multilinkOn, int count)
{
    using K = Entry::Kind;
    const auto any = count > 0;
    std::vector<Entry> e;
    e.push_back ({ K::caption, captionFor (multilinkOn, count) });
    e.push_back ({ K::caption, TR_NOOP ("Automation") });
    e.push_back ({ K::item, TR_NOOP ("Edit events"), Action::editEvents, any });
    e.push_back ({ K::item, TR_NOOP ("Init song with these positions"), Action::initSong, any });
    e.push_back ({ K::item, TR_NOOP ("Init pattern with these positions"), Action::initPattern, any });
    e.push_back ({ K::separator, {}, Action::none, true });
    e.push_back ({ K::item, TR_NOOP ("Create automation clips"), Action::createClips, any });
    e.push_back ({ K::caption, TR_NOOP ("Remote control") });
    e.push_back ({ K::item, TR_NOOP ("Link to controllers..."), Action::linkToControllers, any });
    e.push_back ({ K::item, TR_NOOP ("Override global links..."), Action::overrideGlobal, any });
    e.push_back ({ K::caption, TR_NOOP ("Value") });
    e.push_back ({ K::item, TR_NOOP ("Randomize"), Action::randomize, any });
    e.push_back ({ K::item, TR_NOOP ("Humanize"), Action::humanize, any });
    if (multilinkOn)
    {
        e.push_back ({ K::separator, {}, Action::none, true });
        e.push_back ({ K::item, TR_NOOP ("Cancel"), Action::cancel, true });
    }
    return e;
}

// Live check 190ew CTL-1 (FL 2026): with multilink off the Multilink button's menu has two columns, the last tweaked
// control's own menu on the left (captioned with the control's name), this menu on the right; on, this menu alone.
inline bool showsOwnColumn (bool multilinkOn, bool hasLastTweaked) noexcept { return ! multilinkOn && hasLastTweaked; }
// LYNX's own one-step "Link to controller" / "Override global links" (round 177b-4) act on the queued controls: shown
// only while multilink is on (FL's menu has no such greyed lines when it is off)
inline bool showsLynxOneStepItems (bool multilinkOn) noexcept { return multilinkOn; }

// Whether the action ends multilink: every item does, except Randomize and Humanize while multilink is on (FL keeps
// the queue so they can be repeated) [0x12349a0, 0x12343f0: the end call is skipped while multilink is on].
inline bool endsMultilink (Action a, bool multilinkOn) noexcept
{
    if (a == Action::randomize || a == Action::humanize)
        return ! multilinkOn;
    return a != Action::none;
}

// FL's value scale for these laws: 0 ... 2^30 [0x12349a0]
constexpr std::int64_t fullScale = 0x40000000;

// FL's Random(n): a 32-bit random number scaled to 0 ... n-1 [0x40a140]
inline std::int64_t randomBelow (std::uint32_t random32, std::uint32_t n) noexcept
{
    return (std::int64_t) (((std::uint64_t) random32 * (std::uint64_t) n) >> 32);
}

// Randomize: a new value anywhere in the range, Random(2^30 + 1) [0x12349a0]
inline float randomize (std::uint32_t random32) noexcept
{
    return (float) ((double) randomBelow (random32, (std::uint32_t) fullScale + 1u) / (double) fullScale);
}

// Humanize: the value moved by Random(2^28 + 1) - 2^27, at most an eighth of the range either way, kept inside the
// range [0x12343f0]
inline float humanize (float value, std::uint32_t random32) noexcept
{
    const auto now = (std::int64_t) std::llround (std::clamp ((double) value, 0.0, 1.0) * (double) fullScale);
    auto v = now - 0x8000000 + randomBelow (random32, 0x10000001u);
    if (v < 1)
        v = 0;
    if (v > fullScale)
        v = fullScale;
    return (float) ((double) v / (double) fullScale);
}

// Init pattern with these positions: the value as the pattern's event at its start [0x12345c0 -> 0x1052040: the
// pattern's event list, position 0]. True if anything changed.
inline bool initPattern (Song& song, int pattern, const AutomationTarget& target, float value)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return false;
    auto& lanes = song.patterns[(std::size_t) pattern].events;
    auto lane = std::find_if (lanes.begin(), lanes.end(), [&] (const PatternEvents& l) { return l.target == target; });
    if (lane == lanes.end())
    {
        lanes.push_back ({ target, {} });
        lane = lanes.end() - 1;
    }
    auto& ev = lane->events;
    value = std::clamp (value, 0.0f, 1.0f);
    if (! ev.empty() && ev.front().tick == 0)
    {
        if (ev.front().value == value)
            return false;
        ev.front().value = value;
        return true;
    }
    ev.insert (ev.begin(), PatternEvent { 0, value });
    return true;
}
} // namespace mlmenu

namespace samplehistory
{
// FL keeps one list for every channel, newest first, at most 30 paths; a path already in it stays where it is
// [0x1231030, called by the sampler's sample loader]. LYNX keeps it for the session (whether FL saves it is a live
// check).
constexpr std::size_t capacity = 30;

inline std::vector<std::string>& list()
{
    static std::vector<std::string> paths;
    return paths;
}

inline void add (const std::string& path)
{
    if (path.empty())
        return;
    auto& l = list();
    if (std::find (l.begin(), l.end(), path) != l.end())
        return;
    l.insert (l.begin(), path);
    while (l.size() > capacity)
        l.pop_back();
}

// FL's mouse wheel over the Load button [0x0f68f50]: with at least two samples in the history and the channel's
// sample among them, down (delta < 0) = the next (older) one, up = the previous one; nullopt = nothing to load
inline std::optional<std::string> step (const std::string& current, int delta)
{
    const auto& l = list();
    if (l.size() < 2 || delta == 0)
        return std::nullopt;
    const auto at = std::find (l.begin(), l.end(), current);
    if (at == l.end())
        return std::nullopt;
    const auto i = (int) (at - l.begin());
    if (delta < 0 && i + 1 < (int) l.size())
        return l[(std::size_t) i + 1];
    if (delta > 0 && i > 0)
        return l[(std::size_t) i - 1];
    return std::nullopt;
}
} // namespace samplehistory

namespace chenv
{
// A .CHENV file: the shown target's articulator record, 68 bytes (17 whole numbers) read and written as they are
// [0x0f71560]. The fields are taken in the order of the .flp 0xDA record (flvoice::Articulator), which is how FL
// keeps that record in memory; a file saved by FL and read back here is a live check.
constexpr std::size_t size = 68;

inline std::vector<std::uint8_t> write (const flvoice::Articulator& a)
{
    const std::int32_t r[17] = { a.flags, a.envelope ? -1 : 0, a.delay, a.attack, a.hold, a.decay, a.sustain, a.release, a.amount,
                                 a.lfoDelay, a.lfoAttack, a.lfoAmount, a.lfoSpeed, a.lfoShape, a.attackTension, a.decayTension,
                                 a.releaseTension };
    std::vector<std::uint8_t> bytes (size);
    std::memcpy (bytes.data(), r, size);
    return bytes;
}

inline std::optional<flvoice::Articulator> read (const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < size)
        return std::nullopt;
    std::int32_t r[17];
    std::memcpy (r, bytes.data(), size);
    flvoice::Articulator a;
    a.flags = r[0], a.envelope = r[1] != 0;
    a.delay = r[2], a.attack = r[3], a.hold = r[4], a.decay = r[5], a.sustain = r[6], a.release = r[7], a.amount = r[8];
    a.lfoDelay = r[9], a.lfoAttack = r[10], a.lfoAmount = r[11], a.lfoSpeed = r[12], a.lfoShape = r[13];
    a.attackTension = r[14], a.decayTension = r[15], a.releaseTension = r[16];
    return a;
}
} // namespace chenv

namespace flsamplervoice
{
// A sampler channel that runs FL's voice (SamplerSettings::flChannel, 89 or 90 values: five 17-value articulator
// records, Mod X, Mod Y, the filter type, the project's PPQ, the pan law) seen as the Envelope / instrument page's
// settings, and written back without touching the other values.
inline bool has (const SamplerSettings& s) noexcept { return s.flChannel.size() == 89 || s.flChannel.size() == 90; }

inline flvoice::Settings read (const SamplerSettings& s)
{
    flvoice::Settings out;
    if (! has (s))
        return out;
    for (int k = 0; k < flvoice::targetCount; ++k)
    {
        const auto* r = s.flChannel.data() + 17 * k;
        auto& a = out.articulators[(std::size_t) k];
        a.flags = r[0], a.envelope = r[1] != 0;
        a.delay = r[2], a.attack = r[3], a.hold = r[4], a.decay = r[5], a.sustain = r[6], a.release = r[7], a.amount = r[8];
        a.lfoDelay = r[9], a.lfoAttack = r[10], a.lfoAmount = r[11], a.lfoSpeed = r[12], a.lfoShape = r[13];
        a.attackTension = r[14], a.decayTension = r[15], a.releaseTension = r[16];
    }
    out.modX = s.flChannel[85], out.modY = s.flChannel[86], out.filterType = s.flChannel[87];
    return out;
}

inline void write (SamplerSettings& s, const flvoice::Settings& in)
{
    if (! has (s))
        return;
    for (int k = 0; k < flvoice::targetCount; ++k)
    {
        auto* r = s.flChannel.data() + 17 * k;
        const auto& a = in.articulators[(std::size_t) k];
        if ((r[1] != 0) != a.envelope) // (an untouched switch keeps FL's own truthy value)
            r[1] = a.envelope ? -1 : 0;
        r[0] = a.flags;
        r[2] = a.delay, r[3] = a.attack, r[4] = a.hold, r[5] = a.decay, r[6] = a.sustain, r[7] = a.release, r[8] = a.amount;
        r[9] = a.lfoDelay, r[10] = a.lfoAttack, r[11] = a.lfoAmount, r[12] = a.lfoSpeed, r[13] = a.lfoShape;
        r[14] = a.attackTension, r[15] = a.decayTension, r[16] = a.releaseTension;
    }
    s.flChannel[85] = in.modX, s.flChannel[86] = in.modY, s.flChannel[87] = in.filterType;
}
} // namespace flsamplervoice
namespace sparestate
{
// FL's plugin menu "Store in spare state" / "Flip with spare state" (A/B compare) [0x12bb790, 0x12bb880]: the plugin's
// own state (not the channel's) kept aside per plugin instance, outside the project and the undo history. Store keeps a
// copy; Flip swaps the plugin's state with the kept one (nothing to flip before a store).
struct Store
{
    std::vector<std::pair<const void*, std::vector<std::uint8_t>>> kept;

    const std::vector<std::uint8_t>* find (const void* plugin) const
    {
        for (const auto& [p, bytes] : kept)
            if (p == plugin)
                return &bytes;
        return nullptr;
    }
    bool has (const void* plugin) const { return find (plugin) != nullptr; }
    void store (const void* plugin, std::vector<std::uint8_t> state)
    {
        for (auto& [p, bytes] : kept)
            if (p == plugin)
            {
                bytes = std::move (state);
                return;
            }
        kept.push_back ({ plugin, std::move (state) });
    }
    // the state to load into the plugin (and `current` becomes the kept one); nullopt = nothing stored
    std::optional<std::vector<std::uint8_t>> flip (const void* plugin, std::vector<std::uint8_t> current)
    {
        for (auto& [p, bytes] : kept)
            if (p == plugin)
            {
                auto load = std::move (bytes);
                bytes = std::move (current);
                return load;
            }
        return std::nullopt;
    }
    void forget (const void* plugin)
    {
        kept.erase (std::remove_if (kept.begin(), kept.end(), [plugin] (const auto& k) { return k.first == plugin; }), kept.end());
    }
};

inline Store& shared()
{
    static Store s;
    return s;
}
} // namespace sparestate
} // namespace lynx
