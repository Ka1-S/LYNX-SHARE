#include "core/LuxeVerb.h"
#include "core/SpaceVerb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "core/Simd.h"

#include "core/IldspMath.h"

// FL Studio's LuxeVerb (REA_rodada60). The engine reproduces the operation order of FL's shared shimmer-reverb
// engine in single precision, under the rounding the engine itself selects (toward zero, flush-to-zero and
// denormals-are-zero while it renders); parameter set-up runs in the caller's mode with FTZ/DAZ like FL's audio
// thread. Field comments name the role of each value; §-references are to docs/analise/REA_rodada60.md.

#pragma clang fp contract(off)
#pragma STDC FENV_ACCESS ON

namespace lynx
{
using namespace ildsp;

namespace
{
inline float hx (std::uint32_t u) noexcept { return ffrom (u); }
inline int truncF (float v) noexcept { return _mm_cvttss_si32 (_mm_set_ss (v)); }
inline int cvtF (float v) noexcept { return _mm_cvtss_si32 (_mm_set_ss (v)); } // current rounding mode

class CsrScope
{
public:
    explicit CsrScope (unsigned set, unsigned clear = 0) noexcept : saved (_mm_getcsr()) { _mm_setcsr ((saved & ~clear) | set); }
    ~CsrScope() { _mm_setcsr (saved); }
    CsrScope (const CsrScope&) = delete;
    CsrScope& operator= (const CsrScope&) = delete;

private:
    unsigned saved;
};

// primes below 20000 (the size control picks line lengths among them, §2.3)
const std::vector<int>& primes()
{
    static const std::vector<int> p = [] {
        std::vector<int> r;
        std::vector<char> s (20000, 1);
        for (int i = 2; i < 20000; ++i)
            if (s[(size_t) i])
            {
                r.push_back (i);
                for (int j = i * 2; j < 20000; j += i) s[(size_t) j] = 0;
            }
        return r;
    }();
    return p;
}

// per-line length factor at the default size (× rate) and the base numbers the size control scales (§2.3)
const std::uint32_t lineFactor[16] = { 0x3ca2e944, 0x3cf20b78, 0x3d311f2f, 0x3d3eaea9, 0x3d7bcc14, 0x3d76f7aa, 0x3da3cb27, 0x3db09c69,
                                       0x3dcc323f, 0x3dcb14ec, 0x3de633df, 0x3dfd0d12, 0x3e054e46, 0x3e105bc4, 0x3e1cfd78, 0x3e227853 };
const float lineBase[16] = { 155, 217, 296, 313, 398, 388, 494, 531, 601, 599, 664, 722, 755, 809, 870, 900 };

constexpr int predelaySize = 0x29fe0;   // predelay line (samples)
constexpr int diffuserSize = 4096;      // each diffuser all-pass
constexpr float c03333 = 0.333332986f;  // the shifter's cubic interpolation weights
constexpr float c01667 = 0.166666999f;

// ------------------------------------------------------------------------------------------------- pitch shifter
// Granular pitch shifter with its own delay line and filter (§2.6): grains of `grainLen` samples read a circular
// recording at `ratio` speed under a sin² window; a new grain starts every grainLen / density samples (± jitter).
struct Shifter
{
    float fs = 44100.0f;
    std::vector<float> recL, recR;  // grain recording (2-tap averaged input)
    int recSize = 0, recWrite = 0;
    float semitones = 0, density = 2.5f, jitterTime = 0, jitterPos = 0, mix = 0.5f;
    int grainLen = 0, countdown = 0;
    float ratio = 1.0f, jitterScale = 0;
    std::array<int, 20> active {}, length {}, remain {};
    std::array<float, 20> pos {}, rate {};
    std::vector<float> window;
    float lp = 0, prevL = 0, prevR = 0;
    float delayCur = 0, delayCoef = 0, delayTarget = 0, delayFrac = 0, fbGain = 0.5f, tempo = 120.0f;
    float delayMax = 0, delayMin = 0;
    int delaySize = 0, delayWrite = 0, mode = 1;
    std::vector<float> delayL, delayR;
    float clipCeil = 1.0f, clipK = 0.2f;
    bool clipGrains = true;
    float st[12] {};   // filter states: L s1/s2 of the three stages, then R
    float co[15] {};   // three biquads (b0 b1 b2 a1 a2) of the feedback filter
    float fbL = 0, fbR = 0;

    void zeroStates() noexcept
    {
        std::fill (std::begin (st), std::end (st), 0.0f);
    }

    void init (float rate_)
    {
        fs = rate_;
        recSize = truncF (fs * 0.800000012f);
        recL.assign ((size_t) recSize + 4, 0.0f);
        recR.assign ((size_t) recSize + 4, 0.0f);
        recWrite = 0;
        semitones = 0;
        prevR = 0; prevL = 0;
        jitterScale = fs * hx (0x3d39c277);
        density = 2.5f; jitterTime = 0; jitterPos = 0; mix = 0.5f; ratio = 1.0f;
        grainLen = truncF (fs * hx (0xc3d2dcfe));
        countdown = (int) std::round ((double) ((float) grainLen * hx (0x3ecccccd)));
        delayMin = fs * hx (0x3a83126f);
        delaySize = truncF (fs * hx (0x408051ec));
        delayMax = (float) (delaySize - 1);
        delayL.assign ((size_t) delaySize + 4, 0.0f);
        delayR.assign ((size_t) delaySize + 4, 0.0f);
        delayWrite = 0;
        fbGain = 0.5f; mode = 1;
        delayTarget = fs * hx (0x3dcccccd);
        delayCur = delayTarget;
        tempo = 120.0f; clipCeil = 1.0f; clipK = hx (0x3e4ccccd); clipGrains = true;
        delayCoef = hx (0x408d1eb8) / fs;
        zeroStates();
        const int r = (int) std::round ((double) fs);
        auto set = [this] (std::initializer_list<std::pair<int, std::uint32_t>> l) {
            for (auto& e : l) co[e.first] = hx (e.second);
        };
        set ({ { 5, 0x3f800000 }, { 10, 0x3f800000 }, { 12, 0 }, { 14, 0 } });
        if (r == 22050)
            set ({ { 0, 0x3f7476de }, { 1, 0xbff44ac0 }, { 2, 0x3f741eae }, { 3, 0xbffa216d }, { 4, 0x3f7476de }, { 6, 0xc00008d5 },
                   { 7, 0x3f8011b1 }, { 8, 0xbffda9aa }, { 9, 0x3f7b8813 }, { 11, 0xbf801c88 }, { 13, 0xbf78cea6 } });
        else
        {
            set ({ { 6, 0xc000193e }, { 11, 0xbf7fc18e }, { 7, 0x3f803284 } });
            if (r == 48000)
                set ({ { 0, 0x3f7aa288 }, { 1, 0xbffa8fac }, { 2, 0x3f7a7cdf }, { 3, 0xbffd4bb6 }, { 4, 0x3f7aa289 }, { 8, 0xbffef264 }, { 9, 0x3f7deff6 }, { 13, 0xbf7cabb2 } });
            else if (r == 88200)
                set ({ { 0, 0x3f7d10ef }, { 1, 0xbffcfde4 }, { 2, 0x3f7ceae8 }, { 3, 0xbffe86d5 }, { 4, 0x3f7d10f8 }, { 8, 0xbfff6e65 }, { 9, 0x3f7ee01b }, { 13, 0xbf7e2ebf } });
            else if (r == 96000)
                set ({ { 0, 0x3f7d4da2 }, { 1, 0xbffd3a93 }, { 2, 0x3f7d2793 }, { 3, 0xbffea56b }, { 4, 0x3f7d4da2 }, { 8, 0xbfff7a52 }, { 9, 0x3f7ef771 }, { 13, 0xbf7e5478 } });
            else if (r == 192000)
                set ({ { 0, 0x3f7ea5e7 }, { 1, 0xbffe92be }, { 2, 0x3f7e7fa4 }, { 3, 0xbfff504a }, { 4, 0x3f7ea148 }, { 8, 0xbfffbd17 }, { 9, 0x3f7f7ae3 }, { 13, 0xbf7f2f37 } });
            else // 44.1 kHz and every other rate
                set ({ { 0, 0x3f7a2a76 }, { 1, 0xbffa17a3 }, { 2, 0x3f7a04df }, { 3, 0xbffd0ea7 }, { 4, 0x3f7a2a75 }, { 8, 0xbffeda1d }, { 9, 0x3f7dc179 }, { 13, 0xbf7c60dc } });
        }
        active.fill (0);
        window.assign (0x400 + 4, 0.0f);
        for (int i = 0; i < 0x400; ++i)
        {
            const double a = (double) ((float) i * hx (0x40490fdb)) / (0x400 - 1.0);
            const double s = std::sin (a);
            window[(size_t) i] = (float) (s * s);
        }
        fbL = 0; fbR = 0;
    }

    void reset() noexcept
    {
        std::fill (recL.begin(), recL.end(), 0.0f);
        std::fill (recR.begin(), recR.end(), 0.0f);
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        zeroStates();
        fbL = 0; fbR = 0;
    }

    void set (int c, float v)
    {
        switch (c)
        {
            case 0: semitones = v; ratio = powf_ (2.0f, v * hx (0x3daaaaab)); break;
            case 1: grainLen = truncF ((v * hx (0x3a83126f)) * fs); break;
            case 2: density = v; break;
            case 3: jitterTime = v; break;
            case 4: jitterPos = v; break;
            case 5: mix = v; break;
            case 6: // delay in ms
            {
                const float x = (v * fs) * hx (0x3a83126f);
                delayTarget = x;
                delayTarget = fmaxf_ (delayMin, fminf_ (x, delayMax));
                break;
            }
            case 7: fbGain = v; break;
            case 8: mode = (int) std::round ((double) v); break;
            case 9: tempo = v; break;
            case 10: // delay in sixteenths of the tempo
            {
                const float x = ((v * fs) * 15.0f) / tempo;
                delayTarget = x;
                delayTarget = fminf_ (x + 1.0f, delayMax);
                break;
            }
            case 13: clipCeil = v; clipK = hx (0x3e4ccccd) / v; break;
            case 14: clipGrains = v >= 0.5f; break;
            case 15: // feed-forward pitch: semitones, fixed density/jitter, grain length from the interval
            {
                semitones = v;
                ratio = powf_ (2.0f, v * hx (0x3daaaaab));
                density = 2.25f; jitterTime = 0.0f;
                const float x = fs * hx (0x3e19999a);
                if (v == 0.0f) { jitterPos = 0.0f; grainLen = truncF (x); break; }
                jitterPos = hx (0x3e99999a);
                const int e = truncF (x);
                grainLen = e;
                float d;
                if (v > 12.0f) d = (v - 12.0f) * 6.25f;
                else if (! (v >= -12.0f)) d = v * 6.25f + 75.0f;
                else break;
                grainLen = truncF ((float) e - d);
                break;
            }
            default: break; // 11, 12 (grain shape, formant) are not used by LuxeVerb
        }
    }

    static float clip (float t) noexcept // soft clip of t = k·x: (2 − |t/2|)·t/2 inside ±2
    {
        if (t > 2.0f) return 1.0f;
        if (! (t >= -2.0f)) return -1.0f;
        const float h = t * 0.5f;
        return (2.0f - std::fabs (h)) * h;
    }

    void process (std::uint32_t& seed, int nch, const float* in, float* out) noexcept
    {
        const float in0 = in[0];
        const float in1 = nch > 1 ? in[1] : 0.0f;
        float x8 = in0, x7 = in1, dL = 0.0f, dR = 0.0f;
        if (mode != 1)
        {
            const float d = (delayTarget - delayCur) * delayCoef + delayCur;
            delayCur = d;
            const int e = truncF (d);
            const float fr = d - (float) e;
            delayFrac = fr;
            int c = delayWrite - e;
            while (c < 0) c += delaySize;
            int c1 = c - 1;
            while (c1 < 0) c1 += delaySize;
            const float om = 1.0f - fr;
            dL = om * delayL[(size_t) c] + fr * delayL[(size_t) c1];
            dR = fr * delayR[(size_t) c1] + om * delayR[(size_t) c];
            if (mode == 2) { x8 = in0 + fbL; x7 = in1 + fbR; }
            else if (mode == 3) { x8 = dL; x7 = dR; }
        }
        // 2-tap smoothing of the recording when shifting up
        const float r3 = (float) std::fmin ((double) (2.0f - ratio), 1.0);
        lp = r3;
        const float a = 1.0f - r3, bq = r3 + 1.0f;
        const float w1 = ((bq * x8) + (a * prevL)) * 0.5f;
        prevL = x8;
        float w2 = 0.0f;
        if (nch > 1)
        {
            w2 = ((bq * x7) + (a * prevR)) * 0.5f;
            prevR = x7;
        }
        recL[(size_t) recWrite] = w1;
        recR[(size_t) recWrite] = w2;
        recWrite = recWrite + 1 >= recSize ? 0 : recWrite + 1;
        countdown -= 1;
        if (countdown < 1) // start a grain
        {
            seed = seed * 0x343fdu + 0x269ec3u;
            const int r1 = (int) ((seed >> 16) & 0x7fffu);
            const float j = ((float) r1 * hx (0xb8000100)) * (jitterTime * jitterScale);
            const int jc = truncF (j);
            countdown = truncF ((float) grainLen / density + 0.5f) - jc;
            int k = 0;
            bool ok = true;
            if (active[0] != 0)
            {
                do ++k;
                while (k < 20 && active[(size_t) k] != 0); // the engine scans on past 20; never reached in practice
                if (k >= 20) ok = false;
            }
            if (ok)
            {
                active[(size_t) k] = 1;
                int back = 0;
                if (ratio > 1.0f) back = _mm_cvttsd_si32 (_mm_set_sd ((double) (ratio - 1.0f) * (double) grainLen));
                const float pos0 = (float) (recWrite - back - 5);
                seed = seed * 0x343fdu + 0x269ec3u;
                const int r2 = (int) ((seed >> 16) & 0x7fffu);
                const float jj = ((float) r2 * hx (0x38000100)) * (jitterPos * jitterScale);
                float p = pos0 - jj;
                pos[(size_t) k] = p;
                while (0.0f > pos[(size_t) k]) pos[(size_t) k] = (float) recSize + pos[(size_t) k];
                while (! ((float) recSize > pos[(size_t) k])) pos[(size_t) k] = pos[(size_t) k] - (float) recSize;
                rate[(size_t) k] = ratio;
                length[(size_t) k] = grainLen;
                remain[(size_t) k] = grainLen;
            }
        }
        float accL = 0.0f, accR = 0.0f;
        const int n = 0x400;
        for (size_t k = 0; k < 20; ++k)
        {
            if (active[k] == 0) continue;
            float t = 1.0f - (float) remain[k] / (float) length[k];
            t = t * ((float) n - 1.0f);
            const int ci = truncF (t);
            const float fr = t - (float) ci;
            float wv = (1.0f - fr) * window[(size_t) ci];
            if (ci != n - 1) wv = fr * window[(size_t) ci + 1] + wv;
            const float ps = pos[k];
            const int ip = truncF (ps);
            const float f = ps - (float) ip;
            const float f2 = f * f;
            const int size = recSize;
            const size_t i0 = (size_t) ip;
            const size_t im1 = (size_t) ((ip - 1 >= 0) ? ip - 1 : ip + size - 1);
            const size_t i1 = (size_t) ((ip + 1 < size) ? ip + 1 : ip + 1 - size);
            const size_t i2 = (size_t) ((ip + 2 < size) ? ip + 2 : ip + 2 - size);
            auto interp = [&] (const std::vector<float>& buf) {
                const float y0 = buf[i0], ym1 = buf[im1], y1 = buf[i1], y2 = buf[i2];
                float c1 = y1 - (ym1 * c03333 + y0 * 0.5f);
                c1 = c1 - y2 * c01667;
                float c3 = y2 * c01667 - ym1 * c01667;
                c3 = c3 + (y0 - y1) * 0.5f;
                const float c2 = (y1 * 0.5f + ym1 * 0.5f) - y0;
                float v = c1 + c3 * f2;
                v = v * f;
                v = v + y0;
                v = v + c2 * f2;
                return v * wv;
            };
            accL = accL + interp (recL);
            if (nch > 1) accR = accR + interp (recR);
            const float np = ps + rate[k];
            pos[k] = np;
            if (np >= (float) size) pos[k] = np - (float) size;
            remain[k] -= 1;
            if (remain[k] < 1) active[k] = 0;
        }
        if (clipGrains)
        {
            accL = clipCeil * clip (clipK * accL);
            accR = clipCeil * clip (clipK * accR);
        }
        float fxL = 0.0f, fxR = 0.0f;
        bool filt = true;
        if (mode == 2) { fxL = dL; fxR = dR; }
        else if (mode == 3) { fxL = accL; fxR = accR; }
        else if (mode == 4) { fxL = dL; fxR = dR; }
        else if (mode == 1) filt = false;
        if (filt)
        {
            const float b0 = co[0], b1 = co[1], b2 = co[2], a1 = co[3], a2 = co[4];
            // left: three transposed direct-form II stages
            const float y1 = b0 * fxL + st[0];
            st[0] = (b1 * fxL + st[3]) - a1 * y1;
            st[3] = b2 * fxL - a2 * y1;
            const float z = y1 * co[5] + st[1];
            st[1] = (co[6] * y1 + st[4]) - z * co[8];
            st[4] = y1 * co[7] - z * co[9];
            const float u = z * co[10] + st[2];
            st[2] = (co[11] * z + st[5]) - u * co[13];
            st[5] = z * co[12] - u * co[14];
            const float oL = u * fbGain;
            fbL = oL;
            // right
            const float y1r = b0 * fxR + st[6];
            st[6] = (b1 * fxR + st[9]) - a1 * y1r;
            st[9] = b2 * fxR - a2 * y1r;
            const float zr = y1r * co[5] + st[7];
            st[7] = (co[6] * y1r + st[10]) - zr * co[8];
            st[10] = y1r * co[7] - zr * co[9];
            const float ur = zr * co[10] + st[8];
            const float oR = fbGain * ur;
            st[8] = (co[11] * zr + st[11]) - ur * co[13];
            st[11] = zr * co[12] - ur * co[14];
            fbR = oR;
            if (! clipGrains)
            {
                fbL = clipCeil * clip (clipK * oL);
                fbR = clipCeil * clip (clipK * oR);
            }
            float wL = 0.0f, wR = 0.0f;
            if (mode == 2) { wL = accL; wR = accR; }
            else if (mode == 3) { wL = in0 + fbL; wR = in1 + fbR; }
            else if (mode == 4) { wR = accR + fbR; wL = accL + fbL; }
            delayL[(size_t) delayWrite] = wL;
            delayR[(size_t) delayWrite] = wR;
            delayWrite = delayWrite + 1 > delaySize - 1 ? 0 : delayWrite + 1;
        }
        float oL = accL;
        if (mode == 4) oL = oL + fbL;
        out[0] = mix * oL + (1.0f - mix) * in0;
        if (nch > 1)
        {
            float oR = accR;
            if (mode == 4) oR = oR + fbR;
            out[1] = mix * oR + (1.0f - mix) * in1;
        }
    }
};

// one-pole smoother towards a target with a time constant: c = 1 − 0.37^(1 / (rate · time))
struct Smoother
{
    float cur = 0, target = 0, time = hx (0x3ba3d70a), rate = 44100.0f, coef = hx (0x3b936c00);

    void setRate (float r) noexcept
    {
        rate = r;
        coef = 1.0f - powf_ (hx (0x3ebd70a4), 1.0f / (r * time));
    }
    float step() noexcept { return cur = (target - cur) * coef + cur; }
};

// peak follower: attack blends towards |x|, release decays
struct Follower
{
    float attackC = hx (0x3ba30f80), releaseC = hx (0x3982c000), rate = 44100.0f, state = 0, attackK = hx (0x3f7eb9e1), releaseK = hx (0x3f7fefa8);
};

// predelay line with a smoothed fractional read (linear, or cubic in high quality)
struct Predelay
{
    std::vector<float> buf = std::vector<float> ((size_t) predelaySize, 0.0f);
    int write = 0;
    float target = 1.0f, coef = hx (0x391d4952), cur = 1.0f;

    float read (bool hq) noexcept
    {
        const float c = (target - cur) * coef + cur;
        cur = c;
        const int ip = truncF (c);
        const float fr = c - (float) ip;
        int i0 = write - ip;
        while (i0 < 0) i0 += predelaySize;
        int i1 = i0 - 1;
        if (i1 < 0) i1 += predelaySize;
        if (! hq) return (1.0f - fr) * buf[(size_t) i0] + fr * buf[(size_t) i1];
        int i2 = i0 + 1;
        if (i2 >= predelaySize) i2 -= predelaySize;
        int i3 = i0 - 2;
        if (i3 < 0) i3 += predelaySize;
        const float f2 = fr * fr;
        const float y0 = buf[(size_t) i0], ya = buf[(size_t) i1], yb = buf[(size_t) i2], yc = buf[(size_t) i3];
        float c1 = ya - (yb * c03333 + y0 * 0.5f);
        c1 = c1 - yc * c01667;
        const float c2 = (ya + yb) * 0.5f - y0;
        float c3 = yc * c01667 - yb * c01667;
        c3 = c3 + (y0 - ya) * 0.5f;
        float v = f2 * c3 + c1;
        v = v * fr;
        v = v + y0;
        return v + f2 * c2;
    }
    void push (float x) noexcept
    {
        buf[(size_t) write] = x;
        if (++write >= predelaySize) write = 0;
    }
};

// eight all-passes of 4096 samples in series
struct Diffuser
{
    std::vector<float> buf = std::vector<float> ((size_t) diffuserSize * 8, 0.0f);
    std::array<int, 8> delay { 1, 1, 1, 1, 1, 1, 1, 1 };
    std::array<float, 8> gain {};
    int write = 0;

    float process (float x) noexcept
    {
        const int w = write;
        float in = x;
        for (size_t k = 0; k < 8; ++k)
        {
            float* b = buf.data() + diffuserSize * k;
            const float y = b[(w - delay[k]) & (diffuserSize - 1)];
            const float v = y * gain[k] + in;
            b[w] = v;
            in = y - v * gain[k];
        }
        write = (w + 1) & (diffuserSize - 1);
        return in;
    }
};

// four modulated delay lines of the network (one group)
struct LineGroup
{
    std::array<float, 4> smooth { hx (0x3a83126f), hx (0x3a83126f), hx (0x3a83126f), hx (0x3a83126f) };
    std::array<float, 4> cur {}, target {};
    std::array<int, 4> mask {};
    int write = 0, size = 1;
    std::array<std::vector<float>, 4> buf;

    void init (double fs)
    {
        const int t = _mm_cvttsd_si32 (_mm_set_sd (fs * 1.0 + 0.5));
        int s = 1;
        if (t > 1)
            while (s < t) s += s;
        size = s;
        mask.fill (s - 1);
        for (auto& b : buf) b.assign ((size_t) s, 0.0f);
        write = 0;
    }
};
} // namespace

namespace luxeverb
{
struct Engine::Impl
{
    // network
    std::array<float, 16> lineOut {}, inj {}, lineLen {}, lfoOfs {};
    std::array<std::array<float, 16>, 16> mat {};
    std::array<float, 4> modDepth {};
    std::array<float, 16> dA1 {}, dB0 {}, dB1 {}, dZ {};
    std::array<LineGroup, 4> groups;
    float wetDb = 0;            // after the envelope (dB)
    float fs = 0, rateRel = 0, fs16 = 0;
    float fbSample = 0;         // the feedback path's output
    std::array<Smoother, 13> sm; // 0 freeze gain, 1 freeze mix, 2 envelope, 3 width, 4 brightness, 5 decay, 6…8 peak f/g/q,
                                 // 9 input low cut, 10 input high cut, 11 feedback high cut, 12 feedback low cut
    Follower env, meterL, meterR;
    float envOut = 0, mL = 0, mR = 0;
    float limCur = 0, limTarget = 0, limTime = hx (0x3ba3d70a), limRate = 44100.0f, limCoef = hx (0x3b936c00);
    float dryCur = 0, dryTarget = 0, gainK = 0, wetCur = 0;
    float outWetDb = 0, inGain = 0, envThresh = 0, envScale = 0, envOffset = 0;
    bool sidechain = false, hq = false, reverbOn = false, feedbackOn = false, limiter = false;
    int freezeMode = 0, envMode = 0, pitchType = 0;
    float fbSemis = 0, ffPitch1 = 0, ffPitch2 = 0, ffAmp1 = 0, ffAmp2 = 0, ffDelay1 = 0, ffDelay2 = 0, ffDry = 0;
    float eB0 = 0, eB1 = 0, eB2 = 0, eA1 = 0, eA2 = 0, eS1 = 0, eS2 = 0;           // envelope high-pass
    float lpB0 = 0, lpB1 = 0, lpA1 = 0, lpSL = 0, lpSR = 0;                       // input high cut (one pole)
    float hpB0 = 0, hpB1 = 0, hpA1 = 0, hpSL = 0, hpSR = 0;                       // input low cut (one pole)
    float pkB0 = 0, pkB1 = 0, pkB2 = 0, pkA1 = 0, pkA2 = 0, pkL1 = 0, pkL2 = 0, pkR1 = 0, pkR2 = 0; // output peak
    std::array<Diffuser, 2> diff;
    float dcK = hx (0x3f733333), dcS = 0;
    float flB0 = 0, flB1 = 0, flB2 = 0, flA1 = 0, flA2 = 0, flS1 = 0, flS2 = 0;   // feedback low-pass
    float fhB0 = 0, fhB1 = 0, fhB2 = 0, fhA1 = 0, fhA2 = 0, fhS1 = 0, fhS2 = 0;   // feedback high-pass
    std::array<Predelay, 2> pre;
    float decay = 0, decayBright = 0, predelay = 1.0f, predelayMax = hx (0x4827f800), tempo = 120.0f;
    float charAngle = 0;
    std::array<float, 4> rot {};           // 2×2 rotation (c, s, −s, c)
    std::array<std::array<float, 4>, 4> q {};
    float sizeGain = 0;
    int dampNext = 0;
    float lfoInc = 0, lfoPhase = 0;
    Shifter fb, ff1, ff2;
    float fbGain = 0;
    float ffIn[2] {}, ffOut[2] {};
    float diffMix = 0, fbMix = 0;
    float wetGain = 0;
    std::uint32_t seed = 0x3920a; // the library's LCG seed (process-wide in FL, per instance here)

    Impl()
    {
        for (size_t k = 0; k < 16; ++k) { dA1[k] = 0.0f; dB0[k] = 1.0f; dB1[k] = 0.0f; dZ[k] = 0.0f; }
        for (auto& g : groups) g.init (44100.0);
    }

    void dampCoef (int k, float x10) noexcept
    {
        float x6 = (lineLen[(size_t) k] / fs) * -3.0f;
        const float g1 = powf_ (10.0f, x6 / decay);
        x6 = x6 / decayBright;
        const float fs2 = fs + fs;
        const float x8 = x10 * fs2;
        float g2 = powf_ (10.0f, x6);
        g2 = (g2 * x10) * fs2;
        const float n = 1.0f / (x8 + 1.0f);
        dB0[(size_t) k] = (g2 + g1) * n;
        dA1[(size_t) k] = (1.0f - x8) * n;
        dB1[(size_t) k] = (g1 - g2) * n;
    }

    void buildMatrix() noexcept
    {
        auto R = [this] (int r, int c) { return rot[(size_t) (2 * r + c)]; };
        for (int i = 0; i < 4; ++i)
        {
            const int a = i >> 1, b = i & 1;
            q[(size_t) i] = { R (b, 0) * R (a, 0), R (a, 0) * R (b, 1), R (b, 0) * R (a, 1), R (a, 1) * R (b, 1) };
        }
        for (int i = 0; i < 16; ++i)
        {
            const int a = i >> 2, b = i & 3;
            for (int p = 0; p < 4; ++p)
                for (int r = 0; r < 4; ++r)
                    mat[(size_t) i][(size_t) (4 * p + r)] = q[(size_t) a][(size_t) p] * q[(size_t) b][(size_t) r];
        }
    }

    void setFreeze (int m) noexcept
    {
        if (m == 1) { sm[0].target = 1.0f; sm[1].target = 1.0f; return; }
        sm[1].target = 0.0f;
        sm[0].target = m == 2 ? 0.0f : 1.0f;
    }

    void clearLines() noexcept
    {
        for (auto& g : groups)
            for (auto& b : g.buf) std::fill (b.begin(), b.end(), 0.0f);
        for (size_t k = 0; k < 16; ++k) dZ[k] = 0.0f;
        for (auto& p : pre) std::fill (p.buf.begin(), p.buf.end(), 0.0f);
        for (auto& d : diff) std::fill (d.buf.begin(), d.buf.end(), 0.0f);
    }

    void highPass (float w, float qv) noexcept // envelope detector high-pass (RBJ)
    {
        const float wn = (w * hx (0x40c90fdb)) / fs;
        float s, c;
        sincosf_ (wn, s, c);
        const float al = s * (0.5f / qv);
        const float n = 1.0f / (al + 1.0f);
        eA1 = (c * -2.0f) * n;
        eA2 = (1.0f - al) * n;
        eB1 = -n - c / (al + 1.0f);
        const float b0 = ((c + 1.0f) * 0.5f) * n;
        eB0 = b0;
        eB2 = b0;
    }

    void reset() noexcept
    {
        clearLines();
        eS1 = eS2 = 0; lpSL = lpSR = 0; pkL1 = pkL2 = pkR1 = pkR2 = 0; hpSL = hpSR = 0;
        flS1 = flS2 = 0; fhS1 = fhS2 = 0;
        dcS = 0;
        fb.reset();
        fbSample = 0;
    }

    void setRate (float r)
    {
        wetGain = 0;
        fs = r;
        decay = 3.5f;
        rateRel = r * hx (0x37be37c6);
        fs16 = r * 0.0625f;
        decayBright = 0.5f;
        for (auto& g : groups) g.init ((double) fs);
        dryCur = 0.5f; dryTarget = 0.5f; wetCur = 0.5f;
        gainK = 1.0f - powf_ (hx (0x3ebd70a4), hx (0x4235d174) / r);
        envOffset = 0;
        sidechain = false; hq = true; reverbOn = true; feedbackOn = true; limiter = true;
        inGain = 1.0f;
        predelayMax = hx (0x4827f800); tempo = 120.0f; predelay = 1.0f;
        pitchType = 0; ffPitch1 = 0; ffPitch2 = 0; ffAmp1 = 0.5f; ffAmp2 = 0.5f; ffDelay1 = 0; ffDelay2 = 0; ffDry = 0.5f;
        for (size_t k = 0; k < 16; ++k) lineLen[k] = r * hx (lineFactor[k]);
        sizeGain = hx (0x3feeeeef); dampNext = 0;
        const float smc = hx (0x40d3ae15) / r;
        for (size_t g = 0; g < 4; ++g)
            for (size_t k = 0; k < 4; ++k)
            {
                groups[g].smooth[k] = smc;
                groups[g].cur[k] = lineLen[4 * g + k];
                groups[g].target[k] = lineLen[4 * g + k];
            }
        { // envelope high-pass at 20 Hz, Q 0.75
            float s, c;
            sincosf_ (hx (0x42fb53d2) / r, s, c);
            const float al = s * hx (0x3f2aaaab), n = 1.0f / (al + 1.0f);
            eB0 = ((c + 1.0f) * 0.5f) * n; eB2 = eB0;
            eA1 = (c * -2.0f) * n;
            eB1 = -n - c / (al + 1.0f);
            eA2 = (1.0f - al) * n;
        }
        { // feedback low-pass at 10 kHz
            float s, c;
            sincosf_ (hx (0x47756fdb) / fs, s, c);
            const float al = s * hx (0x3f2aaaab), n = 1.0f / (al + 1.0f);
            flA1 = (c * -2.0f) * n;
            const float b1 = (1.0f - c) * n;
            flB1 = b1; flB0 = b1 * 0.5f; flB2 = b1 * 0.5f;
            flA2 = (1.0f - al) * n;
        }
        { // input high cut set-up (overwritten per sample)
            float s, c;
            sincosf_ (hx (0x47f56fdb) / fs, s, c);
            const float al = s * hx (0x3f2aaaab), n = 1.0f / (al + 1.0f);
            lpSL = (c * -2.0f) * n;
            const float b1 = (1.0f - c) * n;
            lpB1 = b1; lpB0 = b1 * 0.5f; lpA1 = b1 * 0.5f;
            lpSR = (1.0f - al) * n;
        }
        { // input low cut set-up (overwritten per sample)
            float s, c;
            sincosf_ (hx (0x42fb53d2) / fs, s, c);
            const float al = s * hx (0x3f2aaaab), n = 1.0f / (al + 1.0f);
            hpB0 = ((c + 1.0f) * 0.5f) * n; hpA1 = hpB0;
            hpSL = (c * -2.0f) * n;
            hpB1 = -n - c / (al + 1.0f);
            hpSR = (1.0f - al) * n;
        }
        { // output peak at 1 kHz (overwritten per sample)
            float s, c;
            sincosf_ (hx (0x45c4597c) / fs, s, c);
            const float A = (4.0f - s) / (s + 4.0f);
            pkA2 = A;
            const float oma = 1.0f - A;
            const float h = (A + 1.0f) * 0.5f;
            const float nc = -(c * (A + 1.0f));
            pkB1 = nc; pkA1 = nc;
            pkB0 = h + oma;
            pkB2 = h - oma;
        }
        {
            const std::uint32_t df[8] = { 0x3b9b3eb6, 0x3b6b9f20, 0x3c50a676, 0x3c187e7c, 0x3bfeb57e, 0x3c83fe5d, 0x3c55aac2, 0x3c1204e6 };
            const std::uint32_t dg[8] = { 0x3f400000, 0x3f200000, 0x3f333333, 0x3f000000, 0x3f2ccccd, 0x3f3645a2, 0x3f19999a, 0x3f153f7d };
            for (size_t k = 0; k < 8; ++k)
                for (auto& d : diff)
                {
                    d.delay[k] = truncF (r * hx (df[k]));
                    d.gain[k] = hx (dg[k]);
                }
        }
        lfoPhase = 0;
        lfoInc = 0.5f / r;
        for (size_t i = 0; i < 16; ++i) lfoOfs[i] = (float) i * 0.0625f;
        charAngle = 45.0f;
        rot = { hx (0x3f3504f2), hx (0x3f3504f3), hx (0xbf3504f3), hx (0x3f3504f2) };
        buildMatrix();
        limTime = hx (0x3c23d70a);
        limCoef = 1.0f - powf_ (hx (0x3ebd70a4), 100.0f / limRate); // the limiter keeps its 44.1 kHz rate (§2.8)
        {
            const float e1 = expf_ (-110.0f / meterL.rate);
            meterL.releaseK = e1; meterL.releaseC = 1.0f - e1;
            const float e2 = expf_ (-110.0f / meterR.rate);
            meterR.releaseK = e2; meterR.releaseC = 1.0f - e2;
        }
        {
            const float x10 = 1.0f / (hx (0x4504ccc2) * hx (0x40c90fdb));
            for (int k = 0; k < 16; ++k) dampCoef (k, x10);
        }
        sm[0].rate = fs;
        sm[0].cur = 1.0f; sm[0].target = 1.0f; sm[0].time = hx (0x3ca3d70a);
        sm[0].coef = 1.0f - powf_ (hx (0x3ebd70a4), 50.0f / fs);
        sm[1].cur = 1.0f; sm[1].target = 1.0f; sm[1].time = hx (0x3ca3d70a); sm[1].rate = fs;
        sm[1].coef = 1.0f - powf_ (hx (0x3ebd70a4), 50.0f / fs);
        env.state = 0; env.rate = fs;
        {
            const float e1 = expf_ (-220.0f / fs);
            env.attackC = 1.0f - e1; env.attackK = e1;
            const float e2 = expf_ (-11.0f / fs);
            env.releaseK = e2; env.releaseC = 1.0f - e2;
        }
        fbGain = 0;
        fb.init (fs);
        fb.set (0, 12.0f); fb.set (1, 150.0f); fb.set (2, 3.5f); fb.set (3, hx (0x3dcccccd)); fb.set (4, hx (0x3dcccccd));
        fb.set (5, 1.0f); fb.set (6, 1.0f); fb.set (7, 0.0f); fb.set (13, 5.0f); fb.set (8, 3.0f); fb.set (14, 1.0f);
        fbSample = 0;
        dcK = (hx (0x357025b9) - fs * hx (0x2c3c0ba4)) * fs + hx (0x3f6a6e3a);
        ff1.init (fs);
        ff1.set (0, 7.0f); ff1.set (1, 150.0f); ff1.set (2, 2.25f); ff1.set (3, 0.0f); ff1.set (4, hx (0x3dcccccd));
        ff1.set (5, 1.0f); ff1.set (6, 1.0f); ff1.set (7, 0.0f); ff1.set (13, 5.0f); ff1.set (8, 3.0f); ff1.set (14, 0.0f);
        ff2.init (fs);
        ff2.set (0, 12.0f); ff2.set (1, 150.0f); ff2.set (2, 2.25f); ff2.set (3, 0.0f); ff2.set (4, hx (0x3dcccccd));
        ff2.set (5, 1.0f); ff2.set (6, 1.0f); ff2.set (7, 0.0f); ff2.set (13, 5.0f); ff2.set (8, 3.0f); ff2.set (14, 0.0f);
        diffMix = 0; fbMix = 0;
        sidechain = false;
        freezeMode = 1;
        for (size_t j = 0; j < 6; ++j) sm[j].setRate (fs);
        for (size_t j = 6; j < 13; ++j) sm[j].setRate (fs16);
        reset();
    }

    void setTempo (float bpm)
    {
        tempo = bpm;
        fb.set (9, bpm);
        ff1.set (9, bpm);
        ff2.set (9, bpm);
    }

    void setParam (int idx, float v)
    {
        switch (idx)
        {
            case 0: inGain = powf_ (10.0f, v * hx (0x3d4ccccd)); break;
            case 1: sm[10].target = v; break;
            case 2: sm[9].target = v; break;
            case 3:
            {
                const bool old = reverbOn, on = v >= 0.5f;
                reverbOn = on;
                if (! on)
                {
                    if (! old) break;
                    clearLines();
                    sm[0].target = 1.0f; sm[1].target = 1.0f;
                    break;
                }
                if (! old) setFreeze (freezeMode);
                break;
            }
            case 4: sm[5].target = v; break;
            case 5:
            case 6:
            {
                const float x = idx == 5 ? (v * hx (0x3a83126f)) * fs : (15.0f / tempo) * (v * fs);
                const float y = fmaxf_ (1.0f, fminf_ (predelayMax, x));
                predelay = y; pre[0].target = y; pre[1].target = y;
                break;
            }
            case 7: sm[4].target = v; break;
            case 8:
            {
                const float a = v * 90.0f;
                charAngle = a;
                float s, c;
                sincosf_ (a * hx (0x3c8efa36), s, c);
                rot = { c, s, -s, c };
                buildMatrix();
                break;
            }
            case 9:
            {
                const float s = v * hx (0x419d7ae1) + hx (0x436650a4);
                float x12 = (s - 175.0f) * hx (0x3d8d3dcb) + 100.0f;
                const float x13 = 1.0f / (hx (0x4504ccc2) * hx (0x40c90fdb));
                float x15 = hx (0xbe550c08) - (x12 / s) * hx (0xbf9aa181);
                x15 = x15 * s;
                x12 = x12 - x15;
                x12 = x12 * hx (0x3bd3680d);
                const auto& pr = primes();
                for (int k = 0; k < 16; ++k)
                {
                    const int ix = std::clamp (truncF (x12 * lineBase[k] + x15), 0, (int) pr.size() - 1);
                    const float d = ((float) pr[(size_t) ix] * fs) * hx (0x37be37c6);
                    lineLen[(size_t) k] = d;
                    groups[(size_t) (k >> 2)].target[(size_t) (k & 3)] = d;
                    dampCoef (k, x13);
                }
                sizeGain = (s - 250.0f) * hx (0x3aaec33e) + 1.0f;
                break;
            }
            case 10: lfoInc = v / fs; break;
            case 11:
            {
                const float a = (v * hx (0x3dcccccd)) * rateRel;
                modDepth.fill (a);
                break;
            }
            case 12: diffMix = v; break;
            case 13:
            {
                freezeMode = truncF (std::round (v));
                if (reverbOn) setFreeze (freezeMode);
                break;
            }
            case 14: hq = v >= 0.5f; break;
            case 15:
            {
                const bool old = feedbackOn, on = v >= 0.5f;
                feedbackOn = on;
                if (! on && old)
                {
                    flS1 = flS2 = 0; fhS1 = fhS2 = 0;
                    dcS = 0;
                    fb.reset();
                }
                break;
            }
            case 16: fbSemis = v; fb.set (0, v); break;
            case 17: fb.set (6, v); break;
            case 18: fb.set (10, v); break;
            case 19: fbGain = v; break;
            case 20: sm[11].target = v; break;
            case 21: sm[12].target = v; break;
            case 22: fbMix = v; break;
            case 23: envScale = v; break;
            case 24: envThresh = v; break;
            case 25: { const float k = expf_ (hx (0xc5098000) / (v * env.rate)); env.attackK = k; env.attackC = 1.0f - k; break; }
            case 26: { const float k = expf_ (hx (0xc5098000) / (v * env.rate)); env.releaseK = k; env.releaseC = 1.0f - k; break; }
            case 27: envOffset = v; break;
            case 28: envMode = truncF (std::round (v)); break;
            case 29: sidechain = v >= 0.5f; break;
            case 30: highPass (v, 0.75f); break;
            case 31:
            {
                const float t = v * hx (0x3a83126f);
                sm[2].time = t;
                sm[2].coef = 1.0f - powf_ (hx (0x3ebd70a4), 1.0f / (t * sm[2].rate));
                break;
            }
            case 32:
            {
                const float g = powf_ (10.0f, v * hx (0x3d4ccccd));
                dryTarget = fmaxf_ (fminf_ (g, hx (0x40b33333)), 0.0f);
                break;
            }
            case 33: outWetDb = v; break;
            case 34: sm[6].target = v; break;
            case 35: sm[7].target = powf_ (10.0f, v * hx (0x3d4ccccd)); break;
            case 36: sm[8].target = v; break;
            case 37: sm[3].target = v; break;
            case 38: limiter = v >= 0.5f; break;
            case 39: pitchType = truncF (std::round (v)); break;
            case 40: ffDry = v; break;
            case 41: ffPitch1 = v; ff1.set (15, v); break;
            case 42: ffPitch2 = v; ff2.set (15, v); break;
            case 43: ffAmp1 = v; break;
            case 44: ffAmp2 = v; break;
            case 45: ffDelay1 = v; ff1.set (6, v); break;
            case 46: ff1.set (10, v); break;
            case 47: ffDelay2 = v; ff2.set (6, v); break;
            case 48: ff2.set (10, v); break;
            default: break;
        }
    }

    void render (int n, const float* in, const float* sc, float* out) noexcept
    {
        const float h = 0.5f, one = 1.0f, two = 2.0f;
        float envDb = 0.0f;
        for (int s = 0; s < n; ++s)
        {
            const float xL = in[2 * s], xR = in[2 * s + 1];
            // envelope follower (input or sidechain, high-passed), smoothed, in dB
            if (envMode == 1)
            {
                sm[2].target = 0.0f;
                env.state = 0.0f;
                sm[2].step();
            }
            else
            {
                float e;
                if (sidechain) e = (sc[2 * s + 1] + sc[2 * s]) * h;
                else e = (xR + xL) * (inGain * h);
                const float y = e * eB0 + eS1;
                float s1 = e * eB1 - y * eA1;
                s1 = s1 + eS2;
                eS2 = e * eB2 - y * eA2;
                eS1 = s1;
                const float a = std::fabs (y);
                float v = env.state;
                if (a > v) v = v * env.attackK + a * env.attackC;
                else v = v * env.releaseK;
                env.state = v;
                sm[2].target = v;
                envOut = v;
                sm[2].step();
                envDb = log10f_ (sm[2].cur) * 20.0f;
            }
            float r6 = inGain * xR, l7 = inGain * xL;
            // feed-forward pitch shifters
            if (pitchType == 1 && feedbackOn)
            {
                ffIn[0] = l7; ffIn[1] = r6;
                l7 = l7 * ffDry; r6 = r6 * ffDry;
                ff1.process (seed, 2, ffIn, ffOut);
                l7 = l7 + ffAmp1 * ffOut[0];
                r6 = r6 + ffAmp1 * ffOut[1];
                ff2.process (seed, 2, ffIn, ffOut);
                r6 = r6 + ffAmp2 * ffOut[1];
                l7 = l7 + ffAmp2 * ffOut[0];
            }
            { // input low / high cut (one-pole, coefficients follow the smoothed frequencies)
                const float a = sm[9].step();
                const float w = (a * hx (0x40490fdb)) / fs;
                const float nn = one / (w + one);
                hpB0 = nn; hpB1 = -nn; hpA1 = (w - one) * nn;
                const float b = sm[10].step();
                const float w2 = (b * hx (0x40490fdb)) / fs;
                const float n2 = one / (w2 + one);
                lpB0 = w2 * n2; lpB1 = w2 * n2; lpA1 = (w2 - one) * n2;
            }
            float L, R;
            {
                const float yL = hpB0 * l7 + hpSL;
                const float yR = hpB0 * r6 + hpSR;
                hpSL = hpB1 * l7 - yL * hpA1;
                hpSR = hpB1 * r6 - yR * hpA1;
                L = yL * lpB0 + lpSL;
                R = yR * lpB0 + lpSR;
                lpSL = yL * lpB1 - L * lpA1;
                lpSR = yR * lpB1 - R * lpA1;
            }
            if (reverbOn) // predelay and diffuser
            {
                const float pL = pre[0].read (hq);
                const float pR = pre[1].read (hq);
                pre[0].push (L);
                pre[1].push (R);
                const float om = one - diffMix;
                L = diff[0].process (pL) * diffMix + om * pL;
                R = diff[1].process (pR) * diffMix + om * pR;
            }
            if (feedbackOn && pitchType == 0) // feedback injection (level compensated for decay and pitch)
            {
                float f = (fbGain * fbSample) * 8.0f;
                const float g = fminf_ (powf_ (decay, hx (0xbe4ccccd)), one);
                const float sem = fbSemis;
                const float k = sem * hx (0x3daaaaab);
                f = f * g;
                if (sem >= 0.0f) f = f * (k * hx (0x3f333333) + h);
                else f = f * (h - k * two);
                f = f * sm[1].cur;
                L = L + f;
                R = R + f;
            }
            const float mono = (R + L) * h;
            sm[0].step();
            sm[1].step();
            if (reverbOn)
            {
                float ph = lfoInc + lfoPhase;
                L = L * sm[0].cur;
                R = R * sm[0].cur;
                ph = ph - (float) truncF (ph);
                lfoPhase = ph;
                for (size_t g = 0; g < 4; ++g)
                    for (size_t l = 0; l < 4; ++l)
                    {
                        float t = ph + lfoOfs[4 * g + l];
                        t = t - (float) cvtF (t);
                        t = t + t;
                        const float a = t < one ? t : one;
                        float b = t > one ? t : one;
                        b = b - one;
                        const float a2 = a - a * a;
                        const float b2 = b - b * b;
                        float tri = a2 - b2;
                        tri = tri * 4.0f;
                        tri = tri * modDepth[l];
                        tri = tri + one;
                        tri = tri * lineLen[4 * g + l];
                        groups[g].target[l] = tri;
                    }
                decay = sm[5].step();
                if (envMode == 3) // envelope → decay
                {
                    float x = fmaxf_ (envDb - envThresh, 0.0f);
                    x = x + envOffset * envThresh;
                    x = x * envScale;
                    x = x + decay;
                    decay = x;
                    x = fminf_ (x, 20.0f);
                    decay = fmaxf_ (x, hx (0x3e800000));
                }
                const float br = sm[4].step();
                decayBright = decay * br;
                dampCoef (dampNext, one / (hx (0x4504ccc2) * hx (0x40c90fdb)));
                dampNext = dampNext + 1 >= 16 ? 0 : dampNext + 1;
                const float fz = sm[1].cur, ofz = one - fz;
                for (size_t g = 0; g < 4; ++g)
                {
                    auto& G = groups[g];
                    float v[4];
                    const int w = G.write;
                    for (size_t l = 0; l < 4; ++l)
                    {
                        const float c = (G.target[l] - G.cur[l]) * G.smooth[l] + G.cur[l];
                        G.cur[l] = c;
                        const int ip = truncF (c);
                        const float fr = c - (float) ip;
                        const int mask = G.mask[l];
                        const int i0 = (w - ip) & mask;
                        const float* buf = G.buf[l].data();
                        if (hq)
                        {
                            const int i1 = (i0 + 1) & mask, im1 = (i0 - 1) & mask, im2 = (im1 - 1) & mask;
                            const float y0 = buf[i0], yn = buf[i1], yo = buf[im1], yo2 = buf[im2];
                            const float d1 = y0 - yo;
                            const float d3 = (yo - yn) * 0.5f;
                            float d15 = (yo2 - y0) * 0.5f;
                            const float d2 = d1 + d3;
                            const float d0 = d2 + d1;
                            d15 = d15 + d0;
                            const float d0b = d15 + d2;
                            d15 = d15 * fr;
                            d15 = d15 - d0b;
                            d15 = d15 * fr;
                            d15 = d15 + d3;
                            d15 = d15 * fr;
                            v[l] = d15 + y0;
                        }
                        else
                        {
                            const int i1 = (i0 - 1) & mask;
                            v[l] = buf[i0] * (one - fr) + buf[i1] * fr;
                        }
                    }
                    G.write = (w + 1) & G.mask[1];
                    for (size_t l = 0; l < 4; ++l)
                    {
                        const size_t c = 4 * g + l;
                        const float y = v[l] * dB0[c] + dZ[c];
                        dZ[c] = v[l] * dB1[c] - y * dA1[c];
                        lineOut[c] = y * fz + v[l] * ofz;
                    }
                }
                for (size_t i = 0; i < 7; ++i) { inj[2 + 2 * i] = L; inj[3 + 2 * i] = R; }
                const float mm = (R + L) * h;
                inj[0] = mm; inj[1] = mm;
                const auto x = lineOut;
                for (size_t i = 0; i < 16; ++i)
                {
                    const auto& M = mat[i];
                    float lane[4];
                    for (size_t qq = 0; qq < 4; ++qq)
                    {
                        const float p01 = x[qq] * M[qq] + x[4 + qq] * M[4 + qq];
                        const float p23 = x[8 + qq] * M[8 + qq] + x[12 + qq] * M[12 + qq];
                        lane[qq] = p01 + p23;
                    }
                    float acc = lane[0] + lane[1];
                    acc = acc + lane[2];
                    acc = acc + lane[3];
                    acc = acc + inj[i];
                    inj[i] = acc;
                    auto& G = groups[i >> 2];
                    G.buf[i & 3][(size_t) G.write] = acc;
                }
                float r12 = lineOut[1] + lineOut[0];
                float r13 = r12 + lineOut[2];
                r12 = r12 + lineOut[3];
                for (size_t i = 2; i < 8; ++i)
                {
                    r13 = r13 + lineOut[2 * i];
                    r12 = r12 + lineOut[2 * i + 1];
                }
                const float gsum = sizeGain * 0.125f;
                L = r13 * gsum;
                R = r12 * gsum;
            }
            { // soft limiter on the louder channel's peak
                float a = std::fabs (L), v = meterL.state;
                v = a > v ? v * meterL.attackK + a * meterL.attackC : v * meterL.releaseK;
                meterL.state = v;
                mL = v;
                a = std::fabs (R);
                float u = meterR.state;
                u = a > u ? u * meterR.attackK + a * meterR.attackC : u * meterR.releaseK;
                meterR.state = u;
                mR = u;
                if (limiter) limTarget = fmaxf_ (mL, u);
                else limTarget = hx (0x3e7ffd61);
                const float lv = (limTarget - limCur) * limCoef + limCur;
                limCur = lv;
                const float gg = fmaxf_ (lv * 4.0f, one);
                const float ig = one / gg;
                L = L * ig;
                R = R * ig;
            }
            if (feedbackOn && pitchType == 0) // feedback path: shifter, DC blocker, low-pass, high-pass
            {
                float fin = (R + L) * (fbMix * h);
                fin = fin + (one - fbMix) * mono;
                fb.process (seed, 1, &fin, &fbSample);
                const float x0 = fbSample;
                const float yy = x0 + dcS;
                fbSample = yy;
                dcS = yy * dcK - x0;
                {
                    const float c = sm[11].step();
                    const float w = (c * hx (0x409c347c)) / fs;
                    const float nn = one / (w + one);
                    const float wm = (w - one) * nn;
                    const float wn = w * nn;
                    flB0 = wn * wn; flB2 = wn * wn;
                    flB1 = (wn * two) * wn;
                    flA2 = wm * wm;
                    flA1 = wm * two;
                    const float c2 = sm[12].step();
                    const float w2 = (c2 * hx (0x409c347c)) / fs;
                    const float n2 = one / (w2 + one);
                    const float wm2 = (w2 - one) * n2;
                    fhB0 = n2 * n2;
                    fhA1 = wm2 * two;
                    const float nneg = -n2;
                    fhB1 = (n2 * two) * nneg;
                    fhB2 = nneg * nneg;
                    fhA2 = wm2 * wm2;
                }
                const float xi = fbSample;
                const float y1 = xi * flB0 + flS1;
                float s1 = xi * flB1 - y1 * flA1;
                s1 = s1 + flS2;
                const float s2 = xi * flB2 - flA2 * y1;
                flS1 = s1;
                flS2 = s2;
                const float z = y1 * fhB0 + fhS1;
                fbSample = z;
                float t1 = y1 * fhB1 - z * fhA1;
                t1 = t1 + fhS2;
                const float t2 = y1 * fhB2 - fhA2 * z;
                fhS1 = t1;
                fhS2 = t2;
            }
            { // output peak filter (coefficients follow the smoothed frequency / gain / Q)
                const float fq = sm[6].step();
                const float w = (fq * hx (0x40c90fdb)) / fs;
                const float gn = sm[7].step();
                float qv = sm[8].step();
                const float sn = sinf_ (w);
                qv = qv * two;
                const float A = (qv - sn) / (qv + sn);
                const float cs = cosf_ (w);
                pkA2 = A;
                const float nc = -(cs * (A + one));
                pkB1 = nc; pkA1 = nc;
                const float hb = (A + one) * h;
                float hg = (one - A) * h;
                hg = hg * gn;
                pkB2 = hb - hg;
                pkB0 = hb + hg;
            }
            float oL, oR;
            {
                oL = pkB0 * L + pkL1;
                oR = pkB0 * R + pkR1;
                float sL = pkB1 * L - pkA1 * oL;
                float sR = pkB1 * R - pkA1 * oR;
                sL = sL + pkL2;
                sR = sR + pkR2;
                pkL1 = sL;
                pkR1 = sR;
                pkL2 = pkB2 * L - pkA2 * oL;
                pkR2 = pkB2 * R - pkA2 * oR;
            }
            if (envMode == 2) // envelope → wet level
            {
                float x = fmaxf_ (envDb - envThresh, 0.0f);
                const float y = envOffset * envThresh;
                const float z = envScale * two;
                x = x + y;
                wetDb = z * x + outWetDb;
            }
            else wetDb = outWetDb;
            float wg = powf_ (10.0f, wetDb * hx (0x3d4ccccd));
            wg = fminf_ (wg, hx (0x40b33333));
            wg = fmaxf_ (wg, 0.0f);
            wetGain = wg;
            const float k = gainK;
            const float dry = (dryTarget - dryCur) * k + dryCur;
            const float wet = (wg - wetCur) * k + wetCur;
            dryCur = dry;
            wetCur = wet;
            const float wdt = sm[3].step();
            const float mid = (oR + oL) * h;
            float side = oL - mid;
            side = side * wdt;
            out[2 * s] = (side + mid) * wet + dry * xL;
            out[2 * s + 1] = xR * dry + (mid - side) * wet;
        }
    }
};

// ------------------------------------------------------------------------------------------------- plugin side

const std::array<Range, luxeVerbControlCount> ranges { {
    { -600, 1500 }, { 40, 20000 }, { 20, 5000 }, { 0, 1 }, { 250, 20000 }, { 0, 5000 }, { 1, 768 }, { 0, 1000 }, { 0, 100 },
    { 1, 100 }, { 0, 1000 }, { 0, 100 }, { 0, 1000 }, { 0, 2 }, { 0, 1 }, { 0, 1 }, { -120, 120 }, { 10, 5000 }, { 1, 768 },
    { 0, 1000 }, { 20, 5000 }, { 20, 5000 }, { 0, 1000 }, { -1000, 1000 }, { -6000, 0 }, { 50, 1000 }, { 50, 1000 },
    { 0, 100 }, { 0, 2 }, { 0, 1 }, { 20, 2000 }, { 10, 1000 }, { -600, 1500 }, { -600, 1500 }, { 40, 10000 },
    { -1800, 1800 }, { 25, 5000 }, { 0, 200 }, { 0, 1 }, { 0, 1 }, { 0, 1000 }, { -24, 24 }, { -24, 24 }, { 0, 1000 },
    { 0, 1000 }, { 10, 5000 }, { 1, 768 }, { 0, 1000 }, { 1, 768 } } };

const std::array<const char*, luxeVerbParamCount> names { {
    "Input wet gain", "Input high cut", "Input low cut", "Reverb", "Decay", "Pre-delay", "Pre-delay (sync)", "Brightness",
    "Character", "Size", "Mod frequency", "Mod amount", "Diffusion", "Freeze mode", "High quality", "Feedback",
    "Feedback pitch", "Feedback delay", "Feedback delay (sync)", "Feedback gain", "Feedback high cut", "Feedback low cut",
    "Feedback reverb mix", "Envelope scale", "Envelope threshold", "Envelope attack", "Envelope decay", "Envelope offset",
    "Envelope mode", "Envelope sidechain", "Envelope low cut", "Envelope smoothing", "Dry level", "Wet level",
    "Peak frequency", "Peak gain", "Peak Q", "Stereo width", "Limiter", "Pitch type", "Feed-forward unpitched",
    "Feed-forward pitch 1", "Feed-forward pitch 2", "Feed-forward amount 1", "Feed-forward amount 2",
    "Feed-forward delay 1", "Feed-forward delay 1 (sync)", "Feed-forward delay 2", "Feed-forward delay 2 (sync)",
    "Pre-delay sync", "Feedback delay sync", "Feed-forward delay 1 sync", "Feed-forward delay 2 sync" } };

namespace
{
// controls of a new plugin
const std::array<int, luxeVerbControlCount> defaultRaw { {
    746, 12991, 20, 1, 13105, 1, 192, 850, 50, 20, 100, 10, 1000, 0, 1, 1, 0, 1000, 192, 700, 1719, 20, 1000, 0, -4000,
    50, 400, 50, 1, 0, 301, 10, 746, 746, 7726, 0, 1859, 125, 1, 0, 0, 0, 20, 0, 20, 1000, 192, 1000, 192 } };
// what a state older than version 9 leaves in controls 39…48 (the plugin sets these values before reading it)
const std::array<int, 10> oldStateTail { { 0, 0, 24, 20, 1719, 20, 1000, 192, 1000, 192 } };

// the plugin's curve: lo + (hi − lo) · x^(1/k) (in double, then to float)
float curve (float x, double lo, double hi, double k) noexcept
{
    const double v = x > 0.0f ? std::exp (std::log ((double) x) / k) : 0.0;
    return (float) (lo + (hi - lo) * v);
}
} // namespace

Settings::Settings() : raw (defaultRaw) {}

float engineValue (int k, int v) noexcept
{
    if (k < 0 || k >= luxeVerbControlCount) return 0.0f;
    const auto rg = ranges[(size_t) k];
    // the control's position 0…1 (in double, clamped, then to float); the freeze and envelope mode selectors have no
    // position (their scale is 0)
    const double scale = (k == 13 || k == 28) ? 0.0 : 1.0 / (double) (rg.hi - rg.lo);
    const double pd = std::clamp ((double) (v - rg.lo) * scale, 0.0, 1.0);
    const float x = (float) pd;
    switch (k)
    {
        case 0: case 32: case 33: return curve (x, -60.0, 15.0, 2.0);            // dB
        case 1: return curve (x, rg.lo, rg.hi, 1.5);                              // Hz
        case 2: case 20: case 21: case 30: case 34: return curve (x, rg.lo, rg.hi, 0.5);
        case 4: return curve (x, 0.25, 20.0, 0.4);                               // s
        case 5: return curve (x, 0.0, 500.0, 0.5);                               // ms
        case 6: case 18: case 46: case 48: return x * 16.0f;                     // sixteenths
        case 8: case 11: return (float) ((double) v * 0.01);
        case 9: case 31: return x * 99.0f + 1.0f;
        case 13: case 28: return (float) (v + 1);
        case 16: return (float) (((double) x - 0.5) * 24.0);                     // semitones
        case 17: case 45: case 47: return x * 499.0f + 1.0f;                     // ms
        case 23: return x * 2.0f - 1.0f;
        case 24: case 35: case 37: return (float) v * 0.01f;
        case 25: case 26: case 38: case 41: case 42: return (float) v;
        case 27: return (float) ((double) x - 0.5);
        case 36: return (float) ((double) x * 4.75 + 0.25);
        default: return x;                                                        // 3, 7, 10, 12, 14, 15, 19, 22, 29, 39, 40, 43, 44
    }
}

Settings fromParams (const EffectParams& params) noexcept
{
    Settings s;
    for (size_t k = 0; k < (size_t) luxeVerbControlCount; ++k)
    {
        const auto rg = ranges[k];
        s.raw[k] = rg.lo + (int) std::lround (std::clamp (params[k], 0.0f, 1.0f) * (float) (rg.hi - rg.lo));
    }
    for (size_t j = 0; j < 4; ++j) s.sync[j] = params[(size_t) luxeVerbControlCount + j] >= 0.5f;
    return s;
}

void toParams (const Settings& s, EffectParams& params) noexcept
{
    for (size_t k = 0; k < (size_t) luxeVerbControlCount; ++k)
    {
        const auto rg = ranges[k];
        const int v = std::clamp (s.raw[k], rg.lo, rg.hi);
        params[k] = (float) (v - rg.lo) / (float) (rg.hi - rg.lo);
    }
    for (size_t j = 0; j < 4; ++j) params[(size_t) luxeVerbControlCount + j] = s.sync[j] ? 1.0f : 0.0f;
}

std::optional<Settings> parseState (const std::vector<std::uint8_t>& d)
{
    // int version; int (page); bytes: predelay sync, feedback sync, 3 display switches; controls (int32 each:
    // 38 below version 8, 39 in version 8, 49 from version 9); version 9 adds the two feed-forward sync bytes
    if (d.size() < 13) return std::nullopt;
    auto i32 = [&d] (size_t o) { std::int32_t v; std::memcpy (&v, d.data() + o, 4); return v; };
    const int version = i32 (0);
    if (version < 7 || version >= 10) return std::nullopt;
    const int count = version < 8 ? 38 : version < 9 ? 39 : 49;
    const size_t need = 13 + 4 * (size_t) count + (version >= 9 ? 2 : 0);
    if (d.size() < need) return std::nullopt;
    Settings s;
    for (size_t k = 0; k < oldStateTail.size(); ++k) s.raw[39 + k] = oldStateTail[k];
    if (version < 8) s.raw[38] = 1; // limiter on
    s.sync[0] = d[8] != 0;
    s.sync[1] = d[9] != 0;
    for (int k = 0; k < count; ++k) s.raw[(size_t) k] = i32 (13 + 4 * (size_t) k);
    if (version >= 9)
    {
        s.sync[2] = d[13 + 4 * 49] != 0;
        s.sync[3] = d[13 + 4 * 49 + 1] != 0;
    }
    // older states: the plugin reads no feed-forward sync flags (not confirmed what it then uses; LYNX: off)
    return s;
}

// ------------------------------------------------------------------------------------------------- engine

namespace
{
// which of each delay pair the plugin re-sends with the tempo
constexpr int syncPair[4][2] = { { 5, 6 }, { 17, 18 }, { 45, 46 }, { 47, 48 } };
} // namespace

Engine::Engine() : d (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (int sampleRate, const Settings& s, double bpm)
{
    create (sampleRate);
    load (s, bpm);
}

void Engine::create (int sampleRate)
{
    CsrScope csr (0x8040u, 0x6000u);
    d = std::make_unique<Impl>();
    d->setTempo (120.0f); // (setRate sets 120 again; the tempo that counts is the one load() sends)
    d->setRate ((float) sampleRate);
}

void Engine::flush() noexcept
{
    CsrScope csr (0x8040u, 0x6000u);
    d->reset();
}

void Engine::load (const Settings& s, double bpm)
{
    CsrScope csr (0x8040u, 0x6000u);
    const float t = (float) bpm;
    for (int k = 0; k < luxeVerbControlCount; ++k) d->setParam (k, engineValue (k, s.raw[(size_t) k]));
    d->setTempo (t);
    for (size_t j = 0; j < 4; ++j)
    {
        const int k = syncPair[j][s.sync[j] ? 1 : 0];
        d->setParam (k, engineValue (k, s.raw[(size_t) k]));
    }
    cur = s;
    curBpm = bpm;
}

void Engine::set (const Settings& s, double bpm)
{
    if (s == cur && bpm == curBpm) return;
    CsrScope csr (0x8040u, 0x6000u);
    for (int k = 0; k < luxeVerbControlCount; ++k)
        if (s.raw[(size_t) k] != cur.raw[(size_t) k]) d->setParam (k, engineValue (k, s.raw[(size_t) k]));
    if (bpm != curBpm || s.sync != cur.sync)
    {
        d->setTempo ((float) bpm);
        for (size_t j = 0; j < 4; ++j)
        {
            const int k = syncPair[j][s.sync[j] ? 1 : 0];
            d->setParam (k, engineValue (k, s.raw[(size_t) k]));
        }
    }
    cur = s;
    curBpm = bpm;
}

void Engine::process (float* left, float* right, int n, const float* scLeft, const float* scRight) noexcept
{
    CsrScope csr (0xe040u);
    float in[2 * 256], sc[2 * 256], out[2 * 256];
    for (int o = 0; o < n; o += 256)
    {
        const int m = std::min (256, n - o);
        for (int i = 0; i < m; ++i)
        {
            in[2 * i] = left[o + i];
            in[2 * i + 1] = right[o + i];
            sc[2 * i] = scLeft != nullptr ? scLeft[o + i] : left[o + i];
            sc[2 * i + 1] = scRight != nullptr ? scRight[o + i] : right[o + i];
        }
        d->render (m, in, sc, out);
        for (int i = 0; i < m; ++i)
        {
            left[o + i] = out[2 * i];
            right[o + i] = out[2 * i + 1];
        }
    }
}
} // namespace luxeverb

// ------------------------------------------------------------------------------------------------- Space engine

namespace luxeverb::sp
{
const std::array<const char*, luxeVerbSpaceCount> names { { "Space engine", "Space style", "Space", "Decay rate", "Distance",
                                                             "Space brightness", "Space character", "Thickness", "Space width",
                                                             "Space pre-delay", "Space mix", "Low decay", "Low decay freq.",
                                                             "High decay", "High decay freq." } };
// off; Modern, 2.5 s, 100 %, 50 %, 0 %, 30 %, 0 %, 70 %, 0 ms, 30 %, ×1 below 200 Hz, ×1 above 4 kHz (the reference's defaults)
const std::array<float, luxeVerbSpaceCount> defaults { { 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.7f, 0.0f, 0.3f,
                                                         0.5f, 0.5f, 0.5f, 0.56370f } };
namespace
{
// "Space" knob → seconds; the knots are where the reference's own knob reads 0.5, 1, 2.5, 4 and 6 s (its saved values)
constexpr std::array<std::pair<double, double>, 7> spaceKnots { { { 0.0, 0.3 }, { 0.1286, 0.5 }, { 0.25, 1.0 }, { 0.5, 2.5 },
                                                                  { 0.7, 4.0 }, { 0.8444, 6.0 }, { 1.0, 10.0 } } };
}
double decaySeconds (float v) noexcept
{
    const double x = std::clamp ((double) v, 0.0, 1.0);
    for (std::size_t i = 1; i < spaceKnots.size(); ++i)
        if (x <= spaceKnots[i].first)
        {
            const auto [x0, y0] = spaceKnots[i - 1];
            const auto [x1, y1] = spaceKnots[i];
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        }
    return spaceKnots.back().second;
}
float decayValue (double seconds) noexcept
{
    const double y = std::clamp (seconds, spaceKnots.front().second, spaceKnots.back().second);
    for (std::size_t i = 1; i < spaceKnots.size(); ++i)
        if (y <= spaceKnots[i].second)
        {
            const auto [x0, y0] = spaceKnots[i - 1];
            const auto [x1, y1] = spaceKnots[i];
            return (float) (x0 + (x1 - x0) * (y - y0) / (y1 - y0));
        }
    return 1.0f;
}
double rateOf (float v) noexcept { return std::pow (4.0, 2.0 * std::clamp ((double) v, 0.0, 1.0) - 1.0); }
double predelayMs (float v) noexcept { const double x = std::clamp ((double) v, 0.0, 1.0); return 500.0 * x * x * x; }
double multOf (float v) noexcept { return rateOf (v); }
double lowHz (float v) noexcept { return 20.0 * std::pow (100.0, std::clamp ((double) v, 0.0, 1.0)); }
double highHz (float v) noexcept { return 500.0 * std::pow (40.0, std::clamp ((double) v, 0.0, 1.0)); }

std::string text (int index, float v)
{
    char b[48];
    const auto pct = [&] (double x) { std::snprintf (b, sizeof b, "%.0f%%", x); return std::string (b); };
    switch (index)
    {
        case engine: return v >= 0.5f ? "Space" : "LuxeVerb";
        case style: return v < 0.25f ? "Modern" : v < 0.75f ? "Vintage" : "Plate";
        case space: std::snprintf (b, sizeof b, "%.2f s", decaySeconds (v)); return b;
        case rate: return pct (100.0 * rateOf (v));
        case distance: case character: case width: case mix: return pct (100.0 * std::clamp (v, 0.0f, 1.0f));
        case brightness: case thickness: return pct (100.0 * (2.0 * std::clamp (v, 0.0f, 1.0f) - 1.0));
        case predelay: std::snprintf (b, sizeof b, predelayMs (v) < 10.0 ? "%.1f ms" : "%.0f ms", predelayMs (v)); return b;
        case lowMult: case highMult: std::snprintf (b, sizeof b, "x%.2f", multOf (v)); return b;
        case lowFreq: std::snprintf (b, sizeof b, "%.0f Hz", lowHz (v)); return b;
        case highFreq: std::snprintf (b, sizeof b, highHz (v) < 1000.0 ? "%.0f Hz" : "%.1f kHz", highHz (v) < 1000.0 ? highHz (v) : highHz (v) / 1000.0); return b;
        default: return {};
    }
}

static space::Settings settingsOf (const EffectParams& p) noexcept
{
    const auto at = [&] (int i) { return p[(std::size_t) (luxeVerbSpaceFirst + i)]; };
    space::Settings s;
    s.style = at (style) < 0.25f ? space::Style::modern : at (style) < 0.75f ? space::Style::vintage : space::Style::plate;
    s.decaySec = decaySeconds (at (space));
    s.rate = rateOf (at (rate));
    s.distance = at (distance);
    s.brightness = 2.0 * at (brightness) - 1.0;
    s.character = at (character);
    s.thickness = 2.0 * at (thickness) - 1.0;
    s.width = at (width);
    s.predelayMs = predelayMs (at (predelay));
    s.mix = at (mix);
    s.lowMult = multOf (at (lowMult));
    s.lowHz = lowHz (at (lowFreq));
    s.highMult = multOf (at (highMult));
    s.highHz = highHz (at (highFreq));
    return s;
}
} // namespace luxeverb::sp

// ------------------------------------------------------------------------------------------------- effect slot

namespace
{
class LuxeVerbEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        rate = (int) std::lround (sampleRate);
        engine.create (rate > 0 ? rate : 44100); // allocates the lines here, not in the audio callback
        spaceEngine.prepare (rate > 0 ? rate : 44100);
        created = true;
        ready = false;
    }
    void reset() noexcept override
    {
        if (created) engine.flush();
        spaceEngine.reset();
    }
    void setSidechain (const float* l, const float* r) noexcept override
    {
        // called only when the slot has a side input: a silent source is silence (FL hands the plugin its buffer)
        scL = l != nullptr ? l : zeros.data();
        scR = r != nullptr ? r : zeros.data();
    }
    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        if (params[(std::size_t) luxeVerbSpaceFirst] >= 0.5f) // the Space engine (round tp172b)
        {
            if (! spaceOn)
                spaceEngine.reset();
            spaceOn = true;
            spaceEngine.set (luxeverb::sp::settingsOf (params));
            spaceEngine.process (left, right, n);
            scL = scR = nullptr;
            return;
        }
        if (spaceOn && created)
            engine.flush();
        spaceOn = false;
        const auto s = luxeverb::fromParams (params);
        const double t = bpm > 0.0 ? bpm : 120.0;
        if (! ready)
        {
            if (! created)
            {
                engine.create (rate);
                created = true;
            }
            engine.load (s, t);
            ready = true;
        }
        else engine.set (s, t);
        if (scL == zeros.data() && (int) zeros.size() < n) // longer than the silence buffer: follow the input
            scL = scR = nullptr;
        engine.process (left, right, n, scL, scR);
        scL = scR = nullptr;
    }

private:
    luxeverb::Engine engine;
    space::Engine spaceEngine;
    bool spaceOn = false;
    int rate = 44100;
    bool created = false, ready = false;
    const float* scL = nullptr;
    const float* scR = nullptr;
    std::vector<float> zeros = std::vector<float> (8192, 0.0f);
};

std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof b, f, v);
    return b;
}
} // namespace

std::unique_ptr<Effect> makeLuxeVerb() { return std::make_unique<LuxeVerbEffect>(); }

std::optional<EffectSlot> luxeVerbFromFl (const std::vector<std::uint8_t>& state)
{
    const auto s = luxeverb::parseState (state);
    if (! s) return std::nullopt;
    EffectSlot slot;
    slot.kind = EffectKind::luxeVerb;
    slot.params = {};
    luxeverb::toParams (*s, slot.params);
    for (int i = 0; i < luxeVerbSpaceCount; ++i) // the Space engine stays off, at its defaults
        slot.params[(std::size_t) (luxeVerbSpaceFirst + i)] = luxeverb::sp::defaults[(std::size_t) i];
    return slot;
}

std::string luxeVerbParamText (int param, float value01)
{
    if (param >= luxeVerbSpaceFirst) return luxeverb::sp::text (param - luxeVerbSpaceFirst, value01);
    if (param >= luxeVerbControlCount) return value01 >= 0.5f ? "on" : "off";
    if (param < 0) return {};
    const auto rg = luxeverb::ranges[(size_t) param];
    const int raw = rg.lo + (int) std::lround (std::clamp (value01, 0.0f, 1.0f) * (float) (rg.hi - rg.lo));
    const float v = luxeverb::engineValue (param, raw);
    switch (param)
    {
        case 0: case 32: case 33: return fmt ("%.1f dB", v);
        case 1: case 2: case 20: case 21: case 30: case 34: return fmt ("%.0f Hz", v);
        case 4: return fmt ("%.2f s", v);
        case 5: case 17: case 45: case 47: return fmt ("%.1f ms", v);
        case 31: return fmt ("%.1f ms", v);
        case 6: case 18: case 46: case 48: return fmt ("%.2f / 16", v);
        case 9: return fmt ("%.0f m", v);
        case 10: return fmt ("%.2f Hz", v);
        case 13: return raw == 0 ? "normal" : raw == 1 ? "freeze" : "sustain";
        case 28: return raw == 0 ? "env → off" : raw == 1 ? "env → wet" : "env → decay";
        case 16: case 41: case 42: return fmt ("%+.1f st", v);
        case 24: return fmt ("%.2f dB", v);
        case 25: case 26: return fmt ("%.0f ms", v);
        case 3: case 14: case 15: case 29: case 38: return raw != 0 ? "on" : "off";
        case 39: return raw != 0 ? "feed-forward" : "feedback";
        default: return fmt ("%.2f", v);
    }
}
} // namespace lynx
