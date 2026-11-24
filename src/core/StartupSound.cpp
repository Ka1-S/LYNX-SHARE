#include "core/StartupSound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lynx::startup
{
namespace
{
constexpr double twoPi = 6.283185307179586;

// deterministic white noise in [-1, 1] (the same sound on every start)
struct Noise
{
    std::uint32_t state = 0x1a2b3c4du;

    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) ((double) (state >> 8) / 8388608.0 - 1.0);
    }
};

// Chamberlin state-variable filter (band-pass output)
struct BandPass
{
    double low = 0.0, band = 0.0;

    float process (float x, double frequency, double q, double sampleRate) noexcept
    {
        const auto f = 2.0 * std::sin (3.141592653589793 * std::min (frequency, sampleRate * 0.2) / sampleRate);
        low += f * band;
        const auto high = (double) x - low - band / q;
        band += f * high;
        return (float) band;
    }
};

// one plucked string (Karplus-Strong with a fractional delay read) plus a sine "sheen"
void addPluck (std::vector<float>& left, std::vector<float>& right, double sampleRate, double start, double frequency,
               float gain, float pan, Noise& noise)
{
    const auto first = (std::size_t) (start * sampleRate);
    const auto period = sampleRate / frequency;
    const auto size = (std::size_t) period + 4;
    std::vector<float> ring (size, 0.0f);

    // excitation: a burst of low-passed noise one period long
    float smooth = 0.0f;
    for (std::size_t i = 0; i < size; ++i)
    {
        smooth += 0.55f * (noise.next() - smooth);
        ring[i] = smooth;
    }

    const auto panL = std::sqrt (0.5f * (1.0f - pan)), panR = std::sqrt (0.5f * (1.0f + pan));
    const auto decay = std::pow (0.001, 1.0 / (0.9 * frequency)); // about -60 dB after 0.9 s
    std::size_t write = 0;
    float previous = 0.0f;

    for (auto n = first; n < left.size(); ++n)
    {
        // read `period` samples back (linear interpolation)
        const auto readPos = (double) write - period + (double) size;
        const auto i0 = (std::size_t) readPos % size, i1 = (i0 + 1) % size;
        const auto frac = (float) (readPos - std::floor (readPos));
        const auto delayed = ring[i0] + frac * (ring[i1] - ring[i0]);
        const auto out = (float) (decay * 0.5 * (delayed + previous)); // averaging = the string's damping
        previous = delayed;
        ring[write] = out;
        write = (write + 1) % size;

        const auto t = (double) (n - first) / sampleRate;
        const auto sheen = (float) (0.22 * std::sin (twoPi * frequency * t) * std::exp (-t / 0.45)
                                    + 0.05 * std::sin (twoPi * frequency * 2.76 * t) * std::exp (-t / 0.12));
        const auto s = gain * (out + sheen);
        left[n] += s * panL;
        right[n] += s * panR;
    }
}
} // namespace

StereoSound makeSound (double sampleRate)
{
    StereoSound sound;
    sound.sampleRate = sampleRate;
    const auto frames = (std::size_t) (soundLength * sampleRate);
    auto& left = sound.left;
    auto& right = sound.right;
    left.assign (frames, 0.0f);
    right.assign (frames, 0.0f);
    Noise noise;

    // 1. whoosh: band-passed noise sweeping up while it swells, cut short at the landing
    {
        BandPass bandL, bandR;
        Noise noiseR;
        noiseR.state = 0x5eed1234u;

        for (std::size_t n = 0; n < frames; ++n)
        {
            const auto t = (double) n / sampleRate;
            double env = 0.0;

            if (t < impactTime)
                env = std::pow (t / impactTime, 2.2);
            else
                env = std::exp (-(t - impactTime) / 0.035);

            if (env < 1.0e-5 && t > impactTime)
                break;

            const auto centre = 350.0 * std::pow (3200.0 / 350.0, std::min (1.0, t / impactTime));
            left[n] += (float) (0.55 * env) * bandL.process (noise.next(), centre, 1.6, sampleRate);
            right[n] += (float) (0.55 * env) * bandR.process (noiseR.next(), centre * 1.08, 1.6, sampleRate);
        }
    }

    // 2. the landing: a short pitched-down thump with a soft click on top
    {
        const auto first = (std::size_t) (impactTime * sampleRate);
        double phase = 0.0;
        float click = 0.0f;

        for (auto n = first; n < frames; ++n)
        {
            const auto t = (double) (n - first) / sampleRate;

            if (t > 0.3)
                break;

            const auto frequency = 70.0 + 170.0 * std::exp (-t / 0.018);
            phase += twoPi * frequency / sampleRate;
            click += 0.35f * (noise.next() - click);
            const auto s = (float) (0.9 * std::sin (phase) * std::exp (-t / 0.055)) + click * (float) (0.5 * std::exp (-t / 0.003));
            left[n] += s;
            right[n] += s;
        }
    }

    // 3. the chime: D, A and the D above, plucked one after another and spread across the stereo field
    addPluck (left, right, sampleRate, impactTime + 0.012, 587.33, 0.55f, -0.35f, noise);
    addPluck (left, right, sampleRate, impactTime + 0.082, 880.00, 0.42f, 0.35f, noise);
    addPluck (left, right, sampleRate, impactTime + 0.152, 1174.66, 0.32f, 0.0f, noise);

    // 4. a light ping-pong echo (darkened in its feedback)
    {
        const auto delay = (std::size_t) (0.165 * sampleRate);
        std::vector<float> echoL (frames, 0.0f), echoR (frames, 0.0f);
        float toneL = 0.0f, toneR = 0.0f;

        for (std::size_t n = delay; n < frames; ++n)
        {
            toneL += 0.45f * ((right[n - delay] + 0.4f * echoR[n - delay]) - toneL);
            toneR += 0.45f * ((left[n - delay] + 0.4f * echoL[n - delay]) - toneR);
            echoL[n] = toneL;
            echoR[n] = toneR;
        }

        for (std::size_t n = 0; n < frames; ++n)
        {
            left[n] += 0.22f * echoL[n];
            right[n] += 0.22f * echoR[n];
        }
    }

    // 5. fade out the tail and set the level
    const auto fade = (std::size_t) (0.25 * sampleRate);
    for (std::size_t i = 0; i < fade && i < frames; ++i)
    {
        const auto g = (float) i / (float) fade;
        left[frames - 1 - i] *= g;
        right[frames - 1 - i] *= g;
    }

    float peak = 0.0f;
    for (std::size_t n = 0; n < frames; ++n)
        peak = std::max ({ peak, std::abs (left[n]), std::abs (right[n]) });

    if (peak > 0.0f)
        for (std::size_t n = 0; n < frames; ++n)
        {
            left[n] *= peakLevel / peak;
            right[n] *= peakLevel / peak;
        }

    return sound;
}
StereoSound makeDoneSound (double sampleRate)
{
    StereoSound sound;
    sound.sampleRate = sampleRate;
    const auto frames = (std::size_t) (0.9 * sampleRate);
    sound.left.assign (frames, 0.0f);
    sound.right.assign (frames, 0.0f);
    Noise noise;
    noise.state = 0x0d0e5eedu;
    addPluck (sound.left, sound.right, sampleRate, 0.0, 587.33, 0.5f, -0.3f, noise);
    addPluck (sound.left, sound.right, sampleRate, 0.06, 880.00, 0.42f, 0.3f, noise);
    addPluck (sound.left, sound.right, sampleRate, 0.12, 1174.66, 0.36f, 0.0f, noise);
    const auto fade = (std::size_t) (0.2 * sampleRate);
    float peak = 0.0f;
    for (std::size_t n = 0; n < frames; ++n)
    {
        if (n + fade >= frames)
        {
            const auto g = (float) (frames - 1 - n) / (float) fade;
            sound.left[n] *= g;
            sound.right[n] *= g;
        }
        peak = std::max ({ peak, std::abs (sound.left[n]), std::abs (sound.right[n]) });
    }
    if (peak > 0.0f)
        for (std::size_t n = 0; n < frames; ++n)
        {
            sound.left[n] *= peakLevel / peak;
            sound.right[n] *= peakLevel / peak;
        }
    return sound;
}
} // namespace lynx::startup
