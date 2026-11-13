#include "core/Mastering.h"

#include <algorithm>
#include <cmath>

#include "core/ApexLimiter.h"
#include "core/Effects.h"
#include "core/Loudness.h"

namespace lynx::mastering
{
const std::vector<double>& targets()
{
    static const std::vector<double> t { -9.0, -11.0, -14.0, -16.0, -23.0 };
    return t;
}

void measure (const std::vector<float>& left, const std::vector<float>& right, double sampleRate, double& lufs, double& truePeakDb)
{
    loudness::Meter meter;
    meter.prepare (sampleRate);
    const auto n = std::min (left.size(), right.size());
    constexpr std::size_t block = 4096;
    for (std::size_t i = 0; i < n; i += block)
        meter.process (left.data() + i, right.data() + i, (int) std::min (block, n - i));
    meter.flushTruePeak(); // (round 190co: the estimator answers 68 samples late; the file's last samples count too)
    lufs = meter.integratedLoudness();
    truePeakDb = meter.truePeakDb();
}

namespace
{
// `gainDb` then Apex Limiter (true peak, the ceiling) over a copy of the input, with the limiter's latency removed
void limit (const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR,
            double sampleRate, double gainDb, double ceilingDb)
{
    const auto n = std::min (inL.size(), inR.size());
    auto limiter = apex::make();
    limiter->prepare (sampleRate);
    EffectParams params {};
    const auto& info = apex::info();
    for (std::size_t p = 0; p < info.params.size() && p < params.size(); ++p)
        params[p] = info.params[p].defaultValue;
    const auto boost = std::clamp (gainDb, 0.0, 30.0);
    const auto cut = (float) std::pow (10.0, std::min (gainDb, 0.0) / 20.0); // (the limiter's gain only boosts)
    params[apex::pGain] = (float) (boost / 30.0);
    params[apex::pCeiling] = (float) std::clamp (1.0 + ceilingDb / 30.0, 0.0, 1.0);
    params[apex::pTruePeak] = 1.0f;
    params[apex::pStyle] = 0.0f; // clean
    params[apex::pDither] = 0.0f;

    const auto latency = (std::size_t) std::max (0, limiter->latencySamples());
    outL.assign (n, 0.0f);
    outR.assign (n, 0.0f);
    constexpr std::size_t block = 512;
    std::vector<float> l (block), r (block);
    for (std::size_t done = 0; done < n + latency; done += block)
    {
        const auto count = std::min (block, n + latency - done);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto at = done + i;
            l[i] = at < n ? inL[at] * cut : 0.0f;
            r[i] = at < n ? inR[at] * cut : 0.0f;
        }
        limiter->process (l.data(), r.data(), (int) count, params, 120.0);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto at = done + i;
            if (at >= latency && at - latency < n)
            {
                outL[at - latency] = l[i];
                outR[at - latency] = r[i];
            }
        }
    }
}
} // namespace

Result process (std::vector<float>& left, std::vector<float>& right, double sampleRate, const Settings& settings)
{
    Result result;
    double tp = 0.0;
    measure (left, right, sampleRate, result.inputLufs, tp);
    if (result.inputLufs <= loudness::silence + 1.0 || left.empty())
    {
        result.outputLufs = result.inputLufs;
        result.outputTruePeakDb = tp;
        return result;
    }

    // the limiter takes loudness away the harder it works: find the gain from the measured results (a secant search
    // on gain → loudness, a few passes at most)
    auto gain = settings.targetLufs - result.inputLufs;
    double prevGain = 0.0, prevLufs = result.inputLufs;
    std::vector<float> outL, outR;
    for (int pass = 0; pass < 8; ++pass)
    {
        limit (left, right, outL, outR, sampleRate, gain, settings.ceilingDb);
        measure (outL, outR, sampleRate, result.outputLufs, result.outputTruePeakDb);
        result.gainDb = gain;
        const auto error = settings.targetLufs - result.outputLufs;
        if (std::abs (error) < 0.1 || gain >= 30.0)
            break;
        const auto slope = (result.outputLufs - prevLufs) / (gain - prevGain); // dB of loudness per dB of gain
        prevGain = gain;
        prevLufs = result.outputLufs;
        const auto step = error / std::clamp (std::isfinite (slope) ? slope : 1.0, 0.1, 1.0);
        gain = std::min (30.0, gain + std::clamp (step, -12.0, 12.0));
    }
    left.swap (outL);
    right.swap (outR);
    return result;
}
} // namespace lynx::mastering
