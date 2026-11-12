#pragma once

// Hot-swap (round 190ce, docs/analise/REA_rodada190ce.md §2): the reference DAW's "temporary link between the browser
// and the device". While it is on, what the Browser loads replaces the linked device, preset or sample in place instead
// of making a new channel or adding an effect; Q, Esc, the Browser's bar or the device's button end it.
//
// What LYNX links:
//   - a mixer effect slot: another effect from the Plugin database, an FL effect preset (.fst) or one of LYNX's own
//     effect presets (.lynxpreset of the same effect) takes its place; the slot keeps its row, its Mix and its sidechain
//     sources, and the device comes in with its own on / off state: on (round 190cy, Live 12.4.16 live check 190cw: an
//     Utility that was off, swapped for the preset "Wide Stereo", came in on);
//   - a channel: another generator from the Plugin database or a synth preset replaces its instrument (the channel keeps
//     its place, notes, insert and levels, core replaceGenerator); a channel that plays a sample takes another sample.
// The link is a mode of the session, never saved. Every swap is one ordinary song edit (one undo step each).
// No JUCE: tested in isolation (tests/LiveBrowser190ceTests.inc).

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "core/LayoutStyle.h"
#include "core/MixerOps.h"
#include "core/PlaylistOps.h"
#include "core/RackOps.h"
#include "core/SampleFiles.h"
#include "core/Song.h"

namespace lynx::hotswap
{
enum class Kind : std::uint8_t { none, effect, channel };

// Round 190ck (the 0.1.27 rollup, coordinator's decision): the default key of "view.hotSwap", a plain Q, is the
// reference DAW's key and acts only in the Ableton-style (Clip-launch) layout. In FL's Pattern layout and in the Simple
// layout the default key does nothing (the chord falls through as if unbound; FL has no hot-swap mode and gives Q no
// such meaning): hot-swap stays reachable there from the Mixer slot's menu, the Browser's bar and Esc. A key the user
// bound to the action himself (`defaultKeys` false) works in every layout.
constexpr bool keyActsInLayout (layoutstyle::Style style, bool defaultKeys) noexcept
{
    return style == layoutstyle::Style::clipLaunch || ! defaultKeys;
}

// What the Browser is linked to. The slot / channel is followed by its running instance (core InstanceTag), so the link
// survives a reorder of the chain or of the Channel Rack; a swap gives the new device's instance back.
struct Target
{
    Kind kind = Kind::none;
    std::uint64_t instance = 0;
    bool active() const noexcept { return kind != Kind::none && instance != 0; }
    bool operator== (const Target&) const = default;
};

struct EffectPlace
{
    int track = -1, slot = -1;
    bool found() const noexcept { return track >= 0 && slot >= 0; }
};

inline EffectPlace findEffect (const Song& song, std::uint64_t instance)
{
    if (instance != 0)
        for (int t = 0; t < (int) song.mixer.size(); ++t)
            for (int s = 0; s < (int) song.mixer[(std::size_t) t].effects.size(); ++s)
                if (song.mixer[(std::size_t) t].effects[(std::size_t) s].instance.id == instance)
                    return { t, s };
    return {};
}

inline int findChannel (const Song& song, std::uint64_t instance)
{
    if (instance != 0)
        for (int c = 0; c < (int) song.channels.size(); ++c)
            if (song.channels[(std::size_t) c].instance.id == instance)
                return c;
    return -1;
}

inline Target targetOfEffect (const Song& song, int track, int slot)
{
    if (track < 0 || track >= (int) song.mixer.size() || slot < 0 || slot >= (int) song.mixer[(std::size_t) track].effects.size())
        return {};
    return { Kind::effect, song.mixer[(std::size_t) track].effects[(std::size_t) slot].instance.id };
}

inline Target targetOfChannel (const Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return {};
    return { Kind::channel, song.channels[(std::size_t) channel].instance.id };
}

// The linked device still exists (it was not deleted, the project was not replaced): otherwise the mode ends.
inline bool exists (const Song& song, const Target& target)
{
    return target.kind == Kind::effect    ? findEffect (song, target.instance).found()
           : target.kind == Kind::channel ? findChannel (song, target.instance) >= 0
                                          : false;
}

// A channel whose sound is a sample file: hot-swap offers samples for it (as Enter in the Browser always did).
inline bool playsSample (const ChannelDef& c) noexcept
{
    switch (c.instrument)
    {
        case InstrumentKind::sampler:
        case InstrumentKind::audioClip:
        case InstrumentKind::kick:
        case InstrumentKind::snare:
        case InstrumentKind::hat:
        case InstrumentKind::clap:
        case InstrumentKind::granulizer:
        case InstrumentKind::wavetraveller:
            return true;
        default:
            return false;
    }
}

// --- the swaps -------------------------------------------------------------------------------------------------------
// Another device into the linked slot. The slot's place in the chain and in the mixer rows, its Mix and its sidechain
// sources stay; the new device is a new instance with its own on / off state (round 190cy: as loaded, not the old
// device's). Returns the link to it (none: the slot is gone).
inline Target swapEffect (Song& song, const Target& target, EffectSlot fresh)
{
    const auto at = findEffect (song, target.instance);
    if (target.kind != Kind::effect || ! at.found())
        return {};
    auto& slot = song.mixer[(std::size_t) at.track].effects[(std::size_t) at.slot];
    fresh.row = slot.row;
    fresh.mix = slot.mix;
    fresh.smartDisable = slot.smartDisable;
    if (mixerops::acceptsSidechain (fresh.kind))
    {
        fresh.sidechain = slot.sidechain;
        fresh.sidechain2 = slot.sidechain2;
    }
    slot = std::move (fresh);
    return { Kind::effect, slot.instance.id };
}

// A preset of the same effect into the linked slot: its settings change (the instance, and so the link, stays) and the
// device is on, as a preset loads it (round 190cy, Live 12.4.16: the swapped-in preset's own state; LYNX presets carry
// no on / off). False when the slot is gone or holds another kind of effect.
inline bool swapEffectPreset (Song& song, const Target& target, EffectKind kind, const EffectParams& params,
                              const std::vector<std::uint8_t>* state, const std::string& presetName)
{
    const auto at = findEffect (song, target.instance);
    if (target.kind != Kind::effect || ! at.found())
        return false;
    auto& slot = song.mixer[(std::size_t) at.track].effects[(std::size_t) at.slot];
    if (slot.kind != kind)
        return false;
    slot.params = params;
    if (state != nullptr)
        slot.state = *state;
    slot.presetName = presetName;
    slot.enabled = true;
    return true;
}

// A hosted plugin's own preset file loaded into the linked slot: the device is on, as above (round 190cy).
inline bool presetLoadedInto (Song& song, const Target& target)
{
    const auto at = findEffect (song, target.instance);
    if (target.kind != Kind::effect || ! at.found())
        return false;
    song.mixer[(std::size_t) at.track].effects[(std::size_t) at.slot].enabled = true;
    return true;
}

// Another instrument into the linked channel (core replaceGenerator: place, notes, insert, levels, colour stay).
inline Target swapInstrument (Song& song, const Target& target, ChannelDef fresh)
{
    const auto channel = findChannel (song, target.instance);
    if (target.kind != Kind::channel || channel < 0 || ! replaceGenerator (song, channel, std::move (fresh)))
        return {};
    return { Kind::channel, song.channels[(std::size_t) channel].instance.id };
}

// Another sample into the linked channel (the Browser's Enter): the channel and the link stay.
inline bool swapSample (Song& song, const Target& target, const std::string& path, const std::string& name)
{
    const auto channel = findChannel (song, target.instance);
    if (target.kind != Kind::channel || channel < 0 || ! playsSample (song.channels[(std::size_t) channel]))
        return false;
    audioclips::loadSampleInto (song.channels[(std::size_t) channel], path, name);
    return true;
}

// --- what the Browser offers -----------------------------------------------------------------------------------------
enum class Candidate : std::uint8_t
{
    none,
    effect,       // a Plugin database effect
    generator,    // a Plugin database generator
    effectPreset, // .fst (an FL effect preset) or .lynxpreset (one of LYNX's effect presets)
    synthPreset,  // .fst / .gmsynth of a synth LYNX converts
    sample
};

inline std::string lowerExtension (const std::string& fileName)
{
    const auto dot = fileName.find_last_of ('.');
    const auto slash = fileName.find_last_of ("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    auto ext = fileName.substr (dot);
    for (auto& c : ext)
        c = (char) std::tolower ((unsigned char) c);
    return ext;
}

// What a file can be for the linked device (by its extension; the content decides when it is loaded).
inline Candidate candidateFor (const Song& song, const Target& target, const std::string& fileName)
{
    const auto ext = lowerExtension (fileName);
    if (target.kind == Kind::effect)
        return findEffect (song, target.instance).found() && (ext == ".fst" || ext == ".lynxpreset") ? Candidate::effectPreset : Candidate::none;
    if (target.kind == Kind::channel)
    {
        const auto channel = findChannel (song, target.instance);
        if (channel < 0)
            return Candidate::none;
        if (ext == ".fst" || ext == ".gmsynth")
            return Candidate::synthPreset;
        if (isSampleExtension (ext) && playsSample (song.channels[(std::size_t) channel]))
            return Candidate::sample;
    }
    return Candidate::none;
}

// --- the session -----------------------------------------------------------------------------------------------------
// The Browser's side of the link: what it is linked to and what the Browser showed before, put back when the mode ends
// (the tab and the search text; LYNX's choice, the reference leaves its browser where the user browsed to).
struct Session
{
    Target target;
    std::vector<Target> earlier; // the devices this link replaced, oldest first: Undo brings one back and the link follows
    int previousTab = 0;
    std::string previousSearch;
    int swaps = 0; // loads made in this session (each one undo step)

    bool active() const noexcept { return target.active(); }

    void begin (const Target& t, int tab, std::string search)
    {
        if (! active()) // (re-linking to another device keeps what the Browser showed before the first link)
        {
            previousTab = tab;
            previousSearch = std::move (search);
        }
        target = t;
        earlier.clear();
        swaps = 0;
    }
    // A swap put a new device in the linked one's place.
    void retarget (const Target& t)
    {
        if (! t.active())
            return;
        if (target.active() && ! (target == t))
            earlier.push_back (target);
        target = t;
        ++swaps;
    }
    void loaded() noexcept { ++swaps; } // (a preset or a sample: the device stays)
    struct Restore
    {
        int tab = 0;
        std::string search;
    };
    // Ends the mode; returns what the Browser goes back to.
    Restore end()
    {
        Restore r { previousTab, std::move (previousSearch) };
        *this = {};
        return r;
    }
    // The song changed. The linked device is still there: nothing to do. It is gone but a device this link replaced is
    // back (Undo of a swap): the link moves to that one. Neither (the device was deleted, another project): false, and
    // the caller ends the mode.
    bool stillValid (const Song& song)
    {
        if (! active() || exists (song, target))
            return true;
        for (auto i = earlier.size(); i-- > 0;)
            if (exists (song, earlier[i]))
            {
                target = earlier[i];
                earlier.resize (i);
                return true;
            }
        return false;
    }
};
} // namespace lynx::hotswap
