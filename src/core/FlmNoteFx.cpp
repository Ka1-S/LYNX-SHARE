#include "core/FlmNoteFx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
#include <random>

namespace lynx::flmnotefx
{
namespace
{
constexpr std::uint16_t maskOf (std::initializer_list<int> intervals) noexcept
{
    std::uint16_t m = 0;
    for (const auto i : intervals)
        m = (std::uint16_t) (m | (1u << i));
    return m;
}

// FLM's Scale list in its order (the names and interval sets are music theory; the direction is FLM's choice per scale)
constexpr ScaleInfo scales[scaleCount] = {
    { "Chromatic", 0x0FFF, 0 },
    { "Major", maskOf ({ 0, 2, 4, 5, 7, 9, 11 }), 1 },
    { "Major Bebop", maskOf ({ 0, 2, 4, 5, 7, 8, 9, 11 }), 1 },
    { "Major Bulgarian", maskOf ({ 0, 2, 5, 6, 8, 9, 11 }), 1 },
    { "Major Pentatonic", maskOf ({ 0, 2, 4, 7, 9 }), 1 },
    { "Major Persian", maskOf ({ 0, 1, 4, 5, 6, 8, 11 }), 1 },
    { "Major Polymode", maskOf ({ 0, 2, 3, 5, 6, 8, 10, 11 }), 1 },
    { "Minor Harmonic", maskOf ({ 0, 2, 3, 5, 7, 8, 11 }), -1 },
    { "Minor Hungarian", maskOf ({ 0, 1, 2, 3, 6, 7, 8, 11 }), -1 },
    { "Minor Melodic", maskOf ({ 0, 2, 3, 5, 7, 9, 11 }), -1 },
    { "Minor Natural", maskOf ({ 0, 2, 3, 5, 7, 8, 10 }), -1 },
    { "Minor Neapolitan", maskOf ({ 0, 1, 3, 5, 7, 8, 11 }), -1 },
    { "Minor Pentatonic", maskOf ({ 0, 3, 5, 7, 10 }), -1 },
    { "Minor Polymode", maskOf ({ 0, 1, 2, 4, 5, 7, 8, 10 }), -1 },
    { "Minor Romanian", maskOf ({ 0, 2, 3, 6, 7, 9, 10 }), -1 },
    { "Arabic", maskOf ({ 0, 1, 4, 5, 7, 8, 11 }), -1 },
    { "Blues", maskOf ({ 0, 3, 5, 6, 7, 10 }), -1 },
    { "Diminished", maskOf ({ 0, 2, 3, 5, 6, 8, 9, 11 }), -1 },
    { "Dominant Bebop", maskOf ({ 0, 2, 4, 5, 7, 9, 10, 11 }), -1 },
    { "Dorian", maskOf ({ 0, 2, 3, 5, 7, 9, 10 }), -1 },
    { "Egyptian", maskOf ({ 0, 2, 5, 7, 10 }), -1 },
    { "Enigmatic", maskOf ({ 0, 1, 4, 6, 8, 10, 11 }), 1 },
    { "Hirajoshi", maskOf ({ 0, 2, 3, 7, 8 }), -1 },
    { "Iwato", maskOf ({ 0, 1, 5, 6, 10 }), -1 },
    { "Japanese Insen", maskOf ({ 0, 1, 5, 7, 10 }), -1 },
    { "Locrian", maskOf ({ 0, 1, 3, 5, 6, 8, 10 }), -1 },
    { "Locrian Super", maskOf ({ 0, 1, 3, 4, 6, 8, 10 }), -1 },
    { "Lydian", maskOf ({ 0, 2, 4, 6, 7, 9, 11 }), 1 },
    { "Mixolydian", maskOf ({ 0, 2, 4, 5, 7, 9, 10 }), -1 },
    { "Neapolitan", maskOf ({ 0, 1, 3, 5, 7, 9, 11 }), -1 },
    { "Phrygian", maskOf ({ 0, 1, 3, 5, 7, 8, 10 }), -1 },
    { "Phrygian Dominant", maskOf ({ 0, 1, 4, 5, 7, 8, 10 }), -1 },
    { "Whole Tone", maskOf ({ 0, 2, 4, 6, 8, 10 }), 1 },
};

std::uint16_t rotated (std::uint16_t intervals, int root) noexcept
{
    std::uint16_t m = 0;
    for (int i = 0; i < 12; ++i)
        if ((intervals >> i) & 1u)
            m = (std::uint16_t) (m | (1u << ((i + root) % 12)));
    return m;
}

float at (std::span<const float> p, std::size_t i, float fallback) noexcept { return i < p.size() ? p[i] : fallback; }
bool enabled (std::span<const float> p) noexcept { return at (p, 0, 1.0f) >= 0.5f; }
bool inMask (std::uint16_t mask, int key) noexcept { return key >= 0 && key <= 127 && ((mask >> (key % 12)) & 1u) != 0; }
} // namespace

const ScaleInfo& scaleInfo (int index) noexcept { return scales[(std::size_t) std::clamp (index, 0, scaleCount - 1)]; }

int transposeShift (std::span<const float> params) noexcept
{
    if (! enabled (params))
        return 0;
    const auto octave = (int) std::lround (std::clamp (8.0 * (double) at (params, 1, 0.5f) - 4.0, -4.0, 4.0));
    const auto semitone = (int) std::lround (std::clamp (24.0 * (double) at (params, 2, 0.5f) - 12.0, -12.0, 12.0));
    return semitone + 12 * octave;
}

ScaleSetting scaleSetting (std::span<const float> params) noexcept
{
    ScaleSetting s;
    s.enabled = enabled (params);
    s.snap = at (params, 3, 0.0f) > 0.5f;
    const auto root = std::clamp ((int) std::lround (11.0 * (double) at (params, 1, 0.0f)), 0, 11);
    const auto index = std::clamp ((int) std::lround (33.0 * (double) at (params, 2, 0.0f)), 0, scaleCount);
    if (params.size() >= 16)
    {
        s.mask = 0;
        for (int i = 0; i < 12; ++i)
            if (params[(std::size_t) (4 + i)] > 0.5f)
                s.mask = (std::uint16_t) (s.mask | (1u << i));
    }
    else // (a file without the switches: the named scale)
        s.mask = index < scaleCount ? rotated (scales[index].mask, root) : (std::uint16_t) 0x0FFF;
    // the switches matching a named scale at this root: that scale's direction; anything else is a custom set
    s.direction = 0;
    if (index < scaleCount && rotated (scales[index].mask, root) == s.mask)
        s.direction = scales[index].direction;
    else
        for (const auto& named : scales)
            if (rotated (named.mask, root) == s.mask)
            {
                s.direction = named.direction;
                break;
            }
    return s;
}

int scaleKey (const ScaleSetting& s, int key) noexcept
{
    if ((s.mask & 0x0FFF) == 0 || inMask (s.mask, key))
        return key;
    if (s.direction != 0)
    {
        for (int k = 1; k < 12; ++k)
        {
            if (inMask (s.mask, key + k * s.direction))
                return key + k * s.direction;
            if (inMask (s.mask, key - k * s.direction))
                return key - k * s.direction;
        }
        return key;
    }
    for (int d = 1; d < 12; ++d)
    {
        if (inMask (s.mask, key + d))
            return key + d;
        if (inMask (s.mask, key - d))
            return key - d;
    }
    return key;
}

bool Chain::add (std::uint32_t type, std::span<const float> params)
{
    if (type != transpose && type != scale && type != randomizer)
        return false;
    if (! enabled (params))
        return true; // (passes every note through)
    Step s;
    s.type = type;
    if (type == transpose)
    {
        s.shift = transposeShift (params);
        if (s.shift == 0)
            return true;
    }
    else if (type == scale)
    {
        s.scale = scaleSetting (params);
        if ((s.scale.mask & 0x0FFF) == 0x0FFF || (s.scale.mask & 0x0FFF) == 0)
            return true;
        hasFilter = hasFilter || ! s.scale.snap;
    }
    else
    {
        s.range = std::clamp ((int) std::lround (24.0 * (double) at (params, 1, 0.0f)), 0, 24);
        s.velocity = std::clamp (at (params, 2, 0.0f), 0.0f, 1.0f);
        s.pan = std::clamp (at (params, 3, 0.0f), 0.0f, 1.0f);
        s.gate = std::clamp (at (params, 4, 1.0f), 0.0f, 1.0f);
        if (s.range == 0 && s.velocity <= 0.0f && s.pan <= 0.0f && s.gate >= 1.0f)
            return true;
        hasRandom = true;
    }
    steps.push_back (s);
    return true;
}

double Chain::uniform() noexcept
{
    // (a small generator of LYNX's own: FLM's draws differ at every playback anyway)
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (double) (state >> 8) / 16777216.0;
}

bool Chain::apply (Note& n)
{
    for (const auto& s : steps)
    {
        if (s.type == transpose)
            n.key = std::clamp (n.key + s.shift, 0, 127);
        else if (s.type == scale)
        {
            const auto key = scaleKey (s.scale, n.key);
            if (key != n.key && ! s.scale.snap)
                return false;
            n.key = key;
        }
        else
        {
            if (s.gate <= 0.0f || (s.gate < 1.0f && (double) s.gate < uniform()))
                return false;
            if (s.range > 0)
                n.key = std::clamp (n.key + (int) std::floor (uniform() * (2 * s.range + 1)) - s.range, 0, 127);
            if (s.velocity > 0.0f)
                n.velocity = std::clamp (n.velocity + (float) ((2.0 * uniform() - 1.0) * s.velocity), 0.0f, 1.0f);
            if (s.pan > 0.0f)
                n.pan = (float) ((std::clamp (2.0 * n.pan - 1.0 + (2.0 * uniform() - 1.0) * s.pan, -1.0, 1.0) + 1.0) * 0.5);
        }
    }
    return true;
}
} // namespace lynx::flmnotefx

// --- round 190de: Arpeggiator and Note Echo (FLM_FORMAT §7.8; PROVEN-REA in FLM's FL plugin, laws in LYNX's words) ---
namespace lynx::flmnotefx
{
namespace
{
int roundAway (double x) noexcept { return (int) (x >= 0.0 ? std::floor (x + 0.5) : -std::floor (-x + 0.5)); }
// a stored value → one of n steps (round half away from zero)
int steps (float v, int n) noexcept { return std::clamp (roundAway ((double) (n - 1) * std::clamp ((double) v, 0.0, 1.0)), 0, n - 1); }
int cmod (int a, int b) noexcept { return b == 0 ? 0 : a % b; } // (C's remainder)

constexpr double rateBeats[] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 2.0, -1.0 }; // −1 = one bar
constexpr double rateMul[] = { 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 };
constexpr int oldRate[] = { 0, 2, 4, 6, 8, 9, 10, 11, 12, 13, 14 }; // 31-value files: per-arp rate at 16 / 27, 11 steps
struct Retrig { bool notes; double beats; int bars; };
constexpr Retrig retrigs[] = { { false, 0, 0 }, { true, 0, 0 }, { false, 0.5, 0 }, { false, 1, 0 }, { false, 1.5, 0 }, { false, 2, 0 },
                               { false, 3, 0 }, { false, 0, 1 }, { false, 0, 2 }, { false, 0, 3 }, { false, 0, 4 }, { false, 0, 8 } };
const char* const rhythms[] = { "10", "101", "1001", "1011", "10101", "100101", "100110", "101011", "1001010", "1001011", "1001101",
                                "1010101", "1011011", "10001011", "10001101", "10010010", "10010110", "10010111", "10011010", "10101011", "10101101" };

std::vector<int> cascade (const std::vector<int>& a, bool up, bool first)
{
    std::vector<int> out;
    const auto n = (int) a.size();
    if (n == 0)
        return out;
    const int stepsUp[] = { 1, 1, -1 }, stepsDown[] = { -1, -1, 1 };
    const auto* st = up ? stepsUp : stepsDown;
    const auto i0 = up ? 0 : n - 1;
    if (first)
        out.push_back (a[(std::size_t) i0]);
    int k = 0, u = i0 + st[0];
    while (u >= 0 && u < n)
    {
        k = (k + 1) % 3;
        out.push_back (a[(std::size_t) u]);
        u += st[k];
    }
    return out;
}

// the sequence of a mode over the held keys (arrival order), and each played key's source key
std::vector<int> buildSequence (int mode, const std::vector<int>& held, int range, int start, std::map<int, int>& source)
{
    const auto R = range + 1;
    source.clear();
    std::vector<int> expanded;
    for (const auto key : held)
        for (int o = 0; o < R; ++o)
        {
            const auto k = key + 12 * o;
            if (k >= 128)
                continue;
            const auto it = source.find (k);
            if (it == source.end())
            {
                source[k] = key;
                expanded.push_back (k);
            }
            else if (key == k || (it->second != k && key < it->second))
                it->second = key;
        }
    if (mode == 0)
        return expanded;
    std::vector<int> a;
    for (const auto& [k, src] : source)
        a.push_back (k);
    const auto n = (int) a.size();
    const auto at = [&a] (int i) { return a[(std::size_t) i]; };
    std::vector<int> out;
    const auto upDown = [] (std::vector<int> s) {
        if (s.size() > 1)
            for (auto i = (int) s.size() - 2; i >= 1; --i)
                s.push_back (s[(std::size_t) i]);
        return s;
    };
    switch (mode)
    {
        case 1: case 2: return a;
        case 3: return std::vector<int> (a.rbegin(), a.rend());
        case 4: return upDown (a);
        case 5: return upDown (std::vector<int> (a.rbegin(), a.rend()));
        case 6:
        {
            const auto h = n / 2;
            int lo, hi;
            if (n % 2 == 0)
                out = { at (h - 1), at (h) }, lo = h - 2, hi = h + 1;
            else
                out = { at (h) }, lo = h - 1, hi = h + 1;
            for (; lo >= 0 || hi < n; --lo, ++hi)
            {
                if (lo >= 0)
                    out.push_back (at (lo));
                if (hi < n)
                    out.push_back (at (hi));
            }
            return out;
        }
        case 7:
            for (int i = 0, j = n - 1; i <= j; ++i, --j)
            {
                if (i != j)
                    out.push_back (at (i));
                out.push_back (at (j));
            }
            return out;
        case 8:
        {
            auto s = held;
            std::sort (s.begin(), s.end());
            const auto m = (int) s.size();
            const auto anchor = s[(std::size_t) ((m + cmod (start, m)) % m)];
            for (int o = 0; o < R; ++o)
                if (anchor + 12 * o < 128)
                    out.push_back (anchor + 12 * o);
            return out;
        }
        case 9: case 10:
            if (n == 1)
                return a;
            for (int i = 1; i < n; ++i)
                out.push_back (at (0)), out.push_back (at (i));
            if (mode == 10)
                for (int i = n - 2; i > 1; --i)
                    out.push_back (at (0)), out.push_back (at (i));
            return out;
        case 11: case 12:
            if (n == 1)
                return a;
            for (int i = n - 2; i >= 0; --i)
                out.push_back (at (n - 1)), out.push_back (at (i));
            if (mode == 12)
                for (int i = 1; i < n - 2; ++i)
                    out.push_back (at (n - 1)), out.push_back (at (i));
            return out;
        case 13: case 14: case 15:
        {
            std::vector<int> upJ, downJ;
            for (int i = 0; i < n; i += 2)
                upJ.push_back (at (i));
            for (int i = 1; i < n; i += 2)
                upJ.push_back (at (i));
            for (int i = n - 1; i >= 0; i -= 2)
                downJ.push_back (at (i));
            for (int i = n - 2; i >= 0; i -= 2)
                downJ.push_back (at (i));
            if (mode == 13)
                return upJ;
            if (mode == 14)
                return downJ;
            upJ.insert (upJ.end(), downJ.begin(), downJ.end());
            return upJ;
        }
        case 16:
        {
            auto s = held;
            std::sort (s.begin(), s.end());
            const auto m = (int) s.size();
            if (m == 1)
                return { s[0] };
            const auto D = m < 3 ? 1 : (m - 2) * 3;
            const auto iters = m == 2 ? R / std::gcd (R, 2) : (m - 2) * R;
            int c = 0, j = m > 2 ? 2 : 1;
            const auto octave = [&] (int idx) {
                const auto v = s[(std::size_t) idx] + 12 * ((c / D) % R);
                ++c;
                if (v < 128)
                    out.push_back (v);
            };
            for (int it = 0; it < iters; ++it)
            {
                out.push_back (s[0]);
                octave (1);
                if (j < 2)
                    out.push_back (s[0]);
                else
                {
                    const auto i = std::clamp (j - 1, 1, m - 1);
                    octave (i);
                }
                octave (std::clamp (j, 1, m - 1));
                j = m < 3 ? 1 : (j < m - 1 ? j + 1 : 2);
            }
            return out;
        }
        case 17: return cascade (a, true, true);
        case 18: return cascade (a, false, true);
        case 19:
        {
            out = n > 1 ? cascade (a, true, true) : a;
            if (n > 1)
            {
                const int st[] = { -1, -1, 1 };
                int k = 0, u = n - 2;
                while (u >= 0 && u < n)
                {
                    k = (k + 1) % 3;
                    out.push_back (at (u));
                    u += st[k];
                }
            }
            return out;
        }
        default: return a; // the random modes draw from the sorted set
    }
}

struct ArpSettings
{
    double base = 0.5, ms = 250.0;
    bool sync = true, latch = false, enabled = true;
    double swing = 0.0, swingRate = 0.5, velocity = 0.0, gate = 0.5, mul = 1.0;
    int mode = 0, range = 0, octave = 0, start = 0, rhythm = 0, rotate = 0, repeats = 0, retrig = 0;

    double step (double bpm) const noexcept { return (sync ? base : std::max (ms * 0.001 * bpm / 60.0, 1.0e-6)) * mul; }
};

ArpSettings arpSettings (std::span<const float> raw, int arp, int beatsPerBar)
{
    std::vector<float> p (raw.begin(), raw.end());
    if (p.size() < 32) // a 31-value file: the per-arp rate is at 16 / 27, in 11 old steps
    {
        p.resize (33, 0.0f);
        p[31] = (float) oldRate[steps (p[16], 11)] / 14.0f;
        p[32] = (float) oldRate[steps (p[27], 11)] / 14.0f;
    }
    p.resize (33, 0.0f);
    const auto g = [&p] (int k) { return p[(std::size_t) k]; };
    const int map[] = { arp == 0 ? 9 : 20, arp == 0 ? 10 : 21, arp == 0 ? 11 : 22, arp == 0 ? 12 : 23, arp == 0 ? 13 : 24, arp == 0 ? 14 : 25,
                        arp == 0 ? 15 : 26, arp == 0 ? 31 : 32, arp == 0 ? 17 : 28, arp == 0 ? 18 : 29, arp == 0 ? 19 : 30 };
    ArpSettings a;
    const auto rate = rateBeats[steps (g (1), 8)];
    a.base = rate < 0.0 ? (double) beatsPerBar : rate;
    a.sync = g (3) > 0.5f;
    a.ms = 50.0 + 950.0 * (double) g (2);
    a.swing = std::clamp ((double) g (4), 0.0, 1.0);
    a.swingRate = steps (g (5), 2) == 0 ? 0.5 : 0.25;
    a.latch = g (6) > 0.5f;
    a.velocity = std::clamp (2.0 * (double) g (7 + arp) - 1.0, -1.0, 1.0);
    a.gate = std::clamp ((double) g (map[0]), 0.0, 1.0) * 0.9 + 0.1;
    a.mode = steps (g (map[1]), 24);
    a.range = steps (g (map[2]), 4);
    a.octave = steps (g (map[3]), 7) - 3;
    a.start = steps (g (map[4]), 17) - 8;
    a.rhythm = steps (g (map[5]), 22);
    const auto length = a.rhythm > 0 ? (int) std::strlen (rhythms[a.rhythm - 1]) : 0;
    a.rotate = length > 1 ? roundAway ((double) (length - 1) * std::clamp (2.0 * (double) g (map[6]) - 1.0, -1.0, 1.0)) : 0;
    a.mul = rateMul[steps (g (map[7]), 15)];
    a.repeats = steps (g (map[8]), 17);
    a.retrig = steps (g (map[9]), 12);
    a.enabled = g (map[10]) > 0.5f;
    return a;
}

std::vector<TimedNote> runArp (const ArpSettings& a, const std::vector<TimedNote>& in, double bpm, int beatsPerBar, std::mt19937& rng)
{
    std::vector<TimedNote> out;
    if (in.empty())
        return out;
    const auto st = a.step (bpm);
    struct Ev { double t; bool on; int key; float velocity, pan; };
    std::vector<Ev> evs;
    for (const auto& n : in)
    {
        evs.push_back ({ n.end, false, n.key, n.velocity, n.pan });
        evs.push_back ({ n.start, true, n.key, n.velocity, n.pan });
    }
    // (note-offs before note-ons at the same time: FLM's order there is not fixed)
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& x, const Ev& y) { return x.t < y.t || (x.t == y.t && ! x.on && y.on); });
    std::vector<int> held, seq, bag;
    std::map<int, int> source;
    std::map<int, float> vel, pan;
    int idx = 0, cycles = 0, lastPlayed = -1, walkPos = -1, walkDir = 1;
    bool done = false, haveNext = false;
    double next = 0.0, anchor = 0.0, retrigAt = 0.0;
    const auto& rt = retrigs[a.retrig];
    const auto period = a.retrig >= 2 ? std::max (rt.beats + rt.bars * (double) beatsPerBar, st) : 0.0;
    const auto oneSample = bpm / 60.0 / 44100.0;
    const auto rhythmLength = a.rhythm > 0 ? (int) std::strlen (rhythms[a.rhythm - 1]) : 0;
    double end = 0.0;
    for (const auto& e : evs)
        end = std::max (end, e.t);
    end += 8.0;
    const auto cutFrom = [&out] (double t) {
        for (auto& o : out)
            if (o.end > t && o.start < t)
                o.end = t;
    };
    const auto pick = [&] () -> int {
        const auto n = (int) seq.size();
        if (n < 2)
            return seq.empty() ? -1 : seq[0];
        switch (a.mode)
        {
            case 20: return seq[(std::size_t) std::uniform_int_distribution<int> (0, n - 1) (rng)];
            case 21:
                if (bag.empty() || (int) bag.size() > n)
                {
                    bag.resize ((std::size_t) n);
                    std::iota (bag.begin(), bag.end(), 0);
                    std::shuffle (bag.begin(), bag.end(), rng);
                    if (seq[(std::size_t) bag.back()] == lastPlayed)
                        for (std::size_t i = 0; i + 1 < bag.size(); ++i)
                            if (seq[(std::size_t) bag[i]] != lastPlayed)
                            {
                                std::swap (bag[i], bag.back());
                                break;
                            }
                }
                {
                    const auto i = bag.back();
                    bag.pop_back();
                    return seq[(std::size_t) i];
                }
            case 22: return seq[(std::size_t) (idx % n)];
            default:
            {
                if (walkPos < 0 || walkPos >= n)
                {
                    walkPos = std::uniform_int_distribution<int> (0, n - 1) (rng);
                    walkDir = std::uniform_int_distribution<int> (0, 1) (rng) != 0 ? 1 : -1;
                    return seq[(std::size_t) walkPos];
                }
                int cand[3], weight[3], count = 0;
                for (const auto& [d, w] : { std::pair<int, int> { walkDir, 50 }, { -walkDir, 30 }, { 2 * walkDir, 20 } })
                    if (walkPos + d >= 0 && walkPos + d < n)
                        cand[count] = walkPos + d, weight[count++] = w;
                if (count == 0)
                    return seq[(std::size_t) walkPos];
                int total = 0;
                for (int i = 0; i < count; ++i)
                    total += weight[i];
                auto r = std::uniform_int_distribution<int> (0, total - 1) (rng), chosen = cand[0];
                for (int i = 0; i < count; ++i)
                {
                    if (r < weight[i])
                    {
                        chosen = cand[i];
                        break;
                    }
                    r -= weight[i];
                }
                walkDir = chosen >= walkPos ? 1 : -1;
                walkPos = chosen;
                return seq[(std::size_t) walkPos];
            }
        }
    };
    const auto rebuild = [&] {
        seq = buildSequence (a.mode, held, a.range, a.start, source);
        if (a.mode == 22)
            std::shuffle (seq.begin(), seq.end(), rng);
        bag.clear();
    };
    const auto fireUntil = [&] (double until) {
        while (true)
        {
            if (period > 0.0 && ! held.empty() && retrigAt < until && (! haveNext || retrigAt <= next))
            {
                cutFrom (retrigAt);
                idx = 0, cycles = 0, done = false, next = retrigAt, anchor = retrigAt, haveNext = true;
                retrigAt += period;
                continue;
            }
            if (period > 0.0 && held.empty() && retrigAt < until)
                retrigAt += period * std::max (1.0, std::ceil ((until - retrigAt) / period));
            if (held.empty() || ! haveNext || next >= until || done)
                return;
            const auto t = next;
            auto play = true;
            if (rhythmLength > 0)
            {
                const auto k = (long long) std::floor ((t - anchor) / st + 1e-9);
                const auto r = (int) (((k % rhythmLength) - a.rotate) % rhythmLength + rhythmLength) % rhythmLength;
                play = rhythms[a.rhythm - 1][r] == '1';
            }
            if (play)
            {
                auto ts = t;
                const auto off = a.swing > 0.0 ? std::min (a.swing * 0.5 * a.swingRate, st * 0.5) : 0.0;
                if (off > 0.0)
                {
                    auto ph = std::fmod (t, 2.0 * a.swingRate);
                    if (ph < 0.0)
                        ph += 2.0 * a.swingRate;
                    if (ph >= a.swingRate)
                        ts = t + off * std::clamp (1.0 - (ph - a.swingRate) / a.swingRate, 0.0, 1.0);
                }
                auto length = a.gate * st;
                if (a.gate >= 0.999999 && oneSample < length)
                    length -= oneSample;
                auto cycle = (int) seq.size();
                const auto emit = [&] (int key, int src) {
                    if (key < 0 || key >= 128 || ! a.enabled)
                        return;
                    out.push_back ({ ts, ts + length, key, (float) std::clamp ((1.0 + a.velocity) * (double) vel[src], 0.0, 1.0), pan[src] });
                };
                if (a.mode == 1)
                {
                    cycle = a.range + 1;
                    auto sorted = held;
                    std::sort (sorted.begin(), sorted.end());
                    for (const auto k : sorted)
                        emit (k + 12 * ((cycle + idx % cycle) % cycle) + 12 * a.octave, k);
                }
                else if (! seq.empty())
                {
                    const auto L = (int) seq.size();
                    const auto key = a.mode >= 20 ? pick() : seq[(std::size_t) (a.mode == 8 ? idx % L : (L + cmod (a.start + idx, L)) % L)];
                    const auto src = source.count (key) != 0 ? source[key] : key;
                    lastPlayed = key;
                    emit (key + 12 * a.octave, vel.count (src) != 0 ? src : held.front());
                }
                ++idx;
                if (idx >= std::max (1, cycle))
                {
                    idx = 0;
                    ++cycles;
                    if (a.repeats > 0 && cycles >= a.repeats)
                        done = true;
                }
            }
            next = t + st;
        }
    };
    for (const auto& e : evs)
    {
        fireUntil (e.t);
        if (e.on)
        {
            const auto wasEmpty = held.empty();
            if (std::find (held.begin(), held.end(), e.key) == held.end())
                held.push_back (e.key);
            vel[e.key] = e.velocity;
            pan[e.key] = e.pan;
            if (wasEmpty)
            {
                next = std::ceil (e.t / st - 1e-12) * st;
                idx = 0, cycles = 0, done = false, anchor = next, haveNext = true;
                walkPos = -1;
                if (period > 0.0)
                    retrigAt = std::ceil (e.t / period - 1e-12) * period;
            }
            else if (rt.notes)
                idx = 0, cycles = 0, done = false, anchor = next;
            rebuild();
        }
        else
        {
            if (! a.latch)
                if (const auto it = std::find (held.begin(), held.end(), e.key); it != held.end())
                    held.erase (it);
            if (held.empty())
            {
                cutFrom (e.t); // (the sounding arp notes end at the release)
                haveNext = false;
            }
            else
            {
                rebuild();
                if (idx >= (int) held.size())
                    idx = std::max (0, (int) held.size() - 1);
            }
        }
    }
    fireUntil (end);
    return out;
}
} // namespace

bool timeEffectActive (std::uint32_t type, std::span<const float> p) noexcept
{
    if (p.empty() || p[0] < 0.5f)
        return false; // (switched off: the notes pass)
    if (type == noteEcho)
        return p.size() > 1 && roundAway (32.0 * (double) p[1]) > 0;
    return type == arpeggiator;
}

std::vector<TimedNote> arpeggiate (std::span<const float> params, const std::vector<TimedNote>& in, double bpm, int beatsPerBar, bool& random)
{
    std::mt19937 rng (0x190DEu);
    std::vector<TimedNote> out;
    random = false;
    for (int arp = 0; arp < 2; ++arp)
    {
        const auto a = arpSettings (params, arp, std::max (1, beatsPerBar));
        if (! a.enabled)
            continue;
        random = random || a.mode >= 20;
        const auto notes = runArp (a, in, bpm, std::max (1, beatsPerBar), rng);
        out.insert (out.end(), notes.begin(), notes.end());
    }
    std::stable_sort (out.begin(), out.end(), [] (const TimedNote& x, const TimedNote& y) { return x.start < y.start; });
    return out;
}

std::vector<TimedNote> echo (std::span<const float> params, const std::vector<TimedNote>& in, double bpm, bool& random)
{
    std::vector<float> p (params.begin(), params.end());
    static constexpr float defaultsEcho[16] = { 1.0f, 0.25f, 0.24253564f, 0.24253564f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f };
    for (auto k = p.size(); k < 16; ++k)
        p.push_back (defaultsEcho[k]);
    const auto g = [&p] (int k) { return (double) std::clamp (p[(std::size_t) k], 0.0f, 1.0f); };
    const auto reps = roundAway (32.0 * g (1));
    const auto sync = g (4) > 0.5;
    const auto x = 0.03125 + 7.96875 * std::pow (g (sync ? 3 : 2), 2.0);
    auto D = x;
    if (sync)
    {
        static constexpr double grid[] = { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 3.0, 4.0, 8.0 };
        D = grid[0];
        for (const auto v : grid)
            if (std::abs (v - x) < std::abs (D - x))
                D = v;
    }
    const auto signedLaw = [] (double v) { return v > 0.5 ? 2.0 * (v - 0.5) : 1.8 * v - 0.9; };
    const auto pitchOffset = roundAway (48.0 * g (5) - 24.0), pitch = roundAway (24.0 * g (8) - 12.0);
    const auto velOffset = signedLaw (g (6)), factor = 1.0 + signedLaw (g (7));
    const auto drift = 0.7 + 0.6 * g (9), lengthDrift = g (10) > 0.5 ? 2.0 * g (10) : 0.5 + g (10);
    const auto rho = g (11), panOffset = 2.0 * g (12) - 1.0, panDrift = g (13) - 0.5;
    const auto pingPong = g (14) > 0.5;
    random = rho > 0.0;
    std::mt19937 rng (0x190DEu);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    // (times in samples at 44.1 kHz, as FLM counts them, then back to beats)
    const auto sr = 44100.0, spb = sr * 60.0 / bpm;
    const auto tick = (long long) (sr * 0.46875 / bpm);
    const auto ds = (long long) (D * spb);
    std::vector<TimedNote> out;
    for (const auto& n : in)
    {
        auto dry = n;
        if (reps <= 0)
        {
            out.push_back (dry);
            continue;
        }
        auto k = std::clamp (n.key + pitchOffset, 0, 127);
        auto v = std::clamp ((double) n.velocity + velOffset, 0.0, 1.0);
        auto pn = std::clamp (panOffset + std::clamp (2.0 * (double) n.pan - 1.0, -1.0, 1.0), -1.0, 1.0);
        const auto l0 = (long long) std::llround ((n.end - n.start) * spb);
        double gap = (double) ds, pos = n.start * spb + (double) ds;
        long long length = l0 <= ds ? l0 : -1;
        if ((n.end - n.start) * spb > (double) ds)
            dry.end = n.start + (double) ds / spb; // (still held at the first echo: it ends there)
        out.push_back (dry);
        for (int r = reps - 1;; --r)
        {
            const auto echoNo = reps - r;
            v = std::clamp (v * factor, 0.0, 1.0);
            k = std::clamp (k + pitch, 0, 127);
            const auto q = pingPong && echoNo % 2 == 0 ? -pn : pn;
            auto nextGap = r > 0 ? std::pow (drift, echoNo + 1) * (double) ds : 0.0;
            if (r > 0 && rho > 0.0)
                nextGap = std::max (0.0, nextGap * (1.0 + rho * (2.0 * u (rng) - 1.0)));
            if (length == -1)
                length = (long long) gap;
            auto ln = (long long) ((double) length * lengthDrift + 0.5);
            ln = std::max (0LL, std::min (ln, (long long) gap));
            if (r > 0)
                ln = std::max (0LL, std::min (ln, std::max (0LL, (long long) nextGap - 1)));
            out.push_back ({ pos / spb, (pos + (double) ln) / spb, k, (float) v, (float) std::clamp ((q + 1.0) * 0.5, 0.0, 1.0) });
            length = ln;
            if (! (r > 0 && v > 0.007874016 && ln >= tick))
                break;
            pn = std::clamp (panDrift + pn, -1.0, 1.0);
            pos += (double) (long long) nextGap;
            gap = nextGap;
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const TimedNote& a, const TimedNote& b) { return a.start < b.start; });
    return out;
}
} // namespace lynx::flmnotefx
