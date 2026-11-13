#pragma once

// The project's key (round 184a, design D13; shared with round 183a's Clip properties column and GAPMAP #9 Scale Mode):
// ONE accessor over the one store, Song::scaleHighlight — FL's own project scale (.flp event 0xF3: FL's reference-scale
// index + root; −1 Default, −2 Black notes = FL's default and "no key" here, −3 Automatic). No other model holds a key:
// the Simple workflow's Key button, Scale keys and Chord pad, and the Clip-launch workflow's clip column all read and
// write it through these functions, so .lynx, .flp and live sessions carry it as they already do. Header only, no JUCE.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/NoteTools.h"
#include "core/Song.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text // marks the names for translation (ui/I18n.h defines the same)
#endif

namespace lynx::projectkey
{
struct Key
{
    int root = 0;    // 0 = C … 11 = B
    int flScale = 0; // index into notetools::flReferenceScales() (what .flp 0xF3 stores)
    bool operator== (const Key&) const = default;
};

constexpr int noKey = -2; // FL's "Black notes" highlight, its default: no key chosen

inline bool validFlScale (int flScale) noexcept { return flScale >= 0 && flScale < (int) notetools::flReferenceScales().size(); }

// The project's key; nullopt when none is chosen (FL's Default, Black notes, Automatic, or an index LYNX doesn't know).
inline std::optional<Key> get (const Song& song)
{
    const auto& h = song.scaleHighlight;
    if (! validFlScale (h.scale))
        return std::nullopt;
    return Key { std::clamp (h.root, 0, 11), h.scale };
}

// The scale's degrees above the root (FL's table), e.g. { 0, 2, 4, 5, 7, 9, 11 } for Major.
inline const std::vector<int>& intervals (const Key& key)
{
    static const std::vector<int> none;
    return validFlScale (key.flScale) ? notetools::flReferenceScales()[(std::size_t) key.flScale].intervals : none;
}

// --- loops that follow the key (round 190ds, core/KeyFollow.h) -----------------------------------------------------------
// A key is "minor" when its scale has a minor third and no major third (Minor, Dorian, Phrygian, minor pentatonic...).
inline bool isMinor (const Key& key)
{
    const auto& d = intervals (key);
    return std::find (d.begin(), d.end(), 3) != d.end() && std::find (d.begin(), d.end(), 4) == d.end();
}

// The semitones (−6 … +5) that move a loop in `loopRoot` (major, or minor) to the project's key: the nearest move between
// the two keys' relative-major roots, so A minor and C major (the same notes) need none, and G minor → D minor is −5.
inline int loopShift (int loopRoot, bool loopMinor, const Key& project)
{
    const auto from = ((loopRoot + (loopMinor ? 3 : 0)) % 12 + 12) % 12;
    const auto to = ((project.root + (isMinor (project) ? 3 : 0)) % 12 + 12) % 12;
    auto d = ((to - from) % 12 + 12) % 12; // 0..11
    return d >= 6 ? d - 12 : d;
}

// Moves every audio clip that follows the key (Clip::followKey, its channel's loop key known) to the project's key, and
// every clip that stopped following back to its own key: the move lives in the clip's pitch (Clip::keyShift = the part of
// it that is the key's), so the user's own pitch on top stays. Every clip list (each arrangement, the clip grid).
inline void retuneClip (Clip& c, const Song& song, const std::optional<Key>& project)
{
    if (c.kind != ClipKind::audio)
        return;
    int target = 0;
    if (c.followKey && project && c.channel >= 0 && c.channel < (int) song.channels.size())
        if (const auto& s = song.channels[(std::size_t) c.channel].sampler; s.loopKeyRoot >= 0)
            target = loopShift (s.loopKeyRoot, s.loopKeyMinor, *project);
    if (target == c.keyShift)
        return;
    const auto own = c.pitch - (float) c.keyShift;
    c.pitch = std::clamp (own + (float) target, -12.0f, 12.0f);
    c.keyShift = (std::int8_t) std::lround (c.pitch - own);
}

inline void retuneFollowers (Song& song)
{
    const auto project = get (song);
    song.forEachClipList ([&] (std::vector<Clip>& list) {
        for (auto& c : list)
            retuneClip (c, song, project);
    });
}

// Sets (or, with nullopt, clears) the project's key. Call inside AudioEngine::editSong: one undo step. An invalid scale
// clears the key; the root is wrapped into 0…11. Clips that follow the key move with it (round 190ds), in the same edit.
inline void set (Song& song, std::optional<Key> key)
{
    if (! key || ! validFlScale (key->flScale))
        song.scaleHighlight = { noKey, 0 };
    else
        song.scaleHighlight = { key->flScale, ((key->root % 12) + 12) % 12 };
    retuneFollowers (song);
}

// LYNX's own scale list index with the same degrees (notetools::scales(), what the Piano roll's snap uses); 0 = none listed.
inline int lynxScale (const Key& key) noexcept { return notetools::scaleForFlReference (key.flScale); }

// Whether a MIDI key is in the key's scale.
inline bool contains (const Key& key, int midiKey)
{
    const auto degree = ((midiKey - key.root) % 12 + 12) % 12;
    const auto& d = intervals (key);
    return std::find (d.begin(), d.end(), degree) != d.end();
}

// "A Minor Natural (Aeolian)": the root (sharps) and FL's scale name, English, for tr() of the scale part by the UI.
inline std::string rootName (int root)
{
    static const std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[(std::size_t) (((root % 12) + 12) % 12)];
}
inline std::string name (const Key& key)
{
    return validFlScale (key.flScale) ? rootName (key.root) + " " + notetools::flReferenceScales()[(std::size_t) key.flScale].name : std::string();
}

// The Key button's short scale name (English, for tr(); round 184a S11): "major", "minor", "blues"... for the scales the
// menu lists first; FL's own name for the rest. Empty for an invalid index.
inline std::string shortScaleName (int flScale)
{
    if (! validFlScale (flScale))
        return {};
    static const std::array<std::pair<const char*, const char*>, 10> shortNames { {
        { "Major (Ionian)", TR_NOOP ("major") }, { "Minor Natural (Aeolian)", TR_NOOP ("minor") }, { "Major Pentatonic", TR_NOOP ("major pentatonic") },
        { "Minor Pentatonic", TR_NOOP ("minor pentatonic") }, { "Other Blues", TR_NOOP ("blues") }, { "Minor Harmonic", TR_NOOP ("harmonic minor") },
        { "Other Dorian", TR_NOOP ("Dorian") }, { "Other Mixolydian", TR_NOOP ("Mixolydian") }, { "Other Phrygian", TR_NOOP ("Phrygian") }, { "Other Lydian", TR_NOOP ("Lydian") } } };
    const std::string full = notetools::flReferenceScales()[(std::size_t) flScale].name;
    for (const auto& [fl, shortName] : shortNames)
        if (full == fl)
            return shortName;
    return full;
}

// The Key menu's order: the common scales first (Major, Minor, the pentatonics, Blues, Harmonic minor, the modes), then
// the rest of FL's list in its own order. Every FL index appears once.
inline std::vector<int> commonFirst()
{
    static const std::vector<int> order = [] {
        const auto& list = notetools::flReferenceScales();
        std::vector<int> out;
        for (const char* wanted : { "Major (Ionian)", "Minor Natural (Aeolian)", "Major Pentatonic", "Minor Pentatonic", "Other Blues",
                                    "Minor Harmonic", "Other Dorian", "Other Mixolydian", "Other Phrygian", "Other Lydian" })
            for (int i = 0; i < (int) list.size(); ++i)
                if (std::string (list[(std::size_t) i].name) == wanted)
                    out.push_back (i);
        for (int i = 0; i < (int) list.size(); ++i)
            if (std::find (out.begin(), out.end(), i) == out.end())
                out.push_back (i);
        return out;
    }();
    return order;
}
} // namespace lynx::projectkey
