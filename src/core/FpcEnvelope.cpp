#include "core/FpcEnvelope.h"

// FPC's pad envelopes (round 190z, docs/analise/REA_rodada190z.md §2): FPC_x64.dll's envelope classes (constructor
// FUN_0083c240: time scale 1, capability byte (b & 0xEC) | 0x4C — decay point kept, loop / sustain kept, arpeggio
// bytes cleared; the pan class's offset 0.5 from its reset FUN_0083c3f0), the library's start (FUN_00788c80 +
// FUN_0078b000 / FUN_0078b0e0), step (FUN_00789090), point level (FUN_00788470) and release (FUN_00788ed0), the
// knob factors (FUN_0078af90: value / 128) and FPC's tick (FUN_00873060 / FUN_00872ff0). Clean-room.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "core/DelphiMath.h"

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace lynx
{
namespace
{
using u32 = std::uint32_t;

inline float F (double v) noexcept { return (float) v; }
inline long long rnd (double x) noexcept { return (long long) std::nearbyint (x); } // round half to even
inline long long trunc64 (double x) noexcept { return (long long) x; }
inline bool phaseWrap (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (u32) (b >> 32) > 0x3fefffffu;
}
inline bool hiBelowHalf (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (u32) (b >> 32) < 0x3fe00000u;
}

struct Pt
{
    float dt = 0, y = 0, t = 0;
    std::uint8_t mode = 0;
    int sign = 0;
    float k = 0, ilk = 0, L = 0, p4 = 0, p5 = 0, p6 = 0, w = 0;
};
struct Env
{
    int kind = 1, ver = 3;
    std::vector<Pt> pts;
    int flags = 0, on = 0, decay = -1, loop = -1, sus = -1;
};

#include "core/FlCurves.inc"
} // namespace

struct FpcEnvelope::Curve
{
    lynx::Curve c;
    float kA = 1.0f, kD = 1.0f, kS = 0.0f, kR = 1.0f;

    // FUN_00788470: the point's level, lifted by the sustain shift × its weight (clipped to 0…1 when lifted), less the
    // offset
    double level (int i, float shift, bool clip = true) const noexcept
    {
        if (i < 0 || i >= (int) c.pts.size())
            return 0.0;
        const auto& p = c.pts[(std::size_t) i];
        double v = (double) p.y;
        if (shift != 0.0f)
        {
            v = v + (double) (p.w * shift);
            if (clip)
                v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        }
        return v - c.offset;
    }
};

FpcEnvelope::FpcEnvelope (const fpc::Envelope& stored, bool pan)
{
    if (! stored.unparsed.empty())
        return;
    auto e = std::make_shared<Curve>();
    auto& c = e->c;
    float last = 0.0f;
    for (const auto& p : stored.points)
    {
        Pt q;
        q.dt = (float) p.time;
        q.y = std::isnan (p.value) ? last : (float) p.value; // (the loader keeps the previous level for a NaN)
        q.t = p.tension;
        q.mode = (std::uint8_t) (p.type & 0xFF);
        last = q.y;
        c.pts.push_back (q);
    }
    if (c.pts.empty())
        c.pts.push_back (Pt {});
    const int n = (int) c.pts.size();
    auto index = [n] (int i) { return i >= 0 && i < n ? i : -1; };
    c.offset = pan ? 0.5 : 0.0;
    c.flags = stored.tail[0];
    c.on = stored.tail[1];
    c.decay = index (stored.tail[2]);
    c.loop = index (stored.tail[3]);
    c.sus = index (stored.tail[4]);
    c.prepare();
    e->kA = (float) stored.tail[5] / 128.0f;
    e->kD = (float) stored.tail[6] / 128.0f;
    e->kS = (float) stored.tail[7] / 128.0f;
    e->kR = (float) stored.tail[8] / 128.0f;
    curve = std::move (e);
}

bool FpcEnvelope::enabled() const noexcept { return curve != nullptr && curve->c.on != 0; }

int FpcEnvelope::ticksFor (double rate) noexcept
{
    const auto x = (double) (48 * (int) rate) / 44100.0;
    return std::max (4, (int) rnd (x / 4.0) * 4);
}

double FpcEnvelope::scaleFor (double rate) noexcept
{
    // FUN_00873060: x = 48·rate/44100, tick = max(4, 4·round(x/4)), u = tick·48 / (x·96); FUN_00872ff0: u·96/44100·2
    const auto x = (double) (48 * (int) rate) / 44100.0;
    const auto tick = ticksFor (rate);
    const auto u = (double) (tick * 48) / (x * 96.0);
    return u * 96.0 / 44100.0 * 2.0;
}

void FpcEnvelope::start (State& s, double scale) const noexcept
{
    if (! enabled())
    {
        s.stage = s.sub = 9;
        s.start = s.delta = s.out = 0.0f;
        return;
    }
    const auto& e = *curve;
    const auto& c = e.c;
    s.idx = 0;
    s.shift = 0.0f;
    s.stage = c.decay < 0 ? 3 : 1;
    if (c.pts[0].dt == 0.0f)
        s.rate = 1.0, s.phase = 1.0;
    else
        s.rate = scale / (double) c.pts[0].dt, s.phase = 0.0;
    s.sub = s.stage;
    s.start = F (e.level (0, 0.0f));
    s.delta = 0.0f;
    s.out = s.start;
    s.kA = e.kA, s.kD = e.kD, s.kS = e.kS, s.kR = e.kR;
    // FUN_0078b000: the S lift exists with a sustain point; the loop holds when it has no length or DEC is 0
    s.sus = c.sus < 0 ? 0.0f : s.kS;
    s.hold = c.zeroloop || s.kD <= 0.0f;
    // FUN_0078b0e0: without a decay point the lift applies from the start
    s.shift = s.stage < 2 ? s.shift : s.sus;
    s.out = F (e.level (0, s.shift));
}

void FpcEnvelope::step (State& s, double scale, double ticks) const noexcept
{
    if (curve == nullptr)
        return;
    const auto& e = *curve;
    const auto& c = e.c;
    const int n = (int) c.pts.size();
    if (s.idx < 0 || s.idx > n - 1)
        return;
    s.phase = s.phase + ticks * s.rate;
    while (phaseWrap (s.phase))
    {
        if (s.rate == 0.0)
            s.phase = ticks;
        else
            s.phase = (s.phase - 1.0) / s.rate;
        s.start = F (e.level (s.idx, s.shift));
        bool stop;
        if (s.idx < c.endpt || c.endpt < 0 || s.stage > 5)
        {
            if (c.decay <= s.idx && c.decay >= 0 && s.sub < 3)
            {
                s.stage = std::max (s.stage, 3);
                s.sub = 3;
                s.shift = s.sus;
            }
            if (s.idx < n - 1)
            {
                ++s.idx;
                stop = false;
            }
            else
            {
                s.stage = 9;
                stop = true;
            }
        }
        else if (c.loop < 0)
            stop = true;
        else
        {
            s.sub = 4;
            s.idx = c.loop + 1;
            stop = s.hold;
        }
        s.delta = F (e.level (s.idx, s.shift) - (double) s.start);
        if (! stop)
        {
            // the knob on the segment's time: ATT up to the decay point, DEC after it, REL in the release when there is
            // a sustain point (else DEC)
            const auto released = s.stage >= 6 && c.sus >= 0;
            const float k = released ? s.kR : (s.stage > 2 ? s.kD : s.kA);
            const auto dur = (double) c.pts[(std::size_t) s.idx].dt * (double) k;
            if (dur <= 0.0)
            {
                s.rate = 1.0;
                s.phase = s.phase + 1.0;
            }
            else
            {
                s.rate = scale / dur;
                s.phase = s.phase * s.rate;
            }
        }
        else
            s.phase = s.rate = 0.0;
    }
    if (s.delta == 0.0f)
        s.out = s.start;
    else
        s.out = F ((double) (s.start + c.shape (s.idx, s.phase) * s.delta));
}

void FpcEnvelope::release (State& s) const noexcept
{
    if (curve == nullptr || s.stage >= 6)
        return;
    const auto& e = *curve;
    const auto& c = e.c;
    if (c.sus >= 0)
    {
        // leave from the level reached: the lift becomes what makes the sustain point's level the output now
        s.idx = c.sus;
        const auto at = e.level (s.idx, s.shift, false);
        s.shift = F ((double) s.shift + ((double) s.out - at));
        s.phase = 1.0;
        s.sub = 6;
    }
    s.stage = 6;
    if (s.rate == 0.0)
        s.phase = 1.0;
}

int FpcEnvelope::pointCount() const noexcept { return curve != nullptr ? (int) curve->c.pts.size() : 0; }
double FpcEnvelope::pointTime (int i) const noexcept
{
    return curve != nullptr && i >= 0 && i < pointCount() ? (double) curve->c.pts[(std::size_t) i].dt : 0.0;
}
double FpcEnvelope::pointLevel (int i) const noexcept
{
    return curve != nullptr && i >= 0 && i < pointCount() ? (double) curve->c.pts[(std::size_t) i].y : 0.0;
}
float FpcEnvelope::shape (int i, double x) const noexcept { return curve != nullptr ? curve->c.shape (i, x) : (float) x; }
int FpcEnvelope::decayPoint() const noexcept { return curve != nullptr ? curve->c.decay : -1; }
int FpcEnvelope::loopPoint() const noexcept { return curve != nullptr ? curve->c.loop : -1; }
int FpcEnvelope::sustainPoint() const noexcept { return curve != nullptr ? curve->c.sus : -1; }
float FpcEnvelope::attack() const noexcept { return curve != nullptr ? curve->kA : 1.0f; }
float FpcEnvelope::decay() const noexcept { return curve != nullptr ? curve->kD : 1.0f; }
float FpcEnvelope::sustain() const noexcept { return curve != nullptr ? curve->kS : 0.0f; }
float FpcEnvelope::releaseTime() const noexcept { return curve != nullptr ? curve->kR : 1.0f; }

std::vector<float> FpcEnvelope::trace (double rate, int heldTicks, int maxTicks) const
{
    std::vector<float> out;
    if (! enabled())
        return out;
    const auto scale = scaleFor (rate);
    State s;
    start (s, scale);
    for (int t = 0; t < maxTicks; ++t)
    {
        if (t == heldTicks)
            release (s);
        out.push_back (s.out + (float) curve->c.offset);
        step (s, scale, 1.0);
        if (s.finished() && t >= heldTicks)
        {
            out.push_back (s.out + (float) curve->c.offset);
            break;
        }
    }
    return out;
}
} // namespace lynx
