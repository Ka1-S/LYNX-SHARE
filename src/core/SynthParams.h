#pragma once

// LYNX Synth's knobs as automation targets (issue #152, round 190he: "LYNX SHARE's own plugins can't be linked"). FL
// makes every parameter of its plugins linkable (Link to controller, automation clips, Init song value); LYNX Synth's
// page had plain knobs with no parameter menu. A synth channel's AutomationTarget { channelParam, channel, 0, base + k }
// with k one of these. The numbers start at `base` so that an FL project's dead automation on a 3x Osc that LYNX
// converted to its synth (FL plugin parameter P = channelParam P, small numbers) never turns into one of these.
// Values 0…1 follow the knobs' own travel (the Synth page's ranges and skews), so a clip at 50 % is the knob at mid
// travel and a MIDI controller's centre is the knob's centre.

#include "core/Song.h"

#include <algorithm>
#include <cmath>

namespace lynx::synthparam
{
constexpr int base = 0x2800;
// the oscillators' knobs (osc o: o * oscFields + field), then the page's other knobs
enum Field : int { octave, semitones, detune, pan, level, oscFields };
enum : int
{
    attack = 3 * oscFields, decay, sustain, release, cutoff, resonance, porta, fm, fmDecay, keyTrack, sweep, sweepTime,
    bandLow, bandMid, bandHigh, count
};

constexpr bool valid (int param) noexcept { return param >= base && param < base + count; }
constexpr int target (int k) noexcept { return base + k; }
constexpr int indexOf (int param) noexcept { return param - base; }
constexpr int oscParam (int osc, Field f) noexcept { return osc * oscFields + f; }

struct Range
{
    const char* name; // English, as the page's labels ("Osc 1" goes before an oscillator's)
    float lo, hi, mid; // mid < 0: linear (else the knob's skew from its mid point, as juce::Slider)
    float step;        // > 0: the knob's interval (whole octaves, semitones and cents)
};

inline const Range& rangeOf (int k) noexcept
{
    // the same ranges and skews as the Synth page's knobs (ui/ChannelSettings.cpp SynthPanel)
    static constexpr Range oscRanges[oscFields] = { { "Octave", -3.0f, 3.0f, -1.0f, 1.0f },     { "Semitone", -11.0f, 11.0f, -1.0f, 1.0f },
                                                    { "Fine", -100.0f, 100.0f, -1.0f, 1.0f },  { "Pan", -1.0f, 1.0f, -1.0f, 0.0f },
                                                    { "Level", 0.0f, 4.0f, 1.0f, 0.0f } };
    static constexpr Range others[count - attack] = {
        { "Attack", 0.0f, 5.0f, 0.5f, 0.0f },       { "Decay", 0.0f, 5.0f, 0.5f, 0.0f },    { "Sustain", 0.0f, 1.0f, -1.0f, 0.0f },
        { "Release", 0.0f, 5.0f, 0.5f, 0.0f },      { "Cutoff", 20.0f, 20000.0f, 1000.0f, 0.0f }, { "Resonance", 0.0f, 1.0f, -1.0f, 0.0f },
        { "Porta time", 0.0f, 2.0f, 0.25f, 0.0f },  { "FM", 0.0f, 8.0f, 1.0f, 0.0f },       { "FM decay", 0.0f, 5.0f, 0.5f, 0.0f },
        { "Keytrack", -1.0f, 2.0f, -1.0f, 0.0f },   { "Sweep", 0.0f, 100.0f, 2.0f, 0.0f },  { "Sweep time", 0.001f, 2.0f, 0.1f, 0.0f },
        { "Low", 0.0f, 2.0f, -1.0f, 0.0f },         { "Mid", 0.0f, 2.0f, -1.0f, 0.0f },     { "High", 0.0f, 2.0f, -1.0f, 0.0f } };
    static constexpr Range none { "", 0.0f, 1.0f, -1.0f, 0.0f };
    if (k < 0 || k >= count)
        return none;
    return k < attack ? oscRanges[k % oscFields] : others[k - attack];
}

// 1-based oscillator of an oscillator knob, 0 for the others
constexpr int oscOf (int k) noexcept { return k >= 0 && k < attack ? k / oscFields + 1 : 0; }

inline double skewOf (const Range& r) noexcept
{
    return r.mid < 0.0f ? 1.0 : std::log (0.5) / std::log ((double) (r.mid - r.lo) / (double) (r.hi - r.lo));
}

// natural value (octaves, seconds, Hz…) -> 0…1 of the knob's travel
inline float toValue (int k, float natural) noexcept
{
    const auto& r = rangeOf (k);
    const auto p = std::clamp ((double) (natural - r.lo) / (double) (r.hi - r.lo), 0.0, 1.0);
    return (float) std::pow (p, skewOf (r));
}

// 0…1 of the knob's travel -> natural value, on the knob's interval
inline float toNatural (int k, float value) noexcept
{
    const auto& r = rangeOf (k);
    const auto v = std::clamp ((double) value, 0.0, 1.0);
    const auto p = v <= 0.0 ? 0.0 : std::exp (std::log (v) / skewOf (r));
    auto x = r.lo + (r.hi - r.lo) * p;
    if (r.step > 0.0f)
        x = r.lo + std::round ((x - r.lo) / r.step) * r.step;
    return (float) std::clamp (x, (double) r.lo, (double) r.hi);
}

inline float natural (const SynthSettings& s, int k) noexcept
{
    if (k >= 0 && k < attack)
    {
        const auto& o = s.osc[(std::size_t) (k / oscFields)];
        switch (k % oscFields)
        {
            case octave: return (float) o.octave;
            case semitones: return (float) o.semitones;
            case detune: return o.detune;
            case pan: return o.pan;
            default: return o.level;
        }
    }
    switch (k)
    {
        case attack: return s.attack;
        case decay: return s.decay;
        case sustain: return s.sustain;
        case release: return s.release;
        case cutoff: return s.cutoff;
        case resonance: return s.resonance;
        case porta: return s.portaBeats;
        case fm: return s.fm;
        case fmDecay: return s.fmDecay;
        case keyTrack: return s.keyTrack;
        case sweep: return s.sweep;
        case sweepTime: return s.sweepTime;
        case bandLow: return s.bandGain[0];
        case bandMid: return s.bandGain[1];
        case bandHigh: return s.bandGain[2];
        default: return 0.0f;
    }
}

// writes a natural value as the page's knob does (whole octaves / semitones; FM decay 0 = a constant index)
inline void setNatural (SynthSettings& s, int k, float x) noexcept
{
    if (k < 0 || k >= count || ! std::isfinite (x))
        return;
    const auto& r = rangeOf (k);
    x = std::clamp (x, r.lo, r.hi);
    if (k < attack)
    {
        auto& o = s.osc[(std::size_t) (k / oscFields)];
        switch (k % oscFields)
        {
            case octave: o.octave = (int) std::lround (x); break;
            case semitones: o.semitones = (int) std::lround (x); break;
            case detune: o.detune = x; break;
            case pan: o.pan = x; break;
            default: o.level = x; break;
        }
        return;
    }
    switch (k)
    {
        case attack: s.attack = x; break;
        case decay: s.decay = x; break;
        case sustain: s.sustain = x; break;
        case release: s.release = x; break;
        case cutoff: s.cutoff = x; break;
        case resonance: s.resonance = x; break;
        case porta: s.portaBeats = x; break;
        case fm: s.fm = x; break;
        case fmDecay:
            s.fmDecay = x;
            if (x <= 0.0f)
                s.fmSustain = 1.0f;
            break;
        case keyTrack: s.keyTrack = x; break;
        case sweep: s.sweep = x; break;
        case sweepTime: s.sweepTime = x; break;
        case bandLow: s.bandGain[0] = x; break;
        case bandMid: s.bandGain[1] = x; break;
        case bandHigh: s.bandGain[2] = x; break;
        default: break;
    }
}

inline float get (const SynthSettings& s, int k) noexcept { return toValue (k, natural (s, k)); }
inline void set (SynthSettings& s, int k, float value) noexcept { setNatural (s, k, toNatural (k, value)); }
} // namespace lynx::synthparam
