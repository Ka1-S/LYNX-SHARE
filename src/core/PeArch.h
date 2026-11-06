#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// The CPU a Windows module (DLL / EXE, a PE file) was built for, from its first bytes (round 176a: 32-bit plugins run
// in LYNX-bridge32.exe). Pure, so the core tests cover it.
namespace lynx::pearch
{
enum class Arch
{
    unknown, // not a PE file, or cut short
    x86,     // IMAGE_FILE_MACHINE_I386: a 32-bit plugin
    x64,     // IMAGE_FILE_MACHINE_AMD64
    arm64,   // IMAGE_FILE_MACHINE_ARM64 (and ARM64EC / ARM64X modules, which also run in an x64 process)
    other
};

// How many leading bytes `fromHeader` may need: the DOS header names where the PE header is (e_lfanew, usually
// 0x80-0x200); 4096 covers every linker in use.
constexpr std::size_t headerBytes = 4096;

inline Arch fromHeader (const std::uint8_t* data, std::size_t size) noexcept
{
    if (data == nullptr || size < 0x40 || data[0] != 'M' || data[1] != 'Z')
        return Arch::unknown;
    std::uint32_t peOffset = 0;
    std::memcpy (&peOffset, data + 0x3c, 4); // (little-endian on every platform LYNX builds for)
    if (peOffset > size || size - peOffset < 6 || std::memcmp (data + peOffset, "PE\0\0", 4) != 0)
        return Arch::unknown;
    const auto machine = (std::uint16_t) (data[peOffset + 4] | (data[peOffset + 5] << 8));
    switch (machine)
    {
        case 0x014c: return Arch::x86;
        case 0x8664: return Arch::x64;
        case 0xaa64: case 0xa641: return Arch::arm64;
        default: return Arch::other;
    }
}

// The module a VST3 bundle runs on this architecture (Contents/<folder>/<bundle name>), Steinberg's folder names.
inline const char* vst3Folder (Arch arch) noexcept
{
    switch (arch)
    {
        case Arch::x86: return "x86-win";
        case Arch::x64: return "x86_64-win";
        case Arch::arm64: return "arm64-win";
        default: return "";
    }
}
} // namespace lynx::pearch
