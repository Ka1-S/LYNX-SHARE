#pragma once

// Finds a block of an installation's data file wherever a build put it (round 190rh): the source holds the block's length, a
// 64-bit hash (FNV-1a) of its first 32 bytes and its SHA-256, never the data. Only an exact match of the whole block counts.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/Sha256.h"

namespace lynx
{
struct DataBlock
{
    std::uint64_t anchor;
    std::size_t length;
    const char* sha256;
};

// the offset of the block in `b` (2-byte aligned), or −1
inline std::ptrdiff_t locateBlock (const std::vector<std::uint8_t>& b, const DataBlock& block)
{
    if (b.size() < block.length || block.length < 32)
        return -1;
    for (std::size_t at = 0; at + block.length <= b.size(); at += 2)
    {
        std::uint64_t h = 0xcbf29ce484222325ull;
        for (std::size_t k = 0; k < 32; ++k)
            h = (h ^ b[at + k]) * 0x100000001b3ull;
        if (h == block.anchor && sha256Hex (b.data() + at, block.length) == block.sha256)
            return (std::ptrdiff_t) at;
    }
    return -1;
}
} // namespace lynx
