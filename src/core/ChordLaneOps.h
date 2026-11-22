#pragma once

// The Chord lane's commands (round 190pf, research190oe P13; the model and the time edits are in core/ChordLane.h):
//  - detect chords from notes: the notes the song's pattern clips play over a range (or the selected clips' only; an audio
//    clip's notes come from the polyphonic converter, ui/AudioToMidi), named per bar, half bar or beat with the Piano roll's
//    chord readout (prtools::detectChord), equal neighbours joined into one block;
//  - chords from the key regions (round 190nu's key markers, as the song plays them; else the project's key): the tonic
//    chord of each region;
//  - the degree chords of the key at a point (the picker's first list: the Scale degree chord stamp's chords, round 190nu);
//  - export: the chords as notes in a new pattern, the Chord pad's chord set, and time markers for formats without a chord
//    track (.flp).
// Patent desk check (/mnt/e/lynx-work/190pf/PATENT-GATE.md): detection stays ONE offline pass over stored notes / a stored
// clip with exact interval templates — no live estimate confirmed by a later pass (Apple US 9,852,721), no tuning offset
// applied to the audio (US 9,804,818), no learned chord-sequence model refining the chords (Yamaha US 10,586,519).
// Header only, no JUCE. Tested in tests/ChordLane190pfTests.inc.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/ChordLane.h"
#include "core/KeyRegions.h"
#include "core/PianoRollTools.h"
#include "core/Song.h"
#include "core/MarkerPlan.h"
#include "core/PlaylistOps.h"

namespace lynx::chordlane
{
// A note as the song plays it: song ticks, MIDI key
struct TimedNote
{
    std::int64_t start = 0, end = 0;
    int key = 60;
};

// Channels whose notes name no harmony: drums, the one-shot Sampler, audio clips, controllers, and the channels that
// follow the lane themselves
inline bool harmonic (const ChannelDef& c) noexcept
{
    switch (c.instrument)
    {
        case InstrumentKind::kick:
        case InstrumentKind::snare:
        case InstrumentKind::hat:
        case InstrumentKind::clap:
        case InstrumentKind::sampler:
        case InstrumentKind::audioClip:
        case InstrumentKind::controller:
        case InstrumentKind::drumpad:
        case InstrumentKind::drumsynth:
        case InstrumentKind::fruitkick:
        case InstrumentKind::bassdrum:
            return false;
        default:
            return c.followChords == followOff;
    }
}

// The notes the Playlist's pattern clips play in [from, to) (each clip's passes when it is longer than its pattern), of
// harmonic channels; `onlyClips` (Playlist indices) limits them to those clips when not empty
inline std::vector<TimedNote> songNotes (const Song& song, std::int64_t from, std::int64_t to, const std::vector<int>& onlyClips = {})
{
    std::vector<TimedNote> out;
    for (int ci = 0; ci < (int) song.playlist.size(); ++ci)
    {
        const auto& c = song.playlist[(std::size_t) ci];
        if (! onlyClips.empty() && std::find (onlyClips.begin(), onlyClips.end(), ci) == onlyClips.end())
            continue;
        if (c.kind != ClipKind::pattern || c.muted || c.gridSlot >= 0 || c.length <= 0 || c.pattern < 0 || c.pattern >= (int) song.patterns.size()
            || c.end() <= from || c.start >= to)
            continue;
        const auto& p = song.patterns[(std::size_t) c.pattern];
        const auto period = std::max<std::int64_t> (1, p.effectiveLength (song.meter()));
        const auto passes = c.offset + c.length > period ? (c.offset + c.length - 1) / period : 0;
        for (std::int64_t pass = c.offset / period; pass <= passes; ++pass)
            for (const auto& n : p.notes)
            {
                if (n.muted || n.channel < 0 || n.channel >= (int) song.channels.size() || ! harmonic (song.channels[(std::size_t) n.channel]))
                    continue;
                if (passes > 0 && n.start >= period)
                    continue;
                const auto at = n.start + pass * period; // (in the clip's content)
                if (at < c.offset || at >= c.offset + c.length)
                    continue;
                TimedNote t;
                t.start = c.start + (at - c.offset);
                t.end = std::min (t.start + std::max<std::int64_t> (1, n.length), c.end());
                t.key = n.key;
                if (t.end > from && t.start < to)
                    out.push_back (t);
            }
    }
    return out;
}

enum class Resolution
{
    bar,
    halfBar,
    beat
};

// The chord of a set of sounding keys: prtools::detectChord on the pitch classes that sound long enough (a quarter of
// the strongest one), the weakest dropped one by one until a shape fits. nullopt: none.
inline std::optional<SongChord> chordOfWeights (const std::array<double, 12>& weight, const std::array<int, 12>& lowestKey, bool flats = false)
{
    double most = 0.0;
    for (const auto w : weight)
        most = std::max (most, w);
    if (most <= 0.0)
        return std::nullopt;
    std::vector<int> pcs;
    for (int pc = 0; pc < 12; ++pc)
        if (weight[(std::size_t) pc] >= 0.25 * most)
            pcs.push_back (pc);
    std::sort (pcs.begin(), pcs.end(), [&] (int a, int b) { return weight[(std::size_t) a] > weight[(std::size_t) b]; });
    while (pcs.size() >= 2)
    {
        std::vector<int> keys;
        for (const auto pc : pcs)
            keys.push_back (lowestKey[(std::size_t) pc]);
        if (const auto d = prtools::detectChord (keys, flats))
        {
            // the suffix after the root's name (and before a "/bass")
            auto n = d->name;
            if (const auto slash = n.find ('/'); slash != std::string::npos)
                n = n.substr (0, slash);
            auto rootLen = (std::size_t) 1;
            if (n.size() > 1 && (n[1] == '#' || n[1] == 'b'))
                rootLen = 2;
            SongChord c;
            if (fromSuffix (d->root, n.substr (rootLen), d->bass != d->root ? d->bass : -1, c))
                return c;
        }
        pcs.pop_back();
    }
    return std::nullopt;
}

// The chords of `notes` over [from, to), one per step (a bar, half a bar or a beat of the meter in force), equal
// neighbours joined; a step with no harmony leaves a gap
inline std::vector<SongChord> detect (const Song& song, const std::vector<TimedNote>& notes, std::int64_t from, std::int64_t to,
                                      Resolution resolution = Resolution::bar)
{
    std::vector<SongChord> out;
    if (to <= from)
        return out;
    auto at = std::max<std::int64_t> (0, from);
    for (int guard = 0; at < to && guard < 100000; ++guard)
    {
        const auto bar = song.barAt (at);
        const auto parts = resolution == Resolution::bar ? 1 : resolution == Resolution::halfBar ? 2 : std::max (1, markers::signatureAt (song, bar.tick).first);
        const auto step = std::max<std::int64_t> (1, bar.length / parts);
        // the step of the bar `at` is in
        const auto k = std::clamp<std::int64_t> ((at - bar.tick) / step, 0, parts - 1);
        const auto a = bar.tick + k * step;
        const auto b = k == parts - 1 ? bar.tick + bar.length : a + step;
        const auto wa = std::max (a, from), wb = std::min (b, to);
        std::array<double, 12> weight {};
        std::array<int, 12> lowest;
        lowest.fill (1000);
        for (const auto& n : notes)
        {
            const auto o = std::min (n.end, wb) - std::max (n.start, wa);
            if (o <= 0)
                continue;
            const auto pc = wrap12 (n.key);
            weight[(std::size_t) pc] += (double) o;
            lowest[(std::size_t) pc] = std::min (lowest[(std::size_t) pc], n.key);
        }
        if (auto c = chordOfWeights (weight, lowest))
        {
            c->start = wa;
            c->length = wb - wa;
            if (! out.empty() && out.back().end() == wa && out.back().root == c->root && out.back().quality == c->quality
                && out.back().extensions == c->extensions && out.back().bass == c->bass)
                out.back().length += c->length;
            else
                out.push_back (*c);
        }
        at = std::max (b, at + 1);
    }
    return out;
}

// Puts detected chords on the lane over [from, to): the range is cleared first (one undo step around it: the caller's
// editSong). The number of blocks written.
inline int write (Song& song, std::int64_t from, std::int64_t to, const std::vector<SongChord>& chords)
{
    auto& list = song.chords();
    clearRange (list, from, to);
    int n = 0;
    for (const auto& c : chords)
        if (put (list, c) >= 0)
            ++n;
    return n;
}

// --- keys --------------------------------------------------------------------------------------------------------------

// The chord a list of MIDI keys names (prtools::detectChord), or nullopt
inline std::optional<SongChord> fromKeys (const std::vector<int>& keys)
{
    std::array<double, 12> weight {};
    std::array<int, 12> lowest;
    lowest.fill (1000);
    for (const auto k : keys)
    {
        weight[(std::size_t) wrap12 (k)] += 1.0;
        lowest[(std::size_t) wrap12 (k)] = std::min (lowest[(std::size_t) wrap12 (k)], k);
    }
    return chordOfWeights (weight, lowest);
}

// The key the song is in at `tick`: its key regions as they play (round 190nu, keyregions::songKeyMarkers), else the
// project's key; nullopt when neither is set
inline std::optional<projectkey::Key> keyAt (const Song& song, std::int64_t tick)
{
    std::optional<projectkey::Key> k = projectkey::get (song);
    for (const auto& m : keyregions::songKeyMarkers (song))
    {
        if (m.tick > tick)
            break;
        const auto s = keyregions::stateOf (m);
        if (s.kind == keyregions::State::key)
            k = s.value;
        else
            k = projectkey::get (song);
    }
    return k;
}

// The seven chords of a key's degrees (the picker's first list; the Scale degree chord stamp's chords, round 190nu): each
// with its numeral ("I", "ii", "vii°")
struct DegreeChord
{
    SongChord chord;
    std::string numeral;
};
inline std::vector<DegreeChord> degreeChords (const projectkey::Key& key, keyregions::DegreeSize size = keyregions::DegreeSize::triad)
{
    std::vector<DegreeChord> out;
    const auto scale = keyregions::degreesOf (key);
    if (! scale.valid())
        return out;
    for (const auto step : scale.steps)
    {
        int root = 0;
        const auto rel = keyregions::diatonicChord (scale, 60 + key.root + step, size, 0, root);
        std::vector<int> keys;
        for (const auto r : rel)
            keys.push_back (root + r);
        if (auto c = fromKeys (keys))
        {
            c->bass = -1;
            out.push_back ({ *c, keyregions::degreeNumeral (scale, root, keys, size) });
        }
    }
    return out;
}

// One block per key region over [from, to): the tonic chord of the region's key (without regions: the project's key over
// the whole range). The blocks, starts in song ticks.
inline std::vector<SongChord> fromKeyRegions (const Song& song, std::int64_t from, std::int64_t to)
{
    std::vector<SongChord> out;
    if (to <= from)
        return out;
    std::vector<std::int64_t> cuts { from };
    for (const auto& m : keyregions::songKeyMarkers (song))
        if (m.tick > from && m.tick < to)
            cuts.push_back (m.tick);
    cuts.push_back (to);
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i)
    {
        if (cuts[i + 1] <= cuts[i])
            continue;
        const auto key = keyAt (song, cuts[i]);
        if (! key)
            continue;
        const auto chords = degreeChords (*key);
        if (chords.empty())
            continue;
        auto c = chords.front().chord;
        c.start = cuts[i];
        c.length = cuts[i + 1] - cuts[i];
        if (! out.empty() && out.back().end() == c.start && out.back().root == c.root && out.back().quality == c.quality && out.back().extensions == c.extensions)
            out.back().length += c.length;
        else
            out.push_back (c);
    }
    return out;
}

// --- exports -----------------------------------------------------------------------------------------------------------

// The chord's notes as the MIDI export writes them: root position from `rootOctave` (C4 = 60 for octave 4), the bass
// (slash bass, else the root) an octave under the root
inline std::vector<int> voicing (const SongChord& c, int rootOctave = 4, bool bassNote = true)
{
    std::vector<int> keys;
    const auto root = 12 * (rootOctave + 1) + c.root;
    for (const auto i : intervals (c))
        keys.push_back (root + i);
    if (bassNote)
        keys.push_back (12 * rootOctave + wrap12 (c.bass >= 0 ? c.bass : c.root));
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    for (auto& k : keys)
        k = std::clamp (k, 0, 127);
    return keys;
}

// "Export chords as MIDI": a new pattern with the lane's chords as notes on `channel` (an existing channel that plays
// notes; -1: a new Synth channel named `channelName`), and a clip of it on the first free Playlist track at the song's
// start. The new pattern's index, or -1 without chords. Call inside AudioEngine::editSong (one undo step).
inline int exportAsPattern (Song& song, int channel, const std::string& patternName, const std::string& channelName)
{
    const auto list = song.chords();
    if (list.empty())
        return -1;
    if (channel < 0 || channel >= (int) song.channels.size())
    {
        ChannelDef def;
        def.name = channelName;
        def.instrument = InstrumentKind::synth;
        def.mixerTrack = song.claimFreeInsert();
        song.channels.push_back (def);
        channel = (int) song.channels.size() - 1;
    }
    Pattern p;
    p.name = patternName;
    for (const auto& c : list)
        for (const auto k : voicing (c))
        {
            Note n;
            n.start = c.start;
            n.length = std::max<std::int64_t> (1, c.length);
            n.channel = channel;
            n.key = k;
            n.velocity = 100.0f / 128.0f;
            p.notes.push_back (n);
        }
    p.sortNotes();
    song.patterns.push_back (std::move (p));
    const auto index = (int) song.patterns.size() - 1;
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = index;
    clip.track = std::max (0, trackmodes::firstFreeTrack (song));
    clip.start = 0;
    clip.length = list.back().end();
    song.playlist.push_back (clip);
    return index;
}

// The Simple layout's Chord pad (core/ScaleKeys.h): the lane's chords, each once in song order (at most 16), as the
// project's chord set (Song::keyboardChords), octave 4; a slash bass is left out (the pad plays the chord)
inline std::vector<KeyboardChord> padSet (const std::vector<SongChord>& list)
{
    std::vector<KeyboardChord> out;
    for (const auto& c : list)
    {
        KeyboardChord k;
        k.root = c.root;
        k.quality = c.quality;
        k.octave = 4;
        k.extensions = c.extensions;
        if (std::find (out.begin(), out.end(), k) == out.end())
            out.push_back (k);
        if (out.size() >= 16)
            break;
    }
    return out;
}

// What an export to a format without a chord track writes (.flp time markers): the arrangement's markers with a plain
// marker named after each chord at its start, unless one of that name is there already; sorted. `added` gets how many.
inline std::vector<Marker> markersWithChords (const std::vector<Marker>& markers, const std::vector<SongChord>& list, int* added = nullptr)
{
    auto out = markers;
    int count = 0;
    for (const auto& c : list)
    {
        const auto label = name (c);
        if (std::none_of (markers.begin(), markers.end(), [&] (const Marker& m) { return ! m.isSignature() && m.action == Marker::none && m.tick == c.start && m.name == label; }))
        {
            out.push_back ({ c.start, label, 0, 4 });
            ++count;
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; });
    if (added != nullptr)
        *added = count;
    return out;
}
} // namespace lynx::chordlane
