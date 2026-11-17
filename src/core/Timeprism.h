#pragma once

// Timeprism (round 190ko): LYNX's spectral freezer and delay. The input is cut into overlapping spectra
// (core/SpectralCore.h); a freezer holds a spectrum and keeps playing it (by hand, every interval, or at each detected
// onset; cross-faded or with its own fade in / out, up to eight at once), and a spectral delay sends every frequency
// through its own delay time — tilted towards the highs or lows, sprayed at random, masked to one side of the spectrum,
// spread between left and right — with feedback whose repeats move up or down in frequency. It is the engine an
// imported Ableton Live "Spectral Time" opens on. Clean-room: every law below comes from LYNX's own measurements of
// Live 12.4's output on test signals (round 190ko, /mnt/e/lynx-work/meld190ko, set s1, RESULTS.md §2); nothing of
// Live's code or content is used. Header-only DSP, no allocation in process().
//
// The parameters are 0…1 (EffectParams); the laws below turn them into Live's units, which the .als import maps back.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/SpectralCore.h"

namespace lynx::timeprism
{
// (the Device View's card shows the first seven: the delay's main controls and the mix)
enum Param
{
    time, feedback, shift, tilt, spray, delayMix, mix, mask, stereo, input, delayOn, timeUnit, divisions, sixteenths, freezerOn, freeze,
    freezeMode, retrigger, syncUnits, interval, intervalBeats, fadeType, fadeIn, fadeOut, crossfade, sensitivity, resolution, order, count
};

// ---- the knob laws (all parameters 0..1) ---------------------------------------------------------------------------
inline bool on (float v) noexcept { return v >= 0.5f; }
inline double logLaw (float v, double lo, double hi) noexcept { return lo * std::pow (hi / lo, (double) std::clamp (v, 0.0f, 1.0f)); }
inline float logValue (double x, double lo, double hi) noexcept
{
    return (float) std::clamp (std::log (std::max (x, lo) / lo) / std::log (hi / lo), 0.0, 1.0);
}
inline int stepOf (float v, int steps) noexcept { return std::clamp ((int) std::lround (v * (float) (steps - 1)), 0, steps - 1); }
inline float stepValue (int i, int steps) noexcept { return steps > 1 ? (float) std::clamp (i, 0, steps - 1) / (float) (steps - 1) : 0.0f; }
inline double linLaw (float v, double lo, double hi) noexcept { return lo + (hi - lo) * (double) std::clamp (v, 0.0f, 1.0f); }
inline float linValue (double x, double lo, double hi) noexcept { return (float) std::clamp ((x - lo) / (hi - lo), 0.0, 1.0); }

inline double intervalSeconds (float v) noexcept { return logLaw (v, 0.02, 10.0); }
inline double fadeInSeconds (float v) noexcept { return logLaw (v, 0.002, 10.0); }
inline double fadeOutSeconds (float v) noexcept { return logLaw (v, 0.02, 10.0); }
inline double timeSeconds (float v) noexcept { return logLaw (v, 0.01, 3.0); }
inline double tiltOf (float v) noexcept { return linLaw (v, -2.0, 2.0); }
inline double sprayOf (float v) noexcept { return linLaw (v, 0.0, 0.4); }
inline double maskOf (float v) noexcept { return linLaw (v, -1.0, 1.0); }
inline double shiftHz (float v) noexcept { return linLaw (v, -400.0, 400.0); }
inline int resolutionOf (float v) noexcept { return stepOf (v, 5); }
// Live's beat menus: Notes divisions 0…21 and Sync intervals 0…21 (the same table as Live's LFO sync rates, in bars)
inline double divisionBeats (int index) noexcept
{
    static constexpr double bars[22] { 8.0, 6.0, 4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.375, 1.0 / 3.0, 0.3125, 0.25, 0.1875, 1.0 / 6.0, 0.125,
                                       1.0 / 12.0, 0.0625, 1.0 / 24.0, 0.03125, 1.0 / 48.0, 0.015625 };
    return 4.0 * bars[std::clamp (index, 0, 21)];
}
// the delay time in seconds: Time (ms), Notes, 16th, 16th triplets, dotted 16th [Live's Mode menu]
inline double delaySeconds (const float* p, double bpm) noexcept
{
    const auto beat = 60.0 / std::max (bpm, 1.0);
    switch (stepOf (p[timeUnit], 5))
    {
        case 1: return divisionBeats (stepOf (p[divisions], 22)) * beat;
        case 2: return (double) (1 + stepOf (p[sixteenths], 16)) * 0.25 * beat;
        case 3: return (double) (1 + stepOf (p[sixteenths], 16)) * 0.25 * beat * 2.0 / 3.0;
        case 4: return (double) (1 + stepOf (p[sixteenths], 16)) * 0.25 * beat * 1.5;
        default: return timeSeconds (p[time]);
    }
}

// ---- the measured laws ---------------------------------------------------------------------------------------------
// Resolution 0…4 → the analysis frame [MEASURED s1 t08: the impulse's smear and the tones' leakage]
inline int frameSizeOf (int resolution) noexcept { return 256 << std::clamp (resolution, 0, 4); }
constexpr int maxFrame = 4096;
constexpr double maxDelaySeconds = 3.0 + 0.4 + 3.0; // the time, the spray, the tilt's reach (×2 at Tilt 2)

class Engine
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int r = 0; r < 5; ++r)
            stft[(std::size_t) r].prepare (frameSizeOf (r));
        // the spectral delay's ring: frames × bins is the same at every resolution (2 · seconds · rate per channel)
        ringCells = (std::size_t) (2.0 * maxDelaySeconds * rate) + (std::size_t) maxFrame * 8;
        ringL.assign (ringCells, {});
        ringR.assign (ringCells, {});
        const auto bins = maxFrame / 2 + 1;
        for (auto* v : { &frozenMagL, &frozenMagR, &frozenDphL, &frozenDphR, &phaseL, &phaseR, &lastPhL, &lastPhR, &lastMag })
            v->assign ((std::size_t) bins * slots, 0.0f);
        delayOffL.assign ((std::size_t) bins, 0);
        delayOffR.assign ((std::size_t) bins, 0);
        sprayL.assign ((std::size_t) bins, 0.0f);
        sprayR.assign ((std::size_t) bins, 0.0f);
        tmpL.assign ((std::size_t) bins, {});
        tmpR.assign ((std::size_t) bins, {});
        dry.assign ((std::size_t) maxFrame * 2 + 8, {});
        reset();
    }
    void reset() noexcept
    {
        for (auto& s : stft)
            s.reset();
        std::fill (ringL.begin(), ringL.end(), spectral::cpx {});
        std::fill (ringR.begin(), ringR.end(), spectral::cpx {});
        for (auto& f : freezes)
            f = Freeze {};
        frameIndex = 0;
        sinceFreeze = 1.0e9;
        lastFlux = 0.0f;
        manualHeld = false;
        everFrozen = false;
        dryPos = 0;
        std::fill (dry.begin(), dry.end(), std::array<float, 2> {});
        rng.s = 0x2468aceu;
        for (std::size_t k = 0; k < sprayL.size(); ++k)
            sprayL[k] = rng.uni(), sprayR[k] = rng.uni();
    }
    int latency() const noexcept { return frameSizeOf (current); }

    void process (float* l, float* r, int n, const float* p, double bpm) noexcept
    {
        const auto res = resolutionOf (p[resolution]);
        if (res != current)
        {
            current = res;
            stft[(std::size_t) res].reset();
            std::fill (ringL.begin(), ringL.end(), spectral::cpx {});
            std::fill (ringR.begin(), ringR.end(), spectral::cpx {});
            for (auto& f : freezes)
                f = Freeze {};
            dryPos = 0;
            std::fill (dry.begin(), dry.end(), std::array<float, 2> {});
        }
        auto& s = stft[(std::size_t) res];
        const auto N = s.frameSize();
        const auto inGain = std::clamp (p[input], 0.0f, 1.0f);
        const auto wet = std::clamp (p[mix], 0.0f, 1.0f);
        const auto dryGain = (float) dryLaw (wet), wetGain = (float) wetLaw (wet);
        for (int pos = 0; pos < n; pos += block)
        {
            const auto c = std::min (block, n - pos);
            float dl[block], dr[block];
            for (int i = 0; i < c; ++i)
            {
                // the dry path, delayed by the frame so that dry and wet line up (the slot reports the frame as latency)
                auto& d = dry[(std::size_t) dryPos];
                dl[i] = d[0], dr[i] = d[1];
                d = { l[pos + i], r[pos + i] };
                dryPos = (dryPos + 1) % N;
                l[pos + i] *= inGain;
                r[pos + i] *= inGain;
            }
            s.process (l + pos, r + pos, c, [&] (spectral::cpx* L, spectral::cpx* R) { frame (L, R, s, p, bpm); });
            for (int i = 0; i < c; ++i)
            {
                l[pos + i] = l[pos + i] * wetGain + dl[i] * dryGain;
                r[pos + i] = r[pos + i] * wetGain + dr[i] * dryGain;
            }
        }
    }

    // Dry/Wet [MEASURED s1 t07 gdw0.5: MODEL until fitted — a linear crossfade]
    static double dryLaw (double w) noexcept { return 1.0 - w; }
    static double wetLaw (double w) noexcept { return w; }

private:
    static constexpr int slots = 8, block = 256;
    struct Freeze
    {
        bool active = false;
        int slot = 0;
        float level = 0.0f, inStep = 1.0f, outStep = 1.0f;
        double holdLeft = 0.0; // seconds before the fade out starts (crossfade: the interval)
        int stage = 0;          // 0 fading in, 1 holding, 2 fading out
    };

    static float princarg (float x) noexcept
    {
        constexpr auto tp = (float) (2.0 * spectral::pi);
        return x - tp * std::round (x / tp);
    }

    // a new freeze from the current analysis (its magnitudes and true phase advances)
    void capture (const spectral::cpx* L, const spectral::cpx* R, int bins, int hop, int N, double fadeInS, double fadeOutS, double hold,
                  bool envelope) noexcept
    {
        int slot = -1;
        for (int i = 0; i < slots; ++i)
            if (! freezes[(std::size_t) i].active)
            {
                slot = i;
                break;
            }
        if (slot < 0) // all busy: take the quietest
        {
            slot = 0;
            for (int i = 1; i < slots; ++i)
                if (freezes[(std::size_t) i].level < freezes[(std::size_t) slot].level)
                    slot = i;
        }
        if (! envelope) // crossfade mode: the others fade out now
            for (auto& f : freezes)
                if (f.active)
                    f.stage = 2, f.outStep = (float) (hopSeconds (hop) / std::max (fadeInS, hopSeconds (hop)));
        auto* mL = frozenMagL.data() + (std::size_t) slot * (std::size_t) bins;
        auto* mR = frozenMagR.data() + (std::size_t) slot * (std::size_t) bins;
        auto* dL = frozenDphL.data() + (std::size_t) slot * (std::size_t) bins;
        auto* dR = frozenDphR.data() + (std::size_t) slot * (std::size_t) bins;
        auto* pL = phaseL.data() + (std::size_t) slot * (std::size_t) bins;
        auto* pR = phaseR.data() + (std::size_t) slot * (std::size_t) bins;
        for (int k = 0; k < bins; ++k)
        {
            const auto expected = (float) (2.0 * spectral::pi * k * hop / N);
            const auto phL = std::arg (L[k]), phR = std::arg (R[k]);
            mL[k] = std::abs (L[k]);
            mR[k] = std::abs (R[k]);
            dL[k] = expected + princarg (phL - lastPhL[(std::size_t) k] - expected);
            dR[k] = expected + princarg (phR - lastPhR[(std::size_t) k] - expected);
            pL[k] = phL;
            pR[k] = phR;
        }
        auto& f = freezes[(std::size_t) slot];
        f.active = true;
        f.slot = slot;
        f.level = 0.0f;
        f.stage = 0;
        f.inStep = (float) (hopSeconds (hop) / std::max (fadeInS, hopSeconds (hop)));
        f.outStep = (float) (hopSeconds (hop) / std::max (fadeOutS, hopSeconds (hop)));
        f.holdLeft = hold;
    }
    double hopSeconds (int hop) const noexcept { return (double) hop / rate; }

    void freezer (spectral::cpx* L, spectral::cpx* R, int bins, int hop, int N, const float* p, double bpm) noexcept
    {
        const auto hopS = hopSeconds (hop);
        // the onset detector: the spectral flux (positive magnitude changes), against a threshold set by Sensitivity
        float flux = 0.0f, total = 1.0e-9f;
        for (int k = 1; k < bins; ++k)
        {
            const auto m = std::abs (L[k]) + std::abs (R[k]);
            flux += std::max (0.0f, m - lastMag[(std::size_t) k]);
            total += m;
            lastMag[(std::size_t) k] = m;
        }
        const auto onsetValue = flux / total;
        const auto manual = ! on (p[freezeMode]);
        const auto envelope = on (p[fadeType]);
        const auto fadeInS = fadeInSeconds (p[fadeIn]), fadeOutS = fadeOutSeconds (p[fadeOut]);
        sinceFreeze += hopS;
        if (manual)
        {
            const auto want = on (p[freeze]);
            if (want && ! manualHeld)
                capture (L, R, bins, hop, N, fadeInS, fadeOutS, 1.0e12, false);
            if (! want && manualHeld)
                for (auto& f : freezes)
                    if (f.active)
                        f.stage = 2;
            manualHeld = want;
        }
        else
        {
            const auto sync = on (p[retrigger]);
            const auto every = sync ? (on (p[syncUnits]) ? divisionBeats (stepOf (p[intervalBeats], 22)) * 60.0 / std::max (bpm, 1.0)
                                                         : intervalSeconds (p[interval]))
                                    : 0.0;
            bool trigger = false;
            if (sync)
                trigger = sinceFreeze >= every - 0.5 * hopS;
            else
            {
                const auto threshold = 0.6f * std::pow (1.0f - std::clamp (p[sensitivity], 0.0f, 0.999f), 2.0f) + 0.02f;
                trigger = onsetValue > threshold && lastFlux <= threshold && sinceFreeze > 0.05;
                lastFlux = onsetValue;
            }
            if (trigger)
            {
                sinceFreeze = 0.0;
                if (envelope)
                    capture (L, R, bins, hop, N, fadeInS, fadeOutS, 0.0, true);
                else
                {
                    const auto xf = std::max (hopS, (double) std::clamp (p[crossfade], 0.0f, 1.0f) * (sync ? every : 0.1));
                    capture (L, R, bins, hop, N, xf, xf, 1.0e12, false);
                }
            }
        }
        // the input's phases for the next capture
        for (int k = 0; k < bins; ++k)
            lastPhL[(std::size_t) k] = std::arg (L[k]), lastPhR[(std::size_t) k] = std::arg (R[k]);
        // the output: the input until the first freeze, then the sum of the freezes
        bool any = false;
        for (const auto& f : freezes)
            any = any || f.active;
        if (! any)
        {
            if (manual || ! everFrozen)
                return; // (nothing frozen: the input passes)
            for (int k = 0; k < bins; ++k)
                L[k] = R[k] = {};
            return;
        }
        everFrozen = true;
        std::fill (tmpL.begin(), tmpL.begin() + bins, spectral::cpx {});
        std::fill (tmpR.begin(), tmpR.begin() + bins, spectral::cpx {});
        for (auto& f : freezes)
        {
            if (! f.active)
                continue;
            switch (f.stage)
            {
                case 0:
                    f.level = std::min (1.0f, f.level + f.inStep);
                    if (f.level >= 1.0f)
                        f.stage = 1;
                    break;
                case 1:
                    f.holdLeft -= hopS;
                    if (f.holdLeft <= 0.0)
                        f.stage = 2;
                    break;
                default:
                    f.level -= f.outStep;
                    if (f.level <= 0.0f)
                    {
                        f.level = 0.0f;
                        f.active = false;
                    }
                    break;
            }
            if (! f.active)
                continue;
            const auto g = f.level;
            const auto o = (std::size_t) f.slot * (std::size_t) bins;
            for (int k = 0; k < bins; ++k)
            {
                auto& pl = phaseL[o + (std::size_t) k];
                auto& pr = phaseR[o + (std::size_t) k];
                pl = princarg (pl + frozenDphL[o + (std::size_t) k]);
                pr = princarg (pr + frozenDphR[o + (std::size_t) k]);
                tmpL[(std::size_t) k] += std::polar (frozenMagL[o + (std::size_t) k] * g, pl);
                tmpR[(std::size_t) k] += std::polar (frozenMagR[o + (std::size_t) k] * g, pr);
            }
        }
        for (int k = 0; k < bins; ++k)
            L[k] = tmpL[(std::size_t) k], R[k] = tmpR[(std::size_t) k];
    }

    // the per-bin delay in frames: Time, then Tilt (towards highs / lows), Spray (a fixed random spread), Mask (which side
    // they act on) and Stereo (left and right apart) [MEASURED s1 t03…t05]
    void delayTimes (int bins, int hop, int N, const float* p, double bpm, int maxFrames) noexcept
    {
        const auto T = delaySeconds (p, bpm);
        const auto tl = tiltOf (p[tilt]), sp = sprayOf (p[spray]), mk = maskOf (p[mask]), st = (double) std::clamp (p[stereo], 0.0f, 1.0f);
        const auto hopS = hopSeconds (hop);
        for (int k = 0; k < bins; ++k)
        {
            const auto hz = std::max (1.0, (double) k * rate / N);
            const auto x = std::clamp (std::log2 (hz / 20.0) / 10.0, 0.0, 1.0); // 0 at 20 Hz … 1 at 20.48 kHz
            double w = 1.0;
            if (mk > 0.0)
                w = std::clamp ((x - (1.0 - mk)) * 8.0 + 0.5, 0.0, 1.0);
            else if (mk < 0.0)
                w = std::clamp (((-mk) - x) * 8.0 + 0.5, 0.0, 1.0);
            const auto tiltL = tl * (x - 0.5) * T, tiltR = tl * (1.0 - 2.0 * st) * (x - 0.5) * T;
            const auto sprayLs = sp * sprayL[(std::size_t) k], sprayRs = sp * ((1.0 - st) * sprayL[(std::size_t) k] + st * sprayR[(std::size_t) k]);
            const auto dL = T + w * (tiltL + sprayLs), dR = T + w * (tiltR + sprayRs);
            delayOffL[(std::size_t) k] = std::clamp ((int) std::lround (dL / hopS), 1, maxFrames - 1);
            delayOffR[(std::size_t) k] = std::clamp ((int) std::lround (dR / hopS), 1, maxFrames - 1);
        }
    }

    void delay (spectral::cpx* L, spectral::cpx* R, int bins, int hop, int N, const float* p, double bpm) noexcept
    {
        const auto maxFrames = (int) (ringCells / (std::size_t) bins);
        delayTimes (bins, hop, N, p, bpm, maxFrames);
        const auto fb = std::clamp (p[feedback], 0.0f, 1.0f);
        const auto dmix = std::clamp (p[delayMix], 0.0f, 1.0f);
        const auto slot = (std::size_t) (frameIndex % maxFrames);
        // the repeats move by Shift each time round [MEASURED s1 t06]: the read spectrum shifted by Shift in bins, with
        // the phase turning by the shift's hop advance so the moved partial keeps its frequency
        const auto shiftBins = shiftHz (p[shift]) * N / rate;
        const auto whole = (int) std::lround (shiftBins);
        const auto rot = std::polar (1.0f, (float) (2.0 * spectral::pi * (double) whole * (double) hop / N * (double) (frameIndex % 4096)));
        (void) rot;
        for (int k = 0; k < bins; ++k)
        {
            const auto rl = ringL[((std::size_t) ((frameIndex - delayOffL[(std::size_t) k]) % maxFrames + maxFrames) % (std::size_t) maxFrames) * (std::size_t) bins + (std::size_t) k];
            const auto rr = ringR[((std::size_t) ((frameIndex - delayOffR[(std::size_t) k]) % maxFrames + maxFrames) % (std::size_t) maxFrames) * (std::size_t) bins + (std::size_t) k];
            tmpL[(std::size_t) k] = rl;
            tmpR[(std::size_t) k] = rr;
        }
        for (int k = 0; k < bins; ++k)
        {
            const auto from = k - whole;
            spectral::cpx sl {}, sr {};
            if (from > 0 && from < bins - 1)
            {
                // a bin moved by `whole` turns by 2π·whole·hop / N per frame relative to its new place (hop / N = ¼)
                const auto turn = std::polar (1.0f, (float) (spectral::pi * 0.5 * (double) whole * (double) (frameIndex & 3)));
                sl = tmpL[(std::size_t) from] * turn;
                sr = tmpR[(std::size_t) from] * turn;
            }
            ringL[slot * (std::size_t) bins + (std::size_t) k] = L[k] + fb * sl;
            ringR[slot * (std::size_t) bins + (std::size_t) k] = R[k] + fb * sr;
        }
        for (int k = 0; k < bins; ++k)
        {
            L[k] = L[k] * (1.0f - dmix) + tmpL[(std::size_t) k] * dmix;
            R[k] = R[k] * (1.0f - dmix) + tmpR[(std::size_t) k] * dmix;
        }
    }

    void frame (spectral::cpx* L, spectral::cpx* R, const spectral::Stft& s, const float* p, double bpm) noexcept
    {
        const auto N = s.frameSize(), hop = s.hopSize(), bins = s.bins();
        const auto fz = on (p[freezerOn]), dl = on (p[delayOn]);
        if (on (p[order]))
        {
            if (dl)
                delay (L, R, bins, hop, N, p, bpm);
            if (fz)
                freezer (L, R, bins, hop, N, p, bpm);
        }
        else
        {
            if (fz)
                freezer (L, R, bins, hop, N, p, bpm);
            if (dl)
                delay (L, R, bins, hop, N, p, bpm);
        }
        ++frameIndex;
    }

    double rate = 48000.0;
    std::array<spectral::Stft, 5> stft;
    int current = 3;
    std::size_t ringCells = 0;
    std::vector<spectral::cpx> ringL, ringR, tmpL, tmpR;
    std::vector<float> frozenMagL, frozenMagR, frozenDphL, frozenDphR, phaseL, phaseR, lastPhL, lastPhR, lastMag;
    std::vector<int> delayOffL, delayOffR;
    std::vector<float> sprayL, sprayR;
    std::array<Freeze, slots> freezes {};
    std::vector<std::array<float, 2>> dry;
    int dryPos = 0;
    std::int64_t frameIndex = 0;
    double sinceFreeze = 0.0;
    float lastFlux = 0.0f;
    bool manualHeld = false, everFrozen = false;
    spectral::Rand rng;
};
} // namespace lynx::timeprism
