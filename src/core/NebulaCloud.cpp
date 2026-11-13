// Nebula's cloud networks: a C++ port of the Faust library function `re.valhallaSupermassive`
// (grame-cncm/faustlibraries, reverbs.lib; author David Braun; MIT licence — the full notice is in NebulaCloud.h and
// THIRD_PARTY.md).

#include "core/NebulaCloud.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Denormals.h"

namespace lynx::verbs::cloud
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int maxLines = 16;
constexpr double fixedOffset = 33.0;  // samples at every rate: the model keeps the reference's own offset
constexpr int outputOffset = 32;      // samples, serial networks; also added to the looped network's first stage
constexpr double glide = 0.25;        // samples per sample: a length change glides like tape (as the classic network)
constexpr std::int32_t phaseMask = 0x3fffffff; // the modulator's 30-bit phase accumulator
constexpr double phaseSpan = 1073741824.0;
// which quarter of the modulator's turn each line of the four-channel all-pass networks follows
constexpr int quadrant[maxLines] = { 0, 2, 1, 3, 1, 3, 0, 2, 2, 0, 3, 1, 3, 1, 2, 0 };
} // namespace

struct Network::Impl
{
    double rate = 48000.0, maxDelay = 2.0 * 48000.0;
    int mask = 0, pos = 0;
    std::vector<float> memory;
    float* line[maxLines] {};

    Topology topology = Topology::none;
    bool primed = false;

    // per line: the power of the warp ratio and the extra samples of its length, its place on the modulator's turn
    int exponent[maxLines] {};
    double extra[maxLines] {};
    double modCos[maxLines] {}, modSin[maxLines] {};
    double target[maxLines] {}, length[maxLines] {};
    float reader[maxLines] {}; // state of each line's all-pass interpolator

    float loop[maxLines] {};               // what the lines returned last (the feedback networks)
    float stageOut[8][4] {}, stageIn[8][4] {}; // the matrix all-passes' last output and input
    struct Cut
    {
        float x1 = 0.0f, high = 0.0f, low = 0.0f;
    };
    Cut cut[4];
    float held[2][64] {}; // the serial networks' output offset
    int heldPos = 0;
    float lastLeft = 0.0f, lastRight = 0.0f;

    // modulator: one quadrature oscillator, each part low-passed at 0.25 Hz
    std::int32_t phase = 0, phaseStep = 0;
    double oscSin = 0.0, oscCos = 1.0, turnSin = 0.0, turnCos = 1.0;
    double lowB = 0.0, lowA = 0.0, sinIn = 0.0, sinOut = 0.0, cosIn = 0.0, cosOut = 0.0;
    double amount = 0.0, amountGoal = 0.0;

    // glided coefficients
    struct Coefficients
    {
        float gain = 0.0f, inject = 0.0f, allpass = 0.0f, feedback = 0.0f; // loop gain √F, input gain, 0.91 F, F
        float cosT = 1.0f, sinT = 0.0f;                                    // scattering angle
        float cosW = 1.0f, sinW = 0.0f;                                    // width
        float highPole = 0.0f, lowPole = 0.0f;                             // the cuts' poles
    };
    Coefficients now, goal;
    float smoothing = 0.0f;

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        maxDelay = 2.09 * rate;
        int size = 1;
        while (size < (int) (2.2 * rate) + 8)
            size <<= 1;
        mask = size - 1;
        memory.assign ((std::size_t) size * maxLines, 0.0f);
        for (int i = 0; i < maxLines; ++i)
            line[i] = memory.data() + (std::size_t) i * (std::size_t) size;
        const auto k = std::tan (pi * 0.25 / rate);
        lowB = k / (1.0 + k);
        lowA = (k - 1.0) / (k + 1.0);
        smoothing = (float) (1.0 - std::exp (-1.0 / (0.005 * rate)));
        clear();
    }

    void clear() noexcept
    {
        std::fill (memory.begin(), memory.end(), 0.0f);
        pos = 0;
        std::fill (std::begin (reader), std::end (reader), 0.0f);
        std::fill (std::begin (loop), std::end (loop), 0.0f);
        for (auto& s : stageOut)
            std::fill (std::begin (s), std::end (s), 0.0f);
        for (auto& s : stageIn)
            std::fill (std::begin (s), std::end (s), 0.0f);
        for (auto& c : cut)
            c = {};
        for (auto& h : held)
            std::fill (std::begin (h), std::end (h), 0.0f);
        heldPos = 0;
        lastLeft = lastRight = 0.0f;
        phase = 0;
        sinIn = sinOut = cosIn = cosOut = 0.0;
        primed = false;
    }

    void place (Topology t) noexcept
    {
        topology = t;
        for (int i = 0; i < maxLines; ++i)
        {
            exponent[i] = 0;
            extra[i] = 0.0;
            auto angle = 0.0;
            switch (t)
            {
                case Topology::fdn16:
                    exponent[i] = i;
                    angle = 2.0 * pi * i / 16.0;
                    break;
                case Topology::fdn4:
                case Topology::fdn4Filtered:
                    exponent[i] = 4 * (i & 3);
                    angle = 0.5 * pi * (i & 3);
                    break;
                case Topology::serialQuad:
                case Topology::loopedQuad:
                {
                    // line (stage s, channel c) is the model's line 4 c + s
                    const auto s = i >> 2, c = i & 3;
                    exponent[i] = 4 * c + s;
                    angle = 0.5 * pi * quadrant[4 * c + s];
                    if (t == Topology::loopedQuad && s == 0)
                        extra[i] = outputOffset;
                    break;
                }
                case Topology::serialStereo:
                    // line (stage s, channel c) is the model's line 2 s + c
                    exponent[i] = i;
                    angle = pi * (i >> 1) / 4.0 + pi * (i & 1);
                    break;
                case Topology::none: break;
            }
            modCos[i] = std::cos (angle);
            modSin[i] = std::sin (angle);
        }
    }

    void aim (const Controls& c) noexcept
    {
        const auto warp = std::clamp (c.warp, 0.0, 1.0);
        const auto ratio = 1.0 - warp * warp * warp / std::sqrt (8.0);
        const auto samples = std::clamp (c.delayMs, 0.0, 2000.0) * 0.001 * rate;
        double power[maxLines];
        power[0] = 1.0;
        for (int i = 1; i < maxLines; ++i)
            power[i] = power[i - 1] * ratio;
        for (int i = 0; i < maxLines; ++i)
            target[i] = samples * power[exponent[i]] + fixedOffset + extra[i];

        const auto feedback = std::clamp (c.feedback, 0.0, 1.0);
        const auto gain = std::min (0.9995, std::sqrt (feedback)); // (the cap keeps a network at Feedback 100 % decaying)
        goal.gain = (float) gain;
        goal.inject = (float) std::sqrt (std::min (1.0, 0.2 / std::max (1e-6, gain)));
        goal.allpass = (float) (0.91 * feedback);
        // (LYNX: the looped network's global gain is capped like the others'. The model lets it reach 1 and sustain; its
        // moving interpolators are not passive, so an uncapped loop is not guaranteed to stay bounded)
        goal.feedback = (float) std::min (0.9995, feedback);
        const auto theta = pi * std::clamp (c.density, 0.0, 1.0) / 4.0;
        goal.cosT = (float) std::cos (theta);
        goal.sinT = (float) std::sin (theta);
        const auto wide = (1.0 - std::clamp (c.width, -1.0, 1.0)) * pi / 4.0;
        goal.cosW = (float) std::cos (wide);
        goal.sinW = (float) std::sin (wide);
        goal.highPole = (float) std::exp (-2.0 * pi * std::clamp (c.lowCutHz, 10.0, 2000.0) / rate);
        goal.lowPole = (float) std::exp (-2.0 * pi * std::clamp (c.highCutHz, 200.0, 20000.0) / rate);

        const auto depth = std::clamp (c.modDepth, 0.0, 1.0);
        amountGoal = (500.0 / 48000.0) * rate * depth * depth;
        phaseStep = (std::int32_t) (std::clamp (c.modRateHz, 0.01, 10.0) * phaseSpan / rate + 0.5);
        const auto at = 2.0 * pi * (double) phase / phaseSpan, turn = 2.0 * pi * (double) phaseStep / phaseSpan;
        oscSin = std::sin (at);
        oscCos = std::cos (at);
        turnSin = std::sin (turn);
        turnCos = std::cos (turn);

        if (! primed)
        {
            primed = true;
            now = goal;
            amount = amountGoal;
            std::copy (std::begin (target), std::end (target), std::begin (length));
        }
    }

    // one sample of the modulator and of the glided coefficients
    inline void advance (double& modSine, double& modCosine) noexcept
    {
        phase = (phase + phaseStep) & phaseMask;
        const auto s = oscSin * turnCos + oscCos * turnSin, c = oscCos * turnCos - oscSin * turnSin;
        oscSin = s;
        oscCos = c;
        sinOut = lowB * (s + sinIn) - lowA * sinOut;
        sinIn = s;
        cosOut = lowB * (c + cosIn) - lowA * cosOut;
        cosIn = c;
        amount += std::clamp (amountGoal - amount, -glide, glide);
        modSine = sinOut * amount;
        modCosine = cosOut * amount;

        const auto k = smoothing;
        now.gain += k * (goal.gain - now.gain);
        now.inject += k * (goal.inject - now.inject);
        now.allpass += k * (goal.allpass - now.allpass);
        now.feedback += k * (goal.feedback - now.feedback);
        now.cosT += k * (goal.cosT - now.cosT);
        now.sinT += k * (goal.sinT - now.sinT);
        now.cosW += k * (goal.cosW - now.cosW);
        now.sinW += k * (goal.sinW - now.sinW);
        now.highPole += k * (goal.highPole - now.highPole);
        now.lowPole += k * (goal.lowPole - now.lowPole);
    }

    // one sample of `count` lines from `first`: writes in[], returns what each line holds `length + modulation`
    // samples back, read through a moving first-order all-pass interpolator (it keeps the loop's magnitude)
    template <int count>
    inline void lines (int first, const float* in, float* out, double modSine, double modCosine) noexcept
    {
        for (int k = 0; k < count; ++k)
        {
            const auto i = first + k;
            length[i] += std::clamp (target[i] - length[i], -glide, glide);
            // (the loop itself is one sample long, hence the −1)
            const auto d = std::clamp (length[i] + modSine * modCos[i] + modCosine * modSin[i] - 1.0, 2.0, maxDelay);
            const auto whole = (int) (d - 0.49999);
            const auto part = (float) (d - whole);
            const auto a = (1.0f - part) / (1.0f + part);
            auto* buffer = line[i];
            buffer[pos] = in[k];
            out[k] = reader[i] = a * buffer[(pos - whole) & mask] + buffer[(pos - whole - 1) & mask] - a * reader[i];
        }
    }

    // R(θ) ⊗ R(θ) on four channels in the order L, R, L, R
    inline void scatter (float* v) const noexcept
    {
        const auto cc = now.cosT * now.cosT, ss = now.sinT * now.sinT, cs = now.cosT * now.sinT;
        const auto a = v[0], b = v[1], c = v[2], d = v[3];
        v[0] = cc * a - cs * b - cs * c + ss * d;
        v[1] = cs * a + cc * b - ss * c - cs * d;
        v[2] = cs * a - ss * b + cc * c - cs * d;
        v[3] = ss * a + cs * b + cs * c + cc * d;
    }

    // zero at DC with a pole (the low cut), then a unity-DC one-pole low-pass (the high cut)
    inline float filter (Cut& c, float x) const noexcept
    {
        c.high = (x - c.x1) + now.highPole * c.high;
        c.x1 = x;
        return c.low = (1.0f - now.lowPole) * c.high + now.lowPole * c.low;
    }

    inline void emit (float l, float r, bool offset, bool cuts, float& outLeft, float& outRight) noexcept
    {
        if (offset)
        {
            held[0][heldPos] = l;
            held[1][heldPos] = r;
            l = held[0][(heldPos - outputOffset) & 63];
            r = held[1][(heldPos - outputOffset) & 63];
            heldPos = (heldPos + 1) & 63;
        }
        if (cuts)
        {
            l = filter (cut[0], l);
            r = filter (cut[1], r);
        }
        outLeft = now.cosW * l + now.sinW * r;
        outRight = now.cosW * r + now.sinW * l;
    }

    // a matrix all-pass over four channels: v = S (lines (a y + x')), y = v − a x, with y fed back
    inline void quadAllpass (int stage, int firstLine, float* x, double modSine, double modCosine) noexcept
    {
        float in[4], v[4];
        for (int i = 0; i < 4; ++i)
            in[i] = now.allpass * stageOut[stage][i] + stageIn[stage][i];
        lines<4> (firstLine, in, v, modSine, modCosine);
        scatter (v);
        for (int i = 0; i < 4; ++i)
        {
            stageIn[stage][i] = x[i];
            x[i] = stageOut[stage][i] = v[i] - now.allpass * x[i];
        }
    }

    void runFdn16 (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        static const float norm = (float) (1.0 / std::sqrt (8.0));
        for (int k = 0; k < n; ++k)
        {
            double ms, mc;
            advance (ms, mc);
            const auto xl = inL[k], xr = inR[k];
            // four butterfly stages, each rotating the lines whose index differs in one bit
            float t[maxLines], u[maxLines];
            const auto c = now.cosT, s = now.sinT;
            for (int i = 0; i < 16; i += 2)
            {
                u[i] = c * loop[i] - s * loop[i + 1];
                u[i + 1] = c * loop[i + 1] + s * loop[i];
            }
            for (int i = 0; i < 16; i += 4)
                for (int j = 0; j < 2; ++j)
                {
                    t[i + j] = c * u[i + j] - s * u[i + j + 2];
                    t[i + j + 2] = c * u[i + j + 2] + s * u[i + j];
                }
            for (int i = 0; i < 16; i += 8)
                for (int j = 0; j < 4; ++j)
                {
                    u[i + j] = c * t[i + j] - s * t[i + j + 4];
                    u[i + j + 4] = c * t[i + j + 4] + s * t[i + j];
                }
            for (int j = 0; j < 8; ++j)
            {
                t[j] = c * u[j] - s * u[j + 8];
                t[j + 8] = c * u[j + 8] + s * u[j];
            }
            const auto feedL = now.inject * lastLeft, feedR = now.inject * lastRight;
            for (int i = 0; i < 16; i += 2)
            {
                u[i] = now.gain * t[i] + feedL;
                u[i + 1] = now.gain * t[i + 1] + feedR;
            }
            lines<16> (0, u, loop, ms, mc);
            auto l = 0.0f, r = 0.0f;
            for (int i = 0; i < 16; i += 2)
            {
                l += loop[i];
                r += loop[i + 1];
            }
            emit (l * norm, r * norm, false, true, outL[k], outR[k]);
            lastLeft = xl;
            lastRight = xr;
            pos = (pos + 1) & mask;
        }
    }

    void runFdn4 (const float* inL, const float* inR, float* outL, float* outR, int n, bool filtered) noexcept
    {
        static const float norm = (float) (1.0 / std::sqrt (2.0));
        for (int k = 0; k < n; ++k)
        {
            double ms, mc;
            advance (ms, mc);
            const auto xl = inL[k], xr = inR[k];
            const auto feedL = now.inject * lastLeft, feedR = now.inject * lastRight;
            float t[4] = { loop[0], loop[1], loop[2], loop[3] }, u[4];
            if (! filtered)
                scatter (t); // the outputs are taken before the matrix
            for (int i = 0; i < 4; i += 2)
            {
                u[i] = now.gain * t[i] + feedL;
                u[i + 1] = now.gain * t[i + 1] + feedR;
            }
            lines<4> (0, u, loop, ms, mc);
            if (filtered)
            {
                // both cuts inside the loop, the outputs after the matrix
                for (int i = 0; i < 4; ++i)
                    loop[i] = filter (cut[i], loop[i]);
                scatter (loop);
                emit (loop[0], loop[1], false, false, outL[k], outR[k]);
            }
            else
                emit ((loop[0] + loop[2]) * norm, (loop[1] + loop[3]) * norm, false, true, outL[k], outR[k]);
            lastLeft = xl;
            lastRight = xr;
            pos = (pos + 1) & mask;
        }
    }

    void runSerialQuad (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int k = 0; k < n; ++k)
        {
            double ms, mc;
            advance (ms, mc);
            const auto xl = inL[k], xr = inR[k];
            float x[4] = { xl, xr, xl, xr };
            for (int s = 0; s < 4; ++s)
                quadAllpass (s, 4 * s, x, ms, mc);
            // (the all-passes' instantaneous path, a⁴ of the input, is taken out)
            const auto a2 = now.allpass * now.allpass, through = a2 * a2;
            emit (x[0] - through * xl, x[1] - through * xr, true, true, outL[k], outR[k]);
            pos = (pos + 1) & mask;
        }
    }

    void runLoopedQuad (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int k = 0; k < n; ++k)
        {
            double ms, mc;
            advance (ms, mc);
            const auto xl = inL[k], xr = inR[k];
            float u[4], x[4];
            for (int i = 0; i < 4; i += 2)
            {
                u[i] = now.feedback * loop[i] + lastLeft;
                u[i + 1] = now.feedback * loop[i + 1] + lastRight;
            }
            lines<4> (0, u, x, ms, mc);
            scatter (x);
            for (int s = 1; s < 4; ++s)
                quadAllpass (s - 1, 4 * s, x, ms, mc);
            for (int i = 0; i < 4; ++i)
                loop[i] = x[i];
            emit (x[0], x[1], false, true, outL[k], outR[k]);
            lastLeft = xl;
            lastRight = xr;
            pos = (pos + 1) & mask;
        }
    }

    void runSerialStereo (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int k = 0; k < n; ++k)
        {
            double ms, mc;
            advance (ms, mc);
            const auto xl = inL[k], xr = inR[k];
            float x[2] = { xl, xr };
            for (int s = 0; s < 8; ++s)
            {
                float in[2], v[2];
                in[0] = now.allpass * stageOut[s][0] + stageIn[s][0];
                in[1] = now.allpass * stageOut[s][1] + stageIn[s][1];
                lines<2> (2 * s, in, v, ms, mc);
                const auto a = now.cosT * v[0] - now.sinT * v[1], b = now.sinT * v[0] + now.cosT * v[1];
                stageIn[s][0] = x[0];
                stageIn[s][1] = x[1];
                x[0] = stageOut[s][0] = a - now.allpass * x[0];
                x[1] = stageOut[s][1] = b - now.allpass * x[1];
            }
            const auto a2 = now.allpass * now.allpass, a4 = a2 * a2, through = a4 * a4;
            emit (x[0] - through * xl, x[1] - through * xr, true, true, outL[k], outR[k]);
            pos = (pos + 1) & mask;
        }
    }
};

Network::Network() : impl (std::make_unique<Impl>()) {}
Network::~Network() = default;

void Network::prepare (double sampleRate) { impl->prepare (sampleRate); }
void Network::reset() noexcept { impl->clear(); }

void Network::process (const float* inL, const float* inR, float* outL, float* outR, int n, const Controls& c) noexcept
{
    auto& m = *impl;
    if (n <= 0 || m.memory.empty())
        return;
    const ScopedFlushDenormals flush; // (a tail that has decayed must not circulate as denormal numbers)
    if (c.topology != m.topology)
    {
        // another network: other lines, other states (the caller fades the wet signal around the change)
        m.clear();
        m.place (c.topology);
    }
    m.aim (c);
    switch (m.topology)
    {
        case Topology::fdn16: m.runFdn16 (inL, inR, outL, outR, n); break;
        case Topology::serialQuad: m.runSerialQuad (inL, inR, outL, outR, n); break;
        case Topology::serialStereo: m.runSerialStereo (inL, inR, outL, outR, n); break;
        case Topology::loopedQuad: m.runLoopedQuad (inL, inR, outL, outR, n); break;
        case Topology::fdn4: m.runFdn4 (inL, inR, outL, outR, n, false); break;
        case Topology::fdn4Filtered: m.runFdn4 (inL, inR, outL, outR, n, true); break;
        case Topology::none:
            std::fill (outL, outL + n, 0.0f);
            std::fill (outR, outR + n, 0.0f);
            break;
    }
}
} // namespace lynx::verbs::cloud
