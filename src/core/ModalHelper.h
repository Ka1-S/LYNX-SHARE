#pragma once

// The Piano roll's Chord helper (round 190mc, GitHub discussion #105): modal interchange from the selected chord.
//
// Select a chord in the Piano roll; the helper reads the key the chord implies from the chord itself PLUS the chords
// before it (the context), with the selected chord's root as the tonic ("the current chord is treated as the new key
// signature", the discussion's own words), and lists
//  1. the related keys for modal interchange: the parallel modes of that tonic (the parallel minor or major first) and
//     the closely related keys, ranked from common to uncommon use, each with its key signature;
//  2. the borrowed chords of the chosen related key, ranked from common to uncommon use, each with its movement from
//     the current root as an interval up and down ("Up a Perfect 4th (or down a Perfect 5th)"), the change of quality,
//     the common tones and a short mood.
// A click inserts the chord after the selection, voice-led from it (or in a chosen inversion, with an extension).
//
// Deterministic music theory only (no model, no data of any product): the seven church modes and the harmonic minor as
// interval sets, the diatonic triads / sevenths / ninths built on them, LYNX's own tables of how common each borrowing is
// and what it tends to sound like (written for this round from standard harmony teaching: parallel minor borrowings in
// major — iv, bVII, bVI, bIII, i, v, ii° —, the harmonic minor's V and vii° in minor, the Dorian IV, the Phrygian bII,
// the Lydian II, secondary dominants from the closely related keys). Header only, no JUCE; costs nothing until called.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/NoteNames.h"
#include "core/PianoRollTools.h"
#include "core/Song.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text // marks the words for translation (ui/I18n.h defines the same)
#endif

namespace lynx::modalhelp
{
inline int pc (int v) noexcept { return ((v % 12) + 12) % 12; }

// --- scales ---------------------------------------------------------------------------------------------------------

enum class Mode : int
{
    ionian,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    aeolian,
    locrian,
    harmonicMinor
};
constexpr int churchModes = 7;
constexpr int modeCount = 8;

inline const std::array<int, 7>& steps (Mode m) noexcept
{
    static constexpr std::array<std::array<int, 7>, modeCount> table { {
        { 0, 2, 4, 5, 7, 9, 11 }, // Ionian (major)
        { 0, 2, 3, 5, 7, 9, 10 }, // Dorian
        { 0, 1, 3, 5, 7, 8, 10 }, // Phrygian
        { 0, 2, 4, 6, 7, 9, 11 }, // Lydian
        { 0, 2, 4, 5, 7, 9, 10 }, // Mixolydian
        { 0, 2, 3, 5, 7, 8, 10 }, // Aeolian (natural minor)
        { 0, 1, 3, 5, 6, 8, 10 }, // Locrian
        { 0, 2, 3, 5, 7, 8, 11 }, // harmonic minor
    } };
    return table[(std::size_t) std::clamp ((int) m, 0, modeCount - 1)];
}

// a mode whose tonic triad is major (Ionian, Lydian, Mixolydian): its borrowings read against the major tables
inline bool majorType (Mode m) noexcept { return m == Mode::ionian || m == Mode::lydian || m == Mode::mixolydian; }

inline const char* modeName (Mode m) noexcept
{
    switch (m)
    {
        case Mode::ionian: return TR_NOOP ("Ionian");
        case Mode::dorian: return TR_NOOP ("Dorian");
        case Mode::phrygian: return TR_NOOP ("Phrygian");
        case Mode::lydian: return TR_NOOP ("Lydian");
        case Mode::mixolydian: return TR_NOOP ("Mixolydian");
        case Mode::aeolian: return TR_NOOP ("Aeolian");
        case Mode::locrian: return TR_NOOP ("Locrian");
        case Mode::harmonicMinor: return TR_NOOP ("Harmonic minor");
    }
    return "";
}

// how far the mode's tonic sits above its relative major's (the church modes are rotations of the major scale)
inline int relativeMajorOffset (Mode m) noexcept
{
    switch (m)
    {
        case Mode::dorian: return 2;
        case Mode::phrygian: return 4;
        case Mode::lydian: return 5;
        case Mode::mixolydian: return 7;
        case Mode::aeolian:
        case Mode::harmonicMinor: return 9;
        case Mode::locrian: return 11;
        case Mode::ionian: break;
    }
    return 0;
}

struct Scale
{
    int tonic = 0; // pitch class
    Mode mode = Mode::ionian;

    bool contains (int key) const noexcept
    {
        const auto& s = steps (mode);
        return std::find (s.begin(), s.end(), pc (key - tonic)) != s.end();
    }
    int degreeOf (int key) const noexcept // 0…6, −1 when not in the scale
    {
        const auto& s = steps (mode);
        const auto it = std::find (s.begin(), s.end(), pc (key - tonic));
        return it == s.end() ? -1 : (int) (it - s.begin());
    }
    // the same seven pitch classes (C Ionian and A Aeolian)
    bool sameNotes (const Scale& o) const noexcept
    {
        for (const auto v : steps (mode))
            if (! o.contains (tonic + v))
                return false;
        return true;
    }
    bool operator== (const Scale&) const = default;
};

// --- spelling (letters follow the scale: C Aeolian is C D E♭ F G A♭ B♭, never D♯) ------------------------------------

struct Spelled
{
    int letter = 0;     // 0 C, 1 D, 2 E, 3 F, 4 G, 5 A, 6 B
    int accidental = 0; // −2…2 (♭♭ … ♯♯)
    bool operator== (const Spelled&) const = default;
};

inline int naturalPc (int letter) noexcept
{
    static constexpr int natural[7] { 0, 2, 4, 5, 7, 9, 11 };
    return natural[((letter % 7) + 7) % 7];
}

inline Spelled spellAs (int pitchClass, int letter) noexcept
{
    letter = ((letter % 7) + 7) % 7;
    auto d = pc (pitchClass) - naturalPc (letter);
    if (d > 6)
        d -= 12;
    if (d < -6)
        d += 12;
    return { letter, d };
}

// The name of a spelled note in the user's note names (Settings › General › Note names): "E♭", "Mi♭", Germanic "B" / "H"
inline std::string spelledName (const Spelled& s, notenames::Style style = notenames::style())
{
    static const char* english[7] { "C", "D", "E", "F", "G", "A", "B" };
    static const char* solfege[7] { "Do", "Re", "Mi", "Fa", "Sol", "La", "Si" };
    std::string base = style == notenames::Style::solfege ? solfege[s.letter] : english[s.letter];
    auto acc = s.accidental;
    if (style == notenames::Style::germanic && s.letter == 6)
    {
        if (acc == -1)
            return "B"; // (German B = B♭)
        base = "H";
    }
    std::string out = base;
    for (; acc > 0; --acc)
        out += "\xe2\x99\xaf"; // ♯
    for (; acc < 0; ++acc)
        out += "\xe2\x99\xad"; // ♭
    return out;
}

// The letter of a scale's tonic: the spelling with the fewest accidentals in the key signature (ties: sharps for the
// major-type modes, flats for the others — F♯ major, E♭ minor)
inline int tonicLetter (const Scale& s) noexcept
{
    int best = 0, bestCost = 1 << 20;
    for (int letter = 0; letter < 7; ++letter)
    {
        const auto t = spellAs (s.tonic, letter);
        if (std::abs (t.accidental) > 1)
            continue;
        int cost = 0, sign = 0;
        for (int d = 0; d < 7; ++d)
        {
            const auto n = spellAs (s.tonic + steps (s.mode)[(std::size_t) d], letter + d);
            cost += 10 * std::abs (n.accidental) * std::abs (n.accidental);
            sign += n.accidental;
        }
        if (sign < 0 && majorType (s.mode))
            cost += 1;
        if (sign > 0 && ! majorType (s.mode))
            cost += 1;
        if (cost < bestCost)
        {
            bestCost = cost;
            best = letter;
        }
    }
    return best;
}

inline Spelled spellDegree (const Scale& s, int degree) noexcept
{
    const auto letter = tonicLetter (s);
    return spellAs (s.tonic + steps (s.mode)[(std::size_t) (((degree % 7) + 7) % 7)], letter + degree);
}

// The key signature of a scale: the sharps or flats of its relative major (church modes); the harmonic minor's is the
// natural minor's, its raised seventh written as an accidental of its own (`raised`).
struct Signature
{
    int sharps = 0, flats = 0;
    std::vector<Spelled> notes;   // in signature order (F♯ C♯ G♯ … / B♭ E♭ A♭ …)
    std::optional<Spelled> raised; // harmonic minor: the raised seventh (B♮ in C harmonic minor)
};

inline Signature signatureOf (const Scale& s)
{
    Signature out;
    const auto base = s.mode == Mode::harmonicMinor ? Scale { s.tonic, Mode::aeolian } : s;
    std::array<Spelled, 7> degree {};
    for (int d = 0; d < 7; ++d)
        degree[(std::size_t) d] = spellDegree (base, d);
    static constexpr int sharpOrder[7] { 3, 0, 4, 1, 5, 2, 6 }; // F C G D A E B
    static constexpr int flatOrder[7] { 6, 2, 5, 1, 4, 0, 3 };  // B E A D G C F
    for (const auto letter : sharpOrder)
        for (const auto& n : degree)
            if (n.letter == letter && n.accidental > 0)
                out.notes.push_back (n), ++out.sharps;
    for (const auto letter : flatOrder)
        for (const auto& n : degree)
            if (n.letter == letter && n.accidental < 0)
                out.notes.push_back (n), ++out.flats;
    if (s.mode == Mode::harmonicMinor)
        out.raised = spellDegree (s, 6);
    return out;
}

// --- chords ---------------------------------------------------------------------------------------------------------

enum class Quality
{
    major,
    minor,
    diminished,
    augmented,
    sus2,
    sus4,
    other
};

inline const char* qualityName (Quality q) noexcept
{
    switch (q)
    {
        case Quality::major: return TR_NOOP ("major");
        case Quality::minor: return TR_NOOP ("minor");
        case Quality::diminished: return TR_NOOP ("diminished");
        case Quality::augmented: return TR_NOOP ("augmented");
        case Quality::sus2: return "sus2";
        case Quality::sus4: return "sus4";
        case Quality::other: break;
    }
    return TR_NOOP ("other");
}

inline bool hasInterval (const std::vector<int>& intervals, int i) noexcept
{
    return std::find_if (intervals.begin(), intervals.end(), [i] (int v) { return pc (v) == i; }) != intervals.end();
}

// the triad inside a chord (intervals above its root)
inline Quality qualityOf (const std::vector<int>& intervals) noexcept
{
    const auto m3 = hasInterval (intervals, 3), M3 = hasInterval (intervals, 4), p5 = hasInterval (intervals, 7),
               d5 = hasInterval (intervals, 6), a5 = hasInterval (intervals, 8);
    if (M3 && a5 && ! p5)
        return Quality::augmented;
    if (m3 && d5 && ! p5)
        return Quality::diminished;
    if (M3)
        return Quality::major;
    if (m3)
        return Quality::minor;
    if (hasInterval (intervals, 2))
        return Quality::sus2;
    if (hasInterval (intervals, 5))
        return Quality::sus4;
    return p5 ? Quality::major : Quality::other; // (a power chord reads as major)
}

inline std::vector<int> pitchClasses (const prtools::Chord& c)
{
    std::vector<int> out;
    for (const auto i : c.intervals)
        if (std::find (out.begin(), out.end(), pc (c.root + i)) == out.end())
            out.push_back (pc (c.root + i));
    return out;
}

// The chord a set of keys spells: the detector's root (exact templates), else the lowest key as the root
inline prtools::Chord chordOfKeys (const std::vector<int>& keys)
{
    prtools::Chord c;
    if (keys.empty())
        return c;
    const auto low = *std::min_element (keys.begin(), keys.end());
    const auto detected = prtools::detectChord (keys);
    c.root = detected ? detected->root : pc (low);
    for (const auto k : keys)
    {
        const auto i = pc (k - c.root);
        if (std::find (c.intervals.begin(), c.intervals.end(), i) == c.intervals.end())
            c.intervals.push_back (i);
    }
    std::sort (c.intervals.begin(), c.intervals.end());
    if (pc (low) != c.root)
        c.bass = pc (low);
    return c;
}

// The chord symbol after the root: "" (major), "m", "°", "+", "7", "maj7", "m7", "m7♭5", "°7", "sus2", "sus4", "add9", "9"…
inline std::string chordSuffix (const std::vector<int>& intervals)
{
    const auto q = qualityOf (intervals);
    const auto m7 = hasInterval (intervals, 10), M7 = hasInterval (intervals, 11), d7 = q == Quality::diminished && hasInterval (intervals, 9);
    const auto ninth = std::find (intervals.begin(), intervals.end(), 14) != intervals.end() || std::find (intervals.begin(), intervals.end(), 13) != intervals.end()
                       || std::find (intervals.begin(), intervals.end(), 15) != intervals.end();
    const auto flatNinth = std::find (intervals.begin(), intervals.end(), 13) != intervals.end();
    std::string s;
    switch (q)
    {
        case Quality::minor: s = "m"; break;
        case Quality::diminished: s = m7 ? "m7\xe2\x99\xad" "5" : d7 ? "\xc2\xb0" "7" : "\xc2\xb0"; break;
        case Quality::augmented: s = "+"; break;
        default: break;
    }
    if (q != Quality::diminished)
    {
        if (M7)
            s += ninth ? "maj9" : "maj7";
        else if (m7)
            s += ninth ? (flatNinth ? "7\xe2\x99\xad" "9" : "9") : "7";
        else if (ninth)
            s += "add9";
    }
    if (q == Quality::sus2)
        s += "sus2";
    if (q == Quality::sus4)
        s += "sus4";
    return s;
}

// Roman numeral of a chord root in a key: the degree's case says the quality ("iv", "♭VII", "ii°", "♯iv°", "III+")
inline std::string numeral (const Scale& home, int root, Quality q)
{
    static const char* upper[7] { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const char* lower[7] { "i", "ii", "iii", "iv", "v", "vi", "vii" };
    // the degree of the home MAJOR scale the interval is nearest to (♭ / ♯ say how it differs)
    static constexpr int degreeOf[12] { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
    static constexpr int major[7] { 0, 2, 4, 5, 7, 9, 11 };
    const auto i = pc (root - home.tonic);
    auto d = degreeOf[i];
    if (i == 6 && q == Quality::diminished) // (♯iv° rather than ♭v°: the leading tone to V)
        d = 3;
    const auto diff = i - major[d]; // (against the major scale: borrowed chords read ♭VI, ♭VII in a minor home too)
    std::string acc;
    if (diff < 0)
        acc = "\xe2\x99\xad";
    else if (diff > 0)
        acc = "\xe2\x99\xaf";
    const auto lowerCase = q == Quality::minor || q == Quality::diminished;
    std::string out = acc + (lowerCase ? lower[d] : upper[d]);
    if (q == Quality::diminished)
        out += "\xc2\xb0";
    else if (q == Quality::augmented)
        out += "+";
    return out;
}

// --- intervals ------------------------------------------------------------------------------------------------------

// the name of a span of 0…11 semitones
inline const char* intervalName (int semitones) noexcept
{
    switch (pc (semitones))
    {
        case 1: return TR_NOOP ("Minor 2nd");
        case 2: return TR_NOOP ("Major 2nd");
        case 3: return TR_NOOP ("Minor 3rd");
        case 4: return TR_NOOP ("Major 3rd");
        case 5: return TR_NOOP ("Perfect 4th");
        case 6: return TR_NOOP ("Tritone");
        case 7: return TR_NOOP ("Perfect 5th");
        case 8: return TR_NOOP ("Minor 6th");
        case 9: return TR_NOOP ("Major 6th");
        case 10: return TR_NOOP ("Minor 7th");
        case 11: return TR_NOOP ("Major 7th");
        default: break;
    }
    return TR_NOOP ("Unison");
}

// The root movement from one root to another, as the shorter way first: "Up a Perfect 4th (or down a Perfect 5th)".
struct Movement
{
    int up = 0, down = 0; // semitones up / down (0 = the same root)
    bool upFirst = true;  // up is the shorter way (a tritone: up first)
};

inline Movement movement (int fromRoot, int toRoot) noexcept
{
    Movement m;
    m.up = pc (toRoot - fromRoot);
    m.down = m.up == 0 ? 0 : 12 - m.up;
    m.upFirst = m.up <= 6;
    return m;
}

// --- the key the chords imply ---------------------------------------------------------------------------------------

struct Analysis
{
    bool valid = false;
    prtools::Chord chord;          // the selected chord (root, intervals above it, slash bass)
    std::vector<int> keys;         // its keys, low to high
    Quality quality = Quality::other;
    Scale key;                     // the implied key: the chord's root as the tonic, the mode the context implies
    std::array<double, churchModes> modeScore {}; // how well each church mode on that tonic fits (higher = better)
    int contextChords = 0;         // how many chords before the selection were read
    bool forcedTonic = false, forcedMode = false;
};

// How common each church mode is as the home of a progression (a small prior that only breaks ties): major and natural
// minor first, then Mixolydian and Dorian, then Lydian and Phrygian, Locrian last.
inline double modePrior (Mode m) noexcept
{
    switch (m)
    {
        case Mode::ionian: return 1.0;
        case Mode::aeolian: return 0.95;
        case Mode::mixolydian: return 0.8;
        case Mode::dorian: return 0.78;
        case Mode::lydian: return 0.6;
        case Mode::phrygian: return 0.58;
        case Mode::locrian: return 0.3;
        case Mode::harmonicMinor: break;
    }
    return 0.0;
}

// The selected chord (its keys) read with the chords before it (oldest first, each a set of keys). The tonic is the
// chord's root unless `tonic` says otherwise; the mode is the church mode that holds the chord and the most of the
// context (recent chords weigh more: ×0.8 per chord back), the project's key breaking near-ties, else the prior.
inline Analysis analyse (const std::vector<int>& selectedKeys, const std::vector<std::vector<int>>& context = {},
                         std::optional<Scale> projectKey = std::nullopt, std::optional<int> tonic = std::nullopt,
                         std::optional<Mode> mode = std::nullopt)
{
    Analysis a;
    auto keys = selectedKeys;
    keys.erase (std::remove_if (keys.begin(), keys.end(), [] (int k) { return k < 0 || k > 127; }), keys.end());
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    a.keys = keys;
    if (keys.empty())
        return a;
    a.chord = chordOfKeys (keys);
    a.quality = qualityOf (a.chord.intervals);
    a.key.tonic = tonic ? pc (*tonic) : a.chord.root;
    a.forcedTonic = tonic.has_value();
    const auto pcs = pitchClasses (a.chord);
    std::vector<std::pair<std::vector<int>, double>> weighted;
    double weight = 1.0;
    for (auto it = context.rbegin(); it != context.rend() && weighted.size() < 8; ++it)
    {
        std::vector<int> set;
        for (const auto k : *it)
            if (std::find (set.begin(), set.end(), pc (k)) == set.end())
                set.push_back (pc (k));
        if (set.empty())
            continue;
        weighted.emplace_back (std::move (set), weight);
        weight *= 0.8;
    }
    a.contextChords = (int) weighted.size();
    auto best = Mode::ionian;
    double bestScore = -1.0e9;
    for (int m = 0; m < churchModes; ++m)
    {
        const Scale s { a.key.tonic, (Mode) m };
        double fit = 0.0;
        for (const auto p : pcs)
            fit += s.contains (p) ? 1.0 : 0.0;
        fit /= (double) std::max<std::size_t> (1, pcs.size());
        double context = 0.0, total = 0.0;
        for (const auto& [set, w] : weighted)
        {
            double in = 0.0;
            for (const auto p : set)
                in += s.contains (p) ? 1.0 : 0.0;
            context += w * in / (double) set.size();
            total += w;
        }
        const auto contextFit = total > 0.0 ? context / total : 0.0;
        const auto project = projectKey && s.sameNotes (*projectKey) ? 1.0 : 0.0;
        const auto score = 4.0 * fit + 2.0 * contextFit + 0.4 * project + 0.3 * modePrior ((Mode) m);
        a.modeScore[(std::size_t) m] = score;
        if (score > bestScore + 1.0e-9)
        {
            bestScore = score;
            best = (Mode) m;
        }
    }
    a.key.mode = mode ? *mode : best;
    a.forcedMode = mode.has_value();
    a.valid = true;
    return a;
}

// The chords before `from` among `notes` of `channel` (oldest first): two or more pitch classes starting within a 32nd
// of each other form one chord, with the notes still sounding there; a single note is left out (a melody is not a chord);
// at most `count`, the latest.
inline std::vector<std::vector<int>> contextChords (const std::vector<Note>& notes, int channel, std::int64_t from, int count = 4,
                                                    const std::vector<int>& exclude = {})
{
    std::vector<const Note*> before;
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (n.channel != channel || n.start >= from || n.muted
            || std::find (exclude.begin(), exclude.end(), (int) i) != exclude.end())
            continue;
        before.push_back (&n);
    }
    std::sort (before.begin(), before.end(), [] (const Note* x, const Note* y) { return x->start < y->start || (x->start == y->start && x->key < y->key); });
    const auto window = (std::int64_t) ppq / 8;
    std::vector<std::vector<int>> chords;
    for (std::size_t i = 0; i < before.size();)
    {
        const auto at = before[i]->start;
        std::vector<int> keys;
        std::size_t j = i;
        for (; j < before.size() && before[j]->start - at <= window; ++j)
            keys.push_back (before[j]->key);
        std::vector<int> struck; // (a chord starts where two or more pitch classes start together)
        for (const auto k : keys)
            if (std::find (struck.begin(), struck.end(), pc (k)) == struck.end())
                struck.push_back (pc (k));
        if (struck.size() < 2)
        {
            i = j;
            continue;
        }
        for (const auto* n : before) // (a held bass or pad under the new notes belongs to the chord)
            if (n->start < at && n->start + n->length > at + window)
                keys.push_back (n->key);
        std::sort (keys.begin(), keys.end());
        keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
        std::vector<int> classes;
        for (const auto k : keys)
            if (std::find (classes.begin(), classes.end(), pc (k)) == classes.end())
                classes.push_back (pc (k));
        if (classes.size() >= 2)
            chords.push_back (std::move (keys));
        i = j;
    }
    if ((int) chords.size() > count)
        chords.erase (chords.begin(), chords.end() - count);
    return chords;
}

// --- panel 1: the related keys --------------------------------------------------------------------------------------

enum class Relation
{
    parallel,      // the same tonic, another mode (the parallel minor / major and the other modes)
    relative,      // the same key signature, another tonic (as a harmonic minor: its V is the new colour)
    dominant,      // the key a fifth above
    subdominant,   // the key a fifth below
    dominantRelative,
    subdominantRelative
};

inline const char* relationName (Relation r, Mode home, Mode mode) noexcept
{
    switch (r)
    {
        case Relation::parallel:
            if (majorType (home) && mode == Mode::aeolian)
                return TR_NOOP ("Parallel minor");
            if (! majorType (home) && mode == Mode::ionian)
                return TR_NOOP ("Parallel major");
            return TR_NOOP ("Parallel mode");
        case Relation::relative: return majorType (home) ? TR_NOOP ("Relative minor") : TR_NOOP ("Relative major");
        case Relation::dominant: return TR_NOOP ("Dominant key");
        case Relation::subdominant: return TR_NOOP ("Subdominant key");
        case Relation::dominantRelative:
        case Relation::subdominantRelative: return TR_NOOP ("Closely related key");
    }
    return "";
}

struct RelatedKey
{
    Scale scale;
    Relation relation = Relation::parallel;
    int rank = 0; // 1 = the most common source of borrowed chords
};

// The parallel modes to borrow from, the most common first, for each home mode (LYNX's table from standard teaching:
// major borrows from the parallel minor first, then Mixolydian's ♭VII and Dorian; minor borrows the major's I / IV, then
// the harmonic minor's V, Dorian's IV, Phrygian's ♭II; the bright and dark extremes last).
inline std::vector<Mode> parallelOrder (Mode home)
{
    using M = Mode;
    switch (home)
    {
        case M::ionian: return { M::aeolian, M::mixolydian, M::dorian, M::phrygian, M::lydian, M::harmonicMinor, M::locrian };
        case M::lydian: return { M::ionian, M::mixolydian, M::aeolian, M::dorian, M::phrygian, M::harmonicMinor, M::locrian };
        case M::mixolydian: return { M::ionian, M::dorian, M::aeolian, M::lydian, M::phrygian, M::harmonicMinor, M::locrian };
        case M::aeolian: return { M::ionian, M::harmonicMinor, M::dorian, M::phrygian, M::mixolydian, M::lydian, M::locrian };
        case M::dorian: return { M::aeolian, M::ionian, M::mixolydian, M::harmonicMinor, M::phrygian, M::lydian, M::locrian };
        case M::phrygian: return { M::aeolian, M::locrian, M::dorian, M::harmonicMinor, M::ionian, M::mixolydian, M::lydian };
        case M::locrian: return { M::phrygian, M::aeolian, M::dorian, M::harmonicMinor, M::ionian, M::mixolydian, M::lydian };
        case M::harmonicMinor: return { M::ionian, M::aeolian, M::dorian, M::phrygian, M::mixolydian, M::lydian, M::locrian };
    }
    return {};
}

struct Borrowed;
inline std::vector<Borrowed> borrowedChords (const Analysis& a, const RelatedKey& source);
// (rollup 0.1.30 step 11: relatedKeys only asks whether a key has any; defined after Borrowed is complete — libc++ (the
// Windows zig build) instantiates std::vector<Borrowed> where it is used, so the call cannot stand before the struct)
inline bool hasBorrowedChords (const Analysis& a, const RelatedKey& source);

// Panel 1: the parallel modes (the three most common first), then the closely related keys (dominant, subdominant,
// relative and their relatives: the sources of the secondary dominants), then the rarer modes; a key with nothing to
// borrow (every chord already in the home key) is left out.
inline std::vector<RelatedKey> relatedKeys (const Analysis& a)
{
    std::vector<RelatedKey> out;
    if (! a.valid)
        return out;
    const auto home = a.key;
    const auto order = parallelOrder (home.mode);
    const auto add = [&out, &a] (Scale s, Relation r) {
        for (const auto& k : out)
            if (k.scale == s)
                return;
        RelatedKey k { s, r, 0 };
        if (! hasBorrowedChords (a, k))
            return;
        k.rank = (int) out.size() + 1;
        out.push_back (k);
    };
    for (std::size_t i = 0; i < order.size() && i < 3; ++i)
        add ({ home.tonic, order[i] }, Relation::parallel);
    // the closely related keys (one accidental away at most): the six keys of the home's signature and of its two
    // neighbours on the circle of fifths (relative major R, R's dominant and subdominant, and their relative minors).
    // Named from the home tonic: the same notes = relative, a fifth up = dominant key, a fifth down = subdominant key.
    // Minor keys lend as harmonic minors (their V and vii° are what they add).
    const auto major = pc (home.tonic - relativeMajorOffset (home.mode));
    const auto homeIsMinor = ! majorType (home.mode);
    std::vector<Scale> close { { major, Mode::ionian }, { pc (major + 9), Mode::harmonicMinor }, { pc (major + 7), Mode::ionian },
                               { pc (major + 4), Mode::harmonicMinor }, { pc (major + 5), Mode::ionian }, { pc (major + 2), Mode::harmonicMinor } };
    const auto relationOf = [&home, homeIsMinor] (const Scale& s) {
        if (s.tonic == pc (home.tonic + 7) && majorType (s.mode) != homeIsMinor)
            return Relation::dominant;
        if (s.tonic == pc (home.tonic + 5) && majorType (s.mode) != homeIsMinor)
            return Relation::subdominant;
        if (Scale { s.tonic, s.mode == Mode::harmonicMinor ? Mode::aeolian : s.mode }.sameNotes (home) && majorType (s.mode) == homeIsMinor)
            return Relation::relative;
        return majorType (s.mode) ? Relation::dominantRelative : Relation::subdominantRelative;
    };
    // order: relative, dominant, subdominant, then the other three
    std::stable_sort (close.begin(), close.end(), [&relationOf] (const Scale& x, const Scale& y) { return (int) relationOf (x) < (int) relationOf (y); });
    for (const auto& s : close)
        if (s.tonic != home.tonic)
            add (s, relationOf (s));
    for (std::size_t i = 3; i < order.size(); ++i)
        add ({ home.tonic, order[i] }, Relation::parallel);
    return out;
}

// --- panel 2: the borrowed chords -----------------------------------------------------------------------------------

enum class Extension
{
    triad,
    seventh,
    ninth,
    sus2,
    sus4,
    add9
};

inline const char* extensionName (Extension e) noexcept
{
    switch (e)
    {
        case Extension::triad: return TR_NOOP ("Triad");
        case Extension::seventh: return TR_NOOP ("7th");
        case Extension::ninth: return TR_NOOP ("9th");
        case Extension::sus2: return "sus2";
        case Extension::sus4: return "sus4";
        case Extension::add9: return "add9";
    }
    return "";
}

struct Borrowed
{
    prtools::Chord chord;        // root + intervals (the triad, or with the extension)
    Spelled root;                // spelled in the source scale
    int degree = 0;              // in the source scale
    Quality quality = Quality::major;
    std::string numeral;         // against the home key ("iv", "♭VII")
    Movement move;               // from the selected chord's root
    int commonTones = 0;         // pitch classes shared with the selected chord
    int foreignTones = 0;        // pitch classes outside the home key
    double score = 0.0;          // lower = more common
    int rank = 0;
    std::vector<const char*> mood; // TR_NOOP words, two or three
    bool secondaryDominant = false; // the V of a related key (V/V, V/vi…)
    std::string target;          // and the chord it leads to ("V/ii": "Dm")
};

namespace detail
{
// The diatonic chord on `degree` of a scale: the triad, with its 7th or 9th (the scale's own notes), or sus2 / sus4 /
// add9 on the triad's root (the scale's 2nd / 4th).
inline std::vector<int> stackOn (const Scale& s, int degree, Extension e)
{
    const auto& st = steps (s.mode);
    const auto note = [&st, degree] (int k) {
        const auto d = degree + k;
        return st[(std::size_t) (d % 7)] + 12 * (d / 7) - st[(std::size_t) degree];
    };
    std::vector<int> iv { 0, note (2), note (4) };
    switch (e)
    {
        case Extension::seventh: iv.push_back (note (6)); break;
        case Extension::ninth: iv.push_back (note (6)); iv.push_back (note (8)); break;
        case Extension::sus2: iv[1] = note (1); break;
        case Extension::sus4: iv[1] = note (3); break;
        case Extension::add9: iv.push_back (note (8)); break;
        case Extension::triad: break;
    }
    return iv;
}

struct Entry
{
    int interval;      // root above the home tonic
    Quality quality;
    double rank;       // lower = more common
    std::array<const char*, 3> mood;
};

// LYNX's tables: how common a borrowed chord is and what it tends to sound like, by its root above the home tonic and its
// quality, for a major-type and a minor-type home (round 190mc; from standard harmony teaching, own words).
inline const std::vector<Entry>& table (bool majorHome)
{
    using Q = Quality;
    static const std::vector<Entry> major {
        { 5, Q::minor, 1, { TR_NOOP ("bittersweet"), TR_NOOP ("nostalgic"), TR_NOOP ("soft melancholy") } },        // iv
        { 10, Q::major, 2, { TR_NOOP ("open"), TR_NOOP ("rock-driven"), TR_NOOP ("anthemic") } },                   // ♭VII
        { 8, Q::major, 3, { TR_NOOP ("warm"), TR_NOOP ("cinematic"), TR_NOOP ("wistful") } },                       // ♭VI
        { 3, Q::major, 4, { TR_NOOP ("noble"), TR_NOOP ("heroic"), TR_NOOP ("bright but darkened") } },              // ♭III
        { 0, Q::minor, 5, { TR_NOOP ("sudden shadow"), TR_NOOP ("dramatic"), TR_NOOP ("stark") } },                  // i
        { 7, Q::minor, 6, { TR_NOOP ("melancholic"), TR_NOOP ("yearning"), nullptr } },                              // v
        { 7, Q::major, 6.5, { TR_NOOP ("strong pull home"), TR_NOOP ("resolving"), nullptr } },                      // V (in Mixolydian)
        { 5, Q::major, 6.6, { TR_NOOP ("warm"), TR_NOOP ("grounded"), nullptr } },                                   // IV (in Lydian)
        { 2, Q::diminished, 7, { TR_NOOP ("tense"), TR_NOOP ("uneasy"), nullptr } },                                 // ii°
        { 1, Q::major, 8, { TR_NOOP ("dark"), TR_NOOP ("exotic"), nullptr } },                                       // ♭II
        { 2, Q::major, 9, { TR_NOOP ("bright"), TR_NOOP ("lifting"), TR_NOOP ("dreamy") } },                          // II
        { 11, Q::diminished, 9.5, { TR_NOOP ("tense"), TR_NOOP ("leading"), nullptr } },                             // vii°
        { 4, Q::minor, 9.6, { TR_NOOP ("gentle"), TR_NOOP ("tender"), nullptr } },                                   // iii
        { 3, Q::augmented, 9.8, { TR_NOOP ("suspenseful"), TR_NOOP ("mysterious"), nullptr } },                      // ♭III+
        { 10, Q::minor, 10, { TR_NOOP ("brooding"), TR_NOOP ("dark"), nullptr } },                                   // ♭vii
        { 11, Q::minor, 11, { TR_NOOP ("airy"), TR_NOOP ("floating"), nullptr } },                                   // vii
        { 6, Q::diminished, 12, { TR_NOOP ("restless"), TR_NOOP ("suspended"), nullptr } },                          // ♯iv°
        { 3, Q::minor, 13, { TR_NOOP ("gloomy"), TR_NOOP ("mysterious"), nullptr } },                                // ♭iii
        { 8, Q::minor, 14, { TR_NOOP ("dark"), TR_NOOP ("mournful"), nullptr } },                                    // ♭vi
        { 6, Q::major, 15, { TR_NOOP ("strange"), TR_NOOP ("otherworldly"), nullptr } },                             // ♭V
        { 4, Q::diminished, 15.5, { TR_NOOP ("unstable"), nullptr, nullptr } },                                      // iii°
        { 9, Q::diminished, 16, { TR_NOOP ("tense"), TR_NOOP ("restless"), nullptr } },                              // vi°
        { 7, Q::diminished, 17, { TR_NOOP ("unstable"), TR_NOOP ("eerie"), nullptr } },                              // v°
        { 0, Q::diminished, 18, { TR_NOOP ("unstable"), TR_NOOP ("eerie"), nullptr } },                              // i°
    };
    static const std::vector<Entry> minor {
        { 7, Q::major, 1, { TR_NOOP ("strong pull home"), TR_NOOP ("dramatic"), nullptr } },                        // V
        { 5, Q::major, 2, { TR_NOOP ("hopeful"), TR_NOOP ("soulful"), nullptr } },                                   // IV
        { 0, Q::major, 3, { TR_NOOP ("bright surprise"), TR_NOOP ("radiant"), nullptr } },                           // I (Picardy)
        { 1, Q::major, 4, { TR_NOOP ("dark"), TR_NOOP ("Spanish"), TR_NOOP ("exotic") } },                            // ♭II
        { 11, Q::diminished, 5, { TR_NOOP ("tense"), TR_NOOP ("urgent"), nullptr } },                                // vii°
        { 2, Q::minor, 6, { TR_NOOP ("smooth"), TR_NOOP ("jazzy"), nullptr } },                                      // ii
        { 8, Q::major, 6.5, { TR_NOOP ("warm"), TR_NOOP ("cinematic"), TR_NOOP ("wistful") } },                       // ♭VI (Dorian home)
        { 5, Q::minor, 6.6, { TR_NOOP ("bittersweet"), TR_NOOP ("nostalgic"), nullptr } },                           // iv (Dorian home)
        { 9, Q::minor, 7, { TR_NOOP ("hopeful"), TR_NOOP ("gentle"), nullptr } },                                    // vi
        { 4, Q::minor, 8, { TR_NOOP ("bittersweet"), TR_NOOP ("tender"), nullptr } },                                // iii
        { 2, Q::major, 9, { TR_NOOP ("bright"), TR_NOOP ("lifting"), nullptr } },                                    // II
        { 10, Q::major, 9.2, { TR_NOOP ("open"), TR_NOOP ("anthemic"), nullptr } },                                  // ♭VII
        { 3, Q::major, 9.3, { TR_NOOP ("noble"), TR_NOOP ("heroic"), nullptr } },                                    // ♭III
        { 7, Q::minor, 9.4, { TR_NOOP ("melancholic"), TR_NOOP ("yearning"), nullptr } },                            // v
        { 2, Q::diminished, 9.5, { TR_NOOP ("tense"), TR_NOOP ("uneasy"), nullptr } },                               // ii°
        { 0, Q::minor, 9.6, { TR_NOOP ("sudden shadow"), TR_NOOP ("dramatic"), nullptr } },                          // i (Locrian home)
        { 9, Q::diminished, 10, { TR_NOOP ("restless"), nullptr, nullptr } },                                         // vi°
        { 10, Q::minor, 11, { TR_NOOP ("brooding"), nullptr, nullptr } },                                             // ♭vii
        { 3, Q::augmented, 12, { TR_NOOP ("suspenseful"), TR_NOOP ("mysterious"), nullptr } },                       // III+
        { 6, Q::major, 13, { TR_NOOP ("strange"), TR_NOOP ("otherworldly"), nullptr } },                             // ♭V
        { 6, Q::diminished, 13.5, { TR_NOOP ("restless"), TR_NOOP ("suspended"), nullptr } },                        // ♯iv°
        { 4, Q::major, 14, { TR_NOOP ("surprising"), TR_NOOP ("bright"), nullptr } },                                // III
        { 7, Q::diminished, 15, { TR_NOOP ("unstable"), TR_NOOP ("eerie"), nullptr } },                              // v°
        { 4, Q::diminished, 16, { TR_NOOP ("unstable"), nullptr, nullptr } },                                         // iii°
    };
    return majorHome ? major : minor;
}

inline const Entry* lookup (bool majorHome, int interval, Quality q) noexcept
{
    for (const auto& e : table (majorHome))
        if (e.interval == interval && e.quality == q)
            return &e;
    return nullptr;
}

inline std::vector<const char*> fallbackMood (Quality q)
{
    switch (q)
    {
        case Quality::major: return { TR_NOOP ("brightening") };
        case Quality::minor: return { TR_NOOP ("darkening") };
        case Quality::diminished: return { TR_NOOP ("tense") };
        case Quality::augmented: return { TR_NOOP ("suspenseful") };
        default: break;
    }
    return { TR_NOOP ("open") };
}
} // namespace detail

// Panel 2: the chords of `source` that are not in the home key (the new colours it lends), the most common first. From a
// parallel mode they are ranked and described by LYNX's table; from a related key its V (the secondary dominant) and
// vii° come first, then the chords with the fewest notes outside the home key. The selected chord itself is left out.
// `ext` builds each chord as a triad or with its 7th / 9th / sus / add9 (the source scale's own notes); the ranking and
// the words read the triad.
inline std::vector<Borrowed> borrowedChords (const Analysis& a, const RelatedKey& source, Extension ext)
{
    std::vector<Borrowed> out;
    if (! a.valid)
        return out;
    const auto& home = a.key;
    const auto majorHome = majorType (home.mode);
    const auto selectedPcs = pitchClasses (a.chord);
    const auto selectedQuality = a.quality;
    for (int d = 0; d < 7; ++d)
    {
        const auto triad = detail::stackOn (source.scale, d, Extension::triad);
        const auto root = pc (source.scale.tonic + steps (source.scale.mode)[(std::size_t) d]);
        int foreign = 0;
        for (const auto i : triad)
            foreign += home.contains (root + i) ? 0 : 1;
        if (foreign == 0)
            continue;
        const auto q = qualityOf (triad);
        if (root == a.chord.root && q == selectedQuality)
            continue;
        Borrowed b;
        b.degree = d;
        b.quality = q;
        b.chord.root = root;
        b.chord.intervals = detail::stackOn (source.scale, d, ext);
        b.root = spellDegree (source.scale, d);
        b.numeral = numeral (home, root, q);
        b.move = movement (a.chord.root, root);
        b.foreignTones = foreign;
        for (const auto p : pitchClasses (b.chord))
            b.commonTones += std::find (selectedPcs.begin(), selectedPcs.end(), p) != selectedPcs.end() ? 1 : 0;
        const auto interval = pc (root - home.tonic);
        if (source.relation == Relation::parallel)
        {
            if (const auto* e = detail::lookup (majorHome, interval, q))
            {
                b.score = e->rank;
                for (const auto* w : e->mood)
                    if (w != nullptr)
                        b.mood.push_back (w);
            }
            else
            {
                b.score = 30.0 + foreign + 0.1 * d;
                b.mood = detail::fallbackMood (q);
            }
        }
        else
        {
            // a related key: its V leads into its tonic (V/V, V/vi…), its vii° leans there too
            const auto target = Scale { source.scale.tonic, source.scale.mode };
            if (d == 4 && q == Quality::major)
            {
                b.secondaryDominant = true;
                b.score = 1.0;
                b.mood = { TR_NOOP ("driving"), TR_NOOP ("expectant") };
            }
            else if (d == 6 && q == Quality::diminished)
            {
                b.score = 2.0;
                b.mood = { TR_NOOP ("tense"), TR_NOOP ("leaning") };
            }
            else
            {
                b.score = 10.0 + foreign + 0.1 * d;
                if (const auto* e = detail::lookup (majorHome, interval, q))
                    for (const auto* w : e->mood)
                    {
                        if (w != nullptr)
                            b.mood.push_back (w);
                    }
                else
                    b.mood = detail::fallbackMood (q);
            }
            const auto tonicName = spelledName (spellDegree (target, 0));
            b.target = tonicName + (majorType (target.mode) ? "" : "m");
            if (b.secondaryDominant) // ("V/V", "V/vi": the chord it leads to, as a numeral of the home key)
                b.numeral = "V/" + numeral (home, target.tonic, majorType (target.mode) ? Quality::major : Quality::minor);
            else if (d == 6 && q == Quality::diminished)
                b.numeral = "vii\xc2\xb0/" + numeral (home, target.tonic, majorType (target.mode) ? Quality::major : Quality::minor);
        }
        out.push_back (std::move (b));
    }
    std::stable_sort (out.begin(), out.end(), [] (const Borrowed& x, const Borrowed& y) { return x.score < y.score; });
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i].rank = (int) i + 1;
    return out;
}

inline std::vector<Borrowed> borrowedChords (const Analysis& a, const RelatedKey& source)
{
    return borrowedChords (a, source, Extension::triad);
}

inline bool hasBorrowedChords (const Analysis& a, const RelatedKey& source)
{
    return ! borrowedChords (a, source).empty();
}

// --- voicing --------------------------------------------------------------------------------------------------------

enum class Inversion
{
    voiceLed, // the inversion and octave closest to the selected chord (LYNX's choice: the least total movement)
    root,
    first,
    second,
    third
};

inline const char* inversionName (Inversion i) noexcept
{
    switch (i)
    {
        case Inversion::voiceLed: return TR_NOOP ("Voice-led");
        case Inversion::root: return TR_NOOP ("Root position");
        case Inversion::first: return TR_NOOP ("1st inversion");
        case Inversion::second: return TR_NOOP ("2nd inversion");
        case Inversion::third: return TR_NOOP ("3rd inversion");
    }
    return "";
}

namespace detail
{
// the chord's notes from inversion `inv` up, close, its lowest note `low` (a key)
inline std::vector<int> closeVoicing (const prtools::Chord& c, int inv, int low)
{
    std::vector<int> iv = c.intervals;
    std::sort (iv.begin(), iv.end());
    const auto n = (int) iv.size();
    if (n == 0)
        return {};
    inv = ((inv % n) + n) % n;
    std::vector<int> keys;
    auto last = -1000;
    for (int k = 0; k < n; ++k)
    {
        const auto i = iv[(std::size_t) ((inv + k) % n)];
        auto key = low + pc (c.root + i - low);
        if (k == 0)
            key = low;
        // ninths keep their octave above the root's when they sit at the top; otherwise every note above the last
        while (key <= last)
            key += 12;
        keys.push_back (key);
        last = key;
    }
    return keys;
}

inline double cost (const std::vector<int>& to, const std::vector<int>& from)
{
    if (from.empty())
        return 0.0;
    double moves = 0.0;
    for (const auto k : to)
    {
        auto nearest = 1000;
        for (const auto f : from)
            nearest = std::min (nearest, std::abs (k - f));
        moves += nearest;
    }
    double a = 0.0, b = 0.0;
    for (const auto k : to)
        a += k;
    for (const auto k : from)
        b += k;
    return moves + 0.5 * std::abs (a / (double) to.size() - b / (double) from.size());
}
} // namespace detail

// The keys of `chord` after the selected chord's `previous` keys: voice-led (every inversion and octave tried, the one
// with the least movement and the nearest centre wins; ties: the lower inversion) or in a fixed inversion with its lowest
// note nearest the previous chord's lowest. Without a previous chord: root position from C4 (key 60).
inline std::vector<int> voice (const prtools::Chord& chord, const std::vector<int>& previous, Inversion inversion)
{
    const auto n = (int) chord.intervals.size();
    if (n == 0)
        return {};
    std::vector<int> prev = previous;
    std::sort (prev.begin(), prev.end());
    const auto anchor = prev.empty() ? 60 : prev.front();
    const auto lowFor = [&chord, anchor] (int inv) {
        std::vector<int> iv = chord.intervals;
        std::sort (iv.begin(), iv.end());
        const auto bottom = pc (chord.root + iv[(std::size_t) (inv % (int) iv.size())]);
        auto low = anchor + pc (bottom - anchor);
        if (low - anchor > 6)
            low -= 12;
        return low;
    };
    auto clampKeys = [] (std::vector<int> k) {
        while (! k.empty() && k.back() > 127)
            for (auto& v : k)
                v -= 12;
        while (! k.empty() && k.front() < 0)
            for (auto& v : k)
                v += 12;
        return k;
    };
    if (inversion != Inversion::voiceLed || prev.empty())
    {
        const auto inv = inversion == Inversion::voiceLed ? 0 : std::min (n - 1, (int) inversion - (int) Inversion::root);
        return clampKeys (detail::closeVoicing (chord, inv, lowFor (inv)));
    }
    std::vector<int> best;
    auto bestCost = 1.0e18;
    for (int inv = 0; inv < n; ++inv)
        for (const auto shift : { 0, -12, 12 })
        {
            const auto keys = clampKeys (detail::closeVoicing (chord, inv, lowFor (inv) + shift));
            const auto c = detail::cost (keys, prev);
            if (c < bestCost - 1.0e-9)
            {
                bestCost = c;
                best = keys;
            }
        }
    return best;
}
} // namespace lynx::modalhelp
