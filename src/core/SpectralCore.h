#pragma once

// Round 190ko: the short-time Fourier frame both of LYNX's spectral effects run on (core/Timeprism.h, core/Harmonia.h).
// A stereo pair is transformed as one complex signal (left = real, right = imaginary), so one complex FFT per hop
// gives both channels' spectra. Hann analysis and synthesis windows, a hop of a quarter frame (the windows' squared sum
// is constant, 1.5, and is divided out), frame sizes 256 … 4096. All memory is taken in prepare(); process() does not
// allocate. LYNX's own code.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace lynx::spectral
{
using cpx = std::complex<float>;
constexpr double pi = 3.14159265358979323846;

// in-place radix-2 FFT of a power-of-two size with its own tables
class Fft
{
public:
    void prepare (int size)
    {
        n = size;
        twiddle.resize ((std::size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            twiddle[(std::size_t) i] = std::polar (1.0f, (float) (-2.0 * pi * i / n));
        rev.resize ((std::size_t) n);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            rev[(std::size_t) i] = r;
        }
    }
    int size() const noexcept { return n; }
    // forward (e^-iωt); inverse = conjugate twiddles, unscaled
    void run (cpx* a, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(std::size_t) i])
                std::swap (a[i], a[rev[(std::size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = twiddle[(std::size_t) (j * step)];
                    if (inverse)
                        w = std::conj (w);
                    const auto u = a[i + j], v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
    }

private:
    int n = 0;
    std::vector<cpx> twiddle;
    std::vector<int> rev;
};

// Splits the packed transform Z of (l + i r) into the two real signals' half spectra (bins 0 … n/2)
inline void unpack (const cpx* z, int n, cpx* left, cpx* right) noexcept
{
    for (int k = 0; k <= n / 2; ++k)
    {
        const auto a = z[k], b = std::conj (z[(n - k) & (n - 1)]);
        left[k] = 0.5f * (a + b);
        right[k] = cpx (0.0f, -0.5f) * (a - b);
    }
}
// the reverse: two half spectra → the packed full spectrum of (l + i r)
inline void pack (const cpx* left, const cpx* right, int n, cpx* z) noexcept
{
    for (int k = 0; k <= n / 2; ++k)
    {
        const auto zl = left[k], zr = right[k];
        z[k] = zl + cpx (0.0f, 1.0f) * zr;
        if (k > 0 && k < n / 2)
            z[n - k] = std::conj (zl) + cpx (0.0f, 1.0f) * std::conj (zr);
    }
}

// The stereo STFT: feed samples, a callback edits each frame's two half spectra (bins 0 … size/2) in place.
// Latency: size samples (the output of a frame is complete once the next frames have overlapped it).
class Stft
{
public:
    void prepare (int frameSize)
    {
        size = frameSize;
        hop = size / 4;
        fft.prepare (size);
        window.resize ((std::size_t) size);
        for (int i = 0; i < size; ++i)
            window[(std::size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
        inL.assign ((std::size_t) size, 0.0f);
        inR.assign ((std::size_t) size, 0.0f);
        outL.assign ((std::size_t) size * 2, 0.0f);
        outR.assign ((std::size_t) size * 2, 0.0f);
        work.assign ((std::size_t) size, {});
        specL.assign ((std::size_t) size / 2 + 1, {});
        specR.assign ((std::size_t) size / 2 + 1, {});
        reset();
    }
    void reset() noexcept
    {
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);
        std::fill (outL.begin(), outL.end(), 0.0f);
        std::fill (outR.begin(), outR.end(), 0.0f);
        fill = 0;
        readPos = 0;
    }
    int frameSize() const noexcept { return size; }
    int hopSize() const noexcept { return hop; }
    int bins() const noexcept { return size / 2 + 1; }
    int latency() const noexcept { return size; }

    // processes n samples in place; onFrame (cpx* left, cpx* right) edits the half spectra
    template <typename F>
    void process (float* l, float* r, int n, F&& onFrame) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            // the input history: the newest `size` samples, written at `fill` (a ring)
            inL[(std::size_t) fill] = l[i];
            inR[(std::size_t) fill] = r[i];
            fill = (fill + 1) % size;
            // the output: read the accumulator, clear behind
            const auto o = (std::size_t) readPos;
            l[i] = outL[o];
            r[i] = outR[o];
            outL[o] = 0.0f;
            outR[o] = 0.0f;
            readPos = (readPos + 1) % (size * 2);
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                frame (onFrame);
            }
        }
    }

private:
    template <typename F>
    void frame (F& onFrame) noexcept
    {
        for (int j = 0; j < size; ++j)
        {
            const auto s = (std::size_t) ((fill + j) % size);
            work[(std::size_t) j] = cpx (inL[s] * window[(std::size_t) j], inR[s] * window[(std::size_t) j]);
        }
        fft.run (work.data(), false);
        unpack (work.data(), size, specL.data(), specR.data());
        onFrame (specL.data(), specR.data());
        pack (specL.data(), specR.data(), size, work.data());
        fft.run (work.data(), true);
        // overlap-add: the frame's first sample lands `size` samples after the oldest input sample it holds, i.e. the
        // output is the input delayed by exactly `size` samples when nothing is changed
        const auto scale = 1.0f / ((float) size * 1.5f);
        for (int j = 0; j < size; ++j)
        {
            const auto w = window[(std::size_t) j] * scale;
            const auto o = (std::size_t) ((readPos + j) % (size * 2));
            outL[o] += work[(std::size_t) j].real() * w;
            outR[o] += work[(std::size_t) j].imag() * w;
        }
    }

    Fft fft;
    int size = 0, hop = 0, fill = 0, readPos = 0, sinceHop = 0;
    std::vector<float> window, inL, inR, outL, outR;
    std::vector<cpx> work, specL, specR;
};

// a small deterministic random generator (no allocation, same sequence on every platform)
struct Rand
{
    std::uint32_t s = 0x12345678u;
    std::uint32_t next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // 0 … 1
    float bi() noexcept { return 2.0f * uni() - 1.0f; }
};
} // namespace lynx::spectral
