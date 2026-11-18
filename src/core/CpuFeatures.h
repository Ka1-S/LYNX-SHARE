#pragma once
// Runtime CPU dispatch for the hottest DSP loops (round 190kz, codegen). The release builds keep the x86-64 baseline
// (SSE2) so every 64-bit CPU runs LYNX; a few loops also have AVX / AVX2 variants compiled with
// __attribute__ ((target (...))) and picked here at run time. Rules for such a variant (docs/PLATFORMS.md "Runtime
// CPU dispatch"): the same IEEE operations in the same order per element, only in wider vectors: never "fma" in the
// target list (a fused multiply-add rounds once instead of twice), no reassociated sums, no approximations. So every
// path renders the same bits; tests pin that for each dispatched loop.
//
// cpu::level() is computed once: what the CPU and the OS support (CPUID, and XGETBV for the AVX register state),
// capped by the environment variable LYNX_CPU=sse2|sse4|avx|avx2 (tests and benchmarks: hash both paths on one
// machine). cpu::setLevelForTest caps it from code (tests); it never raises it above the hardware.
#include <atomic>
#include <cstdlib>
#include <cstring>
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
 #include <cpuid.h>
 #define LYNX_CPU_DISPATCH 1
#else
 #define LYNX_CPU_DISPATCH 0
#endif

namespace lynx::cpu
{
enum Level : int
{
    baseline = 0, // not x86, or no dispatch: the portable code only
    sse2 = 1,
    sse41 = 2,
    avx = 3,
    avx2 = 4
};

// what the hardware and the OS support (no environment cap)
inline int detectLevel() noexcept
{
#if LYNX_CPU_DISPATCH
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (! __get_cpuid (1, &a, &b, &c, &d))
        return sse2;
    int level = sse2;
    if ((c & (1u << 19)) != 0) // SSE4.1
        level = sse41;
    else
        return level;
    if ((c & (1u << 27)) == 0 || (c & (1u << 28)) == 0) // OSXSAVE, AVX
        return level;
    unsigned lo = 0, hi = 0;
    __asm__ __volatile__ ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    if ((lo & 6u) != 6u) // the OS saves the XMM and YMM state
        return level;
    level = avx;
    if (__get_cpuid_max (0, nullptr) >= 7)
    {
        __cpuid_count (7, 0, a, b, c, d);
        if ((b & (1u << 5)) != 0) // AVX2
            level = avx2;
    }
    return level;
#else
    return baseline;
#endif
}

// LYNX_CPU=sse2|sse4|avx|avx2 (also baseline / scalar = no dispatched path at all); unset or unknown: no cap
inline int environmentCap() noexcept
{
    const char* v = std::getenv ("LYNX_CPU");
    if (v == nullptr || *v == 0)
        return avx2;
    if (std::strcmp (v, "baseline") == 0 || std::strcmp (v, "scalar") == 0)
        return baseline;
    if (std::strcmp (v, "sse2") == 0)
        return sse2;
    if (std::strcmp (v, "sse4") == 0 || std::strcmp (v, "sse41") == 0)
        return sse41;
    if (std::strcmp (v, "avx") == 0)
        return avx;
    return avx2;
}

inline std::atomic<int>& levelSlot() noexcept
{
    static std::atomic<int> slot { -1 };
    return slot;
}

inline int hardwareLevel() noexcept
{
    static const int hw = detectLevel();
    return hw;
}

// the level the dispatched loops use
inline int level() noexcept
{
    auto v = levelSlot().load (std::memory_order_relaxed);
    if (v < 0)
    {
        const auto hw = hardwareLevel(), cap = environmentCap();
        v = hw < cap ? hw : cap;
        levelSlot().store (v, std::memory_order_relaxed);
    }
    return v;
}

inline bool hasSse41() noexcept { return level() >= sse41; }
inline bool hasAvx() noexcept { return level() >= avx; }
inline bool hasAvx2() noexcept { return level() >= avx2; }

// tests: run the dispatched loops at most at `cap` (never above the hardware); -1 = back to the hardware / LYNX_CPU
inline void setLevelForTest (int cap) noexcept
{
    levelSlot().store (-1, std::memory_order_relaxed);
    if (cap < 0)
        return;
    const auto v = level();
    levelSlot().store (cap < v ? cap : v, std::memory_order_relaxed);
}

inline const char* levelName (int l) noexcept
{
    switch (l)
    {
        case sse2: return "sse2";
        case sse41: return "sse4.1";
        case avx: return "avx";
        case avx2: return "avx2";
        default: return "baseline";
    }
}
} // namespace lynx::cpu

// LYNX_TARGET_AVX2 / LYNX_TARGET_AVX: the attribute of a dispatched variant (empty where there is no dispatch)
#if LYNX_CPU_DISPATCH
 #define LYNX_TARGET_AVX2 __attribute__ ((target ("avx2")))
 #define LYNX_TARGET_AVX __attribute__ ((target ("avx")))
 #define LYNX_TARGET_SSE41 __attribute__ ((target ("sse4.1")))
#else
 #define LYNX_TARGET_AVX2
 #define LYNX_TARGET_AVX
 #define LYNX_TARGET_SSE41
#endif
