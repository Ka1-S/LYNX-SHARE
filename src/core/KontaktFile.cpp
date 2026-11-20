#include "core/KontaktFile.h"

#include <cstring>
#include <set>

namespace lynx::msimport::kontakt
{
namespace
{
std::uint32_t u32 (const std::uint8_t* p) { std::uint32_t v; std::memcpy (&v, p, 4); return v; }
std::uint64_t u64 (const std::uint8_t* p) { std::uint64_t v; std::memcpy (&v, p, 8); return v; }
std::uint32_t swap (std::uint32_t v) { return v >> 24 | (v >> 8 & 0xFF00) | (v << 8 & 0xFF0000) | v << 24; }

// The NI container (ConvertWithMoss' description): an item = u64 size (with itself), u32 1, "hsin", u32, u32, a
// 16-byte id, a chain of data chunks (each: u64 size, domain, u32 type, u32 version, then the next chunk unless
// this one is the terminator, type 1, then its own data), u32 version, u32 child count, the children (u32 index,
// domain, u32 type, an item). Collects the chunk types; false when the structure does not hold.
bool walkItem (const std::uint8_t* p, std::size_t n, std::set<std::uint32_t>& types, int depth, int& budget);

bool walkChunk (const std::uint8_t* p, std::size_t n, std::size_t& used, std::set<std::uint32_t>& types, int depth, int& budget)
{
    if (n < 20 || --budget < 0 || depth > 64)
        return false;
    const auto size = u64 (p);
    if (size < 20 || size > n)
        return false;
    const auto type = u32 (p + 12);
    types.insert (type);
    used = (std::size_t) size;
    if (type == 1)
        return true;
    std::size_t inner = 0;
    return walkChunk (p + 20, (std::size_t) size - 20, inner, types, depth + 1, budget);
}

bool walkItem (const std::uint8_t* p, std::size_t n, std::set<std::uint32_t>& types, int depth, int& budget)
{
    if (n < 40 || depth > 32 || --budget < 0)
        return false;
    const auto size = u64 (p);
    if (size < 40 || size > n || u32 (p + 8) != 1 || std::memcmp (p + 12, "hsin", 4) != 0)
        return false;
    std::size_t at = 40, used = 0;
    if (! walkChunk (p + at, (std::size_t) size - at, used, types, depth, budget))
        return false;
    at += used;
    if (at + 8 > size)
        return false;
    const auto children = u32 (p + at + 4);
    at += 8;
    for (std::uint32_t i = 0; i < children && i < 4096; ++i)
    {
        if (at + 12 > size)
            return false;
        at += 12;
        if (! walkItem (p + at, (std::size_t) size - at, types, depth + 1, budget))
            return false;
        at += (std::size_t) u64 (p + at);
    }
    return true;
}
} // namespace

Kind detect (const std::uint8_t* d, std::size_t n)
{
    if (n >= 16 && std::memcmp (d + 12, "hsin", 4) == 0)
        return Kind::container;
    if (n < 16)
        return Kind::unknown;
    for (const auto magic : { u32 (d), swap (u32 (d)) })
        switch (magic)
        {
            case 0x5EE56EB3u: return Kind::kontakt1;
            case 0x1290A87Fu:
            {
                // (the 4.2 header: version word 0x110 at 8)
                const auto v = (std::uint16_t) (d[8] | d[9] << 8);
                return v == 0x110 ? Kind::kontakt42 : Kind::kontakt2;
            }
            case 0x2F5C204Eu: return Kind::monolith;
            default: break;
        }
    return Kind::unknown;
}

bool protectedContainer (const std::uint8_t* d, std::size_t n)
{
    std::set<std::uint32_t> types;
    int budget = 100000;
    walkItem (d, n, types, 0, budget);
    return types.count (116) > 0; // an Encryption Item
}

std::optional<Program> readInstrument (const std::string& path, const Access& access, Report& report)
{
    report.instruments = 1;
    const auto head = access.read ? access.read (path, (std::size_t) 32 << 20) : std::vector<std::uint8_t> {};
    if (head.empty())
    {
        report.error = "The file could not be read (or is larger than 32 MB).";
        return std::nullopt;
    }
    switch (detect (head.data(), head.size()))
    {
        case Kind::container:
            report.error = protectedContainer (head.data(), head.size())
                               ? "This Kontakt instrument belongs to a protected (encrypted) library. LYNX SHARE does not open protected "
                                 "libraries; load Kontakt as a plugin to play it."
                               : "Kontakt 4.2 – 8 instruments are not imported yet. Load Kontakt as a plugin to play it; its NCW samples "
                                 "open in LYNX SHARE as audio files.";
            break;
        case Kind::monolith:
            report.error = "Kontakt monoliths (instrument and samples in one file) are not imported. Load Kontakt as a plugin to play it.";
            break;
        case Kind::kontakt1: case Kind::kontakt2: case Kind::kontakt42:
            report.error = "Kontakt 1 – 4.1 instruments are not imported yet. Load Kontakt as a plugin to play it; its NCW samples open "
                           "in LYNX SHARE as audio files.";
            break;
        default:
            report.error = "This is not a Kontakt instrument LYNX SHARE recognises.";
            break;
    }
    return std::nullopt;
}
} // namespace lynx::msimport::kontakt
