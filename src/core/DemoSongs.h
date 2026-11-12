#pragma once

// LYNX's demo songs (round 190dv, owner decision O5 of cov190dq: "two more short LYNX-made genre demos offered on first
// run"). Beside the showcase song (core/ShowcaseSong.h, Demo.lynx in the installers) there are two short songs in other
// genres, built only from LYNX's own instruments (the built-in synth's presets) and Loop Starter's own kit and grooves
// (its built-in configuration, never FL's or a sample library: the same notes on every computer), as Simple-workflow
// tracks (core/SimpleTracks.h), with one-knob effects and a Master look. They are credited as LYNX-made in the
// project's info and free to use. `LYNX --demosongs <folder>` writes them as .lynx files (resources/demos/ in the repo);
// the Start from list and the Welcome screen open them from here. Render-checked in tests/FirstSongTour190dvTests.inc:
// no clipping, integrated loudness about −14 LUFS.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/ApexLimiter.h"
#include "core/GoalKnobs.h"
#include "core/LoopStarter.h"
#include "core/MasterLooks.h"
#include "core/ProjectKey.h"
#include "core/ScaleKeys.h"
#include "core/ShowcaseSong.h"
#include "core/SimpleTracks.h"
#include "core/Song.h"
#include "core/SynthPresets.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::demos
{
constexpr int count = 3; // 0 the showcase song, 1 hip hop, 2 pop

// English, for tr()
inline const char* name (int demo) noexcept
{
    switch (demo)
    {
        case 1: return "Late Bus";
        case 2: return "Sunrise";
        default: return "Night Drive";
    }
}
inline const char* genre (int demo) noexcept
{
    switch (demo)
    {
        case 1: return TR_NOOP ("Hip hop");
        case 2: return TR_NOOP ("Pop");
        default: return TR_NOOP ("Electronic");
    }
}
// the file name in resources/demos/
inline std::string fileName (int demo) { return std::string ("LYNX Demo - ") + name (demo) + ".lynx"; }

constexpr const char* credit = "Made by the LYNX SHARE project with LYNX's own instruments and Loop Starter kit. Free to use, change and share.";

namespace detail
{
inline SynthSettings preset (const char* wanted)
{
    for (const auto& [n, s] : synthPresets())
        if (n == wanted)
            return s;
    return SynthSettings {};
}

inline ChannelDef synthChannel (const char* presetName, float volume)
{
    ChannelDef c;
    c.instrument = InstrumentKind::synth;
    c.synth = preset (presetName);
    c.volume = volume;
    return c;
}

inline Note note (int channel, double beat, double beats, int key, float velocity)
{
    Note n;
    n.channel = channel;
    n.start = (std::int64_t) (beat * ppq);
    n.length = (std::int64_t) (beats * ppq);
    n.key = key;
    n.velocity = velocity;
    return n;
}

// Copies of the clip of `pattern` on `track` at every `every` bars from `fromBar` up to `toBar` (the first one, made by
// addTrack at bar 0, is moved to `fromBar`)
inline void repeatClip (Song& song, int track, int pattern, int fromBar, int toBar, int every)
{
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;
    Clip proto;
    bool found = false;
    for (auto it = song.playlist.begin(); it != song.playlist.end();)
        if (it->track == track && it->kind == ClipKind::pattern && it->pattern == pattern)
        {
            proto = *it;
            found = true;
            it = song.playlist.erase (it);
        }
        else
            ++it;
    if (! found)
        return;
    for (int b = fromBar; b < toBar; b += every)
    {
        auto c = proto;
        c.start = b * bar;
        c.length = std::min<std::int64_t> (proto.length, (toBar - b) * bar);
        song.playlist.push_back (c);
    }
}

// A Simple Drums track from Loop Starter's own genre (built-in kit, no samples) with a fixed seed: the same beat on
// every computer, repeated from `fromBar` to `toBar`
inline simpletracks::Result drums (Song& song, const char* genreId, std::uint64_t seed, int fromBar, int toBar, float kitVolume)
{
    static const auto config = loopstarter::builtinConfig();
    loopstarter::Sources sources;
    sources.config = &config;
    sources.presets[genreId] = loopstarter::builtinPresets (genreId);
    const auto plan = loopstarter::generate (sources, genreId, seed);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::drums;
    q.name = "Drums";
    q.kit = loopstarter::kitChannels (plan);
    for (auto& c : q.kit)
        c.volume = kitVolume;
    q.pattern = loopstarter::kitPattern (plan);
    auto r = simpletracks::addTrack (song, q);
    if (r.ok() && r.pattern >= 0)
    {
        const auto bars = std::max (1, (int) (song.patterns[(std::size_t) r.pattern].effectiveLength (song.beatsPerBar) / ((std::int64_t) ppq * song.beatsPerBar)));
        repeatClip (song, r.track, r.pattern, fromBar, toBar, bars);
    }
    return r;
}

inline simpletracks::Result instrument (Song& song, const char* trackName, ChannelDef channel, std::vector<Note> notes, int bars)
{
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    q.name = trackName;
    q.channel = std::move (channel);
    Pattern p;
    p.notes = std::move (notes);
    p.lengthTicks = (std::int64_t) bars * song.beatsPerBar * ppq;
    q.pattern = std::move (p);
    return simpletracks::addTrack (song, q);
}

inline void oneKnob (Song& song, const simpletracks::Result& r, int goal, float amount)
{
    if (r.ok() && r.insert >= 0 && r.insert < (int) song.mixer.size())
        song.mixer[(std::size_t) r.insert].effects.push_back (goals::make (goal, amount));
}

inline void insertLevel (Song& song, const simpletracks::Result& r, float volume, float pan = 0.0f)
{
    if (r.ok() && r.insert >= 0 && r.insert < (int) song.mixer.size())
    {
        song.mixer[(std::size_t) r.insert].volume = volume;
        song.mixer[(std::size_t) r.insert].pan = pan;
    }
}

inline Song base (double bpm, projectkey::Key key, int demo)
{
    Song song;
    song.bpm = bpm;
    song.mixer = Song::defaultMixer();
    projectkey::set (song, key);
    song.info.title = name (demo);
    song.info.author = "LYNX SHARE";
    song.info.genre = genre (demo);
    song.info.comments = credit;
    return song;
}

// The Master look, then LYNX's Apex Limiter in true-peak mode (ceiling −1 dB) bringing the song to about −14 LUFS: the
// look's own limiter works on samples, and the bright drums left inter-sample peaks up to +3 dBTP after it (measured)
inline void finish (Song& song, int look, float intensity, float inputDb, float limiterGainDb)
{
    MasterLookSettings m;
    m.id = look;
    m.intensity = intensity;
    m.inputDb = inputDb;
    looks::apply (song, m);
    auto limiter = makeEffectSlot (EffectKind::apexLimiter);
    limiter.params[(std::size_t) apex::pGain] = limiterGainDb / 30.0f;      // (0 … 30 dB)
    limiter.params[(std::size_t) apex::pCeiling] = 29.0f / 30.0f;           // −1 dB (−30 … 0 dB)
    limiter.params[(std::size_t) apex::pTruePeak] = 1.0f;
    song.mixer[0].effects.push_back (limiter);
    song.mode = PlayMode::song;
    song.currentPattern = 0;
}

inline int scaleIndex (const char* flName)
{
    const auto& list = notetools::flReferenceScales();
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::string (list[(std::size_t) i].name) == flName)
            return i;
    return 0;
}
} // namespace detail

// --- "Late Bus": hip hop, C minor, 90 BPM, 16 bars ---------------------------------------------------------------------
// Loop Starter's own boom-bap groove, a sub bass on the kick, seventh chords on a soft pad (Room), a bell line (Echo)
// from bar 9; Master look Warm tape.
inline Song makeHipHop()
{
    using namespace detail;
    const projectkey::Key key { 0, scaleIndex ("Minor Natural (Aeolian)") };
    auto song = base (90.0, key, 1);
    const int bars = 16;

    const auto kit = drums (song, "boom bap", 0x1a7eb05ULL, 2, bars, 0.78f);
    insertLevel (song, kit, 0.8f);

    // i – VI – III – VII (Cm Ab Eb Bb), sevenths, one bar each
    const int degrees[] { 0, 5, 2, 6 };
    std::vector<Note> chordNotes, bassNotes, bellNotes;
    for (int b = 0; b < 4; ++b)
    {
        const auto chord = scalekeys::diatonicChord (key, degrees[b], 48, scalekeys::Complexity::seventh, scalekeys::Spread::close);
        for (std::size_t k = 0; k < chord.size(); ++k)
            chordNotes.push_back (note (0, b * 4 + 0.03 * (double) k, 3.8, chord[k], 0.62f));
        // ("Reese Bass" plays its saws an octave under the key and its sub two under, core/ShowcaseSong.h: keys C3 … B3
        // give saws at 65–123 Hz and the sub at 33–62 Hz)
        const auto root = 48 + scalekeys::wrap12 (chord.front() - 48);
        bassNotes.push_back (note (0, b * 4 + 0.0, 1.4, root, 0.9f));
        bassNotes.push_back (note (0, b * 4 + 1.5, 0.9, root, 0.75f));
        bassNotes.push_back (note (0, b * 4 + 2.5, 1.3, root + (b == 3 ? 2 : 0), 0.82f));
    }
    // a bell phrase in C minor pentatonic (C Eb F G Bb), two bars answered by two
    const double beats[] { 0.0, 0.5, 1.0, 2.0, 3.0, 4.5, 5.0, 6.0, 8.0, 8.5, 9.0, 10.0, 11.5, 12.0, 13.0, 14.0 };
    const int keys[] { 79, 82, 79, 75, 77, 75, 72, 70, 79, 82, 84, 82, 79, 77, 75, 72 };
    for (int i = 0; i < 16; ++i)
        bellNotes.push_back (note (0, beats[i], i % 4 == 3 ? 1.5 : 0.45, keys[i], i % 2 == 0 ? 0.7f : 0.55f));

    const auto pad = instrument (song, "Keys", synthChannel ("Pad", 0.62f), chordNotes, 4);
    repeatClip (song, pad.track, pad.pattern, 0, bars, 4);
    oneKnob (song, pad, goals::room, 0.45f);
    insertLevel (song, pad, 0.7f, -0.15f);

    const auto bass = instrument (song, "Bass", synthChannel ("Reese Bass", 0.7f), bassNotes, 4);
    repeatClip (song, bass.track, bass.pattern, 4, bars, 4);
    oneKnob (song, bass, goals::warm, 0.3f);
    insertLevel (song, bass, 0.6f);

    const auto bell = instrument (song, "Bell", synthChannel ("Bell", 0.5f), bellNotes, 4);
    repeatClip (song, bell.track, bell.pattern, 8, bars, 4);
    oneKnob (song, bell, goals::echo, 0.35f);
    insertLevel (song, bell, 0.55f, 0.2f);

    song.addMarker ({ 0, "Intro" });
    song.addMarker ({ 4 * (std::int64_t) ppq * 4, "Verse" });
    song.addMarker ({ 8 * (std::int64_t) ppq * 4, "Hook" });
    finish (song, looks::warmTape, 0.5f, 0.0f, 2.0f);
    return song;
}

// --- "Sunrise": pop, G major, 112 BPM, 16 bars ---------------------------------------------------------------------------
// Loop Starter's own four-on-the-floor groove, an eighth-note bass, wide chords on the Supersaw (Room), a pluck melody
// (Echo) from bar 9; Master look Balanced.
inline Song makePop()
{
    using namespace detail;
    const projectkey::Key key { 7, scaleIndex ("Major (Ionian)") };
    auto song = base (112.0, key, 2);
    const int bars = 16;

    const auto kit = drums (song, "house", 0x52c0deULL, 4, bars, 0.75f);
    insertLevel (song, kit, 0.78f);

    // I – V – vi – IV (G D Em C)
    const int degrees[] { 0, 4, 5, 3 };
    std::vector<Note> chordNotes, bassNotes, leadNotes;
    for (int b = 0; b < 4; ++b)
    {
        const auto chord = scalekeys::diatonicChord (key, degrees[b], 55, scalekeys::Complexity::triad, scalekeys::Spread::open);
        for (const auto k : chord)
        {
            chordNotes.push_back (note (0, b * 4 + 0.0, 1.8, k, 0.6f));
            chordNotes.push_back (note (0, b * 4 + 2.5, 1.4, k, 0.52f));
        }
        const auto root = 48 + scalekeys::wrap12 (chord.front() - 48); // (keys C3 … B3, as above)
        for (int e = 0; e < 8; ++e)
            bassNotes.push_back (note (0, b * 4 + e * 0.5, 0.4, root + (e == 7 ? 7 : 0), e % 2 == 0 ? 0.85f : 0.6f));
    }
    // the melody: G major pentatonic (G A B D E)
    const int line[] { 71, 74, 76, 74, 71, 69, 67, 69, 74, 76, 79, 76, 74, 71, 69, 71,
                       71, 74, 76, 79, 81, 79, 76, 74, 72, 71, 69, 67, 69, 71, 74, 71 };
    for (int k = 0; k < 32; ++k)
        if (k % 8 != 7)
            leadNotes.push_back (note (0, k * 0.5, k % 4 == 3 ? 0.9 : 0.4, line[k], k % 2 == 0 ? 0.8f : 0.62f));

    const auto chords = instrument (song, "Chords", synthChannel ("Supersaw", 0.5f), chordNotes, 4);
    repeatClip (song, chords.track, chords.pattern, 0, bars, 4);
    oneKnob (song, chords, goals::room, 0.35f);
    insertLevel (song, chords, 0.6f, -0.1f);

    const auto bass = instrument (song, "Bass", synthChannel ("Reese Bass", 0.65f), bassNotes, 4);
    repeatClip (song, bass.track, bass.pattern, 4, bars, 4);
    insertLevel (song, bass, 0.6f);

    const auto lead = instrument (song, "Lead", synthChannel ("Pluck", 0.62f), leadNotes, 4);
    repeatClip (song, lead.track, lead.pattern, 8, bars, 4);
    oneKnob (song, lead, goals::echo, 0.3f);
    insertLevel (song, lead, 0.62f, 0.15f);

    song.addMarker ({ 0, "Intro" });
    song.addMarker ({ 4 * (std::int64_t) ppq * 4, "Verse" });
    song.addMarker ({ 8 * (std::int64_t) ppq * 4, "Chorus" });
    finish (song, looks::balanced, 0.5f, 3.0f, 5.0f);
    return song;
}

inline Song make (int demo)
{
    switch (demo)
    {
        case 1: return makeHipHop();
        case 2: return makePop();
        default: return makeShowcaseSong (0);
    }
}
} // namespace lynx::demos
