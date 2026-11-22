#pragma once

// The Chord lane (round 190pf, research190oe P13): chords over the song as blocks on a lane under the Playlist's ruler
// (Cubase's Chord Track, Logic's Chord track, Studio Pro's Chord track; Live users ask for one, research190oe L9). A LYNX
// extra: FL Studio and Live have no chord track. The lane's chords
//  - name the harmony of each stretch of the song (drawn by hand, picked from the key's degree chords, detected from the
//    notes of the clips under a range or from an audio clip, made from the key regions);
//  - make the channels that follow them (ChannelDef::followChords) play their notes moved onto the chord under them while
//    the song plays: the pattern keeps its notes (Cubase's "Follow Chord Track" / Studio Pro's "Follow chords" re-pitch
//    what plays; LYNX does it in the scheduler, so a pattern placed under four chords plays four ways);
//  - light the Piano roll's rows of the chord under the song position (an option of its menu), feed the Simple layout's
//    Chord pad (its chord set), and leave as MIDI notes or .flp markers.
// The model is Song::chords() (SongChord in Song.h): per arrangement, sorted by start, no overlaps, gaps allowed. The
// time commands keep it in step (insertTime / deleteTime here, called beside the Sections lane's).
//
// Header only, no JUCE. The commands that need the Piano roll tools, the key regions or ArrangeOps are in
// core/ChordLaneOps.h. Tested in tests/ChordLane190pfTests.inc.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::chordlane
{
inline int wrap12 (int n) noexcept { return ((n % 12) + 12) % 12; }

// --- the chord shapes ------------------------------------------------------------------------------------------------
// The shapes the lane names and offers (the picker's list, in this order), as the Chord pad's sets store chords: a quality
// (core/ScaleKeys.h: 0 major, 1 minor, 2 augmented, 3 diminished, 4 sus2, 5 sus4, 6 power chord) and extensions above the
// root. `suffix` is the name after the root, as prtools::detectChord writes it ("m7", "maj7"...).
struct Shape
{
    int quality = 0;
    std::array<int, 3> extensions { 0, 0, 0 }; // 0 = none
    const char* suffix = "";
};

inline constexpr Shape shapes[] = {
    { 0, { 0, 0, 0 }, "" },       { 1, { 0, 0, 0 }, "m" },       { 0, { 10, 0, 0 }, "7" },     { 0, { 11, 0, 0 }, "maj7" },
    { 1, { 10, 0, 0 }, "m7" },    { 3, { 0, 0, 0 }, "dim" },     { 3, { 10, 0, 0 }, "m7b5" },  { 3, { 9, 0, 0 }, "dim7" },
    { 2, { 0, 0, 0 }, "aug" },    { 4, { 0, 0, 0 }, "sus2" },    { 5, { 0, 0, 0 }, "sus4" },   { 5, { 10, 0, 0 }, "7sus4" },
    { 0, { 9, 0, 0 }, "6" },      { 1, { 9, 0, 0 }, "m6" },      { 0, { 14, 0, 0 }, "add9" },  { 1, { 14, 0, 0 }, "madd9" },
    { 0, { 10, 14, 0 }, "9" },    { 0, { 11, 14, 0 }, "maj9" },  { 1, { 10, 14, 0 }, "m9" },   { 1, { 11, 0, 0 }, "m(maj7)" },
    { 2, { 10, 0, 0 }, "7#5" },   { 2, { 11, 0, 0 }, "maj7#5" }, { 0, { 10, 13, 0 }, "7b9" },  { 0, { 10, 15, 0 }, "7#9" },
    { 0, { 9, 14, 0 }, "6/9" },   { 5, { 10, 14, 0 }, "9sus4" }, { 1, { 10, 14, 17 }, "m11" }, { 0, { 10, 21, 0 }, "13" },
    { 6, { 0, 0, 0 }, "5" },
};
inline constexpr int shapeCount = (int) (sizeof (shapes) / sizeof (shapes[0]));

// The triad of a quality, above the root (core/ScaleKeys.h's qualityIntervals, without allocating)
inline int qualityMask (int quality) noexcept
{
    switch (quality)
    {
        case 1: return (1 << 0) | (1 << 3) | (1 << 7);
        case 2: return (1 << 0) | (1 << 4) | (1 << 8);
        case 3: return (1 << 0) | (1 << 3) | (1 << 6);
        case 4: return (1 << 0) | (1 << 2) | (1 << 7);
        case 5: return (1 << 0) | (1 << 5) | (1 << 7);
        case 6: return (1 << 0) | (1 << 7);
        default: return (1 << 0) | (1 << 4) | (1 << 7);
    }
}

// The chord's pitch classes as a 12-bit mask (bit 0 = C); the bass note is one of them. No allocation (the audio thread).
inline int pitchClasses (const SongChord& c) noexcept
{
    int rel = qualityMask (c.quality);
    for (const auto e : c.extensions)
        if (e > 0)
            rel |= 1 << (e % 12);
    int mask = 0;
    for (int i = 0; i < 12; ++i)
        if (rel & (1 << i))
            mask |= 1 << wrap12 (c.root + i);
    if (c.bass >= 0)
        mask |= 1 << wrap12 (c.bass);
    return mask;
}

// The chord's notes above its root (root position, extensions in place: 0, 4, 7, 10, 14...), sorted, no repeats
inline std::vector<int> intervals (const SongChord& c)
{
    std::vector<int> out;
    const auto triad = qualityMask (c.quality);
    for (int i = 0; i < 12; ++i)
        if (triad & (1 << i))
            out.push_back (i);
    for (const auto e : c.extensions)
        if (e > 0 && e < 36)
            out.push_back (e);
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

inline int shapeOf (const SongChord& c) noexcept
{
    auto ext = c.extensions;
    std::sort (ext.begin(), ext.end());
    for (int i = 0; i < shapeCount; ++i)
    {
        const auto& s = shapes[i];
        if (s.quality != c.quality)
            continue;
        std::vector<int> want;
        for (const auto e : s.extensions)
            if (e > 0)
                want.push_back (e);
        if (want == ext)
            return i;
    }
    return -1;
}

inline SongChord make (int root, int shape, int bass = -1)
{
    SongChord c;
    c.root = wrap12 (root);
    const auto& s = shapes[std::clamp (shape, 0, shapeCount - 1)];
    c.quality = s.quality;
    for (const auto e : s.extensions)
        if (e > 0)
            c.extensions.push_back (e);
    c.bass = bass < 0 || wrap12 (bass) == c.root ? -1 : wrap12 (bass);
    return c;
}

inline const char* rootName (int pc, bool flats) noexcept
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flat[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (flats ? flat : sharps)[wrap12 (pc)];
}

// "C", "Am7", "Bbmaj7", "G/B": the lane's name of a chord (a shape outside the list: its triad's name plus "add" numbers)
inline std::string name (const SongChord& c, bool flats = false)
{
    std::string s = rootName (c.root, flats);
    if (const auto i = shapeOf (c); i >= 0)
        s += shapes[i].suffix;
    else
    {
        s += c.quality == 1 ? "m" : c.quality == 2 ? "aug" : c.quality == 3 ? "dim" : c.quality == 4 ? "sus2" : c.quality == 5 ? "sus4" : c.quality == 6 ? "5" : "";
        for (const auto e : c.extensions)
            if (e > 0)
                s += "(" + std::to_string (e) + ")";
    }
    if (c.bass >= 0 && wrap12 (c.bass) != wrap12 (c.root))
        s += std::string ("/") + rootName (c.bass, flats);
    return s;
}

// The chord named by prtools::detectChord's suffix ("", "m7", "maj7"...) on `root`; false when the lane has no such shape
inline bool fromSuffix (int root, const std::string& suffix, int bass, SongChord& out)
{
    for (int i = 0; i < shapeCount; ++i)
        if (suffix == shapes[i].suffix)
        {
            out = make (root, i, bass);
            return true;
        }
    if (suffix == "7b5") // (a rare shape the lane doesn't list: its nearest, the dominant seventh)
    {
        out = make (root, 2, bass);
        return true;
    }
    return false;
}

// --- the list ----------------------------------------------------------------------------------------------------------

// The chord that holds `tick` (start <= tick < end), or -1. Binary search, no allocation (the audio thread).
inline int indexAt (const std::vector<SongChord>& list, std::int64_t tick) noexcept
{
    auto it = std::upper_bound (list.begin(), list.end(), tick, [] (std::int64_t t, const SongChord& c) { return t < c.start; });
    if (it == list.begin())
        return -1;
    --it;
    return tick < it->end() ? (int) (it - list.begin()) : -1;
}

inline int indexOfUid (const std::vector<SongChord>& list, std::uint64_t uid) noexcept
{
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(std::size_t) i].uid == uid)
            return i;
    return -1;
}

inline std::uint64_t newUid (const std::vector<SongChord>& list) noexcept
{
    std::uint64_t most = 0;
    for (const auto& c : list)
        most = std::max (most, c.uid);
    return most + 1;
}

// Sorted, no empty chord, no overlap (a chord that runs into the next one ends where it starts), a uid on each, the
// root / bass wrapped into 0..11 (bass = root becomes -1).
inline void normalize (std::vector<SongChord>& list)
{
    list.erase (std::remove_if (list.begin(), list.end(), [] (const SongChord& c) { return c.length <= 0 || c.start < 0; }), list.end());
    std::stable_sort (list.begin(), list.end(), [] (const SongChord& a, const SongChord& b) { return a.start < b.start; });
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
        if (list[i].end() > list[i + 1].start)
            list[i].length = list[i + 1].start - list[i].start;
    list.erase (std::remove_if (list.begin(), list.end(), [] (const SongChord& c) { return c.length <= 0; }), list.end());
    for (auto& c : list)
    {
        c.root = wrap12 (c.root);
        c.quality = std::clamp (c.quality, 0, 6);
        c.bass = c.bass < 0 || wrap12 (c.bass) == c.root ? -1 : wrap12 (c.bass);
        if (c.uid == 0 || std::count_if (list.begin(), list.end(), [&c] (const SongChord& o) { return o.uid == c.uid; }) > 1)
            c.uid = newUid (list);
    }
}

// [from, to) leaves the lane's chords: what of a chord lies inside goes (one inside it is cut in two), nothing moves.
inline void clearRange (std::vector<SongChord>& list, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;
    std::vector<SongChord> out;
    for (const auto& c : list)
    {
        if (c.end() <= from || c.start >= to)
        {
            out.push_back (c);
            continue;
        }
        if (c.start < from)
        {
            auto left = c;
            left.length = from - c.start;
            out.push_back (left);
        }
        if (c.end() > to)
        {
            auto right = c;
            right.start = to;
            right.length = c.end() - to;
            right.uid = 0; // (a new block)
            out.push_back (right);
        }
    }
    list = std::move (out);
    normalize (list);
}

// Puts `chord` on the lane over [chord.start, chord.end()): what was there gives way. Its index.
inline int put (std::vector<SongChord>& list, SongChord chord)
{
    if (chord.length <= 0)
        return -1;
    clearRange (list, chord.start, chord.end());
    chord.uid = newUid (list);
    list.push_back (chord);
    normalize (list);
    return indexOfUid (list, chord.uid);
}

// Sets chord `index`'s harmony (root, shape, bass), keeping its place
inline void setChord (std::vector<SongChord>& list, int index, const SongChord& harmony)
{
    if (index < 0 || index >= (int) list.size())
        return;
    auto& c = list[(std::size_t) index];
    c.root = harmony.root;
    c.quality = harmony.quality;
    c.extensions = harmony.extensions;
    c.bass = harmony.bass;
    normalize (list);
}

// Moves chord `index` to start at `start` (what it lands on gives way). Its new index.
inline int move (std::vector<SongChord>& list, int index, std::int64_t start)
{
    if (index < 0 || index >= (int) list.size())
        return -1;
    auto c = list[(std::size_t) index];
    list.erase (list.begin() + index);
    c.start = std::max<std::int64_t> (0, start);
    const auto uid = c.uid;
    clearRange (list, c.start, c.end());
    if (indexOfUid (list, uid) >= 0) // (a piece of another chord took its uid)
        c.uid = newUid (list);
    list.push_back (c);
    normalize (list);
    return indexOfUid (list, c.uid);
}

// Sets chord `index`'s end (at least one tick after its start; the next chord gives way)
inline void resize (std::vector<SongChord>& list, int index, std::int64_t end)
{
    if (index < 0 || index >= (int) list.size())
        return;
    auto c = list[(std::size_t) index];
    c.length = std::max<std::int64_t> (1, end - c.start);
    list.erase (list.begin() + index);
    clearRange (list, c.start, c.end());
    list.push_back (c);
    normalize (list);
}

inline void remove (std::vector<SongChord>& list, int index)
{
    if (index >= 0 && index < (int) list.size())
        list.erase (list.begin() + index);
}

// --- keeping the lane in step with the time commands ---------------------------------------------------------------------

// `length` ticks open at `at`: chords from there on move right; one that holds `at` inside grows (as the Sections lane's).
inline void insertTime (std::vector<SongChord>& list, std::int64_t at, std::int64_t length)
{
    if (length <= 0)
        return;
    for (auto& c : list)
        if (c.start >= at)
            c.start += length;
        else if (c.end() > at)
            c.length += length;
}

// [from, to) leaves the arrangement: what of a chord lies inside goes, later ones move left.
inline void deleteTime (std::vector<SongChord>& list, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;
    const auto cut = to - from;
    for (auto& c : list)
    {
        const auto start = c.start, end = c.end();
        const auto keepStart = start < from ? start : (start < to ? from : start - cut);
        const auto keepEnd = end <= from ? end : (end <= to ? from : end - cut);
        c.start = keepStart;
        c.length = keepEnd - keepStart;
    }
    list.erase (std::remove_if (list.begin(), list.end(), [] (const SongChord& c) { return c.length <= 0; }), list.end());
    normalize (list); // (two chords that met at the cut stay two)
}

// The chords of [from, to), cut to it, starts counted from `from` (ArrangeOps' Copy Time)
inline std::vector<SongChord> piece (const std::vector<SongChord>& list, std::int64_t from, std::int64_t to)
{
    std::vector<SongChord> out;
    for (const auto& c : list)
    {
        const auto a = std::max (c.start, from), b = std::min (c.end(), to);
        if (b <= a)
            continue;
        auto x = c;
        x.start = a - from;
        x.length = b - a;
        x.uid = 0;
        out.push_back (x);
    }
    return out;
}

// Places a piece at `at` (Paste Time: the time is open already), over what is there
inline void pastePiece (std::vector<SongChord>& list, std::int64_t at, const std::vector<SongChord>& part)
{
    for (auto c : part)
    {
        c.start += at;
        put (list, c);
    }
}

// --- following the chords (ChannelDef::followChords) ---------------------------------------------------------------------
enum Follow : std::uint8_t
{
    followOff = 0,
    followChordTones = 1, // each note to the nearest note of the chord (a tie goes down)
    followBass = 2        // each note to the nearest chord bass note (the slash bass, else the root; a tie goes down)
};

// `key` moved onto `chord` as `mode` asks (the octave it is in is kept as far as the nearest note allows)
inline int follow (int key, const SongChord& chord, int mode) noexcept
{
    if (mode == followOff)
        return key;
    const auto mask = mode == followBass ? 1 << wrap12 (chord.bass >= 0 ? chord.bass : chord.root) : pitchClasses (chord);
    if (mask == 0)
        return key;
    for (int d = 0; d <= 6; ++d)
    {
        if (key - d >= 0 && (mask & (1 << wrap12 (key - d))))
            return key - d;
        if (key + d <= 127 && (mask & (1 << wrap12 (key + d))))
            return key + d;
    }
    return key;
}

// The key a note of `channel` plays at song tick `tick`: moved onto the chord there when the channel follows the lane.
// Inline, no allocation: called by the scheduler for every note it starts (one test of a byte when nothing follows).
inline int playedKey (const Song& song, const ChannelDef* channel, int key, std::int64_t tick) noexcept
{
    if (channel == nullptr || channel->followChords == followOff)
        return key;
    const auto& list = song.chords();
    const auto i = indexAt (list, tick);
    return i < 0 ? key : std::clamp (follow (key, list[(std::size_t) i], channel->followChords), 0, 127);
}

// --- the Piano roll's rows -----------------------------------------------------------------------------------------------
// The chord the Piano roll lights at song position `tick` (its option "Highlight the Chord lane's chord"): its root and its
// pitch-class mask, or root -1 when no chord is there.
struct Highlight
{
    int root = -1, mask = 0;
    bool operator== (const Highlight&) const = default;
};
inline Highlight highlightAt (const Song& song, std::int64_t tick) noexcept
{
    const auto& list = song.chords();
    const auto i = indexAt (list, tick);
    if (i < 0)
        return {};
    return { list[(std::size_t) i].root, pitchClasses (list[(std::size_t) i]) };
}

// --- the lane's visibility (a LYNX extra in every layout, off by default: Playlist menu › View › "Chord lane", Settings ›
// LYNX extras, ui-options.txt "chordLane"; the Simple layout's ruler menu has it too) ------------------------------------
inline bool& laneOn() noexcept
{
    static bool on = false;
    return on;
}
// The Piano roll's option (its menu › View, ui-options.txt "prChordHighlight"): its rows show the chord under the song
// position instead of the scale
inline bool& pianoRollHighlight() noexcept
{
    static bool on = false;
    return on;
}
} // namespace lynx::chordlane
