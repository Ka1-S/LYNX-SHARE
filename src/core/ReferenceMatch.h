#pragma once

// Reference matching for mastering (round 190ch, LYNX's own; the window is ui/ReferenceMatchPanel.h). Everything runs
// on this computer, on the song's own render and a reference file the user picks: no service, no model.
//
//   analyse   integrated loudness and true peak (core/Loudness.h, ITU-R BS.1770-4 / EBU R 128), the long-term average
//             spectrum in 31 third-octave bands (20 Hz … 20 kHz; Welch's method, Hann windows, 50 % overlap, the power
//             of both channels) and the stereo width (side against mid energy, and the channels' correlation)
//   suggest   the tone difference reference − song per band, without its mean level (100 Hz … 10 kHz), smoothed along
//             the bands, scaled by Amount and limited to ±6 dB per band; then a Facet EQ setting fitted to that curve
//             (a low shelf, six bells, a high shelf: least squares on the EQ's own response), and the gain that
//             brings the song to the reference's loudness
//   apply     the suggestion as two ordinary slots at the end of the Master, both named "Reference match": Facet EQ
//             (the curve; its output gain = the loudness gain) and Apex Limiter (true-peak ceiling). They are LYNX's
//             own plugins with their own editors: every band and the gain can be changed or switched off afterwards.
//
// Nothing here is hidden processing: what Match does is exactly what those two slots show. No JUCE: tested in
// tests/BandlabExtras190chTests.inc.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "core/ApexLimiter.h"
#include "core/AudioEditTools.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/Loudness.h"
#include "core/Song.h"

namespace lynx::refmatch
{
constexpr int bands = 31;
constexpr double maxBandDb = 6.0;       // the most the suggestion moves a band
constexpr double maxLoudnessDb = 12.0;  // the most it moves the level
constexpr const char* slotName = "Reference match"; // EffectSlot::presetName of the two slots (how they are found again)
constexpr int chainSlots = 2;

// The third-octave centre frequencies: 1 kHz × 2^((k − 17) / 3), 19.7 Hz … 20.2 kHz
inline double bandHz (int k) noexcept { return 1000.0 * std::pow (2.0, (double) (k - 17) / 3.0); }

struct Analysis
{
    bool valid = false;
    double seconds = 0.0;
    double lufs = loudness::silence;       // integrated
    double truePeakDb = loudness::silence; // dBTP
    double sideDb = -120.0;                // side energy against mid energy, dB (−120 = mono)
    double correlation = 1.0;              // of left and right, −1 … 1
    std::array<double, bands> spectrumDb {}; // each band's mean power, dB (0 dB = a full-scale sine's power is −3)
    int usedBands = bands;                 // bands below the Nyquist frequency (the ones above repeat the last)

    // the stereo width as a share: how much of the energy is in the side signal, 0 (mono) … 100 %
    double widthPercent() const noexcept
    {
        const auto ratio = std::pow (10.0, sideDb / 10.0);
        return 100.0 * ratio / (1.0 + ratio);
    }
};

// Measures a stereo buffer (`right` may be the same pointer as `left` for mono). At most the first 15 minutes.
inline Analysis analyse (const float* left, const float* right, std::size_t frames, double sampleRate)
{
    Analysis a;
    if (left == nullptr || right == nullptr || sampleRate < 8000.0)
        return a;
    frames = std::min (frames, (std::size_t) (sampleRate * 900.0));
    a.seconds = (double) frames / sampleRate;
    if (frames < (std::size_t) (sampleRate * 0.5))
        return a;

    // loudness, true peak
    loudness::Meter meter;
    meter.prepare (sampleRate);
    for (std::size_t at = 0; at < frames; at += 4096)
        meter.process (left + at, right + at, (int) std::min<std::size_t> (4096, frames - at));
    a.lufs = meter.integratedLoudness();
    a.truePeakDb = meter.truePeakDb();

    // width
    double mid = 0.0, side = 0.0, ll = 0.0, rr = 0.0, lr = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double l = left[i], r = right[i];
        mid += 0.25 * (l + r) * (l + r);
        side += 0.25 * (l - r) * (l - r);
        ll += l * l;
        rr += r * r;
        lr += l * r;
    }
    // (no mid at all, as with one channel inverted: all side; silence counts as mono)
    a.sideDb = side <= 1.0e-30 ? -120.0 : mid <= 1.0e-12 * side ? 120.0 : std::clamp (10.0 * std::log10 (side / mid), -120.0, 120.0);
    a.correlation = ll > 0.0 && rr > 0.0 ? std::clamp (lr / std::sqrt (ll * rr), -1.0, 1.0) : 1.0;

    // long-term average spectrum: windows of about 0.37 s (2.7 Hz per bin at 44.1 kHz), half overlapped
    std::size_t n = 16384;
    while ((double) n < sampleRate / 3.0)
        n *= 2;
    while (n > 1024 && n > frames)
        n /= 2;
    const auto hop = n / 2;
    std::vector<double> window (n), re (n), im (n), power (n / 2 + 1, 0.0);
    constexpr double pi = 3.14159265358979323846;
    double windowPower = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        window[i] = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n);
        windowPower += window[i] * window[i];
    }
    long long count = 0;
    for (std::size_t at = 0; at + n <= frames; at += hop)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto* in = channel == 0 ? left : right;
            for (std::size_t i = 0; i < n; ++i)
            {
                re[i] = (double) in[at + i] * window[i];
                im[i] = 0.0;
            }
            audioedit::fftInPlace (re, im, false);
            for (std::size_t i = 0; i <= n / 2; ++i)
                power[i] += re[i] * re[i] + im[i] * im[i];
        }
        ++count;
    }
    if (count == 0)
        return a;
    // one-sided power per bin, mean of the two channels: the bins of a band add up to the band's mean square
    const auto scale = 2.0 / (windowPower * (double) n * 2.0 * (double) count);
    const auto df = sampleRate / (double) n;
    a.usedBands = 0;
    for (int k = 0; k < bands; ++k)
    {
        const auto lo = bandHz (k) * std::pow (2.0, -1.0 / 6.0), hi = std::min (bandHz (k) * std::pow (2.0, 1.0 / 6.0), sampleRate * 0.5);
        if (bandHz (k) >= sampleRate * 0.5 * 0.98)
        {
            a.spectrumDb[(std::size_t) k] = k > 0 ? a.spectrumDb[(std::size_t) k - 1] : -120.0;
            continue;
        }
        a.usedBands = k + 1;
        // each bin covers [(i − ½) df, (i + ½) df): the part of it inside the band counts
        const auto first = (std::size_t) std::max (1.0, std::floor (lo / df + 0.5)), last = std::min (n / 2, (std::size_t) std::floor (hi / df + 0.5));
        double sum = 0.0;
        for (auto i = first; i <= last; ++i)
        {
            const auto binLo = ((double) i - 0.5) * df, binHi = ((double) i + 0.5) * df;
            const auto part = std::max (0.0, std::min (hi, binHi) - std::max (lo, binLo)) / df;
            sum += power[i] * part;
        }
        a.spectrumDb[(std::size_t) k] = 10.0 * std::log10 (sum * scale + 1.0e-14);
    }
    a.valid = true;
    return a;
}

inline Analysis analyse (const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    const auto n = right.empty() ? left.size() : std::min (left.size(), right.size());
    return analyse (left.data(), right.empty() ? left.data() : right.data(), n, sampleRate);
}

// --- the suggestion ------------------------------------------------------------------------------------------------------
struct Options
{
    double amount = 1.0;         // 0 … 1: how much of the tone difference is taken
    bool matchLoudness = true;
    double ceilingDb = -1.0;     // the limiter's true-peak ceiling (dBTP)
    double trimDb = 0.0;         // added to the loudness gain: what a measured result still missed (a limiter that has to
                                 // hold peaks back leaves the song quieter than the plain gain says)
};

struct Suggestion
{
    bool valid = false;
    std::array<double, bands> differenceDb {}; // reference − song per band, mean level removed (what was measured)
    std::array<double, bands> targetDb {};     // smoothed, × Amount, limited to ±6 dB: what the EQ is asked for
    std::array<double, bands> eqDb {};         // what the fitted EQ does at each band
    double fitErrorDb = 0.0;                   // the largest |eq − target| over the bands
    double eqLoudnessDb = 0.0;                 // how much the EQ alone changes the song's loudness (estimated, see suggest)
    double gainDb = 0.0;                       // loudness: reference − (song + eqLoudnessDb), limited to ±12 dB (0 when not matched)
    double targetLufs = loudness::silence;     // the reference's integrated loudness
    double ceilingDb = -1.0;
    EffectParams eq {};                        // Facet EQ's parameters (EffectKind::facetEq)
};

// The EQ's fixed layout: a low shelf, six bells, a high shelf (the gains are what gets fitted)
constexpr int eqBands = 8;
constexpr std::array<double, eqBands> eqHz { 45.0, 110.0, 260.0, 620.0, 1500.0, 3600.0, 8000.0, 13000.0 };
constexpr std::array<double, eqBands> eqQ { 0.7, 0.8, 0.8, 0.8, 0.8, 0.8, 0.9, 0.7 };

namespace detail
{
// a value 0…1 whose law gives `wanted` (the law rises or falls steadily over 0…1)
template <typename Law>
float valueFor (Law law, double wanted) noexcept
{
    double lo = 0.0, hi = 1.0;
    const auto rising = law (1.0f) >= law (0.0f);
    for (int i = 0; i < 40; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        if ((law ((float) mid) < wanted) == rising)
            lo = mid;
        else
            hi = mid;
    }
    return (float) (0.5 * (lo + hi));
}

inline EffectParams eqLayout()
{
    auto p = makeEffectSlot (EffectKind::facetEq).params;
    for (int b = 0; b < eqBands; ++b)
    {
        const auto shape = b == 0 ? facet::Shape::lowShelf : b == eqBands - 1 ? facet::Shape::highShelf : facet::Shape::bell;
        p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (shape);
        p[(std::size_t) facet::param (b, 1)] = facet::hzValue (eqHz[(std::size_t) b]);
        p[(std::size_t) facet::param (b, 2)] = facet::gainValue (0.0);
        p[(std::size_t) facet::param (b, 3)] = facet::qValue (eqQ[(std::size_t) b]);
    }
    return p;
}

// the EQ's response at the band centres, its output gain left out
inline std::array<double, bands> eqResponse (const EffectParams& p, double sampleRate, int usedBands)
{
    std::array<double, bands> out {};
    auto flat = p;
    flat[(std::size_t) facet::outputParam] = 0.5f;
    for (int k = 0; k < bands; ++k)
        out[(std::size_t) k] = k < usedBands ? facet::responseDb (flat, sampleRate, bandHz (k)) : (k > 0 ? out[(std::size_t) k - 1] : 0.0);
    return out;
}

// solves the n×n system a x = b (Gaussian elimination with pivoting); false if singular
inline bool solve (std::array<std::array<double, eqBands>, eqBands>& a, std::array<double, eqBands>& b) noexcept
{
    for (int c = 0; c < eqBands; ++c)
    {
        int pivot = c;
        for (int r = c + 1; r < eqBands; ++r)
            if (std::abs (a[(std::size_t) r][(std::size_t) c]) > std::abs (a[(std::size_t) pivot][(std::size_t) c]))
                pivot = r;
        if (std::abs (a[(std::size_t) pivot][(std::size_t) c]) < 1.0e-12)
            return false;
        std::swap (a[(std::size_t) pivot], a[(std::size_t) c]);
        std::swap (b[(std::size_t) pivot], b[(std::size_t) c]);
        for (int r = c + 1; r < eqBands; ++r)
        {
            const auto f = a[(std::size_t) r][(std::size_t) c] / a[(std::size_t) c][(std::size_t) c];
            for (int k = c; k < eqBands; ++k)
                a[(std::size_t) r][(std::size_t) k] -= f * a[(std::size_t) c][(std::size_t) k];
            b[(std::size_t) r] -= f * b[(std::size_t) c];
        }
    }
    for (int c = eqBands - 1; c >= 0; --c)
    {
        auto v = b[(std::size_t) c];
        for (int k = c + 1; k < eqBands; ++k)
            v -= a[(std::size_t) c][(std::size_t) k] * b[(std::size_t) k];
        b[(std::size_t) c] = v / a[(std::size_t) c][(std::size_t) c];
    }
    return true;
}
} // namespace detail

// A curve (dB per band) smoothed along the bands: at each band a straight line is fitted to its neighbours (Gaussian
// weights, `sigmaBands` wide) and read at the band, so a steady tilt comes out as it went in, up to both ends.
inline std::array<double, bands> smoothed (const std::array<double, bands>& curve, int usedBands, double sigmaBands = 1.5)
{
    std::array<double, bands> out = curve;
    usedBands = std::clamp (usedBands, 1, bands);
    for (int k = 0; k < usedBands; ++k)
    {
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int j = 0; j < usedBands; ++j)
        {
            const auto x = (double) (j - k), w = std::exp (-0.5 * x * x / (sigmaBands * sigmaBands));
            sw += w;
            sx += w * x;
            sy += w * curve[(std::size_t) j];
            sxx += w * x * x;
            sxy += w * x * curve[(std::size_t) j];
        }
        const auto det = sw * sxx - sx * sx;
        out[(std::size_t) k] = std::abs (det) > 1.0e-9 ? (sxx * sy - sx * sxy) / det : sy / sw;
    }
    for (int k = usedBands; k < bands; ++k)
        out[(std::size_t) k] = out[(std::size_t) usedBands - 1];
    return out;
}

// Fits the EQ's eight gains to `targetDb` (least squares on the EQ's own response, a few Gauss-Newton steps).
inline EffectParams fitEq (const std::array<double, bands>& targetDb, double sampleRate, int usedBands)
{
    auto p = detail::eqLayout();
    usedBands = std::clamp (usedBands, 1, bands);
    std::array<double, eqBands> gain {};
    const auto setGains = [&p, &gain] {
        for (int b = 0; b < eqBands; ++b)
            p[(std::size_t) facet::param (b, 2)] = facet::gainValue (gain[(std::size_t) b]);
    };
    for (int step = 0; step < 8; ++step)
    {
        setGains();
        const auto now = detail::eqResponse (p, sampleRate, usedBands);
        // each band's effect per dB of its gain, around where it stands
        std::array<std::array<double, eqBands>, bands> jac {};
        for (int b = 0; b < eqBands; ++b)
            for (int k = 0; k < usedBands; ++k)
            {
                const auto hz = bandHz (k);
                jac[(std::size_t) k][(std::size_t) b] = (facet::bandResponseDb (p, b, sampleRate, hz, 0.5) - facet::bandResponseDb (p, b, sampleRate, hz, -0.5));
            }
        std::array<std::array<double, eqBands>, eqBands> a {};
        std::array<double, eqBands> rhs {};
        for (int k = 0; k < usedBands; ++k)
        {
            const auto r = targetDb[(std::size_t) k] - now[(std::size_t) k];
            for (int i = 0; i < eqBands; ++i)
            {
                rhs[(std::size_t) i] += jac[(std::size_t) k][(std::size_t) i] * r;
                for (int j = 0; j < eqBands; ++j)
                    a[(std::size_t) i][(std::size_t) j] += jac[(std::size_t) k][(std::size_t) i] * jac[(std::size_t) k][(std::size_t) j];
            }
        }
        for (int i = 0; i < eqBands; ++i)
            a[(std::size_t) i][(std::size_t) i] += 1.0e-3; // (keeps two bands from fighting each other)
        if (! detail::solve (a, rhs))
            break;
        double moved = 0.0;
        for (int b = 0; b < eqBands; ++b)
        {
            const auto next = std::clamp (gain[(std::size_t) b] + rhs[(std::size_t) b], -12.0, 12.0);
            moved = std::max (moved, std::abs (next - gain[(std::size_t) b]));
            gain[(std::size_t) b] = next;
        }
        if (moved < 0.005)
            break;
    }
    for (auto& g : gain) // (a gain the display would show as 0.0 is 0)
        if (std::abs (g) < 0.05)
            g = 0.0;
    setGains();
    return p;
}

// What Match would do for a song measured as `song` against `reference`.
// The loudness meter's K-weighting at a frequency, dB (BS.1770's two filters, core/Loudness.h)
inline double kWeightingDb (double hz, double sampleRate) noexcept
{
    loudness::Biquad shelf, highPass;
    loudness::kWeighting (sampleRate, shelf, highPass);
    const auto w = 2.0 * 3.14159265358979323846 * std::min (hz, 0.499 * sampleRate) / sampleRate;
    const auto power = [w] (const loudness::Biquad& q) {
        const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        const auto nr = q.b0 + q.b1 * c1 + q.b2 * c2, ni = -(q.b1 * s1 + q.b2 * s2);
        const auto dr = 1.0 + q.a1 * c1 + q.a2 * c2, di = -(q.a1 * s1 + q.a2 * s2);
        return (nr * nr + ni * ni) / std::max (1.0e-30, dr * dr + di * di);
    };
    return 10.0 * std::log10 (std::max (1.0e-30, power (shelf) * power (highPass)));
}

inline Suggestion suggest (const Analysis& song, const Analysis& reference, double sampleRate, const Options& options = {})
{
    Suggestion s;
    if (! song.valid || ! reference.valid)
        return s;
    const auto used = std::clamp (std::min (song.usedBands, reference.usedBands), 1, bands);
    double mean = 0.0;
    int counted = 0;
    for (int k = 0; k < bands; ++k)
    {
        s.differenceDb[(std::size_t) k] = reference.spectrumDb[(std::size_t) k] - song.spectrumDb[(std::size_t) k];
        if (k < used && bandHz (k) >= 100.0 && bandHz (k) <= 10000.0)
        {
            mean += s.differenceDb[(std::size_t) k];
            ++counted;
        }
    }
    mean = counted > 0 ? mean / counted : 0.0;
    for (auto& d : s.differenceDb)
        d -= mean;
    const auto amount = std::clamp (options.amount, 0.0, 1.0);
    s.targetDb = smoothed (s.differenceDb, used);
    for (auto& t : s.targetDb)
        t = std::clamp (t * amount, -maxBandDb, maxBandDb);
    s.eq = fitEq (s.targetDb, sampleRate, used);
    s.eqDb = detail::eqResponse (s.eq, sampleRate, used);
    for (int k = 0; k < used; ++k)
        s.fitErrorDb = std::max (s.fitErrorDb, std::abs (s.eqDb[(std::size_t) k] - s.targetDb[(std::size_t) k]));
    s.targetLufs = reference.lufs;
    s.ceilingDb = std::clamp (options.ceilingDb, -12.0, 0.0);
    // The EQ changes the loudness too (a brighter curve is louder), so the gain is what is left after it: the song's
    // band powers, K-weighted as the loudness meter weights them, before and after the EQ.
    {
        double before = 0.0, after = 0.0;
        for (int k = 0; k < used; ++k)
        {
            const auto power = std::pow (10.0, (song.spectrumDb[(std::size_t) k] + kWeightingDb (bandHz (k), sampleRate)) / 10.0);
            before += power;
            after += power * std::pow (10.0, s.eqDb[(std::size_t) k] / 10.0);
        }
        s.eqLoudnessDb = before > 0.0 && after > 0.0 ? 10.0 * std::log10 (after / before) : 0.0;
    }
    if (options.matchLoudness && song.lufs > -70.0 && reference.lufs > -70.0)
        s.gainDb = std::clamp (reference.lufs - song.lufs - s.eqLoudnessDb + options.trimDb, -maxLoudnessDb, maxLoudnessDb);
    s.eq[(std::size_t) facet::outputParam] = detail::valueFor ([] (float v) { return facet::outputDb (v); }, s.gainDb);
    s.valid = true;
    return s;
}

// --- the Master's two slots ------------------------------------------------------------------------------------------------
inline bool isMatchSlot (const EffectSlot& slot) noexcept
{
    return slot.presetName == slotName && (slot.kind == EffectKind::facetEq || slot.kind == EffectKind::apexLimiter);
}

inline std::vector<EffectSlot> chain (const Suggestion& s)
{
    std::vector<EffectSlot> out;
    if (! s.valid)
        return out;
    auto eq = makeEffectSlot (EffectKind::facetEq);
    eq.params = s.eq;
    eq.presetName = slotName;
    auto limiter = makeEffectSlot (EffectKind::apexLimiter);
    limiter.params[apex::pGain] = 0.0f;
    limiter.params[apex::pCeiling] = detail::valueFor ([] (float v) { return apex::ceilingDb (v); }, s.ceilingDb);
    limiter.params[apex::pTruePeak] = 1.0f;
    limiter.presetName = slotName;
    out.push_back (std::move (eq));
    out.push_back (std::move (limiter));
    return out;
}

inline std::vector<int> matchSlots (const Song& song)
{
    std::vector<int> idx;
    if (! song.mixer.empty())
        for (int i = 0; i < (int) song.mixer[0].effects.size(); ++i)
            if (isMatchSlot (song.mixer[0].effects[(std::size_t) i]))
                idx.push_back (i);
    return idx;
}

inline bool present (const Song& song) { return ! matchSlots (song).empty(); }

inline void remove (Song& song)
{
    if (song.mixer.empty())
        return;
    auto& fx = song.mixer[0].effects;
    fx.erase (std::remove_if (fx.begin(), fx.end(), [] (const EffectSlot& e) { return isMatchSlot (e); }), fx.end());
}

// Puts the suggestion on the Master: an earlier match is replaced in place (same slots, same on / off state), else the
// two slots go after the Master's last effect. False (nothing changed) when the Master has no room for two more.
inline bool apply (Song& song, const Suggestion& s)
{
    auto slots = chain (s);
    if (slots.empty() || song.mixer.empty())
        return false;
    auto& fx = song.mixer[0].effects;
    const auto existing = matchSlots (song);
    if (existing.size() == slots.size() && fx[(std::size_t) existing[0]].kind == EffectKind::facetEq
        && fx[(std::size_t) existing[1]].kind == EffectKind::apexLimiter)
    {
        for (std::size_t k = 0; k < slots.size(); ++k)
        {
            auto& old = fx[(std::size_t) existing[k]];
            slots[k].row = old.row;
            slots[k].enabled = old.enabled;
            old = std::move (slots[k]);
        }
        return true;
    }
    if ((int) fx.size() - (int) existing.size() + chainSlots > maxEffectSlots)
        return false;
    remove (song);
    for (auto& slot : slots)
        song.mixer[0].effects.push_back (std::move (slot));
    return true;
}

// "Hear without": both slots off / on as one
inline bool bypassed (const Song& song)
{
    const auto idx = matchSlots (song);
    if (idx.empty())
        return false;
    for (const auto i : idx)
        if (song.mixer[0].effects[(std::size_t) i].enabled)
            return false;
    return true;
}

inline void setBypassed (Song& song, bool off)
{
    for (const auto i : matchSlots (song))
        song.mixer[0].effects[(std::size_t) i].enabled = ! off;
}
} // namespace lynx::refmatch
