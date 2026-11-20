#pragma once

// The big-project performance gate's synthetic projects (round perfgate190id, community slice B12: "big projects make
// the UI sluggish; slow load"). Deterministic: the same code makes the same song, bit for bit, so a change of a
// measurement is a change of LYNX, never of the project. Used by tools/perfbench.cpp (audio CPU, load, memory; the
// CI gate) and by `LYNX --perfgen <folder>` (the same songs as .lynx files + their WAVs, for --paintbench / --opentime
// and by hand). scripts/perf-gate.sh runs the gate; baselines in tests/perf/.
//
//   synth100     100 built-in synth channels into 50 inserts, 3 effects each (round 190ge)
//   inserts50    one synth through 50 inserts chained by sends, 4 effects each (round 190ge)
//   stream       24 audio clips of 60 s on 12 inserts: long audio streaming (round 190ge)
//   big200       200 channels (synths, drums, samplers, audio clips), 120 inserts (100 + 20 buses) with LYNX's own
//                effects, 2,000 pattern / audio clips and 200 automation clips over 64 bars
//   automation   32 synths, 16 inserts, 400 automation clips with a point every 16th (volume, pan, sends, effect
//                parameters and mixes, channel levels, the tempo)
//
// Header only, no JUCE.

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Effects.h"
#include "core/Instruments.h"
#include "core/Song.h"
#include "core/SynthPresets.h"

namespace lynx::perfprojects
{
using Samples = std::map<std::string, std::shared_ptr<const SampleData>>;

inline const std::vector<std::string>& names()
{
    static const std::vector<std::string> n { "synth100", "inserts50", "stream", "big200", "automation" };
    return n;
}

inline SynthSettings presetAt (std::size_t k)
{
    static const auto presets = synthPresets();
    return presets.empty() ? SynthSettings {} : presets[k % presets.size()].second;
}

inline Note makeNote (int channel, double beat, double beats, int key, float velocity)
{
    Note n;
    n.channel = channel;
    n.start = (std::int64_t) (beat * ppq);
    n.length = (std::int64_t) (beats * ppq);
    n.key = key;
    n.velocity = velocity;
    return n;
}

// a deterministic take: a tone at `hz` with a little noise (LCG), stereo
inline std::shared_ptr<SampleData> makeTake (double seconds, double hz, std::uint32_t seed, float level = 0.2f)
{
    auto data = std::make_shared<SampleData>();
    const auto frames = (std::size_t) (44100.0 * seconds);
    data->left.resize (frames);
    data->right.resize (frames);
    double phase = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const auto noise = (float) ((int) (seed >> 9) - (1 << 22)) / (float) (1 << 22);
        phase += hz / 44100.0;
        const auto tone = (float) std::sin (6.283185307179586 * phase);
        data->left[i] = level * tone + 0.02f * noise;
        data->right[i] = level * tone - 0.02f * noise;
    }
    return data;
}

// a one-shot for the sampler channels: a decaying tone with a click, 0.4 s
inline std::shared_ptr<SampleData> makeHit (double hz, std::uint32_t seed)
{
    auto data = std::make_shared<SampleData>();
    const auto frames = (std::size_t) (44100.0 * 0.4);
    data->left.resize (frames);
    data->right.resize (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const auto t = (double) i / 44100.0;
        const auto noise = (float) ((int) (seed >> 9) - (1 << 22)) / (float) (1 << 22);
        const auto v = (float) (std::exp (-t * 9.0) * std::sin (6.283185307179586 * hz * t * (1.0 + 0.5 * std::exp (-t * 30.0))))
                       + (float) std::exp (-t * 120.0) * noise * 0.5f;
        data->left[i] = 0.6f * v;
        data->right[i] = 0.6f * v;
    }
    return data;
}

// 100 synth channels, two per insert, 50 inserts with 3 effects each; every channel plays 4-bar loops (a held chord
// on even channels, an eighth-note line on odd ones) for the whole song
inline Song synth100 (int bars = 16)
{
    Song song;
    song.bpm = 128.0;
    song.mixer = Song::defaultMixer (50);
    constexpr int channels = 100;
    const EffectKind chains[][3] = {
        { EffectKind::parametricEq, EffectKind::compressor, EffectKind::reverb },
        { EffectKind::facetEq, EffectKind::crestCompressor, EffectKind::delay },
        { EffectKind::eq, EffectKind::chorus, EffectKind::apexLimiter },
        { EffectKind::filter, EffectKind::ember, EffectKind::atrium },
        { EffectKind::ridgeStrip, EffectKind::echoform, EffectKind::limiter },
    };
    for (int t = 1; t <= 50; ++t)
        for (const auto kind : chains[(std::size_t) (t % 5)])
            song.mixer[(std::size_t) t].effects.push_back (makeEffectSlot (kind));
    const int roots[4] = { 45, 41, 48, 43 };
    for (int c = 0; c < channels; ++c)
    {
        ChannelDef ch;
        ch.name = "Synth " + std::to_string (c + 1);
        ch.instrument = InstrumentKind::synth;
        ch.synth = presetAt ((std::size_t) c);
        ch.volume = 0.3f;
        ch.mixerTrack = 1 + c / 2;
        song.channels.push_back (ch);
        auto& p = song.patterns.emplace_back();
        p.name = ch.name;
        for (int bar = 0; bar < 4; ++bar)
        {
            const auto root = roots[bar] + 12 * (c % 3);
            if (c % 2 == 0)
                for (int k : { 0, 4, 7 })
                    p.notes.push_back (makeNote (c, bar * 4, 3.9, root + k, 0.7f));
            else
                for (int e = 0; e < 8; ++e)
                    p.notes.push_back (makeNote (c, bar * 4 + e * 0.5, 0.4, root + 12 + (e * 5) % 12, 0.8f));
        }
        for (int b = 0; b < bars; b += 4)
        {
            Clip clip;
            clip.pattern = c;
            clip.track = c % 100;
            clip.start = (std::int64_t) b * 4 * ppq;
            clip.length = p.effectiveLength (song.meter());
            song.playlist.push_back (clip);
        }
    }
    song.mode = PlayMode::song;
    return song;
}

// one synth into insert 1; inserts 1..50 in a chain of sends (n → n+1), 4 effects each, the last to the Master
inline Song inserts50 (int bars = 16)
{
    Song song;
    song.bpm = 128.0;
    song.mixer = Song::defaultMixer (50);
    const EffectKind kinds[] = { EffectKind::parametricEq, EffectKind::compressor, EffectKind::distortion, EffectKind::chorus,
                                 EffectKind::facetEq, EffectKind::ember, EffectKind::delay, EffectKind::utility };
    for (int t = 1; t <= 50; ++t)
    {
        for (int k = 0; k < 4; ++k)
        {
            auto slot = makeEffectSlot (kinds[(std::size_t) ((t + k) % 8)]);
            slot.mix = 0.5f;
            song.mixer[(std::size_t) t].effects.push_back (slot);
        }
        if (t < 50)
        {
            song.mixer[(std::size_t) t].sends.clear();
            song.mixer[(std::size_t) t].toMaster = false;
            MixerSend s;
            s.target = t + 1;
            song.mixer[(std::size_t) t].sends.push_back (s);
        }
    }
    ChannelDef ch;
    ch.name = "Saw";
    ch.instrument = InstrumentKind::synth;
    ch.synth = presetAt (3);
    ch.mixerTrack = 1;
    song.channels.push_back (ch);
    auto& p = song.patterns.emplace_back();
    for (int e = 0; e < 16; ++e)
        p.notes.push_back (makeNote (0, e * 1.0, 0.9, 48 + (e * 7) % 12, 0.8f));
    for (int b = 0; b < bars; b += 4)
    {
        Clip clip;
        clip.pattern = 0;
        clip.start = (std::int64_t) b * 4 * ppq;
        clip.length = p.effectiveLength (song.meter());
        song.playlist.push_back (clip);
    }
    song.mode = PlayMode::song;
    return song;
}

// 24 audio clips of `seconds` each, on 12 inserts, playing together. The takes go into `samples` under
// `folder` + "/take<N>.wav" (perfbench: an in-memory folder; --perfgen writes the files there).
inline Song stream (int seconds, Samples& samples, const std::string& folder = "/stream")
{
    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer (12);
    constexpr int clips = 24;
    for (int c = 0; c < clips; ++c)
    {
        const auto path = folder + "/take" + std::to_string (c) + ".wav";
        samples[path] = makeTake (seconds, 55.0 * (1 + c % 7), 1234567u + (std::uint32_t) c * 7919u);
        ChannelDef ch;
        ch.name = "Take " + std::to_string (c + 1);
        ch.instrument = InstrumentKind::audioClip;
        ch.samplePath = path;
        ch.mixerTrack = 1 + c % 12;
        ch.volume = 0.4f;
        song.channels.push_back (ch);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = c;
        clip.track = c;
        clip.start = 0;
        clip.length = (std::int64_t) (seconds * song.bpm / 60.0 * ppq);
        song.playlist.push_back (clip);
    }
    for (int t = 1; t <= 12; ++t)
        song.mixer[(std::size_t) t].effects.push_back (makeEffectSlot (t % 2 == 0 ? EffectKind::parametricEq : EffectKind::compressor));
    song.mode = PlayMode::song;
    return song;
}

// a point every `stepBeats` over `beats`, a slow wave between lo and hi with bent segments
inline std::vector<AutomationPoint> wavePoints (double beats, double stepBeats, float lo, float hi, int seed)
{
    std::vector<AutomationPoint> points;
    for (double b = 0.0; b <= beats + 1e-9; b += stepBeats)
    {
        AutomationPoint p;
        p.tick = (std::int64_t) std::llround (b * ppq);
        const auto x = std::sin (b * 0.7 + seed * 0.37) * 0.5 + 0.5;
        p.value = lo + (hi - lo) * (float) x;
        p.tension = (seed + (int) (b * 4.0)) % 3 == 0 ? 0.4f : 0.0f;
        points.push_back (p);
    }
    return points;
}

inline int addAutomation (Song& song, AutomationTarget target, std::string name, std::vector<AutomationPoint> points)
{
    Automation a;
    a.name = std::move (name);
    a.target = target;
    a.points = std::move (points);
    song.automations.push_back (std::move (a));
    return (int) song.automations.size() - 1;
}

inline void addAutomationClip (Song& song, int automation, int track, std::int64_t start, std::int64_t length)
{
    Clip clip;
    clip.kind = ClipKind::automation;
    clip.automation = automation;
    clip.pattern = 0;
    clip.track = track;
    clip.start = start;
    clip.length = length;
    song.playlist.push_back (clip);
}

// 200 channels: 100 synths, 20 drums (kick / snare / hat / clap), 40 samplers (8 one-shots), 40 audio clips (8 s loops);
// inserts 1..100 (two channels each), each with a strip, a dynamics unit and every 4th an echo / modulation, sending
// to buses 101..120 (glue, a reverb at 25 % mix, a limiter) which go to the Master (EQ + limiter). 64 bars at 140 BPM:
// each pattern channel plays 12 of the 16 four-bar slots (1,920 clips), each audio channel 2 clips (80): 2,000 clips;
// 200 automation clips of 8 bars (insert volumes, effect parameters, channel pans), a point every beat.
inline Song big200 (Samples& samples, const std::string& folder = "/big200")
{
    Song song;
    song.bpm = 140.0;
    song.mixer = Song::defaultMixer (120);
    const EffectKind strips[] = { EffectKind::ridgeStrip, EffectKind::facetEq, EffectKind::kilnChannel, EffectKind::shellChannel,
                                  EffectKind::fretChannel };
    const EffectKind dynamics[] = { EffectKind::crestCompressor, EffectKind::glowLeveler, EffectKind::ember, EffectKind::aether,
                                    EffectKind::strikeShaper, EffectKind::sentryGate, EffectKind::hushDeEsser };
    const EffectKind motion[] = { EffectKind::echoform, EffectKind::orbit, EffectKind::spiralEcho, EffectKind::latticeMod,
                                  EffectKind::geminiDoubler };
    const EffectKind glue[] = { EffectKind::prismMultiband, EffectKind::anvilBus, EffectKind::crestCompressor };
    const EffectKind spaces[] = { EffectKind::foilPlate, EffectKind::atrium, EffectKind::nebula, EffectKind::halo, EffectKind::epochVerb,
                                  EffectKind::horizon };
    for (int t = 1; t <= 100; ++t)
    {
        auto& m = song.mixer[(std::size_t) t];
        m.effects.push_back (makeEffectSlot (strips[(std::size_t) (t % 5)]));
        m.effects.push_back (makeEffectSlot (dynamics[(std::size_t) (t % 7)]));
        if (t % 4 == 0)
        {
            auto slot = makeEffectSlot (motion[(std::size_t) ((t / 4) % 5)]);
            slot.mix = 0.3f;
            m.effects.push_back (slot);
        }
        m.toMaster = false;
        m.sends.clear();
        MixerSend s;
        s.target = 101 + (t - 1) % 20;
        m.sends.push_back (s);
    }
    for (int t = 101; t <= 120; ++t)
    {
        auto& m = song.mixer[(std::size_t) t];
        m.effects.push_back (makeEffectSlot (glue[(std::size_t) (t % 3)]));
        auto space = makeEffectSlot (spaces[(std::size_t) (t % 6)]);
        space.mix = 0.25f;
        m.effects.push_back (space);
        m.effects.push_back (makeEffectSlot (EffectKind::apexLimiter));
    }
    song.mixer[0].effects.push_back (makeEffectSlot (EffectKind::facetEq));
    song.mixer[0].effects.push_back (makeEffectSlot (EffectKind::apexLimiter));

    constexpr int bars = 64;
    const int roots[4] = { 45, 41, 48, 43 };
    const InstrumentKind drums[] = { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap };
    for (int k = 0; k < 8; ++k)
        samples[folder + "/hit" + std::to_string (k) + ".wav"] = makeHit (60.0 + 35.0 * k, 99991u + (std::uint32_t) k * 31u);
    for (int c = 0; c < 200; ++c)
    {
        ChannelDef ch;
        ch.mixerTrack = 1 + c / 2;
        ch.volume = 0.25f;
        ch.pan = (float) ((c % 9) - 4) / 8.0f;
        Pattern p;
        if (c < 100)
        {
            ch.name = "Synth " + std::to_string (c + 1);
            ch.instrument = InstrumentKind::synth;
            ch.synth = presetAt ((std::size_t) c * 7);
            for (int bar = 0; bar < 4; ++bar)
            {
                const auto root = roots[bar] + 12 * (c % 3);
                if (c % 3 == 0)
                    for (int k : { 0, 3, 7 })
                        p.notes.push_back (makeNote (c, bar * 4, 3.8, root + k, 0.6f));
                else
                    for (int e = 0; e < 8; ++e)
                        p.notes.push_back (makeNote (c, bar * 4 + e * 0.5, 0.35, root + 12 + (e * (c % 5 + 2)) % 12, 0.75f));
            }
        }
        else if (c < 120)
        {
            ch.name = "Drum " + std::to_string (c - 99);
            ch.instrument = drums[(std::size_t) (c % 4)];
            for (int s = 0; s < 64; ++s)
                if ((s * (c % 4 + 3) + c) % (c % 4 == 2 ? 2 : 5) == 0)
                    p.notes.push_back (makeNote (c, s * 0.25, 0.2, 60, 0.6f + 0.1f * (float) (s % 4)));
        }
        else if (c < 160)
        {
            ch.name = "Sampler " + std::to_string (c - 119);
            ch.instrument = InstrumentKind::sampler;
            ch.samplePath = folder + "/hit" + std::to_string (c % 8) + ".wav";
            for (int s = 0; s < 32; ++s)
                if ((s + c) % 3 == 0)
                    p.notes.push_back (makeNote (c, s * 0.5, 0.4, 48 + (s * 5 + c) % 24, 0.7f));
        }
        else
        {
            ch.name = "Loop " + std::to_string (c - 159);
            ch.instrument = InstrumentKind::audioClip;
            ch.samplePath = folder + "/loop" + std::to_string (c - 160) + ".wav";
            samples[ch.samplePath] = makeTake (8.0, 41.0 * (1 + c % 9), 777u + (std::uint32_t) c * 101u, 0.15f);
        }
        song.channels.push_back (ch);
        p.name = ch.name;
        if (c < 160)
        {
            song.patterns.push_back (p);
            const auto pattern = (int) song.patterns.size() - 1;
            const auto length = song.patterns.back().effectiveLength (song.meter());
            for (int slot = 0; slot < bars / 4; ++slot)
            {
                if ((slot + c) % 4 == 0) // (12 of 16 slots)
                    continue;
                Clip clip;
                clip.pattern = pattern;
                clip.track = c;
                clip.start = (std::int64_t) slot * 4 * 4 * ppq;
                clip.length = length;
                song.playlist.push_back (clip);
            }
        }
        else
            for (int k = 0; k < 2; ++k)
            {
                Clip clip;
                clip.kind = ClipKind::audio;
                clip.channel = c;
                clip.track = c;
                clip.start = (std::int64_t) ((c % 4) * 8 + k * 32) * 4 * ppq;
                clip.length = (std::int64_t) (8.0 * song.bpm / 60.0 * ppq);
                song.playlist.push_back (clip);
            }
    }
    // 200 automation clips of 8 bars on tracks 200..249
    const auto clipTicks = (std::int64_t) 8 * 4 * ppq;
    for (int a = 0; a < 200; ++a)
    {
        AutomationTarget target;
        std::string name;
        if (a < 100)
        {
            target.kind = AutomationTargetKind::mixerVolume;
            target.index = 1 + a;
            name = "Insert " + std::to_string (1 + a) + " volume";
        }
        else if (a < 160)
        {
            target.kind = AutomationTargetKind::effectParam;
            target.index = 1 + (a - 100);
            target.slot = 0;
            target.param = a % 4;
            name = "Insert " + std::to_string (target.index) + " slot 1 param " + std::to_string (target.param);
        }
        else
        {
            target.kind = AutomationTargetKind::channelPan;
            target.index = (a - 160) * 5;
            name = "Channel " + std::to_string (target.index + 1) + " pan";
        }
        const auto index = addAutomation (song, target, name, wavePoints (32.0, 1.0, 0.35f, 0.8f, a));
        addAutomationClip (song, index, 200 + a % 50, (std::int64_t) ((a * 3) % (bars - 8)) * 4 * ppq, clipTicks);
    }
    song.mode = PlayMode::song;
    return song;
}

// 32 synths on 16 inserts (EQ, compressor, delay), 400 automation clips of 16 bars with a point every 16th note over
// insert volumes and pans, the sends, effect parameters and mixes, channel volumes and pans, and one tempo clip
inline Song automation()
{
    Song song;
    song.bpm = 124.0;
    song.mixer = Song::defaultMixer (16);
    for (int t = 1; t <= 16; ++t)
    {
        auto& m = song.mixer[(std::size_t) t];
        m.effects.push_back (makeEffectSlot (t % 2 == 0 ? EffectKind::facetEq : EffectKind::parametricEq));
        m.effects.push_back (makeEffectSlot (t % 3 == 0 ? EffectKind::crestCompressor : EffectKind::compressor));
        auto d = makeEffectSlot (EffectKind::delay);
        d.mix = 0.3f;
        m.effects.push_back (d);
        if (t > 1)
        {
            MixerSend s;
            s.target = 1;
            s.level = 0.3f;
            m.sends.push_back (s);
        }
    }
    constexpr int bars = 32;
    for (int c = 0; c < 32; ++c)
    {
        ChannelDef ch;
        ch.name = "Synth " + std::to_string (c + 1);
        ch.instrument = InstrumentKind::synth;
        ch.synth = presetAt ((std::size_t) c * 3);
        ch.volume = 0.35f;
        ch.mixerTrack = 1 + c / 2;
        song.channels.push_back (ch);
        auto& p = song.patterns.emplace_back();
        p.name = ch.name;
        for (int e = 0; e < 16; ++e)
            p.notes.push_back (makeNote (c, e * 1.0, 0.8, 40 + (c * 5 + e * 7) % 36, 0.7f));
        for (int b = 0; b < bars; b += 4)
        {
            Clip clip;
            clip.pattern = c;
            clip.track = c;
            clip.start = (std::int64_t) b * 4 * ppq;
            clip.length = p.effectiveLength (song.meter());
            song.playlist.push_back (clip);
        }
    }
    const auto clipTicks = (std::int64_t) 16 * 4 * ppq;
    for (int a = 0; a < 400; ++a)
    {
        AutomationTarget target;
        const auto track = 1 + a % 16;
        switch (a % 8)
        {
            case 0: target = { AutomationTargetKind::mixerVolume, track, 0, 0 }; break;
            case 1: target = { AutomationTargetKind::mixerPan, track, 0, 0 }; break;
            case 2: target = { AutomationTargetKind::effectParam, track, 0, (a / 8) % 6 }; break;
            case 3: target = { AutomationTargetKind::effectParam, track, 1, (a / 8) % 4 }; break;
            case 4: target = { AutomationTargetKind::effectMix, track, 2, 0 }; break;
            case 5: target = { AutomationTargetKind::mixerSend, track == 1 ? 2 : track, 1, 0 }; break;
            case 6: target = { AutomationTargetKind::channelVolume, a % 32, 0, 0 }; break;
            default: target = { AutomationTargetKind::channelPan, a % 32, 0, 0 }; break;
        }
        const auto index = addAutomation (song, target, "Automation " + std::to_string (a + 1), wavePoints (64.0, 0.25, 0.3f, 0.75f, a));
        addAutomationClip (song, index, 40 + a % 100, (std::int64_t) (((a / 100) * 16) % bars) * 4 * ppq, clipTicks);
    }
    {
        AutomationTarget tempo;
        tempo.kind = AutomationTargetKind::tempo;
        const auto lo = valueFromTempo (122.0), hi = valueFromTempo (126.0);
        const auto index = addAutomation (song, tempo, "Tempo", wavePoints ((double) bars * 4.0, 1.0, lo, hi, 7));
        addAutomationClip (song, index, 39, 0, (std::int64_t) bars * 4 * ppq);
    }
    song.mode = PlayMode::song;
    return song;
}

// a scenario by name (`folder` for the audio files' paths); nullopt-like empty song name check is up to the caller
inline bool make (const std::string& name, Song& song, Samples& samples, const std::string& folder = {})
{
    if (name == "synth100")
        song = synth100 (16);
    else if (name == "inserts50")
        song = inserts50 (16);
    else if (name == "stream")
        song = stream (60, samples, folder.empty() ? "/stream" : folder);
    else if (name == "big200")
        song = big200 (samples, folder.empty() ? "/big200" : folder);
    else if (name == "automation")
        song = automation();
    else
        return false;
    return true;
}
} // namespace lynx::perfprojects
