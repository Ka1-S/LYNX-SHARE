// Round 190kg (0.1.30 performance gate): the basic Delay's loops rearranged for fewer instructions must render
// exactly what round 190ie's loops rendered. The reference below is round 190ie's Delay::process verbatim (the glide
// toward a new tempo's length, read between samples, then whole samples at the steady length); the effect must match
// it bit for bit under steady tempos, ramps, waves and steps, at block sizes around the 64-sample chunks, with Time
// knob moves, ping-pong on and off, and lines shorter than a chunk (very fast tempos: the per-sample fallback).

#include <cstring>

namespace perf190kg
{
struct Delay190ie
{
    static constexpr double maxSeconds = 3.0, glideSeconds = 0.5;
    std::array<std::vector<float>, 2> buffer;
    double sampleRate = 44100.0, glideK = 1.0, current = 0.0;
    int lastIndex = -1;
    bool settled = false;
    std::size_t write = 0;
    float damp[2] {};
    explicit Delay190ie (double sr) : sampleRate (sr)
    {
        buffer[0].assign ((std::size_t) (maxSeconds * 192000.0) + 4, 0.0f);
        buffer[1].assign ((std::size_t) (maxSeconds * 192000.0) + 4, 0.0f);
        glideK = 1.0 - std::pow (0.01, 1.0 / (glideSeconds * sr));
    }
    void process (float* l, float* r, int n, const lynx::EffectParams& params, double bpm) noexcept
    {
        static constexpr std::array<double, 8> divisions { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
        const auto index = std::clamp ((int) (params[0] * 8.0f), 0, 7);
        const auto beats = divisions[(std::size_t) index];
        const auto seconds = std::min (maxSeconds - 0.01, beats * 60.0 / std::max (bpm, 20.0));
        const auto delaySamples = std::max<std::size_t> (1, (std::size_t) (seconds * sampleRate));
        const auto feedback = params[1] * 0.95f;
        const auto mix = params[2];
        const auto pingPong = params[3] > 0.5f;
        const auto dampCoeff = 1.0f - params[4] * 0.9f;
        const auto size = buffer[0].size();
        const auto target = (double) delaySamples;
        if (! settled || index != lastIndex)
            current = target;
        settled = true;
        lastIndex = index;
        auto sample = [&] (int i, float echoL, float echoR) noexcept {
            damp[0] += dampCoeff * (echoL - damp[0]);
            damp[1] += dampCoeff * (echoR - damp[1]);
            echoL = damp[0];
            echoR = damp[1];
            const auto inL = l[i], inR = r[i];
            if (pingPong)
            {
                buffer[0][write] = 0.5f * (inL + inR) + echoR * feedback;
                buffer[1][write] = echoL * feedback;
            }
            else
            {
                buffer[0][write] = inL + echoL * feedback;
                buffer[1][write] = inR + echoR * feedback;
            }
            l[i] = inL + mix * echoL;
            r[i] = inR + mix * echoR;
            write = (write + 1) % size;
        };
        int i = 0;
        for (; i < n && current != target; ++i)
        {
            const auto diff = target - current;
            current = std::abs (diff) < 0.001 ? target : current + diff * glideK;
            const auto whole = (std::size_t) current;
            const auto frac = (float) (current - (double) whole);
            const auto at = write >= whole ? write - whole : write + size - whole, before = at == 0 ? size - 1 : at - 1;
            sample (i, buffer[0][at] + (buffer[0][before] - buffer[0][at]) * frac, buffer[1][at] + (buffer[1][before] - buffer[1][at]) * frac);
        }
        for (; i < n; ++i)
        {
            const auto readPos = (write + size - delaySamples) % size;
            sample (i, buffer[0][readPos], buffer[1][readPos]);
        }
    }
};

inline bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}
} // namespace perf190kg

TEST (perf190kg_delay_renders_round_190ie_bit_for_bit)
{
    using namespace perf190kg;
    constexpr double pi = 3.14159265358979323846;
    // tempo laws (BPM at a block's start, of n samples): steady, ramp, the perf project's wave, a step, and a very fast
    // wave whose 1/16 lines are shorter than a chunk (2,000 … 30,000 BPM: 88 … 6 samples at 44.1 kHz)
    const auto tempo = [&] (int law, int at, int n) {
        const double f = (double) at / (double) n;
        switch (law)
        {
            case 1: return 120.0 + 20.0 * f;
            case 2: return 122.0 + 4.0 * std::sin (2 * pi * 3.0 * f);
            case 3: return f < 0.5 ? 120.0 : 97.0;
            case 4: return 16000.0 + 14000.0 * std::sin (2 * pi * 2.0 * f);
            default: return 120.0;
        }
    };
    int cases = 0, same = 0;
    for (const double rate : { 44100.0, 96000.0 })
        for (const int block : { 37, 64, 128, 200 })
            for (int law = 0; law < 5; ++law)
                for (int variant = 0; variant < 4; ++variant)
                {
                    auto slot = lynx::makeEffectSlot (lynx::EffectKind::delay);
                    slot.params[0] = variant == 1 ? 0.0f : variant == 2 ? 0.9f : 0.4f;   // 1/16, 1 bar, 1/4 dot.
                    slot.params[1] = variant == 3 ? 0.98f : 0.6f;                        // feedback
                    slot.params[2] = 0.5f;                                               // mix
                    slot.params[3] = variant == 1 || variant == 3 ? 1.0f : 0.0f;         // ping-pong
                    slot.params[4] = variant == 2 ? 0.0f : 0.45f;                        // damping
                    auto fx = lynx::makeEffect (slot);
                    CHECK (fx != nullptr);
                    if (fx == nullptr)
                        return;
                    fx->prepare (rate);
                    Delay190ie ref (rate);
                    const int n = (int) (1.6 * rate);
                    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
                    for (int i = 0; i < n; ++i)
                    {
                        const auto t = i / rate;
                        const auto gate = (i % (int) (0.4 * rate)) < (int) (0.15 * rate) ? 1.0 : 0.0;
                        l[(std::size_t) i] = (float) (gate * 0.3 * std::sin (2 * pi * 330.0 * t));
                        r[(std::size_t) i] = (float) (gate * 0.25 * std::sin (2 * pi * 440.0 * t + 0.3));
                    }
                    auto refL = l, refR = r;
                    auto params = slot.params;
                    for (int at = 0; at < n; at += block)
                    {
                        const int len = std::min (block, n - at);
                        if (variant == 0 && at >= n / 2 && at - block < n / 2)
                            params[0] = 0.6f; // a Time knob move: the new length at once
                        const auto bpm = tempo (law, at, n);
                        fx->process (l.data() + at, r.data() + at, len, params, bpm);
                        ref.process (refL.data() + at, refR.data() + at, len, params, bpm);
                    }
                    ++cases;
                    if (sameBits (l, refL) && sameBits (r, refR))
                        ++same;
                    else
                        std::printf ("  Delay differs from round 190ie's loop: %.0f Hz, block %d, tempo law %d, variant %d\n", rate, block, law, variant);
                }
    std::printf ("  basic Delay vs round 190ie's loop: %d / %d renders bit-identical\n", same, cases);
    CHECK (same == cases);
}
