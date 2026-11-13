#include "core/DrumSynthLive.h"
#include "core/DrumParamRanges.h"
#include "core/Sha256.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// The engine is specified in float32 operations (REA_rodada30 §1.5/1.6); fusing a·b + c into one FMA would change
// the rounding, so contraction stays off in this file (GCC and MSVC do not contract on the default x86-64 targets)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace lynx
{
namespace
{
// ---------------------------------------------------------------------------
// Recorded waveforms: resource type "WAVES", id 4003 of the user's drumsynth_eng_x64.dll (§1.7) — raw 16-bit mono
// samples. The engine also seeds its noise generator from them.

constexpr const char* engineSha256 = "9a5f168219c702514f07f3d568a348fc1438d9ffa287310467dd7887cc74c637";
// FL Studio 2025.1.6's build (peImageSha256Hex, round 169g): 51 bytes differ from 2026's (link time stamps, the debug
// directory), not the WAVES resource: tests/Crash169gTests.inc renders both alike
constexpr const char* engineImageSha256Fl25 = "1b503eb29b3a4c724dcc45e86c3fc0293dfcdba6a98595b613a666263362d818";

// Without the DLL the noise starts from the engine's base constant instead of base + Σ(resource samples): the noise
// has the same character, only the exact sample sequence differs from FL's
constexpr std::uint32_t fallbackSeed = 0x56CE;

struct Waves
{
    std::vector<std::int16_t> samples;
    std::uint32_t seed = fallbackSeed;
};

// start, end, loop length of the 18 waves, in 1/4096 sample of the resource (the engine's table; addresses only)
constexpr std::uint32_t waveTable[18][3] = {
    { 0x1a80d000, 0x1fc62000, 0x05455000 }, { 0x19203000, 0x1a831000, 0x00c57000 }, { 0x1fc93000, 0x24efd000, 0x0526b000 },
    { 0x00000000, 0x0178e000, 0x00010000 }, { 0x0178f000, 0x0707d000, 0x00010000 }, { 0x0707e000, 0x0ee8a000, 0x072cb000 },
    { 0x0ee90000, 0x191e5000, 0x06b90000 }, { 0x24f09000, 0x262d7000, 0x00010000 }, { 0x262f6000, 0x280c4000, 0x00010000 },
    { 0x280c4000, 0x2932b000, 0x00010000 }, { 0x2932c000, 0x31912000, 0x00010000 }, { 0x31912000, 0x3345f000, 0x00010000 },
    { 0x3345f000, 0x35890000, 0x00010000 }, { 0x358ab000, 0x39065000, 0x00010000 }, { 0x39065000, 0x3cc0d000, 0x00010000 },
    { 0x3cc0d000, 0x40958000, 0x00010000 }, { 0x40958000, 0x444ec000, 0x00010000 }, { 0x444ec000, 0x47936000, 0x00010000 },
};

std::unique_ptr<Waves> loadWaves (const std::string& path)
{
    if (path.empty())
        return nullptr;
    std::ifstream in (path, std::ios::binary);
    if (! in) // FL-style path on a system with '/' separators (the command-line tools on Linux)
    {
        auto local = path;
        std::replace (local.begin(), local.end(), '\\', '/');
        in.open (local, std::ios::binary);
    }
    if (! in)
        return nullptr;
    const std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (b.size() < 0x1000)
        return nullptr;
    if (const auto image = peImageSha256Hex (b); image != engineSha256 && image != engineImageSha256Fl25)
        return nullptr; // only the decoded builds
    auto u32 = [&] (std::size_t off) { std::uint32_t v = 0; if (off + 4 <= b.size()) std::memcpy (&v, b.data() + off, 4); return v; };
    auto u16 = [&] (std::size_t off) { std::uint16_t v = 0; if (off + 2 <= b.size()) std::memcpy (&v, b.data() + off, 2); return v; };
    const auto pe = (std::size_t) u32 (0x3c);
    const auto sections = u16 (pe + 6), optSize = u16 (pe + 20);
    const auto resourceRva = u32 (pe + 24 + 112 + 16); // PE32+ data directory 2
    auto offset = [&] (std::uint32_t rva) -> std::size_t {
        for (std::size_t i = 0, o = pe + 24 + optSize; i < sections; ++i, o += 40)
        {
            const auto vs = u32 (o + 8), va = u32 (o + 12), rs = u32 (o + 16), ro = u32 (o + 20);
            if (rva >= va && rva < (std::uint64_t) va + std::max (vs, rs))
                return (std::size_t) ro + (rva - va);
        }
        return b.size();
    };
    const auto base = offset (resourceRva);
    if (base >= b.size())
        return nullptr;
    // directory entries: (id or name, offset); names are length-prefixed UTF-16
    auto find = [&] (std::uint32_t dir, auto&& match) -> std::uint32_t {
        const auto count = (std::uint32_t) u16 (base + dir + 12) + u16 (base + dir + 14);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto id = u32 (base + dir + 16 + 8 * i), to = u32 (base + dir + 20 + 8 * i);
            if (match (id))
                return to;
        }
        return 0xffffffffu;
    };
    const auto typeDir = find (0, [&] (std::uint32_t id) {
        if ((id & 0x80000000u) == 0)
            return false;
        const auto at = base + (id & 0x7fffffffu);
        const auto length = u16 (at);
        if (length != 5)
            return false;
        for (std::size_t k = 0; k < 5; ++k)
            if (std::toupper ((int) u16 (at + 2 + 2 * k)) != "WAVES"[k])
                return false;
        return true;
    });
    if ((typeDir & 0x80000000u) == 0)
        return nullptr;
    const auto idDir = find (typeDir & 0x7fffffffu, [] (std::uint32_t id) { return id == 4003; });
    if ((idDir & 0x80000000u) == 0)
        return nullptr;
    const auto leaf = find (idDir & 0x7fffffffu, [] (std::uint32_t) { return true; }); // the first language
    if (leaf == 0xffffffffu || (leaf & 0x80000000u) != 0)
        return nullptr;
    const auto dataRva = u32 (base + leaf), dataSize = u32 (base + leaf + 4);
    const auto at = offset (dataRva);
    if (at + dataSize > b.size() || dataSize < 0x8F5C2 || dataSize / 2 < (waveTable[17][1] >> 12) + 2)
        return nullptr;
    auto waves = std::make_unique<Waves>();
    waves->samples.resize (dataSize / 2);
    std::memcpy (waves->samples.data(), b.data() + at, waves->samples.size() * 2);
    // noise seed: base constant + the int16 at every 254th byte below 0x8F5C0 (mod 2³²)
    auto seed = fallbackSeed;
    for (std::size_t k = 0; k < 0x8F5C0; k += 0xFE)
    {
        std::int16_t s = 0;
        std::memcpy (&s, b.data() + at + k, 2);
        seed += (std::uint32_t) (std::int32_t) s;
    }
    waves->seed = seed;
    return waves;
}

// ---------------------------------------------------------------------------
// Front-end laws (§1.2/1.3)

float patchGain (int level) noexcept // (11^(v/128) − 1)/10, exactly 1 at 128
{
    const auto x = (float) level * 0.0078125f;
    if (x == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (x * 2.3978953f)) - 1.0) * (double) 0.1f);
}

float morphFraction (int amount) noexcept
{
    const auto m = std::min ((float) amount * 0.0078125f, 1.0f);
    auto f = (float) ((double) m * 1.6 - 0.3);
    if (0.9 < (double) f)
        f = (float) ((double) m * 0.4 + 0.6);
    if ((double) f < 0.1)
        f = (float) ((double) m * 0.4);
    return f;
}

using Patches = std::array<std::int32_t, drumSynthPatches * drumSynthPatchParams>;
using EngineParams = std::array<std::int32_t, 35>;

// patch of `key` → the engine's 35 integers, with morph (§1.3)
EngineParams engineParams (const Patches& patches, int key) noexcept
{
    EngineParams out {};
    out[0] = 45000;
    const auto slot = drumSynthKeyToSlot (key);
    if (slot < 0)
        return out;
    const auto* src = patches.data() + drumSynthPatchParams * slot;
    const auto target = src[34];
    // a morph target outside the 120 keys is treated as no morph (LYNX's choice; not observed in FL data)
    const auto targetSlot = target >= 0 && target != key ? drumSynthKeyToSlot (target) : -1;
    const auto* dst = targetSlot >= 0 ? patches.data() + drumSynthPatchParams * targetSlot : nullptr;
    const auto f = dst != nullptr ? morphFraction (src[37]) : 0.0f;
    for (int i = 2; i < 34; ++i)
    {
        if (i == 24 && dst == nullptr)
            continue;
        auto v = src[i];
        if (dst != nullptr && i != 15 && i != 24)
            v = (std::int32_t) std::nearbyint ((float) v + f * (float) (dst[i] - v)); // Delphi Round: half to even
        if (i == 15 && v == -1)
            v = 0x80;
        out[(std::size_t) i] = v;
    }
    return out;
}

// engine integer → float (TriggerVoice prologue, §1.4)
std::array<float, 35> scaleParams (const EngineParams& raw) noexcept
{
    std::array<float, 35> p {};
    for (int i = 0; i < 35; ++i)
    {
        const auto v = (float) raw[(std::size_t) i];
        switch (i)
        {
            case 15: case 18: case 22: case 24: case 32: break; // used as integers
            case 0: case 1: p[(std::size_t) i] = v * 1.52587890625e-05f; break;
            case 4: case 10: p[(std::size_t) i] = v * 0.001f; break;
            case 5: case 6: case 7: case 8: case 9: case 11: case 12: case 13: case 14: case 17: case 19: case 20: case 21: case 33:
                p[(std::size_t) i] = v * 0.007874f; break;
            case 23: p[(std::size_t) i] = v * 0.01f; break;
            default: p[(std::size_t) i] = v * 0.01f + 0.5f; break;
        }
    }
    return p;
}

float bitsToFloat (std::uint32_t u) noexcept { return std::bit_cast<float> (u); }

// ---------------------------------------------------------------------------
// Engine voice (§1.5 set-up, §1.6 render/release), float32 as in the engine; exp/log/floor in double where it uses them

struct Levels
{
    float initVol = 1.0f, finalVol = 1.0f, pitchCents = 0.0f, fcut = 0.0f, fres = 0.0f;
};

struct Engine
{
    float fs = 44100.0f, inv = 1.0f / 44100.0f;
    std::uint32_t seed = fallbackSeed; // one noise generator per instance, shared by its voices
    const std::int16_t* waves = nullptr;

    void setRate (double rate) noexcept
    {
        fs = (float) rate;
        inv = 1.0f / fs;
    }

    float decay (float x) const noexcept // D(x)
    {
        const auto e = std::exp (8.16 - (double) x * 9.21);
        return 1.0f - (float) std::exp (e * -(double) inv);
    }

    static float level (float x, float s, float v) noexcept // G(x, s, v)
    {
        auto a = (s * 3.0f - 1.5f) * v + x * 6.9f;
        a = a - 4.6999998f;
        auto r = (float) std::exp ((double) a);
        if (0.01f > x)
            r = 0.0f;
        return r;
    }

    float coefficient (float x, float r, float s, float v, float q) const noexcept // C(x, s, q)
    {
        const auto m = (s - 0.5f) * 2.23f;
        const auto a = m * v * m * m + x * 6.90777f;
        const auto hz = std::floor (std::exp ((double) a) * 16.0);
        const auto f = std::min (16000.0f, (float) hz * r);
        const auto w0 = inv * 6.2831855f;
        const auto w = f * w0;
        const auto t = (0.04f - q * 0.07f) * w + q * 0.3f;
        return w - t * (w * w);
    }
};

struct EngineVoice
{
    float a = 0, b = 0, pn = 0, c = 0, d = 0, pa = 0;      // Osc1: two two-integrator sections
    float e2 = 0, g2 = 0, pn2 = 0, h2 = 0, k2 = 0, pe2 = 0; // Osc2
    float lp = 0;                                           // tone filter
    float q1 = 0, c64 = 0, f1 = 0, f1t = 0, df1 = 0, sweep = 0;
    float q2 = 0, c2 = 0, f2 = 0, fq2 = 0;
    float env1 = 0, dec1 = 0, env2 = 0, tgt2 = 0, dec2 = 0, att = 0, attinc = 0;
    float rL1 = 0, rL2 = 0, rDec1 = 0, rDec2 = 0;
    std::int32_t count = 0;
    std::uint32_t period = 0, counter = 0;
    float gn = 0, gw = 0;
    std::uint32_t pos = 0, end = 0, loop = 0, step = 0;
    float dry = 0, lpk = 0, s80 = 0, s7c = 0, s78 = 0, s74 = 0, ring = 0, out = 0;

    void trigger (Engine& e, const EngineParams& raw, const Levels& lv) noexcept
    {
        *this = {};
        const auto P = scaleParams (raw);
        const auto twoFV = lv.finalVol * 2.0f;
        const auto w = e.inv * 6.2831855f;
        const auto v = (lv.initVol + lv.initVol) - 1.0f + lv.fres; // velocity term
        const auto x = lv.pitchCents * 0.01f + 60.0f;
        const auto r = (float) std::exp ((double) (x - 60.0f) * 0.05776226504666211);
        // noise / wave source
        const auto x29 = (P[29] + P[29]) - 1.0f;
        const auto ew = (float) std::exp ((double) (v * 0.5f * x29 * x29 * x29 + P[16] * 1.3863f));
        const auto k = e.inv * 22050.0f * r;
        step = (std::uint32_t) (std::int32_t) (std::min (8.0f, ew * k) * 4096.0f);
        const auto wi = (std::uint32_t) raw[15];
        float mixn = 1.0f;
        if (wi < 18)
        {
            pos = waveTable[wi][0], end = waveTable[wi][1], loop = waveTable[wi][2];
            gw = P[17] * 3.0f;
            mixn = 1.0001f - P[17];
            if (e.waves == nullptr)
                step = 0; // no DLL (or another build): the recorded wave is silent, the noise keeps FL's balance
        }
        else
        {
            step = 0;
            gw = 0.0f;
        }
        gn = (float) std::sqrt ((double) e.fs) * 0.0047619f * mixn;
        // Osc1
        if (raw[32] != 0) // noise band
        {
            const auto a5 = P[5];
            auto x1 = a5 + a5;
            if (a5 > 0.5f)
                x1 = 1.0f;
            const auto q = x1 * x1;
            q1 = q;
            c64 = q * x1;
            auto lim = 1.98f - q;
            const auto f = e.coefficient (P[4], r, P[25], v, q);
            lim = f < lim ? f : lim;
            f1 = lim;
            df1 = 0.0f;
            if (a5 > 0.5f)
            {
                const auto t = 1.0f - a5;
                const auto kk = 0.999f - t * 4.0f * t;
                df1 = 0.0f - (kk + kk) * lim;
                f1 = lim * kk + lim;
                f1t = f1;
            }
            else
                f1t = lim;
        }
        else // swept tone
        {
            q1 = 0.0f, c64 = 0.0f, df1 = 0.0f;
            f1t = e.coefficient (P[4], r, 0.5f, 0.0f, 0.0f);
            const auto a5 = P[5];
            const auto xs = 0.5f > a5 ? (a5 + a5) * P[4] : ((a5 + a5) - 1.0f) * (1.0f - P[4]) + P[4];
            f1 = e.coefficient (xs, r, P[25], v, 0.0f);
        }
        // Osc2
        {
            const auto b11 = P[11];
            auto x1 = b11 + b11;
            if (b11 > 0.5f)
                x1 = 1.0f;
            q2 = x1 * x1;
            c2 = q2 * x1;
            auto lim = 1.98f - q2;
            const auto f = e.coefficient (lv.fcut * 0.5f + P[10], r, P[27], v, q2);
            lim = f < lim ? f : lim;
            fq2 = lim, f2 = lim;
            if (b11 > 0.5f)
            {
                const auto t = 1.0f - b11;
                const auto kk = 0.999f - t * 4.0f * t;
                f2 = lim * kk + lim;
                fq2 = lim - lim * kk;
            }
        }
        // envelope 2: attack, then decay toward −L₂·E
        const auto L2 = Engine::level (P[14], P[28], v);
        env2 = L2;
        const auto tgt = -(float) std::exp ((double) (P[9] * 9.0f - 7.0f));
        tgt2 = L2 * tgt;
        const auto ratio = (0.03f - tgt) / (1.0f - tgt);
        const auto lg = std::log ((double) ratio) * (double) e.inv;
        const auto e13 = std::exp ((double) (6.91f - P[13] * 9.21f));
        dec2 = 1.0f - (float) std::exp (e13 * lg);
        att = 0.0f;
        attinc = L2;
        if (P[12] > 0.005f)
            attinc = L2 / ((float) std::exp ((double) (P[12] * 8.5169f - 9.21f)) * e.fs);
        // click at note-on / phase invert: initial states of the second sections
        const auto h = P[23];
        auto c0 = h * h;
        if (h > 0.9f)
            c0 = (float) ((e.seed >> 8) & 0xffu) * 0.0036f; // "at maximum, the offset is random"
        c = c0, h2 = c0;
        d = c0 * c0 * c0 - 1.0f, k2 = d;
        if (raw[22] != 0)
            c64 = -c64, c = -c0, d = -d;
        // envelope 1, sweep
        const auto L1 = Engine::level (P[8], P[26], v);
        env1 = L1;
        dec1 = e.decay (P[7]);
        sweep = e.decay (P[6]);
        // retrigger
        const auto n = raw[18];
        count = n, period = 0, counter = 0;
        if (n < 0)
            count = 0;
        else
        {
            if (n > 100)
                count = 100;
            if (count != 0)
            {
                rL2 = L2, rDec2 = dec2, rL1 = L1, rDec1 = dec1;
                period = counter = (std::uint32_t) (std::int32_t) ((float) std::exp ((double) (P[19] * 8.5169f - 9.21f)) * e.fs);
                if (0.995f > P[20])
                    dec2 = dec1 = e.decay (P[20]);
                else
                    dec2 = dec1 = 0.0f;
            }
        }
        // tone filter
        auto t = (P[30] - 0.5f) * v + P[2];
        t = std::min (1.0f, std::max (0.0f, t));
        dry = 1e-4f;
        if (t > 0.5f)
            t = t + -0.5f, dry = -1.0f;
        const auto g = std::exp ((double) t * 14.0);
        lpk = 1.0f - (float) std::exp (g * ((double) -w * 20.0));
        // waveshaper
        const auto dd = ((P[31] + P[31]) - 1.0f) * v + ((P[3] + P[3]) - 1.0f);
        if (0.0f > dd)
        {
            const auto dc = std::max (-1.0f, dd);
            const auto a1 = dc * -6.6667f * dc;
            s80 = a1, s7c = -a1, s78 = 1.0f + a1, s74 = 0.0f;
        }
        else
        {
            const auto dc = std::min (1.0f, dd);
            const auto a1 = dc * 4.708f * dc;
            s80 = a1, s7c = 0.0f, s78 = dc * dc + 1.0f, s74 = a1 * -0.25f;
        }
        ring = P[33];
        out = Engine::level (P[0], 0.5f, 0.0f) * twoFV;
    }

    void release (const Engine& e, const EngineParams& raw) noexcept
    {
        const auto P = scaleParams (raw);
        const auto p7 = std::clamp (P[7], 0.0f, 1.0f), p13 = std::clamp (P[13], 0.0f, 1.0f), p21 = std::clamp (P[21], 0.0f, 1.0f);
        if (period > 0)
            count = 0;
        if (p21 < 0.95f) // release ≥ 121 ignores note-off
        {
            if (att < env2)
                env2 = att;
            const auto f = p21 * 0.5f + 0.5f;
            dec1 = rDec1 = e.decay (f * p7);
            attinc = dec2 = rDec2 = e.decay (f * p13);
        }
    }

    // returns true when the voice has finished (checked once per call, like the engine's per-block report)
    bool render (Engine& e, float* y, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            if (--counter == 0)
            {
                --count;
                env2 = rL2, env1 = rL1, att = 0.0f;
                if (count > 0)
                    counter = period;
                else
                    dec2 = rDec2, dec1 = rDec1;
            }
            e.seed = e.seed * 0x0BB38435u + 0x3619636Bu;
            auto nz = (bitsToFloat ((e.seed & 0x7fffffu) + 0x40000000u) - 3.0f) * gn;
            if (step != 0)
            {
                pos += step;
                if (pos > end)
                    pos -= loop;
                const auto idx = pos >> 12;
                const auto fr = (std::int32_t) ((pos >> 5) & 0x7fu);
                const std::int32_t s0 = e.waves[idx], s1 = e.waves[idx + 1];
                const auto u = (std::uint32_t) ((s1 - s0) * fr) + ((std::uint32_t) (s0 + 0x808000) << 7);
                nz = nz + (bitsToFloat (u) - 3.0f) * gw;
            }
            // Osc1
            const auto f = (f1t - f1) * sweep + f1;
            f1 = f;
            const auto fq = f + df1;
            auto t0 = q1 * a + b + nz + pn;
            pn = nz;
            const auto an = a - t0 * f;
            a = an;
            b = an * fq + b;
            const auto t1 = c * q1 + d;
            const auto t2 = (an + pa) * c64;
            pa = an;
            const auto cc = c - (t1 + t2) * f;
            c = cc;
            d = cc * fq + d;
            const auto s = cc > 0.0f ? s7c : s80;
            const auto envOld = env1;
            env1 = (9.9999997e-06f - env1) * dec1 + env1;
            auto o = ((cc * s + s78) * cc + s74) * envOld;
            // Osc2 (ring-modulated by Osc1)
            if (env2 > 0.0f)
            {
                auto m = (cc - 1.0f) * ring + 1.0f;
                t0 = q2 * e2 + g2 + pn2 + nz;
                pn2 = nz;
                const auto en = e2 - t0 * f2;
                e2 = en;
                g2 = fq2 * en + g2;
                const auto u1 = (en + pe2) * c2;
                pe2 = en;
                const auto u3 = h2 * q2 + k2;
                const auto hn = h2 - (u1 + u3) * f2;
                h2 = hn;
                k2 = hn * fq2 + k2;
                m = m * hn;
                float val;
                if (att >= env2)
                    val = env2 = (tgt2 - env2) * dec2 + env2;
                else
                    val = att = att + attinc;
                o = o + val * m;
            }
            const auto xc = std::min (1.0f, o > -1.0f ? o : -1.0f); // hard clip
            const auto l = (xc - lp) * lpk + lp;
            lp = l;
            y[i] = (xc * dry + l) * out;
        }
        return count < 1 && 1e-4f > env1 && 1e-4f > env2;
    }
};

// ---------------------------------------------------------------------------
// Settings handed from configure() (UI thread) to the audio thread without blocking
template <typename T>
class Mailbox
{
public:
    void write (const T& value) noexcept
    {
        while (busy.test_and_set (std::memory_order_acquire)) {}
        pending = value;
        fresh = true;
        busy.clear (std::memory_order_release);
    }

    bool read (T& out) noexcept
    {
        if (busy.test_and_set (std::memory_order_acquire))
            return false;
        const auto had = fresh;
        if (had)
            out = pending;
        fresh = false;
        busy.clear (std::memory_order_release);
        return had;
    }

private:
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    T pending {};
    bool fresh = false;
};

struct Settings
{
    Patches patches {};
    float pitchCents = 0.0f; // channel pitch/fine/root shift, cents relative to C5
};

// ---------------------------------------------------------------------------
// The front-end (§1.3): voices, key → patch, morph, voice modes, levels and pan

class DrumSynthInstrument final : public Instrument
{
public:
    static constexpr int maxVoices = 16; // the plugin's default polyphony
    static constexpr int block = 512;    // longest engine render call
    static constexpr int cutFade = 256;  // fade of a cut voice (FL: its host's per-block ramp to 0; law not confirmed)

    ~DrumSynthInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete waves;
    }

    void prepare (double sr) noexcept override
    {
        take();
        engine.setRate (sr);
        engine.seed = waves != nullptr ? waves->seed : fallbackSeed;
        for (auto& v : voices)
            v.active = false;
        orderCount = 0;
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        delete retired.exchange (nullptr);
        if (channel.samplePath != loadedPath)
        {
            loadedPath = channel.samplePath;
            if (auto w = loadWaves (loadedPath))
                delete incoming.exchange (w.release());
        }
        auto& s = staging;
        for (std::size_t i = 0; i < s.patches.size(); ++i)
            s.patches[i] = i < channel.modelParams.size() && std::isfinite (channel.modelParams[i])
                               ? (std::int32_t) std::lround (channel.modelParams[i]) : defaultParam ((int) (i % drumSynthPatchParams));
        s.pitchCents = channel.sampler.pitch * 100.0f;
        mailbox.write (s);
    }

    void setNextNoteExpression (float pan, float, bool) noexcept override { nextPan = pan; }

    // automation (round 168p): a knob of a key's patch (slot·38 + j), 0…1 over its range; sounding voices keep the
    // patch they were triggered with, the level / pan follow at once
    void setParameter (int index, float value) noexcept override
    {
        take();
        const auto r = drumranges::drumSynth (index);
        if (r.valid && std::isfinite (value))
            settings.patches[(std::size_t) index] = (std::int32_t) r.raw (value);
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        const auto pan = nextPan;
        nextPan = 0.0f;
        const auto slot = drumSynthKeyToSlot (key);
        if (slot < 0)
            return;
        const auto* patch = settings.patches.data() + drumSynthPatchParams * slot;
        const auto mode = patch[24]; // 0 Mono, 1 Poly, 2…5 Group 1…4
        if (mode != 1)
            for (int i = 0; i < orderCount; ++i)
                if (auto& v = voices[(std::size_t) order[(std::size_t) i]]; ! v.cut && ((v.mode == 0 && v.slot == slot) || (v.mode > 1 && v.mode == mode)))
                    v.cut = true, v.fade = 1.0f;
        if (orderCount == maxVoices)
            kill (0); // all voices busy: the oldest makes room (LYNX's choice)
        int index = 0;
        while (voices[(std::size_t) index].active)
            ++index;
        auto& v = voices[(std::size_t) index];
        v = {};
        v.active = true;
        v.key = key, v.slot = slot, v.mode = mode, v.notePan = pan;
        v.noteId = startingNoteId(); // (round 190ej)
        // FL velocity (0…128, LYNX 0…1 × 128) → Init.Vol = (11^(vel/100) − 1)/10 (REA_rodada28 §4.1, inference);
        // the engine's own Final.Vol factor is applied with the channel volume at mix time (see render)
        Levels lv;
        lv.initVol = (float) ((std::pow (11.0, 1.28 * std::clamp ((double) velocity, 0.0, 1.0)) - 1.0) / 10.0);
        lv.finalVol = 1.0f;
        lv.pitchCents = settings.pitchCents;
        v.engine.trigger (engine, engineParams (settings.patches, key), lv);
        order[(std::size_t) orderCount++] = index;
    }

    void noteOff (int key) noexcept override
    {
        take();
        for (auto& v : voices)
            if (v.active && ! v.released && endsVoice (v.key, v.noteId, key)) // (round 190ej: the note's own voice, REA_rodada30)
            {
                v.released = true;
                v.engine.release (engine, engineParams (settings.patches, key)); // the patch reloaded (with morph)
            }
    }

    // choke / stop: cut every voice (short fade, as a cut voice in FL)
    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            if (v.active && ! v.cut)
                v.cut = true, v.fade = 1.0f;
    }

    bool idle() const noexcept override { return orderCount == 0; }

    // Levels (§1.3, REA_rodada28 §4.1). In FL each voice leaves the engine already × 2·1.03858·Final.Vol, and the
    // front-end hands it to the host with vol = Final.Vol·g(LEVEL) and pan = Final.Pan + PAN through the per-voice law
    // L = vol·cos((1 + pan)π/4), R = vol·sin(…) — so the channel volume counts twice and channel, note and patch pan
    // add up before one cos/sin law. LYNX's channel gains are gl = √2·V·cos((1 + p)π/4), gr = √2·V·sin(…)
    // (panGains), from which V = √((gl² + gr²)/2) and p are recovered exactly; FL's law is then applied as a whole
    // (no separate 1/√2 factor as in Sakura/Drumpad/BooBass, whose FL level is a single Vol·cos/sin product).
    // Final.Vol = channel volume: the front-end does not link the note velocity to it (it never sends the "link
    // velocity" voice event; FL's default for that flag is not confirmed — at FL's default velocity 100 both readings
    // agree). Velocity acts through the patch's velocity knobs (§1.5). Mod X/Y (Final.FCut/FRes) are 0: LYNX notes
    // have none. Both Final.Vol factors follow channel volume changes per block (FL freezes the engine's at note-on).
    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        if (orderCount == 0)
            return;
        const auto vol = std::sqrt ((gl * gl + gr * gr) * 0.5f);
        const auto channelPan = vol > 0.0f ? std::atan2 (gr, gl) * 1.2732395f - 1.0f : 0.0f;
        for (int done = 0; done < n && orderCount > 0; done += block)
        {
            const auto m = std::min (block, n - done);
            for (int i = 0; i < orderCount;)
            {
                auto& v = voices[(std::size_t) order[(std::size_t) i]];
                const auto finished = v.engine.render (engine, buffer.data(), m);
                const auto* patch = settings.patches.data() + drumSynthPatchParams * v.slot;
                const auto pan = std::clamp (channelPan + v.notePan + (float) patch[1] / 64.0f, -1.0f, 1.0f);
                const auto angle = (1.0f + pan) * 0.78539816f;
                const auto level = vol * vol * patchGain (patch[0]);
                const auto vl = level * std::cos (angle), vr = level * std::sin (angle);
                auto* l = left + done;
                auto* r = right + done;
                if (v.cut)
                {
                    for (int k = 0; k < m && v.fade > 0.0f; ++k)
                    {
                        l[k] += buffer[(std::size_t) k] * vl * v.fade;
                        r[k] += buffer[(std::size_t) k] * vr * v.fade;
                        v.fade -= 1.0f / cutFade;
                    }
                }
                else
                    for (int k = 0; k < m; ++k)
                    {
                        l[k] += buffer[(std::size_t) k] * vl;
                        r[k] += buffer[(std::size_t) k] * vr;
                    }
                if (finished || (v.cut && v.fade <= 0.0f))
                    kill (i);
                else
                    ++i;
            }
        }
    }

private:
    struct Voice
    {
        bool active = false, released = false, cut = false;
        int key = 0, slot = 0, mode = 0;
        std::uint32_t noteId = 0; // the LYNX note that started it (round 190ej)
        float notePan = 0.0f, fade = 1.0f;
        EngineVoice engine;
    };

    // a fresh patch is silent (level 0); the knobs keep FL's centred defaults
    static std::int32_t defaultParam (int i) noexcept { return i == 15 || i == 34 ? -1 : 0; }

    void kill (int orderIndex) noexcept
    {
        voices[(std::size_t) order[(std::size_t) orderIndex]].active = false;
        for (int k = orderIndex; k + 1 < orderCount; ++k)
            order[(std::size_t) k] = order[(std::size_t) k + 1];
        --orderCount;
    }

    void take() noexcept
    {
        mailbox.read (settings);
        if (retired.load() == nullptr)
            if (auto* w = incoming.exchange (nullptr))
            {
                for (auto& v : voices) // voices may be reading the old waves
                    v.active = false;
                orderCount = 0;
                retired.store (waves);
                waves = w;
                engine.waves = waves->samples.data();
                engine.seed = waves->seed;
            }
    }

    Engine engine;
    std::array<Voice, maxVoices> voices {};
    std::array<int, maxVoices> order {}; // active voices in trigger order (the engine renders them in this order)
    int orderCount = 0;
    std::array<float, block> buffer {};
    Settings settings {};
    Mailbox<Settings> mailbox;
    Settings staging {};     // configure() side
    std::string loadedPath;  // configure() side
    std::atomic<Waves*> incoming { nullptr }, retired { nullptr };
    Waves* waves = nullptr;  // audio thread
    float nextPan = 0.0f;
};
} // namespace

int drumSynthKeyToSlot (int key) noexcept
{
    const auto s = key >= 48 && key <= 71 ? key - 48 : key < 48 ? key + 24 : key;
    return s >= 0 && s < drumSynthPatches ? s : -1;
}

std::optional<std::vector<float>> drumSynthLiveParams (const std::vector<std::uint8_t>& state)
{
    // u32 version, u32 editor key, 120 Pascal names, 120 × 38 int32, (version > 2) u32 flags (§1.2)
    auto u32 = [&] (std::size_t at) { std::uint32_t v = 0; std::memcpy (&v, state.data() + at, 4); return v; };
    if (state.size() < 8)
        return std::nullopt;
    const auto version = u32 (0);
    if (version < 2 || version > 4)
        return std::nullopt; // version 1 (24-patch kits) is not handled
    std::size_t pos = 8;
    for (int i = 0; i < drumSynthPatches; ++i)
    {
        if (pos >= state.size())
            return std::nullopt;
        pos += 1 + (std::size_t) state[pos];
    }
    if (pos + (std::size_t) drumSynthPatches * drumSynthPatchParams * 4 > state.size())
        return std::nullopt;
    std::vector<float> out ((std::size_t) drumSynthParamCount, 0.0f);
    for (int i = 0; i < drumSynthPatches * drumSynthPatchParams; ++i)
    {
        auto v = (std::int32_t) u32 (pos + 4 * (std::size_t) i);
        if (version < 4 && i % drumSynthPatchParams == 36 && v == -1)
            v = 0;
        out[(std::size_t) i] = (float) v;
    }
    out[(std::size_t) drumSynthEditorKeyIndex] = (float) std::clamp ((int) u32 (4), 0, 131);
    out[(std::size_t) drumSynthEditorKeyIndex + 1] = (float) version;
    return out;
}

std::unique_ptr<Instrument> makeDrumSynthInstrument() { return std::make_unique<DrumSynthInstrument>(); }
bool drumSynthDllUsable (const std::string& path) { return loadWaves (path) != nullptr; }
} // namespace lynx
