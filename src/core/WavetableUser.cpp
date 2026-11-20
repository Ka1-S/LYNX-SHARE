// Helix+ 2 (round 190ml): the channel's own wave tables and sample (core/WavetableSynth.h). LYNX's own code and design:
// the tables are kept as spectra (64 frames × their partials) in the channel's pluginState and built into mipmaps by the
// factory tables' builder (core/WavetableTables.cpp), so a drawn, imported or typed table is band-limited the same way.
// Import: pitch-detected cycles (a YIN difference function), fixed-size frames or frames spread over the file; frames
// normalised and phase-aligned on request. Formulas: LYNX's own small expression language, evaluated by a tree walker
// with no access to anything but its numbers (no names but its variables, constants and math functions; length, size and
// depth limits). No other product's code, table or file layout is used.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstring>
#include <functional>
#include <mutex>
#include <numeric>

#include "core/Simd.h"
#include "core/WavetableSynth.h"

namespace lynx::wt
{
namespace
{
constexpr double pi = 3.14159265358979323846;

void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto ang = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// a cycle (any length) at `length` points (cubic, periodic)
std::vector<double> resampleCycle (const float* src, double srcLength, int length, const float* end)
{
    std::vector<double> out ((std::size_t) length);
    const auto at = [&] (std::ptrdiff_t i) -> double {
        const auto* p = src + i;
        return p >= end ? 0.0 : (double) *p;
    };
    for (int i = 0; i < length; ++i)
    {
        const auto pos = (double) i * srcLength / (double) length;
        const auto k = (std::ptrdiff_t) pos;
        const auto t = pos - (double) k;
        const auto xm = at (k > 0 ? k - 1 : 0), x0 = at (k), x1 = at (k + 1), x2 = at (k + 2);
        const auto c1 = 0.5 * (x1 - xm), c2 = xm - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);
        out[(std::size_t) i] = ((c3 * t + c2) * t + c1) * t + x0;
    }
    return out;
}

// one cycle of `length` (a power of two) → amplitude / phase of partials 1 … partials (sin (2π h x + φ)); DC dropped
void spectrumOfCycle (const std::vector<double>& cycle, int partials, float* amp, float* phase)
{
    std::vector<std::complex<double>> a (cycle.begin(), cycle.end());
    fft (a, false);
    const auto n = (double) cycle.size();
    for (int h = 1; h <= partials; ++h)
    {
        if (h >= (int) cycle.size() / 2)
        {
            amp[h - 1] = 0.0f, phase[h - 1] = 0.0f;
            continue;
        }
        const auto c = a[(std::size_t) h] * (2.0 / n);
        amp[h - 1] = (float) std::abs (c);
        phase[h - 1] = (float) (std::arg (c) + pi * 0.5);
    }
}

UserTable emptyTable (const std::string& name, int partials)
{
    UserTable t;
    t.name = name;
    t.partials = partials;
    t.amp.assign ((std::size_t) frameCount * (std::size_t) partials, 0.0f);
    t.phase.assign ((std::size_t) frameCount * (std::size_t) partials, 0.0f);
    return t;
}

// key frames (each a spectrum) spread over the 64 frames, the ones between crossfaded (complex: a straight crossfade of
// the cycles)
UserTable spreadFrames (const std::vector<std::vector<float>>& amps, const std::vector<std::vector<float>>& phases, int partials, const std::string& name)
{
    auto t = emptyTable (name, partials);
    const auto keys = (int) amps.size();
    for (int f = 0; f < frameCount; ++f)
    {
        const auto pos = keys > 1 ? (double) f * (double) (keys - 1) / (double) (frameCount - 1) : 0.0;
        const auto k0 = std::min ((int) pos, std::max (0, keys - 2));
        const auto w = keys > 1 ? pos - (double) k0 : 0.0;
        const auto k1 = std::min (k0 + 1, keys - 1);
        for (int h = 0; h < partials; ++h)
        {
            const auto a = std::polar ((double) amps[(std::size_t) k0][(std::size_t) h], (double) phases[(std::size_t) k0][(std::size_t) h]);
            const auto b = std::polar ((double) amps[(std::size_t) k1][(std::size_t) h], (double) phases[(std::size_t) k1][(std::size_t) h]);
            const auto c = a + (b - a) * w;
            t.amp[(std::size_t) (f * partials + h)] = (float) std::abs (c);
            t.phase[(std::size_t) (f * partials + h)] = (float) std::arg (c);
        }
    }
    return t;
}

// the highest partial any frame holds above −120 dB of the table's loudest (the stored size)
int usedPartials (const UserTable& t)
{
    float top = 0.0f;
    for (const auto a : t.amp)
        top = std::max (top, std::abs (a));
    if (top <= 0.0f)
        return 1;
    int used = 1;
    for (int f = 0; f < frameCount; ++f)
        for (int h = t.partials - 1; h >= used; --h)
            if (std::abs (t.amp[(std::size_t) (f * t.partials + h)]) > top * 1.0e-6f)
            {
                used = h + 1;
                break;
            }
    return used;
}

// --- bytes
struct Writer
{
    std::vector<std::uint8_t> b;
    void u8 (std::uint8_t v) { b.push_back (v); }
    void u16 (std::uint16_t v) { b.push_back ((std::uint8_t) v), b.push_back ((std::uint8_t) (v >> 8)); }
    void u32 (std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            b.push_back ((std::uint8_t) (v >> (8 * i)));
    }
    void f64 (double v)
    {
        std::uint64_t bits;
        std::memcpy (&bits, &v, 8);
        for (int i = 0; i < 8; ++i)
            b.push_back ((std::uint8_t) (bits >> (8 * i)));
    }
    void str (const std::string& s)
    {
        const auto n = (std::uint16_t) std::min<std::size_t> (s.size(), 1000);
        u16 (n);
        b.insert (b.end(), s.begin(), s.begin() + n);
    }
    void tag (const char* t) { b.insert (b.end(), t, t + 4); }
};
struct Reader
{
    const std::vector<std::uint8_t>& b;
    std::size_t at = 0;
    bool ok = true;
    bool need (std::size_t n)
    {
        if (at + n > b.size())
            ok = false;
        return ok;
    }
    std::uint8_t u8() { return need (1) ? b[at++] : 0; }
    std::uint16_t u16()
    {
        if (! need (2))
            return 0;
        const auto v = (std::uint16_t) (b[at] | (b[at + 1] << 8));
        at += 2;
        return v;
    }
    std::uint32_t u32()
    {
        if (! need (4))
            return 0;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= (std::uint32_t) b[at + (std::size_t) i] << (8 * i);
        at += 4;
        return v;
    }
    double f64()
    {
        if (! need (8))
            return 0.0;
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i)
            bits |= (std::uint64_t) b[at + (std::size_t) i] << (8 * i);
        at += 8;
        double v;
        std::memcpy (&v, &bits, 8);
        return v;
    }
    std::string str()
    {
        const auto n = u16();
        if (! need (n))
            return {};
        std::string s (b.begin() + (std::ptrdiff_t) at, b.begin() + (std::ptrdiff_t) (at + n));
        at += n;
        return s;
    }
};

// IEEE half floats (the amplitudes)
std::uint16_t toHalf (float f)
{
    std::uint32_t x;
    std::memcpy (&x, &f, 4);
    const auto sign = (x >> 16) & 0x8000u;
    const auto e = (int) ((x >> 23) & 0xffu) - 127 + 15;
    auto m = x & 0x7fffffu;
    if (! std::isfinite (f))
        return (std::uint16_t) (sign | 0x7bffu); // (the largest)
    if (e >= 31)
        return (std::uint16_t) (sign | 0x7bffu);
    if (e <= 0)
    {
        if (e < -10)
            return (std::uint16_t) sign;
        m |= 0x800000u;
        const auto shift = (std::uint32_t) (14 - e);
        auto h = m >> shift;
        if ((m >> (shift - 1)) & 1u)
            ++h;
        return (std::uint16_t) (sign | h);
    }
    auto h = (std::uint32_t) (sign | ((std::uint32_t) e << 10) | (m >> 13));
    if (m & 0x1000u)
        ++h; // (rounds; a carry into the exponent is still right)
    return (std::uint16_t) h;
}
float fromHalf (std::uint16_t h)
{
    const auto sign = (h & 0x8000u) ? -1.0f : 1.0f;
    const auto e = (h >> 10) & 0x1fu;
    const auto m = h & 0x3ffu;
    if (e == 0)
        return sign * std::ldexp ((float) m, -24);
    if (e == 31)
        return sign * 65504.0f;
    return sign * std::ldexp ((float) (m | 0x400u), (int) e - 25);
}
} // namespace

bool HelixUserData::empty() const noexcept
{
    for (const auto& t : tables)
        if (! t.empty())
            return false;
    return sample.frames() == 0 && spectral.empty();
}

std::vector<std::uint8_t> encodeUserData (const HelixUserData& d)
{
    if (d.empty())
        return {};
    Writer w;
    w.tag ("HLX2");
    w.u16 (1); // (version)
    for (int k = 0; k < oscCount; ++k)
    {
        const auto& t = d.tables[(std::size_t) k];
        if (t.empty())
            continue;
        const auto used = usedPartials (t);
        Writer c;
        c.str (t.name);
        c.u16 ((std::uint16_t) used);
        for (int f = 0; f < frameCount; ++f)
            for (int h = 0; h < used; ++h)
            {
                const auto i = (std::size_t) (f * t.partials + h);
                auto a = t.amp[i];
                auto ph = i < t.phase.size() ? (double) t.phase[i] : 0.0;
                if (a < 0.0f) // (a negative amplitude is the opposite phase)
                    a = -a, ph += pi;
                c.u16 (toHalf (a));
                ph -= 2.0 * pi * std::floor (ph / (2.0 * pi));
                c.u16 ((std::uint16_t) ((std::uint32_t) std::lround (ph / (2.0 * pi) * 65536.0) & 0xffffu));
            }
        const char tag[5] { 'T', 'B', (char) ('0' + k), ' ', 0 };
        w.tag (tag);
        w.u32 ((std::uint32_t) c.b.size());
        w.b.insert (w.b.end(), c.b.begin(), c.b.end());
    }
    if (d.sample.frames() > 0)
    {
        const auto stereo = d.sample.r.size() == d.sample.l.size();
        Writer c;
        c.str (d.sampleName);
        c.f64 (d.sample.rate);
        c.u8 (stereo ? 2 : 1);
        c.u32 ((std::uint32_t) d.sample.frames());
        for (int i = 0; i < d.sample.frames(); ++i)
            for (int s = 0; s < (stereo ? 2 : 1); ++s)
            {
                const auto v = std::clamp ((s == 0 ? d.sample.l : d.sample.r)[(std::size_t) i], -1.0f, 1.0f);
                c.u16 ((std::uint16_t) (std::int16_t) std::lround (v * 32767.0f));
            }
        w.tag ("SMPL");
        w.u32 ((std::uint32_t) c.b.size());
        w.b.insert (w.b.end(), c.b.begin(), c.b.end());
    }
    if (! d.spectral.empty()) // (190mt: the sample's partials: frequency as 1/4096 octave above 5 Hz, amplitude as half float)
    {
        const auto& sp = d.spectral;
        const auto frames = std::min (sp.frames, spectralFrames), partials = std::min (sp.partials, maxSpectralPartials);
        Writer c;
        c.u16 ((std::uint16_t) frames);
        c.u16 ((std::uint16_t) partials);
        for (int f = 0; f < frames; ++f)
            for (int h = 0; h < partials; ++h)
            {
                const auto i = (std::size_t) (f * sp.partials + h);
                const auto hz = std::isfinite (sp.hz[i]) ? std::max ((double) sp.hz[i], 5.0) : 5.0;
                c.u16 ((std::uint16_t) std::clamp (std::lround (std::log2 (hz / 5.0) * 4096.0), 0L, 65535L));
                c.u16 (toHalf (std::isfinite (sp.amp[i]) ? std::abs (sp.amp[i]) : 0.0f));
            }
        w.tag ("SPEC");
        w.u32 ((std::uint32_t) c.b.size());
        w.b.insert (w.b.end(), c.b.begin(), c.b.end());
    }
    return w.b;
}

bool decodeUserData (const std::vector<std::uint8_t>& bytes, HelixUserData& out)
{
    out = {};
    if (bytes.size() < 6 || std::memcmp (bytes.data(), "HLX2", 4) != 0)
        return false;
    Reader r { bytes };
    r.at = 4;
    if (r.u16() != 1)
        return false;
    while (r.ok && r.at + 8 <= bytes.size())
    {
        char tag[4];
        std::memcpy (tag, bytes.data() + r.at, 4);
        r.at += 4;
        const auto len = r.u32();
        if (! r.need (len))
            break;
        const auto end = r.at + len;
        if (tag[0] == 'T' && tag[1] == 'B' && tag[2] >= '0' && tag[2] < '0' + oscCount)
        {
            auto& t = out.tables[(std::size_t) (tag[2] - '0')];
            t.name = r.str();
            const auto partials = std::clamp ((int) r.u16(), 0, maxUserPartials);
            if (partials > 0 && r.need ((std::size_t) frameCount * (std::size_t) partials * 4))
            {
                t.partials = partials;
                t.amp.resize ((std::size_t) frameCount * (std::size_t) partials);
                t.phase.resize (t.amp.size());
                for (std::size_t i = 0; i < t.amp.size(); ++i)
                {
                    t.amp[i] = fromHalf (r.u16());
                    t.phase[i] = (float) ((double) r.u16() / 65536.0 * 2.0 * pi);
                }
            }
        }
        else if (std::memcmp (tag, "SMPL", 4) == 0)
        {
            out.sampleName = r.str();
            out.sample.rate = std::clamp (r.f64(), 1000.0, 768000.0);
            const auto ch = std::clamp ((int) r.u8(), 1, 2);
            const auto frames = (std::size_t) r.u32();
            if (frames > 0 && r.need (frames * (std::size_t) ch * 2))
            {
                out.sample.l.resize (frames);
                if (ch == 2)
                    out.sample.r.resize (frames);
                for (std::size_t i = 0; i < frames; ++i)
                    for (int s = 0; s < ch; ++s)
                        (s == 0 ? out.sample.l : out.sample.r)[i] = (float) (std::int16_t) r.u16() / 32767.0f;
            }
        }
        else if (std::memcmp (tag, "SPEC", 4) == 0) // (190mt)
        {
            const auto frames = std::clamp ((int) r.u16(), 0, spectralFrames), partials = std::clamp ((int) r.u16(), 0, maxSpectralPartials);
            if (frames > 0 && partials > 0 && r.need ((std::size_t) frames * (std::size_t) partials * 4))
            {
                auto& sp = out.spectral;
                sp.frames = frames, sp.partials = partials;
                sp.hz.resize ((std::size_t) frames * (std::size_t) partials);
                sp.amp.resize (sp.hz.size());
                for (std::size_t i = 0; i < sp.hz.size(); ++i)
                {
                    sp.hz[i] = (float) (5.0 * std::exp2 ((double) r.u16() / 4096.0));
                    sp.amp[i] = fromHalf (r.u16());
                }
            }
        }
        r.at = end; // (unknown chunks: skipped, so a later version's data still loads)
    }
    return true;
}

UserTable userTableOf (int factoryIndex)
{
    auto t = emptyTable (tableName (factoryIndex), maxUserPartials);
    for (int f = 0; f < frameCount; ++f)
        tableSpectrum (factoryIndex, f, t.amp.data() + (std::size_t) f * maxUserPartials, t.phase.data() + (std::size_t) f * maxUserPartials,
                       maxUserPartials);
    t.partials = usedPartials (t);
    if (t.partials < maxUserPartials) // (stored at the size its partials need)
    {
        UserTable s = emptyTable (t.name, t.partials);
        for (int f = 0; f < frameCount; ++f)
            for (int h = 0; h < t.partials; ++h)
            {
                s.amp[(std::size_t) (f * t.partials + h)] = t.amp[(std::size_t) (f * maxUserPartials + h)];
                s.phase[(std::size_t) (f * t.partials + h)] = t.phase[(std::size_t) (f * maxUserPartials + h)];
            }
        return s;
    }
    return t;
}

UserTable userTableFromCycles (const std::vector<float>& cycles, int length, int frames, const std::string& name)
{
    frames = std::clamp (frames, 1, (int) (cycles.size() / (std::size_t) std::max (length, 1)));
    int n = 1;
    while (n < length && n < 8192)
        n *= 2;
    const auto partials = std::min (maxUserPartials, n / 2 - 1);
    std::vector<std::vector<float>> amps ((std::size_t) frames, std::vector<float> ((std::size_t) partials)), phases = amps;
    for (int f = 0; f < frames; ++f)
    {
        const auto* c = cycles.data() + (std::size_t) f * (std::size_t) length;
        const auto cycle = n == length ? std::vector<double> (c, c + length) : resampleCycle (c, (double) length, n, c + length);
        spectrumOfCycle (cycle, partials, amps[(std::size_t) f].data(), phases[(std::size_t) f].data());
    }
    auto t = spreadFrames (amps, phases, partials, name);
    const auto used = usedPartials (t);
    if (used < partials)
    {
        auto s = emptyTable (name, used);
        for (int f = 0; f < frameCount; ++f)
            for (int h = 0; h < used; ++h)
                s.amp[(std::size_t) (f * used + h)] = t.amp[(std::size_t) (f * partials + h)],
                s.phase[(std::size_t) (f * used + h)] = t.phase[(std::size_t) (f * partials + h)];
        return s;
    }
    return t;
}

std::vector<float> userTableCycle (const UserTable& t, int frame, int length)
{
    std::vector<float> out ((std::size_t) std::max (length, 1), 0.0f);
    if (t.empty())
        return out;
    frame = std::clamp (frame, 0, frameCount - 1);
    int n = 1;
    while (n < length)
        n *= 2;
    std::vector<std::complex<double>> a ((std::size_t) n);
    for (int h = 1; h <= t.partials && h < n / 2; ++h)
    {
        const auto amp = (double) t.amp[(std::size_t) (frame * t.partials + h - 1)];
        const auto ph = (double) t.phase[(std::size_t) (frame * t.partials + h - 1)];
        const auto c = std::polar (amp * 0.5, ph - pi * 0.5);
        a[(std::size_t) h] = c;
        a[(std::size_t) (n - h)] = std::conj (c);
    }
    fft (a, true);
    for (int i = 0; i < length; ++i)
        out[(std::size_t) i] = (float) a[(std::size_t) ((std::int64_t) i * n / length)].real();
    return out;
}

void setUserTableCycle (UserTable& t, int frame, const std::vector<float>& cycle)
{
    if (t.empty() || cycle.size() < 2)
        return;
    frame = std::clamp (frame, 0, frameCount - 1);
    int n = 1;
    while (n < (int) cycle.size())
        n *= 2;
    const auto c = n == (int) cycle.size() ? std::vector<double> (cycle.begin(), cycle.end())
                                           : resampleCycle (cycle.data(), (double) cycle.size(), n, cycle.data() + cycle.size());
    const auto partials = std::min (maxUserPartials, n / 2 - 1);
    if (partials > t.partials) // (the table grows to the partials the drawing has)
    {
        auto g = emptyTable (t.name, partials);
        for (int f = 0; f < frameCount; ++f)
            for (int h = 0; h < t.partials; ++h)
                g.amp[(std::size_t) (f * partials + h)] = t.amp[(std::size_t) (f * t.partials + h)],
                g.phase[(std::size_t) (f * partials + h)] = t.phase[(std::size_t) (f * t.partials + h)];
        t = std::move (g);
    }
    std::vector<float> amp ((std::size_t) partials), phase ((std::size_t) partials);
    spectrumOfCycle (c, partials, amp.data(), phase.data());
    for (int h = 0; h < t.partials; ++h)
    {
        t.amp[(std::size_t) (frame * t.partials + h)] = h < partials ? amp[(std::size_t) h] : 0.0f;
        t.phase[(std::size_t) (frame * t.partials + h)] = h < partials ? phase[(std::size_t) h] : 0.0f;
    }
}

// --- import
double detectPeriod (const float* x, int samples, double rate)
{
    // YIN (de Cheveigné & Kawahara 2002): the cumulative-mean-normalised difference function, its first dip under 0.12 (or
    // its lowest), refined by a parabola; on a window of the file's loud part
    if (x == nullptr || samples < 64 || rate <= 0.0)
        return 0.0;
    const auto minLag = std::max (2, (int) (rate / 2000.0)), maxLag = std::min ((int) (rate / 25.0), samples / 3);
    if (maxLag <= minLag + 2)
        return 0.0;
    // the window starts where the audio first reaches a quarter of its peak
    float peak = 0.0f;
    for (int i = 0; i < samples; ++i)
        peak = std::max (peak, std::abs (x[i]));
    if (peak <= 1.0e-6f)
        return 0.0;
    int start = 0;
    while (start < samples && std::abs (x[start]) < 0.25f * peak)
        ++start;
    const auto window = std::min (4096, samples - start - maxLag);
    if (window < 2 * minLag)
        return 0.0;
    std::vector<double> d ((std::size_t) maxLag + 1, 0.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const auto e = (double) x[start + i] - (double) x[start + i + lag];
            s += e * e;
        }
        d[(std::size_t) lag] = s;
    }
    std::vector<double> c ((std::size_t) maxLag + 1, 1.0);
    double run = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        run += d[(std::size_t) lag];
        c[(std::size_t) lag] = run > 0.0 ? d[(std::size_t) lag] * lag / run : 1.0;
    }
    int best = -1;
    for (int lag = minLag; lag < maxLag; ++lag)
        if (c[(std::size_t) lag] < 0.12)
        {
            while (lag + 1 < maxLag && c[(std::size_t) lag + 1] < c[(std::size_t) lag])
                ++lag;
            best = lag;
            break;
        }
    if (best < 0)
    {
        best = minLag;
        for (int lag = minLag; lag < maxLag; ++lag)
            if (c[(std::size_t) lag] < c[(std::size_t) best])
                best = lag;
        if (c[(std::size_t) best] > 0.5)
            return 0.0; // (nothing periodic)
    }
    const auto a = c[(std::size_t) best - 1], b = c[(std::size_t) best], e = c[(std::size_t) best + 1];
    const auto den = a - 2.0 * b + e;
    const auto shift = std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (a - e) / den, -0.5, 0.5) : 0.0;
    return (double) best + shift;
}

UserTable userTableFromAudio (const float* audio, int samples, double rate, const ImportOptions& o, const std::string& name, std::string* note)
{
    const auto say = [note] (const std::string& s) {
        if (note != nullptr)
            *note = s;
    };
    if (audio == nullptr || samples < 16)
    {
        say ("The file has no audio.");
        return {};
    }
    auto slicing = o.slicing;
    auto period = 0.0;
    if (slicing == Slicing::pitch)
    {
        period = detectPeriod (audio, samples, rate);
        if (period < 8.0)
            slicing = Slicing::fixed; // (no pitch found: fixed frames)
    }
    const auto frameSize = std::clamp (o.frameSize, 16, 65536);
    const auto cycleLength = slicing == Slicing::pitch ? period : (double) std::min (frameSize, samples);
    // the cycles to keep: every one (64 at most, spread evenly) or, spread, 64 windows over the whole file
    const auto available = std::max (1, (int) std::floor ((double) samples / cycleLength));
    const auto keys = slicing == Slicing::spread ? frameCount : std::min (available, frameCount);
    std::vector<float> cycles;
    cycles.reserve ((std::size_t) keys * userFrameLength);
    for (int k = 0; k < keys; ++k)
    {
        double from;
        if (slicing == Slicing::spread)
            from = keys > 1 ? (double) k * std::max (0.0, (double) samples - cycleLength) / (double) (keys - 1) : 0.0;
        else
        {
            const auto cycle = keys > 1 ? (int) std::lround ((double) k * (double) (available - 1) / (double) (keys - 1)) : 0;
            from = (double) cycle * cycleLength;
        }
        const auto first = (std::ptrdiff_t) std::floor (from);
        const auto c = resampleCycle (audio + first, cycleLength, userFrameLength, audio + samples);
        for (const auto v : c)
            cycles.push_back ((float) v);
    }
    auto t = userTableFromCycles (cycles, userFrameLength, keys, name);
    if (t.empty())
    {
        say ("The file has no audio.");
        return {};
    }
    // options: phase-align (each frame's fundamental at phase 0, its partials moved with it), normalise (each frame at the
    // same loudness, RMS)
    for (int f = 0; f < frameCount; ++f)
    {
        auto* a = t.amp.data() + (std::size_t) f * (std::size_t) t.partials;
        auto* ph = t.phase.data() + (std::size_t) f * (std::size_t) t.partials;
        if (o.phaseAlign && a[0] > 1.0e-6f)
        {
            const auto turn = (double) ph[0];
            for (int h = 0; h < t.partials; ++h)
                ph[h] = (float) std::remainder ((double) ph[h] - (double) (h + 1) * turn, 2.0 * pi);
        }
        if (o.normalise)
        {
            double e = 0.0;
            for (int h = 0; h < t.partials; ++h)
                e += (double) a[h] * a[h];
            if (e > 1.0e-12)
            {
                const auto g = (float) (1.0 / std::sqrt (e));
                for (int h = 0; h < t.partials; ++h)
                    a[h] *= g;
            }
        }
    }
    char text[200];
    if (slicing == Slicing::pitch)
        std::snprintf (text, sizeof text, "Pitch found: %.1f Hz (a cycle of %.1f samples); %d cycles over 64 frames.", rate / period, period, keys);
    else if (slicing == Slicing::fixed)
        std::snprintf (text, sizeof text, "%s%d frames of %d samples over 64 frames.", o.slicing == Slicing::pitch ? "No pitch found: " : "", keys,
                       (int) cycleLength);
    else
        std::snprintf (text, sizeof text, "64 frames of %d samples spread over the file.", (int) cycleLength);
    say (text);
    return t;
}

// --- formulas: LYNX's small expression language
namespace
{
struct Node
{
    enum Kind : std::uint8_t { number, var, neg, not_, bin, call, cond } kind = number;
    double value = 0.0;
    int op = 0;         // var: 0 x, 1 p, 2 t, 3 f; bin: the operator; call: the function
    int a = -1, b = -1, c = -1;
};
enum Op { add, sub, mul, divide, mod, power, lt, gt, le, ge, eq, ne, andOp, orOp };
struct Fn
{
    const char* name;
    int args;
};
const Fn functions[] { { "sin", 1 }, { "cos", 1 }, { "tan", 1 }, { "asin", 1 }, { "acos", 1 }, { "atan", 1 }, { "atan2", 2 }, { "sinh", 1 },
                       { "cosh", 1 }, { "tanh", 1 }, { "exp", 1 }, { "log", 1 }, { "log2", 1 }, { "log10", 1 }, { "sqrt", 1 }, { "abs", 1 },
                       { "sign", 1 }, { "floor", 1 }, { "ceil", 1 }, { "round", 1 }, { "frac", 1 }, { "min", 2 }, { "max", 2 }, { "pow", 2 },
                       { "clamp", 3 }, { "mix", 3 }, { "step", 2 }, { "saw", 1 }, { "square", 1 }, { "tri", 1 }, { "rand", 1 } };
constexpr int fnCount = (int) (sizeof functions / sizeof functions[0]);

struct Parser
{
    const std::string& s;
    std::size_t at = 0;
    std::vector<Node> nodes;
    std::string error;
    int depth = 0;
    static constexpr int maxNodes = 4000, maxDepth = 64;

    void fail (const std::string& what)
    {
        if (error.empty())
            error = what + " (at character " + std::to_string (at + 1) + ")";
    }
    void skip()
    {
        while (at < s.size() && std::isspace ((unsigned char) s[at]))
            ++at;
    }
    bool take (const char* t)
    {
        skip();
        const auto n = std::strlen (t);
        if (s.compare (at, n, t) == 0)
        {
            at += n;
            return true;
        }
        return false;
    }
    int add (Node n)
    {
        if ((int) nodes.size() >= maxNodes)
        {
            fail ("The formula is too long");
            return -1;
        }
        nodes.push_back (n);
        return (int) nodes.size() - 1;
    }
    int expression()
    {
        if (++depth > maxDepth)
        {
            fail ("Too many brackets");
            --depth;
            return -1;
        }
        auto c = orExpr();
        if (error.empty() && take ("?"))
        {
            const auto a = expression();
            if (! take (":"))
                fail ("Expected ':'");
            const auto b = expression();
            Node n;
            n.kind = Node::cond, n.a = c, n.b = a, n.c = b;
            c = add (n);
        }
        --depth;
        return c;
    }
    int binary (int left, int op, int right)
    {
        Node n;
        n.kind = Node::bin, n.op = op, n.a = left, n.b = right;
        return add (n);
    }
    int orExpr()
    {
        auto l = andExpr();
        while (error.empty() && take ("||"))
            l = binary (l, orOp, andExpr());
        return l;
    }
    int andExpr()
    {
        auto l = compare();
        while (error.empty() && take ("&&"))
            l = binary (l, andOp, compare());
        return l;
    }
    int compare()
    {
        auto l = sum();
        while (error.empty())
        {
            if (take ("<="))
                l = binary (l, le, sum());
            else if (take (">="))
                l = binary (l, ge, sum());
            else if (take ("=="))
                l = binary (l, eq, sum());
            else if (take ("!="))
                l = binary (l, ne, sum());
            else if (take ("<"))
                l = binary (l, lt, sum());
            else if (take (">"))
                l = binary (l, gt, sum());
            else
                break;
        }
        return l;
    }
    int sum()
    {
        auto l = product();
        while (error.empty())
        {
            if (take ("+"))
                l = binary (l, add_, product());
            else if (take ("-"))
                l = binary (l, sub, product());
            else
                break;
        }
        return l;
    }
    static constexpr int add_ = Op::add;
    int product()
    {
        auto l = unary();
        while (error.empty())
        {
            if (take ("*"))
                l = binary (l, mul, unary());
            else if (take ("/"))
                l = binary (l, divide, unary());
            else if (take ("%"))
                l = binary (l, mod, unary());
            else
                break;
        }
        return l;
    }
    int unary()
    {
        if (take ("-"))
        {
            Node n;
            n.kind = Node::neg, n.a = unary();
            return add (n);
        }
        if (take ("+"))
            return unary();
        if (take ("!"))
        {
            Node n;
            n.kind = Node::not_, n.a = unary();
            return add (n);
        }
        return powerExpr();
    }
    int powerExpr()
    {
        const auto base = atom();
        if (error.empty() && take ("^"))
        {
            if (++depth > maxDepth)
            {
                fail ("Too many powers in a row");
                return -1;
            }
            const auto e = unary(); // (right-associative: 2^3^2 = 2^9)
            --depth;
            return binary (base, power, e);
        }
        return base;
    }
    int atom()
    {
        skip();
        if (at >= s.size())
        {
            fail ("The formula ends too early");
            return -1;
        }
        const auto ch = s[at];
        if (std::isdigit ((unsigned char) ch) || ch == '.')
        {
            const auto from = at;
            while (at < s.size() && (std::isdigit ((unsigned char) s[at]) || s[at] == '.'))
                ++at;
            if (at < s.size() && (s[at] == 'e' || s[at] == 'E') && at + 1 < s.size()
                && (std::isdigit ((unsigned char) s[at + 1]) || ((s[at + 1] == '-' || s[at + 1] == '+') && at + 2 < s.size() && std::isdigit ((unsigned char) s[at + 2]))))
            {
                at += 2;
                while (at < s.size() && std::isdigit ((unsigned char) s[at]))
                    ++at;
            }
            Node n;
            char* end = nullptr;
            const auto text = s.substr (from, at - from);
            n.value = std::strtod (text.c_str(), &end);
            if (end == nullptr || *end != 0)
                fail ("Not a number: " + text);
            return add (n);
        }
        if (take ("("))
        {
            const auto e = expression();
            if (! take (")"))
                fail ("Expected ')'");
            return e;
        }
        if (std::isalpha ((unsigned char) ch) || ch == '_')
        {
            const auto from = at;
            while (at < s.size() && (std::isalnum ((unsigned char) s[at]) || s[at] == '_'))
                ++at;
            auto word = s.substr (from, at - from);
            std::transform (word.begin(), word.end(), word.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            const char* vars[4] { "x", "p", "t", "f" };
            for (int v = 0; v < 4; ++v)
                if (word == vars[v])
                {
                    Node n;
                    n.kind = Node::var, n.op = v;
                    return add (n);
                }
            if (word == "pi" || word == "e")
            {
                Node n;
                n.value = word == "pi" ? pi : 2.718281828459045;
                return add (n);
            }
            for (int fnum = 0; fnum < fnCount; ++fnum)
                if (word == functions[fnum].name)
                {
                    if (! take ("("))
                    {
                        fail ("Expected '(' after " + word);
                        return -1;
                    }
                    Node n;
                    n.kind = Node::call, n.op = fnum;
                    int* slots[3] { &n.a, &n.b, &n.c };
                    for (int k = 0; k < functions[fnum].args; ++k)
                    {
                        if (k > 0 && ! take (","))
                        {
                            fail (word + " takes " + std::to_string (functions[fnum].args) + " values");
                            return -1;
                        }
                        *slots[k] = expression();
                    }
                    if (! take (")"))
                        fail ("Expected ')' after " + word + "'s values");
                    return add (n);
                }
            fail ("Unknown name: " + word);
            return -1;
        }
        fail (std::string ("Unexpected '") + ch + "'");
        return -1;
    }
};

double evaluate (const std::vector<Node>& nodes, int i, const double* vars) noexcept
{
    const auto& n = nodes[(std::size_t) i];
    switch (n.kind)
    {
        case Node::number: return n.value;
        case Node::var: return vars[n.op];
        case Node::neg: return -evaluate (nodes, n.a, vars);
        case Node::not_: return evaluate (nodes, n.a, vars) == 0.0 ? 1.0 : 0.0;
        case Node::cond: return evaluate (nodes, n.a, vars) != 0.0 ? evaluate (nodes, n.b, vars) : evaluate (nodes, n.c, vars);
        case Node::bin:
        {
            const auto a = evaluate (nodes, n.a, vars);
            if (n.op == andOp)
                return a != 0.0 && evaluate (nodes, n.b, vars) != 0.0 ? 1.0 : 0.0;
            if (n.op == orOp)
                return a != 0.0 || evaluate (nodes, n.b, vars) != 0.0 ? 1.0 : 0.0;
            const auto b = evaluate (nodes, n.b, vars);
            switch (n.op)
            {
                case Op::add: return a + b;
                case sub: return a - b;
                case mul: return a * b;
                case divide: return b != 0.0 ? a / b : 0.0;
                case mod: return b != 0.0 ? a - b * std::floor (a / b) : 0.0;
                case power: return std::pow (a, b);
                case lt: return a < b ? 1.0 : 0.0;
                case gt: return a > b ? 1.0 : 0.0;
                case le: return a <= b ? 1.0 : 0.0;
                case ge: return a >= b ? 1.0 : 0.0;
                case eq: return a == b ? 1.0 : 0.0;
                default: return a != b ? 1.0 : 0.0;
            }
        }
        case Node::call:
        {
            const auto a = n.a >= 0 ? evaluate (nodes, n.a, vars) : 0.0;
            const auto b = n.b >= 0 ? evaluate (nodes, n.b, vars) : 0.0;
            const auto c = n.c >= 0 ? evaluate (nodes, n.c, vars) : 0.0;
            const auto wrap = [] (double v) { return v - std::floor (v); };
            switch (n.op)
            {
                case 0: return std::sin (a);
                case 1: return std::cos (a);
                case 2: return std::tan (a);
                case 3: return std::asin (std::clamp (a, -1.0, 1.0));
                case 4: return std::acos (std::clamp (a, -1.0, 1.0));
                case 5: return std::atan (a);
                case 6: return std::atan2 (a, b);
                case 7: return std::sinh (std::clamp (a, -50.0, 50.0));
                case 8: return std::cosh (std::clamp (a, -50.0, 50.0));
                case 9: return std::tanh (a);
                case 10: return std::exp (std::min (a, 50.0));
                case 11: return a > 0.0 ? std::log (a) : 0.0;
                case 12: return a > 0.0 ? std::log2 (a) : 0.0;
                case 13: return a > 0.0 ? std::log10 (a) : 0.0;
                case 14: return a > 0.0 ? std::sqrt (a) : 0.0;
                case 15: return std::abs (a);
                case 16: return a > 0.0 ? 1.0 : a < 0.0 ? -1.0 : 0.0;
                case 17: return std::floor (a);
                case 18: return std::ceil (a);
                case 19: return std::round (a);
                case 20: return wrap (a);
                case 21: return std::min (a, b);
                case 22: return std::max (a, b);
                case 23: return std::pow (a, b);
                case 24: return std::clamp (a, std::min (b, c), std::max (b, c));
                case 25: return a + (b - a) * c;
                case 26: return b >= a ? 1.0 : 0.0;
                case 27: return 1.0 - 2.0 * wrap (a);                       // saw: falling, one cycle per 1
                case 28: return wrap (a) < 0.5 ? 1.0 : -1.0;                // square
                case 29: return 1.0 - 4.0 * std::abs (wrap (a + 0.25) - 0.5); // tri
                default:
                {
                    // rand: a fixed value per whole number (the same table every time)
                    auto k = (std::uint32_t) (std::int64_t) std::floor (a) * 2654435761u;
                    k ^= k >> 16, k *= 0x7feb352du, k ^= k >> 15, k *= 0x846ca68bu, k ^= k >> 16;
                    return (double) (k >> 8) / 8388608.0 - 1.0;
                }
            }
        }
    }
    return 0.0;
}
} // namespace

bool userTableFromFormula (const std::string& formula, UserTable& out, std::string& error)
{
    error.clear();
    if (formula.size() > 2000)
    {
        error = "The formula is too long (2000 characters at most)";
        return false;
    }
    Parser p { formula, 0, {}, {}, 0 };
    const auto root = p.expression();
    p.skip();
    if (p.error.empty() && p.at < formula.size())
        p.fail (std::string ("Unexpected '") + formula[p.at] + "'");
    if (! p.error.empty() || root < 0)
    {
        error = p.error.empty() ? "The formula is empty" : p.error;
        return false;
    }
    std::vector<float> cycles ((std::size_t) frameCount * userFrameLength);
    double vars[4];
    for (int f = 0; f < frameCount; ++f)
        for (int i = 0; i < userFrameLength; ++i)
        {
            const auto ph = (double) i / userFrameLength;
            vars[0] = 2.0 * ph - 1.0, vars[1] = ph, vars[2] = (double) f / (frameCount - 1), vars[3] = (double) f;
            const auto v = evaluate (p.nodes, root, vars);
            cycles[(std::size_t) (f * userFrameLength + i)] = std::isfinite (v) ? (float) std::clamp (v, -1.0e3, 1.0e3) : 0.0f;
        }
    out = userTableFromCycles (cycles, userFrameLength, frameCount, formula.size() > 40 ? formula.substr (0, 40) + "..." : formula);
    return true;
}

std::vector<std::uint8_t> userTableWav (const UserTable& t)
{
    // RIFF / WAVE, 32-bit float, mono, 48 kHz: the 64 frames one after the other (userFrameLength samples each)
    std::vector<float> data;
    data.reserve ((std::size_t) frameCount * userFrameLength);
    float peak = 0.0f;
    for (int f = 0; f < frameCount; ++f)
    {
        const auto c = userTableCycle (t, f, userFrameLength);
        for (const auto v : c)
            peak = std::max (peak, std::abs (v));
        data.insert (data.end(), c.begin(), c.end());
    }
    if (peak > 1.0f)
        for (auto& v : data)
            v /= peak;
    Writer w;
    const auto bytes = (std::uint32_t) (data.size() * 4);
    w.tag ("RIFF");
    w.u32 (36 + bytes);
    w.tag ("WAVE");
    w.tag ("fmt ");
    w.u32 (16);
    w.u16 (3); // (IEEE float)
    w.u16 (1);
    w.u32 (48000);
    w.u32 (48000 * 4);
    w.u16 (4);
    w.u16 (32);
    w.tag ("data");
    w.u32 (bytes);
    const auto at = w.b.size();
    w.b.resize (at + bytes);
    std::memcpy (w.b.data() + at, data.data(), bytes);
    return w.b;
}

// --- Helix round 3 (round 190mt): the sample's partials and its band-limited levels (LYNX's own designs [MODEL])
namespace
{
// the stored form of a partial (SPEC chunk): its frequency on a 1/4096-octave grid above 5 Hz, its amplitude as half float;
// the analysis gives exactly what the chunk keeps, so a stored and a freshly made analysis play the same
float gridHz (double hz) { return (float) (5.0 * std::exp2 ((double) std::clamp (std::lround (std::log2 (std::max (hz, 5.0) / 5.0) * 4096.0), 0L, 65535L) / 4096.0)); }
float gridAmp (double a) { return fromHalf (toHalf ((float) std::max (a, 0.0))); }
} // namespace

SpectralData analyseSpectrum (const SampleBuffer& smp)
{
    SpectralData out;
    const auto n = smp.frames();
    if (n < 16 || ! (smp.rate > 0.0))
        return out;
    const auto stereo = smp.r.size() == smp.l.size();
    std::vector<float> x ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        x[(std::size_t) i] = stereo ? 0.5f * (smp.l[(std::size_t) i] + smp.r[(std::size_t) i]) : smp.l[(std::size_t) i];
    const auto rate = smp.rate;
    int N = 1024; // (the window: ~85 ms or more, a power of two; zero-padded to twice its length)
    while (N < 0.085 * rate && N < 16384)
        N *= 2;
    const int M = 2 * N;
    const int frames = n > N ? spectralFrames : 1;
    // Blackman-Harris (4 terms): side lobes at −92 dB, so a peak is a partial
    std::vector<double> win ((std::size_t) N);
    double wsum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const auto t = 2.0 * pi * i / (N - 1);
        win[(std::size_t) i] = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        wsum += win[(std::size_t) i];
    }
    struct Peak
    {
        double hz, amp;
    };
    std::vector<std::vector<Peak>> peaks ((std::size_t) frames);
    double loudest = 0.0;
    std::vector<std::complex<double>> a ((std::size_t) M);
    std::vector<double> db ((std::size_t) M / 2 + 1);
    for (int f = 0; f < frames; ++f)
    {
        const auto centre = frames > 1 ? N / 2 + (int) ((long long) f * (n - N) / (frames - 1)) : n / 2;
        const auto start = centre - N / 2;
        std::fill (a.begin(), a.end(), std::complex<double> {});
        for (int i = 0; i < N; ++i)
        {
            const auto j = start + i;
            a[(std::size_t) i] = j >= 0 && j < n ? (double) x[(std::size_t) j] * win[(std::size_t) i] : 0.0;
        }
        fft (a, false);
        for (int k = 0; k <= M / 2; ++k)
            db[(std::size_t) k] = 20.0 * std::log10 (std::max (std::abs (a[(std::size_t) k]) * 2.0 / wsum, 1.0e-12));
        auto& list = peaks[(std::size_t) f];
        for (int k = 2; k < M / 2 - 2; ++k)
        {
            const auto al = db[(std::size_t) k - 1], be = db[(std::size_t) k], ga = db[(std::size_t) k + 1];
            if (! (be > al && be >= ga) || be < -100.0)
                continue;
            const auto den = al - 2.0 * be + ga;
            const auto p = den < 0.0 ? std::clamp (0.5 * (al - ga) / den, -0.5, 0.5) : 0.0;
            const auto hz = (k + p) * rate / M;
            if (hz < 20.0 || hz > 0.48 * rate)
                continue;
            list.push_back ({ hz, std::pow (10.0, (be - 0.25 * (al - ga) * p) / 20.0) });
        }
        std::sort (list.begin(), list.end(), [] (const Peak& u, const Peak& v) { return u.amp > v.amp || (u.amp == v.amp && u.hz < v.hz); });
        if (list.size() > (std::size_t) maxSpectralPartials)
            list.resize ((std::size_t) maxSpectralPartials);
        if (! list.empty())
            loudest = std::max (loudest, list.front().amp);
    }
    if (loudest <= 0.0)
        return out;
    // tracks: each peak continues the nearest track of the frame before (within half a semitone or 15 Hz), else takes a
    // free slot; the louder peaks choose first
    constexpr int K = maxSpectralPartials;
    std::vector<double> hz ((std::size_t) frames * K, 0.0), amp ((std::size_t) frames * K, 0.0);
    for (int f = 0; f < frames; ++f)
    {
        const auto& list = peaks[(std::size_t) f];
        const auto frameTop = list.empty() ? 0.0 : list.front().amp;
        std::array<bool, K> taken {};
        for (const auto& pk : list)
        {
            if (pk.amp < loudest * 1.0e-4 || pk.amp < frameTop * 3.16e-4) // (−80 dB of the loudest, −70 dB of the frame's)
                continue;
            int best = -1;
            double bestDist = 1.0e30;
            if (f > 0)
                for (int s = 0; s < K; ++s)
                {
                    const auto ph = hz[(std::size_t) ((f - 1) * K + s)], pa = amp[(std::size_t) ((f - 1) * K + s)];
                    const auto d = std::abs (pk.hz - ph);
                    if (! taken[(std::size_t) s] && pa > 0.0 && d < std::max (15.0, 0.03 * ph) && d < bestDist)
                        best = s, bestDist = d;
                }
            if (best < 0)
                for (int s = 0; s < K; ++s)
                    if (! taken[(std::size_t) s] && (f == 0 || amp[(std::size_t) ((f - 1) * K + s)] <= 0.0))
                    {
                        best = s;
                        break;
                    }
            if (best < 0)
                continue;
            taken[(std::size_t) best] = true;
            hz[(std::size_t) (f * K + best)] = pk.hz;
            amp[(std::size_t) (f * K + best)] = pk.amp;
        }
        for (int s = 0; s < K; ++s) // (a silent slot holds its last frequency)
            if (! taken[(std::size_t) s] && f > 0)
                hz[(std::size_t) (f * K + s)] = hz[(std::size_t) ((f - 1) * K + s)];
    }
    // the slots in order of their energy (the loudest tracks first: Partials keeps the strongest); silent ones dropped
    std::array<double, K> energy {};
    for (int f = 0; f < frames; ++f)
        for (int s = 0; s < K; ++s)
            energy[(std::size_t) s] += amp[(std::size_t) (f * K + s)] * amp[(std::size_t) (f * K + s)];
    std::array<int, K> order {};
    std::iota (order.begin(), order.end(), 0);
    std::stable_sort (order.begin(), order.end(), [&energy] (int u, int v) { return energy[(std::size_t) u] > energy[(std::size_t) v]; });
    int used = 0;
    while (used < K && energy[(std::size_t) order[(std::size_t) used]] > 0.0)
        ++used;
    if (used == 0)
        return out;
    out.frames = frames;
    out.partials = used;
    out.hz.resize ((std::size_t) frames * (std::size_t) used);
    out.amp.resize (out.hz.size());
    for (int f = 0; f < frames; ++f)
        for (int s = 0; s < used; ++s)
        {
            const auto src = (std::size_t) (f * K + order[(std::size_t) s]);
            // (a slot silent before its first peak takes that peak's frequency)
            auto h = hz[src];
            for (int g = f; h <= 0.0 && g < frames; ++g)
                h = hz[(std::size_t) (g * K + order[(std::size_t) s])];
            out.hz[(std::size_t) (f * used + s)] = gridHz (h > 0.0 ? h : 440.0);
            out.amp[(std::size_t) (f * used + s)] = gridAmp (amp[src]);
        }
    return out;
}

namespace
{
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1.0e-16)
            break;
    }
    return sum;
}
// a linear-phase low-pass (Kaiser window, β 7.86: ~80 dB), `taps` odd, cut at `cutoff` cycles a sample, unity gain at DC
std::vector<float> kaiserLowpass (int taps, double cutoff)
{
    constexpr double beta = 7.86;
    std::vector<double> h ((std::size_t) taps);
    const auto m = (double) (taps - 1);
    double sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        const auto x = (double) i - 0.5 * m;
        const auto sinc = x == 0.0 ? 2.0 * cutoff : std::sin (2.0 * pi * cutoff * x) / (pi * x);
        const auto r = 2.0 * x / m;
        h[(std::size_t) i] = sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
        sum += h[(std::size_t) i];
    }
    std::vector<float> out ((std::size_t) taps);
    for (int i = 0; i < taps; ++i)
        out[(std::size_t) i] = (float) (h[(std::size_t) i] / sum);
    return out;
}
// y[j] = Σ h[i] x[j × step + i − (taps − 1) / 2] (x 0 outside), for j < outFrames, into a level's layout (SampleLevels::guard
// zeros before, 2 × guard after); x is a level's layout too when `guarded` (its samples from x[guard]). Four running sums
// (SIMD), added in the same order every time.
std::vector<float> filtered (const float* x, int frames, const std::vector<float>& h, int step, int outFrames)
{
    constexpr int g = SampleLevels::guard;
    std::vector<float> y ((std::size_t) (outFrames + 3 * g), 0.0f);
    const auto taps = (int) h.size(), half = (taps - 1) / 2;
    std::vector<float> padded ((std::size_t) (frames + 2 * taps + 8), 0.0f); // (x with zeros around: no edge cases below)
    std::copy (x, x + frames, padded.begin() + taps);
    std::vector<float> hp ((std::size_t) ((taps + 3) & ~3), 0.0f);
    std::copy (h.begin(), h.end(), hp.begin());
    const auto tq = (int) hp.size();
    for (int j = 0; j < outFrames; ++j)
    {
        const float* xs = padded.data() + taps + j * step - half;
        auto acc = _mm_setzero_ps();
        for (int i = 0; i < tq; i += 4)
            acc = _mm_add_ps (acc, _mm_mul_ps (_mm_loadu_ps (hp.data() + i), _mm_loadu_ps (xs + i)));
        alignas (16) float v[4];
        _mm_store_ps (v, acc);
        y[(std::size_t) (g + j)] = (v[0] + v[1]) + (v[2] + v[3]);
    }
    return y;
}
std::mutex levelsLock;
struct LevelsEntry
{
    std::uint64_t hash = 0;
    int frames = 0;
    bool stereo = false;
    std::weak_ptr<const SampleLevels> levels;
};
std::vector<LevelsEntry>& levelsBuilt()
{
    static std::vector<LevelsEntry> list;
    return list;
}
} // namespace

std::shared_ptr<const SampleLevels> sampleLevels (const SampleBuffer& smp)
{
    const auto n = smp.frames();
    if (n < 8)
        return nullptr;
    const auto stereo = smp.r.size() == smp.l.size();
    std::uint64_t h = 1469598103934665603ull ^ (std::uint64_t) n;
    for (const auto* v : { &smp.l, &smp.r })
        for (const auto f : *v)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &f, sizeof bits);
            h = (h ^ bits) * 1099511628211ull;
        }
    {
        std::lock_guard<std::mutex> lock (levelsLock);
        auto& list = levelsBuilt();
        list.erase (std::remove_if (list.begin(), list.end(), [] (const LevelsEntry& e) { return e.levels.expired(); }), list.end());
        for (const auto& e : list)
            if (e.hash == h && e.frames == n && e.stereo == stereo)
                if (auto l = e.levels.lock())
                    return l;
    }
    // half an octave apart, kept at twice the rate their band needs (so the 8-point read below rejects its images): level 1
    // the sample below 0.354 of its rate; level 2m (m ≥ 1) below 0.5 × 2^−m, level 2m + 1 below 0.354 × 2^−m, both at every
    // 2^(m − 1)-th sample (level 2 and 3 every sample). Each from the even level before it: pass bands 0.8 of the stop edges,
    // ~80 dB from the stop edges (Kaiser).
    static const auto kernel1 = kaiserLowpass (69, 0.3168);   // stop 0.354, the sample's rate
    static const auto kernel2 = kaiserLowpass (101, 0.225);   // stop 0.25 (level 2 from the sample)
    static const auto kernelOdd = kaiserLowpass (137, 0.1591); // stop 0.177 of an even level's rate (levels 3, 5, 7 …)
    static const auto kernelEven = kaiserLowpass (201, 0.1125); // stop 0.125 of an even level's rate, every other sample kept
    auto levels = std::make_shared<SampleLevels>();
    levels->levels.reserve (24); // (the next level reads the one before: no reallocation)
    const auto add = [&levels] (const float* l, const float* r, int frames, const std::vector<float>& h, int step, int shift) {
        SampleLevels::Level lev;
        lev.shift = shift;
        lev.frames = (frames + step - 1) / step;
        lev.l = filtered (l, frames, h, step, lev.frames);
        if (r != nullptr)
            lev.r = filtered (r, frames, h, step, lev.frames);
        levels->levels.push_back (std::move (lev));
        return &levels->levels.back();
    };
    const float* sr = stereo ? smp.r.data() : nullptr;
    (void) add (smp.l.data(), sr, n, kernel1, 1, 0);
    const auto* even = add (smp.l.data(), sr, n, kernel2, 1, 0);
    constexpr int g = SampleLevels::guard;
    for (int k = 3; k <= 24 && even->frames >= 32; k += 2)
    {
        const auto* el = even->l.data() + g;
        const auto* er = even->r.empty() ? nullptr : even->r.data() + g;
        (void) add (el, er, even->frames, kernelOdd, 1, even->shift);
        if (k + 1 <= 24)
            even = add (el, er, even->frames, kernelEven, 2, even->shift + 1);
    }
    std::shared_ptr<const SampleLevels> built = std::move (levels);
    std::lock_guard<std::mutex> lock (levelsLock);
    levelsBuilt().push_back ({ h, n, stereo, built });
    return built;
}
} // namespace lynx::wt
