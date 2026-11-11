#pragma once

// The Browser's "Preview in time" (LYNX extra, round 190aq; TOP-20 item 6 of round 190ak). Off by default.
//
// A loop auditioned in the Browser plays stretched to the project's tempo (LYNX's own stretcher, core/TimeStretch,
// pitch kept) and, while the transport plays, starts on the next beat; with the transport stopped it starts at once.
// The file's tempo comes from, in this order:
//   1. the WAV's ACID beat count (its length in beats);
//   2. the file name: "… 98 bpm …" / "bpm98", or a number from 60 to 200 standing alone between separators when the
//      file is a whole number of beats long at that tempo ("Loop_128_Am");
//   3. LYNX's tempo detection (core/TempoDetect), for files from 2 to 70 seconds that it finds to be a loop of whole
//      beats (4 or more) with a clear pulse.
// A file with no usable tempo (a one-shot, a voice take, a whole song) plays as it always did. Matching the song's key
// is not part of this.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include "core/AutoWarp.h"
#include "core/Instruments.h"
#include "core/Song.h"
#include "core/TempoDetect.h"

namespace lynx::previewtempo
{
enum class Source
{
    none,
    acid,     // the file's ACID beat count
    name,     // the file name
    detected  // LYNX's tempo detection
};

struct Found
{
    double bpm = 0.0; // 0 = no usable tempo: the file plays as it is
    Source source = Source::none;
};

constexpr double minBpm = 40.0, maxBpm = 300.0;
constexpr double maxLoopSeconds = 70.0;              // longer is a song or a take, not a loop
constexpr double minDetectSeconds = 2.0;             // shorter files are not analysed (one-shots)
constexpr double minDetectConfidence = 0.35;         // how much the pulse must stand out (tempodetect::Result::confidence)

// "… 98 bpm …" (autowarp::bpmInName), "bpm98" / "BPM_98", or 0.
inline double explicitNameTempo (const std::string& name)
{
    if (const auto bpm = autowarp::bpmInName (name); bpm > 0.0)
        return bpm;
    std::string s;
    for (const auto c : name)
        s += (char) std::tolower ((unsigned char) c);
    for (auto at = s.find ("bpm"); at != std::string::npos; at = s.find ("bpm", at + 3))
    {
        auto begin = at + 3;
        while (begin < s.size() && (s[begin] == ' ' || s[begin] == '_' || s[begin] == '-'))
            ++begin;
        auto end = begin;
        while (end < s.size() && (std::isdigit ((unsigned char) s[end]) != 0 || s[end] == '.'))
            ++end;
        if (end == begin)
            continue;
        if (const auto bpm = std::atof (s.substr (begin, end - begin).c_str()); bpm >= minBpm && bpm <= maxBpm)
            return bpm;
    }
    return 0.0;
}

// A whole number from 60 to 200 standing alone between separators ("Dm7", "808s", "4bars" don't count), or 0.
inline double bareNameTempo (const std::string& stem)
{
    const auto alnum = [] (char c) { return std::isalnum ((unsigned char) c) != 0; };
    for (std::size_t i = 0; i < stem.size();)
    {
        if (! alnum (stem[i]))
        {
            ++i;
            continue;
        }
        auto end = i;
        bool digits = true;
        while (end < stem.size() && alnum (stem[end]))
            digits = std::isdigit ((unsigned char) stem[end++]) != 0 && digits;
        if (digits && end - i >= 2 && end - i <= 3)
            if (const auto bpm = std::atoi (stem.substr (i, end - i).c_str()); bpm >= 60 && bpm <= 200)
                return (double) bpm;
        i = end;
    }
    return 0.0;
}

// How far `beats` is from a whole number of beats.
inline double offBeat (double beats) noexcept { return std::abs (beats - std::round (beats)); }

// The tempo in a file name for a file of `seconds` (0 = none). `name` may carry its extension.
inline double nameTempo (const std::string& name, double seconds)
{
    auto stem = name;
    if (const auto dot = stem.find_last_of ('.'); dot != std::string::npos && dot > 0)
        stem.resize (dot);
    if (const auto bpm = explicitNameTempo (stem); bpm > 0.0)
        return seconds * bpm / 60.0 >= 1.0 ? bpm : 0.0; // (says "bpm": a loop with a tail counts; under a beat is a hit)
    if (const auto bpm = bareNameTempo (stem); bpm > 0.0)
    {
        // a bare number may be anything ("Snare 100"): only when the file is that many whole beats, two or more
        const auto beats = seconds * bpm / 60.0;
        if (beats >= 1.9 && offBeat (beats) <= 0.06)
            return bpm;
    }
    return 0.0;
}

// The tempo ACID / the name give, without reading the audio (cheap: the message thread may call it).
inline Found quick (const SampleData& sample, const std::string& name)
{
    if (sample.frames() == 0 || ! (sample.sampleRate > 0.0))
        return {};
    const auto seconds = (double) sample.frames() / sample.sampleRate;
    if (seconds > maxLoopSeconds)
        return {};
    if (sample.acidBeats > 0.0f)
        if (const auto bpm = (double) sample.acidBeats * 60.0 / seconds; bpm >= minBpm && bpm <= maxBpm)
            return { bpm, Source::acid };
    if (const auto bpm = nameTempo (name, seconds); bpm > 0.0)
        return { bpm, Source::name };
    return {};
}

// Whether the audio itself is worth analysing when quick() found nothing.
inline bool worthDetecting (const SampleData& sample) noexcept
{
    if (sample.frames() == 0 || ! (sample.sampleRate > 0.0))
        return false;
    const auto seconds = (double) sample.frames() / sample.sampleRate;
    return seconds >= minDetectSeconds && seconds <= maxLoopSeconds;
}

// LYNX's tempo detection, kept only for a loop of whole beats with a clear pulse (reads the audio: a worker thread).
inline Found detect (const SampleData& sample)
{
    if (! worthDetecting (sample))
        return {};
    const auto result = tempodetect::detect (sample.leftData(), sample.isStereo() ? sample.rightData() : nullptr, (std::size_t) sample.frames(),
                                             sample.sampleRate);
    if (! (result.bpm >= minBpm && result.bpm <= maxBpm) || result.confidence < minDetectConfidence)
        return {};
    if (result.beats < 3.9 || offBeat (result.beats) > 0.04)
        return {};
    return { result.bpm, Source::detected };
}

// quick(), then detect().
inline Found find (const SampleData& sample, const std::string& name)
{
    if (const auto found = quick (sample, name); found.bpm > 0.0)
        return found;
    return detect (sample);
}

// The stretch that brings a file of `fileBpm` to `projectBpm`: output length / input length (TimeStretch's ratio).
// Up to one and a half times faster or slower the file simply takes the project's tempo (a 90 BPM loop in a 128 BPM
// song plays at 128). Beyond that its tempo is read an octave up or down, whichever is nearer, so a 70 BPM loop in a
// 140 BPM song plays as it is (half-time) instead of twice as fast. 1 = nothing to do (no usable tempo, or within
// 0.1 %: no stretch at all).
inline double stretchRatio (double fileBpm, double projectBpm) noexcept
{
    if (! (fileBpm > 0.0) || ! (projectBpm > 0.0))
        return 1.0;
    auto ratio = fileBpm / projectBpm;
    if (ratio > 1.5 || ratio < 1.0 / 1.5)
    {
        while (ratio > 1.41421356237)
            ratio *= 0.5;
        while (ratio < 0.70710678118)
            ratio *= 2.0;
    }
    return std::abs (ratio - 1.0) < 0.001 ? 1.0 : ratio;
}

// How many samples from the start of a block until the next beat is heard. `positionTicks` is the transport at the
// block's start, `latencySamples` the PDC delay of what is heard (the song's sound is that late against the
// transport). 0 = on a beat now.
inline double samplesToNextBeat (double positionTicks, double ticksPerSample, int latencySamples) noexcept
{
    if (! (ticksPerSample > 0.0))
        return 0.0;
    const auto heard = positionTicks - (double) latencySamples * ticksPerSample;
    auto phase = std::fmod (heard, (double) ppq);
    if (phase < 0.0)
        phase += (double) ppq;
    if (phase < 1.0e-9)
        return 0.0;
    return ((double) ppq - phase) / ticksPerSample;
}
} // namespace lynx::previewtempo
