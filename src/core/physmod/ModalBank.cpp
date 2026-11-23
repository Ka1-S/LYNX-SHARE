// LYNX Model's modal core (core/physmod/ModalBank.h).

#include "core/physmod/ModalBank.h"

#include "core/CpuFeatures.h"
#include "core/Simd.h"

namespace lynx::lm
{
namespace
{
// one group of 8 modes over n samples: the per-lane outputs added into lanes[i * 8 + lane] (first = store)
template <bool driven, bool first>
void groupSse (ModalBank& b, int g, const float* force, float* lanes, int n) noexcept
{
    const int o = g * laneGroup;
    __m128 re0 = _mm_load_ps (b.re + o), re1 = _mm_load_ps (b.re + o + 4);
    __m128 im0 = _mm_load_ps (b.im + o), im1 = _mm_load_ps (b.im + o + 4);
    const __m128 pr0 = _mm_load_ps (b.pr + o), pr1 = _mm_load_ps (b.pr + o + 4);
    const __m128 pi0 = _mm_load_ps (b.pi + o), pi1 = _mm_load_ps (b.pi + o + 4);
    const __m128 d0 = _mm_load_ps (b.drive + o), d1 = _mm_load_ps (b.drive + o + 4);
    for (int i = 0; i < n; ++i)
    {
        __m128 nr0 = _mm_sub_ps (_mm_mul_ps (re0, pr0), _mm_mul_ps (im0, pi0));
        __m128 nr1 = _mm_sub_ps (_mm_mul_ps (re1, pr1), _mm_mul_ps (im1, pi1));
        const __m128 ni0 = _mm_add_ps (_mm_mul_ps (re0, pi0), _mm_mul_ps (im0, pr0));
        const __m128 ni1 = _mm_add_ps (_mm_mul_ps (re1, pi1), _mm_mul_ps (im1, pr1));
        if constexpr (driven)
        {
            const __m128 f = _mm_set1_ps (force[i]);
            nr0 = _mm_add_ps (nr0, _mm_mul_ps (d0, f));
            nr1 = _mm_add_ps (nr1, _mm_mul_ps (d1, f));
        }
        re0 = nr0; re1 = nr1; im0 = ni0; im1 = ni1;
        float* l = lanes + i * laneGroup;
        if constexpr (first)
        {
            _mm_store_ps (l, ni0);
            _mm_store_ps (l + 4, ni1);
        }
        else
        {
            _mm_store_ps (l, _mm_add_ps (_mm_load_ps (l), ni0));
            _mm_store_ps (l + 4, _mm_add_ps (_mm_load_ps (l + 4), ni1));
        }
    }
    _mm_store_ps (b.re + o, re0); _mm_store_ps (b.re + o + 4, re1);
    _mm_store_ps (b.im + o, im0); _mm_store_ps (b.im + o + 4, im1);
}

// the 8 lanes of each sample summed in a fixed order: s = lanes 0-3 + lanes 4-7, then (s0 + s2) + (s1 + s3)
void sumLanes (const float* lanes, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float* l = lanes + i * laneGroup;
        const float s0 = l[0] + l[4], s1 = l[1] + l[5], s2 = l[2] + l[6], s3 = l[3] + l[7];
        out[i] += (s0 + s2) + (s1 + s3);
    }
}

template <bool driven>
void runSse (ModalBank& b, const float* force, float* out, int n) noexcept
{
    alignas (32) float lanes[maxBlock * laneGroup];
    for (int g = 0; g < b.groups; ++g)
        g == 0 ? groupSse<driven, true> (b, g, force, lanes, n) : groupSse<driven, false> (b, g, force, lanes, n);
    sumLanes (lanes, out, n);
}

#if LYNX_CPU_DISPATCH
template <bool driven, bool first>
__attribute__ ((target ("avx"))) void groupAvx (ModalBank& b, int g, const float* force, float* lanes, int n) noexcept
{
    const int o = g * laneGroup;
    __m256 re = _mm256_load_ps (b.re + o), im = _mm256_load_ps (b.im + o);
    const __m256 pr = _mm256_load_ps (b.pr + o), pi = _mm256_load_ps (b.pi + o), d = _mm256_load_ps (b.drive + o);
    for (int i = 0; i < n; ++i)
    {
        __m256 nr = _mm256_sub_ps (_mm256_mul_ps (re, pr), _mm256_mul_ps (im, pi));
        const __m256 ni = _mm256_add_ps (_mm256_mul_ps (re, pi), _mm256_mul_ps (im, pr));
        if constexpr (driven)
            nr = _mm256_add_ps (nr, _mm256_mul_ps (d, _mm256_set1_ps (force[i])));
        re = nr; im = ni;
        float* l = lanes + i * laneGroup;
        if constexpr (first)
            _mm256_store_ps (l, ni);
        else
            _mm256_store_ps (l, _mm256_add_ps (_mm256_load_ps (l), ni));
    }
    _mm256_store_ps (b.re + o, re);
    _mm256_store_ps (b.im + o, im);
}

// two groups at once: two independent recurrences in flight (one group alone is bound by its own latency); the lanes
// get (lanes + group g) + group g + 1, the order the one-group loop adds them in, so the bits stay the same
template <bool driven, bool first>
__attribute__ ((target ("avx"))) void pairAvx (ModalBank& b, int g, const float* force, float* lanes, int n) noexcept
{
    const int o = g * laneGroup, o2 = o + laneGroup;
    __m256 re = _mm256_load_ps (b.re + o), im = _mm256_load_ps (b.im + o);
    __m256 re2 = _mm256_load_ps (b.re + o2), im2 = _mm256_load_ps (b.im + o2);
    const __m256 pr = _mm256_load_ps (b.pr + o), pi = _mm256_load_ps (b.pi + o), d = _mm256_load_ps (b.drive + o);
    const __m256 pr2 = _mm256_load_ps (b.pr + o2), pi2 = _mm256_load_ps (b.pi + o2), d2 = _mm256_load_ps (b.drive + o2);
    for (int i = 0; i < n; ++i)
    {
        __m256 nr = _mm256_sub_ps (_mm256_mul_ps (re, pr), _mm256_mul_ps (im, pi));
        const __m256 ni = _mm256_add_ps (_mm256_mul_ps (re, pi), _mm256_mul_ps (im, pr));
        __m256 nr2 = _mm256_sub_ps (_mm256_mul_ps (re2, pr2), _mm256_mul_ps (im2, pi2));
        const __m256 ni2 = _mm256_add_ps (_mm256_mul_ps (re2, pi2), _mm256_mul_ps (im2, pr2));
        if constexpr (driven)
        {
            const __m256 f = _mm256_set1_ps (force[i]);
            nr = _mm256_add_ps (nr, _mm256_mul_ps (d, f));
            nr2 = _mm256_add_ps (nr2, _mm256_mul_ps (d2, f));
        }
        re = nr; im = ni; re2 = nr2; im2 = ni2;
        float* l = lanes + i * laneGroup;
        if constexpr (first)
            _mm256_store_ps (l, _mm256_add_ps (ni, ni2));
        else
            _mm256_store_ps (l, _mm256_add_ps (_mm256_add_ps (_mm256_load_ps (l), ni), ni2));
    }
    _mm256_store_ps (b.re + o, re);
    _mm256_store_ps (b.im + o, im);
    _mm256_store_ps (b.re + o2, re2);
    _mm256_store_ps (b.im + o2, im2);
}

template <bool driven>
__attribute__ ((target ("avx"))) void runAvx (ModalBank& b, const float* force, float* out, int n) noexcept
{
    alignas (32) float lanes[maxBlock * laneGroup];
    int g = 0;
    for (; g + 1 < b.groups; g += 2)
        g == 0 ? pairAvx<driven, true> (b, g, force, lanes, n) : pairAvx<driven, false> (b, g, force, lanes, n);
    if (g < b.groups)
        g == 0 ? groupAvx<driven, true> (b, g, force, lanes, n) : groupAvx<driven, false> (b, g, force, lanes, n);
    sumLanes (lanes, out, n);
}
#endif
} // namespace

bool modesUseAvx() noexcept
{
#if LYNX_CPU_DISPATCH
    return cpu::hasAvx();
#else
    return false;
#endif
}

void runModesBaseline (ModalBank& b, const float* force, float* out, int n) noexcept
{
    if (b.groups <= 0 || n <= 0)
        return;
    force != nullptr ? runSse<true> (b, force, out, n) : runSse<false> (b, force, out, n);
}

void runModes (ModalBank& b, const float* force, float* out, int n) noexcept
{
    if (b.groups <= 0 || n <= 0)
        return;
#if LYNX_CPU_DISPATCH
    if (cpu::hasAvx())
    {
        force != nullptr ? runAvx<true> (b, force, out, n) : runAvx<false> (b, force, out, n);
        return;
    }
#endif
    force != nullptr ? runSse<true> (b, force, out, n) : runSse<false> (b, force, out, n);
}

float modalEnergy (const ModalBank& b) noexcept
{
    float e = 0.0f;
    for (int k = 0; k < b.groups * laneGroup; ++k)
        e += b.re[k] * b.re[k] + b.im[k] * b.im[k];
    return e;
}
} // namespace lynx::lm
