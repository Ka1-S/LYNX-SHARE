#include "core/EmphasisFx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/DelphiMath.h"
#include "core/EmphasisEngine.h"

// Front end of FL's Emphasis (docs/analise/REA_rodada167f.md §1): state, controls, engine values. The DSP is the
// shared library's limiter in core/EmphasisEngine.cpp.

#pragma clang fp contract(off)

namespace lynx::emphasis
{
namespace
{
std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

int clampRaw (int k, int raw) noexcept
{
    return std::clamp (raw, rawMin[(std::size_t) k], rawMax[(std::size_t) k]);
}
} // namespace

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4 + 13 * 4)
        return std::nullopt;
    State s;
    s.version = i32At (d, 0);
    if (s.version < 1 || s.version > 10)
        return std::nullopt;
    for (std::size_t k = 0; k < 13; ++k)
        s.raw[k] = i32At (d, 4 + 4 * k);
    if (s.version < 8) // the envelope order was stored 0-based
        s.raw[2] += 1;
    // the three option bytes after the UI fields; a state without them leaves the options off
    std::size_t o = 4 + 13 * 4 + 4 + (s.version >= 7 ? 4 : 0);
    s.raw[13] = s.raw[14] = s.raw[15] = 0;
    if (s.version >= 9 && d.size() > o)
        s.raw[13] = d[o++] != 0;
    if (s.version >= 10 && d.size() >= o + 2)
    {
        s.raw[14] = d[o] != 0;
        s.raw[15] = d[o + 1] != 0;
    }
    for (int k = 0; k < numParams; ++k)
        s.raw[(std::size_t) k] = clampRaw (k, s.raw[(std::size_t) k]);
    return s;
}

void toParams (const State& s, float* params) noexcept
{
    for (int k = 0; k < numParams; ++k)
    {
        const auto lo = rawMin[(std::size_t) k], hi = rawMax[(std::size_t) k];
        params[k] = (float) std::clamp ((double) (s.raw[(std::size_t) k] - lo) / (hi - lo), 0.0, 1.0);
    }
}

int rawValue (int param, float v01) noexcept
{
    const auto k = (std::size_t) std::clamp (param, 0, numParams - 1);
    return rawMin[k] + (int) std::lround (std::clamp (v01, 0.0f, 1.0f) * (float) (rawMax[k] - rawMin[k]));
}

float engineValue (int k, int raw) noexcept
{
    k = std::clamp (k, 0, numParams - 1);
    raw = clampRaw (k, raw);
    // the front end's control value: raw · (1 / 10000) as float for the 0…10000 controls
    const auto x = (float) ((double) raw * (1.0 / 10000.0));
    switch (k)
    {
        case 2: return (float) (raw - 1); // 0-based order
        case 6: return (float) ((double) x * 24.0); // 0…+24 dB, linear in dB
        case 7: case 8: return x;
        case 9:
        {
            // FL's volume curve (e^(x·ln 11) − 1) / 10 in dB
            float v;
            if (x == 1.0f)
                v = 1.0f;
            else
                v = (float) ((dmath::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
            if (! (v > 0.0f))
                return -INFINITY;
            return (float) (dmath::ln ((double) v) * 0.4342944819032518 * 20.0);
        }
        default: return (float) raw; // switches and choices go through as their integer
    }
}

std::string paramText (int param, float v01)
{
    const int raw = rawValue (param, v01);
    char b[32];
    switch (param)
    {
        case 0: case 1: case 3: case 10: case 13: case 14: case 15: return raw != 0 ? "On" : "Off";
        case 2: std::snprintf (b, sizeof b, "%d", raw); return b;
        case 4: return raw != 0 ? "M/S" : "L/R";
        case 5:
        {
            static const char* const modes[] { "Versatile", "Loud", "Transient", "Steady" };
            return modes[std::clamp (raw, 0, 3)];
        }
        case 6: std::snprintf (b, sizeof b, "%+.1f dB", (double) engineValue (6, raw)); return b;
        case 7: std::snprintf (b, sizeof b, "%.0f%%", raw / 100.0); return b;
        case 8:
            if (raw == 0)
                return "Off";
            std::snprintf (b, sizeof b, "%.0f%%", raw / 100.0);
            return b;
        case 9:
        {
            const auto db = engineValue (9, raw);
            if (! std::isfinite (db))
                return "-inf dB";
            std::snprintf (b, sizeof b, "%.2f dB", (double) db);
            return b;
        }
        case 11:
            if (raw == 0)
                return "Off";
            std::snprintf (b, sizeof b, "%dx", 1 << raw);
            return b;
        case 12: std::snprintf (b, sizeof b, "%d", raw); return b;
        default: return {};
    }
}

namespace
{
class EmphasisEffect final : public Effect
{
public:
    explicit EmphasisEffect (const EffectParams* initial)
    {
        // copied (the slot may move after the effect is built); a new instance's defaults without a slot
        for (int k = 0; k < numParams; ++k)
            ep[(std::size_t) engineIndex[(std::size_t) k]] =
                engineValue (k, initial != nullptr ? rawValue (k, (*initial)[(std::size_t) k]) : rawDefault[(std::size_t) k]);
    }
    void prepare (double sampleRate) noexcept override
    {
        rate = (float) sampleRate;
        ready = false;
    }
    void reset() noexcept override {} // FL's flush only resets the plug-in's meters
    // reported to FL's PDC; before the first block from the slot's controls (makeEffect (slot)), so PDC is planned
    // before the song starts
    int latencySamples() const noexcept override
    {
        return ready ? latency : Engine::latencyFor (rate, ep.data());
    }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        std::array<int, numParams> raw {};
        for (int k = 0; k < numParams; ++k)
            raw[(std::size_t) k] = rawValue (k, params[(std::size_t) k]);
        if (! ready)
        {
            // FL: the sample rate first (the engine starts with its own defaults), then the state's controls in the
            // front end's order
            engine.prepare (rate, maxBlock);
            static constexpr int loadOrder[] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 10, 14, 15, 13 };
            for (int k : loadOrder)
                engine.setParam (engineIndex[(std::size_t) k], engineValue (k, raw[(std::size_t) k]));
            current = raw;
            ready = true;
            latency = engine.latency();
            // FL's mixer runs the plug-in on silence before a render starts (as for Emphasizer, round 154: 1536
            // samples, plus 768 for a plug-in that reports latency)
            std::array<float, maxBlock> zl {}, zr {};
            for (int done = 0; done < warmUpSamples; done += maxBlock)
            {
                zl.fill (0.0f);
                zr.fill (0.0f);
                engine.process (zl.data(), zr.data(), zl.data(), zr.data(), std::min (maxBlock, warmUpSamples - done));
            }
        }
        else
        {
            bool changed = false;
            for (int k = 0; k < numParams; ++k)
                if (raw[(std::size_t) k] != current[(std::size_t) k])
                {
                    engine.setParam (engineIndex[(std::size_t) k], engineValue (k, raw[(std::size_t) k]));
                    current[(std::size_t) k] = raw[(std::size_t) k];
                    changed = true;
                }
            if (changed)
                latency = engine.latency();
        }
        for (int done = 0; done < n;)
        {
            const int m = std::min (maxBlock, n - done);
            engine.process (left + done, right + done, left + done, right + done, m);
            done += m;
        }
    }

private:
    static constexpr int maxBlock = 512;
    static constexpr int warmUpSamples = 1536 + 768;
    std::array<float, 28> ep {}; // the initial controls as engine values, by engine index
    Engine engine;
    std::array<int, numParams> current {};
    float rate = 44100.0f;
    int latency = 0;
    bool ready = false;
};
} // namespace

std::unique_ptr<Effect> make (const EffectParams* initial) { return std::make_unique<EmphasisEffect> (initial); }
} // namespace lynx::emphasis
