#include "core/HarmorResynth.h"
#include "core/FlResampler.h"
#include "core/FlVoiceProcessor.h"
#include "core/OggVorbis.h"
#include "core/WavPack.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

namespace lynx::harmor
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr int order = 11;              // image +0xa4: 2^11-point frames
constexpr double hopMs = 5.805;        // image +0x128 (0x4017384ef2a605ce)
constexpr float onsetRise = 2.0f;      // image +0x120

// FUN_00794260: the two analysis settings (decimation +0xa8, windows +0xac/+0xb0 with their parameters +0xb4/+0xb8,
// gains +0xbc/+0xc0)
struct Setting
{
    int decimate = 2;
    int win[2] { 6, 6 };       // 6 Kaiser, 3 Hamming
    float par[2] { 36.0f, 21.6f };
    float gain[2] {};
};
Setting settingFor (int mode)
{
    Setting s;
    if (mode == 1)
    {
        s.decimate = 1;
        s.win[1] = 3;
        s.par[0] = 21.6f, s.par[1] = 0.0f;
        s.gain[0] = 3.47835588f;
        s.gain[1] = (float) (((double) s.gain[0] * 0.0670460760593414) / 0.0427858605980873);
    }
    else
    {
        s.gain[0] = 5.44995737f;
        s.gain[1] = (float) (((double) s.gain[0] * 0.0675629377365112) / 0.0560011900961399);
    }
    return s;
}
constexpr float tinyF = 5.960464477539063e-08f;

// modified Bessel function of the first kind, order 0
double besselI0 (double x) noexcept
{
    double s = 1.0, t = 1.0;
    const auto q = x * x * 0.25;
    for (int k = 1; k < 500; ++k)
    {
        t *= q / ((double) k * (double) k);
        s += t;
        if (t < s * 1e-17)
            break;
    }
    return s;
}

// FUN_0062cb40: type 6 = ippsWinKaiser_32f_I on a buffer of ones, alpha = 2·par/N; type 3 = Hamming
std::vector<float> window (int N, int type, double par)
{
    std::vector<float> w ((std::size_t) N);
    if (type == 3)
    {
        const auto d = 6.283185307179586 / (double) (N - 1);
        for (int i = 0; i < N; ++i)
            w[(std::size_t) i] = (float) (0.53836 - std::cos (d * (double) i) * 0.46164);
        return w;
    }
    const auto alpha = (float) (par * 2.0 / N);
    const auto h = (N - 1) * 0.5;
    const auto den = besselI0 ((double) alpha * h);
    for (int i = 0; i < N; ++i)
    {
        const auto d = i - h;
        const auto r = std::sqrt (std::max (0.0, h * h - d * d));
        w[(std::size_t) i] = (float) (besselI0 ((double) alpha * r) / den);
    }
    return w;
}

void fft (std::vector<std::complex<double>>& a)
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
        const auto ang = -2.0 * pi / (double) len;
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

// resampling to `to` Hz with FL's resampler at Harmor's quality index 6 (TResampler, FUN_00620510: a 64-point
// windowed sinc, widened and lowered when shortening; core/FlResampler.h), the read position advancing by the speed
std::vector<float> resample (const float* x, std::size_t n, double from, double to)
{
    const auto speed = from / to;
    const auto outN = (std::size_t) std::llround ((double) n * to / from);
    std::vector<float> out (outN, 0.0f);
    flrs::Kernel k;
    k.setQuality (6);
    k.setSpeed (speed);
    const int N = k.halfWidth;
    std::vector<float> taps ((std::size_t) (2 * N), 0.0f);
    flvp::X87Extended x87;
    double pos = 0.0;
    for (std::size_t j = 0; j < outN; ++j)
    {
        const auto i = flrs::wholeFrames (pos);
        const auto frac = pos - (double) i;
        for (int t = 0; t < 2 * N; ++t)
        {
            const auto idx = i - N + 1 + t;
            taps[(std::size_t) t] = idx >= 0 && idx < (long long) n ? x[idx] : 0.0f;
        }
        float l = 0.0f, r = 0.0f;
        flrs::interpolate (k, taps.data(), taps.data(), frac, l, r);
        out[j] = l;
        pos += speed;
    }
    return out;
}

inline float levelOf (float pix) noexcept
{
    // FUN_007981f0: (256^(pix/255) − 1) / 255
    const auto k = (float) (std::log (256.0) / 255.0);
    return (float) std::expm1 ((double) (pix * k)) * 0.003921568859368563f;
}

inline double rndHalfEven (double x) noexcept { return std::nearbyint (x); }

std::mutex loaderMutex;
SampleLoader& loaderRef()
{
    static SampleLoader l;
    return l;
}
std::map<std::string, std::weak_ptr<const ResynthPlanes>>& cacheRef()
{
    static std::map<std::string, std::weak_ptr<const ResynthPlanes>> c;
    return c;
}
std::vector<std::shared_ptr<const ResynthPlanes>>& keepRef()
{
    static std::vector<std::shared_ptr<const ResynthPlanes>> k; // the last few analyses stay alive
    return k;
}
} // namespace

std::shared_ptr<const ResynthPlanes> analyseResynthesis (const float* mono, std::size_t frames, double rate,
                                                         const ResynthOptions& options)
{
    if (mono == nullptr || frames == 0 || rate <= 0.0)
        return nullptr;
    const auto S = settingFor (options.mode);
    const int decimate = S.decimate;
    const auto softKnee = (float) (((double) options.soft / 250.0) * ((double) options.soft / 250.0));
    auto P = std::make_shared<ResynthPlanes>();
    const int N = 1 << order;
    const auto C0 = 440.0 * std::exp2 (-57.0 / 12.0);
    const double R = N * C0;
    const auto y = resample (mono, frames, rate, R);
    const auto hop = (int) std::llround (hopMs * R * (double) 0.0010000000474974513f);
    int rows = (N >> 1) - 1;
    const auto cols = (int) std::ceil (((double) y.size() - 1.0 + hop) * (1.0 / hop));
    if (cols <= 0)
        return nullptr;
    P->rate = R, P->hop = hop, P->durMs = (double) frames * 1000.0 / rate;
    P->msCol = (double) (hop * 1000) / R;
    // analysis planes at full resolution (rows = 1023): magnitudes and phases (cycles · ½ around ±¼)
    std::vector<float> g0 ((std::size_t) cols * (std::size_t) rows), p2 ((std::size_t) cols * (std::size_t) rows);
    std::vector<float> adv ((std::size_t) rows);
    for (int i = 0; i < rows; ++i)
        adv[(std::size_t) i] = (float) ((double) hop * (i + 1) / N);
    std::vector<std::complex<double>> X ((std::size_t) N);
    for (int pass = 0; pass < 2; ++pass)
    {
        auto w = window (N, S.win[pass], (double) S.par[pass]);
        const auto mn = *std::min_element (w.begin(), w.end());
        double sum = 0.0;
        for (auto& v : w)
            v = v - mn, sum += (double) v;
        const auto g = S.gain[pass] / (float) sum;
        const int nb = pass == 0 ? rows : rows >> 4;
        for (int d = 0; d < cols; ++d)
        {
            const long long s = (long long) d * hop - (N >> 1);
            for (int k = 0; k < N; ++k)
            {
                const auto idx = s + k;
                const auto v = idx >= 0 && idx < (long long) y.size() ? y[(std::size_t) idx] : 0.0f;
                X[(std::size_t) k] = std::complex<double> ((double) (v * w[(std::size_t) k]), 0.0);
            }
            fft (X);
            float* gc = g0.data() + (std::size_t) d * (std::size_t) rows;
            float* pc = p2.data() + (std::size_t) d * (std::size_t) rows;
            const float* pp = d > 0 ? pc - rows : nullptr;
            for (int i = 0; i < nb; ++i)
            {
                const auto& c = X[(std::size_t) i + 1];
                gc[i] = (float) std::abs (c) * g;
                double v;
                if (d < 1 || gc[i] != 0.0f)
                    v = std::atan2 (c.imag(), c.real()) * 0.15915494309189535 + 0.25;
                else
                    v = (double) (pp[i] * 2.0f + adv[(std::size_t) i]);
                v = v - rndHalfEven (v);
                pc[i] = (float) (v * 0.5);
            }
        }
    }
    // FUN_00794370: every 4th bin (harmonics of C2)
    const int k = 1 << decimate;
    const int nr = ((rows + 1) >> decimate) - 1;
    const int stride = (nr + 7) & ~7;
    P->rows = nr, P->cols = cols, P->stride = stride;
    P->gain.assign ((std::size_t) cols * (std::size_t) stride, 0.0f);
    P->phase.assign ((std::size_t) cols * (std::size_t) stride, 0.0f);
    for (int d = 0; d < cols; ++d)
        for (int i = 0; i < nr; ++i)
        {
            const auto src = (std::size_t) d * (std::size_t) rows + (std::size_t) ((i + 1) * k - 1);
            P->gain[(std::size_t) d * (std::size_t) stride + (std::size_t) i] = g0[src];
            P->phase[(std::size_t) d * (std::size_t) stride + (std::size_t) i] = p2[src];
        }
    rows = nr;
    {
        g0.clear(), g0.shrink_to_fit(), p2.clear(), p2.shrink_to_fit();
    }
    // +0x148: pitch of harmonic 1 (C2) against C5; +0x168: expected phase advance per column
    {
        const auto f1 = R / (double) (1 << (order - decimate)); // Hz of harmonic 1
        P->k148 = 1200.0 * std::log2 (f1 / (440.0 * std::exp2 (3.0 / 12.0)));
    }
    std::vector<float> adv2 ((std::size_t) stride, 0.0f);
    {
        const auto step = (double) hop * 0.5 / (double) (rows + 1);
        for (int i = 0; i < stride; ++i)
            adv2[(std::size_t) i] = (float) (step + (double) (float) step * i);
    }
    // weighting by the harmonic number, the soft knee, pixels
    auto& G = P->gain;
    for (int d = 0; d < cols; ++d)
        for (int i = 0; i < stride; ++i)
            G[(std::size_t) d * (std::size_t) stride + (std::size_t) i] *= (float) (1 + i);
    const auto mx = *std::max_element (G.begin(), G.end());
    const auto f16 = mx > 0.0f ? 1.0f / mx : 1.0f;
    if (softKnee != 0.0f)
    {
        const auto pk = f16 * 160.0f / softKnee;
        for (auto& v : G)
        {
            auto u = std::min (v * pk, 1.0f);
            u = 1.0f - u;
            v = v - u * u * v;
        }
    }
    for (auto& v : G)
        if (v < tinyF)
            v = 0.0f;
    {
        const auto sc = f16 * 255.0f;
        const auto kl = (float) (1.0 / std::log (256.0));
        for (auto& v : G)
        {
            v = v * sc + 1.0f;
            v = (float) std::log ((double) v) * kl * 255.0f;
        }
    }
    P->k38 = mx;
    P->adv = adv2;
    // FUN_00795720, resynthesis branch: peaks, regions, onsets, returns, the frequency plane
    auto& Ph = P->phase;
    P->freqRaw.assign ((std::size_t) cols * (std::size_t) stride, 0.0f);
    const auto k40 = (float) ((rows + 1) / 512.0);
    const int klen = (int) std::llround ((double) (k40 * 4.0f)) + 1;
    const float d8[2] { onsetRise, std::max (onsetRise, 8.0f) };
    const int f0 = (int) std::llround ((double) (k40 * 16.0f));
    std::vector<long long> q ((std::size_t) rows, -0x7fffffffLL);
    std::vector<float> lv ((std::size_t) rows), sm[2] { std::vector<float> ((std::size_t) rows), std::vector<float> ((std::size_t) rows) };
    std::vector<double> v ((std::size_t) rows);
    int sel = 0, cool = 0;
    const auto codeOf = [] (double x) { return (long long) rndHalfEven (x); };
    for (int c = 0; c < cols; ++c)
    {
        float* gc = G.data() + (std::size_t) c * (std::size_t) stride;
        float* pc = Ph.data() + (std::size_t) c * (std::size_t) stride;
        for (int i = 0; i < rows; ++i)
            lv[(std::size_t) i] = levelOf (gc[i]);
        for (int i = 0; i < rows; ++i)
        {
            const auto x = (double) pc[i];
            v[(std::size_t) i] = (double) (float) (x - rndHalfEven (x));
        }
        // peaks: every second bin, against its neighbours
        int cnt = 0;
        for (int i = 0; i < rows;)
        {
            int st = i - (klen >> 1), ln = klen;
            if (st < 0)
                ln += st, st = 0;
            if (st + ln > rows)
                ln = rows - st;
            float m = lv[(std::size_t) st];
            for (int j = st + 1; j < st + ln; ++j)
                m = std::max (m, lv[(std::size_t) j]);
            if (! (lv[(std::size_t) i] < m))
                v[(std::size_t) i] = (double) (float) (v[(std::size_t) i] + 524.0), ++cnt;
            i = st + ln;
        }
        if (cnt == 0)
            for (auto& x : v)
                x = (double) ((float) x + 524.0f);
        // regions: each peak takes the bins down to the previous region and up to the lowest bin before the next peak
        for (int i = 0; i < rows; ++i)
        {
            if (codeOf (v[(std::size_t) i]) != 524)
                continue;
            float best = 4096.0f;
            int j = i - 1, e = i;
            while (j >= 0 && codeOf (v[(std::size_t) j]) < 1)
            {
                if (gc[j] < best)
                    e = j, best = gc[j];
                --j;
                if (j < 0 || codeOf (v[(std::size_t) j]) > 0)
                    e = j + 1;
            }
            for (int m = e; m < i; ++m)
                v[(std::size_t) m] = (double) (float) ((double) (i + 2) + v[(std::size_t) m] - rndHalfEven (v[(std::size_t) m]));
            best = 4096.0f;
            j = i + 1, e = i;
            while (j < rows && codeOf (v[(std::size_t) j]) < 1)
            {
                if (gc[j] < best)
                    best = gc[j], e = j;
                ++j;
                if (j >= rows)
                    e = rows - 1;
            }
            for (int m = i + 1; m <= e; ++m)
                v[(std::size_t) m] = (double) (float) ((double) (i + 2) + v[(std::size_t) m] - rndHalfEven (v[(std::size_t) m]));
        }
        // onsets (522) and returns from silence (523 on the last quiet column)
        {
            auto& s = sm[sel];
            const int h = klen >> 1;
            for (int i = 0; i < rows; ++i)
            {
                float a = 0.0f;
                for (int t = -h; t <= h; ++t)
                    a += lv[(std::size_t) std::min (std::max (i + t, 0), rows - 1)];
                s[(std::size_t) i] = a * (1.0f / (float) klen);
            }
            if (c < 1)
                for (auto& x : v)
                    x = (double) (float) (x - rndHalfEven (x) + 522.0);
            else
            {
                const auto& pv = sm[sel ^ 1];
                int ons = 0;
                for (int i = 0; i < rows; ++i)
                {
                    if (pv[(std::size_t) i] * d8[i < f0 ? 1 : 0] < s[(std::size_t) i] && (double) s[(std::size_t) i] >= 0.0025)
                        v[(std::size_t) i] = (double) (float) (v[(std::size_t) i] - rndHalfEven (v[(std::size_t) i]) + 522.0), ++ons;
                    if ((double) s[(std::size_t) i] >= 0.001)
                    {
                        if (q[(std::size_t) i] >= 0)
                        {
                            auto& z = Ph[(std::size_t) q[(std::size_t) i] * (std::size_t) stride + (std::size_t) i];
                            z = (float) ((double) z - rndHalfEven ((double) z) + 523.0);
                            q[(std::size_t) i] = -0x7fffffffLL;
                        }
                    }
                    else if (q[(std::size_t) i] == -0x7fffffffLL)
                        q[(std::size_t) i] = -c;
                    else
                        q[(std::size_t) i] = c;
                }
                if (ons >= (rows >> 1) && cool < 1)
                {
                    const float* gp = gc - stride;
                    for (int i = 0; i < stride; ++i)
                        gc[i] = gc[i] * 0.25f + gp[i] * 0.75f;
                    cool = 9;
                }
                --cool;
            }
            sel ^= 1;
        }
        for (int i = 0; i < rows; ++i)
            pc[i] = (float) v[(std::size_t) i];
        // FUN_007973f0: the frequency deviation from the phase step
        const float* pp = c > 0 ? pc - stride : pc;
        float* fc = P->freqRaw.data() + (std::size_t) c * (std::size_t) stride;
        for (int i = 0; i < rows; ++i)
        {
            const auto d = ((double) pc[i] - (double) pp[i]) * 2.0 - (double) adv2[(std::size_t) i];
            fc[i] = (float) ((d - rndHalfEven (d)) * (double) (rows + 1) * (1.0 / hop) * 2.0);
        }
    }
    float m = 0.015625f;
    for (auto x : P->freqRaw)
        m = std::max (m, std::fabs (x));
    P->k104 = m;
    P->freq = P->freqRaw;
    const auto inv = 1.0f / m;
    for (auto& x : P->freq)
        x = x * inv;
    return P;
}

void setSampleLoader (SampleLoader loader)
{
    const std::lock_guard<std::mutex> lock (loaderMutex);
    loaderRef() = std::move (loader);
    cacheRef().clear();
}

std::shared_ptr<const ResynthPlanes> resynthesisFor (const std::string& savedPath, const ResynthOptions& options)
{
    if (savedPath.empty())
        return nullptr;
    const auto key = std::to_string (options.mode) + "|" + std::to_string (options.soft) + "|" + savedPath;
    SampleLoader loader;
    {
        const std::lock_guard<std::mutex> lock (loaderMutex);
        if (auto it = cacheRef().find (key); it != cacheRef().end())
            if (auto p = it->second.lock())
                return p;
        loader = loaderRef();
    }
    SampleData s;
    const bool ok = loader ? loader (savedPath, s) : readWavMono (savedPath, s);
    if (! ok || s.mono.empty() || s.rate <= 0.0)
        return nullptr;
    auto P = analyseResynthesis (s.mono.data(), s.mono.size(), s.rate, options);
    if (P == nullptr)
        return nullptr;
    const std::lock_guard<std::mutex> lock (loaderMutex);
    cacheRef()[key] = P;
    auto& keep = keepRef();
    keep.push_back (P);
    if (keep.size() > 8)
        keep.erase (keep.begin());
    return P;
}

bool readWavMono (const std::vector<std::uint8_t>& b, SampleData& out)
{
    // FL's Ogg-in-WAV and plain Ogg Vorbis (FL's own decoding, core/OggVorbis.h), WavPack (core/WavPack.h)
    if (oggvorbis::isOggWav (b.data(), b.size()) || oggvorbis::isOggStream (b.data(), b.size()))
    {
        oggvorbis::Decoded d;
        if (! oggvorbis::decode (b.data(), b.size(), d) || d.channels < 1 || d.data.empty())
            return false;
        out.rate = (double) d.sampleRate;
        out.mono.assign (d.data[0].size(), 0.0f);
        for (std::size_t i = 0; i < out.mono.size(); ++i)
        {
            float a = 0.0f;
            for (const auto& ch : d.data)
                a += ch[i];
            out.mono[i] = a / (float) d.data.size();
        }
        return true;
    }
    if (b.size() >= 4 && std::memcmp (b.data(), "wvpk", 4) == 0)
    {
        DecodedAudio d;
        if (! decodeWavPack (b.data(), b.size(), d) || d.channels.empty())
            return false;
        out.rate = d.sampleRate;
        out.mono.assign (d.channels[0].size(), 0.0f);
        for (std::size_t i = 0; i < out.mono.size(); ++i)
        {
            float a = 0.0f;
            for (const auto& ch : d.channels)
                a += ch[i];
            out.mono[i] = a / (float) d.channels.size();
        }
        return true;
    }
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
        return false;
    const auto u16 = [&b] (std::size_t o) { return (unsigned) b[o] | ((unsigned) b[o + 1] << 8); };
    const auto u32 = [&b] (std::size_t o) { return (std::uint32_t) b[o] | ((std::uint32_t) b[o + 1] << 8) | ((std::uint32_t) b[o + 2] << 16) | ((std::uint32_t) b[o + 3] << 24); };
    int fmt = 0, ch = 0, bits = 0;
    double rate = 0.0;
    std::size_t data = 0, dataLen = 0;
    for (std::size_t o = 12; o + 8 <= b.size();)
    {
        const auto len = (std::size_t) u32 (o + 4);
        if (std::memcmp (b.data() + o, "fmt ", 4) == 0 && o + 8 + 16 <= b.size())
        {
            fmt = (int) u16 (o + 8), ch = (int) u16 (o + 10), rate = (double) u32 (o + 12), bits = (int) u16 (o + 22);
            if (fmt == 0xfffe && len >= 40 && o + 8 + 26 <= b.size())
                fmt = (int) u16 (o + 8 + 24);
        }
        else if (std::memcmp (b.data() + o, "data", 4) == 0)
        {
            data = o + 8, dataLen = std::min (len, b.size() - data);
            break;
        }
        o += 8 + len + (len & 1);
    }
    if (data == 0 || ch < 1 || rate <= 0.0 || (fmt != 1 && fmt != 3))
        return false;
    const int bps = bits / 8;
    if (bps < 1 || bps > 4 || (fmt == 3 && bits != 32))
        return false;
    const auto frames = dataLen / (std::size_t) (bps * ch);
    out.rate = rate;
    out.mono.assign (frames, 0.0f);
    for (std::size_t f = 0; f < frames; ++f)
    {
        float acc = 0.0f;
        for (int c = 0; c < ch; ++c)
        {
            const auto o = data + (f * (std::size_t) ch + (std::size_t) c) * (std::size_t) bps;
            float x = 0.0f;
            if (fmt == 3)
            {
                const auto u = u32 (o);
                std::memcpy (&x, &u, 4);
            }
            else if (bps == 1)
                x = ((float) b[o] - 128.0f) / 128.0f;
            else if (bps == 2)
                x = (float) (std::int16_t) u16 (o) / 32768.0f;
            else if (bps == 3)
                x = (float) ((std::int32_t) (((std::uint32_t) b[o] << 8) | ((std::uint32_t) b[o + 1] << 16) | ((std::uint32_t) b[o + 2] << 24)) >> 8) / 8388608.0f;
            else
                x = (float) ((double) (std::int32_t) u32 (o) / 2147483648.0);
            acc += x;
        }
        out.mono[f] = acc / (float) ch;
    }
    return true;
}

bool readWavMono (const std::string& path, SampleData& out)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    const std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    return readWavMono (b, out);
}
} // namespace lynx::harmor
