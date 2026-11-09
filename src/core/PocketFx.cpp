#include "core/PocketFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

// FL Studio Mobile's laws, read in round 188h (REA_rodada182c.md "F5 effects, round 188h", with the addresses).

namespace lynx::pocket
{
namespace
{
std::string format (const char* fmt, double v)
{
    char text[48];
    std::snprintf (text, sizeof (text), fmt, v);
    return text;
}
std::string percent (float v) { return format ("%.0f%%", (double) v * 100.0); }

// ---------------------------------------------------------------------------------------------------------------
// Distortion (FLM module 6). Per sample and side: x = in + bias; the curve; a sample-and-hold every `crush` samples;
// × Gain; with a bias, a one-pole high-pass at 220.5/fs takes the offset back out. The Stereo knob shares the drive
// between the sides: left × min(1, 2s), right × min(1, 2(1 − s)).
class Distortion final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        dcCoef = (float) (220.5 / fs);
        reset();
    }
    void reset() noexcept override
    {
        hold = 0.0f;
        held = { 0.0f, 0.0f };
        dc = { 0.0f, 0.0f };
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        update (p);
        for (int i = 0; i < n; ++i)
        {
            float l = left[i], r = right != nullptr ? right[i] : left[i];
            l = shape (l + bias, 0);
            r = shape (r + bias, 1);
            if (hold > 0.0f)
            {
                l = held[0];
                r = held[1];
            }
            else
            {
                held = { l, r };
                hold += crush;
            }
            l *= gain;
            r *= gain;
            hold -= 1.0f;
            if (bias != 0.0f)
            {
                dc[0] = (l - dc[0]) * dcCoef + dc[0];
                l -= dc[0];
                dc[1] = (r - dc[1]) * dcCoef + dc[1];
                r -= dc[1];
            }
            left[i] = l;
            if (right != nullptr)
                right[i] = r;
        }
    }

private:
    float shape (float x, int side) const noexcept
    {
        switch (kind)
        {
            case 0: return (x * g[side]) / (std::abs (x) * k[side] + 1.0f);
            case 1: return std::tanh (x * k[side]) * g[side];
            case 2:
            {
                const auto y = x >= 0.0f ? 1.0f - std::pow (10.0f, -(x * k[side])) * (1.0f - x)
                                         : std::pow (10.0f, x * k[side]) * (x + 1.0f) - 1.0f;
                return g[side] * y;
            }
            default: return std::clamp (x * k[side], -1.0f, 1.0f);
        }
    }
    void update (const EffectParams& p) noexcept
    {
        std::array<float, distortionParams> now {};
        std::copy_n (p.begin(), distortionParams, now.begin());
        if (now == last)
            return;
        last = now;
        const auto drive = now[0];
        const auto c = std::clamp (now[1] * now[1] * now[1] * now[1], 0.0f, 1.0f);
        crush = c * 256.0f + 1.0f;
        gain = std::pow (10.0f, now[2] * 24.0f * 0.05f - 1.2f);
        kind = distortionKind (now[4]);
        const auto fl = std::min (now[3] * 2.0f, 1.0f);
        const auto fr = std::min ((1.0f - now[3]) + (1.0f - now[3]), 1.0f);
        const auto b = (now[5] - 0.5f) + (now[5] - 0.5f);
        bias = b * b * b * b * b;
        const float f[2] { fl, fr };
        for (int s = 0; s < 2; ++s)
            switch (kind)
            {
                case 0:
                    g[s] = std::pow (10.0f, drive * f[s] * 5.0f);
                    k[s] = g[s] - 1.0f;
                    break;
                case 1:
                    k[s] = std::pow (10.0f, drive * f[s] * 4.0f) - 0.99f;
                    g[s] = 1.0f / std::tanh (k[s]);
                    break;
                case 2:
                    g[s] = 1.0f;
                    k[s] = drive * drive * 20.0f * f[s];
                    break;
                default:
                    k[s] = std::pow (10.0f, drive * drive * f[s] * 3.0f);
                    g[s] = 1.0f;
                    break;
            }
    }

    double fs = 44100.0;
    float dcCoef = 0.005f;
    std::array<float, distortionParams> last { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    int kind = 0;
    float k[2] {}, g[2] { 1.0f, 1.0f }, crush = 1.0f, gain = 1.0f, bias = 0.0f;
    float hold = 0.0f;
    std::array<float, 2> held {}, dc {};
};

// ---------------------------------------------------------------------------------------------------------------
// Limiter (legacy) (FLM module 5): a peak limiter with no look-ahead. The louder side's |x| is the level; above the
// threshold the gain drops at once to threshold/level; it then recovers over 1/rate seconds along
// g = 1 − cos²(πt/2)·(1 − g₀), rate = 100·(0.2 − 0.2v)² + 1 per second. Output = g × (output gain / threshold).
class LimiterClassic final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }
    void reset() noexcept override
    {
        t = 1.0f;
        g0 = 1.0f;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        const auto threshold = std::pow (10.0f, p[0] * 1.2f - 1.2f);
        const auto r = 0.19999999f - p[1] * 0.2f;
        const auto rate = r * r * 100.0f + 1.0f;
        const auto out = p[2] * 36.0f - 24.0f < -60.0f ? 0.0f : std::pow (10.0f, p[2] * 1.8f - 1.2f);
        const auto step = (float) ((double) rate / fs);
        const auto makeUp = out / threshold;
        for (int i = 0; i < n; ++i)
        {
            const auto level = std::max (std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f);
            const auto s = std::sin (t * 1.5707964f);
            auto g = 1.0f - (1.0f - s * s) * (1.0f - g0);
            if (threshold < g * level)
            {
                t = 0.0f;
                g = threshold / level;
                g0 = g;
            }
            g *= makeUp;
            left[i] *= g;
            if (right != nullptr)
                right[i] *= g;
            t = std::min (step + t, 1.0f);
        }
    }

private:
    double fs = 44100.0;
    float t = 1.0f, g0 = 1.0f;
};
} // namespace

const char* distortionKindName (int kind) noexcept
{
    static const char* const names[] { "Soft", "Tanh", "Exponential", "Hard clip" };
    return names[std::clamp (kind, 0, 3)];
}
int distortionKind (float v) noexcept { return std::clamp ((int) (v * 3.0f), 0, 3); }

std::unique_ptr<Effect> makeDistortion() { return std::make_unique<Distortion>(); }
std::unique_ptr<Effect> makeLimiterClassic() { return std::make_unique<LimiterClassic>(); }

std::string distortionParamText (int param, float v)
{
    switch (param)
    {
        case 2: return format ("%.1f dB", (double) (v * 24.0f - 24.0f));
        case 3: return format ("%.0f", (double) (int) ((v - 0.5f) * 200.0f));
        case 4: return distortionKindName (distortionKind (v));
        case 5: return format ("%.0f", (double) (int) (v * 200.0f - 100.0f));
        default: return percent (v);
    }
}

std::string limiterClassicParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.1f dB", (double) (v * 24.0f - 24.0f));
        case 1:
        {
            const auto r = 0.19999999f - v * 0.2f;
            return format ("%.0f ms", 1000.0 / (double) (r * r * 100.0f + 1.0f));
        }
        case 2: return v * 36.0f - 24.0f < -60.0f ? std::string ("Off") : format ("%.1f dB", (double) (v * 36.0f - 24.0f));
        default: return percent (v);
    }
}
} // namespace lynx::pocket
