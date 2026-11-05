#pragma once

// LuxeVerb's "Space" engine (round tp172b): an algorithmic room for the Pro-R role, next to the FL-exact LuxeVerb
// engine (core/LuxeVerb.cpp), which it leaves untouched. It is built from public literature only: a 16-line feedback
// delay network with a Hadamard matrix and per-line absorption filters set from the wanted decay time per band
// (Jot & Chaigne 1991; Jot 1992), input all-pass diffusion (Schroeder 1962; Dattorro 1997 for the plate), a tapped
// early-reflection stage (Moorer 1979) and delay-line modulation. Its controls are perceptual (decay time, decay rate,
// distance, brightness, character, thickness, width, pre-delay, decay-rate EQ), and their curves were matched
// black-box to the reference's measured impulse responses: RT60 per octave, pre-delay, early-reflection density,
// echo-density build-up, wet level, L/R correlation and the pitch spread of a sustained sine
// (/mnt/e/lynx-work/tp172b/irstats.py). Nothing captured from the reference is shipped.
//
// Header-only: LuxeVerb.cpp includes it. Allocation happens in prepare(); process() is real-time safe.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lynx::space
{
enum class Style { modern, vintage, plate };

struct Settings
{
    Style style = Style::modern;
    double decaySec = 2.5;    // mid-band RT60 at 100 % decay rate (the "Space")
    double rate = 1.0;        // decay-rate multiplier (0.25 … 4)
    double distance = 0.5;    // 0 near (strong, sparse early reflections) … 1 far (dense, soft early field)
    double brightness = 0.0;  // −1 … +1: tilt of the reverb's colour
    double character = 0.3;   // 0 … 1: modulation of the tail (0 = static, 1 = chorused)
    double thickness = 0.0;   // −1 … +1: level of the late body against the early field
    double width = 1.0;       // 0 mono … 1 full; the reverb is fully decorrelated from 0.7
    double predelayMs = 0.0;  // added to the style's own pre-delay
    double mix = 0.3;         // dry/wet
    // decay-rate EQ: decay multipliers below / above two corner frequencies (1 = no change)
    double lowMult = 1.0, lowHz = 200.0, highMult = 1.0, highHz = 4000.0;
    bool operator== (const Settings&) const = default;
};

namespace detail
{
constexpr int lines = 16;
constexpr double pi = 3.14159265358979323846;

// line lengths in ms at size 1 (mutually prime once in samples; spread 1 : 2.9)
constexpr std::array<double, lines> lineMs { 29.7, 33.1, 37.3, 41.1, 43.7, 47.9, 53.3, 57.1,
                                             61.7, 66.1, 69.9, 73.7, 77.9, 81.1, 84.7, 87.7 };

// early reflections: per style, tap times (ms) and gains; the stereo side reads mirrored taps
struct Tap { double ms, gain; };
constexpr std::array<Tap, 12> erModern { { { 1.65, 0.9 }, { 2.31, 0.8 }, { 4.31, 0.7 }, { 4.67, 0.6 }, { 5.83, 0.55 }, { 7.71, 0.5 },
                                           { 9.9, 0.45 }, { 12.7, 0.4 }, { 15.3, 0.35 }, { 19.1, 0.3 }, { 23.9, 0.25 }, { 29.3, 0.2 } } };
constexpr std::array<Tap, 12> erVintage { { { 0.6, 0.7 }, { 1.2, 0.65 }, { 1.7, 0.6 }, { 2.1, 0.55 }, { 2.7, 0.5 }, { 3.1, 0.48 },
                                            { 3.5, 0.45 }, { 4.0, 0.42 }, { 5.3, 0.38 }, { 6.9, 0.33 }, { 8.8, 0.28 }, { 11.2, 0.24 } } };
constexpr std::array<Tap, 12> erPlate { { { 0.02, 0.6 }, { 0.08, 0.55 }, { 1.4, 0.5 }, { 1.46, 0.48 }, { 1.6, 0.45 }, { 1.67, 0.42 },
                                          { 2.77, 0.4 }, { 2.83, 0.38 }, { 3.9, 0.34 }, { 4.6, 0.3 }, { 5.7, 0.26 }, { 6.8, 0.22 } } };

struct StyleDef
{
    double basePredelayMs;   // the style's own delay before its first reflection
    double sizeMin, sizeMax; // line-length scale at short / long decays
    int diffusers;           // input all-passes (density from the start)
    double diffuserGain;
    double lfHalf, hfSlope;  // decay ratio at low / high bands as the decay gets longer (see bandDecay)
    double modDepthMs, modHz;
    double erLevel, lateLevel;
    double rtScale;          // measured mid-band RT60 / the "Space" seconds
    double wetDb;            // wet level trim
    double toneDb, lpHz;     // the style's colour: a high shelf at 2.5 kHz and a gentle low pass (measured tail balance)
};
constexpr std::array<StyleDef, 3> styleDefs { {
    { 5.05, 0.55, 1.35, 2, 0.55, 0.25, 0.045, 0.12, 0.9, 0.55, 1.0, 1.07, 1.9, -7.0, 14000.0 }, // Modern: clean, slowly building
    { 13.1, 0.45, 1.10, 3, 0.60, 0.80, 0.085, 0.08, 0.7, 0.85, 1.0, 0.95, 3.4, -4.0, 9000.0 },  // Vintage: denser early field, dark long tails
    { 0.1, 0.35, 0.85, 4, 0.70, 0.95, 0.045, 0.02, 1.1, 0.35, 1.0, 1.12, -0.3, -3.0, 10000.0 },  // Plate: dense at once, long low end
} };

inline double clamp01 (double v) { return std::clamp (v, 0.0, 1.0); }

// first-order shelving section (bilinear), gain g at DC (low shelf) or at Nyquist (high shelf), corner fc
struct Shelf
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, z = 0.0;
    // low shelf H(s) = (s + g·w) / (s + w): gain g at DC, 1 at the top; bilinear with k = tan(π fc / sr)
    void setLow (double g, double fc, double sr) noexcept
    {
        const double k = std::tan (pi * std::min (fc, sr * 0.45) / sr);
        b0 = (1.0 + g * k) / (1.0 + k); b1 = (g * k - 1.0) / (1.0 + k); a1 = (k - 1.0) / (1.0 + k);
    }
    // high shelf H(s) = (g·s + w) / (s + w): gain 1 at DC, g at the top
    void setHigh (double g, double fc, double sr) noexcept
    {
        const double k = std::tan (pi * std::min (fc, sr * 0.45) / sr);
        b0 = (g + k) / (1.0 + k); b1 = (k - g) / (1.0 + k); a1 = (k - 1.0) / (1.0 + k);
    }
    double run (double x) noexcept
    {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
    void clear() noexcept { z = 0.0; }
};

struct Delay
{
    std::vector<float> buf;
    int pos = 0, mask = 0;
    void allocate (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((std::size_t) n, 0.0f);
        mask = n - 1;
        pos = 0;
    }
    void push (double x) noexcept { buf[(std::size_t) pos] = (float) x; pos = (pos + 1) & mask; }
    double read (double samplesBack) const noexcept // 1 = the last pushed sample
    {
        const double rp = (double) pos - samplesBack;
        const double fl = std::floor (rp);
        const int i = (int) fl;
        const double fr = rp - fl;
        // 4-point Hermite: a linear read would low-pass the tail a little on every pass round the loop
        const double xm = buf[(std::size_t) ((i - 1) & mask)], x0 = buf[(std::size_t) (i & mask)];
        const double x1 = buf[(std::size_t) ((i + 1) & mask)], x2 = buf[(std::size_t) ((i + 2) & mask)];
        const double c1 = 0.5 * (x1 - xm), c2 = xm - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);
        return ((c3 * fr + c2) * fr + c1) * fr + x0;
    }
    double tap (int samplesBack) const noexcept { return buf[(std::size_t) ((pos - samplesBack) & mask)]; }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }
};

struct Allpass
{
    Delay d;
    int len = 1;
    double g = 0.6;
    double run (double x) noexcept
    {
        const double v = d.tap (len);
        const double w = x + g * v;
        d.push (w);
        return v - g * w;
    }
};
} // namespace detail

// The decay time per band the network is set to (also what the editor's decay display draws).
struct BandDecay { double low, mid, high; };
inline BandDecay bandDecay (const Settings& s) noexcept
{
    const auto& st = detail::styleDefs[(std::size_t) s.style];
    // measured: the plate never rings shorter than ~0.7 s
    const double mid = std::clamp (s.decaySec * s.rate * st.rtScale, s.style == Style::plate ? 0.7 : 0.1, 60.0);
    // measured: longer decays grow a longer low end (plate most) and lose top (vintage most)
    const double lf = 1.0 + std::max (0.0, mid - 1.0) * st.lfHalf * 0.15;
    // a dark setting also shortens the top (measured: −100 % brightness takes 8 kHz from 2.1 to 1.65 s)
    const double dark = std::max (0.0, -std::clamp (s.brightness, -1.0, 0.0));
    const double hf = std::clamp ((1.0 - std::max (0.0, mid - 0.5) * st.hfSlope) * (1.0 - 0.25 * dark), 0.3, 1.0);
    return { mid * lf * s.lowMult, mid, mid * hf * s.highMult };
}

class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int maxLine = (int) std::ceil (detail::lineMs.back() * 0.001 * sr * 1.6) + 64;
        for (auto& l : line) l.allocate (maxLine);
        pre.allocate ((int) std::ceil (0.52 * sr) + 8);
        er.allocate ((int) std::ceil (0.045 * sr) + 8);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
            {
                diff[c][(std::size_t) k].d.allocate ((int) std::ceil (0.012 * sr) + 8);
                diff[c][(std::size_t) k].len = std::max (1, (int) std::lround (diffMs[(std::size_t) k] * (c == 0 ? 1.0 : 1.13) * 0.001 * sr));
            }
        dirty = true;
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : line) l.clear();
        pre.clear();
        er.clear();
        for (auto& c : diff)
            for (auto& a : c) a.d.clear();
        for (auto& f : lowShelf) f.clear();
        for (auto& f : highShelf) f.clear();
        for (auto& f : tone) f.clear();
        for (auto& f : toneLp) f.clear();
        phase = 0.0;
    }

    void set (const Settings& s) noexcept
    {
        if (! dirty && s == cur) return;
        cur = s;
        dirty = false;
        using namespace detail;
        const auto& st = styleDefs[(std::size_t) s.style];
        const auto bd = bandDecay (s);
        // line lengths grow with the decay (a bigger room), between the style's limits
        const double sz = st.sizeMin + (st.sizeMax - st.sizeMin) * clamp01 (std::log (std::max (bd.mid, 0.2) / 0.3) / std::log (8.0 / 0.3));
        for (int i = 0; i < lines; ++i)
        {
            const double len = lineMs[(std::size_t) i] * sz * 0.001 * sr;
            lenS[(std::size_t) i] = std::max (8.0, len);
            // Jot: per-line gain for −60 dB after T seconds: g = 10^(−3 · len / (sr · T)), per band via the shelves
            const auto gOf = [&] (double t) { return std::pow (10.0, -3.0 * len / (sr * std::max (0.05, t))); };
            const double gm = gOf (bd.mid), gl = gOf (bd.low), gh = gOf (bd.high);
            midGain[(std::size_t) i] = gm;
            lowShelf[(std::size_t) i].setLow (gl / gm, std::clamp (s.lowHz, 40.0, 2000.0), sr);
            highShelf[(std::size_t) i].setHigh (gh / gm, std::clamp (s.highHz, 500.0, sr * 0.45), sr);
        }
        modDepth = st.modDepthMs * (0.15 + 2.6 * clamp01 (s.character)) * 0.001 * sr;
        modInc = 2.0 * pi * st.modHz * (0.6 + 0.8 * clamp01 (s.character)) / sr;
        // past half distance the source moves into the room's own field: the style's pre-delay shrinks (to 15 %)
        const double farPre = s.distance > 0.5 ? 1.0 - 1.7 * (std::min (s.distance, 1.0) - 0.5) : 1.0;
        preS = std::clamp ((st.basePredelayMs * farPre + std::max (0.0, s.predelayMs)) * 0.001 * sr, 0.0, 0.5 * sr);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
                diff[c][(std::size_t) k].g = k < st.diffusers ? st.diffuserGain : 0.0;
        // distance: near = strong sparse early reflections, far = softer, more of them, closer together
        const double near = 1.0 - clamp01 (s.distance);
        erGain = st.erLevel * (0.6 + 4.4 * near * near * near) * (1.0 + 0.8 * std::max (0.0, s.thickness));
        erSpread = 0.55 + 0.9 * near;
        const double th = std::clamp (s.thickness, -1.0, 1.0);
        lateGain = st.lateLevel * std::pow (10.0, (th >= 0.0 ? 6.0 * th : 13.0 * th) / 20.0) * (1.15 - 0.4 * clamp01 (s.distance));
        // brightness: a high shelf at 2.5 kHz (±12 dB) and, when dark, a low pass coming down from 18 kHz
        const double b = std::clamp (s.brightness, -1.0, 1.0);
        for (auto& f : tone) f.setHigh (std::pow (10.0, (st.toneDb + 12.0 * b) / 20.0), 2500.0, sr);
        const double lpHz = std::min (st.lpHz * (b < 0.0 ? std::pow (0.08, -b) : std::pow (1.6, b)), sr * 0.45);
        const double x = std::exp (-2.0 * pi * lpHz / sr);
        for (auto& f : toneLp) { f.b0 = 1.0 - x; f.b1 = 0.0; f.a1 = -x; }
        side = std::min (1.0, clamp01 (s.width) / 0.7);
        // equal-power dry/wet (measured: at 30 % an impulse's dry part is −1 dB, not −3 dB)
        dryGain = std::cos (clamp01 (s.mix) * pi * 0.5);
        wetGain = std::sin (clamp01 (s.mix) * pi * 0.5);
        // level: measured, the reference's wet energy grows slowly with the decay (≈ mid^0.34 in energy)
        norm = 0.143 * std::pow (std::max (bd.mid, 0.1), -0.16) * std::pow (10.0, st.wetDb / 20.0);
    }

    void process (float* left, float* right, int n) noexcept
    {
        using namespace detail;
        const auto& erTaps = cur.style == Style::modern ? erModern : cur.style == Style::vintage ? erVintage : erPlate;
        for (int i = 0; i < n; ++i)
        {
            const double inL = left[i], inR = right[i];
            pre.push (0.5 * (inL + inR));
            const double x = preS >= 1.0 ? pre.read (preS) : 0.5 * (inL + inR);
            er.push (x);
            // early reflections (two mirrored tap sets)
            double eL = 0.0, eR = 0.0;
            for (std::size_t k = 0; k < erTaps.size(); ++k)
            {
                const int d = std::max (1, (int) std::lround (erTaps[k].ms * erSpread * 0.001 * sr));
                const double v = er.tap (d) * erTaps[k].gain;
                if (k & 1) { eL += v; eR -= 0.6 * v; } else { eR += v; eL -= 0.6 * v; }
            }
            // input diffusion
            double dl = x, dr = x;
            for (int k = 0; k < 4; ++k)
            {
                if (diff[0][(std::size_t) k].g == 0.0) break;
                dl = diff[0][(std::size_t) k].run (dl);
                dr = diff[1][(std::size_t) k].run (dr);
            }
            // the network: read the lines (modulated), absorb, mix with a Hadamard matrix, feed back with the input
            phase += modInc;
            if (phase > 2.0 * pi) phase -= 2.0 * pi;
            std::array<double, lines> v {};
            for (int j = 0; j < lines; ++j)
            {
                const double m = modDepth * std::sin (phase + 0.39 * j);
                const double r = line[(std::size_t) j].read (lenS[(std::size_t) j] + m + modDepth + 1.0);
                v[(std::size_t) j] = highShelf[(std::size_t) j].run (lowShelf[(std::size_t) j].run (r * midGain[(std::size_t) j]));
            }
            double oL = 0.0, oR = 0.0;
            for (int j = 0; j < lines; ++j)
            {
                oL += (j & 1 ? -v[(std::size_t) j] : v[(std::size_t) j]);
                oR += ((j >> 1) & 1 ? -v[(std::size_t) j] : v[(std::size_t) j]) * (j & 4 ? -1.0 : 1.0);
            }
            hadamard (v);
            for (int j = 0; j < lines; ++j)
                line[(std::size_t) j].push (v[(std::size_t) j] + (j & 1 ? dr : dl) * (j < 8 ? 1.0 : -1.0));
            double wL = (oL * lateGain + eL * erGain) * norm, wR = (oR * lateGain + eR * erGain) * norm;
            wL = toneLp[0].run (tone[0].run (wL));
            wR = toneLp[1].run (tone[1].run (wR));
            const double mid = 0.5 * (wL + wR), sd = 0.5 * (wL - wR) * side;
            left[i] = (float) (inL * dryGain + (mid + sd) * wetGain);
            right[i] = (float) (inR * dryGain + (mid - sd) * wetGain);
        }
    }

private:
    static void hadamard (std::array<double, detail::lines>& v) noexcept
    {
        for (int h = 1; h < detail::lines; h <<= 1)
            for (int i = 0; i < detail::lines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const double a = v[(std::size_t) j], b = v[(std::size_t) (j + h)];
                    v[(std::size_t) j] = a + b;
                    v[(std::size_t) (j + h)] = a - b;
                }
        for (auto& x : v) x *= 0.25; // 1/sqrt(16): orthogonal (lossless) mixing
    }

    static constexpr std::array<double, 4> diffMs { 4.77, 3.59, 2.73, 1.91 };
    double sr = 48000.0;
    Settings cur;
    bool dirty = true;
    std::array<detail::Delay, detail::lines> line;
    std::array<double, detail::lines> lenS {}, midGain {};
    std::array<detail::Shelf, detail::lines> lowShelf, highShelf;
    std::array<std::array<detail::Allpass, 4>, 2> diff;
    std::array<detail::Shelf, 2> tone, toneLp;
    detail::Delay pre, er;
    double preS = 0.0, modDepth = 0.0, modInc = 0.0, phase = 0.0, erGain = 1.0, erSpread = 1.0, lateGain = 1.0;
    double side = 1.0, dryGain = 0.89, wetGain = 0.45, norm = 0.3;
};
} // namespace lynx::space
