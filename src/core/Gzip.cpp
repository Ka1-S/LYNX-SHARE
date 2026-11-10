#include "core/Gzip.h"

#include <algorithm>
#include <array>

#include "core/Inflate.h"

namespace lynx::gzip
{
namespace
{
const std::array<std::uint32_t, 256>& crcTable() noexcept
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> t {};
        for (std::uint32_t n = 0; n < 256; ++n)
        {
            auto c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}
std::uint32_t le32 (const std::uint8_t* p) noexcept { return p[0] | (p[1] << 8) | (p[2] << 16) | ((std::uint32_t) p[3] << 24); }
} // namespace

std::uint32_t crc32 (const std::uint8_t* data, std::size_t size, std::uint32_t crc) noexcept
{
    const auto& t = crcTable();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i)
        crc = t[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool isGzip (const std::uint8_t* data, std::size_t size) noexcept
{
    return size >= 18 && data[0] == 0x1F && data[1] == 0x8B && data[2] == 8;
}

std::optional<std::vector<std::uint8_t>> decompress (const std::uint8_t* data, std::size_t size, std::size_t maxSize)
{
    if (! isGzip (data, size))
        return std::nullopt;
    const auto flags = data[3];
    std::size_t pos = 10;
    if ((flags & 4) != 0) // FEXTRA
    {
        if (pos + 2 > size)
            return std::nullopt;
        pos += 2 + (std::size_t) (data[pos] | (data[pos + 1] << 8));
    }
    for (const auto bit : { 8, 16 }) // FNAME, FCOMMENT: zero-terminated
        if ((flags & bit) != 0)
        {
            while (pos < size && data[pos] != 0)
                ++pos;
            ++pos;
        }
    if ((flags & 2) != 0) // FHCRC
        pos += 2;
    if (pos + 8 > size)
        return std::nullopt;
    auto out = inflateZlib (data + pos, size - pos - 8, true, maxSize);
    if (! out)
        return std::nullopt;
    // the trailer follows the DEFLATE stream; its exact place is unknown to inflateZlib, so check it against the
    // file's last 8 bytes (single-member files, which is what DAWs write)
    const auto* tail = data + size - 8;
    if (le32 (tail + 4) != (std::uint32_t) out->size() || le32 (tail) != crc32 (out->data(), out->size()))
        return std::nullopt;
    return out;
}

std::vector<std::uint8_t> compressStored (const std::uint8_t* data, std::size_t size)
{
    std::vector<std::uint8_t> out { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0xFF };
    out.reserve (size + size / 65535 * 5 + 32);
    std::size_t pos = 0;
    do
    {
        const auto len = std::min<std::size_t> (65535, size - pos);
        const auto last = pos + len >= size;
        out.push_back (last ? 1 : 0);
        out.push_back ((std::uint8_t) (len & 0xFF));
        out.push_back ((std::uint8_t) (len >> 8));
        out.push_back ((std::uint8_t) (~len & 0xFF));
        out.push_back ((std::uint8_t) ((~len >> 8) & 0xFF));
        out.insert (out.end(), data + pos, data + pos + len);
        pos += len;
    } while (pos < size);
    const auto crc = crc32 (data, size);
    for (int k = 0; k < 4; ++k)
        out.push_back ((std::uint8_t) (crc >> (8 * k)));
    for (int k = 0; k < 4; ++k)
        out.push_back ((std::uint8_t) ((std::uint32_t) size >> (8 * k)));
    return out;
}
} // namespace lynx::gzip
