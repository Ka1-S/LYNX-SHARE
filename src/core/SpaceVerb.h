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

namespace lynx::spaceverb
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
    // round 190it: ducking, 0 … 1 (the reference's 0 … 48 dB): the wet part dips while the input plays. Measured on
    // refcap030's Pro-R 2 captures (a −6 dB RMS noise burst, Ducking 0 / 25 / 50 / 75 / 100 %): the tail sits 8.5 / 17 /
    // 25.5 / 34 dB down by the burst's end (0.71 × the setting in dB), down within the first 50 ms, and comes back over
    // ~0.3 s once the input stops (−34 → −25 → −16 → −11 → −7 dB in 50 ms steps: τ ≈ 115 ms in dB)
    double ducking = 0.0;
    // round 190kw: the engine's revision (0 = tp172b / 190it, bit for bit; 1 = the Pro-R 2 laws recovered by static
    // analysis: true stereo, the width matrix and rotation, the line-length / level / brightness laws; see Engine::set1)
    // and, for revision 1, the raw knobs it needs: Space 0 … 1, Stereo Width 0 … 1.2
    int revision = 0;
    double spaceKnob = 0.5, widthRaw = 0.7;
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

// round 190lp (revision 1): the fixed early taps measured on refcap030's Pro-R 2 impulse captures (black-box: the onset
// of each single-sample arrival in ms from the input, its polarity, and its level in dB at Space 0 … Distance 0 … rate 1
// as fitted by the tap law in Engine::setTaps). Their times hold at 44.1 / 48 / 96 kHz and at every Space, Decay Rate
// and Distance up to ½.
struct ETap { double ms; int sign; double db; };
constexpr std::array<ETap, 43> tapsL { {
    { 6.6667, +1, -7.42 }, { 8.9583, +1, -21.57 }, { 11.3125, -1, -6.84 }, { 14.3542, -1, -30.47 },
    { 14.6042, -1, -33.94 }, { 18.1250, +1, -13.03 }, { 18.3333, -1, -4.82 }, { 20.6875, +1, -21.99 },
    { 20.8958, +1, -22.71 }, { 22.8125, -1, -25.89 }, { 23.6042, +1, -23.57 }, { 25.4167, -1, -49.54 },
    { 28.4375, +1, -9.79 }, { 28.5000, -1, -20.33 }, { 32.2500, -1, -25.04 }, { 34.3542, +1, -16.18 },
    { 36.5208, -1, -24.59 }, { 38.3542, -1, -19.64 }, { 39.8333, -1, -25.38 }, { 40.7500, +1, -28.31 },
    { 42.8542, +1, -21.84 }, { 43.8333, -1, -20.01 }, { 46.1667, +1, -21.26 }, { 46.5625, -1, -18.96 },
    { 48.2083, +1, -20.26 }, { 49.0833, -1, -28.68 }, { 50.9583, -1, -26.50 }, { 55.4792, -1, -19.45 },
    { 61.8750, +1, -28.77 }, { 64.8750, +1, -20.17 }, { 66.7292, +1, -22.45 }, { 68.8958, -1, -21.91 },
    { 70.4167, -1, -30.99 }, { 72.3542, +1, -20.53 }, { 74.2292, +1, -24.31 }, { 78.8125, -1, -18.96 },
    { 82.8958, -1, -21.41 }, { 83.7708, +1, -21.14 }, { 87.5417, +1, -19.92 }, { 89.5625, +1, -20.57 },
    { 94.5625, +1, -22.86 }, { 96.2708, +1, -22.57 }, { 102.9792, +1, -22.00 } } };
constexpr std::array<ETap, 36> tapsR { {
    { 8.2917, +1, -6.74 }, { 10.9583, -1, -29.36 }, { 12.4792, +1, -8.33 }, { 15.8958, +1, -22.94 },
    { 16.1458, +1, -26.47 }, { 18.7917, +1, -19.74 }, { 21.0417, -1, -10.18 }, { 21.1250, -1, -10.19 },
    { 24.8125, -1, -18.48 }, { 25.6042, +1, -21.99 }, { 30.6875, +1, -21.56 }, { 32.3958, -1, -7.87 },
    { 32.8542, -1, -25.07 }, { 36.4583, +1, -17.50 }, { 36.5625, +1, -16.46 }, { 39.3750, +1, -26.13 },
    { 39.8958, -1, -29.72 }, { 44.3958, +1, -29.60 }, { 47.7083, +1, -19.12 }, { 55.3333, +1, -18.88 },
    { 56.6250, +1, -20.94 }, { 57.7917, +1, -22.99 }, { 58.9167, +1, -21.96 }, { 62.5625, -1, -24.64 },
    { 67.6250, -1, -19.09 }, { 68.9792, +1, -21.98 }, { 72.3125, -1, -21.32 }, { 75.6250, +1, -22.49 },
    { 82.3958, -1, -26.66 }, { 84.4375, +1, -22.66 }, { 89.1042, -1, -23.82 }, { 89.6667, -1, -23.27 },
    { 95.5000, +1, -22.36 }, { 95.8333, -1, -22.55 }, { 97.9792, +1, -30.23 }, { 104.8958, -1, -22.83 } } };

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

// round 190kw: an RBJ biquad (Bristow-Johnson's cookbook), one channel, transposed direct form II
struct Bq
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
    enum class Kind { lowPass, lowShelf, highShelf, peak };
    void set (Kind k, double fc, double q, double db, double sr) noexcept
    {
        const double w = 2.0 * pi * std::clamp (fc, 10.0, sr * 0.49) / sr, cw = std::cos (w), sw = std::sin (w);
        const double al = sw / (2.0 * std::max (q, 0.05)), A = std::pow (10.0, db / 40.0), sa = 2.0 * std::sqrt (A) * al;
        double c0, c1, c2, d0, d1, d2;
        if (k == Kind::lowPass) { c0 = c2 = (1.0 - cw) / 2.0; c1 = 1.0 - cw; d0 = 1.0 + al; d1 = -2.0 * cw; d2 = 1.0 - al; }
        else if (k == Kind::peak) { c0 = 1.0 + al * A; c1 = d1 = -2.0 * cw; c2 = 1.0 - al * A; d0 = 1.0 + al / A; d2 = 1.0 - al / A; }
        else if (k == Kind::lowShelf)
        {
            c0 = A * ((A + 1) - (A - 1) * cw + sa); c1 = 2 * A * ((A - 1) - (A + 1) * cw); c2 = A * ((A + 1) - (A - 1) * cw - sa);
            d0 = (A + 1) + (A - 1) * cw + sa; d1 = -2 * ((A - 1) + (A + 1) * cw); d2 = (A + 1) + (A - 1) * cw - sa;
        }
        else
        {
            c0 = A * ((A + 1) + (A - 1) * cw + sa); c1 = -2 * A * ((A - 1) + (A + 1) * cw); c2 = A * ((A + 1) + (A - 1) * cw - sa);
            d0 = (A + 1) - (A - 1) * cw + sa; d1 = 2 * ((A - 1) - (A + 1) * cw); d2 = (A + 1) - (A - 1) * cw - sa;
        }
        b0 = c0 / d0; b1 = c1 / d0; b2 = c2 / d0; a1 = d1 / d0; a2 = d2 / d0;
    }
    double run (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void clear() noexcept { z1 = z2 = 0.0; }
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
        // round 190kw (revision 1): two 8-line networks (the longest line 5.18 ms × 1.12402^40 ≈ 560 ms), two 2-line
        // networks, a pre-delay and an early-reflection line per channel
        const int maxLine1 = (int) std::ceil (0.6 * sr) + 64;
        for (auto& c : net1)
            for (auto& l : c) l.allocate (maxLine1);
        for (auto& c : two1)
            for (auto& l : c) l.allocate ((int) std::ceil (0.08 * sr) + 64);
        for (auto& d : pre1) d.allocate ((int) std::ceil (0.52 * sr) + 8);
        for (auto& c : early1)
            for (auto& l : c) l.allocate ((int) std::ceil (0.2 * sr) + 8);
        for (auto& d : er1) d.allocate ((int) std::ceil (0.65 * sr) + 8); // round 190lp: the taps (≤ 105 ms) + pre-delay (≤ 500 ms)
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
        duckEnv = duckDb = 0.0;
        thEnv = 0.0;
        for (auto& c : net1)
            for (auto& l : c) l.clear();
        for (auto& c : two1)
            for (auto& l : c) l.clear();
        for (auto& d : pre1) d.clear();
        for (auto& d : er1) d.clear();
        for (auto& c : early1)
            for (auto& l : c) l.clear();
        for (auto& c : abs1)
            for (auto& f : c) f.clear();
        for (auto& c : out1)
            for (auto& f : c) f.clear();
        for (auto& c : tap1)
            for (auto& f : c) f.clear();
    }

    void set (const Settings& s) noexcept
    {
        if (! dirty && s == cur) return;
        cur = s;
        dirty = false;
        if (s.revision >= 1)
        {
            set1 (s);
            return;
        }
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
        if (cur.revision >= 1)
        {
            process1 (left, right, n);
            return;
        }
        using namespace detail;
        const auto& erTaps = cur.style == Style::modern ? erModern : cur.style == Style::vintage ? erVintage : erPlate;
        const bool duck = cur.ducking > 0.0;
        const double duckDepthDb = 0.71 * 48.0 * clamp01 (cur.ducking);
        const double envA = std::exp (-1.0 / (0.001 * sr)), envR = std::exp (-1.0 / (0.005 * sr));
        const double attA = std::exp (-1.0 / (0.01 * sr)), attR = std::exp (-1.0 / (0.115 * sr));
        for (int i = 0; i < n; ++i)
        {
            const double inL = left[i], inR = right[i];
            double duckGain = 1.0;
            if (duck)
            {
                // the input's envelope (peak, 1 / 5 ms) keys the dip in full from −40 dBFS up (fading in from −50)
                const double a = std::max (std::abs (inL), std::abs (inR));
                duckEnv = a > duckEnv ? a + envA * (duckEnv - a) : a + envR * (duckEnv - a);
                const double key = clamp01 ((20.0 * std::log10 (duckEnv + 1e-12) + 50.0) / 10.0);
                const double target = duckDepthDb * key;
                duckDb = target > duckDb ? target + attA * (duckDb - target) : target + attR * (duckDb - target);
                duckGain = std::pow (10.0, -duckDb / 20.0);
            }
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
            const double mid = 0.5 * (wL + wR) * duckGain, sd = 0.5 * (wL - wR) * side * duckGain;
            left[i] = (float) (inL * dryGain + (mid + sd) * wetGain);
            right[i] = (float) (inR * dryGain + (mid - sd) * wetGain);
        }
    }

    // round 190kw, revision 1: the constants fitted on refcap030's Pro-R 2 captures (the laws themselves are below)
    struct Fit1
    {
        // round 190lg: refitted on refcap030's Pro-R 2 captures per time window (0 … 5 ms … 6.4 s) and per band, not on
        // the total level and spectrum alone (190kw's fit let the early network carry the tail: its level then followed
        // the displayed time instead of the decay rate, so long rates died early and Ducking left no tail)
        // round 190lp: + the fixed early taps (below); the early and 2-line networks only where the taps fade out (Distance
        // ½ … ¾ on: the reference's near early field is the taps alone, its far one dense), the levels refitted with them
        double late = 0.3376;    // the late network's level against the reference (G_late × this × the surface below)
        double lateExp = -0.944; // … × (network decay in s)^this …
        // … × 10^(dB / 20) of a fitted surface over Space S, decay rate r, Distance D and Character c:
        // lateS (S − ½) + lateS2 (S − ½)² + lateSR (S − ½) log2 r + lateR log2 r + lateD (D − ½) + lateD2 (D − ½)² + lateCh (c − 0.3)
        double lateS = 28.81, lateS2 = 1.57, lateSR = 1.71, lateR = 5.16, lateD = 3.02, lateD2 = -8.51, lateCh = -1.59;
        double two = 0.14579;    // the 2-line network's level, + twoS (S − ½) + twoD (D − ½) + twoD2 (D − ½)² dB
        double twoS = 6.7, twoD = -2.3, twoD2 = -1.5;
        double early = 0.08906;  // the early network's level (parallel lines, no feedback matrix), + earlyS / D / D2 terms (dB)
        double earlyS = -6.88, earlyD = -9.87, earlyD2 = -6.14;
        double earlyDecay = 0.3; // … and its decay: this × 1.15 × the displayed time (a short build-up, not a second tail)
        double er = 0.2367;      // the early reflections' level, + erS (S − ½) + erD (D − ½) + erD2 (D − ½)² dB
        double erS = -14.04, erD = 0.59, erD2 = -4.57;
        // round 190lp: the fixed early taps (detail::tapsL / tapsR) replace tp172b's twelve. Level of each tap (dB) =
        // its own level + tapS S + tapR log2 r + tapD D + tapT (t / T) (T the network's decay time), plus for the taps that
        // stand out at some Spaces their bumps over k (tapBoosts), tapMainD D + their own slope d D, and tapMainCh from
        // Character ¼ to ½ (from Distance ¼ on); all fade out between Distance tapFar0 and tapFar1 (the reference has none
        // of them from ¾ on). Fitted on the impulse captures' tap peaks (4 413 readings, robust sd 0.6 dB)
        double tapS = -2.823, tapR = -0.536, tapD = -5.865, tapT = -3.163, tapMainD = -6.81, tapMainCh = 2.24;
        double tapFar0 = 0.5, tapFar1 = 0.75;
        double tapTrim = 0.0;    // dB on every tap (the refit against the whole files' level)
        double tapHiCh0 = 0.55;  // Character from which the later standing-out taps lose their lift (over 0.2)
        bool tapUnit = false;    // lab only: every tap at 0 dB (calibration)
        // the whole wet by Distance (dB at D = 0, ¼, ½, ¾, 1, linear between) + distCh (c − ½) min (2 D, 1): far spaces lose
        // their early energy in the reference (−30 dB at 0 … 5 ms from D = 0 to 1) and keep their tail
        std::array<double, 5> distDb { -0.2, -1.17, 0.26, 0.36, 1.0 }; // round 190lz: ¾ 0.06 → 0.36 (Distance 0.8 impulses −1.7 dB)
        double distCh = 1.3;
        // Thickness (the reference's Thickner): a gain on the body (late, 2-line, early networks; the early reflections too
        // below 0) that acts only on a quiet wet: in full below thickLo dBFS, none above thickHi (the wet's (l² + r²) / 2,
        // thickAtt / thickRel s). Measured: an impulse's whole response moves by −13.2 dB per unit below 0 and by
        // 6.8 (1 − (1 − t)²) dB above (here − 1.12 dB at t = 1), a music wet at −25 dBFS by under 1.5 dB either way
        double thickNegDb = 13.2, thickPosDb = 5.68, thickLo = -38.0, thickHi = -28.0, thickAtt = 0.02, thickRel = 0.3;
        double cross = 0.5;      // the unresolved factor x of the CrossInput gain 0.5477 · x · min (v / 0.5477, 1)²: the
                                 // left-only impulses across Stereo Width score 2.92 / 3.32 / 3.40 / 3.20 at x = 0 / 0.5 / 1 /
                                 // 1.5, but x = 1 lifts a centred input by 1.7 dB against the fitted levels (grid 3.33 → 2.90)
        double toneDb = -3.0;    // the 2.5 kHz shelf under the brightness filters
        double lpOctaves = -0.75; // the brightness low pass's corner moved by this many octaves (fitted, one pole)
        double shelfDb = 0.0;    // the high shelf that follows the darkness …
        double darkDb = 18.0;    // … and its cut per unit of darkness (fitted on the measured B = 0 / 1 curves)
        // round 190lz: the taps' own tone, a bell + a 4 kHz shelf on the tap sum. Measured per time window on the impulse
        // captures (third octaves): the 0 … 40 ms window (the taps) carried +3 … +4 dB from 125 Hz to 2 kHz and −1 dB at
        // 4 … 8 kHz against the reference's single-sample arrivals, more the nearer the Distance, at decay rates ≤ × 1 only
        // (no excess at × 2 … × 4): music at Space ≤ ¼ played 1.5 … 2 dB loud while the impulses' broadband level matched.
        // In full up to decay rate eqR0, none from eqR1 on (smoothstep in log2 rate)
        double tapEqDb = -3.0, tapEqHz = 700.0, tapEqQ = 0.5, tapEqHiDb = 1.5, tapEqTrim = 0.0;
        double eqR0 = 1.0, eqR1 = 4.0;
        // round 190lz: the body (late, 2-line, early networks) by Space, dB at S = 0, ¼, ½, ¾, 1 (linear between): the
        // impulse captures' broadband level at Space ½ sat 1.2 dB under the reference at every rate (median of 21 files)
        std::array<double, 5> spaceDb { 0.0, 0.0, 1.3, 0.0, 0.0 };
        std::array<double, 3> styleLate { 1.0, 0.95, 0.6 }, styleEarly { 1.0, 1.145, 1.145 };
        std::array<double, 3> styleLowDb { 0.0, -2.0, -1.0 }, styleHighDb { 0.0, 0.0, -3.0 }, styleLpHz { 0.0, 0.0, 9000.0 };
    };
    static Fit1& fit1() noexcept { static Fit1 f; return f; }

private:
    // ------------------------------------------------------------------------------------------- round 190kw: revision 1
    // the 16 lines of the law split into two 8-line halves, one per channel (the even steps left, the odd ones right): the
    // same spread of lengths at the CPU of revision 0's single network (2 × 16 lines measured 2× the CPU)
    static constexpr int lines1 = 8;
    static void hadamard8 (std::array<double, lines1>& v) noexcept
    {
        for (int h = 1; h < lines1; h <<= 1)
            for (int i = 0; i < lines1; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const double a = v[(std::size_t) j], b = v[(std::size_t) (j + h)];
                    v[(std::size_t) j] = a + b;
                    v[(std::size_t) (j + h)] = a - b;
                }
        for (auto& x : v) x *= 0.35355339059327376; // 1 / √8
    }

    void set1 (const Settings& s) noexcept
    {
        using namespace detail;
        const auto& f = fit1();
        const auto& st = styleDefs[(std::size_t) s.style];
        const auto bd = bandDecay (s);
        const double S = clamp01 (s.spaceKnob), D = clamp01 (s.distance);
        // line lengths: L_j = 5.18 ms × 1.12402^(k + n0 + j), k = ⌊14 sin (π/2 · S^0.8)⌋, n0 = 9 / 10 / 11 by distance, the
        // second channel half a step longer
        const int k = (int) std::floor (14.0 * std::sin (0.5 * pi * std::pow (S, 0.8)));
        const int n0 = D < 0.25 ? 9 : D <= 0.75 ? 10 : 11;
        const auto gOf = [&] (double len, double t) { return std::pow (10.0, -3.0 * len / (sr * std::max (0.02, t))); };
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < lines1; ++j)
            {
                const double len = std::max (8.0, 5.18e-3 * std::pow (1.12402, k + n0 + 2 * j + c) * sr);
                lenS1[(std::size_t) c][(std::size_t) j] = len;
                const double gm = gOf (len, bd.mid), gl = gOf (len, bd.low), gh = gOf (len, bd.high);
                mid1[(std::size_t) c][(std::size_t) j] = gm;
                abs1[(std::size_t) c][(std::size_t) j].setLow (gl / gm, std::clamp (s.lowHz, 40.0, 2000.0), sr);
                abs1[(std::size_t) c][(std::size_t) (j + lines1)].setHigh (gh / gm, std::clamp (s.highHz, 500.0, sr * 0.45), sr);
            }
        // the 2-line network rings ≈ (200 ms + 0.00088 · (T − 200 ms)) × rate: a short body that stays at small spaces
        const double tMs = 1000.0 * bd.mid;
        const double t2 = 0.001 * (200.0 + 0.00088 * (tMs - 200.0)) * s.rate;
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 2; ++j)
            {
                const double len = std::max (8.0, (j == 0 ? 17.3 : 23.9) * (c == 0 ? 1.0 : 1.061) * 0.001 * sr);
                len2[(std::size_t) c][(std::size_t) j] = len;
                g2[(std::size_t) c][(std::size_t) j] = gOf (len, t2);
            }
        // the early network: lines without a feedback matrix (each feeds itself back), decaying at 1.15 × the displayed
        // time without the rate multiplier (the law), shortened by earlyDecay (round 190lg: the time windows of the
        // reference end its energy with the late network's, at every rate)
        const double tEarly = 1.15 * s.decaySec * f.earlyDecay;
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < earlyLines; ++j)
            {
                const double len = std::clamp (5.18e-3 * std::pow (1.12402, k + 2 * j + c) * sr, 8.0, 0.19 * sr);
                lenE[(std::size_t) c][(std::size_t) j] = (int) std::lround (len);
                gE[(std::size_t) c][(std::size_t) j] = gOf (std::round (len), tEarly);
            }
        modDepth = st.modDepthMs * (0.15 + 2.6 * clamp01 (s.character)) * 0.001 * sr;
        modInc = 2.0 * pi * st.modHz * (0.6 + 0.8 * clamp01 (s.character)) / sr;
        const double farPre = s.distance > 0.5 ? 1.0 - 1.7 * (std::min (s.distance, 1.0) - 0.5) : 1.0;
        preS = std::clamp ((st.basePredelayMs * farPre + std::max (0.0, s.predelayMs)) * 0.001 * sr, 0.0, 0.5 * sr);
        for (int c = 0; c < 2; ++c)
            for (int q = 0; q < 4; ++q)
                diff[(std::size_t) c][(std::size_t) q].g = q < st.diffusers ? st.diffuserGain : 0.0;
        const double near = 1.0 - D;
        erGain = st.erLevel * (0.6 + 4.4 * near * near * near) * (1.0 + 0.8 * std::max (0.0, s.thickness)) * f.er;
        erSpread = (0.55 + 0.9 * near) * (0.5 + 0.5 * S);
        // stereo width W (0 … 1.2): v = √|W − 1|; the input's side scaled by s_in into the two networks; the networks'
        // feedback rotated into each other by v; the output's mid / side by X / Y
        const double W = std::clamp (s.widthRaw, 0.0, 1.2), v = std::sqrt (std::abs (W - 1.0));
        sideIn = std::min (std::pow (2.0 * std::min (W, 1.0) - 0.8, 2.0) + 0.21, 1.0);
        if (W < 0.7)
        {
            const double u = W / 0.7;
            outY = u;
            outX = 1.0 + 0.8 * (std::sqrt (2.0 - u * u) - 1.0);
        }
        else
        {
            const double e = std::max (W - 1.0, 0.0);
            outX = 1.0 - 1.25 * e;
            outY = 1.0 + 0.5 * e;
        }
        rotC = std::sqrt (1.0 - 0.5 * v * v); rotS = v / std::sqrt (2.0);
        rot2C = std::sqrt (1.0 - 0.5 * v); rot2S = std::sqrt (v) / std::sqrt (2.0);
        crossIn = 0.5477 * f.cross * std::pow (std::min (v / 0.5477, 1.0), 2.0);
        // brightness B (0 … 1): darkness d = 1 − min (B / 0.7, 1), extra e = max (2.5 (B − 0.6), 0)
        const double B = clamp01 (0.5 * (s.brightness + 1.0));
        const double dk = 1.0 - std::min (B / 0.7, 1.0), ex = std::max (2.5 * (B - 0.6), 0.0);
        // late level: G = ((250 / T_ms)^0.7 + 1.3) · (1 + 0.5 d) · f10 (character / distance)
        const double f4 = (0.9 * std::min (3.33 * clamp01 (s.character), 1.0) + 0.1) * std::min (S + 0.1, 0.85);
        const double sd = std::sqrt (D);
        const double f10 = 1.0 - ((0.045 * sd + 0.0375) * f4 * sd + (0.0375 * sd + 0.03125) * f4) * f4;
        const double th = std::clamp (s.thickness, -1.0, 1.0);
        thickDb = th >= 0.0 ? f.thickPosDb * (1.0 - (1.0 - th) * (1.0 - th)) : f.thickNegDb * th;
        const auto si = (std::size_t) s.style;
        // round 190lg: the fitted surfaces (dB) over Space, decay rate, Distance and Character, and the whole wet by Distance
        const double lr2 = std::log2 (std::max (s.rate, 1e-6)), Sc = S - 0.5, Dc = D - 0.5, ch = clamp01 (s.character);
        const double d4 = 4.0 * D;
        const int di = std::min ((int) d4, 3);
        const double distDb = f.distDb[(std::size_t) di] + (f.distDb[(std::size_t) di + 1] - f.distDb[(std::size_t) di]) * (d4 - di)
                              + f.distCh * (ch - 0.5) * std::min (2.0 * D, 1.0);
        const double s4 = 4.0 * S;
        const int si4 = std::min ((int) s4, 3);
        const double spDb = f.spaceDb[(std::size_t) si4] + (f.spaceDb[(std::size_t) si4 + 1] - f.spaceDb[(std::size_t) si4]) * (s4 - si4);
        const double lateDb = spDb + f.lateS * Sc + f.lateS2 * Sc * Sc + f.lateSR * Sc * lr2 + f.lateR * lr2 + f.lateD * Dc + f.lateD2 * Dc * Dc
                              + f.lateCh * (ch - 0.3) + distDb;
        const auto dbg = [] (double db) { return std::pow (10.0, db / 20.0); };
        lateGain = f.late * f.styleLate[si] * std::pow (std::max (bd.mid, 0.05), f.lateExp)
                   * (std::pow (250.0 / std::max (tMs, 1.0), 0.7) + 1.3) * (1.0 + 0.5 * dk) * f10 * dbg (lateDb);
        // round 190lp: the early and 2-line networks fade in as the fixed taps fade out (the same smoothstep in Distance)
        const double fu = clamp01 ((f.tapFar1 - D) / std::max (f.tapFar1 - f.tapFar0, 1e-6)), nearW = fu * fu * (3.0 - 2.0 * fu);
        twoGain = (1.0 - nearW) * f.two * (1.0 + 0.5 * dk) * dbg (spDb + f.twoS * Sc + f.twoD * Dc + f.twoD2 * Dc * Dc + distDb);
        earlyGain = (1.0 - nearW) * f.early * f.styleEarly[si] * (1.0 + 0.5 * dk) * dbg (spDb + f.earlyS * Sc + f.earlyD * Dc + f.earlyD2 * Dc * Dc + distDb);
        erGain *= dbg (distDb + f.erS * Sc + f.erD * Dc + f.erD2 * Dc * Dc);
        setTaps (s, S, D);
        // round 190lz: the taps' tone (Fit1::tapEqDb …), weighted by the decay rate
        {
            const double ur = clamp01 ((std::log2 (f.eqR1) - lr2) / std::max (std::log2 (f.eqR1) - std::log2 (f.eqR0), 1e-6)), wR = ur * ur * (3.0 - 2.0 * ur);
            tapEq = wR > 0.0 && (f.tapEqDb != 0.0 || f.tapEqHiDb != 0.0 || f.tapEqTrim != 0.0);
            const double tt = dbg (f.tapEqTrim * wR);
            for (int c = 0; c < 2; ++c)
            {
                tap1[(std::size_t) c][0].set (Bq::Kind::peak, f.tapEqHz, f.tapEqQ, f.tapEqDb * wR, sr);
                tap1[(std::size_t) c][1].set (Bq::Kind::highShelf, 4000.0, 0.5, f.tapEqHiDb * wR, sr);
                tap1[(std::size_t) c][0].b0 *= tt; tap1[(std::size_t) c][0].b1 *= tt; tap1[(std::size_t) c][0].b2 *= tt;
            }
        }
        const double wTerm = 0.5 * std::min (1.25 * f.cross, 1.5) * v;
        const double lpHz = std::pow (2.0, 14.136 - 4.0 * dk + f.lpOctaves);
        const double shHz = std::pow (2.0, 13.873 - 2.0 * dk);
        for (int c = 0; c < 2; ++c)
        {
            auto& o = out1[(std::size_t) c];
            o[0].set (Bq::Kind::lowShelf, 200.0, 0.3, -8.0 * ex * ex - 6.0 * wTerm, sr);
            // the brightness low pass: one pole (the measured B = 0 curve flattens to −30 dB above 8 kHz, a biquad's would not)
            const double x = std::exp (-2.0 * pi * std::min (lpHz, sr * 0.45) / sr);
            o[1].b0 = 1.0 - x; o[1].b1 = o[1].b2 = 0.0; o[1].a1 = -x; o[1].a2 = 0.0;
            o[2].set (Bq::Kind::highShelf, shHz, 0.5, f.shelfDb - f.darkDb * dk, sr);
            o[3].set (Bq::Kind::highShelf, 2500.0, 0.5, f.toneDb + f.styleHighDb[si], sr);
            // the style's own colour (fitted per style: Vintage / Plate against Modern)
            o[4].set (Bq::Kind::lowShelf, 500.0, 0.5, f.styleLowDb[si], sr);
            const double y = f.styleLpHz[si] > 0.0 ? std::exp (-2.0 * pi * std::min (f.styleLpHz[si], sr * 0.45) / sr) : 0.0;
            o[5].b0 = 1.0 - y; o[5].b1 = o[5].b2 = 0.0; o[5].a1 = -y; o[5].a2 = 0.0;
        }
        dryGain = std::cos (clamp01 (s.mix) * pi * 0.5);
        wetGain = std::sin (clamp01 (s.mix) * pi * 0.5);
        thA = std::exp (-1.0 / (f.thickAtt * sr)); thR = std::exp (-1.0 / (f.thickRel * sr));
    }

    void process1 (float* left, float* right, int n) noexcept
    {
        using namespace detail;
        const bool duck = cur.ducking > 0.0;
        const double duckDepthDb = 0.71 * 48.0 * clamp01 (cur.ducking);
        const double envA = std::exp (-1.0 / (0.001 * sr)), envR = std::exp (-1.0 / (0.005 * sr));
        const double attA = std::exp (-1.0 / (0.01 * sr)), attR = std::exp (-1.0 / (0.115 * sr));
        // round 190lp: the early and 2-line networks only run while they are heard (Distance past ½); when they come back
        // they start from silence
        const bool nets = earlyGain != 0.0 || twoGain != 0.0;
        if (nets && netsIdle)
        {
            for (auto& c : two1)
                for (auto& l : c) l.clear();
            for (auto& c : early1)
                for (auto& l : c) l.clear();
            tw = {};
        }
        netsIdle = ! nets;
        for (int i = 0; i < n; ++i)
        {
            const double inL = left[i], inR = right[i];
            double duckGain = 1.0;
            if (duck)
            {
                const double a = std::max (std::abs (inL), std::abs (inR));
                duckEnv = a > duckEnv ? a + envA * (duckEnv - a) : a + envR * (duckEnv - a);
                const double key = clamp01 ((20.0 * std::log10 (duckEnv + 1e-12) + 50.0) / 10.0);
                const double target = duckDepthDb * key;
                duckDb = target > duckDb ? target + attA * (duckDb - target) : target + attR * (duckDb - target);
                duckGain = std::pow (10.0, -duckDb / 20.0);
            }
            // true stereo: l_in = M + s_in · S into the left networks, r_in = M − s_in · S into the right; the CrossInput
            // feeds each into the other
            const double m = 0.5 * (inL + inR), sd = 0.5 * (inL - inR) * sideIn;
            const double a0 = m + sd, a1 = m - sd;
            std::array<double, 2> x { a0 + crossIn * a1, a1 + crossIn * a0 };
            std::array<double, 2> e {}, dIn {};
            for (int c = 0; c < 2; ++c)
            {
                auto& pd = pre1[(std::size_t) c];
                pd.push (x[(std::size_t) c]);
                const double xc = preS >= 1.0 ? pd.read (preS) : x[(std::size_t) c];
                // round 190lp: the fixed taps read the channel's input before the network's pre-delay (their times
                // are the reference's from the input; the user pre-delay moves them)
                auto& ed = er1[(std::size_t) c];
                ed.push (x[(std::size_t) c]);
                double ec = 0.0;
                const int nt = nTaps[(std::size_t) c];
                for (int q = 0; q < nt; ++q)
                    ec += ed.tap (tapN[(std::size_t) c][(std::size_t) q]) * tapG[(std::size_t) c][(std::size_t) q];
                e[(std::size_t) c] = ec;
                double dd = xc;
                for (int q = 0; q < 4; ++q)
                {
                    if (diff[(std::size_t) c][(std::size_t) q].g == 0.0) break;
                    dd = diff[(std::size_t) c][(std::size_t) q].run (dd);
                }
                dIn[(std::size_t) c] = dd;
            }
            phase += modInc;
            if (phase > 2.0 * pi) phase -= 2.0 * pi;
            std::array<std::array<double, lines1>, 2> v {};
            std::array<double, 2> o {}, t {}, eo {};
            // the lines' modulation: sin (phase + offset) from one sin / cos a sample (the offsets' sin / cos precomputed)
            const double sp = std::sin (phase), cp = std::cos (phase);
            for (int c = 0; c < 2; ++c)
            {
                auto& vc = v[(std::size_t) c];
                for (int j = 0; j < lines1; ++j)
                {
                    const auto& oc = modOff[(std::size_t) c][(std::size_t) j];
                    const double md = modDepth * (sp * oc[0] + cp * oc[1]);
                    const double r = net1[(std::size_t) c][(std::size_t) j].read (lenS1[(std::size_t) c][(std::size_t) j] + md + modDepth + 1.0);
                    vc[(std::size_t) j] = abs1[(std::size_t) c][(std::size_t) (j + lines1)].run (
                        abs1[(std::size_t) c][(std::size_t) j].run (r * mid1[(std::size_t) c][(std::size_t) j]));
                    o[(std::size_t) c] += (j & 1) ? -vc[(std::size_t) j] : vc[(std::size_t) j];
                }
                hadamard8 (vc);
                if (! nets)
                    continue;
                // the 2-line network (Hadamard 2)
                const double p = two1[(std::size_t) c][0].tap ((int) len2[(std::size_t) c][0]) * g2[(std::size_t) c][0];
                const double q = two1[(std::size_t) c][1].tap ((int) len2[(std::size_t) c][1]) * g2[(std::size_t) c][1];
                t[(std::size_t) c] = p - q;
                tw[(std::size_t) c][0] = (p + q) * 0.70710678118654752;
                tw[(std::size_t) c][1] = (p - q) * 0.70710678118654752;
                // the early network
                double ea = 0.0;
                for (int j = 0; j < earlyLines; ++j)
                {
                    auto& dl = early1[(std::size_t) c][(std::size_t) j];
                    const double y = dl.tap (lenE[(std::size_t) c][(std::size_t) j]) * gE[(std::size_t) c][(std::size_t) j];
                    ea += (j & 1) ? -y : y;
                    dl.push (y + dIn[(std::size_t) c]);
                }
                eo[(std::size_t) c] = ea;
            }
            // the width rotation links the two channels' networks (orthogonal: lossless)
            for (int j = 0; j < lines1; ++j)
            {
                const double a = v[0][(std::size_t) j], b = v[1][(std::size_t) j];
                net1[0][(std::size_t) j].push (rotC * a + rotS * b + ((j & 3) == 1 || (j & 3) == 2 ? -dIn[0] : dIn[0]));
                net1[1][(std::size_t) j].push (rotC * b - rotS * a + ((j & 3) == 1 || (j & 3) == 2 ? -dIn[1] : dIn[1]));
            }
            for (int j = 0; j < (nets ? 2 : 0); ++j)
            {
                const double a = tw[0][(std::size_t) j], b = tw[1][(std::size_t) j];
                two1[0][(std::size_t) j].push (rot2C * a + rot2S * b + dIn[0]);
                two1[1][(std::size_t) j].push (rot2C * b - rot2S * a + (j == 0 ? dIn[1] : -dIn[1]));
            }
            std::array<double, 2> w {};
            // thickness: the body (late, 2-line, early networks) × the gain the wet's level allows (the
            // envelope of the previous output sample); the early reflections only below 0 (a cut)
            double thG = 1.0, thE = 1.0;
            if (thickDb != 0.0)
            {
                const double lv = 10.0 * std::log10 (thEnv + 1e-30);
                const double wv = std::clamp ((fit1().thickHi - lv) / (fit1().thickHi - fit1().thickLo), 0.0, 1.0);
                thG = std::exp (thickDb * wv * 0.11512925464970229);
                if (thickDb < 0.0) thE = thG;
            }
            for (int c = 0; c < 2; ++c)
            {
                double b = o[(std::size_t) c] * lateGain + t[(std::size_t) c] * twoGain + eo[(std::size_t) c] * earlyGain;
                double et = e[(std::size_t) c];
                if (tapEq)
                    et = tap1[(std::size_t) c][1].run (tap1[(std::size_t) c][0].run (et));
                double y = b * thG + et * thE;
                for (auto& fl : out1[(std::size_t) c]) y = fl.run (y);
                w[(std::size_t) c] = y;
            }
            double wm = 0.5 * (w[0] + w[1]) * outX * duckGain, ws = 0.5 * (w[0] - w[1]) * outY * duckGain;
            if (thickDb != 0.0)
            {
                const double e2 = wm * wm + ws * ws; // (l² + r²) / 2 of the wet
                thEnv = e2 > thEnv ? e2 + thA * (thEnv - e2) : e2 + thR * (thEnv - e2);
            }
            left[i] = (float) (inL * dryGain + (wm + ws) * wetGain);
            right[i] = (float) (inR * dryGain + (wm - ws) * wetGain);
        }
    }
    double thickDb = 0.0, thEnv = 0.0, thA = 0.0, thR = 0.0;
    bool netsIdle = true; // round 190lp: the early / 2-line networks were not run (their lines are stale)

    // round 190lp: the fixed early taps' gains and delays (see Fit1's tap laws)
    static constexpr int maxTaps = 48;
    std::array<std::array<double, maxTaps>, 2> tapG {};
    std::array<std::array<int, maxTaps>, 2> tapN {};
    std::array<int, 2> nTaps {};
    void setTaps (const Settings& s, double S, double D) noexcept
    {
        using namespace detail;
        const auto& f = fit1();
        const double lr2 = std::log2 (std::max (s.rate, 1e-6)), ch = clamp01 (s.character);
        const double T = std::max (bandDecay (s).mid, 0.02);
        const double bg = f.tapS * S + f.tapR * lr2 + f.tapD * D;
        const double fu = clamp01 ((f.tapFar1 - D) / std::max (f.tapFar1 - f.tapFar0, 1e-6));
        const double far = fu * fu * (3.0 - 2.0 * fu);
        // Character lifts the standing-out taps from Distance ¼ on (at Distance 0 the reference's do not move)
        const double chU = clamp01 ((ch - 0.25) / 0.25), chD = clamp01 (D / 0.25);
        const double mainDb = f.tapMainD * D + f.tapMainCh * chU * chU * (3.0 - 2.0 * chU) * chD * chD * (3.0 - 2.0 * chD);
        // from Character ~0.6 on the reference's arrivals are modulated: the standing-out ones after 20 ms lose their lift
        // (20 … 40 ms 4 … 8 dB quieter at Character ¾ / 1 than at ½; the first ones stay)
        const double hiU = clamp01 ((ch - f.tapHiCh0) / 0.2), hiCh = hiU * hiU * (3.0 - 2.0 * hiU);
        const double kk = std::floor (14.0 * std::sin (0.5 * pi * std::pow (S, 0.8)));
        const double pre = std::max (0.0, s.predelayMs);
        for (int c = 0; c < 2; ++c)
        {
            const ETap* T0 = c == 0 ? tapsL.data() : tapsR.data();
            const int n = (int) (c == 0 ? tapsL.size() : tapsR.size());
            nTaps[(std::size_t) c] = far > 0.0 || f.tapUnit ? n : 0; // faded out (Distance ¾ on): not read at all
            for (int q = 0; q < n; ++q)
            {
                double db = T0[q].db + bg + f.tapT * 0.001 * T0[q].ms / T;
                for (const auto& b : tapBoosts)
                    if (b.ch == c && std::abs (b.ms - T0[q].ms) < 0.001)
                    {
                        double bump = 0.0;
                        for (std::size_t j = 0; j < tapKappa.size(); ++j)
                            bump += b.a[j] * std::exp (-(kk - tapKappa[j]) * (kk - tapKappa[j]));
                        db += T0[q].ms > 20.0 ? bump * (1.0 - hiCh) : bump;
                        db += mainDb + b.d * D;
                    }
                const double g = f.tapUnit ? 1.0 : std::pow (10.0, (db + f.tapTrim) / 20.0) * far;
                tapG[(std::size_t) c][(std::size_t) q] = T0[q].sign * g;
                tapN[(std::size_t) c][(std::size_t) q] = 1 + (int) std::lround ((T0[q].ms + pre) * 0.001 * sr); // Delay::tap (1) = no delay
            }
        }
    }
    // the taps that stand out at some Spaces: dB = Σ_j a_j e^(−(k − κ_j)²), κ = 0, 5, 6, 10, 13, 14 (the Spaces measured:
    // 0, ⅕, ¼, ½, ¾ … ⅘, 1), k = ⌊14 sin (π/2 S^0.8)⌋ the integer of the line-length law (the reference switches these
    // taps with it: Space ⅕ and ¼ differ, ¾ and ⅘ do not)
    struct TapBoost { int ch; double ms; std::array<double, 6> a; double d; }; // d: the tap's own dB per unit Distance
    static constexpr std::array<double, 6> tapKappa { 0.0, 5.0, 6.0, 10.0, 13.0, 14.0 };
    static constexpr std::array<TapBoost, 15> tapBoosts { {
        { 0, 6.6667, { 10.96, -0.86, -1.78, -4.53, -3.36, -2.80 }, -10.28 },
        { 0, 11.3125, { 13.24, 5.07, 4.34, -10.63, -6.98, -6.00 }, -10.30 },
        { 0, 18.3333, { 2.52, -5.21, -3.78, 1.71, 1.28, 1.91 }, -10.27 },
        { 0, 25.4167, { 40.00, 0.00, 0.00, 0.00, 0.00, 0.00 }, 0.00 },
        { 0, 28.4375, { 2.04, 12.31, -8.53, -2.86, -1.74, -0.92 }, -5.09 },
        { 0, 49.0833, { -2.11, 2.79, 7.75, -0.00, -4.50, -3.14 }, 15.97 },
        { 0, 50.9583, { -0.49, 2.31, 9.58, -5.36, -2.62, -1.27 }, 16.41 },
        { 0, 70.4167, { -3.04, 4.78, 5.66, -0.00, -4.15, -2.13 }, 18.28 },
        { 1, 8.2917, { 13.29, -1.47, -2.34, -5.25, -3.88, -3.30 }, -10.27 },
        { 1, 12.4792, { 11.99, 14.58, -12.77, -7.02, -4.71, -3.90 }, -10.17 },
        { 1, 21.1250, { 8.39, -2.48, 11.96, -8.66, -5.15, -3.98 }, -10.20 },
        { 1, 32.3958, { 3.46, -3.40, -2.01, 5.65, 12.45, -19.50 }, -10.26 },
        { 1, 36.5625, { 3.29, -0.10, -1.13, -1.32, -1.01, -0.45 }, 4.44 },
        { 1, 55.3333, { 0.45, -5.27, -0.52, -1.05, -3.22, 9.83 }, -1.98 },
        { 1, 97.9792, { -8.31, 3.32, 9.01, -1.53, 0.12, 1.02 }, 16.90 } } };

    std::array<std::array<detail::Delay, lines1>, 2> net1;
    std::array<std::array<detail::Delay, 2>, 2> two1;
    static constexpr int earlyLines = 8;
    std::array<std::array<detail::Delay, earlyLines>, 2> early1;
    std::array<std::array<int, earlyLines>, 2> lenE {};
    std::array<std::array<double, earlyLines>, 2> gE {};
    std::array<detail::Delay, 2> pre1, er1;
    std::array<std::array<double, lines1>, 2> lenS1 {}, mid1 {};
    std::array<std::array<detail::Shelf, 2 * lines1>, 2> abs1;
    std::array<std::array<double, 2>, 2> len2 {}, g2 {}, tw {};
    std::array<std::array<std::array<double, 2>, lines1>, 2> modOff = [] {
        std::array<std::array<std::array<double, 2>, lines1>, 2> m {};
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < lines1; ++j)
                m[(std::size_t) c][(std::size_t) j] = { std::cos (0.39 * j + 1.3 * c), std::sin (0.39 * j + 1.3 * c) };
        return m;
    }();
    std::array<std::array<detail::Bq, 6>, 2> out1;
    bool tapEq = false;
    std::array<std::array<detail::Bq, 2>, 2> tap1; // round 190lz: the taps' tone
    double sideIn = 1.0, outX = 1.0, outY = 1.0, rotC = 1.0, rotS = 0.0, rot2C = 1.0, rot2S = 0.0, crossIn = 0.0, twoGain = 0.0, earlyGain = 0.0;

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
    double duckEnv = 0.0, duckDb = 0.0; // round 190it
};
} // namespace lynx::spaceverb
