#pragma once

// Scale keys, Chord pad and Lock to key (round 190ds; design /mnt/e/lynx-work/simple184a/DESIGN.md §3.2, slice S12):
// the on-screen keyboard's three modes and what the typing keyboard plays in them. LYNX's own take on the "play in key"
// keyboards of beginner apps (one key per scale degree; one pad per in-key chord; out-of-key keys locked), clean-room,
// own names. Everything reads the ONE project key (core/ProjectKey.h); nothing here stores a key of its own.
//
//  - Keys        the usual piano. With "Lock to key" the keys outside the project's scale don't play (and are drawn
//                dimmed): an FL Studio Mobile-style key locker.
//  - Scale keys  one key per scale degree (7 per octave for a seven-note scale, 5 for a pentatonic...). A note out of the
//                key can't be played. The typing keyboard's white-key rows play the degrees (Z = the root).
//  - Chord pad   one pad per degree of the seven-note scale (a pentatonic or blues key uses its major / minor parent):
//                the diatonic chord, as a triad, seventh, ninth, sus4 or add9, close or spread; or the project's own
//                chord set (Song::keyboardChords: imported from an .flm file's KEYB chunk, or edited on a pad).
//
// Header only, no JUCE.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "core/ProjectKey.h"
#include "core/Song.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::scalekeys
{
enum class Mode : int { keys = 0, scaleKeys = 1, chordPad = 2 };

enum class Complexity : int { triad = 0, seventh = 1, ninth = 2, sus4 = 3, add9 = 4 };
constexpr int complexityCount = 5;
inline const char* complexityName (Complexity c) noexcept
{
    switch (c)
    {
        case Complexity::triad: return TR_NOOP ("Triads");
        case Complexity::seventh: return TR_NOOP ("Sevenths");
        case Complexity::ninth: return TR_NOOP ("Ninths");
        case Complexity::sus4: return TR_NOOP ("Sus4");
        case Complexity::add9: return TR_NOOP ("Add9");
    }
    return "";
}

enum class Spread : int { close = 0, open = 1, wide = 2 };
constexpr int spreadCount = 3;
inline const char* spreadName (Spread s) noexcept
{
    return s == Spread::close ? TR_NOOP ("Close") : s == Spread::open ? TR_NOOP ("Open") : TR_NOOP ("Wide");
}

// The keyboard's settings: a UI setting of the app (ui-options.txt), not of the project, shared by the on-screen
// keyboard (Touch keyboard window and the Simple dock's Keyboard tab) and the typing keyboard.
struct Settings
{
    Mode mode = Mode::keys;
    bool lockToKey = false; // Keys mode: out-of-key keys don't play
    Complexity complexity = Complexity::triad;
    Spread spread = Spread::close;
    int strumMs = 0;          // 0..120: the chord's notes start this far apart
    bool strumDown = false;   // false: lowest note first
    bool projectChords = false; // Chord pad plays Song::keyboardChords (when the project has some)
    bool operator== (const Settings&) const = default;
};

inline Settings& current() noexcept
{
    static Settings settings;
    return settings;
}

constexpr int maxStrumMs = 120;

// --- the scale ------------------------------------------------------------------------------------------------------

inline int wrap12 (int n) noexcept { return ((n % 12) + 12) % 12; }

// Note `index` of the key's scale counted from `rootNote` (index 0 = rootNote, which must be the key's root): negative
// indices go down, indices past the scale's size go up an octave each time round.
inline int scaleNote (const projectkey::Key& key, int index, int rootNote)
{
    const auto& d = projectkey::intervals (key);
    if (d.empty())
        return rootNote + index;
    const auto n = (int) d.size();
    const auto octave = index >= 0 ? index / n : -((-index + n - 1) / n);
    const auto step = index - octave * n;
    return rootNote + 12 * octave + d[(std::size_t) step];
}

inline int degreeCount (const projectkey::Key& key) { return (int) projectkey::intervals (key).size(); }

// The key's root at or above `note` (the first root not below it)
inline int rootAtOrAbove (const projectkey::Key& key, int note) noexcept { return note + wrap12 (key.root - note); }

// --- chords ---------------------------------------------------------------------------------------------------------

// The seven-note scale chords are built from: the key's own when it has seven notes, else its parent (Major when it has
// a major third, else natural Minor) on the same root.
inline std::vector<int> chordScale (const projectkey::Key& key)
{
    const auto& d = projectkey::intervals (key);
    if (d.size() == 7)
        return d;
    const bool major = std::find (d.begin(), d.end(), 4) != d.end() && std::find (d.begin(), d.end(), 3) == d.end();
    return major ? std::vector<int> { 0, 2, 4, 5, 7, 9, 11 } : std::vector<int> { 0, 2, 3, 5, 7, 8, 10 };
}

inline int chordScaleNote (const std::vector<int>& seven, int index, int rootNote)
{
    const auto octave = index >= 0 ? index / 7 : -((-index + 6) / 7);
    return rootNote + 12 * octave + seven[(std::size_t) (index - octave * 7)];
}

inline void applySpread (std::vector<int>& notes, Spread spread)
{
    if (notes.size() < 3 || spread == Spread::close)
        return;
    notes[1] += 12; // open: the third goes up an octave
    if (spread == Spread::wide)
        notes.insert (notes.begin(), notes.front() - 12); // and the root doubled an octave below
    std::sort (notes.begin(), notes.end());
}

inline void keepInMidi (std::vector<int>& notes)
{
    notes.erase (std::remove_if (notes.begin(), notes.end(), [] (int n) { return n < 0 || n > 127; }), notes.end());
}

// The diatonic chord on degree `degree` (0 = the root; may pass 6 to reach the next octave) with its root note at or
// above `baseNote`'s octave: rootNote = the key's root in the octave of `baseNote` (at or above it).
inline std::vector<int> diatonicChord (const projectkey::Key& key, int degree, int baseNote, Complexity complexity, Spread spread)
{
    const auto seven = chordScale (key);
    const auto root = rootAtOrAbove (key, baseNote);
    auto at = [&] (int i) { return chordScaleNote (seven, degree + i, root); };
    std::vector<int> notes;
    switch (complexity)
    {
        case Complexity::triad: notes = { at (0), at (2), at (4) }; break;
        case Complexity::seventh: notes = { at (0), at (2), at (4), at (6) }; break;
        case Complexity::ninth: notes = { at (0), at (2), at (4), at (6), at (8) }; break;
        case Complexity::sus4: notes = { at (0), at (3), at (4) }; break;
        case Complexity::add9: notes = { at (0), at (2), at (4), at (8) }; break;
    }
    applySpread (notes, spread);
    keepInMidi (notes);
    return notes;
}

// A chord of the project's own chord set (Song::keyboardChords): quality 0 major, 1 minor, 2 augmented, 3 diminished,
// 4 sus2, 5 sus4 (0, 1 and 3 as FL Studio Mobile's KEYB: its default set is C Dm Em F G Am B°=3 C); extensions are
// semitones above the root. `baseNote` = where the set's lowest octave plays.
inline std::vector<int> qualityIntervals (int quality)
{
    switch (quality)
    {
        case 1: return { 0, 3, 7 };
        case 2: return { 0, 4, 8 };
        case 3: return { 0, 3, 6 };
        case 4: return { 0, 2, 7 };
        case 5: return { 0, 5, 7 };
        default: return { 0, 4, 7 };
    }
}
constexpr int qualityCount = 6;
inline const char* qualityName (int quality) noexcept
{
    static const char* names[] { TR_NOOP ("major"), TR_NOOP ("minor"), TR_NOOP ("augmented"), TR_NOOP ("diminished"), "sus2", "sus4" };
    return names[std::clamp (quality, 0, qualityCount - 1)];
}

inline int lowestOctave (const std::vector<KeyboardChord>& set) noexcept
{
    int low = 100;
    for (const auto& c : set)
        low = std::min (low, c.octave);
    return set.empty() ? 0 : low;
}

inline std::vector<int> setChord (const std::vector<KeyboardChord>& set, int index, int baseNote, Spread spread)
{
    if (index < 0 || index >= (int) set.size())
        return {};
    const auto& c = set[(std::size_t) index];
    const auto base = baseNote - wrap12 (baseNote) + 12 * (c.octave - lowestOctave (set));
    std::vector<int> notes;
    for (auto i : qualityIntervals (c.quality))
        notes.push_back (base + wrap12 (c.root) + i);
    for (auto e : c.extensions)
        if (e > 0 && e < 36)
            notes.push_back (base + wrap12 (c.root) + e);
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
    applySpread (notes, spread);
    keepInMidi (notes);
    return notes;
}

// The chord set the Chord pad shows when it starts one from the diatonic chords ("Edit chord..."): the seven triads of
// the key (seven-note parent), octave 4.
inline std::vector<KeyboardChord> diatonicSet (const projectkey::Key& key)
{
    std::vector<KeyboardChord> out;
    for (int d = 0; d < 7; ++d)
    {
        const auto notes = diatonicChord (key, d, 0, Complexity::triad, Spread::close);
        KeyboardChord c;
        c.root = wrap12 (notes[0]);
        const auto third = notes[1] - notes[0], fifth = notes[2] - notes[0];
        c.quality = third == 4 ? (fifth == 8 ? 2 : 0) : (fifth == 6 ? 3 : 1);
        c.octave = 4 + (notes[0] >= 12 ? 1 : 0);
        out.push_back (c);
    }
    return out;
}

// The pad's numeral against the key: "I", "ii", "vii°", "III+" (from the chord's third and fifth above its root)
inline std::string numeral (int degree, const std::vector<int>& notes)
{
    static const std::array<const char*, 7> upper { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const std::array<const char*, 7> lower { "i", "ii", "iii", "iv", "v", "vi", "vii" };
    const auto d = (std::size_t) ((degree % 7 + 7) % 7);
    if (notes.size() < 3)
        return upper[d];
    auto has = [&] (int interval) {
        for (std::size_t i = 1; i < notes.size(); ++i)
            if (wrap12 (notes[i] - notes[0]) == interval)
                return true;
        return false;
    };
    const auto major = has (4), minor = has (3);
    if (minor && has (6) && ! has (7))
        return std::string (lower[d]) + "\xc2\xb0"; // °
    if (major && has (8) && ! has (7))
        return std::string (upper[d]) + "+";
    return minor && ! major ? lower[d] : upper[d];
}

// --- what a key plays -------------------------------------------------------------------------------------------------

// How many pads the Chord pad shows: the chord set's size, or the seven degrees
inline int padCount (const Song& song, const Settings& s)
{
    return s.projectChords && ! song.keyboardChords.empty() ? (int) song.keyboardChords.size() : 7;
}

// Pad `pad`'s notes with its lowest root at or above `baseNote`. Empty with no key (diatonic) or a pad out of range.
inline std::vector<int> padNotes (const Song& song, const Settings& s, int pad, int baseNote)
{
    if (s.projectChords && ! song.keyboardChords.empty())
        return setChord (song.keyboardChords, pad, baseNote, s.spread);
    const auto key = projectkey::get (song);
    if (! key || pad < 0)
        return {};
    return diatonicChord (*key, pad, baseNote, s.complexity, s.spread);
}

// What the typing keyboard plays for one of its notes (`typed`, as FL's layout gives it) in the current mode. `baseC` =
// the C the layout's Z key plays (the typing octave): the white keys from it are the degrees (Z = the root or the first
// pad, X the next...), black keys play nothing in Scale keys / Chord pad. Keys mode: the note itself, or nothing when
// Lock to key is on and the note is out of the key. With no project key every mode plays the note itself.
inline std::vector<int> typedNotes (const Song& song, const Settings& s, int typed, int baseC)
{
    const auto key = projectkey::get (song);
    if (! key || typed < 0 || typed > 127)
        return { typed };
    if (s.mode == Mode::keys)
    {
        if (s.lockToKey && ! projectkey::contains (*key, typed))
            return {};
        return { typed };
    }
    static constexpr int whiteOf[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    const auto pc = wrap12 (typed);
    if (whiteOf[pc] < 0)
        return {};
    const auto octave = (typed - pc) / 12 - (baseC - wrap12 (baseC)) / 12;
    const auto index = octave * 7 + whiteOf[pc];
    const auto base = baseC - wrap12 (baseC);
    if (s.mode == Mode::scaleKeys)
    {
        const auto n = scaleNote (*key, index, rootAtOrAbove (*key, base));
        return n >= 0 && n <= 127 ? std::vector<int> { n } : std::vector<int> {};
    }
    const auto pads = padCount (song, s);
    if (pads <= 0 || index < 0)
        return {};
    return padNotes (song, s, index % pads, base + 12 * (index / pads));
}

// The order a pad's notes start in and each one's delay (ms) for the strum
inline std::vector<std::pair<int, int>> strummed (std::vector<int> notes, const Settings& s)
{
    std::sort (notes.begin(), notes.end());
    if (s.strumDown)
        std::reverse (notes.begin(), notes.end());
    std::vector<std::pair<int, int>> out;
    const auto n = (int) notes.size();
    const auto span = std::clamp (s.strumMs, 0, maxStrumMs);
    for (int i = 0; i < n; ++i)
        out.push_back ({ notes[(std::size_t) i], n > 1 ? span * i / (n - 1) : 0 });
    return out;
}

// FL Studio Mobile's keyboard chord set (an .flm file's KEYB chunk: {"chords": [{"baseNote", "extensions", "octave",
// "quality"}...], "rootNote"}, docs/analise/FLM_FORMAT.md §2) as LYNX's. Its qualities 0 major, 1 minor and 3 diminished
// are proven by FLM's default set (C Dm Em F G Am B°); 2 is taken as augmented, 4 / 5 as sus2 / sus4 (not seen in a
// file). FLM's extension codes are not known: `droppedExtensions` counts the chords that had some (they play as
// triads). A small scanner, not a JSON library: anything it doesn't understand is skipped. At most 24 chords.
inline std::vector<KeyboardChord> parseFlmChordSet (const std::string& json, int* droppedExtensions = nullptr)
{
    std::vector<KeyboardChord> out;
    if (droppedExtensions != nullptr)
        *droppedExtensions = 0;
    const auto list = json.find ("\"chords\"");
    if (list == std::string::npos)
        return out;
    auto pos = json.find ('[', list);
    auto number = [] (const std::string& obj, const char* name, int fallback) {
        const auto k = obj.find (std::string ("\"") + name + "\"");
        if (k == std::string::npos)
            return fallback;
        auto i = obj.find (':', k);
        if (i == std::string::npos)
            return fallback;
        ++i;
        while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\n' || obj[i] == '\r' || obj[i] == '\t'))
            ++i;
        char* end = nullptr;
        const auto v = std::strtol (obj.c_str() + i, &end, 10);
        return end != obj.c_str() + i ? (int) v : fallback;
    };
    while (pos != std::string::npos && out.size() < 24)
    {
        const auto open = json.find ('{', pos), close = json.find ('}', pos);
        if (open == std::string::npos || close == std::string::npos || open > close)
            break;
        const auto obj = json.substr (open, close - open + 1);
        KeyboardChord c;
        c.root = wrap12 (number (obj, "baseNote", 0));
        c.quality = std::clamp (number (obj, "quality", 0), 0, qualityCount - 1);
        c.octave = std::clamp (number (obj, "octave", 4), 0, 9);
        if (const auto e = obj.find ("\"extensions\""); e != std::string::npos)
        {
            const auto a = obj.find ('[', e), z = obj.find (']', e);
            if (a != std::string::npos && z != std::string::npos && obj.find_first_of ("0123456789", a) < z && droppedExtensions != nullptr)
                ++*droppedExtensions;
        }
        out.push_back (std::move (c));
        pos = close + 1;
        // the chords array ends at the first ']' that is not an extensions list
        const auto nextObj = json.find ('{', pos), nextEnd = json.find (']', pos);
        if (nextEnd != std::string::npos && (nextObj == std::string::npos || nextEnd < nextObj))
            break;
    }
    return out;
}

// The labels of the typing keys over the first degrees / pads (FL's layout: the Z row's white keys, then the Q row's)
inline const char* typingLabel (int index) noexcept
{
    static const char* labels[] { "Z", "X", "C", "V", "B", "N", "M", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" };
    return index >= 0 && index < (int) (sizeof (labels) / sizeof (labels[0])) ? labels[index] : "";
}
} // namespace lynx::scalekeys
