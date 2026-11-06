#pragma once

// gzip (RFC 1952) for project files of other programs (round 175a: Live sets are gzipped XML). Reading uses the
// DEFLATE decoder of core/Inflate.h; writing makes a valid gzip member with stored (uncompressed) DEFLATE blocks,
// which every reader accepts — no compressor needed (sets are XML; size is not a concern for an export).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lynx::gzip
{
bool isGzip (const std::uint8_t* data, std::size_t size) noexcept;
// The uncompressed bytes of the first member (nullopt: not gzip, corrupt or over `maxSize`; the CRC is checked).
std::optional<std::vector<std::uint8_t>> decompress (const std::uint8_t* data, std::size_t size, std::size_t maxSize = 512u << 20);
std::vector<std::uint8_t> compressStored (const std::uint8_t* data, std::size_t size);
std::uint32_t crc32 (const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) noexcept;
} // namespace lynx::gzip
