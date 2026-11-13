#pragma once

// FL Studio 2026's audio clip menu (the Playlist's right-click on an audio clip) as a JUCE-free model, round 190dz.
// FL facts: /mnt/e/lynx-work/flclip190dz/HANDOFF.md — [FORM] TFRUITYLOOPSMAINFORM EEAUMenu (rea190bc dfm dump), [H 0x...]
// FLEngine_x64.dll handlers (EEAUMenuPopup 0x1237290 and the OnClick handlers), [SHOT] flgap189o FL-clipmenu-*.png.
// FL draws it in two columns with section bands: "Audio clip" + "Region" on the left, "Sample" + "Automation" + "Fades"
// on the right. The Playlist builds its audio clip menu from this list in this order (texts are the English keys of
// the i18n tables); LYNX's own items with no FL counterpart follow FL's last section below a line.

#include "core/ClipOps.h"
#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::flclipmenu
{
struct Entry
{
    enum class Kind { header, item, check, submenu, separator, columnBreak } kind = Kind::item;
    std::string text;
    // what LYNX does with it: performs it; FL greys it in the menu's usual state and LYNX shows it greyed there (and
    // leaves it out where FL would enable it); or LYNX cannot do it yet (left out of LYNX's menu, listed in HANDOFF)
    enum class Have { yes, greyedOnly, missing } have = Have::yes;
    char mnemonic = 0;      // FL's underlined letter ('&' in its captions), 0 = none
    bool conditional = false; // FL shows it only in some states (Crossfade selected)
    bool operator== (const Entry&) const = default;
};

// FL's menu for one audio clip, in FL's order ([SHOT] FL-clipmenu-none-selected / -all-selected, [FORM] EEAUMenu captions;
// EEAUChanSelMenu's caption "Select source channel" is set by EEAUMenuPopup, "&Select source %s"). Hidden in FL 2026
// (EEAUMenuPopup sets them invisible, never shown): Articulator tools, Edit envelope..., Use current time signature
// from (pattern clips), "Per clip settings" (Set pitch..., Reset stretch factor, Reverse clip).
inline std::vector<Entry> fl()
{
    using K = Entry::Kind;
    using H = Entry::Have;
    return {
        { K::header, TR_NOOP ("Audio clip") },
        { K::item, TR_NOOP ("Preview"), H::yes, 'P' },
        { K::separator, {} },
        { K::check, TR_NOOP ("Muted") },
        { K::separator, {} },
        { K::item, TR_NOOP ("Rename and color..."), H::yes, 'R' },
        { K::item, TR_NOOP ("Change color...") },
        { K::item, TR_NOOP ("Random color") },
        { K::item, TR_NOOP ("Change icon...") },
        { K::separator, {} },
        { K::submenu, TR_NOOP ("Select source channel"), H::yes, 'S' },
        { K::item, TR_NOOP ("Channel settings"), H::yes, 'C' },
        { K::item, TR_NOOP ("Make unique"), H::yes, 'M' },
        { K::item, TR_NOOP ("Select all similar clips") },
        { K::item, TR_NOOP ("Delete") },
        { K::separator, {} },
        { K::item, TR_NOOP ("Properties..."), H::yes, 'o' },
        { K::header, TR_NOOP ("Region") },
        { K::submenu, TR_NOOP ("Select region") },
        { K::submenu, TR_NOOP ("Chop") },
        { K::columnBreak, {} },
        { K::header, TR_NOOP ("Sample") },
        { K::item, TR_NOOP ("Make unique as sample"), H::yes, 'u' },
        { K::item, TR_NOOP ("Edit sample"), H::yes, 'E' },
        { K::item, TR_NOOP ("Pitch-correct sample"), H::missing, 'P' }, // (FL: the clip's sample into Newtone)
        { K::item, TR_NOOP ("Time-warp sample"), H::missing, 'T' },     // (FL: the clip's sample into Newtime)
        { K::item, TR_NOOP ("Extract stems from sample") },
        { K::item, TR_NOOP ("Deverb"), H::missing }, // (FL: its Deverb dialog on the clip's part, the result saved as <name>_Deverb.wav)
        { K::item, TR_NOOP ("Detect tempo"), H::yes, 'D' },
        { K::item, TR_NOOP ("Fit to tempo"), H::yes, 'F' },
        { K::header, TR_NOOP ("Automation") },
        { K::submenu, TR_NOOP ("Automate"), H::yes, 'A' },
        { K::submenu, TR_NOOP ("Crossfade with"), H::greyedOnly, 'f' }, // (greyed while no other channel's audio clip overlaps it)
        { K::header, TR_NOOP ("Fades") },
        { K::item, TR_NOOP ("Reset fades") },
        { K::item, TR_NOOP ("Crossfade selected"), H::yes, 0, true },
        { K::submenu, TR_NOOP ("Fade in curve"), H::missing },
        { K::submenu, TR_NOOP ("Fade out curve"), H::missing },
    };
}

// LYNX's own audio clip items with no FL counterpart (kept: add, never remove), after FL's Fades below a line, in this
// order. Comping's "Promote this take..." comes first when the clip is on a take lane (core/Comping, round 163).
inline std::vector<std::string> lynxExtras()
{
    return { TR_NOOP ("Color palette"), TR_NOOP ("Split here"), TR_NOOP ("Merge clips"), TR_NOOP ("Reverse"), TR_NOOP ("Convert to MIDI"), TR_NOOP ("Slice to New MIDI Track..."), TR_NOOP ("Pitch... (semitones, cents, keep length)"),
             TR_NOOP ("Normalize"), TR_NOOP ("Clip volume"), TR_NOOP ("Open in a new Sampler channel"), TR_NOOP ("Show the file in Explorer"), TR_NOOP ("Cut"), TR_NOOP ("Copy"), TR_NOOP ("Paste here") };
}

// What LYNX shows of FL's list: FL's entries without the missing ones, Crossfade with only while it is greyed in FL
// (`crossfadeWithGreyed`: no other channel's audio clip overlaps the clicked one), Crossfade selected only when FL
// shows it. Separators and headers stay where FL has them.
inline std::vector<Entry> shown (bool crossfadeWithGreyed, bool crossfadeSelectedShown)
{
    std::vector<Entry> out;
    for (const auto& e : fl())
    {
        if (e.have == Entry::Have::missing)
            continue;
        if (e.have == Entry::Have::greyedOnly && ! crossfadeWithGreyed)
            continue;
        if (e.conditional && ! crossfadeSelectedShown)
            continue;
        out.push_back (e);
    }
    return out;
}

// ---- Select region › (EEAUMenuPopup fills it from the sample's regions; EEAUFullRegMenuClick 0x1239b70) -------------
// "Full" (FL's Default radio item, Tag −1: the clip back to its whole source) and one item per region of the sample,
// named "Region N" when the region has no name. A region runs from its start to the next region's start (or the end).
// LYNX's regions: the file's cue markers (SampleData::markerStarts, frames of the file).
struct Region
{
    std::int64_t first = 0, end = 0; // frames of the file, [first, end)
    bool operator== (const Region&) const = default;
};

inline std::vector<Region> sampleRegions (std::vector<std::int64_t> markers, std::int64_t frames)
{
    std::sort (markers.begin(), markers.end());
    markers.erase (std::unique (markers.begin(), markers.end()), markers.end());
    std::vector<Region> out;
    for (std::size_t i = 0; i < markers.size(); ++i)
    {
        const auto first = std::clamp<std::int64_t> (markers[i], 0, frames);
        const auto end = i + 1 < markers.size() ? std::clamp<std::int64_t> (markers[i + 1], 0, frames) : frames;
        if (end > first)
            out.push_back ({ first, end });
    }
    return out;
}

// A region as the clip's start offset and length in ticks: `spanFirst` / `spanFrames` = the part of the file the channel
// plays (clipops::clipSpan), `perTick` = frames per tick (clipops::framesPerTick). Nothing when the region lies outside.
struct ClipWindow
{
    std::int64_t offset = 0, length = 0;
    bool operator== (const ClipWindow&) const = default;
};

inline std::optional<ClipWindow> regionWindow (const Region& r, std::int64_t spanFirst, std::int64_t spanFrames, double perTick)
{
    if (perTick <= 0.0)
        return std::nullopt;
    const auto first = std::clamp<std::int64_t> (r.first - spanFirst, 0, spanFrames);
    const auto end = std::clamp<std::int64_t> (r.end - spanFirst, 0, spanFrames);
    const auto offset = (std::int64_t) std::llround ((double) first / perTick);
    const auto length = (std::int64_t) std::llround ((double) (end - first) / perTick);
    if (length <= 0)
        return std::nullopt;
    return ClipWindow { offset, length };
}

// "Full": the whole played part of the file
inline ClipWindow fullWindow (std::int64_t spanFrames, double perTick)
{
    return { 0, perTick > 0.0 ? std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) spanFrames / perTick)) : 1 };
}

// The radio check: the region the clip plays now (offset and length within one tick), −1 = none of them ("Full")
inline int currentRegion (const std::vector<ClipWindow>& windows, const Clip& clip)
{
    for (int i = 0; i < (int) windows.size(); ++i)
        if (std::llabs (windows[(std::size_t) i].offset - clip.offset) <= 1 && std::llabs (windows[(std::size_t) i].length - clip.length) <= 1)
            return i;
    return -1;
}

// FL's Chop › "Time based" chops ([H 0x1236260], the submenu FL builds once): bars, beats and parts of a beat, in FL's
// order. (FL's other groups — Chop into regions, Dull / Medium / Sharp auto-slicing, Trim outside selection, Repeating,
// Marching, Patterns, Complex — are not in LYNX yet.)
struct Chop
{
    const char* text;
    int beatDivisor; // 0 = a bar, else ppq / beatDivisor ticks
};

inline std::vector<Chop> timeBasedChops()
{
    return { { TR_NOOP ("Chop in bars"), 0 },        { TR_NOOP ("Chop in beats"), 1 },        { TR_NOOP ("Chop in half beats"), 2 }, { TR_NOOP ("Chop in quarter beats"), 4 },
             { TR_NOOP ("Chop in 8th beats"), 8 }, { TR_NOOP ("Chop in 16th beats"), 16 }, { TR_NOOP ("Chop in 32nd beats"), 32 } };
}
} // namespace lynx::flclipmenu
