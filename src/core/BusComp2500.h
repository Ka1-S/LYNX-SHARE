#pragma once

// Round 190mm (sub-round dyn): LYNX's model of a feed-forward / feedback VCA bus compressor with "thrust" (the API-2500
// role). Clean-room: the reference's DSP was read for its structure only (docs/analise/REA_rodada190mm_dyn.md §4: a
// side-chain high-pass, a mean-square detector in the log domain, a soft knee, dB-domain ballistics, the feedback
// option that detects the compressed output, an automatic make-up from the threshold); this engine is our own, much
// simpler circuit-free version of it, every constant fitted on our demo-mode probes (§4.2). Header-only.

#include <algorithm>
#include <array>
#include <cmath>

namespace lynx::buscomp
{
// ---- fitted constants (REA_rodada190mm_dyn.md §4; lab/fitapi.py: 18 bank renders, mean ESR −8.9 → −18.9 dB)
struct Fit
{
    double calDb = -23.4598;                             // the threshold knob → detector dBFS
    double kneeDb[3] { 23.7520, 10.8522, 12.9270 };      // Soft, Med, Hard knee widths (dB)
    double attackScale = 2.26207, releaseScale = 0.364396; // the knobs' times × these
    double detectMs = 9.01427;                           // the mean-square detector
    double thrustHz = 76.8663, thrust[3] { 0.0, 0.628681, 0.564657 }; // Norm, Med, Loud: the side chain's high-pass share
    double makeupK = 0.337269, makeupRefDb = 9.76324;    // auto make-up dB = K · (ref − threshold) · (1 − 1 / ratio)
};

struct Settings
{
    double thresholdDb = 10.0;   // +10 … −20 (the knob)
    double ratio = 4.0;          // 1.5 … ∞ (1e6)
    double attackMs = 1.0, releaseS = 0.5;
    int knee = 2;                // 0 Soft, 1 Med, 2 Hard
    int thrust = 0;              // 0 Norm, 1 Med, 2 Loud
    bool feedback = false;       // the "Old" type
    bool autoMakeup = true;
    double makeupDb = 0.0, mix = 1.0, trimDb = 0.0;
};

class Engine
{
public:
    void prepare (double sampleRate, const Fit& f = {}) noexcept
    {
        sr = sampleRate;
        fit = f;
        reset();
    }
    void reset() noexcept
    {
        ms = 0.0;
        env = 0.0;
        lp[0] = lp[1] = 0.0;
        prev[0] = prev[1] = 0.0;
        last = 0.0f;
    }
    float reductionDb() const noexcept { return last; }

    void process (float* l, float* r, int n, const Settings& st) noexcept
    {
        const double cDet = std::exp (-1000.0 / (std::max (0.01, fit.detectMs) * sr));
        const double cA = std::exp (-1000.0 / (std::max (0.001, st.attackMs * fit.attackScale) * sr));
        const double cR = std::exp (-1.0 / (std::max (0.001, st.releaseS * fit.releaseScale) * sr));
        const double cLp = std::exp (-2.0 * 3.14159265358979323846 * fit.thrustHz / sr);
        const double share = fit.thrust[std::clamp (st.thrust, 0, 2)];
        const double W = fit.kneeDb[std::clamp (st.knee, 0, 2)];
        const double slope = st.ratio >= 1e5 ? 1.0 : 1.0 - 1.0 / std::max (1.0, st.ratio);
        const double thr = st.thresholdDb + fit.calDb;
        const double mkDb = (st.autoMakeup ? fit.makeupK * (fit.makeupRefDb - st.thresholdDb) * slope : 0.0) + st.makeupDb;
        const double makeup = std::pow (10.0, mkDb / 20.0), trim = std::pow (10.0, st.trimDb / 20.0);
        const double mix = std::clamp (st.mix, 0.0, 1.0);
        double most = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x[2] { l[i], r[i] };
            double p = 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const double s = st.feedback ? prev[c] : x[c];
                lp[c] = s + (lp[c] - s) * cLp;
                const double h = s - share * lp[c];
                p += 0.5 * h * h;
            }
            ms = p + (ms - p) * cDet;
            const double over = 10.0 * std::log10 (ms + 1e-30) - thr;
            double red = 0.0;
            if (W > 0.0 && std::abs (over) <= 0.5 * W)
                red = slope * (over + 0.5 * W) * (over + 0.5 * W) / (2.0 * W);
            else if (over > 0.0)
                red = slope * over;
            env = red + (env - red) * (red > env ? cA : cR);
            const double g = std::pow (10.0, -env / 20.0);
            most = std::max (most, env);
            for (int c = 0; c < 2; ++c)
            {
                prev[c] = x[c] * g;
                (c ? r : l)[i] = (float) (trim * x[c] * (mix * g * makeup + (1.0 - mix)));
            }
        }
        last = (float) most;
    }

private:
    double sr = 48000.0;
    Fit fit;
    double ms = 0.0, env = 0.0, lp[2] {}, prev[2] {};
    float last = 0.0f;
};
} // namespace lynx::buscomp
