#pragma once

// Round 190px (perf 7): a block of floats whose address range is reserved at once but whose memory is only made real
// (committed on Windows, touched everywhere) as far as it is used. A delay line sized for its longest possible delay
// (Echoform: 20.6 s per line) then holds in RAM only what its actual delay reaches: the pages past that stay
// untouched, so they are neither in the working set nor (Windows) in the commit charge.
//
// The memory of a fresh block reads as zeros. commit() may be called from the audio thread (no allocation, no lock of
// LYNX's; on Windows one VirtualAlloc(MEM_COMMIT) call on a range already reserved). Header-only, no windows.h: the two
// Win32 functions are declared here with their exact Win64 signatures.

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
extern "C" __declspec (dllimport) void* __stdcall VirtualAlloc (void* address, unsigned long long size, unsigned long type, unsigned long protect);
extern "C" __declspec (dllimport) int __stdcall VirtualFree (void* address, unsigned long long size, unsigned long type);
#else
 #include <sys/mman.h>
#endif

namespace lynx
{
class LazyFloats
{
public:
    LazyFloats() = default;
    ~LazyFloats() { release(); }
    LazyFloats (const LazyFloats&) = delete;
    LazyFloats& operator= (const LazyFloats&) = delete;
    LazyFloats (LazyFloats&& o) noexcept { take (o); }
    LazyFloats& operator= (LazyFloats&& o) noexcept
    {
        if (this != &o)
        {
            release();
            take (o);
        }
        return *this;
    }

    // Reserves `count` floats (off the audio thread). False: the system refused (the caller falls back to a vector).
    bool reserve (std::size_t count) noexcept
    {
        release();
        if (count == 0)
            return false;
        const auto bytes = count * sizeof (float);
#if defined(_WIN32)
        void* at = VirtualAlloc (nullptr, (unsigned long long) bytes, 0x2000u /* MEM_RESERVE */, 0x01u /* PAGE_NOACCESS */);
        if (at == nullptr)
            return false;
#else
        void* at = mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
 #if defined(MAP_NORESERVE)
                                                                     | MAP_NORESERVE
 #endif
                         , -1, 0);
        if (at == MAP_FAILED)
            return false;
#endif
        data_ = static_cast<float*> (at);
        reserved = count;
        committed = 0;
        return true;
    }

    // Makes the first `count` floats usable (they read as zeros the first time). False: the system refused (Windows:
    // the commit limit); what was usable stays usable.
    bool commit (std::size_t count) noexcept
    {
        if (count > reserved)
            count = reserved;
        if (count <= committed)
            return true;
#if defined(_WIN32)
        if (VirtualAlloc (data_, (unsigned long long) (count * sizeof (float)), 0x1000u /* MEM_COMMIT */, 0x04u /* PAGE_READWRITE */) == nullptr)
            return false;
#endif
        committed = count;
        return true;
    }

    float* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return reserved; }
    std::size_t usable() const noexcept { return committed; }

private:
    void release() noexcept
    {
        if (data_ != nullptr)
        {
#if defined(_WIN32)
            VirtualFree (data_, 0, 0x8000u /* MEM_RELEASE */);
#else
            munmap (data_, reserved * sizeof (float));
#endif
        }
        data_ = nullptr;
        reserved = committed = 0;
    }
    void take (LazyFloats& o) noexcept
    {
        data_ = o.data_;
        reserved = o.reserved;
        committed = o.committed;
        o.data_ = nullptr;
        o.reserved = o.committed = 0;
    }

    float* data_ = nullptr;
    std::size_t reserved = 0, committed = 0;
};
} // namespace lynx
