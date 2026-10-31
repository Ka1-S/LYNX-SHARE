// GMS, Ogun/Autogun, Sakura and Fruity DrumSynth Live approximated by the built-in synth (or by a
// drum sample). Formats and formulas in docs/analise/REA_rodada12.md (clean-room).

#include "core/FlSynths.h"
#include "core/Inflate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace lynx
{
namespace
{
constexpr double pi = 3.141592653589793;

std::uint32_t u32At (const std::uint8_t* d, std::size_t offset)
{
    std::uint32_t v = 0;
    std::memcpy (&v, d + offset, 4);
    return v;
}

void setPitch (SynthOscillator& osc, double semitones)
{
    if (! std::isfinite (semitones))
        semitones = 0.0;
    while (semitones < -47.0)
        semitones += 12.0;
    while (semitones > 47.0)
        semitones -= 12.0;
    const auto whole = (int) std::lround (semitones);
    osc.octave = std::clamp (whole / 12, -3, 3);
    osc.semitones = std::clamp (whole - osc.octave * 12, -11, 11);
    osc.detune = (float) std::clamp ((semitones - whole) * 100.0 + osc.detune, -100.0, 100.0);
}

std::string lower (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

// ---- GMS ----

// LYNX waveform from the GMS .wav name (§1.4); `octaveUp` for waves with a dominant 2nd harmonic
Waveform gmsWave (const std::string& path, bool& octaveUp)
{
    auto name = path.substr (path.find_last_of ("\\/") == std::string::npos ? 0 : path.find_last_of ("\\/") + 1);
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos)
        name.resize (dot);
    name = lower (name);
    octaveUp = name == "digital" || name == "double spike" || name == "devils horn" || name == "liquid";

    if (name.empty() || name == "sine" || name == "keys")
        return Waveform::sine;
    if (name == "triangle" || name == "smooth sub" || name == "rhodes" || name == "string")
        return Waveform::triangle;
    if (name.rfind ("square", 0) == 0 || name == "digital")
        return Waveform::square;
    return Waveform::saw;
}

// GMS ladder cutoff in Hz for u = 0..1 (table 2^(9.77u), pole at 4× 44.1 kHz)
double gmsCutoffHz (double u)
{
    const auto g = 0.0005 * std::pow (2.0, 9.7734 * std::clamp (u, 0.0, 0.999));
    return std::clamp (-std::log (1.0 - g) * 4.0 * 44100.0 / (2.0 * pi), 20.0, 20000.0);
}

int truncf32 (float x) { return (int) x; }
// ---- Ogun's timbre engine (REA_rodada24 §1) ----

// IPP-style uniform generator (§1.3): subtract-with-borrow + congruential sequence, our own implementation
std::vector<float> ogunUniform (int count, float lo, float hi, std::int32_t seed)
{
    constexpr std::uint32_t a = 69069u, c = 0x3c6ef373u;
    const auto s = (std::uint32_t) seed;
    std::uint32_t e3 = (s * a + c) * a + c, e2 = s * a + c, e1 = s; // e[k−3], e[k−2], e[k−1]
    std::uint32_t l = 0x436cbae9u;
    std::int32_t borrow = -1;
    const auto scale = (double) (hi - lo) * std::ldexp (1.0, -32);
    const auto offset = (double) (lo + hi) * 0.5;
    std::vector<float> out ((std::size_t) std::max (0, count));
    for (auto& o : out)
    {
        auto v = (std::int32_t) (e2 - e3 + (std::uint32_t) borrow);
        borrow = v < 0 ? -1 : 0;
        if (v < 0)
            v = (std::int32_t) ((std::uint32_t) v - 18u);
        e3 = e2;
        e2 = e1;
        e1 = (std::uint32_t) v;
        l = l * a + c;
        o = (float) ((double) (std::int32_t) ((std::uint32_t) v + l) * scale + offset);
    }
    return out;
}

// 32-bit integer mix used for the seeds (§1.4)
std::int32_t ogunMix (std::int32_t value, std::uint32_t k1)
{
    const auto a = (std::uint32_t) value;
    std::uint32_t u6 = (0u - a - k1) ^ (a << 8);
    std::uint32_t u7 = (0x1eefu - a - u6) ^ (u6 >> 13);
    std::uint32_t u5 = (a - u6 - u7) ^ (u7 >> 12);
    u6 = (u6 - u7 - u5) ^ (u5 << 16);
    u7 = (u7 - u5 - u6) ^ (u6 >> 5);
    u5 = (u5 - u6 - u7) ^ (u7 >> 3);
    u6 = (u6 - u7 - u5) ^ (u5 << 10);
    u6 = (u7 - u5 - u6) ^ (u6 >> 15);
    const auto r = (std::int32_t) u6;
    return r == INT32_MIN ? INT32_MAX : std::abs (r);
}

struct OgunPoint
{
    double dx = 0.0, y = 0.0;
    float tension = 0.0f;
    int mode = 0;
};
using OgunEnvelope = std::vector<OgunPoint>;

// The state's envelopes (`i32 type, i32 version 3, i32 n`, n × 24 B points, 20 B trailer), in order
std::vector<OgunEnvelope> ogunEnvelopes (const std::vector<std::uint8_t>& body)
{
    std::vector<OgunEnvelope> out;
    auto i32 = [&] (std::size_t at) { std::int32_t v; std::memcpy (&v, body.data() + at, 4); return v; };
    std::size_t i = 0x110;
    while (i + 12 <= body.size())
    {
        const auto kind = i32 (i), version = i32 (i + 4), n = i32 (i + 8);
        if (kind >= 0 && kind < 64 && version == 3 && n >= 1 && n <= 70000 && i + 12 + (std::size_t) n * 24 + 20 <= body.size())
        {
            OgunEnvelope env;
            auto ok = true;
            auto j = i + 12;
            for (int k = 0; k < n && ok; ++k, j += 24)
            {
                OgunPoint p;
                std::memcpy (&p.dx, body.data() + j, 8);
                std::memcpy (&p.y, body.data() + j + 8, 8);
                std::memcpy (&p.tension, body.data() + j + 16, 4);
                p.mode = body[j + 20];
                ok = std::isfinite (p.dx) && std::isfinite (p.y) && p.dx >= -1e-9 && p.dx <= 1e6 && std::abs (p.y) < 1e6 && std::abs (p.tension) <= 1.0001f;
                env.push_back (p);
            }
            if (ok)
            {
                out.push_back (std::move (env));
                i = j + 20;
                continue;
            }
        }
        ++i;
    }
    return out;
}

// Segment shape t → 0..1 of FL's envelope library (REA_rodada23 §1.2). Modes 1 and 10 (not transcribed) are linear.
double ogunSegment (double t, int mode, double tension)
{
    const auto sign = tension > 0.0 ? 1 : tension < 0.0 ? -1 : 0;
    auto count = [&] { return std::round ((std::pow (1000001.0, 1.0 - 0.6 * std::abs (tension)) - 1.0) * 0.001); };
    switch (mode)
    {
        case 0:
        {
            if (tension == 0.0)
                return t;
            const auto k = std::pow (1000001.0, std::abs (tension)) - 1.0;
            const auto l = std::log (k + 1.0);
            return tension > 0.0 ? 1.0 - (std::exp ((1.0 - t) * l) - 1.0) / k : (std::exp (t * l) - 1.0) / k;
        }
        case 2: return 0.0;
        case 5:
        {
            const auto c10 = 2.0 * (count() + 1.0);
            return (double) ((long long) std::round (t * c10 - (tension < 0.0 ? -0.5 : 0.5)) & 1);
        }
        case 6:
        {
            const auto c10 = count() + 0.5;
            if (sign > 0 && count() == 0.0)
                return t;
            if (sign < 0)
                return (std::sin ((t * c10 + 0.75) * 2.0 * pi) + 1.0) * 0.5;
            const auto v = t * c10 - 0.5;
            const auto r = std::round (v - 0.5);
            return std::abs ((v - r) * 2.0 - 1.0);
        }
        case 7:
        {
            const auto u = sign > 0 ? 1.0 - t : t;
            return t - (std::pow (u, 6.0) - u) * tension;
        }
        default: return t;
    }
}

double ogunEnvelopeAt (const OgunEnvelope& env, double x)
{
    if (env.empty())
        return 0.0;
    double total = 0.0;
    for (std::size_t j = 1; j < env.size(); ++j)
        total += env[j].dx;
    if (total <= 0.0)
        total = 1.0;
    if (x <= 0.0)
        return env[0].y;
    double start = 0.0;
    for (std::size_t j = 1; j < env.size(); ++j)
    {
        const auto end = start + env[j].dx / total;
        if (x < end || j + 1 == env.size())
        {
            if (x >= end)
                return env[j].y;
            const auto t = end > start ? (x - start) / (end - start) : 0.0;
            return env[j - 1].y + ogunSegment (t, env[j].mode, env[j].tension) * (env[j].y - env[j - 1].y);
        }
        start = end;
    }
    return env.back().y;
}

// Ogun's initial spectrum (§1.5–§1.7) in the "spectral" oscillator. Partial h of an Ogun note of frequency f sounds
// at h·f·2^(7 − P52); the oscillator runs at f·2^o, o = max(−2, 7 − P52), so partial h lands on index h/stride,
// stride = 2^(P52 − 7 + o). With P52 ≤ 9 every partial is an exact index; above, partials are binned (energy) to the
// nearest index. Returns false if the maps cannot be read. `decaySeconds`/`releaseSeconds` = time for the whole
// spectrum to fall 40 dB with the per-partial times (< 0 = no decay).
struct OgunSpectrum
{
    std::array<float, SynthSettings::spectrumSize> amplitude {}, phase {};
    std::array<float, SynthSettings::spectrumSize> decay {}; // per index, seconds to −60 dB (0 = holds)
    std::array<float, SynthSettings::spectrumSize> target {}; // ALT DECAY: level each index settles on
    int octave = 0;
    double decaySeconds = -1.0, releaseSeconds = -1.0;
};

bool ogunSpectrum (const std::int32_t* p, const std::vector<std::uint8_t>& body, OgunSpectrum& out)
{
    const auto envelopes = ogunEnvelopes (body);
    if (envelopes.size() != 82 || body.size() < 0xFC)
        return false;
    const auto& forced = envelopes[78];
    const auto& impulseMap = envelopes[79];
    const auto& decayMap = envelopes[80];
    const auto& releaseMap = envelopes[81];
    const bool perNoteSeed = body[0xE0] != 0, logAxis = body[0xE7] != 0, altDecay = body[0xF8] != 0,
               normalise = body[0xF9] != 0, invSqrt = body[0xFA] != 0;
    std::int32_t pattern[4];
    std::memcpy (pattern, body.data() + 0xE8, sizeof (pattern));

    const auto richness = std::clamp (p[52], 6, 16);
    const auto m = (1 << (richness - 1)) - 1; // partials 1…M
    const auto mapLength = std::min (m * 3 / 2, 0x8000);

    // map sampling (§1.6): x = i/32767 or log2(i + 1)/15; EQs use (3^(2y) − 1)/2
    auto sample = [&] (const OgunEnvelope& env, int length, bool eq, bool sqrtScaled, int patternValue)
    {
        std::vector<double> v ((std::size_t) length);
        for (int i = 0; i < length; ++i)
        {
            const auto x = (logAxis ? std::log2 (i + 1.0) / 15.0 : i / 32767.0) + 5.96e-8;
            auto y = std::clamp (ogunEnvelopeAt (env, x), 0.0, 1.0);
            if (eq)
                y = y * 2.0 == 1.0 ? 1.0 : (std::pow (3.0, 2.0 * y) - 1.0) / 2.0;
            if (sqrtScaled)
                y /= std::sqrt (i + 1.0);
            if (patternValue > 0 && ((patternValue < 5 && i % (patternValue + 1) == patternValue) || (patternValue >= 5 && i % (patternValue - 2) != 0)))
                y = 0.0;
            v[(std::size_t) i] = y;
        }
        return v;
    };
    const auto forcedOn = std::any_of (forced.begin(), forced.end(), [] (const OgunPoint& pt) { return pt.y != 0.0; });
    const auto forcedValues = forcedOn ? sample (forced, m, false, invSqrt, pattern[0]) : std::vector<double> {};
    const auto impulse = sample (impulseMap, mapLength, true, invSqrt, pattern[1]);
    const auto decayEq = sample (decayMap, mapLength, true, invSqrt && altDecay, pattern[2]);
    const auto releaseEq = sample (releaseMap, mapLength, true, false, pattern[3]);
    auto mapAt = [] (const std::vector<double>& v, int i) { return v.empty() ? 1.0 : v[(std::size_t) std::min (i, (int) v.size() - 1)]; };

    // articulators at their neutral values (§1.8): art6 = art7 = art10 = 1, art9 = art11 = art12 = 0
    const auto fullness = std::clamp (p[8] / 1000.0, -1.0, 1.0);
    const auto shape = (std::pow (10000001.0, std::abs (fullness)) - 1.0) * 0.01 * (fullness > 0.0 ? -1.0 : fullness < 0.0 ? 1.0 : 0.0);
    const auto morph = p[9] / 1000.0;
    const auto preDecay = std::max (0.001, p[5] / 1000.0);
    auto seedOf = [] (std::int32_t v) { return v - 1 < 0 ? -1 : ogunMix (v - 0x20cf, 0x1cfc); };
    // Seed 1 per key: key 60 (C5) is used; Seed 2 = "Random" draws a new seed on every note in FL — LYNX keeps one
    // fixed draw (counter 0)
    const auto seed1 = perNoteSeed ? ogunMix (p[10] + 60 - 0x1f88, 0x1e42) : seedOf (p[10]);
    const auto seed2 = p[11] - 1 < 0 ? ogunMix (0 - 0x200e, 0x1dbc) : seedOf (p[11]);

    auto weights = [&] (std::int32_t seed)
    {
        auto u = ogunUniform (m, 0.0f, 1.0f, seed);
        std::vector<double> g ((std::size_t) m);
        for (int i = 0; i < m; ++i)
        {
            const double x = u[(std::size_t) i];
            if (shape == 0.0)
                g[(std::size_t) i] = x;
            else if (shape > 0.0)
                g[(std::size_t) i] = (std::pow (1.0 + shape, x) - 1.0) / shape;
            else
                g[(std::size_t) i] = ((1.0 - shape) - std::pow (1.0 - shape, 1.0 - x)) / -shape;
        }
        return g;
    };
    std::vector<double> b ((std::size_t) m, 0.0);
    if (morph != 0.0)
    {
        const auto g = weights (seed2);
        for (int i = 0; i < m; ++i)
            b[(std::size_t) i] += morph * g[(std::size_t) i];
    }
    if (morph < 1.0)
    {
        const auto g = seed1 < 0 ? std::vector<double> ((std::size_t) m, 1.0) : weights (seed1);
        for (int i = 0; i < m; ++i)
            b[(std::size_t) i] += (1.0 - morph) * g[(std::size_t) i];
    }
    for (int i = 0; i < m; ++i)
    {
        auto& w = b[(std::size_t) i];
        w = std::max (0.0, w);
        if (forcedOn)
            w = 1.0 - forcedValues[(std::size_t) i] * (1.0 - w);
    }

    // pre-decayed amplitudes and RMS normalisation (§1.7)
    std::vector<double> d ((std::size_t) m);
    double sumD = 0.0;
    for (int i = 0; i < m; ++i)
    {
        d[(std::size_t) i] = std::pow (0.01, 1.0 / (b[(std::size_t) i] / preDecay + 0.001));
        sumD += d[(std::size_t) i] * d[(std::size_t) i];
    }
    const auto rms = std::sqrt (sumD / m);
    if (rms <= 0.0)
        return false;
    auto gain = 1.0;
    if (normalise)
    {
        auto mapRms = [&] (const std::vector<double>& v) { double s = 0.0; for (int i = 0; i < m; ++i) s += v[(std::size_t) i] * v[(std::size_t) i]; return std::sqrt (s / m); };
        auto r = mapRms (impulse);
        if (altDecay)
            r = std::max (r, mapRms (decayEq));
        if (r > 5.96e-8)
            gain = 0.5 / r;
    }
    int first = m, last = -1;
    for (int i = 0; i < (int) impulse.size(); ++i)
        if (impulse[(std::size_t) i] > 5.96e-8)
            first = std::min (first, i), last = i;

    const auto theta = ogunUniform (m, 0.0f, 6.2831855f, 3); // fixed phase table (§1.2)
    const auto decayKnob = p[6] >= 10000 ? -1.0 : ((std::pow (21.0, std::clamp (p[6], 0, 10000) / 10000.0) - 1.0) * 997.5 + 50.0) / 1000.0;
    const auto releaseKnob = p[7] >= 10000 ? 20.0 : ((std::pow (21.0, std::clamp (p[7], 0, 10000) / 10000.0) - 1.0) * 997.5 + 50.0) / 1000.0;

    out.octave = std::max (-2, 7 - richness);
    const auto stride = 1 << (richness - 7 + out.octave);
    std::array<double, SynthSettings::spectrumSize> energy {}, strongest {}, weightedTau {}, targetEnergy {};
    std::vector<double> amp ((std::size_t) m, 0.0);
    for (int i = 0; i < m; ++i)
    {
        // ALT DECAY (REA_rodada26 §1.3): every partial moves from its impulse level towards Y = the same law with the
        // Decay EQ, X_k = Y + (A₀ − Y)·(1 − c)^k; it settles on Y instead of fading out
        if (altDecay && decayKnob >= 0.0)
            if (const auto index = (int) std::lround ((i + 1.0) / stride); index >= 1 && index <= SynthSettings::spectrumSize)
            {
                const auto y = 2.0 / std::sqrt ((double) m) * d[(std::size_t) i] * gain / rms * mapAt (decayEq, i);
                targetEnergy[(std::size_t) index - 1] += y * y;
                if (y > strongest[(std::size_t) index - 1] && (i < first || i > last))
                    out.phase[(std::size_t) index - 1] = (float) std::fmod (theta[(std::size_t) i] / (2.0 * pi) + 0.25, 1.0);
            }
        if (i < first || i > last)
            continue;
        const auto a = 2.0 / std::sqrt ((double) m) * d[(std::size_t) i] * gain / rms * mapAt (impulse, i);
        amp[(std::size_t) i] = a;
        const auto index = (int) std::lround ((i + 1.0) / stride);
        if (index < 1 || index > SynthSettings::spectrumSize)
            continue;
        const auto k = (std::size_t) index - 1;
        energy[k] += a * a;
        if (decayKnob >= 0.0) // τ_h = (t − 0.025)·B·E_dec + 0.025 to −40 dB (§1.7)
            weightedTau[k] += a * a * std::max (0.001, (decayKnob - 0.025) * b[(std::size_t) i] * mapAt (decayEq, i) + 0.025);
        if (a > strongest[k])
        {
            strongest[k] = a;
            out.phase[k] = (float) std::fmod (theta[(std::size_t) i] / (2.0 * pi) + 0.25, 1.0);
        }
    }
    for (std::size_t k = 0; k < energy.size(); ++k)
    {
        out.amplitude[k] = (float) (std::sqrt (energy[k]) * pi / 2.0); // physical level (round 23 convention)
        if (altDecay && decayKnob >= 0.0)
        {
            // the gap to Y falls 40 dB in max(0, t_dec − 0.025) s for every partial (per-frame c = 1 − 0.01^(1/T))
            out.target[k] = (float) (std::sqrt (targetEnergy[k]) * pi / 2.0);
            if (energy[k] > 0.0 || targetEnergy[k] > 0.0)
                out.decay[k] = (float) std::min (60.0, 1.5 * std::max (0.001, decayKnob - 0.025));
        }
        else if (energy[k] > 0.0 && decayKnob >= 0.0)
            out.decay[k] = (float) std::min (60.0, 1.5 * weightedTau[k] / energy[k]); // −40 dB → −60 dB
    }

    // time for the summed energy to fall 40 dB when partial h falls 40 dB in τ_h = (t − 0.025)·B·E + 0.025
    auto overall = [&] (double knob, const std::vector<double>& eq)
    {
        std::vector<std::pair<double, double>> parts;
        double total = 0.0;
        for (int i = 0; i < m; ++i)
            if (amp[(std::size_t) i] > 0.0)
            {
                const auto tau = std::max (0.001, (knob - 0.025) * b[(std::size_t) i] * mapAt (eq, i) + 0.025);
                parts.emplace_back (amp[(std::size_t) i] * amp[(std::size_t) i], tau);
                total += parts.back().first;
            }
        if (total <= 0.0)
            return knob;
        double lo = 0.0, hi = 60.0;
        for (int it = 0; it < 50; ++it)
        {
            const auto t = (lo + hi) / 2.0;
            double e = 0.0;
            for (const auto& [a2, tau] : parts)
                e += a2 * std::pow (10.0, -4.0 * t / tau);
            (e > total * 1e-4 ? lo : hi) = t;
        }
        return hi;
    };
    out.decaySeconds = decayKnob < 0.0 ? -1.0 : overall (decayKnob, decayEq);
    out.releaseSeconds = overall (releaseKnob, releaseEq);
    return true;
}
} // namespace

bool synthFromGms (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain)
{
    // "SSLF" + size + the .gmsynth preset
    if (state.size() < 8 || std::memcmp (state.data(), "SSLF", 4) != 0)
        return false;
    const auto size = (std::size_t) u32At (state.data(), 4);
    if (size + 8 > state.size() || size < 4)
        return false;
    const auto* d = state.data() + 8;

    const auto tag = u32At (d, 0);
    const bool syn4 = tag == 0x53796E34, syn3 = tag == 0x53796E33;
    if (! syn4 && ! syn3)
        return false;
    const std::size_t paramsAt = syn4 ? 0xE10 : 0xE04;
    if (paramsAt + 212 * 4 > size)
        return false;

    float x[212];
    std::memcpy (x, d + paramsAt, sizeof (x));
    const auto velocityToCutoff = x[199] == 1.0f; // destination stored as an integer (1 = cutoff)
    for (int i = 0; i < 212; ++i)
        if (! std::isfinite (x[i]) || x[i] < 0.0f || x[i] > 1.0f)
            x[i] = i < 197 ? std::clamp (std::isfinite (x[i]) ? x[i] : 0.0f, 0.0f, 1.0f) : 0.0f;

    std::string waves[3];
    for (int k = 0; k < 3; ++k)
    {
        const auto* slot = reinterpret_cast<const char*> (d + 4 + 512 * (std::size_t) k);
        waves[k].assign (slot, strnlen (slot, 512));
    }

    SynthSettings s;

    // tuning: global octave, channel Δ and semitones/cents of each oscillator (in float32, truncating)
    const auto octave = std::clamp (truncf32 (4.0f * x[44]) - 2, -2, 2);
    const auto channel = -truncf32 (24.0f - 48.0f * x[2]);
    double semis[3] {};
    for (int k = 0; k < 3; ++k)
    {
        bool up = false;
        s.osc[(std::size_t) k].wave = gmsWave (waves[k], up);
        semis[k] = 12.0 * octave + channel + truncf32 (72.0f * x[55 + 2 * k] - 36.0f) + truncf32 (100.0f * x[56 + 2 * k] - 50.0f) / 100.0 + (up ? 12.0 : 0.0);
    }

    // cascaded mix (§1.3)
    const double a = (double) x[48] * x[48], b = (double) x[49] * x[49], n = (double) x[45] * x[45];
    double level[3] = { (1 - a) * (1 - b) * (1 - n), a * (1 - b) * (1 - n), b * (1 - n) };
    for (int k = 0; k < 3; ++k)
    {
        auto& o = s.osc[(std::size_t) k];
        o.detune = 0.0f;
        setPitch (o, semis[k]);
        o.level = (float) std::clamp (level[k], 0.0, 1.0);
        o.pan = 0.0f;
    }

    // modulation osc2 -> osc1
    const auto modKind = std::clamp (truncf32 (2.0f * x[51]), 0, 2);
    const auto m = (double) x[50] * x[50];
    if (m > 0.0 && modKind == 2) // AM: GMS osc 2 becomes the modulator (LYNX osc 3)
    {
        auto osc3 = s.osc[2];
        auto modulator = s.osc[1];
        modulator.level = (float) std::min (1.0, 3.0 * m);
        s.am = true;
        s.osc[1] = osc3; // GMS osc 3 (if audible) takes the free slot
        s.osc[2] = modulator;
    }
    else if (m > 0.0 && modKind == 1 && level[2] < 0.05) // FM/PM: GMS osc 2 becomes the modulator (LYNX osc 3)
    {
        auto modulator = s.osc[1];
        modulator.level = 1.0f;
        modulator.pan = 0.0f;
        s.fm = (float) std::min (8.0, 3.75 * m); // osc 1 phase receives osc2·15·m quarter cycles
        s.osc[2] = modulator;
        if (level[1] < 0.05) // GMS osc 2 was only modulating
            s.osc[1].level = 0.0f;
    }
    else if (m > 0.0 && modKind == 0) // hard sync: a higher, richer osc1
    {
        s.osc[0].wave = Waveform::saw;
        setPitch (s.osc[0], semis[0] + 12.0 * std::log2 (1.0 + 3.0 * m));
    }

    // noise in an empty slot
    if (n > 0.05)
        for (auto& o : s.osc)
            if (o.level < 0.05f && ! ((s.am || s.fm > 0.0f) && &o == &s.osc[2]))
            {
                o = { Waveform::noise, 0, 0.0f, (float) std::min (1.0, n) };
                break;
            }

    // unison: copies of osc 1 in the empty slots
    const auto voices = 1 + truncf32 (15.0f * x[41]);
    if (voices >= 2)
    {
        const auto spread = std::max (3.0, 600.0 * std::pow (0.4 * x[42], 2.0));
        const auto compensation = 1.0 / (1.0 + 0.15 * (voices - 1));
        int sign = 1;
        for (std::size_t k = 1; k < 3; ++k)
            if (s.osc[k].level < 0.05f && ! ((s.am || s.fm > 0.0f) && k == 2))
            {
                s.osc[k] = s.osc[0];
                s.osc[k].detune = (float) std::clamp (s.osc[0].detune + sign * spread, -100.0, 100.0);
                s.osc[k].pan = (float) (sign * x[43]);
                sign = -sign;
            }
        for (std::size_t k = 0; k < 3; ++k)
            if (! ((s.am || s.fm > 0.0f) && k == 2))
                s.osc[k].level = (float) std::clamp (s.osc[k].level * compensation * 1.5, 0.0, 1.0);
    }

    // amplitude: the output is e², times are to −40 dB
    s.attack = (float) std::min (5.0, 1.265 * std::pow ((double) x[4], 3.0));
    const auto td = 50.0 * std::pow ((double) x[5], 4.0) + 0.001;
    const auto tr = 25.0 * std::pow (0.55 * x[7] + 0.1, 3.0) + 0.001;
    s.sustain = x[6] * x[6];
    s.decay = s.sustain >= 0.999f ? 0.0f : (float) std::min (20.0, 2.3 * td / 8.82);
    s.release = (float) std::clamp (2.3 * tr / 8.82, 0.003, 10.0);

    // ladder filter -> LYNX SVF
    auto u = (double) x[12];
    if (velocityToCutoff) // velocity -> cutoff (with a typical velocity of 0.8)
        u += (2.0 * x[198] - 1.0) * 0.8;
    u = std::clamp (u, 0.0, 0.999);
    const auto filterType = std::clamp (truncf32 (2.0f * x[15]), 0, 2);
    s.filterType = filterType == 0 ? 0 : filterType == 1 ? 1 : 2;
    s.cutoff = (float) gmsCutoffHz (u);
    s.resonance = std::min (0.95f, x[13]);
    auto octavesFrom = [&] (double du) { return std::log2 (gmsCutoffHz (u + du) / gmsCutoffHz (u)); };

    // AD modulation envelopes
    for (int k = 0; k < 2; ++k)
    {
        auto amount = (double) x[16 + k] * x[16 + k];
        if (x[18 + k] >= 0.5f)
            amount = -amount;
        if (std::abs (amount) < 1e-4)
            continue;
        const auto attack = std::min (10.0, 7.59 * std::pow ((double) x[20 + k], 5.0));
        const auto decay = std::min (20.0, 4.6 * (70.0 * std::pow (0.8 * x[22 + k] + 0.2, 5.0) + 0.001) / 8.82);
        const auto dest = std::clamp (truncf32 (3.0f * x[24 + k]), 0, 3);
        if (dest == 0 && s.filterEnv.amount == 0.0f)
        {
            if (amount < 0.0) // closing: the cutoff starts low and "opens" back up
                s.filterEnv = { (float) std::clamp (octavesFrom (amount), -10.0, 0.0), (float) attack, 0.0f, (float) decay, 0.0f, s.release };
            else
                s.filterEnv = { (float) std::clamp (octavesFrom (amount), 0.0, 10.0), (float) attack, 0.0f, (float) decay, 0.0f, s.release };
        }
        else if (dest == 2 && s.pitchEnv.amount == 0.0f)
            s.pitchEnv = { (float) (24.0 * amount), (float) attack, 0.0f, (float) decay, 0.0f, s.release };
    }

    // LFOs
    for (int k = 0; k < 2; ++k)
    {
        auto amount = (double) x[28 + k] * x[28 + k];
        if (x[30 + k] >= 0.5f)
            amount = -amount;
        if (std::abs (amount) < 1e-4)
            continue;

        ChannelLfo lfo;
        const auto shape = std::clamp (truncf32 (3.0f * x[32 + k]), 0, 3);
        lfo.shape = shape == 0 ? 0 : shape == 3 ? 2 : 1;
        const auto sync = std::clamp (truncf32 (2.0f * x[36 + k]), 0, 2);
        if (sync == 0)
            lfo.period = (float) (1.0 / (25.0 * std::pow (1.0 - x[34 + k], 2.0) + 0.01));
        else
        {
            static constexpr double beats[] = { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0 };
            lfo.period = (float) (beats[std::clamp (truncf32 (9.0f * x[34 + k]), 0, 8)] * (sync == 2 ? 4.0 : 1.0));
            lfo.sync = true;
        }
        lfo.global = x[26 + k] < 0.5f; // without retrig the phase runs free

        // an envelope targeting "LFO" becomes this LFO's fade-in
        if (std::clamp (truncf32 (3.0f * x[24 + k]), 0, 3) == 3 && x[16 + k] > 0.0f)
            lfo.attack = (float) std::min (10.0, 7.59 * std::pow ((double) x[20 + k], 5.0));
        if (lfo.sync && lfo.attack > 0.0f)
            lfo.attack = (float) (lfo.attack * std::max (1.0, bpm) / 60.0);

        switch (std::clamp (truncf32 (3.0f * x[38 + k]), 0, 3))
        {
            case 0: lfo.amount = (float) std::clamp (octavesFrom (std::abs (amount)) * (amount < 0 ? -1 : 1), -10.0, 10.0); s.lfo[lfoCutoff] = lfo; break;
            case 2: lfo.amount = (float) (12.0 * amount); s.lfo[lfoPitch] = lfo; break;
            case 3: lfo.amount = (float) std::abs (amount); s.lfo[lfoVolume] = lfo; break;
            default: break; // "modulation": no equivalent
        }
    }

    s.mono = x[40] > 0.0f;
    s.porta = x[11] > 0.0f;
    if (s.porta)
        s.portaBeats = (float) std::clamp (0.52 * (2.0 * std::pow ((double) x[10], 3.0)) * std::max (1.0, bpm) / 60.0, 0.01, 8.0);

    if (const auto pan = 2.0f * x[1] - 1.0f; std::abs (pan) > 0.01f)
        for (auto& o : s.osc)
            o.pan = std::clamp (o.pan + pan, -1.0f, 1.0f);

    gain = (float) std::clamp (std::pow (10.0, (80.0 * x[0] - 60.0) / 20.0) * std::pow (10.0, (50.0 * x[8] - 25.0) / 20.0), 0.0, 8.0);
    synth = s;
    return true;
}

// ---- Ogun / Autogun ----

// LYNX synth resonance 0..1 for a Q (the SVF uses k = 1/Q = 2 − 1.9·res; Q < 0.5 becomes 0)
float synthResonanceForQ (double q)
{
    return (float) std::clamp ((2.0 - 1.0 / std::max (q, 1e-3)) / 1.9, 0.0, 1.0);
}

bool synthFromOgun (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    if (state.size() < 8)
        return false;
    auto version = u32At (state.data(), 0);
    if (version > 0xffff)
        version >>= 16;
    if (version < 14 || version > 19)
        return false; // old versions store a reduced block (not used in the current demos)

    const auto body = inflateZlib (state.data() + 4, state.size() - 4, false, 8u << 20);
    if (! body || body->size() < 64 * 4)
        return false;
    std::int32_t p[64];
    std::memcpy (p, body->data(), sizeof (p));

    // timbre knob time: 10000 = infinite
    auto seconds = [] (int v) { return v >= 10000 ? -1.0 : ((std::pow (21.0, std::clamp (v, 0, 10000) / 10000.0) - 1.0) * 997.5 + 50.0) / 1000.0; };

    SynthSettings s;
    const auto fullness = std::clamp (p[8], -1000, 1000);
    const auto richness = std::clamp (p[52] - 5, 1, 11);
    const auto wave = fullness <= -800 && richness <= 3 ? Waveform::sine : fullness < -300 ? Waveform::triangle : Waveform::saw;
    const auto transpose = std::clamp (p[1], -24, 24) + std::clamp (p[2], -50, 50) / 100.0;

    // REA_rodada24 §1: Ogun is an inverse-FFT spectral synth; its initial spectrum goes into the "spectral"
    // oscillator, with each harmonic's own decay (spectrumDecay, 8 frames per note). Approximations: binned
    // partials share an energy-weighted decay, the release is one overall envelope, ALT DECAY's morph towards the
    // Decay EQ uses a target per harmonic (REA_rodada26 §1.3), articulators at their neutral values, Seed 2 "Random" = one fixed draw,
    // partials of dense spectra (richness > 9) binned onto the nearest harmonic of f/4.
    // Without readable maps the old waveform approximation is used.
    OgunSpectrum spectrum;
    const auto spectral = ogunSpectrum (p, *body, spectrum);
    if (spectral)
    {
        s.spectrum = spectrum.amplitude;
        s.spectrumPhase = spectrum.phase;
        s.spectrumDecay = spectrum.decay; // each harmonic decays at its own rate (the envelope then holds)
        s.spectrumTarget = spectrum.target;
        s.osc = { SynthOscillator { Waveform::spectral, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
        setPitch (s.osc[0], transpose);
        s.osc[0].octave = std::clamp (s.osc[0].octave + spectrum.octave, -3, 3);
    }
    else
    {
        s.osc = { SynthOscillator { wave, 0, 0.0f, 0.8f }, SynthOscillator {}, SynthOscillator {} };
        setPitch (s.osc[0], transpose);
        if (fullness > 300) // "metallic" timbre: a partial one octave up
        {
            s.osc[1] = { Waveform::sine, 0, 0.0f, 0.3f };
            setPitch (s.osc[1], transpose + 12.0);
        }
    }

    // spectral: the 40 dB fall of the whole spectrum → LYNX's 60 dB times (×1.5)
    // (with the per-harmonic decay in the spectrum, the volume envelope holds)
    const auto decay = spectral ? -1.0 : seconds (p[6]);
    const auto release = spectral ? 1.5 * spectrum.releaseSeconds : seconds (p[7]);
    s.attack = 0.0f;
    if (decay < 0.0)
    {
        s.decay = 0.0f;
        s.sustain = 1.0f;
    }
    else
    {
        s.decay = (float) std::min (30.0, decay);
        s.sustain = 0.0f;
    }
    s.release = (float) std::clamp (release < 0.0 ? 20.0 : release, 0.005, 30.0); // P7 = 10000 → 20 s (REA_rodada21 §2.2)

    if (const auto voices = std::clamp (p[12], 1, 9); voices >= 2)
    {
        const auto pan = std::clamp (p[13], 0, 128) / 128.0f;
        for (std::size_t k = 1; k < 3; ++k)
            if (s.osc[k].level == 0.0f)
            {
                s.osc[k] = s.osc[0];
                // detune ±5·(11^(P15/256) − 1) cents (REA_rodada21 §2.4)
                const auto spread = (float) (5.0 * (std::pow (11.0, std::clamp (p[15], 0, 256) / 256.0) - 1.0));
                s.osc[k].detune = std::clamp (s.osc[0].detune + (k == 1 ? spread : -spread), -100.0f, 100.0f);
                s.osc[k].pan = k == 1 ? pan : -pan;
            }
        for (auto& o : s.osc)
            o.level *= 0.6f;
    }

    // filter: only exists with order > 0
    s.cutoff = 20000.0f;
    if (const auto order = std::clamp (p[50], 0, 3); order > 0)
    {
        const auto type = std::clamp (p[49], 0, 3);
        const auto u = std::clamp (p[17], 0, 256) / 256.0;
        // REA_rodada21 §2.5: fc = 44100·(c + c₀) Hz (ω limited to 2.8495 rad); order = identical cascaded biquads
        // (LYNX has only one: approximate slope); LP/HP with Q^(1/order) per stage, BP/BS with width in octaves
        double c = 0.0, c0 = 0.0;
        if (type == 0)
            c = 0.0006646667 * (std::pow (1501.0, u) - 1.0), c0 = 0.003;
        else
            c = std::min (type == 3 ? 0.6001 : 1.0001, 0.00039993 * (std::pow (1501.0, u) - 1.0)), c0 = 0.0001;
        s.filterType = type == 0 ? 0 : type == 1 ? 1 : type == 2 ? 3 : 2;
        s.cutoff = (float) std::clamp (44100.0 * (c + c0), 20.0, std::min (20000.0, 2.8495 / (2.0 * 3.141592653589793) * 44100.0));
        auto r = std::clamp (p[18], 0, 256) / 256.0;
        if (p[51] != 0) // FLAT
            r *= (u + 1.0) / 2.0;
        double q = 0.707;
        if (type == 0 || type == 3)
            q = std::pow (r <= 0.0 ? 0.1 : r >= 1.0 ? 50.0 : 0.02495 * (std::pow (2001.0, r) - 1.0) + 0.1, 1.0 / order);
        else
        {
            const auto bw = r <= 0.0 ? 0.01 : r >= 1.0 ? 12.0 : 0.1199 * (std::pow (101.0, r) - 1.0) + 0.01;
            q = 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * bw));
        }
        s.resonance = synthResonanceForQ (q);
    }

    gain = (float) std::clamp ((std::pow (3.0, std::clamp (p[0], 0, 256) / 128.0) - 1.0) / 2.0, 0.0, 4.0); // (3^(P0/128) − 1)/2
    synth = s;
    return true;
}

// ---- Sakura ----

std::optional<std::vector<float>> sakuraParams (const std::vector<std::uint8_t>& state)
{
    // "SSLF" + size + "Skr1" + 146 floats, raw (index 111 is an integer mode; REA_rodada28 §1.1)
    if (state.size() < 12 + 146 * 4 || std::memcmp (state.data(), "SSLF", 4) != 0 || u32At (state.data(), 8) != 0x536B7231)
        return std::nullopt;
    std::vector<float> p (146);
    std::memcpy (p.data(), state.data() + 12, sizeof (float) * 146);
    for (std::size_t k = 0; k < 114; ++k) // floats 114…145 are not parameters
        if (! std::isfinite (p[k]))
            return std::nullopt;
    return p;
}

bool synthFromSakura (const std::vector<std::uint8_t>& state, SynthSettings& synth)
{
    if (state.size() < 12 + 146 * 4 || std::memcmp (state.data(), "SSLF", 4) != 0 || u32At (state.data(), 8) != 0x536B7231)
        return false;
    float t[146];
    std::memcpy (t, state.data() + 12, sizeof (t));
    for (auto& v : t)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;

    // Sakura's strings are physical (exciter + waveguide): here they become a saw (string 1) + triangle
    // (string 2) — the model swap is an approximation. Confirmed (REA_rodada18 §10): Amp A = 35x⁵ + 0.001 s,
    // D = 50x⁵ + 0.001 s and R = 10x⁵ + 0.1 s with poles of time constant T/8.82 (LYNX measures time down to −60 dB:
    // ×6.908/8.82), sustain x², Tune trunc(198x − 99)·0.01 semitone, linear String Mix (string 1 ∝ 1 − mix).
    // The string decay (feedback d^0.05 per period → T60 ≈ 60/(f·(−log10 d)) s) depends on the
    // note: the T60 of key 60 is used (approximation). Hi-Cut (REA_rodada19 §5): 4 first-order poles with
    // c = Cutoff² (no envelope); each pole cuts at −fs·ln(1 − c)/2π and the cascade at ≈ 0.435 of that, with
    // fs = 48 kHz (the engine's coefficient rate; not confirmed as the render rate). In FL it filters the
    // exciter; here, the synth output (2 poles).
    SynthSettings s;
    const auto mix = t[17];
    s.osc = { SynthOscillator { Waveform::saw, 0, 0.0f, 1.0f - mix }, SynthOscillator { Waveform::triangle, 0, 0.0f, mix }, SynthOscillator {} };
    const auto tune = std::clamp ((int) (198.0f * t[1] - 99.0f), -99, 99) * 0.01;
    setPitch (s.osc[0], tune);
    setPitch (s.osc[1], tune);
    auto p5 = [] (float x) { return std::pow ((double) x, 5.0); };
    constexpr double toLynx = 6.907755 / 8.82;
    s.attack = (float) (35.0 * p5 (t[2]) + 0.001);
    s.decay = (float) ((50.0 * p5 (t[3]) + 0.001) * toLynx);
    s.sustain = t[4] * t[4];
    s.release = (float) ((10.0 * p5 (t[5]) + 0.1) * toLynx);
    const auto d = std::clamp ((double) t[6], 0.0, 0.999999);
    const auto stringT60 = d <= 0.0 ? 0.01 : 60.0 / (261.63 * -std::log10 (d));
    if (stringT60 < 30.0 && (s.sustain > 0.0f || stringT60 < s.decay))
    {
        s.sustain = 0.0f;
        s.decay = (float) std::max (0.01, stringT60);
    }
    s.cutoff = 20000.0f;
    if (const auto c = (double) t[29] * t[29]; c < 0.999)
        s.cutoff = (float) std::clamp (0.435 * -48000.0 * std::log (1.0 - c) / (2.0 * 3.141592653589793), 20.0, 20000.0);
    synth = s;
    return true;
}

// ---- Fruity DrumSynth Live ----

std::optional<std::string> drumSynthLiveSound (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 9)
        return std::nullopt;
    const auto version = u32At (state.data(), 0), index = u32At (state.data(), 4);
    if (version < 3 || version > 8 || index >= 120)
        return std::nullopt;

    std::size_t pos = 8;
    std::string name;
    for (std::uint32_t i = 0; i <= index; ++i)
    {
        if (pos >= state.size())
            return std::nullopt;
        const auto len = (std::size_t) state[pos];
        if (pos + 1 + len > state.size())
            return std::nullopt;
        name.assign (reinterpret_cast<const char*> (state.data() + pos + 1), len);
        pos += 1 + len;
    }
    return name.empty() ? std::nullopt : std::optional<std::string> (name);
}

std::vector<std::string> drumSampleCandidates (const std::string& soundName)
{
    const auto name = lower (soundName);
    const bool is909 = name.find ("909") != std::string::npos;
    const std::string first = is909 ? "909" : "808", second = is909 ? "808" : "909";
    const std::string root = "%FLStudioData%\\Patches\\Packs\\Drums\\";
    auto has = [&] (const char* w) { return name.find (w) != std::string::npos; };

    std::vector<std::string> out;
    auto both = [&] (const std::string& folder, const std::string& rest909, const std::string& rest808) {
        for (const auto& kit : { first, second })
            out.push_back (root + folder + "\\" + kit + " " + (kit == "909" ? rest909 : rest808) + ".wav");
    };

    if (has ("kick"))
        both ("Kicks", "Kick", "Kick");
    else if (has ("snare"))
        both ("Snares", "Snare", "Snare");
    else if (has ("rim"))
        both ("Snares", "Rim", "Rim");
    else if (has ("clap"))
    {
        out.push_back (root + "Percussion\\909 Clap.wav");
        out.push_back ("%FLStudioData%\\Patches\\Packs\\Legacy\\Drums\\Dance\\Basic 808 Clap.wav");
    }
    else if (has ("hat o"))
        both ("Hats", "OH", "OH");
    else if (has ("hat"))
        both ("Hats", "CH 1", "CH");
    else if (has ("ride"))
        out.push_back (root + "Cymbals\\909 Ride.wav");
    else if (has ("crash"))
        both ("Cymbals", "Crash", "Crash");
    else if (has ("tom"))
        both ("Toms", "Tom", "Tom");
    else if (has ("cowbell"))
        out.push_back (root + "Percussion\\808 Cowbell.wav");
    else if (has ("clave"))
        both ("Percussion", "Clav", "Clav");
    else if (has ("bongo") || has ("conga"))
        out.push_back (root + "Percussion\\808 Conga.wav");
    else if (has ("maracas"))
        out.push_back (root + "Hats\\808 CH.wav");
    return out;
}
// ---- Kepler (REA_rodada13 §2) ----

bool synthFromKepler (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    // u32 0, u32 size, u32 0, u32 9 + "Kepler-1\0", u32 version, u32 n, n floats
    if (state.size() < 0x21 + 34 * 4 || u32At (state.data(), 12) != 9 || std::memcmp (state.data() + 16, "Kepler-1", 8) != 0)
        return false;
    const auto count = (int) u32At (state.data(), 0x1D);
    if (count < 34 || count > 64 || 0x21 + (std::size_t) count * 4 > state.size())
        return false;
    float k[64] {};
    std::memcpy (k, state.data() + 0x21, (std::size_t) count * 4);
    for (auto& v : k)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;

    // engine 11-point tables, interpolated at x·10 (REA_rodada17 §6)
    auto table = [] (const double (&t)[11], float x) {
        const auto pos = std::clamp ((double) x, 0.0, 1.0) * 10.0;
        const auto i = std::min (9, (int) pos);
        return t[i] + (t[i + 1] - t[i]) * (pos - i);
    };
    static constexpr double attackTable[11] = { 0.003, 0.004, 0.013, 0.05, 0.128, 0.243, 0.401, 0.7, 1.3, 2.403, 3.049 };
    static constexpr double decayTable[11] = { 0.0015, 0.0027, 0.043, 0.141, 0.328, 0.651, 1.144, 2.053, 3.956, 8.466, 10.124 };
    static constexpr double releaseTable[11] = { 0.001, 0.002, 0.02, 0.112, 0.321, 0.572, 1.063, 1.964, 3.38, 6.91, 10.967 };
    static constexpr double cutoffTable[11] = { 2.0, 2.4594, 3.3219, 4.3219, 5.6837, 6.9542, 8.543, 10.4717, 12.388, 15.1635, 15.6096 };
    static constexpr double envAmountTable[11] = { 0.0, 0.389, 1.681, 2.888, 4.017, 5.039, 6.069, 7.288, 8.475, 8.957, 8.972 };

    SynthSettings s;
    const auto octave = std::clamp ((int) std::lround (2.0 * k[32]) - 1, -1, 1); // −1, 0, +1
    const auto saw = k[14] >= 0.5f, pulse = k[13] >= 0.5f, sub = k[15] >= 0.5f;
    s.osc = { SynthOscillator { Waveform::saw, octave, 0.0f, saw ? 1.0f : 0.0f },
              SynthOscillator { Waveform::square, octave, 0.0f, pulse ? 1.0f : 0.0f },
              SynthOscillator {} };
    if (! saw && ! pulse)
        s.osc[0].level = 1.0f;
    if (k[17] > k[16] * (sub ? 1.0f : 0.0f) && k[17] > 0.05f) // noise louder than the sub
        s.osc[2] = { Waveform::noise, 0, 0.0f, k[17] };
    else if (sub)
        s.osc[2] = { Waveform::square, std::max (-3, octave - 1), 0.0f, std::max (0.05f, k[16]) };

    // Env1: attack reaches 1 at T_A (linear in LYNX); Kepler's decay/release are to −20 dB, LYNX's
    // to −60 dB (exponential): × 3
    s.attack = (float) table (attackTable, k[26]);
    s.decay = (float) (3.0 * table (decayTable, k[27]));
    s.sustain = k[28];
    s.release = (float) std::max (0.003, 3.0 * table (releaseTable, k[29]));
    if (k[25] >= 0.5f) // gate-driven VCA
    {
        s.attack = 0.002f;
        s.decay = 0.0f;
        s.sustain = 1.0f;
        s.release = 0.01f;
    }

    s.cutoff = (float) std::clamp (std::pow (2.0, table (cutoffTable, k[19])), 20.0, 20000.0);
    s.resonance = std::min (0.95f, k[20]);
    if (const auto octaves = table (envAmountTable, k[22]); octaves > 0.01)
    {
        const auto sign = k[21] >= 0.5f ? -1.0f : 1.0f; // polarity
        s.filterEnv = { sign * (float) octaves, s.attack, 0.0f, s.decay, s.sustain, s.release };
    }
    // LFO1 (REA_rodada17 §6, rodada18 §7, rodada19 §3): a single, nearly sinusoidal global LFO, value ±1;
    // rate 21.739·2^(−T(x)) Hz; vibrato ±A(k10) octaves and cutoff ±A_c(k23) octaves (peaks); delay = linear
    // ramp of D(k7) s. The ×10 "Range" only exists on the Kepler Exo panel (left off here). BPM Sync (k6 ≥ 0.5,
    // REA_rodada33 §3.4): period n/192 beats, n = trunc((2^(7(1 − x)) − 1)·48.370079 + 1.1) from the rate knob as
    // stored (what the engine uses right after loading, also for old states). Chorus (panel 30): mode trunc(3x + 0.5) = Off / I / II / I+II, FL's chain (REA_rodada26 §3).
    static constexpr double lfoRateTable[11] = { 7.4422, 5.8074, 4.3499, 3.6563, 3.1131, 2.6712, 2.0463, 1.3093, 0.8515, 0.2838, 0.0 };
    static constexpr double vibratoTable[11] = { 0, 0.00116, 0.00813, 0.01506, 0.02196, 0.02882, 0.05259, 0.07819, 0.12057, 0.17728, 0.21388 };
    static constexpr double lfoCutoffTable[11] = { 0, 0.0914, 0.38038, 0.662, 0.96846, 1.23732, 1.48249, 1.85735, 2.27554, 2.66441, 2.82 };
    static constexpr double lfoDelayTable[11] = { 0, 0.229, 0.447, 0.547, 0.833, 1.0, 2.074, 4.775, 5.612, 6.774, 6.706 };
    ChannelLfo lfo;
    s.chorus = std::clamp ((int) (3.0f * k[30] + 0.5f), 0, 3);
    lfo.period = (float) (1.0 / (21.739 * std::exp2 (-table (lfoRateTable, k[5]))));
    if (k[6] >= 0.5f)
    {
        const auto curve = std::exp2 ((1.0f - k[5]) * 7.0f) - 1.0f; // float32 steps, as the engine
        const auto n = (int) (curve * 48.370079f + 1.1f);
        lfo.sync = true;
        lfo.period = (float) std::max (1, n) / 192.0f;
        lfo.secondsTiming = true;
    }
    lfo.attack = (float) table (lfoDelayTable, k[7]);
    lfo.global = true;
    if (const auto oct = table (vibratoTable, k[10]); oct > 1e-4)
    {
        s.lfo[lfoPitch] = lfo;
        s.lfo[lfoPitch].amount = (float) (12.0 * oct);
    }
    if (const auto oct = table (lfoCutoffTable, k[23]); oct > 1e-3)
    {
        s.lfo[lfoCutoff] = lfo;
        s.lfo[lfoCutoff].amount = (float) oct;
    }

    // Main Volume: m(v) = 0.34968v − 0.32754v² + 1.39229v³ (v = 1 -> 0 dB)
    const auto v = (double) k[33];
    gain = (float) std::clamp (0.34968 * v - 0.32754 * v * v + 1.39229 * v * v * v, 0.0, 2.0);
    synth = s;
    return true;
}

// ---- FLEX (§1.1): only the plugin state (the packs are protected and are not opened) ----

std::optional<std::pair<std::string, std::string>> flexPresetName (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 0x411 + 1024 || u32At (state.data(), 0) != 1)
        return std::nullopt;
    auto text = [&] (std::size_t offset) {
        const auto* p = reinterpret_cast<const char*> (state.data() + offset);
        return std::string (p, strnlen (p, 1024));
    };
    auto pack = text (0x10), preset = text (0x411);
    if (preset.empty() && pack.empty())
        return std::nullopt;
    return std::make_pair (pack, preset);
}
// ---- BooBass (REA_rodada10 §5): bass with Bass/Mid/Treble ----

bool synthFromBooBass (const std::vector<std::uint8_t>& state, SynthSettings& synth)
{
    if (state.size() < 16 || u32At (state.data(), 0) != 1)
        return false;
    auto knob = [&] (std::size_t i) { return std::clamp ((int) u32At (state.data(), 4 + 4 * i), 0, 65535) / 65535.0; };
    const auto bass = knob (0), mid = knob (1), treble = knob (2);

    // REA_rodada18 §11: BooBass's sound is a sample (attack + loop) that is not in the project data;
    // here the source is a saw one octave down with a decay (approximation). The three knobs are the confirmed
    // 3-band EQ: below 0.1 the band is muted; Bass 10^((12x − 12)/20), Mid 10^((10x − 10)/20),
    // Treble 10^((12x − 9)/20). The 2nd stage (low-end saturation, ×3, limiter) is left out.
    auto band = [] (double x, double range, double top) { return x < 0.1 ? 0.0f : (float) std::pow (10.0, (range * x - range + top) / 20.0); };
    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::saw, -1, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    s.cutoff = 20000.0f;
    s.bandGain = { band (bass, 12.0, 0.0), band (mid, 10.0, 0.0), band (treble, 12.0, 3.0) };
    s.attack = 0.002f;
    s.decay = 0.6f;
    s.sustain = 0.5f;
    s.release = 0.08f;
    synth = s;
    return true;
}

// ---- Drumpad (§6): only the patch name; the physical model is not reproduced ----

std::optional<std::string> drumpadPatchName (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 0x40 || std::memcmp (state.data(), "SSLF", 4) != 0 || std::memcmp (state.data() + 8, "3PMD", 4) != 0)
        return std::nullopt;
    const auto* p = reinterpret_cast<const char*> (state.data() + 12);
    std::string name (p, strnlen (p, 0x30));
    if (name.empty())
        return std::nullopt;
    return name;
}

std::optional<std::array<float, 38>> drumpadParams (const std::vector<std::uint8_t>& state)
{
    // "SSLF" + size + "3PMD" + 128 B name + 38 floats (parameters 0…37 in order), REA_rodada18 §12
    if (state.size() < 140 + 38 * 4 || std::memcmp (state.data(), "SSLF", 4) != 0 || std::memcmp (state.data() + 8, "3PMD", 4) != 0)
        return std::nullopt;
    std::array<float, 38> p {};
    std::memcpy (p.data(), state.data() + 140, sizeof (float) * 38);
    for (auto& v : p)
        if (! std::isfinite (v))
            return std::nullopt;
    return p;
}

SynthSettings drumToneForName (const std::string& patchName)
{
    const auto name = lower (patchName);
    auto has = [&] (const char* w) { return name.find (w) != std::string::npos; };

    // "generic" drum: sine with a pitch drop + a little noise for the attack
    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator { Waveform::noise, 0, 0.0f, 0.12f }, SynthOscillator {} };
    s.attack = 0.0f;
    s.decay = 0.3f;
    s.sustain = 0.0f;
    s.release = 0.1f;
    s.cutoff = 6000.0f;
    s.pitchEnv = { 5.0f, 0.0f, 0.0f, 0.06f, 0.0f, 0.05f };

    if (has ("war") || has ("taiko") || has ("lo") || has ("floor"))
    {
        s.osc[0].octave = -1;
        s.decay = 0.6f;
    }
    if (has ("bongo") || has ("hi "))
    {
        s.osc[0].octave = 1;
        s.decay = 0.15f;
    }
    if (has ("conga"))
        s.decay = 0.25f;
    if (has ("wood") || has ("block") || has ("clave") || has ("rim"))
    {
        s.osc[0] = { Waveform::triangle, 1, 0.0f, 1.0f };
        s.osc[1].level = 0.05f;
        s.decay = 0.06f;
        s.pitchEnv.amount = 2.0f;
    }
    return s;
}
// ---- PoiZone, BassDrum, Toxic Biohazard (REA_rodada15 §12) ----

namespace
{
bool sslfFloats (const std::vector<std::uint8_t>& state, const char* tag, std::size_t offset, float* out, int count)
{
    if (state.size() < offset + (std::size_t) count * 4 || std::memcmp (state.data(), "SSLF", 4) != 0 || std::memcmp (state.data() + 8, tag, 4) != 0)
        return false;
    std::memcpy (out, state.data() + offset, (std::size_t) count * 4);
    for (int i = 0; i < count; ++i)
        out[i] = std::isfinite (out[i]) ? std::clamp (out[i], 0.0f, 1.0f) : 0.0f;
    return true;
}

double ms4 (double lo, double span, float x) { return (lo + span * std::pow ((double) x, 4.0)) / 1000.0; }
} // namespace

bool synthFromPoiZone (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    float p[74];
    if (! sslfFloats (state, "4reV", 12, p, 74) && ! sslfFloats (state, "3reV", 12, p, 74)) // "Ver3": same layout (REA_rodada44 §1.3)
        return false;

    SynthSettings s;
    const auto transpose = std::floor (48.0f * p[17]) - 24.0 + (200.0 * p[16] - 100.0) / 100.0;
    const auto waveA = p[0] > 0.0f ? Waveform::square : Waveform::saw;
    const auto waveB = p[4] > 0.0f ? Waveform::square : Waveform::saw;
    s.osc[0] = { waveA, 0, 0.0f, std::clamp (1.0f - p[8], 0.0f, 1.0f) };
    s.osc[1] = { waveB, 0, (float) (50.0 * p[7]), std::clamp (p[8], 0.0f, 1.0f) };
    s.osc[2] = { Waveform::noise, 0, 0.0f, p[9] * 0.5f };
    setPitch (s.osc[0], transpose);
    setPitch (s.osc[1], transpose + std::floor (48.0f * p[6]) - 24.0);
    s.osc[1].detune = (float) std::clamp (s.osc[1].detune + 50.0 * p[7], -100.0, 100.0);

    s.attack = (float) ms4 (1.0, 4999.0, p[22]);
    s.decay = (float) ms4 (1.0, 19999.0, p[23]);
    s.sustain = p[24];
    s.release = (float) ms4 (15.0, 9985.0, p[25]);
    // cutoff (REA_rodada17 §8): 2^(10·v + 6) Hz with v = 1.1·x clipped at 0.91 (64 Hz … 35 kHz)
    s.cutoff = (float) std::clamp (std::pow (2.0, 10.0 * std::min (0.91, 1.1 * (double) p[18]) + 6.0), 20.0, 20000.0);
    s.resonance = std::min (0.95f, p[19]);
    s.filterType = std::clamp ((int) (2.0f * p[66]), 0, 2) == 0 ? 0 : (int) (2.0f * p[66]) == 1 ? 1 : 2;
    // filter envelope on the cutoff: ±10·a² octaves, a = 2x − 1 (REA_rodada17 §8)
    if (const auto a = 2.0f * p[10] - 1.0f; (int) (4.0f * p[65]) == 0 && std::abs (a) > 0.01f)
        s.filterEnv = { (a < 0.0f ? -10.0f : 10.0f) * a * a, (float) ms4 (5.0, 49995.0, p[11]), 0.0f, (float) ms4 (5.0, 49995.0, p[12]), p[13],
                        (float) ms4 (5.0, 49995.0, p[14]) };
    if (std::abs (p[26] - 0.5f) > 0.01f && p[28] > 0.0f)
    {
        ChannelLfo lfo;
        lfo.period = (float) (1.0 / std::max (0.02, 30.0 * std::pow ((double) p[28], 5.0)));
        const auto shape = std::clamp ((int) (4.0f * p[68]), 0, 4);
        lfo.shape = shape == 1 ? 2 : shape == 2 ? 1 : 0;
        const auto amount = 2.0f * p[26] - 1.0f;
        switch (std::clamp ((int) (5.0f * p[67]), 0, 5))
        {
            case 0: lfo.amount = (amount < 0.0f ? -10.0f : 10.0f) * amount * amount; s.lfo[lfoCutoff] = lfo; break; // ±10·l² oct
            case 1: case 2: lfo.amount = std::floor (24.0f * amount); s.lfo[lfoPitch] = lfo; break;               // up to ±24 st
            default: break; // PW and pan: scales not confirmed
        }
    }
    s.mono = p[44] >= 0.5f;
    s.porta = p[46] > 0.0f;
    gain = (float) std::pow (10.0, (30.0 * p[15] - 20.0) / 20.0);
    synth = s;
    return true;
}

bool synthFromBassDrum (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    float p[23];
    if (! sslfFloats (state, "1kiK", 0x50C, p, 23) && ! sslfFloats (state, "2kiK", 0x50C, p, 23))
        return false;

    // sine at the "Base" frequency (2^(4x + 4) Hz). Confirmed (REA_rodada17 §7 + rodada18 §9): duration = linear
    // ramp of (x³ + 0.001) s, no release; Peak f = base·(1 + 100·peak³·s⁴), with s falling by
    // (1 − 0.0002/T_s) every sample (at 44.1 kHz), T_s = 20·slide³ + 0.001. The click and the sample layer
    // are left out (the oscillator table is nearly sinusoidal).
    const auto baseHz = std::pow (2.0, 4.0 * p[6] + 4.0);
    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    setPitch (s.osc[0], 12.0 * std::log2 (baseHz / 261.63));
    const auto duration = (float) (std::pow ((double) p[2], 3.0) + 0.001);
    s.attack = 0.0005f;
    s.decay = duration;   // LYNX's decay reaches −60 dB in the given time (the BassDrum ramp reaches zero)
    s.sustain = 0.0f;
    s.release = duration; // no release: releasing the note does not cut it
    s.cutoff = 20000.0f;
    s.sweep = (float) (100.0 * std::pow ((double) p[7], 3.0));
    const auto slide = 20.0 * std::pow ((double) p[8], 3.0) + 0.001;
    s.sweepTime = (float) (-1.0 / (44100.0 * std::log (1.0 - 0.0002 / slide))); // time constant of s
    gain = (float) std::pow (10.0, 40.0 * (p[0] - 0.5) / 20.0);
    synth = s;
    return true;
}

bool synthFromToxic (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    float p[301];
    if (! sslfFloats (state, "2iBT", 0x204C, p, 294))
        return false;
    // p300 (filter mode) is raw, outside the 0..1 range: read separately (REA_rodada19 §4.1)
    std::fill (&p[294], &p[301], 0.0f);
    if (state.size() >= 0x204C + 301 * 4)
        std::memcpy (&p[294], state.data() + 0x204C + 294 * 4, sizeof (float) * 7);
    const auto filterMode = std::isfinite (p[300]) ? std::clamp ((int) p[300], 0, 3) : 0;

    // Fallback when the exact engine (core/Toxic.cpp, REA_rodada45) can't play the state. Carrier = enabled operator
    // with the highest "Mix" (REA_rodada17 §9) + the enabled modulator with the largest matrix cell into it (phase
    // deviation = 2·cell·output cycles; REA_rodada45 §3.6 corrects round 18's direction: "Om Fm c", parameter
    // 109 + 6c + m, is operator m modulating operator c). Filter (REA_rodada19 §4): mode p300 (0 off, 1 LP, 2 BP,
    // 3 HP), cutoff 2^(9u + 5) Hz at note 60, key-track 0.09·KT octave per semitone, envelope ±9·EGAmt octaves
    // with EGAmt = sign(2x − 1)(2x − 1)². The filter is FL's 4-pole ladder (REA_rodada26 §2: table, resonance
    // scale R(u), input saturation), with the resonance p16 as is.
    int carrier = -1;
    for (int i = 0; i < 6; ++i)
        if (p[25 + i] >= 0.5f && (carrier < 0 || p[103 + i] > p[103 + carrier]))
            carrier = i;
    if (carrier < 0)
        carrier = 0;

    auto steps = [] (float x) { return (double) std::clamp ((int) (198.0f * x - 99.0f), -99, 99); }; // trunc(198x − 99)
    const auto semis = steps (p[31 + carrier]) + steps (p[37 + carrier]) * 0.01 + (double) (int) (48.0f * p[2] - 24.0f);
    auto seconds = [] (float x) { return (float) std::max (50.0 * std::pow ((double) x, 4.0), 0.00101); };

    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    setPitch (s.osc[0], semis);

    int modulator = -1;
    for (int j = 0; j < 6; ++j)
        if (j != carrier && p[25 + j] >= 0.5f && p[109 + 6 * carrier + j] > 0.005f
            && (modulator < 0 || p[109 + 6 * carrier + j] > p[109 + 6 * carrier + modulator]))
            modulator = j;
    if (modulator >= 0)
    {
        const auto cell = std::floor (100.0f * p[109 + 6 * carrier + modulator]) * 0.01f;
        s.osc[2] = SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f };
        setPitch (s.osc[2], steps (p[31 + modulator]) + steps (p[37 + modulator]) * 0.01 + (double) (int) (48.0f * p[2] - 24.0f));
        s.fm = 2.0f * cell * cell;
        // the modulator's envelope scales its output: decay down to sustain (the attack is left out)
        s.fmDecay = seconds (p[79 + modulator]);
        s.fmSustain = p[85 + modulator] * p[85 + modulator];
    }

    s.attack = seconds (p[3]);
    s.decay = seconds (p[4]);
    s.sustain = p[5] * p[5];
    s.release = 0.35f * seconds (p[6]) + 0.02f;
    s.cutoff = 20000.0f;
    if (filterMode != 0)
    {
        const auto u = (double) p[15], kt = 0.09 * 12.0 * p[17]; // octaves per octave
        s.filterType = filterMode == 1 ? 0 : filterMode == 2 ? 1 : 2;
        s.keyTrack = (float) kt;
        s.cutoff = (float) std::clamp (std::exp2 (9.0 * u + 5.0 - kt), 20.0, 20000.0); // LYNX measures at key 48
        s.ladder = true; // FL's 4-pole filter with its resonance scale (REA_rodada26 §2)
        s.resonance = std::clamp (p[16], 0.0f, 1.0f);
        const auto eg = 2.0 * p[23] - 1.0;
        const auto egAmount = 9.0 * (eg < 0.0 ? -eg * eg : eg * eg);
        if (std::abs (egAmount) > 0.01)
            // FL's own envelope law (REA_rodada33 §2): T = max(50x⁴, 0.00101) s, sustain x², exponential stages
            s.filterEnv = { (float) egAmount, seconds (p[19]), 0.0f, seconds (p[20]), p[21] * p[21], seconds (p[22]), 1 };
    }
    gain = 1.0f;
    synth = s;
    return true;
}
// ---- Morphine (REA_rodada16 §3): additive with up to 4 generators; each becomes an oscillator from the shape of
// the average spectrum (frames weighted by duration × gain). Envelope, level and tuning: the master block ----

namespace
{
std::uint32_t tagAt (const std::uint8_t* d, std::size_t o) { return u32At (d, o); }
constexpr std::uint32_t morTag (const char (&t)[5]) { return (std::uint32_t) t[3] << 24 | (std::uint32_t) t[2] << 16 | (std::uint32_t) t[1] << 8 | (std::uint32_t) t[0]; }

struct MorphineGenerator
{
    std::array<double, 128> energy {}; // average energy of each harmonic
    double weight = 0.0;
    float mix = 0.0f;
};

// MBP0: 3 vectors of 128 values in 16 groups of 8 (mask + one f32 per set bit); the 1st is the amplitude
bool readPartials (const std::vector<std::uint8_t>& d, std::size_t& o, std::size_t end, std::array<float, 128>& amplitude)
{
    if (o + 4 > end || tagAt (d.data(), o) != morTag ("0PBM"))
        return false;
    o += 4;
    for (int a = 0; a < 3; ++a)
        for (int g = 0; g < 16; ++g)
        {
            if (o >= end)
                return false;
            const auto mask = d[o++];
            for (int b = 0; b < 8; ++b)
                if ((mask >> b) & 1)
                {
                    if (o + 4 > end)
                        return false;
                    float v = 0.0f;
                    std::memcpy (&v, d.data() + o, 4);
                    o += 4;
                    if (a == 0)
                        amplitude[(std::size_t) (g * 8 + b)] = std::isfinite (v) ? v : 0.0f;
                }
        }
    return true;
}

bool readGenerator (const std::vector<std::uint8_t>& d, std::size_t o, std::size_t end, MorphineGenerator& gen)
{
    // MVC0 · i32 · MZH0 · i32 zones · { i32, f32 level, i32 n, MSP0 } · i32 n · 6 × i32 (the 1st is Mix, f32)
    if (o + 8 > end || tagAt (d.data(), o) != morTag ("0CVM"))
        return false;
    o += 8;
    if (o + 8 > end || tagAt (d.data(), o) != morTag ("0HZM"))
        return false;
    const auto zones = (int) u32At (d.data(), o + 4);
    o += 8;
    for (int z = 0; z < zones; ++z)
    {
        if (o + 12 > end)
            return false;
        const auto zoneEnd = o + 12 + (std::size_t) u32At (d.data(), o + 8); // includes the size field itself [DATA]
        o += 12;
        if (o + 8 > end || tagAt (d.data(), o) != morTag ("0PSM"))
            return false;
        const auto frames = (int) u32At (d.data(), o + 4);
        o += 8;
        for (int f = 0; f < frames; ++f)
        {
            if (o + 12 > end)
                return false;
            const auto duration = (double) (std::int32_t) u32At (d.data(), o);
            float frameGain = 0.0f;
            std::memcpy (&frameGain, d.data() + o + 4, 4);
            const auto next = o + 12 + (std::size_t) u32At (d.data(), o + 8);
            o += 12;
            std::array<float, 128> amplitude {};
            if (! readPartials (d, o, end, amplitude) || o != next)
                return false;
            const auto w = std::max (0.0, duration) * std::abs ((double) frameGain);
            for (std::size_t n = 0; n < 128; ++n)
                gen.energy[n] += w * (double) amplitude[n] * amplitude[n];
            gen.weight += w;
        }
        o = zoneEnd > o ? zoneEnd : o;
        if (o > end)
            return false;
    }
    if (o + 8 > end)
        return false;
    std::memcpy (&gen.mix, d.data() + o + 4, 4);
    gen.mix = std::isfinite (gen.mix) ? std::clamp (gen.mix, 0.0f, 1.0f) : 0.0f;
    return true;
}

// waveform from the spectrum: dominant fundamental -> sine; odd harmonics only -> square (slow falloff) or
// triangle (fast falloff, ~1/n²); everything else -> saw
[[maybe_unused]] Waveform waveFromSpectrum (const std::array<double, 128>& e)
{
    double total = 0.0, odd = 0.0, peak = 0.0;
    for (std::size_t n = 0; n < e.size(); ++n)
    {
        total += e[n];
        if (n % 2 == 0)
            odd += e[n];
        peak = std::max (peak, e[n]);
    }
    if (total <= 0.0)
        return Waveform::sine;
    int strong = 0;
    for (auto v : e)
        strong += std::sqrt (v / peak) > 0.05 ? 1 : 0;
    if (e[0] / total >= 0.7 && strong <= 6)
        return Waveform::sine;
    if (odd / total >= 0.8)
    {
        // log-log slope of the amplitudes of the odd harmonics present
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        int count = 0;
        for (std::size_t n = 0; n < e.size(); n += 2)
            if (e[n] > peak * 1e-6)
            {
                const auto x = std::log ((double) n + 1.0), y = 0.5 * std::log (e[n]);
                sx += x, sy += y, sxx += x * x, sxy += x * y, ++count;
            }
        const auto slope = count >= 2 ? (count * sxy - sx * sy) / std::max (1e-12, count * sxx - sx * sx) : -1.0;
        return slope >= -1.4 ? Waveform::square : Waveform::triangle;
    }
    return Waveform::saw;
}
} // namespace

bool synthFromMorphine (const std::vector<std::uint8_t>& d, SynthSettings& synth, float& gain)
{
    if (d.size() < 32 || std::memcmp (d.data(), "SSLF", 4) != 0 || tagAt (d.data(), 8) != morTag ("0roM") || tagAt (d.data(), 12) != morTag ("0RPM"))
        return false;

    std::size_t o = 16;
    std::array<MorphineGenerator, 4> gens {};
    for (auto& g : gens)
    {
        if (o + 4 > d.size())
            return false;
        const auto n = (std::size_t) u32At (d.data(), o);
        if (n > d.size() - o - 4 || ! readGenerator (d, o + 4, o + 4 + n, g))
            return false;
        o += 4 + n;
    }
    for (int block = 0; block < 2; ++block) // MML0 (morph line) and MMD0 (modulation envelopes)
    {
        if (o + 8 > d.size())
            return false;
        o += 4 + (std::size_t) u32At (d.data(), o);
    }
    if (o + 4 + 40 * 4 > d.size() || u32At (d.data(), o) != 0x5C4)
        return false;
    float a[40];
    std::memcpy (a, d.data() + o + 4, sizeof (a));
    for (auto& v : a)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;

    // the generators that make sound, by Mix
    std::vector<int> active;
    for (int g = 0; g < 4; ++g)
        if (gens[(std::size_t) g].weight > 0.0 && std::any_of (gens[(std::size_t) g].energy.begin(), gens[(std::size_t) g].energy.end(), [] (double v) { return v > 0.0; }))
            active.push_back (g);
    std::stable_sort (active.begin(), active.end(), [&] (int x, int y) { return gens[(std::size_t) x].mix > gens[(std::size_t) y].mix; });

    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    const auto transpose = (double) (int) ((a[5] - 0.5f) * 48.0f) + (double) (int) ((a[6] - 0.5f) * 200.0f) / 100.0;
    // average spectrum of the active generators (energy per harmonic × Mix²) in the "Harmonics" oscillator; phases
    // are not read (0). Scale: the strongest harmonic is 1 (Morphine's absolute level was not measured)
    std::array<double, 128> energy {};
    for (const auto g : active)
        for (std::size_t n = 0; n < energy.size(); ++n)
            energy[n] += gens[(std::size_t) g].energy[n] / std::max (1e-12, gens[(std::size_t) g].weight)
                         * (double) gens[(std::size_t) g].mix * gens[(std::size_t) g].mix;
    const auto strongest = *std::max_element (energy.begin(), energy.end());
    if (strongest > 0.0)
    {
        for (std::size_t n = 0; n < energy.size(); ++n)
            s.spectrum[n] = (float) std::sqrt (energy[n] / strongest);
        s.osc[0] = { Waveform::spectral, 0, 0.0f, 1.0f };
    }
    setPitch (s.osc[0], transpose);

    // master envelope (s): A 15·v^3.5, D 30·v³, S v², R 10·v³ (min. 3 ms)
    s.attack = (float) (15.0 * std::pow ((double) a[9], 3.5));
    s.decay = (float) (30.0 * std::pow ((double) a[10], 3.0));
    s.sustain = a[11] * a[11];
    s.release = (float) std::max (0.003, 10.0 * std::pow ((double) a[12], 3.0));
    s.cutoff = 20000.0f;
    gain = (float) std::pow (10.0, (48.0 * a[0] - 24.0) / 20.0);
    synth = s;
    return true;
}
} // namespace lynx
