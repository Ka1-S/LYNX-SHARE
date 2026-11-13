#pragma once

// LYNX's own demo project (Demo.lynx in the installers, the README / website screenshots and the website's demo.mp3):
// `LYNX.exe --showcase output.lynx [pattern]` saves this song. In the core so the tests can render it.

#include <algorithm>
#include <cstdint>

#include "core/Effector.h"
#include "core/Effects.h"
#include "core/MasterLooks.h"
#include "core/Song.h"
#include "core/SynthPresets.h"

namespace lynx
{
// The bass line's keys over the chord roots (A2 F2 C3 G2). "Reese Bass" plays its two saws an octave under the key and
// its sub two under: +12 puts the saws on A2 F2 C3 G2 (87–131 Hz) and the sub on A1 F1 C2 G1 (44–65 Hz). It was -12 up
// to 0.1.25: saws at 22–33 Hz, the sub at 11–16 Hz, two octaves under a bass (reported for 0.1.25).
constexpr int showcaseBassTranspose = 12;
// The lead's Effector (Distort) input: 0.1 reads "-40.0 dB" (the gain is the value squared)
constexpr float showcaseLeadEffectorInput = 0.1f;

inline Song makeShowcaseSong (int currentPattern = 0)
{
    Song song;
    song.bpm = 124.0;
    song.mixer = Song::defaultMixer();
    const auto presets = synthPresets();
    auto preset = [&] (const char* name) {
        for (const auto& [n, sset] : presets)
            if (n == name)
                return sset;
        return SynthSettings {};
    };
    auto channel = [&] (const char* name, InstrumentKind kind, float volume, int track, std::uint32_t colour) {
        ChannelDef c;
        c.name = name;
        c.instrument = kind;
        c.volume = volume;
        c.mixerTrack = track;
        c.colour = colour;
        song.channels.push_back (c);
        return (int) song.channels.size() - 1;
    };
    const auto kick = channel ("Kick", InstrumentKind::kick, 0.9f, 1, 0xffd9534f);
    const auto clap = channel ("Clap", InstrumentKind::clap, 0.75f, 2, 0xfff0ad4e);
    const auto hat = channel ("Hat", InstrumentKind::hat, 0.6f, 3, 0xfff7d774);
    const auto snare = channel ("Snare", InstrumentKind::snare, 0.7f, 4, 0xffe8835a);
    const auto bass = channel ("Reese Bass", InstrumentKind::synth, 0.8f, 5, 0xff5bc0de);
    song.channels[(std::size_t) bass].synth = preset ("Reese Bass"); // preset name as defined in core/SynthPresets
    const auto pad = channel ("Pad", InstrumentKind::synth, 0.7f, 6, 0xff9b7fd6);
    song.channels[(std::size_t) pad].synth = preset ("Pad");
    const auto pluck = channel ("Pluck", InstrumentKind::synth, 0.72f, 7, 0xff5cb85c);
    song.channels[(std::size_t) pluck].synth = preset ("Pluck");
    const auto lead = channel ("Supersaw", InstrumentKind::synth, 0.6f, 8, 0xff4fc3a1);
    song.channels[(std::size_t) lead].synth = preset ("Supersaw");

    auto addPattern = [&] (const char* name, std::uint32_t colour) {
        auto& p = song.patterns.emplace_back();
        p.name = name;
        p.colour = colour;
        return (int) song.patterns.size() - 1;
    };
    auto note = [&] (int pattern, int ch, double beat, double beats, int key, float velocity = 0.8f) {
        Note n;
        n.channel = ch;
        n.start = (std::int64_t) (beat * ppq);
        n.length = (std::int64_t) (beats * ppq);
        n.key = key;
        n.velocity = velocity;
        song.patterns[(std::size_t) pattern].notes.push_back (n);
    };

    const auto beat = addPattern ("Beat", 0xffd9534f);
    for (int s = 0; s < 16; s += 4)
        song.patterns[(std::size_t) beat].toggleStep (kick, s);
    for (int s : { 4, 12 })
        song.patterns[(std::size_t) beat].toggleStep (clap, s);
    for (int s = 2; s < 16; s += 4)
        song.patterns[(std::size_t) beat].toggleStep (hat, s);
    for (int s : { 7, 15 })
        song.patterns[(std::size_t) beat].toggleStep (snare, s);

    const auto groove = addPattern ("Groove", 0xfff0ad4e);
    for (int s = 0; s < 16; ++s)
        song.patterns[(std::size_t) groove].toggleStep (hat, s);
    for (int s : { 4, 12 })
        song.patterns[(std::size_t) groove].toggleStep (clap, s);

    // Am – F – C – G, one bar each
    const int roots[4] = { 45, 41, 48, 43 };
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
    const auto bassline = addPattern ("Bass", 0xff5bc0de);
    for (int bar = 0; bar < 4; ++bar)
        for (int e = 0; e < 8; ++e)
            if (e != 3)
                note (bassline, bass, bar * 4 + e * 0.5, 0.4, roots[bar] + showcaseBassTranspose + (e == 6 ? 12 : 0), e % 2 == 0 ? 0.9f : 0.65f);
    const auto chordsPattern = addPattern ("Chords", 0xff9b7fd6);
    for (int bar = 0; bar < 4; ++bar)
        for (int k = 0; k < 3; ++k)
            note (chordsPattern, pad, bar * 4, 3.9, chords[bar][k], 0.7f);
    const auto melody = addPattern ("Melody", 0xff5cb85c);
    const int line[] = { 69, 72, 76, 74, 72, 69, 67, 69, 65, 69, 72, 74, 72, 69, 67, 64,
                         72, 76, 79, 76, 74, 72, 74, 76, 71, 74, 79, 78, 76, 74, 71, 67 };
    for (int k = 0; k < 32; ++k)
        if (k % 8 != 7)
            note (melody, pluck, k * 0.5, k % 4 == 3 ? 0.9 : 0.4, line[k], k % 2 == 0 ? 0.85f : 0.65f);
    const auto hook = addPattern ("Hook", 0xff4fc3a1);
    for (int bar = 0; bar < 4; ++bar)
        for (int k = 0; k < 3; ++k)
            note (hook, lead, bar * 4 + k * 1.5, k == 2 ? 1.0 : 1.4, chords[bar][2] + 12 - k * 3, 0.8f);

    // Playlist: intro, verse, drop, breakdown, final drop
    song.playlistTracks = { { "Drums" }, { "Groove" }, { "Bass" }, { "Chords" }, { "Melody" }, { "Hook" }, { "Automation" } };
    const auto bar = (std::int64_t) ppq * 4;
    auto clip = [&] (int pattern, int track, int fromBar, int bars) {
        const auto length = song.patterns[(std::size_t) pattern].effectiveLength (song.meter());
        for (int b = fromBar; b < fromBar + bars;)
        {
            Clip c;
            c.pattern = pattern;
            c.track = track;
            c.start = b * bar;
            c.length = length;
            song.playlist.push_back (c);
            b += (int) std::max<std::int64_t> (1, length / bar);
        }
    };
    clip (chordsPattern, 3, 0, 8);
    clip (melody, 4, 4, 4);
    clip (beat, 0, 8, 16);
    clip (groove, 1, 12, 12);
    clip (bassline, 2, 8, 8);
    clip (melody, 4, 8, 8);
    clip (chordsPattern, 3, 16, 4);
    clip (bassline, 2, 20, 4);
    clip (hook, 5, 20, 4);
    clip (melody, 4, 20, 4);
    song.addMarker ({ 0, "Intro" });
    song.addMarker ({ 8 * bar, "Drop" });
    song.addMarker ({ 16 * bar, "Breakdown" });
    song.addMarker ({ 20 * bar, "Final" });

    // automation: the pad volume opens up during the intro
    Automation a;
    a.name = "Pad Volume";
    a.target = { AutomationTargetKind::mixerVolume, 6, 0, 0 };
    a.points = { { 0, 0.15f, 0.4f }, { 8 * bar, 0.8f, 0.0f } };
    song.automations.push_back (a);
    Clip ac;
    ac.kind = ClipKind::automation;
    ac.automation = 0;
    ac.track = 6;
    ac.start = 0;
    ac.length = 8 * bar;
    song.playlist.push_back (ac);

    // mixer: names, colours and effects (bass compressor sidechained to the kick)
    const char* names[] = { "Master", "Kick", "Clap", "Hat", "Snare", "Bass", "Pad", "Pluck", "Lead" };
    for (int t = 0; t < 9; ++t)
    {
        song.mixer[(std::size_t) t].name = names[t];
        if (t > 0)
            song.mixer[(std::size_t) t].colour = song.channels[(std::size_t) t - 1].colour;
    }
    auto fx = [&] (int track, EffectKind kind) -> EffectSlot& {
        song.mixer[(std::size_t) track].effects.push_back (makeEffectSlot (kind));
        return song.mixer[(std::size_t) track].effects.back();
    };
    {
        // kick EQ: cut below 30 Hz, weight at 60 Hz, remove the "boxiness" at 350 Hz, brightness at 4 kHz
        auto& eq = fx (1, EffectKind::parametricEq);
        auto band = [&] (int b, peq::BandType type, double hz, double db, double q) {
            eq.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (type);
            eq.params[(std::size_t) peq::param (b, 1)] = peq::hzValue (hz);
            eq.params[(std::size_t) peq::param (b, 2)] = peq::gainValue (db);
            eq.params[(std::size_t) peq::param (b, 3)] = peq::qValue (q);
        };
        band (0, peq::BandType::lowCut, 30.0, 0.0, 0.7);
        band (1, peq::BandType::peak, 62.0, 5.0, 1.2);
        band (2, peq::BandType::peak, 350.0, -6.0, 2.0);
        band (3, peq::BandType::peak, 4200.0, 3.5, 1.5);
        band (4, peq::BandType::highCut, 14000.0, 0.0, 0.7);
        for (int b = 5; b < peq::bands; ++b)
            eq.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (peq::BandType::off);
    }
    fx (1, EffectKind::distortion).mix = 0.3f;
    auto& comp = fx (5, EffectKind::compressor);
    comp.params[0] = 0.55f;
    comp.params[1] = 0.8f;
    comp.sidechain = 1;
    fx (5, EffectKind::parametricEq);
    fx (6, EffectKind::chorus);
    fx (6, EffectKind::reverb);
    fx (7, EffectKind::delay);
    fx (7, EffectKind::reverb).mix = 0.4f;
    // (Distort at its full input level made the lead 21 dB louder than without it: the last section played 9.6 LU
    // over the rest, into the limiter; reported for 0.1.25)
    fx (8, EffectKind::effector).params[(std::size_t) effector::inputLevel] = showcaseLeadEffectorInput;
    fx (8, EffectKind::delay);
    fx (0, EffectKind::parametricEq);
    auto& limiter = fx (0, EffectKind::limiter);
    limiter.params = { 11.7f / 12.0f, 0.1f, 0.5f };
    // round 190ea: the Limiter holds sample peaks (−0.3 dBFS), and the song left +0.8 dBTP between them (16× meter and
    // ffmpeg agree; the website's demo.mp3 measures +0.2 dBTP). The Master looks' true-peak stage ends the chain: −1 dBTP.
    song.mixer[0].effects.push_back (looks::truePeakStage());
    song.mixer[1].volume = 0.78f;
    song.mixer[5].volume = 0.72f;
    song.mixer[6].volume = 0.6f;
    song.mixer[6].pan = -0.2f;
    song.mixer[7].pan = 0.25f;

    song.mode = PlayMode::song;
    song.currentPattern = std::clamp (currentPattern, 0, (int) song.patterns.size() - 1);
    return song;
}
} // namespace lynx
