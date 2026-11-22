#pragma once

#include <cstdlib>

// Enables flush-to-zero/denormals-are-zero on the thread while the object exists. Reverb tails,
// filters and delays decay into denormal numbers, which on x86 cost tens of times more
// per operation; zeroing them changes nothing audible.
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE, or ARM64 through core/Simd.h (sse2neon)
 #include "core/Simd.h"
#endif

namespace lynx
{
// Round 190po: the x87 unit at 64-bit precision on the calling thread while the object exists (precision control 11;
// rounding and exception masks kept). LYNX's FL-exact engines keep sums and one-pole states in long double (Fruity
// Limiter / Compressor, Delay 2 / 3, Reeverb 2, Harmless's effects...): the 80-bit x87 format at the 64-bit precision
// FL runs it at (control word 0x033F) and Linux / macOS give every thread (0x037F). On Windows x64 only the main thread
// starts so (MinGW's start-up runs fninit); every other thread — the render workers, the render-ahead and audio device
// threads, an Export window's thread — starts at the system's 0x027F, 53-bit precision, and Windows' thread start-up
// restores that after the TLS callbacks (NtContinue with the initial context), so it is set here, where LYNX renders.
// The rack's task graph hands a unit to whichever thread is free: in `LYNX --export` (the render on the main thread) a
// unit ran at 64 bits in one block and at 53 in the next, and two exports of one project differed run to run (round
// 190pd: Exoplanet, three hashes from one exe; Linux stable). Only the Zig / MinGW Windows build does anything here
// (MSVC's long double is a double; elsewhere the threads already have it). LYNX_X87_DEFAULT=1 leaves the thread's mode
// as it is (the A/B of --selftest's determinism190po item: a determinism diagnostic only).
class ScopedExtendedPrecision
{
public:
    ScopedExtendedPrecision() noexcept
    {
#if defined(_WIN32) && defined(__x86_64__) && defined(__GNUC__)
        __asm__ volatile ("fnstcw %0" : "=m" (saved));
        if ((saved & 0x0300u) != 0x0300u && ! leaveDefault())
        {
            const auto extended = (unsigned short) (saved | 0x0300u);
            __asm__ volatile ("fldcw %0" : : "m" (extended));
            changed = true;
        }
#endif
    }

    ~ScopedExtendedPrecision()
    {
#if defined(_WIN32) && defined(__x86_64__) && defined(__GNUC__)
        if (changed)
            __asm__ volatile ("fnclex\n\tfldcw %0" : : "m" (saved));
#endif
    }

    ScopedExtendedPrecision (const ScopedExtendedPrecision&) = delete;
    ScopedExtendedPrecision& operator= (const ScopedExtendedPrecision&) = delete;

    // the calling thread's x87 control word (0x037F where LYNX uses no x87 unit: MSVC, ARM64)
    static unsigned controlWord() noexcept
    {
#if defined(__x86_64__) && defined(__GNUC__)
        unsigned short cw = 0;
        __asm__ volatile ("fnstcw %0" : "=m" (cw));
        return cw;
#else
        return 0x037Fu;
#endif
    }

    static bool leaveDefault() noexcept
    {
        static const bool leave = [] {
            const char* e = std::getenv ("LYNX_X87_DEFAULT");
            return e != nullptr && e[0] == '1';
        }();
        return leave;
    }

private:
    unsigned short saved = 0;
    bool changed = false;
};

class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE, or ARM64 through core/Simd.h (sse2neon)
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u); // FTZ (bit 15) + DAZ (bit 6)
#endif
    }

    ~ScopedFlushDenormals()
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE, or ARM64 through core/Simd.h (sse2neon)
        _mm_setcsr (saved);
#endif
    }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
    // (round 190po: every scope that renders also gets the x87's 64-bit precision; see ScopedExtendedPrecision)
    ScopedExtendedPrecision precision;
    unsigned int saved = 0;
};

// Keeps this thread's floating-point mode (MXCSR, and the x87 control word on x86-64 GCC/Clang) across foreign code —
// a hosted plugin's process call (round 165i). Some plugins change it (Delphi-built ones load their own default;
// others turn flush-to-zero off), which then slowed down or changed LYNX's own DSP for the rest of the block. FL Studio
// does the same: it re-asserts its x87 control word and MXCSR (FTZ, plus DAZ when the CPU has it) at 71 places of
// FLEngine, e.g. before its render lock (REA_rodada165i). Restores only when the mode changed (ldmxcsr is not free).
class KeepFloatMode
{
public:
    KeepFloatMode() noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64)
        csr = _mm_getcsr();
#endif
#if defined(__x86_64__) && defined(__GNUC__)
        __asm__ volatile ("fnstcw %0" : "=m" (cw));
#endif
    }

    ~KeepFloatMode()
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64)
        if (_mm_getcsr() != csr)
            _mm_setcsr (csr);
#endif
#if defined(__x86_64__) && defined(__GNUC__)
        unsigned short now = 0;
        __asm__ volatile ("fnstcw %0" : "=m" (now));
        if (now != cw)
            __asm__ volatile ("fnclex\n\tfldcw %0" : : "m" (cw));
#endif
    }

    KeepFloatMode (const KeepFloatMode&) = delete;
    KeepFloatMode& operator= (const KeepFloatMode&) = delete;

private:
    unsigned int csr = 0;
    unsigned short cw = 0;
};

// A thread's floating-point mode, to give it to helper threads (round 165i): work split over threads then rounds and
// flushes exactly like it would on the thread that asked for it
class FloatMode
{
public:
    static FloatMode current() noexcept
    {
        FloatMode m;
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64)
        m.csr = _mm_getcsr();
#endif
#if defined(__x86_64__) && defined(__GNUC__)
        __asm__ volatile ("fnstcw %0" : "=m" (m.cw));
#endif
        return m;
    }

    void applyHere() const noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64)
        _mm_setcsr (csr);
#endif
#if defined(__x86_64__) && defined(__GNUC__)
        __asm__ volatile ("fnclex\n\tfldcw %0" : : "m" (cw));
#endif
    }

private:
    unsigned int csr = 0;
    unsigned short cw = 0;
};
} // namespace lynx
