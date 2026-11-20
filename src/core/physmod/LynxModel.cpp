// LYNX Model (core/physmod/LynxModel.h): tables, interpolation, the mallet exciter, voices, the instrument.

#include "core/physmod/LynxModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "core/HostSlide.h"
#include "core/ModelParams.h"
#include "core/Song.h"

namespace lynx
{
namespace lm
{
namespace
{
#include "core/physmod/tables/marimba.inc"
#include "core/physmod/tables/vibraphone.inc"
#include "core/physmod/tables/xylophone.inc"
#include "core/physmod/tables/glockenspiel.inc"
#include "core/physmod/tables/tubularbells.inc"
#include "core/physmod/tables/kalimba.inc"

constexpr double twoPi = 6.283185307179586476925;
constexpr double hDrive = 0.1; // modes the contact pulse reaches less than this get a kick (Engine::Impl::start)

struct Blob
{
    const unsigned char* data;
    std::size_t size;
};
const Blob blobs[modelCount] = { { lpm_marimba, sizeof (lpm_marimba) }, { lpm_vibraphone, sizeof (lpm_vibraphone) },
                                 { lpm_xylophone, sizeof (lpm_xylophone) }, { lpm_glockenspiel, sizeof (lpm_glockenspiel) },
                                 { lpm_tubularbells, sizeof (lpm_tubularbells) }, { lpm_kalimba, sizeof (lpm_kalimba) } };

float halfToFloat (std::uint16_t h) noexcept
{
    const std::uint32_t sign = (std::uint32_t) (h >> 15) << 31;
    std::uint32_t exp = (h >> 10) & 0x1f, man = h & 0x3ff, bits;
    if (exp == 0)
    {
        if (man == 0)
            bits = sign;
        else
        {
            exp = 127 - 15 + 1;
            while ((man & 0x400) == 0)
            {
                man <<= 1;
                --exp;
            }
            bits = sign | (exp << 23) | ((man & 0x3ff) << 13);
        }
    }
    else if (exp == 31)
        bits = sign | 0x7f800000u | (man << 13);
    else
        bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
    float f;
    std::memcpy (&f, &bits, 4);
    return f;
}

std::uint16_t u16 (const std::uint8_t* p) noexcept { return (std::uint16_t) (p[0] | (p[1] << 8)); }
float f32 (const std::uint8_t* p) noexcept
{
    const std::uint32_t b = (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24);
    float f;
    std::memcpy (&f, &b, 4);
    return f;
}
float f16 (const std::uint8_t* p) noexcept { return halfToFloat (u16 (p)); }

// a table that still plays (one sine) when a compiled-in table could not be read
Table fallbackTable (int model)
{
    Table t;
    t.model = model;
    TableAnchor a;
    a.count = 1;
    t.anchors.push_back (a);
    t.modes.push_back ({});
    return t;
}

double keyHz (double key) noexcept { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); }

// --- interpolation of mode sets ----------------------------------------------------------------------------------------
constexpr int setCap = 96; // (two anchors' modes, before the strongest maxModes are kept)
struct ModeSet
{
    int n = 0;
    float ratio[setCap] {}, ampDb[setCap] {}, t60[setCap] {};
    std::uint8_t shape[setCap] {}, fam[setCap] {};
    bool invert[setCap] {};
    float gainDb = 0.0f, contact = 0.0005f, noiseDb = -60.0f, noiseHz = 2000.0f, noiseQ = 1.0f, noiseMs = 10.0f, riseMs = 0.0f;
};

// (a mode of family absoluteFamily is a line at its own pitch - another tine or bar ringing along - so played at another
// key it keeps its frequency in Hz: its ratio re that key)
ModeSet fromAnchor (const Table& t, const TableAnchor& a, float key)
{
    ModeSet s;
    const auto toKey = std::pow (2.0f, (a.key - key) / 12.0f);
    for (int k = 0; k < a.count && s.n < setCap; ++k)
    {
        const auto idx = (std::size_t) a.first + (std::size_t) k;
        if (idx >= t.modes.size())
            break;
        const auto& m = t.modes[idx];
        s.ratio[s.n] = m.family == absoluteFamily ? m.ratio * toKey : m.ratio;
        s.ampDb[s.n] = m.ampDb;
        s.t60[s.n] = m.t60;
        s.shape[s.n] = m.shape;
        s.invert[s.n] = m.invert;
        s.fam[s.n] = m.family;
        ++s.n;
    }
    s.gainDb = a.gainDb;
    s.contact = a.contact;
    s.noiseDb = a.noiseDb;
    s.noiseHz = a.noiseHz;
    s.noiseQ = a.noiseQ;
    s.noiseMs = a.noiseMs;
    s.riseMs = a.riseMs;
    return s;
}

float energyOf (const ModeSet& s, int k) noexcept
{
    const auto a = std::pow (10.0f, s.ampDb[k] / 20.0f);
    return a * a * std::min (s.t60[k], 3.0f);
}

// a weighted blend of two sets (t = the weight of b): the same mode on both sides (the same family id, round 190nl, when
// `families` - two keys of one layer; for modes without one, and across layers: within 4 % of each other) is one mode
// (frequency, level, T60 blended); the others fade with their side's weight
ModeSet blend (const ModeSet& a, const ModeSet& b, float t, bool families = true, int fade = 0)
{
    // (an unpaired mode's fade, dB per unit of 20 log10 (weight): 1 = amplitude, 0.5 = power; round 190nm)
    const float fadeDb = fade == 1 ? 10.0f : 20.0f;
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    ModeSet o;
    const auto lerp = [t] (float x, float y) { return x + (y - x) * t; };
    const auto glerp = [t] (float x, float y) { return std::exp (std::log (std::max (x, 1e-6f)) * (1.0f - t) + std::log (std::max (y, 1e-6f)) * t); };
    // pairs within 4 %, the closest first (a side line next to the main mode must not take the other set's main mode)
    int pairOf[setCap];
    bool used[setCap] {};
    std::fill (pairOf, pairOf + setCap, -1);
    struct Pair
    {
        float d;
        int i, j;
    };
    Pair pairs[setCap * 8];
    int np = 0;
    // (a family pairs across any distance; a tagged mode never pairs with a mode of another family)
    for (int i = 0; i < a.n; ++i)
        for (int j = 0; j < b.n && np < setCap * 8; ++j)
        {
            const auto d = std::abs (std::log (b.ratio[j] / a.ratio[i]));
            if (families && a.fam[i] != 0 && b.fam[j] != 0 && a.fam[i] != absoluteFamily && b.fam[j] != absoluteFamily)
            {
                if (a.fam[i] == b.fam[j])
                    pairs[np++] = { -1.0f + d * 1.0e-3f, i, j };
            }
            else if (d < 0.0392f) // (4 %; absolute lines pair so too)
                pairs[np++] = { d, i, j };
        }
    std::sort (pairs, pairs + np, [] (const Pair& x, const Pair& y) { return x.d < y.d || (x.d == y.d && (x.i < y.i || (x.i == y.i && x.j < y.j))); });
    for (int k = 0; k < np; ++k)
        if (pairOf[pairs[k].i] < 0 && ! used[pairs[k].j])
        {
            pairOf[pairs[k].i] = pairs[k].j;
            used[pairs[k].j] = true;
        }
    for (int i = 0; i < a.n && o.n < setCap; ++i)
    {
        if (const int best = pairOf[i]; best >= 0)
        {
            o.ratio[o.n] = glerp (a.ratio[i], b.ratio[best]);
            o.ampDb[o.n] = lerp (a.ampDb[i], b.ampDb[best]);
            o.t60[o.n] = glerp (a.t60[i], b.t60[best]);
            o.shape[o.n] = a.shape[i] != 0 ? a.shape[i] : b.shape[best];
            o.invert[o.n] = a.invert[i];
            o.fam[o.n] = a.fam[i];
        }
        else
        {
            o.ratio[o.n] = a.ratio[i];
            o.ampDb[o.n] = a.ampDb[i] + fadeDb * std::log10 (1.0f - t);
            o.t60[o.n] = a.t60[i];
            o.shape[o.n] = a.shape[i];
            o.invert[o.n] = a.invert[i];
            o.fam[o.n] = a.fam[i];
        }
        ++o.n;
    }
    for (int j = 0; j < b.n && o.n < setCap; ++j)
        if (! used[j])
        {
            o.ratio[o.n] = b.ratio[j];
            o.ampDb[o.n] = b.ampDb[j] + fadeDb * std::log10 (t);
            o.t60[o.n] = b.t60[j];
            o.shape[o.n] = b.shape[j];
            o.invert[o.n] = b.invert[j];
            o.fam[o.n] = b.fam[j];
            ++o.n;
        }
    o.gainDb = lerp (a.gainDb, b.gainDb);
    o.contact = glerp (a.contact, b.contact);
    o.noiseDb = lerp (a.noiseDb, b.noiseDb);
    o.noiseHz = glerp (a.noiseHz, b.noiseHz);
    o.noiseQ = glerp (a.noiseQ, b.noiseQ);
    o.noiseMs = glerp (a.noiseMs, b.noiseMs);
    o.riseMs = lerp (a.riseMs, b.riseMs);
    return o;
}

// one recorded key's layers at velocity v (outside the layers: the nearest one, its level following the velocity and
// its contact the Hertz law tau ~ v^-1/5)
ModeSet atVelocity (const Table& t, std::size_t first, std::size_t end, float v, float key)
{
    const auto& lo = t.anchors[first];
    const auto& hi = t.anchors[end - 1];
    if (v <= lo.velocity || end - first == 1)
    {
        auto s = fromAnchor (t, v <= lo.velocity ? lo : hi, key);
        const auto ref = v <= lo.velocity ? lo.velocity : hi.velocity;
        const auto r = std::max (v, 0.02f) / std::max (ref, 0.02f);
        s.gainDb += 20.0f * std::log10 (r);
        s.contact *= std::pow (r, -0.2f);
        return s;
    }
    if (v >= hi.velocity)
        return fromAnchor (t, hi, key);
    for (auto i = first; i + 1 < end; ++i)
        if (v <= t.anchors[i + 1].velocity)
        {
            const auto& a = t.anchors[i];
            const auto& b = t.anchors[i + 1];
            const auto w = (v - a.velocity) / std::max (b.velocity - a.velocity, 1e-6f);
            // (family ids belong to one layer: two layers of a key pair their modes by ratio)
            return blend (fromAnchor (t, a, key), fromAnchor (t, b, key), w, false);
        }
    return fromAnchor (t, hi, key);
}

// --- the strike-position law ------------------------------------------------------------------------------------------
constexpr double freeBeta[] = { 4.730040745, 7.853204624, 10.99560784, 14.13716549, 17.27875966, 20.42035225, 23.56194490, 26.70353756 };
constexpr double clampedBeta[] = { 1.875104069, 4.694091133, 7.854757438, 10.99554073, 14.13716839, 17.27875953, 20.42035225, 23.56194490 };
} // namespace

std::optional<Table> parseTable (const std::uint8_t* d, std::size_t size, std::string* error)
{
    const auto fail = [error] (const char* why) -> std::optional<Table> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (d == nullptr || size < 40 || std::memcmp (d, "LPM1", 4) != 0)
        return fail ("not an LPM1 table");
    if (u16 (d + 4) != 1)
        return fail ("unknown LPM version");
    Table t;
    t.model = u16 (d + 6);
    const int anchors = u16 (d + 8), modes = u16 (d + 10);
    t.beam = d[12];
    t.noiseOrder = d[13] == 2 ? 2 : 1; // (round 190nl; 0 in older tables)
    t.onset = d[14] == 1 || d[14] == 2 ? d[14] : 0; // (round 190nm; 0 in older tables; 2: round 190nn)
    t.fade = d[15] == 1 ? 1 : 0;       // (round 190nm; 0 in older tables)
    t.posRef = f32 (d + 16);
    t.posU0 = f32 (d + 20);
    t.posU1 = f32 (d + 24);
    t.gainOffsetDb = f32 (d + 28);
    std::memcpy (t.provenance.data(), d + 32, 8);
    if (t.model < 0 || t.model >= modelCount || anchors <= 0 || size != 40u + 32u * (std::size_t) anchors + 10u * (std::size_t) modes)
        return fail ("bad LPM size or model");
    const auto* a = d + 40;
    for (int i = 0; i < anchors; ++i, a += 32)
    {
        TableAnchor x;
        x.key = f32 (a);
        x.velocity = f32 (a + 4);
        x.gainDb = f32 (a + 8);
        x.contact = f32 (a + 12);
        x.noiseDb = f16 (a + 16);
        x.noiseHz = f16 (a + 18);
        x.noiseQ = f16 (a + 20);
        x.noiseMs = f16 (a + 22);
        x.first = u16 (a + 24);
        x.count = u16 (a + 26);
        x.riseMs = f32 (a + 28);
        if (! std::isfinite (x.riseMs) || x.riseMs < 0.0f || x.riseMs > 200.0f)
            return fail ("bad LPM anchor");
        if ((int) x.first + (int) x.count > modes || ! std::isfinite (x.key) || ! std::isfinite (x.gainDb) || ! (x.contact > 0.0f))
            return fail ("bad LPM anchor");
        if (! t.anchors.empty() && (x.key < t.anchors.back().key || (x.key == t.anchors.back().key && x.velocity < t.anchors.back().velocity)))
            return fail ("LPM anchors out of order");
        t.anchors.push_back (x);
    }
    const auto* m = d + 40 + 32 * anchors;
    for (int i = 0; i < modes; ++i, m += 10)
    {
        TableMode x;
        x.ratio = f32 (m);
        x.ampDb = f16 (m + 4);
        x.t60 = f16 (m + 6);
        x.shape = (std::uint8_t) (m[8] & 0x7f);
        x.invert = (m[8] & 0x80) != 0; // (round 190nm; never set before)
        x.family = m[9];
        if (! (x.ratio > 0.0f) || ! std::isfinite (x.ratio) || ! (x.t60 > 0.0f) || ! std::isfinite (x.ampDb))
            return fail ("bad LPM mode");
        t.modes.push_back (x);
    }
    t.bytes = size;
    return t;
}

const Table& builtInTable (int model) noexcept
{
    static const auto tables = [] {
        std::array<Table, modelCount> t;
        for (int i = 0; i < modelCount; ++i)
        {
            auto parsed = parseTable (blobs[i].data, blobs[i].size);
            t[(std::size_t) i] = parsed && parsed->model == i ? std::move (*parsed) : fallbackTable (i);
        }
        return t;
    }();
    return tables[(std::size_t) std::clamp (model, 0, modelCount - 1)];
}

std::size_t builtInTableBytes (int model) noexcept { return blobs[std::clamp (model, 0, modelCount - 1)].size; }

std::size_t tableMemoryBytes (const Table& t) noexcept
{
    return sizeof (Table) + t.anchors.capacity() * sizeof (TableAnchor) + t.modes.capacity() * sizeof (TableMode);
}

NoteModes interpolate (const Table& t, float key, float velocity)
{
    NoteModes out;
    if (t.anchors.empty())
        return out;
    // the recorded keys: [first, end) anchor ranges of equal key
    std::size_t lo = 0, hi = 0;
    {
        std::size_t i = 0;
        std::size_t prevStart = 0;
        bool found = false;
        while (i < t.anchors.size())
        {
            std::size_t j = i;
            while (j < t.anchors.size() && t.anchors[j].key == t.anchors[i].key)
                ++j;
            if (t.anchors[i].key >= key)
            {
                lo = i == 0 ? 0 : prevStart;
                hi = i;
                if (t.anchors[i].key == key || i == 0)
                    lo = hi = i;
                found = true;
                break;
            }
            prevStart = i;
            i = j;
        }
        if (! found)
            lo = hi = prevStart;
    }
    const auto endOf = [&t] (std::size_t s) {
        auto e = s;
        while (e < t.anchors.size() && t.anchors[e].key == t.anchors[s].key)
            ++e;
        return e;
    };
    const auto v = std::clamp (velocity, 0.0f, 1.0f);
    ModeSet s;
    if (lo == hi)
        s = atVelocity (t, lo, endOf (lo), v, key);
    else
    {
        const auto k0 = t.anchors[lo].key, k1 = t.anchors[hi].key;
        const auto w = (key - k0) / std::max (k1 - k0, 1e-6f);
        s = blend (atVelocity (t, lo, endOf (lo), v, key), atVelocity (t, hi, endOf (hi), v, key), w, true, t.fade);
    }
    // beyond the recorded keys: the nearest key's modes transposed, the T60s shorter by 2^-0.5 per octave up (higher
    // bars ring shorter) and longer by 2^0.25 per octave down
    const auto beyond = lo == hi ? (key - t.anchors[lo].key) / 12.0f : 0.0f;
    const auto t60Scale = beyond > 0.0f ? std::pow (2.0f, -0.5f * beyond) : beyond < 0.0f ? std::pow (2.0f, -0.25f * beyond) : 1.0f;
    // the strongest maxModes, then in frequency order
    int order[setCap];
    for (int k = 0; k < s.n; ++k)
        order[k] = k;
    std::sort (order, order + s.n, [&s] (int a, int b) { return energyOf (s, a) > energyOf (s, b); });
    // (round 190nm: the strongest that the voice can play - the build-up twins take 4 lanes; before, a note with more
    // than 44 modes and twins lost its highest-frequency modes in Engine::Impl::start, not its weakest)
    const int n = std::min (s.n, maxModes - (t.onset == 0 && s.riseMs > 0.05f ? 4 : 0));
    std::sort (order, order + n, [&s] (int a, int b) { return s.ratio[a] < s.ratio[b]; });
    const auto base = keyHz (key);
    const auto main = std::pow (10.0f, (s.gainDb + t.gainOffsetDb) / 20.0f);
    float bestMain = 1e9f;
    for (int i = 0; i < n; ++i)
    {
        const int k = order[i];
        out.hz[(std::size_t) i] = (float) (base * s.ratio[k]);
        out.amp[(std::size_t) i] = main * std::pow (10.0f, s.ampDb[k] / 20.0f);
        out.t60[(std::size_t) i] = std::max (s.t60[k] * t60Scale, 0.005f);
        out.shape[(std::size_t) i] = s.shape[k];
        out.invert[(std::size_t) i] = s.invert[k];
        if (const auto d = std::abs (std::log (s.ratio[k])); d < bestMain)
        {
            bestMain = d;
            out.mainHz = out.hz[(std::size_t) i];
        }
    }
    out.count = n;
    out.gain = main;
    out.contact = s.contact;
    out.noiseAmp = main * std::pow (10.0f, s.noiseDb / 20.0f);
    out.noiseHz = s.noiseHz;
    out.noiseQ = s.noiseQ;
    out.noiseMs = s.noiseMs;
    out.riseMs = s.riseMs;
    return out;
}

float modeShape (int beam, int shape, float u) noexcept
{
    if (shape <= 0)
        return 1.0f;
    const auto x = (double) std::clamp (u, 0.0f, 1.0f);
    const int k = shape - 1;
    if (k < 8)
    {
        const auto b = beam == 1 ? clampedBeta[k] : freeBeta[k];
        const auto bx = b * x;
        if (beam == 1)
        {
            const auto s = (std::cosh (b) + std::cos (b)) / (std::sinh (b) + std::sin (b));
            return (float) (std::cosh (bx) - std::cos (bx) - s * (std::sinh (bx) - std::sin (bx)));
        }
        const auto s = (std::cosh (b) - std::cos (b)) / (std::sinh (b) - std::sin (b));
        return (float) (std::cosh (bx) + std::cos (bx) - s * (std::sinh (bx) + std::sin (bx)));
    }
    // higher orders: the interior's asymptotic form (the end terms have died out)
    const auto b = beam == 1 ? (2 * shape - 1) * 1.5707963267948966 : (2 * shape + 1) * 1.5707963267948966;
    return (float) (beam == 1 ? std::sqrt (2.0) * std::sin (b * x - 0.7853981633974483) : std::sqrt (2.0) * std::cos (b * x + 0.7853981633974483));
}

const char* modelName (int model) noexcept
{
    static const char* n[modelCount] = { "Marimba", "Vibraphone", "Xylophone", "Glockenspiel", "Tubular Bells", "Kalimba" };
    return n[std::clamp (model, 0, modelCount - 1)];
}

const char* modelId (int model) noexcept
{
    static const char* n[modelCount] = { "marimba", "vibraphone", "xylophone", "glockenspiel", "tubularbells", "kalimba" };
    return n[std::clamp (model, 0, modelCount - 1)];
}

std::array<double, stateSize> presetValues (int model) noexcept
{
    model = std::clamp (model, 0, modelCount - 1);
    std::array<double, stateSize> v {};
    v[p::model] = model;
    v[p::hardness] = 0.5;
    v[p::position] = builtInTable (model).posRef;
    v[p::damping] = 0.5;
    v[p::resonator] = 0.5;
    v[p::motor] = 0.0;
    v[p::release] = model == vibraphone ? 0.35 : 1.0; // (a vibraphone's damper bar; the others ring on, as played)
    v[p::tone] = 0.0;
    v[p::velocity] = 0.5;
    v[p::volume] = 0.0;
    v[p::noise] = 0.0;
    return v;
}

const std::array<double, stateSize>& defaults() noexcept
{
    static const auto d = presetValues (marimba);
    return d;
}

std::string paramName (int index)
{
    static const char* n[paramCount] = { "Model", "Hardness", "Position", "Damping", "Resonator", "Motor", "Release", "Tone",
                                         "Velocity", "Volume", "Noise" };
    return index >= 0 && index < paramCount ? n[index] : "";
}

namespace
{
std::string fixed (double v, int decimals)
{
    char b[48];
    std::snprintf (b, sizeof (b), "%.*f", decimals, v);
    return b;
}
double releaseSeconds (double x) noexcept { return 0.03 * std::pow (600.0, std::clamp (x, 0.0, 1.0)); }
bool releaseRings (double x) noexcept { return x >= 0.995; }
} // namespace

std::string paramText (int index, double v)
{
    switch (index)
    {
        case p::model: return modelName ((int) std::lround (v));
        case p::hardness: case p::position: case p::damping: case p::resonator: case p::velocity:
            return fixed (100.0 * v, 0) + " %";
        case p::motor: return v < 0.05 ? "Off" : fixed (v, 1) + " Hz";
        case p::release:
        {
            if (releaseRings (v))
                return "Ring";
            const auto s = releaseSeconds (v);
            return s < 1.0 ? fixed (s * 1000.0, 0) + " ms" : fixed (s, 2) + " s";
        }
        case p::tone: return (v > 0.0005 ? "+" : "") + fixed (100.0 * v, 0) + " %";
        case p::volume: return (v > 0.05 ? "+" : "") + fixed (v, 1) + " dB";
        case p::noise: return v <= -39.95 ? "Off" : (v > 0.05 ? "+" : "") + fixed (v, 1) + " dB";
        default: break;
    }
    return fixed (v, 2);
}

// --- the engine ---------------------------------------------------------------------------------------------------------

struct Engine::Impl
{
    struct Voice
    {
        ModalBank bank;
        float rabs[maxModes] {}, omega[maxModes] {}, amw[maxModes] {};
        int modes = 0;
        int key = -1;
        bool active = false, released = false;
        std::uint32_t age = 0;
        // the contact force: an impulse through two one-pole low-passes (t e^(-t / tau))
        double c = 0.0, y1 = 0.0, y2 = 0.0;
        int pulseLeft = 0, pulsePos = 0, peakAt = 0;
        bool pulseStart = false;
        alignas (32) float kick[maxModes] {}; // the fast modes' amplitudes, added at the force's peak
        // the strike noise: white noise through a unity-peak band-pass (TPT state-variable filter); its envelope
        // follows the contact force up to the force's peak, then decays
        float nAmp = 0.0f, nEnv = 0.0f, nMul = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, nk = 1.0f, ic1 = 0.0f, ic2 = 0.0f;
        float ic3 = 0.0f, ic4 = 0.0f; // (the second band-pass section, tables with noiseOrder 2)
        bool nTwo = false;
        int nPos = 0, nPeakAt = 0;
        float nPeak = 1.0f;
        std::uint32_t rng = 1;
        float onLeft = 0.0f, onMul = 0.0f; // (round 190nm) the onset: the output is scaled by 1 - onLeft, onLeft *= onMul
        bool onSquared = false;            // (round 190nn) by (1 - onLeft)^2: the S-shaped onset of Table::onset 2
        float amGain = 1.0f;     // the motor's gain applied to the resonated modes now
        float pitchUsed = 0.0f;  // the pitch offset (semitones) the poles were set with
        float peakEnergy = 0.0f;
    };

    std::array<Voice, maxVoices> voices {};
    std::array<double, stateSize> values = defaults();
    double rate = 48000.0;
    const Table* override = nullptr;
    float pitch = 0.0f;
    double motorPhase = 0.0;
    std::uint32_t counter = 0, seeds = 0x1234567u;

    const Table& table() const noexcept { return override != nullptr ? *override : builtInTable ((int) std::lround (values[p::model])); }

    void setPoles (Voice& v, float semis) noexcept
    {
        const auto scale = std::pow (2.0f, semis / 12.0f);
        for (int k = 0; k < v.modes; ++k)
        {
            const auto w = (double) v.omega[k] * scale;
            if (w >= twoPi * 0.45)
            {
                v.bank.pr[k] = v.bank.pi[k] = 0.0f;
                continue;
            }
            v.bank.pr[k] = (float) (v.rabs[k] * std::cos (w));
            v.bank.pi[k] = (float) (v.rabs[k] * std::sin (w));
        }
        v.pitchUsed = semis;
    }

    void start (Voice& v, int key, float velocity, float cents) noexcept
    {
        const auto& t = table();
        const auto sens = std::clamp (values[p::velocity], 0.0, 1.0);
        const auto veff = sens <= 0.0 ? 1.0f : (float) std::pow ((double) std::clamp (velocity, 1.0f / 127.0f, 1.0f), 2.0 * sens);
        const auto nm = interpolate (t, (float) key + cents * 0.01f, veff);
        v = Voice {};
        v.key = key;
        v.active = true;
        v.age = ++counter;
        v.rng = (seeds = seeds * 1664525u + 1013904223u) | 1u;

        const auto hard = std::clamp (values[p::hardness], 0.0, 1.0);
        const auto tauRef = std::max ((double) nm.contact, 1.0e-5);
        const auto tau = tauRef * std::pow (2.0, (0.5 - hard) * 3.0);
        const auto cRef = std::exp (-1.0 / std::max (tauRef * rate, 0.05));
        v.c = std::exp (-1.0 / std::max (tau * rate, 0.05));
        v.pulseLeft = std::min ((int) std::ceil (std::max (tau * rate, 0.05) * 18.0) + 2, 8192);
        v.pulseStart = true;
        // the force (1 - c)^2 (n + 1) c^n peaks at n = -1 / ln c - 1
        v.peakAt = std::max (0, (int) std::lround (-1.0 / std::log (std::max (v.c, 1e-9)) - 1.0));
        v.pulsePos = 0;

        const auto pos = std::clamp (values[p::position], 0.0, 1.0);
        const auto u = t.posU0 + (t.posU1 - t.posU0) * (float) pos;
        const auto uRef = t.posU0 + (t.posU1 - t.posU0) * t.posRef;
        const auto damp = std::pow (2.0, (0.5 - std::clamp (values[p::damping], 0.0, 1.0)) * 4.0);
        const auto res = std::clamp (values[p::resonator], 0.0, 1.0);
        const auto resGain = std::pow (10.0, 24.0 * (res - 0.5) / 20.0);
        const auto resT60 = std::pow (2.0, -1.5 * (res - 0.5));
        const auto tone = std::clamp (values[p::tone], -1.0, 1.0);
        int m = 0;
        const bool twins = t.onset == 0 && nm.riseMs > 0.05f;
        const int room = maxModes - (twins ? 4 : 0); // (lanes left for the build-up twins)
        for (int k = 0; k < nm.count && m < room; ++k)
        {
            const auto hz = (double) nm.hz[(std::size_t) k];
            if (hz <= 0.0 || hz >= 0.45 * rate)
                continue;
            const auto w = twoPi * hz / rate;
            const bool resonated = std::abs (hz / nm.mainHz - 1.0) < 0.06;
            auto t60 = (double) nm.t60[(std::size_t) k] * damp * (resonated ? resT60 : 1.0);
            t60 = std::clamp (t60, 0.003, 120.0);
            // the contact's spectrum at this mode (exact for the two one-pole cascade): |H| = (1 - c)^2 / |1 - c e^-iw|^2
            const auto hOf = [w] (double c) { return (1.0 - c) * (1.0 - c) / (1.0 - 2.0 * c * std::cos (w) + c * c); };
            const auto h = hOf (v.c), hRef = hOf (cRef);
            auto target = (double) nm.amp[(std::size_t) k] * std::min (h / std::max (hRef, 1e-12), 8.0);
            target *= std::pow (hz / nm.mainHz, 0.75 * tone);
            if (resonated)
                target *= resGain;
            double sign = nm.invert[(std::size_t) k] ? -1.0 : 1.0;
            if (const int sh = nm.shape[(std::size_t) k]; sh > 0)
            {
                const auto a = (double) modeShape (t.beam, sh, u), r = (double) modeShape (t.beam, sh, uRef);
                target *= std::clamp (std::abs (a) / std::max (std::abs (r), 1e-6), 0.03, 4.0);
                sign *= a < 0.0 ? -1.0 : 1.0;
            }
            v.rabs[m] = (float) std::pow (10.0, -3.0 / (t60 * rate));
            v.omega[m] = (float) w;
            v.amw[m] = resonated ? 1.0f : 0.0f;
            // a mode much faster than the contact (|H| < 0.1) would follow the force quasi-statically, a low thump the
            // size of its drive: it gets its amplitude as one kick at the force's peak instead
            if (h >= hDrive)
                v.bank.drive[m] = (float) (sign * target / h);
            else
                v.kick[m] = (float) (sign * target);
            ++m;
        }
        // the tube's build-up: each resonated mode gets a twin of opposite sign that dies with the rise time constant,
        // so the pair grows as e^(-t/T) - e^(-t/rise)
        if (twins)
        {
            const auto twinR = (float) std::exp (-1.0 / (nm.riseMs * 0.001 * rate));
            for (int k = 0, end = m; k < end && m < maxModes; ++k)
                if (v.amw[k] != 0.0f)
                {
                    v.rabs[m] = std::min (twinR, v.rabs[k]);
                    v.omega[m] = v.omega[k];
                    v.amw[m] = 1.0f;
                    v.bank.drive[m] = -v.bank.drive[k];
                    v.kick[m] = -v.kick[k];
                    ++m;
                }
        }
        // (round 190nm) the whole note's onset: the voice's output rises as 1 - e^(-t / rise)
        if (t.onset != 0 && nm.riseMs > 0.05f)
        {
            v.onMul = (float) std::exp (-1.0 / (nm.riseMs * 0.001 * rate));
            v.onLeft = 1.0f;
            v.onSquared = t.onset == 2;
        }
        v.modes = m;
        v.bank.groups = (m + laneGroup - 1) / laneGroup;
        setPoles (v, pitch);

        // strike noise: harder mallets click louder and brighter
        const auto nDb = values[p::noise];
        if (nDb > -39.95 && nm.noiseAmp > 0.0f)
        {
            const auto hr = tauRef / tau;
            const auto fc = std::clamp ((double) nm.noiseHz * std::sqrt (hr), 60.0, 0.42 * rate);
            const auto q = std::clamp ((double) nm.noiseQ, 0.4, 8.0);
            const auto g = std::tan (3.141592653589793 * fc / rate);
            v.nk = (float) (1.0 / q);
            v.nTwo = t.noiseOrder == 2;
            v.a1 = (float) (1.0 / (1.0 + g * (g + 1.0 / q)));
            v.a2 = (float) (g * v.a1);
            v.a3 = (float) (g * v.a2);
            v.nAmp = (float) ((double) nm.noiseAmp * std::pow (10.0, nDb / 20.0) * std::pow (hr, 0.7));
            v.nEnv = 1.0f;
            v.nMul = (float) std::exp (-1.0 / (std::max ((double) nm.noiseMs, 0.5) * 0.001 * rate));
            v.nPeakAt = v.peakAt;
            v.nPeak = (float) ((1.0 - v.c) * (1.0 - v.c) * (v.nPeakAt + 1) * std::pow (v.c, (double) v.nPeakAt));
            v.nPos = 0;
        }
        v.peakEnergy = 0.0f;
    }

    void release (Voice& v) noexcept
    {
        v.released = true;
        const auto x = values[p::release];
        if (releaseRings (x))
            return;
        const auto r = (float) std::pow (10.0, -3.0 / (releaseSeconds (x) * rate));
        bool changed = false;
        for (int k = 0; k < v.modes; ++k)
            if (v.rabs[k] > r)
            {
                v.rabs[k] = r;
                changed = true;
            }
        if (changed)
            setPoles (v, v.pitchUsed);
    }

    Voice& freeVoice() noexcept
    {
        Voice* best = nullptr;
        for (auto& v : voices)
            if (! v.active)
                return v;
        // steal the quietest (its energy re its own peak, the oldest on a tie)
        float bestE = 0.0f;
        for (auto& v : voices)
        {
            const auto e = modalEnergy (v.bank) / std::max (v.peakEnergy, 1e-30f) + (v.pulseLeft > 0 ? 10.0f : 0.0f);
            if (best == nullptr || e < bestE || (e == bestE && v.age < best->age))
            {
                best = &v;
                bestE = e;
            }
        }
        return *best;
    }

    void renderVoice (Voice& v, float* out, int n) noexcept
    {
        if (v.onLeft <= 1.0e-4f)
        {
            renderVoiceInto (v, out, n);
            return;
        }
        // (the onset: the voice into its own buffer, scaled while 1 - e^(-t / rise) rises; a few ms per note)
        alignas (32) float own[maxBlock];
        std::fill (own, own + n, 0.0f);
        renderVoiceInto (v, own, n);
        auto left = v.onLeft;
        if (v.onSquared)
            for (int i = 0; i < n; ++i)
            {
                const auto g = 1.0f - left;
                out[i] += own[i] * g * g;
                left *= v.onMul;
            }
        else
            for (int i = 0; i < n; ++i)
            {
                out[i] += own[i] * (1.0f - left);
                left *= v.onMul;
            }
        v.onLeft = left;
    }

    void renderVoiceInto (Voice& v, float* out, int n) noexcept
    {
        alignas (32) float force[maxBlock];
        int done = 0;
        if (v.pulseLeft > 0)
        {
            const int m = std::min (v.pulseLeft, n);
            const auto g = 1.0 - v.c;
            for (int i = 0; i < m; ++i)
            {
                const double x = v.pulseStart ? 1.0 : 0.0;
                v.pulseStart = false;
                v.y1 = g * x + v.c * v.y1;
                v.y2 = g * v.y1 + v.c * v.y2;
                force[i] = (float) v.y2;
            }
            const int k = v.peakAt - v.pulsePos; // (the kick lands before sample k of this block)
            if (k >= 0 && k < m)
            {
                runModes (v.bank, force, out, k);
                for (int j = 0; j < v.modes; ++j)
                    v.bank.re[j] += v.kick[j];
                runModes (v.bank, force + k, out + k, m - k);
            }
            else
                runModes (v.bank, force, out, m);
            v.pulseLeft -= m;
            v.pulsePos += m;
            done = m;
        }
        if (done < n)
            runModes (v.bank, nullptr, out + done, n - done);
        if (v.nAmp > 0.0f && v.nEnv > 1.0e-4f)
        {
            for (int i = 0; i < n; ++i)
            {
                v.rng = v.rng * 1664525u + 1013904223u;
                const auto x = (float) (std::int32_t) v.rng * (1.0f / 2147483648.0f);
                const auto v3 = x - v.ic2;
                const auto v1 = v.a1 * v.ic1 + v.a2 * v3;
                const auto v2 = v.ic2 + v.a2 * v.ic1 + v.a3 * v3;
                v.ic1 = 2.0f * v1 - v.ic1;
                v.ic2 = 2.0f * v2 - v.ic2;
                auto bp = v1 * v.nk; // (unity at the centre)
                if (v.nTwo)
                {
                    const auto w3 = bp - v.ic4;
                    const auto w1 = v.a1 * v.ic3 + v.a2 * w3;
                    const auto w2 = v.ic4 + v.a2 * v.ic3 + v.a3 * w3;
                    v.ic3 = 2.0f * w1 - v.ic3;
                    v.ic4 = 2.0f * w2 - v.ic4;
                    bp = w1 * v.nk;
                }
                float env;
                if (v.nPos < v.nPeakAt)
                    env = i < done ? force[i] / v.nPeak : 1.0f; // (rising with the contact force)
                else
                {
                    env = v.nEnv;
                    v.nEnv *= v.nMul;
                }
                ++v.nPos;
                out[i] += bp * v.nAmp * env;
            }
        }
        const auto e = modalEnergy (v.bank);
        v.peakEnergy = std::max (v.peakEnergy, e);
        if (v.pulseLeft <= 0 && (v.nAmp <= 0.0f || v.nEnv <= 1.0e-4f) && (e < v.peakEnergy * 1.0e-12f || e < 1.0e-16f))
            v.active = false;
    }

    void applyMotor (float depth, double phaseEnd) noexcept
    {
        const auto g = (float) (1.0 - depth * (0.5 - 0.5 * std::cos (phaseEnd)));
        for (auto& v : voices)
        {
            if (! v.active || v.amGain == g)
                continue;
            const auto ratio = g / v.amGain;
            for (int k = 0; k < v.modes; ++k)
                if (v.amw[k] != 0.0f)
                {
                    v.bank.re[k] *= ratio;
                    v.bank.im[k] *= ratio;
                }
            v.amGain = g;
        }
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate)
{
    impl->rate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
}

double Engine::sampleRate() const noexcept { return impl->rate; }

void Engine::setValues (const double* v) noexcept
{
    for (int i = 0; i < stateSize; ++i)
        if (std::isfinite (v[i]))
            impl->values[(std::size_t) i] = v[i];
}

void Engine::setParam (int index, double value) noexcept
{
    if (index >= 0 && index < stateSize && std::isfinite (value))
        impl->values[(std::size_t) index] = value;
}

double Engine::param (int index) const noexcept { return index >= 0 && index < stateSize ? impl->values[(std::size_t) index] : 0.0; }
void Engine::setTable (const Table* table) noexcept { impl->override = table; }

void Engine::note (int key, float velocity, float cents) noexcept
{
    key = std::clamp (key, 0, 127);
    if (velocity <= 0.0f)
    {
        for (auto& v : impl->voices)
            if (v.active && v.key == key && ! v.released)
                impl->release (v);
        return;
    }
    impl->start (impl->freeVoice(), key, velocity, cents);
}

void Engine::setPitchOffset (float semitones) noexcept { impl->pitch = std::isfinite (semitones) ? semitones : 0.0f; }

void Engine::allNotesOff() noexcept
{
    for (auto& v : impl->voices)
        if (v.active && ! v.released)
            impl->release (v);
}

void Engine::reset() noexcept
{
    for (auto& v : impl->voices)
        v = Impl::Voice {};
    impl->motorPhase = 0.0;
    impl->seeds = 0x1234567u;
    impl->counter = 0;
}

void Engine::render (float* left, float* right, int n) noexcept
{
    auto& s = *impl;
    for (auto& v : s.voices)
        if (v.active && v.pitchUsed != s.pitch)
            s.setPoles (v, s.pitch);
    const auto motor = std::clamp (s.values[p::motor], 0.0, 10.0);
    const auto depth = (float) (0.8 * std::min (1.0, 2.0 * std::clamp (s.values[p::resonator], 0.0, 1.0)));
    const int step = motor > 0.05 ? 32 : maxBlock;
    const auto gain = (float) std::pow (10.0, std::clamp (s.values[p::volume], -40.0, 6.0) / 20.0);
    alignas (32) float mono[maxBlock];
    for (int pos = 0; pos < n; pos += step)
    {
        const int c = std::min (step, n - pos);
        std::fill (mono, mono + c, 0.0f);
        for (auto& v : s.voices)
            if (v.active)
                s.renderVoice (v, mono, c);
        if (motor > 0.05)
        {
            s.motorPhase = std::fmod (s.motorPhase + twoPi * motor * c / s.rate, twoPi);
            s.applyMotor (depth, s.motorPhase);
        }
        else
            s.applyMotor (0.0f, 0.0);
        for (int i = 0; i < c; ++i)
        {
            left[pos + i] += mono[i] * gain;
            right[pos + i] += mono[i] * gain;
        }
    }
}

bool Engine::idle() const noexcept
{
    for (const auto& v : impl->voices)
        if (v.active)
            return false;
    return true;
}

int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : impl->voices)
        n += v.active ? 1 : 0;
    return n;
}

std::size_t Engine::footprintBytes() noexcept { return sizeof (Engine) + sizeof (Impl); }

int Engine::modesOfVoice (int voice) const noexcept
{
    return voice >= 0 && voice < maxVoices && impl->voices[(std::size_t) voice].active ? impl->voices[(std::size_t) voice].modes : 0;
}

std::vector<float> renderReference (const std::vector<double>& state, const std::vector<RefNote>& notes, double sampleRate, int length,
                                    int block, const Table* table)
{
    Engine e;
    e.prepare (sampleRate);
    std::array<double, stateSize> v = defaults();
    for (std::size_t i = 0; i < state.size() && i < v.size(); ++i)
        v[i] = state[i];
    e.setValues (v.data());
    e.setTable (table);
    std::vector<float> out ((std::size_t) std::max (length, 0) * 2, 0.0f);
    std::vector<float> l ((std::size_t) std::max (block, 1)), r ((std::size_t) std::max (block, 1));
    block = std::clamp (block, 1, maxBlock);
    for (int pos = 0; pos < length; pos += block)
    {
        const int c = std::min (block, length - pos);
        for (const auto& nt : notes)
        {
            if (nt.start >= pos && nt.start < pos + c)
                e.note (nt.key, nt.velocity, nt.cents);
            if (nt.release >= pos && nt.release < pos + c)
                e.note (nt.key, 0.0f);
        }
        std::fill (l.begin(), l.begin() + c, 0.0f);
        std::fill (r.begin(), r.begin() + c, 0.0f);
        e.render (l.data(), r.data(), c);
        for (int i = 0; i < c; ++i)
        {
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i];
            out[(std::size_t) (pos + i) * 2 + 1] = r[(std::size_t) i];
        }
    }
    return out;
}
} // namespace lm

// --- the channel's values

std::vector<float> lynxModelPreset (int model)
{
    const auto v = lm::presetValues (model);
    return std::vector<float> (v.begin(), v.end());
}

std::vector<float> lynxModelDefaults() { return lynxModelPreset (lm::marimba); }

std::vector<double> lynxModelState (const ChannelDef& channel)
{
    const auto& d = lm::defaults();
    std::vector<double> s (d.begin(), d.end());
    for (std::size_t i = 0; i < s.size() && i < channel.modelParams.size(); ++i)
        if (std::isfinite (channel.modelParams[i]))
            s[i] = channel.modelParams[i];
    return s;
}

const std::vector<EngineParam>& lynxModelEngineParams()
{
    static const auto list = [] {
        struct Spec
        {
            float low, high;
            bool stepped;
        };
        const Spec spec[lm::paramCount] = { { 0, 5, true }, { 0, 1, false }, { 0, 1, false }, { 0, 1, false }, { 0, 1, false }, { 0, 10, false },
                                            { 0, 1, false }, { -1, 1, false }, { 0, 1, false }, { -40, 6, false }, { -40, 12, false } };
        std::vector<EngineParam> v;
        for (int i = 0; i < lm::paramCount; ++i)
        {
            EngineParam p;
            p.index = i;
            p.name = lm::paramName (i);
            p.low = spec[i].low;
            p.high = spec[i].high;
            p.stepped = spec[i].stepped;
            v.push_back (std::move (p));
        }
        return v;
    }();
    return list;
}

namespace modelparams
{
namespace
{
std::optional<float> getLynxModel (const ChannelDef& c, const EngineParam& p)
{
    if (c.instrument != InstrumentKind::lynxModel || p.index < 0 || p.index >= lm::paramCount)
        return std::nullopt;
    return p.index < (int) c.modelParams.size() ? c.modelParams[(std::size_t) p.index] : (float) lm::defaults()[(std::size_t) p.index];
}

bool setLynxModel (ChannelDef& c, const EngineParam& p, float raw)
{
    if (c.instrument != InstrumentKind::lynxModel || p.index < 0 || p.index >= lm::paramCount)
        return false;
    if ((int) c.modelParams.size() < lm::stateSize)
    {
        const auto d = lynxModelDefaults();
        const auto have = c.modelParams.size();
        c.modelParams.resize ((std::size_t) lm::stateSize);
        std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c.modelParams.begin() + (std::ptrdiff_t) have);
    }
    c.modelParams[(std::size_t) p.index] = raw;
    return true;
}
} // namespace

Table lynxModelModelTable() { return { &lynxModelEngineParams(), &getLynxModel, &setLynxModel }; }
} // namespace modelparams

// --- the instrument

namespace
{
class LynxModelInstrument final : public Instrument
{
public:
    LynxModelInstrument()
    {
        const auto& d = lm::defaults();
        for (int i = 0; i < lm::stateSize; ++i)
            values[(std::size_t) i].store ((float) d[(std::size_t) i]);
    }

    void prepare (double sampleRate) noexcept override
    {
        engine.prepare (sampleRate);
        rate = sampleRate;
        taken = 0;
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        pitchCents.store (channel.sampler.pitch * 100.0f);
        const auto& d = lm::defaults();
        bool changed = false;
        for (int i = 0; i < lm::stateSize; ++i)
        {
            auto v = i < (int) channel.modelParams.size() ? channel.modelParams[(std::size_t) i] : (float) d[(std::size_t) i];
            if (! std::isfinite (v))
                v = (float) d[(std::size_t) i];
            if (values[(std::size_t) i].exchange (v) != v)
                changed = true;
        }
        if (changed)
            version.fetch_add (1);
    }

    void setParameter (int index, float value) noexcept override
    {
        const auto* p = index >= 0 && index < lm::paramCount ? &lynxModelEngineParams()[(std::size_t) index] : nullptr;
        if (p == nullptr || ! std::isfinite (value))
            return;
        const auto raw = p->raw (value);
        values[(std::size_t) index].store (raw);
        engine.setParam (index, raw);
    }

    void setNextNoteExpression (float, float cents, bool) noexcept override { nextCents = cents; }
    void setChannelPitchNow (float knob, float rest) noexcept override { live.set (knob, rest); }
    void pitchBend (float semitones) noexcept override { bend = std::isfinite (semitones) ? semitones : 0.0f; }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        engine.note (std::clamp (key, 0, 127), std::max (std::clamp (velocity, 0.0f, 1.0f), 1.0f / 127.0f), nextCents - liveRest());
        nextCents = 0.0f;
    }
    void noteOff (int key) noexcept override { engine.note (std::clamp (key, 0, 127), 0.0f); }
    void allNotesOff() noexcept override { engine.allNotesOff(); }
    void panic() noexcept override { engine.reset(); }
    bool idle() const noexcept override { return engine.idle(); }

    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override
    {
        take();
        if (n <= 0 || engine.sampleRate() != rate)
            return;
        engine.setPitchOffset ((std::isnan (live.knob) ? pitchCents.load() : live.now()) * 0.01f + bend);
        for (int pos = 0; pos < n; pos += chunk)
        {
            const auto c = std::min (chunk, n - pos);
            std::fill (bufL, bufL + c, 0.0f);
            std::fill (bufR, bufR + c, 0.0f);
            engine.render (bufL, bufR, c);
            for (int i = 0; i < c; ++i)
            {
                left[pos + i] += bufL[i] * gainLeft;
                right[pos + i] += bufR[i] * gainRight;
            }
        }
    }

private:
    float liveRest() const noexcept { return std::isnan (live.rest) ? 0.0f : live.rest; }

    void take() noexcept
    {
        const auto v = version.load();
        if (v == taken)
            return;
        taken = v;
        std::array<double, lm::stateSize> state {};
        for (int i = 0; i < lm::stateSize; ++i)
            state[(std::size_t) i] = values[(std::size_t) i].load (std::memory_order_relaxed);
        engine.setValues (state.data());
    }

    static constexpr int chunk = lm::maxBlock;
    lm::Engine engine;
    std::array<std::atomic<float>, lm::stateSize> values {};
    std::atomic<std::uint32_t> version { 1 };
    std::uint32_t taken = 0;
    std::atomic<float> pitchCents { 0.0f };
    LiveChannelPitch live;
    float nextCents = 0.0f, bend = 0.0f;
    double rate = 48000.0;
    float bufL[chunk] {}, bufR[chunk] {};
};
} // namespace

std::unique_ptr<Instrument> makeLynxModelInstrument() { return std::make_unique<LynxModelInstrument>(); }
} // namespace lynx
