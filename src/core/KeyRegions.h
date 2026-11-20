#pragma once

// Key regions on the Piano roll ruler (round 190nu, GitHub discussion #105; docs/analise/REA_rodada190nu-keys.md).
//
// FL Studio 2026 has them as pattern time markers of action "Key/Scale" (marker type 12): Piano roll ▾ › Time markers ›
// Add key marker ▸ (a Root note column and a Scale column, as View › Scale highlighting), and on any marker's menu
// Action › Key/scale ▸. The marker is named after its key ("C Major (Ionian)"), and from it on the grid's rows are shaded
// for that key until the next key marker (FL check 190nu: a C Major marker at bar 1 and a C Minor Natural one at bar 3
// shade the rows of each key on each side). The .flp keeps the key in the marker's own events (190nu, a project saved by
// FL 2026): 0x2E the root (0 = C), 0xA8 FL's reference-scale index, 0x65 the out-of-scale keys as 12 bits (bit 11 − pitch
// class, root included: 1322 = C major's C# D# F# G# A#), 0xCD the name.
//
// LYNX keeps the key on the Marker (keyRoot, keyFlScale: an index into notetools::flReferenceScales(), or −1 Default = the
// Piano roll's own scale from here, −2 Black notes = no scale from here). Before the first key marker the Piano roll's own
// scale applies (the toolbar's, the project key). LYNX extras on top: a key region over a time selection (a key marker at
// its start and one at its end that brings back what was there), the regions in MIDI files (key signature meta events,
// with a marker meta event "Key: <name>" that keeps the exact scale for LYNX), and the Chord stamp's "Scale degree chord"
// that builds the clicked degree's chord in the region's key (diatonicChord below).
//
// Header only, no JUCE.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/ProjectKey.h"
#include "core/Song.h"

namespace lynx::keyregions
{
using Key = projectkey::Key;

constexpr int defaultScale = -1; // FL's "Default": the Piano roll's own scale applies again from the marker
constexpr int noScale = -2;      // FL's "Black notes": no scale from the marker on

inline bool isKeyMarker (const Marker& m) noexcept { return m.action == Marker::keyScale; }

// The key a key marker sets; nullopt for Default / Black notes (see state()).
inline std::optional<Key> keyOf (const Marker& m)
{
    if (! isKeyMarker (m) || ! projectkey::validFlScale (m.keyFlScale))
        return std::nullopt;
    return Key { ((m.keyRoot % 12) + 12) % 12, m.keyFlScale };
}

// What applies at a point: the editor's own scale (before any key marker, or after a Default one), no scale, or a key.
struct State
{
    enum Kind { editor, none, key } kind = editor;
    Key value {};
    bool operator== (const State& o) const noexcept { return kind == o.kind && (kind != key || value == o.value); }
};

inline State stateOf (const Marker& m)
{
    if (const auto k = keyOf (m))
        return { State::key, *k };
    return { m.keyFlScale == noScale ? State::none : State::editor, {} };
}

inline bool hasKeyMarkers (const Pattern& p) noexcept
{
    return std::any_of (p.markers.begin(), p.markers.end(), [] (const Marker& m) { return isKeyMarker (m); });
}

// The key marker in force at `tick` (the last one at or before it); −1 when none.
inline int markerIndexAt (const Pattern& p, std::int64_t tick) noexcept
{
    int found = -1;
    for (int i = 0; i < (int) p.markers.size(); ++i)
        if (isKeyMarker (p.markers[(std::size_t) i]) && p.markers[(std::size_t) i].tick <= tick)
            if (found < 0 || p.markers[(std::size_t) i].tick >= p.markers[(std::size_t) found].tick)
                found = i;
    return found;
}

inline State stateAt (const Pattern& p, std::int64_t tick)
{
    const auto i = markerIndexAt (p, tick);
    return i < 0 ? State {} : stateOf (p.markers[(std::size_t) i]);
}

// The spans of a pattern from 0 to `end` (at least one), each with what applies in it. Key markers at the same tick: the
// last in the list wins (as markerIndexAt).
struct Span
{
    std::int64_t from = 0, to = 0;
    State state;
};
inline std::vector<Span> spans (const Pattern& p, std::int64_t end)
{
    std::vector<std::pair<std::int64_t, State>> cuts;
    for (const auto& m : p.markers)
        if (isKeyMarker (m) && m.tick < end)
        {
            if (! cuts.empty() && cuts.back().first == m.tick)
                cuts.back().second = stateOf (m);
            else
                cuts.emplace_back (m.tick, stateOf (m));
        }
    std::stable_sort (cuts.begin(), cuts.end(), [] (const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<Span> out;
    std::int64_t at = 0;
    State now;
    for (const auto& [tick, state] : cuts)
    {
        if (tick > at)
            out.push_back ({ at, tick, now });
        at = std::max (at, tick);
        now = state;
    }
    out.push_back ({ at, std::max (at, end), now });
    return out;
}

// --- names (FL's: "C Major (Ionian)", the root with sharps; Default / Black notes as FL's menu says them) ---------------
inline std::string nameOf (const State& s)
{
    switch (s.kind)
    {
        case State::key: return projectkey::name (s.value);
        case State::none: return "Black notes";
        case State::editor: break;
    }
    return "Default";
}

// Gives a marker the key `s` (and FL's action Key/Scale). FL renames the marker after its key (190nu: C → A in the menu
// turned "C Minor Natural (Aeolian)" into "A Minor Natural (Aeolian)"); a name the user typed stays.
inline void setState (Marker& m, const State& s)
{
    const auto oldName = isKeyMarker (m) ? nameOf (stateOf (m)) : std::string();
    const auto renamed = m.name.empty() || m.name == oldName;
    m.action = Marker::keyScale;
    m.numerator = 0; // (not a time signature)
    m.keyRoot = s.kind == State::key ? s.value.root : 0;
    m.keyFlScale = s.kind == State::key ? s.value.flScale : s.kind == State::none ? noScale : defaultScale;
    if (renamed)
        m.name = nameOf (s);
}

inline Marker keyMarker (std::int64_t tick, const State& s)
{
    Marker m;
    m.tick = std::max<std::int64_t> (0, tick);
    setState (m, s);
    return m;
}

inline void sortByTick (Pattern& p)
{
    std::stable_sort (p.markers.begin(), p.markers.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });
}

// FL's Add key marker: a key marker at `tick` (one already there takes the new key). Returns its index.
inline int addKeyMarker (Pattern& p, std::int64_t tick, const State& s)
{
    tick = std::max<std::int64_t> (0, tick);
    for (int i = 0; i < (int) p.markers.size(); ++i)
        if (isKeyMarker (p.markers[(std::size_t) i]) && p.markers[(std::size_t) i].tick == tick)
        {
            setState (p.markers[(std::size_t) i], s);
            return i;
        }
    const auto m = keyMarker (tick, s);
    p.markers.push_back (m);
    sortByTick (p);
    for (int i = (int) p.markers.size() - 1; i >= 0; --i)
        if (p.markers[(std::size_t) i] == m)
            return i;
    return -1;
}

// LYNX extra: the key `s` over [from, to) only. The key markers inside go, a key marker at `from` sets `s`, and at `to`
// one brings back what applied there before (unless a key marker is already at `to`, or `s` already applied there).
inline void addKeyRegion (Pattern& p, std::int64_t from, std::int64_t to, const State& s)
{
    from = std::max<std::int64_t> (0, from);
    if (to <= from)
        return (void) addKeyMarker (p, from, s);
    const auto after = stateAt (p, to);
    const auto markerAtEnd = std::any_of (p.markers.begin(), p.markers.end(), [to] (const Marker& m) { return isKeyMarker (m) && m.tick == to; });
    p.markers.erase (std::remove_if (p.markers.begin(), p.markers.end(),
                                     [from, to] (const Marker& m) { return isKeyMarker (m) && m.tick > from && m.tick < to; }),
                     p.markers.end());
    addKeyMarker (p, from, s);
    if (! markerAtEnd && ! (after == s))
        addKeyMarker (p, to, after);
}

// --- the scale degrees in force ------------------------------------------------------------------------------------
// A scale as pitch classes: its root and the degrees above it (empty = none).
struct Degrees
{
    int root = 0;
    std::vector<int> steps;
    bool valid() const noexcept { return ! steps.empty(); }
    bool contains (int midiKey) const
    {
        const auto d = ((midiKey - root) % 12 + 12) % 12;
        return std::find (steps.begin(), steps.end(), d) != steps.end();
    }
};
inline Degrees degreesOf (const Key& k) { return { k.root, projectkey::intervals (k) }; }

// --- FL's .flp fields (see the top) ----------------------------------------------------------------------------------
inline int flOutOfScaleMask (const Key& k)
{
    int mask = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (! projectkey::contains (k, pc))
            mask |= 1 << (11 - pc);
    return mask;
}

// The key of an FL key marker: its scale index when the mask agrees (or there is no mask), else the first scale of
// FL's list with that root and mask; nullopt when neither (Default / Black notes / unknown).
inline std::optional<Key> keyFromFl (int root, int flScale, int mask)
{
    root = ((root % 12) + 12) % 12;
    if (projectkey::validFlScale (flScale) && (mask <= 0 || flOutOfScaleMask ({ root, flScale }) == mask))
        return Key { root, flScale };
    if (mask > 0)
        for (int s = 0; s < (int) notetools::flReferenceScales().size(); ++s)
            if (flOutOfScaleMask ({ root, s }) == mask)
                return Key { root, s };
    return std::nullopt;
}

// --- MIDI key signatures (FF 59 02 sf mi) ---------------------------------------------------------------------------
inline int flScaleNamed (const char* name)
{
    const auto& list = notetools::flReferenceScales();
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::string (list[(std::size_t) i].name) == name)
            return i;
    return -1;
}
inline int majorScale() { static const int i = flScaleNamed ("Major (Ionian)"); return i; }
inline int minorScale() { static const int i = flScaleNamed ("Minor Natural (Aeolian)"); return i; }

// The sharps (> 0) or flats (< 0) of the major key on `root` (F♯ major: 6 sharps; G♭ is not chosen).
inline int fifthsOfMajor (int root) noexcept
{
    const auto f = (((root % 12) + 12) % 12) * 7 % 12;
    return f > 6 ? f - 12 : f;
}

// A key as a MIDI key signature: (sharps/flats, minor). A scale with the notes of a major scale takes that major scale's
// signature (D Dorian: no sharps or flats), minor (mi 1) when it has a minor third and no major one; another scale takes
// its root's major or natural minor signature (C harmonic minor: 3 flats, minor). MIDI files hold only these 30 keys.
inline std::pair<int, int> midiSignature (const Key& k)
{
    const auto& steps = projectkey::intervals (k);
    const auto minor = projectkey::isMinor (k);
    if (steps.size() == 7)
        for (int parent = 0; parent < 12; ++parent)
        {
            bool same = true;
            for (const auto d : steps)
                same = same && projectkey::contains ({ parent, majorScale() }, k.root + d);
            if (same)
                return { fifthsOfMajor (parent), minor ? 1 : 0 };
        }
    return { fifthsOfMajor (minor ? k.root + 3 : k.root), minor ? 1 : 0 };
}

// A MIDI key signature as a key: the major key, or the natural minor (a relative minor's tonic).
inline Key keyFromMidiSignature (int sharps, int minor)
{
    sharps = std::clamp (sharps, -7, 7);
    const auto major = ((sharps * 7) % 12 + 12) % 12;
    return minor != 0 ? Key { (major + 9) % 12, minorScale() } : Key { major, majorScale() };
}

// The marker meta event LYNX writes next to each key signature, "Key: C Minor Harmonic", and its key when read back
// (nullopt for any other text).
inline std::string midiKeyText (const Key& k) { return "Key: " + projectkey::name (k); }
inline std::optional<Key> keyFromMidiText (const std::string& text)
{
    if (text.rfind ("Key: ", 0) != 0)
        return std::nullopt;
    const auto rest = text.substr (5);
    for (int root = 0; root < 12; ++root)
    {
        const auto r = projectkey::rootName (root) + " ";
        if (rest.rfind (r, 0) != 0)
            continue;
        const auto scale = flScaleNamed (rest.substr (r.size()).c_str());
        if (scale >= 0)
            return Key { root, scale };
    }
    return std::nullopt;
}

// The key changes a MIDI file gets for a pattern: where each span's key starts (`base` = the editor's own scale, the
// project key; a span with no key starts nothing, a key equal to the one before is not repeated).
inline std::vector<std::pair<std::int64_t, Key>> keyChanges (const Pattern& p, std::optional<Key> base, std::int64_t end)
{
    std::vector<std::pair<std::int64_t, Key>> out;
    for (const auto& span : spans (p, std::max<std::int64_t> (end, 1)))
    {
        const auto key = span.state.kind == State::key ? std::optional<Key> (span.state.value) : span.state.kind == State::editor ? base : std::nullopt;
        if (key && (out.empty() || ! (out.back().second == *key)))
            out.emplace_back (span.from, *key);
    }
    return out;
}

// The key markers of a song as File › Export › MIDI file writes its notes (exportopts::midiExportNotes): in Song mode with
// pattern clips, each clip's key markers where the clip plays them (and the key in force where a clip starts inside a
// region), in time order; otherwise the current pattern's.
inline std::vector<Marker> songKeyMarkers (const Song& song)
{
    std::vector<Marker> out;
    const auto patternClips = song.mode == PlayMode::song
                           && std::any_of (song.playlist.begin(), song.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::pattern; });
    if (! patternClips)
    {
        if (song.currentPattern >= 0 && song.currentPattern < (int) song.patterns.size())
            for (const auto& m : song.patterns[(std::size_t) song.currentPattern].markers)
                if (isKeyMarker (m))
                    out.push_back (m);
        return out;
    }
    for (const auto& c : song.playlist)
    {
        if (c.kind != ClipKind::pattern || c.muted || c.length <= 0 || c.pattern < 0 || c.pattern >= (int) song.patterns.size())
            continue;
        const auto& p = song.patterns[(std::size_t) c.pattern];
        if (! hasKeyMarkers (p))
            continue;
        if (const auto i = markerIndexAt (p, c.offset); i >= 0 && p.markers[(std::size_t) i].tick < c.offset)
        {
            auto m = p.markers[(std::size_t) i];
            m.tick = c.start;
            out.push_back (m);
        }
        for (auto m : p.markers)
            if (isKeyMarker (m) && m.tick >= c.offset && m.tick < c.offset + c.length)
            {
                m.tick = c.start + (m.tick - c.offset);
                out.push_back (m);
            }
    }
    std::stable_sort (out.begin(), out.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });
    return out;
}

// --- the Chord stamp's "Scale degree chord" (LYNX extra; esiowry's rule in #105: in C Ionian C gives I = C major, D gives
// ii = D minor, E gives iii = E minor...) ------------------------------------------------------------------------------
enum class DegreeSize
{
    triad,   // 1 3 5
    sixth,   // 1 3 5 6
    seventh, // 1 3 5 7
    ninth    // 1 3 5 7 9
};

// The clicked key's chord in `scale`: the key snapped to the nearest degree (a tie goes down, as the scale snap), then
// every other degree above it, as many as `size` asks; `inversion` 1… moves the lowest notes up an octave (an inversion
// past the chord's notes stays the last one). Keys relative to the snapped root, root position first; the root itself is
// returned in `rootKey`. Empty for an empty scale.
inline std::vector<int> diatonicChord (const Degrees& scale, int key, DegreeSize size, int inversion, int& rootKey)
{
    rootKey = key;
    if (! scale.valid())
        return {};
    const auto n = (int) scale.steps.size();
    // nearest in-scale key (down first on a tie)
    for (int d = 0; d < 12; ++d)
    {
        if (scale.contains (key - d))
        {
            rootKey = key - d;
            break;
        }
        if (scale.contains (key + d))
        {
            rootKey = key + d;
            break;
        }
    }
    const auto rel = ((rootKey - scale.root) % 12 + 12) % 12;
    int degree = 0;
    for (int i = 0; i < n; ++i)
        if (scale.steps[(std::size_t) i] == rel)
            degree = i;
    const auto above = [&] (int k) { // semitones from the chord's root up to the degree k steps higher
        const auto d = degree + k;
        return scale.steps[(std::size_t) (d % n)] + 12 * (d / n) - scale.steps[(std::size_t) degree];
    };
    std::vector<int> out { 0, above (2), above (4) };
    if (size == DegreeSize::sixth)
        out.push_back (above (5));
    if (size == DegreeSize::seventh || size == DegreeSize::ninth)
        out.push_back (above (6));
    if (size == DegreeSize::ninth)
        out.push_back (above (8));
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    inversion = std::clamp (inversion, 0, (int) out.size() - 1);
    for (int i = 0; i < inversion; ++i)
    {
        out.push_back (out.front() + 12);
        out.erase (out.begin());
    }
    return out;
}

// Whether a scale's chords are spelled with flats: its key signature has flats (C minor's A♭, not G♯); a scale that is
// no major scale's subset goes by its root's major / natural minor (as midiSignature).
inline bool prefersFlats (const Degrees& d)
{
    if (! d.valid())
        return false;
    const auto has = [&d] (int v) { return std::find (d.steps.begin(), d.steps.end(), v) != d.steps.end(); };
    const auto minor = has (3) && ! has (4);
    if (d.steps.size() == 7)
        for (int parent = 0; parent < 12; ++parent)
        {
            bool same = true;
            for (const auto s : d.steps)
                same = same && projectkey::contains ({ parent, majorScale() }, d.root + s);
            if (same)
                return fifthsOfMajor (parent) < 0;
        }
    return fifthsOfMajor (minor ? d.root + 3 : d.root) < 0;
}

// The roman numeral of the degree chord (I, ii, iii°…: its third and fifth decide the case and the sign; extensions add
// 6 / 7 / 9), for the hint and the ghost's label.
inline std::string degreeNumeral (const Degrees& scale, int rootKey, const std::vector<int>& chordKeys, DegreeSize size)
{
    static const std::array<const char*, 7> upper { "I", "II", "III", "IV", "V", "VI", "VII" };
    if (! scale.valid() || chordKeys.empty())
        return {};
    const auto rel = ((rootKey - scale.root) % 12 + 12) % 12;
    int degree = 0;
    for (int i = 0; i < (int) scale.steps.size(); ++i)
        if (scale.steps[(std::size_t) i] == rel)
            degree = i;
    std::vector<int> pcs;
    for (const auto k : chordKeys)
        pcs.push_back (((k - rootKey) % 12 + 12) % 12);
    const auto has = [&pcs] (int v) { return std::find (pcs.begin(), pcs.end(), v) != pcs.end(); };
    const auto minorThird = has (3) && ! has (4);
    const auto dim = minorThird && has (6) && ! has (7);
    const auto aug = has (4) && has (8) && ! has (7);
    std::string s = degree < 7 ? upper[(std::size_t) degree] : std::to_string (degree + 1);
    if (minorThird)
        for (auto& c : s)
            c = (char) (c - 'A' + 'a');
    if (dim)
        s += "\xc2\xb0";
    else if (aug)
        s += "+";
    s += size == DegreeSize::ninth ? "9" : size == DegreeSize::seventh ? "7" : size == DegreeSize::sixth ? "6" : "";
    return s;
}
} // namespace lynx::keyregions
