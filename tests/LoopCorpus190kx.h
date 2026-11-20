#pragma once

// Round 190kx (loop analysis): a corpus of LYNX's OWN synthetic loops with known tempo and key, to measure the key and
// tempo detectors (core/KeyFollow detectKey, core/TempoDetect detect, core/LoopAnalysis). Nothing here comes from a
// commercial sample: drums are synthesised (a pitched sine kick, noise snares and hats), tonal parts are additive tones
// playing chord progressions, bass lines, melodies and arpeggios in a known key. Deterministic (a fixed PRNG per item).
// Used by tests/LoopAnalysis190kxTests.inc (a small subset) and tools/loop_accuracy.cpp (the whole corpus).

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace loopcorpus
{
constexpr double rate = 44100.0;
constexpr double twoPi = 6.283185307179586;

struct Rng
{
    std::uint64_t s;
    explicit Rng (std::uint64_t seed) : s (seed * 6364136223846793005ull + 1442695040888963407ull) {}
    std::uint32_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return (std::uint32_t) (s >> 33);
    }
    double uniform() { return (double) next() / 2147483648.0; } // 0..1
    double bipolar() { return uniform() * 2.0 - 1.0; }
    int below (int n) { return (int) (next() % (std::uint32_t) n); }
};

enum class Kind
{
    drums,  // drums only: a tempo, no key
    tonal,  // pitched parts only: a key (and the tempo of their rhythm)
    mixed,  // drums + bass + chords (+ melody): both
    oneShot // a single hit or chord, no loop: neither a tempo of whole beats nor (for a hit) a key
};

struct Item
{
    std::string name;        // e.g. "drums-house-124"
    std::string style;       // drum style or tonal part
    Kind kind = Kind::drums;
    double bpm = 0.0;        // the written tempo (0: none)
    bool halfTimeFeel = false; // the backbeat falls on beat 3 (trap): the felt pulse is half the written tempo
    int keyRoot = -1;        // -1: no key
    bool minor = false;
    double beats = 0.0;      // the loop's length in beats at `bpm`
    std::shared_ptr<lynx::SampleData> audio;
};

// --- voices --------------------------------------------------------------------------------------------------------
inline void addKick (std::vector<float>& out, std::size_t at, float gain)
{
    const auto n = (std::size_t) (0.35 * rate);
    double phase = 0.0;
    for (std::size_t i = 0; i < n && at + i < out.size(); ++i)
    {
        const auto t = (double) i / rate;
        const auto f = 48.0 + 110.0 * std::exp (-t * 30.0);
        phase += twoPi * f / rate;
        out[at + i] += gain * (float) (std::sin (phase) * std::exp (-t * 9.0) + (i < 60 ? 0.3 * (1.0 - i / 60.0) : 0.0));
    }
}
inline void addSnare (std::vector<float>& out, std::size_t at, float gain, Rng& rng)
{
    const auto n = (std::size_t) (0.2 * rate);
    double lp = 0.0;
    for (std::size_t i = 0; i < n && at + i < out.size(); ++i)
    {
        const auto t = (double) i / rate;
        const auto noise = rng.bipolar();
        lp += 0.5 * (noise - lp);
        out[at + i] += gain * (float) (0.7 * lp * std::exp (-t * 22.0) + 0.4 * std::sin (twoPi * 185.0 * t) * std::exp (-t * 30.0));
    }
}
inline void addHat (std::vector<float>& out, std::size_t at, float gain, Rng& rng, bool open)
{
    const auto n = (std::size_t) ((open ? 0.25 : 0.05) * rate);
    double prev = 0.0;
    for (std::size_t i = 0; i < n && at + i < out.size(); ++i)
    {
        const auto t = (double) i / rate;
        const auto noise = rng.bipolar();
        const auto hp = noise - prev; // a first difference: the highs
        prev = noise;
        out[at + i] += gain * (float) (0.35 * hp * std::exp (-t * (open ? 12.0 : 70.0)));
    }
}

// an additive tone: `harmonics` partials of 1/k (odd only for a square-like tone), an attack and a decay
struct Timbre
{
    int harmonics = 8;
    bool oddOnly = false;
    double decay = 1.5;   // per second
    double attack = 0.005;
    double bright = 1.0;  // the partials' fall: 1/k^bright
};
inline void addTone (std::vector<float>& out, std::size_t at, double seconds, int midi, float gain, const Timbre& tb, double detuneCents = 0.0)
{
    const auto f0 = 440.0 * std::pow (2.0, (midi - 69 + detuneCents / 100.0) / 12.0);
    const auto n = (std::size_t) (seconds * rate);
    const auto release = (std::size_t) (0.03 * rate);
    const auto attack = std::max<std::size_t> (1, (std::size_t) (tb.attack * rate));
    const auto decayStep = std::exp (-tb.decay / rate);
    // the envelope once, then each partial by a rotating phasor (fast; the corpus is large)
    const auto total = std::min (n + release, at < out.size() ? out.size() - at : 0);
    std::vector<double> env (total);
    double d = 1.0;
    for (std::size_t i = 0; i < total; ++i, d *= decayStep)
    {
        auto e = std::min (1.0, (double) i / (double) attack) * d;
        if (i >= n)
            e *= 1.0 - (double) (i - n) / (double) release;
        env[i] = e;
    }
    for (int k = 1; k <= tb.harmonics; ++k)
    {
        if (tb.oddOnly && k % 2 == 0)
            continue;
        const auto f = f0 * k;
        if (f > 9000.0)
            break;
        const auto a = gain / std::pow ((double) k, tb.bright);
        const auto w = twoPi * f / rate;
        const auto cr = std::cos (w), ci = std::sin (w);
        double re = 1.0, im = 0.0; // e^{i w t}, renormalised now and then
        for (std::size_t i = 0; i < total; ++i)
        {
            out[at + i] += (float) (a * env[i] * im);
            const auto nr = re * cr - im * ci;
            im = re * ci + im * cr;
            re = nr;
            if ((i & 1023) == 0)
            {
                const auto m = 1.0 / std::sqrt (re * re + im * im);
                re *= m, im *= m;
            }
        }
    }
}

// --- scales and progressions ---------------------------------------------------------------------------------------
inline const std::array<int, 7>& scale (bool minor)
{
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 }, natural { 0, 2, 3, 5, 7, 8, 10 };
    return minor ? natural : major;
}
// a triad on scale degree `degree` (0-based) of the key, as MIDI notes from `base`
inline std::array<int, 3> triad (int root, bool minor, int degree, int base)
{
    const auto& s = scale (minor);
    std::array<int, 3> out {};
    for (int i = 0; i < 3; ++i)
    {
        const auto d = degree + 2 * i;
        out[(std::size_t) i] = base + root + s[(std::size_t) (d % 7)] + 12 * (d / 7);
    }
    return out;
}
// common progressions (scale degrees, one chord per bar); the minor ones may raise the 7th for the V chord (harmonic)
inline std::vector<int> progression (bool minor, int which)
{
    static const std::vector<std::vector<int>> maj { { 0, 4, 5, 3 }, { 0, 3, 4, 0 }, { 1, 4, 0, 5 }, { 0, 5, 3, 4 } };
    static const std::vector<std::vector<int>> min { { 0, 5, 2, 6 }, { 0, 3, 4, 0 }, { 0, 6, 5, 6 }, { 0, 3, 6, 2 } };
    const auto& list = minor ? min : maj;
    return list[(std::size_t) (which % (int) list.size())];
}

// --- the loops -----------------------------------------------------------------------------------------------------
inline std::size_t frameAt (double beat, double bpm) { return (std::size_t) std::llround (beat * 60.0 / bpm * rate); }

// a drum pattern on 16ths of `bars` 4/4 bars. Styles: house, rock, boombap, trap (half-time: snare on 3), dnb, halftime
// (a slow tempo with busy 16th hats), shuffle (swung 8ths)
inline void drumPattern (std::vector<float>& out, const std::string& style, double bpm, int bars, Rng& rng, float gain = 1.0f)
{
    const auto at = [&] (double beat) {
        auto f = (double) frameAt (beat, bpm);
        f += rng.bipolar() * 0.003 * rate; // ±3 ms of human timing
        return (std::size_t) std::max (0.0, f);
    };
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto b0 = 4.0 * bar;
        if (style == "house")
        {
            for (int q = 0; q < 4; ++q)
                addKick (out, at (b0 + q), 0.9f * gain), addHat (out, at (b0 + q + 0.5), 0.8f * gain, rng, q % 2 == 1);
            addSnare (out, at (b0 + 1), 0.6f * gain, rng), addSnare (out, at (b0 + 3), 0.6f * gain, rng);
            for (int s = 0; s < 16; ++s)
                if (s % 2 == 1)
                    addHat (out, at (b0 + s * 0.25), 0.3f * gain, rng, false);
        }
        else if (style == "rock")
        {
            addKick (out, at (b0), 0.9f * gain), addKick (out, at (b0 + 2), 0.9f * gain);
            if (bar % 2 == 1)
                addKick (out, at (b0 + 2.5), 0.7f * gain);
            addSnare (out, at (b0 + 1), 0.8f * gain, rng), addSnare (out, at (b0 + 3), 0.8f * gain, rng);
            for (int e = 0; e < 8; ++e)
                addHat (out, at (b0 + e * 0.5), (e % 2 ? 0.4f : 0.6f) * gain, rng, false);
        }
        else if (style == "boombap" || style == "shuffle")
        {
            const auto swing = style == "shuffle" ? 0.66 : 0.58; // the off-8th's place in the beat
            addKick (out, at (b0), 0.9f * gain), addKick (out, at (b0 + 1 + swing), 0.6f * gain), addKick (out, at (b0 + 2.75), 0.7f * gain);
            addSnare (out, at (b0 + 1), 0.8f * gain, rng), addSnare (out, at (b0 + 3), 0.8f * gain, rng);
            for (int q = 0; q < 4; ++q)
                addHat (out, at (b0 + q), 0.5f * gain, rng, false), addHat (out, at (b0 + q + swing), 0.35f * gain, rng, false);
        }
        else if (style == "trap")
        {
            // written at `bpm`, felt at half: kick on 1 and "2a", snare on 3, 16th hats with a 32nd roll on beat 4
            addKick (out, at (b0), 1.0f * gain), addKick (out, at (b0 + 1.75), 0.8f * gain), addKick (out, at (b0 + 3.5), 0.6f * gain);
            addSnare (out, at (b0 + 2), 0.9f * gain, rng);
            for (int s = 0; s < 16; ++s)
            {
                if (s >= 12 && bar % 2 == 1)
                {
                    addHat (out, at (b0 + s * 0.25), 0.35f * gain, rng, false), addHat (out, at (b0 + s * 0.25 + 0.125), 0.25f * gain, rng, false);
                    continue;
                }
                addHat (out, at (b0 + s * 0.25), (s % 2 ? 0.25f : 0.4f) * gain, rng, false);
            }
        }
        else if (style == "dnb")
        {
            addKick (out, at (b0), 1.0f * gain), addKick (out, at (b0 + 2.5), 0.8f * gain);
            addSnare (out, at (b0 + 1), 0.9f * gain, rng), addSnare (out, at (b0 + 3), 0.9f * gain, rng);
            for (int e = 0; e < 8; ++e)
                addHat (out, at (b0 + e * 0.5), (e % 2 ? 0.45f : 0.3f) * gain, rng, false);
        }
        else // halftime: slow tempo, kick 1 and 2&, snare 3... no: snare on 2 and 4, busy 16th hats
        {
            addKick (out, at (b0), 1.0f * gain), addKick (out, at (b0 + 1.5), 0.7f * gain), addKick (out, at (b0 + 2.25), 0.6f * gain);
            addSnare (out, at (b0 + 1), 0.9f * gain, rng), addSnare (out, at (b0 + 3), 0.9f * gain, rng);
            for (int s = 0; s < 16; ++s)
                addHat (out, at (b0 + s * 0.25), (s % 4 == 0 ? 0.45f : s % 2 ? 0.25f : 0.35f) * gain, rng, false);
        }
    }
}

// a tonal part in a key over `bars` bars. Parts: chords (held, one per bar), stabs (8th-note chords), bass, melody,
// arp (16ths), pad (long chords, slow attack)
inline void tonalPart (std::vector<float>& out, const std::string& part, int root, bool minor, double bpm, int bars, int prog, Rng& rng,
                       float gain = 1.0f)
{
    const auto prg = progression (minor, prog);
    const auto beatSec = 60.0 / bpm;
    const auto& sc = scale (minor);
    Timbre saw { 10, false, 0.8, 0.008, 1.0 }, square { 9, true, 1.2, 0.004, 1.0 }, keys { 5, false, 2.5, 0.003, 1.6 },
        pluck { 12, false, 5.0, 0.002, 1.1 }, pad { 8, false, 0.2, 0.25, 1.3 };
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto degree = prg[(std::size_t) (bar % (int) prg.size())];
        const auto chord = triad (root, minor, degree, 48 + (root >= 6 ? -12 : 0) + 12);
        const auto b0 = 4.0 * bar;
        if (part == "chords" || part == "pad")
        {
            for (auto n : chord)
                addTone (out, frameAt (b0, bpm), 4.0 * beatSec * 0.98, n, 0.18f * gain, part == "pad" ? pad : keys, rng.bipolar() * 6.0);
        }
        else if (part == "stabs")
        {
            for (int e = 0; e < 8; ++e)
                if (e != 3 && e != 7)
                    for (auto n : chord)
                        addTone (out, frameAt (b0 + e * 0.5, bpm), 0.4 * beatSec, n, 0.12f * gain, saw);
        }
        else if (part == "bass")
        {
            const auto low = 36 + ((root + sc[(std::size_t) degree]) % 12);
            const int pattern[8] = { 0, -1, 0, 0, -1, 0, 7, 0 };
            for (int e = 0; e < 8; ++e)
                if (pattern[e] >= 0)
                    addTone (out, frameAt (b0 + e * 0.5, bpm), 0.45 * beatSec, low + (pattern[e] == 7 && minor ? 7 : pattern[e]), 0.35f * gain, square);
        }
        else if (part == "arp")
        {
            for (int s = 0; s < 16; ++s)
            {
                const int order[4] = { 0, 1, 2, 1 };
                const auto n = chord[(std::size_t) order[s % 4]] + (s >= 8 ? 12 : 0);
                addTone (out, frameAt (b0 + s * 0.25, bpm), 0.22 * beatSec, n, 0.15f * gain, pluck);
            }
        }
        else // melody: chord tones on the strong beats, scale steps between, the tonic to end
        {
            auto pos = 0.0;
            while (pos < 4.0 - 1e-9)
            {
                const auto len = rng.uniform() < 0.5 ? 0.5 : 1.0;
                int midi;
                if (bar == bars - 1 && pos >= 3.0)
                    midi = 60 + root;
                else if (std::fmod (pos, 1.0) < 1e-9)
                    midi = chord[(std::size_t) rng.below (3)] + 12;
                else
                    midi = 60 + root + sc[(std::size_t) rng.below (7)];
                addTone (out, frameAt (b0 + pos, bpm), len * beatSec * 0.9, midi, 0.2f * gain, saw);
                pos += len;
            }
        }
    }
}

inline std::shared_ptr<lynx::SampleData> finish (std::vector<float> mono, bool stereo)
{
    float peak = 0.0f;
    for (auto v : mono)
        peak = std::max (peak, std::abs (v));
    if (peak > 0.0f)
        for (auto& v : mono)
            v *= 0.8f / peak;
    auto s = std::make_shared<lynx::SampleData>();
    s->sampleRate = rate;
    s->left = mono;
    if (stereo)
        s->right = std::move (mono);
    return s;
}

inline Item drums (const std::string& style, double bpm, int bars, std::uint64_t seed)
{
    Rng rng (seed);
    Item it;
    it.style = style;
    it.kind = Kind::drums;
    it.bpm = bpm;
    it.halfTimeFeel = style == "trap";
    it.beats = 4.0 * bars;
    std::vector<float> buf (frameAt (it.beats, bpm), 0.0f);
    drumPattern (buf, style, bpm, bars, rng);
    it.audio = finish (std::move (buf), seed % 2 == 0);
    it.name = "drums-" + style + "-" + std::to_string ((int) std::lround (bpm));
    return it;
}

inline const char* noteName (int pc)
{
    static const char* names[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pc % 12) + 12) % 12];
}

inline Item tonal (const std::string& part, int root, bool minor, double bpm, int bars, std::uint64_t seed)
{
    Rng rng (seed);
    Item it;
    it.style = part;
    it.kind = Kind::tonal;
    it.bpm = bpm;
    it.keyRoot = root;
    it.minor = minor;
    it.beats = 4.0 * bars;
    std::vector<float> buf (frameAt (it.beats, bpm), 0.0f);
    if (part == "band") // bass + chords + melody, no drums
    {
        tonalPart (buf, "bass", root, minor, bpm, bars, (int) (seed % 4), rng);
        tonalPart (buf, "chords", root, minor, bpm, bars, (int) (seed % 4), rng);
        tonalPart (buf, "melody", root, minor, bpm, bars, (int) (seed % 4), rng);
    }
    else
        tonalPart (buf, part, root, minor, bpm, bars, (int) (seed % 4), rng);
    it.audio = finish (std::move (buf), seed % 3 == 0);
    it.name = std::string ("tonal-") + part + "-" + noteName (root) + (minor ? "m" : "") + "-" + std::to_string ((int) std::lround (bpm));
    return it;
}

inline Item mixed (const std::string& style, int root, bool minor, double bpm, int bars, std::uint64_t seed)
{
    Rng rng (seed);
    Item it;
    it.style = style;
    it.kind = Kind::mixed;
    it.bpm = bpm;
    it.halfTimeFeel = style == "trap";
    it.keyRoot = root;
    it.minor = minor;
    it.beats = 4.0 * bars;
    std::vector<float> buf (frameAt (it.beats, bpm), 0.0f);
    drumPattern (buf, style, bpm, bars, rng, 0.8f);
    tonalPart (buf, "bass", root, minor, bpm, bars, (int) (seed % 4), rng, 0.8f);
    tonalPart (buf, seed % 2 ? "stabs" : "chords", root, minor, bpm, bars, (int) (seed % 4), rng, 0.8f);
    if (seed % 3 == 0)
        tonalPart (buf, "melody", root, minor, bpm, bars, (int) (seed % 4), rng, 0.7f);
    it.audio = finish (std::move (buf), true);
    it.name = "mixed-" + style + "-" + noteName (root) + (minor ? "m" : "") + "-" + std::to_string ((int) std::lround (bpm));
    return it;
}

// a one-shot: a single drum hit (no key) or a single held chord of 2.5 s (its key is not a loop's; no tempo)
inline Item oneShot (bool chord, int root, std::uint64_t seed)
{
    Rng rng (seed);
    Item it;
    it.kind = Kind::oneShot;
    it.style = chord ? "chord-hit" : "drum-hit";
    std::vector<float> buf ((std::size_t) (2.5 * rate), 0.0f);
    if (chord)
        for (auto n : triad (root, false, 0, 60))
            addTone (buf, 0, 2.3, n, 0.2f, Timbre { 8, false, 1.0, 0.004, 1.2 });
    else
        addKick (buf, 0, 1.0f), addSnare (buf, 0, 0.3f, rng);
    it.audio = finish (std::move (buf), false);
    it.name = std::string ("oneshot-") + it.style + "-" + noteName (root);
    return it;
}

// The whole corpus (tools/loop_accuracy.cpp) or a small subset (`small`, the unit tests): drums at 70–180 BPM in seven
// styles, the four tonal parts and the band in all 24 keys, mixed loops, one-shots.
inline std::vector<Item> corpus (bool small, const std::string& only = {})
{
    std::vector<Item> out;
    // `only`: a part of "drums-<style>", "tonal-<part>", "mixed-<style>" or "oneshot": the others are not made
    const auto wanted = [&only] (const std::string& tag) { return only.empty() || tag.find (only) != std::string::npos || only.find (tag) != std::string::npos; };
    std::uint64_t seed = 190;
    // drums: each style over its usual range (and the general ones over the whole range)
    struct Range
    {
        const char* style;
        double lo, hi;
    };
    const Range ranges[] = { { "house", 112, 130 }, { "rock", 70, 180 }, { "boombap", 70, 100 }, { "trap", 130, 160 },
                             { "dnb", 160, 180 },   { "halftime", 70, 95 }, { "shuffle", 80, 140 } };
    const double tempos[] = { 70, 72, 75, 78, 80, 84, 85, 88, 90, 92, 95, 98, 100, 102, 105, 108, 110, 112, 115, 118, 120, 122, 124, 125,
                              126, 128, 130, 132, 135, 138, 140, 142, 145, 148, 150, 155, 160, 165, 170, 172, 174, 175, 178, 180 };
    int k = 0;
    for (const auto& r : ranges)
        for (const auto bpm : tempos)
            if (bpm >= r.lo && bpm <= r.hi && (! small || (k++ % 5) == 0))
            {
                ++seed;
                if (wanted (std::string ("drums-") + r.style))
                    out.push_back (drums (r.style, bpm, bpm >= 150 ? 8 : bpm < 85 ? 2 : 4, seed));
            }
    // tonal parts: every key for the full corpus (a third of them for the subset)
    const char* parts[] = { "chords", "bass", "melody", "arp", "stabs", "pad", "band" };
    int t = 0;
    for (const auto* part : parts)
        for (int root = 0; root < 12; ++root)
            for (const bool minor : { false, true })
            {
                ++seed;
                if (small && (t++ % 5) != 0) // (5: the subset rotates through roots and modes)
                    continue;
                const auto bpm = 80.0 + (double) ((root * 7 + (minor ? 3 : 0)) % 13) * 5.0; // 80..140
                if (wanted (std::string ("tonal-") + part))
                    out.push_back (tonal (part, root, minor, bpm, 4, seed));
            }
    // mixed loops
    const char* styles[] = { "house", "boombap", "trap", "rock", "dnb", "halftime" };
    int m = 0;
    for (const auto* style : styles)
        for (int root = 0; root < 12; ++root)
            for (const bool minor : { false, true })
            {
                ++seed;
                if ((root + (minor ? 5 : 0)) % 3 != 0)
                    continue; // 8 keys per style
                if (small && (m++ % 4) != 0)
                    continue;
                double lo = 80, hi = 140;
                for (const auto& r : ranges)
                    if (std::string (r.style) == style)
                        lo = r.lo, hi = r.hi;
                const auto bpm = std::round (lo + (hi - lo) * (double) ((root * 5 + (minor ? 1 : 0)) % 9) / 8.0);
                if (wanted (std::string ("mixed-") + style))
                    out.push_back (mixed (style, root, minor, bpm, bpm >= 150 ? 8 : bpm < 85 ? 2 : 4, seed));
            }
    // one-shots
    for (int i = 0; i < (small ? 2 : 6); ++i)
        if (++seed; wanted ("oneshot"))
            out.push_back (oneShot (i % 2 == 1, i * 5 % 12, seed));
    return out;
}
} // namespace loopcorpus
