#pragma once

// One stage of a half-band polyphase FIR oversampler over a block of samples (round 190gp, after round 190ge's
// mixsuite::Oversampler): the same sums as one sample at a time, each output still adding its taps in the same order
// from 0, four (and eight) outputs side by side in SSE lanes. The stage's history is left as that many single steps
// leave it, so blocks of any size give the same samples, bit for bit.
//
// `Stage` holds: int taps (non-zero side taps, the centre excluded), centreDelay ((c − 1) / 2 in input samples),
// upPos / downPos, coef[taps] (h[2j]) and the rings upHist / evenHist / oddHist (size 2 · taps: mirrored at + taps, the
// newest sample at the position).

#include <algorithm>
#include <array>

#include "core/Simd.h"

namespace lynx::halfband
{
// acc[k] = Σ coef[j] · lin[L − 1 + k − j] over j = 0 … L − 1, summed in that order from 0, for k in [0, count)
inline void sums (const float* coef, int L, const float* lin, int count, float* acc) noexcept
{
    const int P = L - 1;
    int k = 0;
#if LYNX_X86
    for (; k + 8 <= count; k += 8)
    {
        auto a0 = _mm_setzero_ps(), a1 = _mm_setzero_ps();
        for (int j = 0; j < L; ++j)
        {
            const auto c = _mm_set1_ps (coef[j]);
            a0 = _mm_add_ps (a0, _mm_mul_ps (c, _mm_loadu_ps (lin + P + k - j)));
            a1 = _mm_add_ps (a1, _mm_mul_ps (c, _mm_loadu_ps (lin + P + k + 4 - j)));
        }
        _mm_storeu_ps (acc + k, a0);
        _mm_storeu_ps (acc + k + 4, a1);
    }
    for (; k + 4 <= count; k += 4)
    {
        auto a0 = _mm_setzero_ps();
        for (int j = 0; j < L; ++j)
            a0 = _mm_add_ps (a0, _mm_mul_ps (_mm_set1_ps (coef[j]), _mm_loadu_ps (lin + P + k - j)));
        _mm_storeu_ps (acc + k, a0);
    }
#endif
    for (; k < count; ++k)
    {
        float a = 0.0f;
        for (int j = 0; j < L; ++j)
            a += coef[j] * lin[P + k - j];
        acc[k] = a;
    }
}

constexpr int maxTaps = 64, maxBlock = 256;

// up: m samples in (m ≤ maxBlock), 2m out: y[2k] = 2 · the half-band of the input, y[2k + 1] = the input centreDelay
// samples back (as m calls of a per-sample up)
template <typename Stage>
void upBlock (Stage& s, const float* in, int m, float* out) noexcept
{
    const int L = s.taps, P = L - 1;
    std::array<float, maxTaps + maxBlock> lin;
    std::array<float, maxBlock> acc;
    for (int j = 0; j < P; ++j)
        lin[(std::size_t) (P - 1 - j)] = s.upHist[(std::size_t) (s.upPos + j)];
    std::copy (in, in + m, lin.data() + P);
    sums (s.coef.data(), L, lin.data(), m, acc.data());
    for (int k = 0; k < m; ++k)
    {
        out[2 * k] = 2.0f * acc[(std::size_t) k];
        out[2 * k + 1] = lin[(std::size_t) (P + k - s.centreDelay)];
    }
    s.upPos = ((s.upPos - m) % L + L) % L;
    for (int j = 0; j < L; ++j)
    {
        const auto v = lin[(std::size_t) (P + m - 1 - j)];
        const auto q = (s.upPos + j) % L;
        s.upHist[(std::size_t) q] = s.upHist[(std::size_t) (q + L)] = v;
    }
}

// down: m (even, odd) pairs in, m out: the half-band of the even samples + half the odd one centreDelay + 1 back
template <typename Stage>
void downBlock (Stage& s, const float* in, int m, float* out) noexcept
{
    const int L = s.taps, P = L - 1;
    std::array<float, maxTaps + maxBlock> evenLin, oddLin;
    std::array<float, maxBlock> acc;
    for (int j = 0; j < P; ++j)
    {
        evenLin[(std::size_t) (P - 1 - j)] = s.evenHist[(std::size_t) (s.downPos + j)];
        oddLin[(std::size_t) (P - 1 - j)] = s.oddHist[(std::size_t) (s.downPos + j)];
    }
    for (int k = 0; k < m; ++k)
    {
        evenLin[(std::size_t) (P + k)] = in[2 * k];
        oddLin[(std::size_t) (P + k)] = in[2 * k + 1];
    }
    sums (s.coef.data(), L, evenLin.data(), m, acc.data());
    for (int k = 0; k < m; ++k)
        out[k] = acc[(std::size_t) k] + 0.5f * oddLin[(std::size_t) (P + k - s.centreDelay - 1)];
    s.downPos = ((s.downPos - m) % L + L) % L;
    for (int j = 0; j < L; ++j)
    {
        const auto q = (s.downPos + j) % L;
        s.evenHist[(std::size_t) q] = s.evenHist[(std::size_t) (q + L)] = evenLin[(std::size_t) (P + m - 1 - j)];
        s.oddHist[(std::size_t) q] = s.oddHist[(std::size_t) (q + L)] = oddLin[(std::size_t) (P + m - 1 - j)];
    }
}
} // namespace lynx::halfband
