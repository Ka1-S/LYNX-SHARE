#pragma once

// Round 190ks (Discord: "can plugins of the same type share resources?"): the same audio loaded twice from two files
// (a kit copied into the project folder and still in the library, two sound banks that are byte copies) is kept once.
// A 64-bit content hash finds the candidates; the caller compares the content itself before sharing, so a collision never
// shares the wrong data. Any thread; no allocation.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace lynx::contentshare
{
// on unless the environment says LYNX_CONTENT_SHARE=0 (the before / after measurement of --sharetest; read once)
inline bool enabled() noexcept
{
    static const bool on = [] {
        const char* e = std::getenv ("LYNX_CONTENT_SHARE");
        return e == nullptr || e[0] != '0';
    }();
    return on;
}

// multiply-xorshift mixing over 8-byte words (a fingerprint, not a cryptographic hash; portable, no 128-bit arithmetic):
// a few GB/s, so a minute of stereo float audio (21 MB) costs a few ms once at load
inline std::uint64_t hashBytes (const void* data, std::size_t size, std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept
{
    auto mix = [] (std::uint64_t a, std::uint64_t b) noexcept {
        a *= b;
        return a ^ (a >> 29);
    };
    const auto* p = static_cast<const unsigned char*> (data);
    auto h = seed ^ (size * 0xA0761D6478BD642Full);
    std::size_t i = 0;
    for (; i + 8 <= size; i += 8)
    {
        std::uint64_t w;
        std::memcpy (&w, p + i, 8);
        h = mix (h ^ w, 0xE7037ED1A0B428DBull);
    }
    std::uint64_t tail = 0;
    if (i < size)
        std::memcpy (&tail, p + i, size - i);
    return mix (h ^ tail, 0x8EBC6AF09C88C6E3ull) ^ size;
}
} // namespace lynx::contentshare
