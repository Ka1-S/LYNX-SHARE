// FL Studio's BassDrum (REA_rodada43 §2): a 1:1 port of the round-43 reference model. Float arithmetic follows the
// engine's operation order (including MSVC's reassociated products), doubles where the engine computes in double, the
// engine's own CRT powf/sincosf algorithms where their last bits matter, and long double for FL's x87 pan law.

#include "core/BassDrum.h"
#include "core/HostSlide.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

// the float/double operation order follows the engine's, so fusing a·b + c into one FMA would change the results:
// contraction stays off (the FMA steps of FL's CRT powf use std::fma explicitly).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace
{
std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    if (o + 4 <= d.size())
        std::memcpy (&v, d.data() + o, 4);
    return v;
}

std::uint64_t bitsOf (double d)
{
    std::uint64_t q;
    std::memcpy (&q, &d, 8);
    return q;
}

double fromBits (std::uint64_t q)
{
    double d;
    std::memcpy (&d, &q, 8);
    return d;
}

std::uint32_t bitsOf (float f)
{
    std::uint32_t q;
    std::memcpy (&q, &f, 4);
    return q;
}

// ---- the engine's statically linked CRT math (REA_rodada43 §2.9) ----
struct PowTables
{
    double rcp[258] {}, lg[258] {}, e2[64] {};
    PowTables()
    {
        for (int i = 0; i < 258; ++i)
        {
            rcp[i] = 2.0 / (1.0 + i / 256.0);
            lg[i] = std::log (1.0 + i / 256.0);
        }
        for (int j = 0; j < 64; ++j)
            e2[j] = std::exp2 (j / 64.0);
    }
};

const PowTables& powTables()
{
    static const PowTables t;
    return t;
}

bool expTail (double t, bool fmaPath, float& out)
{
    if (t > 88.72283935546875)
    {
        out = INFINITY;
        return true;
    }
    if (! (t > -103.2789306640625))
        return false;
    const auto& T = powTables();
    const auto k = (int) std::nearbyint (t * 92.33248261689366);
    const auto kd = (double) k;
    double res;
    if (fmaPath)
    {
        const auto r = std::fma (-kd, 0.010830424696249145, t);
        auto p = std::fma (std::fma (0.16666666666666666, r, 0.5), r, 1.0);
        p = r * p;
        res = std::fma (p, T.e2[k & 63], T.e2[k & 63]);
    }
    else
    {
        const auto r = t - kd * 0.010830424696249145;
        const auto p = (r * r) * (r * 0.16666666666666666 + 0.5) + r;
        res = p * T.e2[k & 63] + T.e2[k & 63];
    }
    res = fromBits (bitsOf (res) + (std::uint64_t) ((std::int64_t) (k >> 6) << 52));
    out = (float) res;
    return true;
}

// a 32-bit sine of the CRT's vector routine (SSE path), used by sincosf for |x| ≥ 2⁻¹³
float vsinf (float x)
{
    auto u = [] (std::uint32_t b) { float f; std::memcpy (&f, &b, 4); return f; };
    const float invPi = u (0x3ea2f983), magic = u (0x4b400000), p1 = u (0x40490000), p2 = u (0x3a7da000), p3 = u (0x34222000),
                p4 = u (0x2cb4611a), c9 = u (0x362edef8), c7 = u (0xb94fb7ff), c5 = u (0x3c088766), c3 = u (0xbe2aaaa6);
    const float ax = std::fabs (x);
    const bool neg = std::signbit (x);
    const float k = ax * invPi + magic;
    const auto kb = bitsOf (k);
    const float n = k - magic;
    float r = ax - p1 * n;
    r = r - p2 * n;
    r = r - p3 * n;
    r = r - p4 * n;
    const float r2 = r * r;
    if ((kb & 1) != 0)
        r = -r;
    float p = c9 * r2 + c7;
    p = p * r2 + c5;
    p = p * r2 + c3;
    const float t = (r2 * p) * r;
    const float y = r + t;
    return neg ? -y : y;
}

void sincosf32 (float x, float& s, float& c)
{
    const float ax = std::fabs (x);
    if (ax < 0.000122070312f)
    {
        s = x;
        c = 1.0f - (ax * ax) * 0.5f;
        return;
    }
    s = vsinf (x);
    c = vsinf (ax + 1.57079637f);
}

float powfm (float x, float y) { return bassdrum::powfMsvc (x, y); }

// ---- data read at run time from the user's installation ----
struct Wave
{
    std::vector<float> s; // channel 0, followed by 4 zeros of padding (the engine's buffers are padded)
    int length = 0;
};

std::shared_ptr<const Wave> readWave (const std::string& path)
{
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const Wave>> cache;
    const std::lock_guard<std::mutex> lock (mutex);
    if (const auto it = cache.find (path); it != cache.end())
        return it->second;
    std::shared_ptr<const Wave> result;
    std::ifstream in (path, std::ios::binary);
    if (in)
    {
        const std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        auto u32 = [&] (std::size_t o) { std::uint32_t v; std::memcpy (&v, b.data() + o, 4); return v; };
        int tag = 0, ch = 0, bits = 0;
        const std::uint8_t* data = nullptr;
        std::size_t size = 0;
        for (std::size_t i = 12; b.size() >= 8 && i + 8 <= b.size();)
        {
            const auto n = (std::size_t) u32 (i + 4);
            if (std::memcmp (b.data() + i, "fmt ", 4) == 0 && i + 24 <= b.size())
            {
                tag = b[i + 8] | b[i + 9] << 8;
                ch = b[i + 10] | b[i + 11] << 8;
                bits = b[i + 22] | b[i + 23] << 8;
            }
            if (std::memcmp (b.data() + i, "data", 4) == 0)
                data = b.data() + i + 8, size = std::min (n, b.size() - (i + 8));
            i += 8 + n + (n & 1);
        }
        if (data != nullptr && ch >= 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32))
        {
            const auto frame = (std::size_t) (ch * bits / 8);
            const auto n = size / frame;
            auto w = std::make_shared<Wave>();
            w->length = (int) n;
            w->s.assign (n + 4, 0.0f);
            for (std::size_t k = 0; k < n; ++k)
            {
                const auto* p = data + k * frame;
                float v = 0.0f;
                if (tag == 3 && bits == 32)
                    std::memcpy (&v, p, 4);
                else if (bits == 16)
                    v = (float) (std::int16_t) (p[0] | p[1] << 8) * 3.05175781e-05f;
                else if (bits == 8)
                {
                    const float x = (float) p[0] * 0.00392156886f;
                    v = ((x - 0.5f) + x) - 0.5f;
                }
                else if (bits == 24)
                    v = (float) (std::int32_t) ((std::uint32_t) (p[0] << 8 | p[1] << 16 | p[2] << 24) >> 8 | ((p[2] & 0x80) != 0 ? 0xff000000u : 0u))
                        * 1.1920929e-07f; // 24-bit scale: not confirmed (no factory sample uses it)
                else
                    v = (float) (std::int32_t) u32 ((std::size_t) (p - b.data())) * 4.65661287e-10f; // 32-bit integer: not confirmed
                w->s[k] = v;
            }
            result = w;
        }
    }
    cache[path] = result;
    return result;
}

std::string joinPath (std::string dir, const std::string& name)
{
    if (! dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += (dir.find ('\\') != std::string::npos && dir.find ('/') == std::string::npos) ? '\\' : '/';
    return dir + name;
}

std::shared_ptr<const Wave> readWaveAnySeparator (const std::string& path)
{
    if (auto w = readWave (path))
        return w;
    auto p = path;
    std::replace (p.begin(), p.end(), '\\', '/');
    return p == path ? nullptr : readWave (p);
}

// ---- state ----
struct Patch
{
    float P[23] {};
    std::string noise;
};

bool parse (const std::vector<std::uint8_t>& state, Patch& p)
{
    if (state.size() < 0x50C + 23 * 4 || std::memcmp (state.data(), "SSLF", 4) != 0)
        return false;
    const auto tag = i32At (state, 8);
    if (tag != 0x4b696b31 && tag != 0x4b696b32) // "1kiK", "2kiK"
        return false;
    std::memcpy (p.P, state.data() + 0x50C, sizeof (p.P));
    std::string path;
    for (std::size_t i = 12 + 256; i < 12 + 256 + 1024 && state[i] != 0; ++i)
        path.push_back ((char) state[i]);
    const auto cut = path.find_last_of ("/\\");
    p.noise = cut == std::string::npos ? path : path.substr (cut + 1);
    return true;
}

// ---- engine ----
struct Resampler // FUN_180025140 & co: 2048-sample buffer, linear read, double position
{
    float buf[2048 + 8] {};
    int filled = 0, n = 0;
    double pos = 0.0, ratio = 0.0;

    void reset()
    {
        std::fill (std::begin (buf), std::end (buf), 0.0f);
        filled = 0, pos = 0.0, ratio = 0.0, n = 0;
    }
    int prepare (double r, int count, int& start)
    {
        ratio = r, n = count, start = filled;
        const int need = (int) ((double) count * r + pos) - start + 2;
        filled = start + need;
        return need;
    }
    void produce (float* out)
    {
        for (int k = 0; k < n; ++k)
        {
            const int i = (int) pos;
            const auto fr = (float) (pos - (double) i);
            out[k] = i < 0 || i >= 2048 ? 0.0f : (1.0f - fr) * buf[i] + fr * buf[i + 1];
            pos = pos + ratio;
        }
        const int e = (int) pos, rem = filled - e;
        if (rem > 0)
            std::memmove (buf, buf + e, sizeof (float) * (std::size_t) rem);
        filled = rem;
        pos = pos - (double) e;
    }
};

struct HighPass // FUN_18001d4e0 / FUN_18001d5e0
{
    float f = 0.0f, q = 0.0f, fs = 0.0f, b0 = 0.0f, a1 = 0.0f, a2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    static float flush (float v) { return v < 9.99999972e-10f && -9.99999972e-10f < v ? 0.0f : v; }
    void reset() { x1 = x2 = y1 = y2 = 0.0f; }
    void setup (float nf, float nq, float nfs)
    {
        if (nf == f && nq == q && nfs == fs)
            return;
        f = nf, q = nq, fs = nfs;
        const auto w = (float) ((double) f * 6.2831853071795862 / (double) fs);
        float sn, cs;
        sincosf32 (w, sn, cs);
        const float nb0 = (cs + 1.0f) / ((sn / q) + 2.0f);
        const float g = 1.0f / (sn + q);
        const float na1 = (cs * g) * (q * -2.0f);
        const float na2 = (q - sn) * g;
        b0 = flush (nb0), a1 = flush (na1), a2 = flush (na2);
    }
    float operator() (float x)
    {
        x = flush (x);
        const float y = flush (((((x - (x1 + x1)) + x2) * b0) - y1 * a1) - a2 * y2);
        x2 = x1, y2 = y1, x1 = x, y1 = y;
        return y;
    }
};

float tableRead (const Wave& T, float p) // FUN_180028c90
{
    const int N = T.length;
    float fr = p - (float) (int) p;
    if (fr < 0.0f)
        fr = fr + 1.0f;
    const float x = (float) N * fr;
    const int i = std::max ((int) x, 0);
    if (i >= N)
    {
        const float r = x - (float) (N - 1);
        return r * T.s[0] + (1.0f - r) * T.s[(std::size_t) (N - 1)];
    }
    const float r = x - (float) i;
    const float nx = i == N - 1 ? T.s[0] : T.s[(std::size_t) i + 1];
    return r * nx + (1.0f - r) * T.s[(std::size_t) i];
}

struct Voice
{
    int note = -1;
    long order = 0;
    bool released = false, active = false;
    float vel = 1.0f, ratio = 1.0f, fs = 44100.0f;
    Resampler res;
    HighPass hp;
    // note-start fields
    float drive = 0, lvl = 0, d0 = 0, d4 = 1, d8 = 0, dc = 0, e0 = 1, a4 = 0, a8 = 1, ac = 0, b0 = 0, b4 = 0, b8 = 0, bc = 1, c0 = 0, c4 = 0,
          c8 = 0, e8 = 1, ec = 0, f4 = 0, lp = 0;
    int cc = 0, silent = 10000000;
    double npos = 0.0, n148 = 1.0;
    float n150 = 0, n154 = 0, n158 = 0, n15c = 0, n160 = 0, n164 = 0, n168 = 0, n16c = 0, n180 = 0;
    float n170[4] {};

    Voice()
    {
        hp.setup (100.0f, 1.0f, 44100.0f);
        reset();
    }
    void reset() // FUN_180028f80 + the voice's "note = −1"
    {
        res.reset();
        lp = 0.0f, d8 = 0.0f, d0 = 0.0f;
        std::fill (std::begin (n170), std::end (n170), 0.0f);
        active = false;
        hp.reset();
        silent = 10000000;
        note = -1;
    }
};

class Engine
{
public:
    static constexpr int voiceCount = 99, chunk = 512;

    bool init (const Patch& p, std::shared_ptr<const Wave> table, std::shared_ptr<const Wave> noiseSample, double fs)
    {
        if (table == nullptr || table->length < 2)
            return false;
        patch = p, T = std::move (table), noise = std::move (noiseSample), rate = fs;
        for (auto& v : voices)
            v.fs = (float) fs;
        return true;
    }

    float param (int i) const { return std::clamp (patch.P[i], 0.0f, 1.0f); }

    void noteOn (int note, int vel7)
    {
        vel[note] = (float) vel7 * 0.00787401572f;
        Voice* v = nullptr;
        Voice* old = nullptr;
        Voice* oldRel = nullptr;
        for (auto& w : voices)
        {
            if (w.note < 0)
            {
                v = &w;
                break;
            }
            if (w.released)
            {
                if (oldRel == nullptr || w.order < oldRel->order)
                    oldRel = &w;
            }
            else if (old == nullptr || w.order < old->order)
                old = &w;
        }
        if (v == nullptr)
            v = oldRel != nullptr ? oldRel : old;
        v->order = counter++;
        v->note = note;
        v->released = false;
        v->vel = powfm (vel[note], 1.0f);
        start (*v);
    }

    void noteOff (int note)
    {
        vel[note] = 0.0f;
        for (auto& w : voices)
            if (w.note == note)
                w.released = true;
    }

    bool anyActive() const
    {
        return std::any_of (std::begin (voices), std::end (voices), [] (const Voice& v) { return v.active; });
    }
    bool noteSounds (int note) const
    {
        return std::any_of (std::begin (voices), std::end (voices), [note] (const Voice& v) { return v.active && v.note == note; });
    }

    // one wrapper chunk (n ≤ chunk): adds into l/r
    void process (int n, const float* L, const float* R, const float* Tp, float* l, float* r)
    {
        const float sr = param (1);
        float buf[chunk];
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            const auto g = (float) std::pow (2.0, (double) ((sr - 0.5f) * 4.0f));
            const int note = v.note;
            v.ratio = powfm (2.0f, (note >= 0 ? Tp[note] : 0.0f) * 0.0833333358f) * g;
            std::fill (buf, buf + n, 0.0f);
            renderVoice (v, buf, n);
            const float gl = note >= 0 ? L[note] : 1.0f, gr = note >= 0 ? R[note] : 1.0f;
            // FUN_18002b830 mixes R = x·gR + L_old (the left sum so far, not the right one) and L = x·gL + L_old: with
            // several voices FL's right channel carries the earlier voices' left contributions (REA_rodada43 §2.7)
            for (int i = 0; i < n; ++i)
            {
                const float lOld = l[i];
                r[i] = buf[i] * gr + lOld;
                l[i] = buf[i] * gl + lOld;
            }
        }
    }

    float vel[128] {};

private:
    void start (Voice& v) // FUN_180028ff0
    {
        const auto P = [this] (int i) { return param (i); };
        const int N = T->length;
        v.active = true;
        v.silent = 0;
        v.d8 = 1.0f, v.d0 = 1.0f, v.d4 = 1.0f;
        const float inv = 1024.0f / (float) N;
        const float x80 = P (4);
        v.drive = ((x80 * x80) * (x80 * x80)) * 2560.0f;
        v.lvl = powfm (10.0f, ((P (0) - 0.5f) + P (0)) - 0.5f) * v.vel;
        const float dur = (P (2) * P (2)) * P (2);
        if (dur > 0.0f)
            v.d4 = (float) (1.0 / ((double) (dur * 44100.0f) + 44.1));
        const float base = powfm (2.0f, (P (6) + 1.0f) * 4.0f);
        const float peak = (P (7) * P (7)) * (P (7) * 100.0f);
        const auto slide = (float) ((double) ((P (8) * P (8)) * (P (8) * 20.0f)) + 0.001);
        const float xm8 = inv * peak;
        v.dc = 0.0f, v.a8 = 1.0f, v.b0 = xm8, v.a4 = 0.25f, v.bc = 1.0f;
        float t = P (5) * P (5);
        t = t * t;
        t = t * t;
        v.e0 = 1.0f / (t * 100000.0f + 1.0f);
        const float k84 = 2.26757365e-05f;
        v.ac = (base * inv) * k84;
        v.b4 = (float) (0.0002 / (double) slide);
        const float inv2 = inv * k84;
        const float vb = (P (9) + P (9)) - 1.0f, vp = (P (10) + P (10)) - 1.0f, vs = (P (11) + P (11)) - 1.0f;
        v.c0 = (powfm (2.0f, vb) * base) * inv2;
        v.c4 = powfm (2.0f, vp) * xm8;
        v.b8 = 0.25f;
        v.c8 = (float) (0.0002 / (double) (powfm (2.0f, vs) * slide));
        v.cc = (int) ((P (12) * P (12)) * 350.0f);
        v.e8 = (powfm (P (13), 0.125f) * 0.25f) + 0.75f;
        {
            const double x = (double) P (15), q = x * x;
            v.f4 = (float) ((q * q) * x) * 25.0f;
        }
        v.ec = powfm (2.0f, P (14) * 14.0f) * k84;
        v.hp.setup (42.0f, ((P (3) * P (3)) * 6.0f) + 0.5f, v.fs);
        v.hp.reset();
        v.npos = 0.0;
        const float x90 = P (19);
        t = ((x90 - 0.5f) + x90) - 0.5f;
        v.n154 = ((t * t) * t) * 4410.0f;
        v.n148 = std::pow (2.0, ((double) P (20) - 0.5) * 4.0);
        t = P (21) * P (21);
        v.n150 = t + t;
        const float x88 = P (17);
        t = (x88 * x88) * x88;
        t = t * t;
        v.n168 = (float) (1.0 / ((double) (t * 44100.0f) + 44.1));
        v.n16c = P (18);
        const float x84 = P (16);
        if (x84 >= 9.99999975e-05f)
        {
            t = (x84 * x84) * x84;
            const float rr = k84 / (t * t);
            v.n158 = 0.0f, v.n164 = rr, v.n160 = rr, v.n15c = 1.0f;
        }
        else
            v.n158 = 0.99f, v.n164 = 1.0f, v.n160 = v.n168, v.n15c = v.n16c;
        v.n180 = P (22) * P (22);
    }

    void generate (Voice& v, float* out, int n) // FUN_180029440: internal 44.1 kHz samples
    {
        const Wave& Tb = *T;
        for (int k = 0; k < n; ++k)
        {
            const float x5 = v.n154;
            float osc = 0.0f;
            if (! (x5 > 0.0f))
            {
                const float cp = v.e8;
                float ck = 0.0f;
                if (cp < 1.0f)
                {
                    ck = tableRead (Tb, cp) * v.f4;
                    v.e8 = cp + v.ec;
                }
                float a8 = v.a8;
                a8 = a8 - a8 * v.b4;
                v.a8 = a8;
                float q = a8 * a8;
                q = q * q;
                float inc = ((q * v.b0) + 1.0f) * v.ac;
                inc = std::min (std::max (inc, 0.0f), 0.5f);
                float ph = inc + v.a4;
                v.a4 = ph;
                if (ph > 1.0f)
                    ph = ph - 1.0f, v.a4 = ph;
                osc = tableRead (Tb, ph + ck);
                if (v.cc > 0)
                    --v.cc;
                else
                {
                    float bc = v.bc;
                    bc = bc - bc * v.c8;
                    v.bc = bc;
                    q = bc * bc;
                    q = q * q;
                    inc = ((q * v.c4) + 1.0f) * v.c0;
                    inc = std::min (std::max (inc, 0.0f), 0.5f);
                    ph = inc + v.b8;
                    v.b8 = ph;
                    if (ph > 1.0f)
                        ph = ph - 1.0f, v.b8 = ph;
                    osc = osc + tableRead (Tb, ph + ck);
                }
            }
            if (x5 >= 0.0f)
            {
                if (noise != nullptr)
                {
                    const double pos = v.npos;
                    const auto i = (long long) std::floor (pos);
                    const auto fr = (float) (pos - (double) (int) pos);
                    float sm = 0.0f;
                    if (i >= 0 && i < noise->length)
                        sm = (1.0f - fr) * noise->s[(std::size_t) i] + fr * noise->s[(std::size_t) i + 1];
                    const float a = v.n180;
                    float x = ((sm - v.n170[0]) * a) + v.n170[0];
                    v.n170[0] = x;
                    x = ((x - v.n170[1]) * a) + v.n170[1];
                    v.n170[1] = x;
                    x = ((x - v.n170[2]) * a) + v.n170[2];
                    v.n170[2] = x;
                    x = ((x - v.n170[3]) * a) + v.n170[3];
                    v.n170[3] = x;
                    v.npos = pos + v.n148;
                    float e = ((v.n15c - v.n158) * v.n160) + v.n158;
                    v.n158 = e;
                    if (e > 0.99f)
                    {
                        v.n15c = v.n16c, v.n160 = v.n168, v.n158 = 0.99f;
                        e = 0.99f;
                    }
                    x = x * v.n150;
                    float e2 = e * e;
                    e2 = e2 * e2;
                    e2 = e2 * e2;
                    e2 = e2 * e2;
                    osc = osc + x * e2;
                }
                if (v.n154 > 0.0f)
                {
                    const float t = v.n154 - 1.0f;
                    v.n154 = t;
                    if (! (t > 0.0f))
                        v.n154 = 0.0f;
                }
            }
            else
            {
                const float t = x5 + 1.0f;
                v.n154 = t;
                if (t >= 0.0f)
                    v.n154 = 0.0f;
            }
            float g = 0.0f;
            if (v.d4 < 1.0f && v.d0 > 0.0f)
            {
                const float d = v.d0, nd = d - v.d4;
                g = d * 7.0f;
                g = g / (g + 1.0f);
                v.d0 = nd;
            }
            else
                v.d0 = 0.0f;
            const float d8 = v.d8;
            g = ((g * 0.005f) - (d8 * 0.005f)) + d8;
            v.d8 = g;
            const float dc = v.dc;
            v.dc = ((1.0f - dc) * v.e0) + dc;
            out[k] = (dc * dc) * (g * osc);
        }
    }

    void renderVoice (Voice& v, float* out, int n) // FUN_180029910
    {
        if (! v.active)
            return;
        const double ratio = std::min (std::max ((double) v.ratio, 0.25), 4.0) * (44100.0 / (double) v.fs);
        int done = 0, rem = n;
        while (rem > 0)
        {
            const int m = std::min (rem, 512);
            rem -= m;
            int st = 0;
            const int need = v.res.prepare (ratio, m, st);
            if (need > 0)
                generate (v, v.res.buf + st, need);
            v.res.produce (out + done);
            done += m;
        }
        const float d = v.drive, h = 0.5f;
        const float n1 = h / ((d * h) + 1.0f);
        const float n2 = n1 / (((n1 * n1) * d) + 1.0f);
        const float n3 = n2 / ((std::fabs ((n2 * n2) * n2) * d) + 1.0f);
        const float n4 = n3 / ((((d * n3) * n3) * (n3 * n3)) + 1.0f);
        const float norm = 1.0f / n4;
        for (int k = 0; k < n; ++k)
        {
            const float x = out[k];
            const float sl = ((x - v.lp) * 0.7f) + v.lp;
            v.lp = sl;
            float hp = x - sl;
            hp = hp / ((std::fabs (hp) * d) + 1.0f);
            hp = hp / (((hp * hp) * d) + 1.0f);
            hp = hp / ((std::fabs ((hp * hp) * hp) * d) + 1.0f);
            hp = hp / ((((d * hp) * hp) * (hp * hp)) + 1.0f);
            const float y = (hp * norm) + sl;
            const float o = (v.hp (y) * v.lvl) * 0.4f;
            out[k] = o;
            if (std::fabs (o) >= 9.99999975e-06f)
                v.silent = 0;
            else
                ++v.silent;
        }
        if (v.silent > (int) (v.fs * 0.0232199542f))
            v.reset();
    }

    Patch patch;
    std::shared_ptr<const Wave> T, noise;
    double rate = 44100.0;
    Voice voices[voiceCount];
    long counter = 0;
};

// FL's host ComputeLRVol (x87 law A)
void lawA (double pan, double vol, float& l, float& r)
{
    const auto p = (float) std::clamp (pan, -1.0, 1.0);
    const long double t = (1.0L + (long double) p) * (long double) 0.78539816339744828;
    l = (float) (std::cos (t) * (long double) (float) vol);
    r = (float) (std::sin (t) * (long double) (float) vol);
}

int velFromInitVol (double iv) // CFLWrapper TriggerVoice (REA_rodada28 §4.2)
{
    const auto x = (float) iv * 10.0f + 1.0f;
    const auto v = ((float) std::log ((double) x) * 0.3284587264060974f) * 127.0f;
    return std::clamp ((int) (double) v, 0, 127);
}

// The FL wrapper (CFLWrapper): held notes, per-note tables, chunked Gen_Render, host-voice kill
class Plugin
{
public:
    struct Held
    {
        bool on = false;
        double fvol = 1.0, fpan = 0.0, pitchCents = 0.0;
        bool down = true; // its note off has not come yet
        // a slide note / the live PITCH knob moving Final.Pitch (round 167a): BassDrum reads the host's per-note pitch
        // table every chunk (REA_rodada43 §2, Tp in process), so a sounding hit follows it
        HostSlide slide;
    };

    Engine eng;
    Held held[128];

    void noteOn (int key, double initVol, double fvol, double fpan, double pitchCents)
    {
        key = std::clamp (key, 0, 127);
        if (held[key].on)
            eng.noteOff (key);
        held[key] = { true, fvol, fpan, pitchCents };
        eng.noteOn (key, velFromInitVol (initVol));
    }
    void noteOff (int key)
    {
        if (key >= 0 && key < 128)
        {
            held[key].down = false;
            eng.noteOff (key);
        }
    }
    // a slide note (round 167a): the held keys glide to `toCents` (Final.Pitch, cents from C5) over `samples`
    void slideTo (double toCents, float samples) noexcept
    {
        for (int i = 0; i < 128; ++i)
            if (held[i].on && held[i].down)
                held[i].slide.start ((float) (toCents - (100.0 * i - 6000.0 + held[i].pitchCents)), samples);
    }
    void followPitch (float now) noexcept
    {
        for (auto& h : held)
            if (h.on)
                h.slide.follow (now);
    }
    void setLevels (double fvol, double panOffset, const float* notePan)
    {
        for (int k = 0; k < 128; ++k)
            if (held[k].on)
                held[k].fvol = fvol, held[k].fpan = std::clamp ((double) notePan[k] + panOffset, -1.0, 1.0);
    }
    bool idle() const
    {
        return ! eng.anyActive() && std::none_of (std::begin (held), std::end (held), [] (const Held& h) { return h.on; });
    }

    // one Gen_Render: adds n samples into l/r
    void render (float* l, float* r, int n)
    {
        float L[128], R[128], Tp[128];
        float cl = 0.0f, cr = 0.0f, cp = 0.0f;
        bool first = true;
        for (auto& h : held)
            if (h.slide.moving())
                h.slide.advance ((float) n);
        for (int i = 0; i < 128; ++i)
        {
            if (held[i].on)
            {
                lawA (held[i].fpan, held[i].fvol, cl, cr);
                const auto fp = held[i].slide.cents != 0.0f ? (float) (100.0 * i - 6000.0 + held[i].pitchCents + (double) held[i].slide.cents)
                                                            : (float) (100.0 * i - 6000.0 + held[i].pitchCents);
                cp = (float) ((double) fp * 0.01 - (double) i + 60.0);
                if (first)
                {
                    for (int j = 0; j < i; ++j)
                        L[j] = cl, R[j] = cr, Tp[j] = cp;
                    first = false;
                }
            }
            L[i] = cl, R[i] = cr, Tp[i] = cp;
        }
        for (int c0 = 0; c0 < n; c0 += Engine::chunk)
        {
            const int m = std::min (Engine::chunk, n - c0);
            float bl[Engine::chunk] {}, br[Engine::chunk] {};
            eng.process (m, L, R, Tp, bl, br);
            for (int i = 0; i < m; ++i)
                l[c0 + i] += bl[i], r[c0 + i] += br[i];
        }
        for (int k = 0; k < 128; ++k)
            if (held[k].on && eng.vel[k] == 0.0f && ! eng.noteSounds (k))
                held[k].on = false;
    }
};

struct Setup
{
    std::unique_ptr<Plugin> plugin;
    double rate = 0.0;
};

class BassDrumInstrument final : public Instrument
{
public:
    ~BassDrumInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete current;
    }

    void prepare (double sr) noexcept override
    {
        delete retired.exchange (nullptr);
        rate.store (sr);
        rebuild();
        take();
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        delete retired.exchange (nullptr);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        if (channel.pluginState == state && channel.samplePath == dir)
            return;
        state = channel.pluginState;
        dir = channel.samplePath;
        rebuild();
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        if (plugin == nullptr)
            return;
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (std::pow (11.0, 1.28 * vel) - 1.0) / 10.0; // FL's Init.Vol (REA_rodada28 §4.1)
        const auto k = std::clamp (key, 0, 127);
        notePan[k] = nextPan;
        const auto knob = pitchCents.load();
        plugin->noteOn (k, initVol, channelVol, std::clamp ((double) nextPan + channelPan, -1.0, 1.0), (double) knob + (double) nextCents);
        plugin->held[k].slide.anchor (live.ref (knob));
        nextPan = 0.0f, nextCents = 0.0f;
    }

    // slide notes and the live PITCH knob / main pitch move the sounding hit (round 167a; a slide doesn't retrigger)
    void slideTo (int key, float cents, float samples) noexcept override
    {
        take();
        if (plugin != nullptr)
            plugin->slideTo (100.0 * (std::clamp (key, 0, 127) - 60) + (double) pitchCents.load() + (double) cents, samples);
    }
    void setChannelPitchNow (float knob, float rest) noexcept override
    {
        live.set (knob, rest);
        if (plugin != nullptr)
            plugin->followPitch (live.now());
    }

    void noteOff (int key) noexcept override
    {
        take();
        if (plugin != nullptr)
            plugin->noteOff (key);
    }

    void allNotesOff() noexcept override
    {
        take();
        if (plugin != nullptr)
            for (int k = 0; k < 128; ++k)
                if (plugin->held[k].on)
                    plugin->noteOff (k);
    }

    bool idle() const noexcept override { return plugin == nullptr || plugin->idle(); }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        if (plugin == nullptr || n <= 0)
            return;
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / 3.141592653589793) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        if (vol != channelVol || pan != channelPan)
        {
            channelVol = vol, channelPan = pan;
            plugin->setLevels (vol, pan, notePan);
        }
        for (int done = 0; done < n; done += 512)
            plugin->render (left + done, right + done, std::min (512, n - done));
    }

private:
    void rebuild() noexcept
    {
        try
        {
            auto s = std::make_unique<Setup>();
            s->rate = rate.load();
            Patch p;
            if (parse (state, p) && ! dir.empty())
            {
                auto table = readWaveAnySeparator (joinPath (dir, "sounddata"));
                std::shared_ptr<const Wave> noise;
                if (! p.noise.empty())
                    noise = readWaveAnySeparator (joinPath (joinPath (dir, "Noise Samples"), p.noise));
                auto pl = std::make_unique<Plugin>();
                if (pl->eng.init (p, table, noise, s->rate))
                    s->plugin = std::move (pl);
            }
            delete incoming.exchange (s.release());
        }
        catch (...)
        {
        }
    }

    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* s = incoming.exchange (nullptr))
        {
            retired.store (current);
            current = s;
            plugin = current->plugin.get();
            if (plugin != nullptr)
                plugin->setLevels (channelVol, channelPan, notePan);
        }
    }

    Setup* current = nullptr;
    Plugin* plugin = nullptr;
    std::atomic<Setup*> incoming { nullptr }, retired { nullptr };
    std::atomic<double> rate { 44100.0 };
    std::atomic<float> pitchCents { 0.0f };
    LiveChannelPitch live; // (audio thread)
    std::vector<std::uint8_t> state;
    std::string dir;
    double channelVol = 1.0, channelPan = 0.0;
    float notePan[128] {};
    float nextPan = 0.0f, nextCents = 0.0f;
};
} // namespace

bool bassDrumStateUsable (const std::vector<std::uint8_t>& state)
{
    Patch p;
    return parse (state, p);
}

std::string bassDrumNoiseName (const std::vector<std::uint8_t>& state)
{
    Patch p;
    return parse (state, p) ? p.noise : std::string();
}

int bassDrumParamOffset (const std::vector<std::uint8_t>& state, int index)
{
    return bassDrumStateUsable (state) && index >= 0 && index < 23 ? 0x50C + 4 * index : -1;
}

std::unique_ptr<Instrument> makeBassDrumInstrument() { return std::make_unique<BassDrumInstrument>(); }

namespace bassdrum
{
float powfMsvc (float x, float y)
{
    const auto xb = bitsOf (x), yb = bitsOf (y);
    if ((yb & 0x7fffffffu) == 0)
        return 1.0f;
    if (yb == 0x3f800000u)
        return x;
    if (! (x > 0.0f) || ! std::isfinite (x) || ! std::isfinite (y))
        return x == 0.0f ? (y > 0.0f ? 0.0f : INFINITY) : (float) std::pow ((double) x, (double) y);
    const auto xd = (double) x, yd = (double) y;
    float out = 0.0f;
    if (xb < 0x3f880000u && std::fabs (xd - 1.0) < 0.0625)
    {
        // near 1: series logarithm, then the SSE exponential
        const double d = xd - 1.0, s = d / (d + 2.0), sd = s * d, u = s + s, u2 = u * u;
        const double a = u2 * 0.012500000003771751 + 0.08333333333333179, b = u2 * 0.0004348877777076146 + 0.0022321399879194482;
        const double u3 = u * u2, u7 = (u2 * u2) * u3;
        const double ser = (u7 * b) + (u3 * a);
        const double lnx = d + (ser - sd);
        return expTail (yd * lnx, false, out) ? out : 0.0f;
    }
    const auto& T = powTables();
    const auto q = bitsOf (xd);
    const auto mant = q & 0xfffffffffffffULL;
    const auto i = (int) ((mant >> 44) + ((mant >> 43) & 1));
    const double F = fromBits (((std::uint64_t) (i | 0x3fe00)) << 44), m = fromBits (mant | 0x3fe0000000000000ULL);
    const auto e = (double) ((int) ((q >> 52) & 0x7ff) - 0x3ff);
    const double r = (F - m) * T.rcp[i];
    double p = std::fma (std::fma (0.3333333333333333, r, 0.5), r, 1.0);
    p = r * p;
    const double lnx = (e * 0.6931471805599453 + T.lg[i]) - p;
    return expTail (yd * lnx, true, out) ? out : 0.0f;
}

std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const std::string& pluginDir, int block, bool noise)
{
    Patch p;
    if (length <= 0 || ! parse (state, p))
        return {};
    auto table = readWaveAnySeparator (joinPath (pluginDir, "sounddata"));
    std::shared_ptr<const Wave> ns;
    if (noise && ! p.noise.empty())
        ns = readWaveAnySeparator (joinPath (joinPath (pluginDir, "Noise Samples"), p.noise));
    auto pl = std::make_unique<Plugin>();
    if (! pl->eng.init (p, table, ns, sampleRate))
        return {};
    std::vector<int> evs;
    for (const auto& nt : notes)
    {
        evs.push_back (nt.start);
        if (nt.release >= 0)
            evs.push_back (nt.release);
    }
    block = std::max (1, block);
    std::vector<float> out (2 * (std::size_t) length, 0.0f), l ((std::size_t) block), r ((std::size_t) block);
    int t = 0;
    while (t < length)
    {
        for (const auto& nt : notes)
        {
            if (nt.start == t)
                pl->noteOn (nt.key, nt.initVol, nt.finalVol, nt.finalPan, nt.pitchCents);
            if (nt.release == t)
                pl->noteOff (std::clamp (nt.key, 0, 127));
        }
        auto m = std::min (block, length - t);
        for (auto e : evs)
            if (t < e && e < t + m)
                m = e - t;
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        pl->render (l.data(), r.data(), m);
        for (int j = 0; j < m; ++j)
        {
            out[2 * (std::size_t) (t + j)] = l[(std::size_t) j];
            out[2 * (std::size_t) (t + j) + 1] = r[(std::size_t) j];
        }
        t += m;
    }
    return out;
}
} // namespace bassdrum
} // namespace lynx
