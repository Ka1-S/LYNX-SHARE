#include "core/ZipLite.h"

#include <algorithm>
#include <cctype>

#include "core/Gzip.h"
#include "core/Inflate.h"

// Record layouts (APPNOTE.TXT §4.3): little-endian throughout.
//   local file header    0x04034b50, 30 bytes + name + extra
//   central directory    0x02014b50, 46 bytes + name + extra + comment
//   end of central dir   0x06054b50, 22 bytes + comment (≤ 65535)
//   ZIP64 end locator    0x07064b50, 20 bytes, right before the end record
//   ZIP64 end record     0x06064b50, 56 bytes (+ extensible data)
//   ZIP64 extra field    id 0x0001: the 64-bit sizes / offset for the fields stored as 0xFFFFFFFF, in that order

namespace lynx::zip
{
namespace
{
constexpr std::uint32_t localSig = 0x04034b50, centralSig = 0x02014b50, endSig = 0x06054b50, end64LocatorSig = 0x07064b50,
                        end64Sig = 0x06064b50;

std::uint16_t le16 (const std::uint8_t* p) noexcept { return (std::uint16_t) (p[0] | (p[1] << 8)); }
std::uint32_t le32 (const std::uint8_t* p) noexcept { return p[0] | (p[1] << 8) | (p[2] << 16) | ((std::uint32_t) p[3] << 24); }
std::uint64_t le64 (const std::uint8_t* p) noexcept { return le32 (p) | ((std::uint64_t) le32 (p + 4) << 32); }

void put16 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back ((std::uint8_t) v);
    out.push_back ((std::uint8_t) (v >> 8));
}
void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    put16 (out, v & 0xFFFF);
    put16 (out, v >> 16);
}

bool fail (std::string* error, const std::string& why)
{
    if (error != nullptr)
        *error = why;
    return false;
}

std::string normalised (std::string_view name)
{
    std::string s (name);
    std::replace (s.begin(), s.end(), '\\', '/');
    while (s.rfind ("./", 0) == 0)
        s.erase (0, 2);
    while (! s.empty() && s.front() == '/')
        s.erase (0, 1);
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}
} // namespace

bool isZip (const std::uint8_t* data, std::size_t size) noexcept
{
    return size >= 22 && data != nullptr && (le32 (data) == localSig || le32 (data) == endSig);
}

bool Reader::open (const std::uint8_t* data, std::size_t size, std::string* error)
{
    bytes = data;
    length = size;
    list.clear();
    if (data == nullptr || size < 22)
        return fail (error, "not a ZIP archive (too short)");
    // the end record: the last signature within the comment's reach
    std::size_t endAt = std::string::npos;
    const auto lowest = size >= 22 + 65535 ? size - 22 - 65535 : 0;
    for (std::size_t i = size - 22 + 1; i-- > lowest;)
        if (le32 (data + i) == endSig && i + 22 + le16 (data + i + 20) <= size)
        {
            endAt = i;
            break;
        }
    if (endAt == std::string::npos)
        return fail (error, "not a ZIP archive (no end of central directory)");
    const auto* e = data + endAt;
    if (le16 (e + 4) != 0 || le16 (e + 6) != 0)
        return fail (error, "multi-disk ZIP archives are not supported");
    std::uint64_t count = le16 (e + 10), dirSize = le32 (e + 12), dirOffset = le32 (e + 16);
    if (count == 0xFFFF || dirSize == 0xFFFFFFFFu || dirOffset == 0xFFFFFFFFu)
    {
        // ZIP64: the locator sits right before the end record and points at the ZIP64 end record
        if (endAt < 20 || le32 (data + endAt - 20) != end64LocatorSig)
            return fail (error, "ZIP64 archive without its locator");
        const auto at = le64 (data + endAt - 20 + 8);
        if (at > size || size - at < 56 || le32 (data + at) != end64Sig)
            return fail (error, "ZIP64 end record missing");
        count = le64 (data + at + 32);
        dirSize = le64 (data + at + 40);
        dirOffset = le64 (data + at + 48);
    }
    if (dirOffset > size || dirSize > size - dirOffset)
        return fail (error, "central directory outside the file");
    if (count > dirSize / 46 + 1)
        return fail (error, "central directory too short for its entries");
    std::size_t pos = (std::size_t) dirOffset;
    const auto dirEnd = (std::size_t) (dirOffset + dirSize);
    list.reserve ((std::size_t) count);
    for (std::uint64_t k = 0; k < count; ++k)
    {
        if (pos + 46 > dirEnd || le32 (data + pos) != centralSig)
            return fail (error, "corrupt central directory");
        const auto* c = data + pos;
        EntryInfo info;
        info.flags = le16 (c + 8);
        info.method = le16 (c + 10);
        info.crc = le32 (c + 16);
        info.compressedSize = le32 (c + 20);
        info.size = le32 (c + 24);
        const std::size_t nameLen = le16 (c + 28), extraLen = le16 (c + 30), commentLen = le16 (c + 32);
        info.localOffset = le32 (c + 42);
        if (pos + 46 + nameLen + extraLen + commentLen > dirEnd)
            return fail (error, "corrupt central directory entry");
        info.name.assign ((const char*) c + 46, nameLen);
        // ZIP64 extra field: the 64-bit values of the fields saturated above, in their order
        const auto* x = c + 46 + nameLen;
        for (std::size_t q = 0; q + 4 <= extraLen;)
        {
            const auto id = le16 (x + q), n = le16 (x + q + 2);
            if (q + 4 + n > extraLen)
                break;
            if (id == 0x0001)
            {
                std::size_t r = q + 4;
                const auto take = [&] (std::uint64_t& v) {
                    if (v == 0xFFFFFFFFu && r + 8 <= q + 4 + n)
                    {
                        v = le64 (x + r);
                        r += 8;
                    }
                };
                take (info.size);
                take (info.compressedSize);
                take (info.localOffset);
            }
            q += 4 + n;
        }
        list.push_back (std::move (info));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

int Reader::find (std::string_view name) const
{
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name)
            return (int) i;
    const auto want = normalised (name);
    for (std::size_t i = 0; i < list.size(); ++i)
        if (normalised (list[i].name) == want)
            return (int) i;
    return -1;
}

std::optional<std::vector<std::uint8_t>> Reader::extract (std::string_view name, std::string* error, std::size_t maxSize) const
{
    const auto i = find (name);
    if (i < 0)
    {
        fail (error, "no entry " + std::string (name));
        return std::nullopt;
    }
    return extract (i, error, maxSize);
}

std::optional<std::vector<std::uint8_t>> Reader::extract (int index, std::string* error, std::size_t maxSize) const
{
    if (index < 0 || index >= (int) list.size())
    {
        fail (error, "no such entry");
        return std::nullopt;
    }
    const auto& e = list[(std::size_t) index];
    if ((e.flags & 1) != 0)
    {
        fail (error, "encrypted entry: " + e.name);
        return std::nullopt;
    }
    if (e.size > maxSize)
    {
        fail (error, "entry too large: " + e.name);
        return std::nullopt;
    }
    if (e.localOffset > length || length - e.localOffset < 30 || le32 (bytes + e.localOffset) != localSig)
    {
        fail (error, "corrupt local header: " + e.name);
        return std::nullopt;
    }
    const auto* h = bytes + e.localOffset;
    const auto start = e.localOffset + 30 + le16 (h + 26) + le16 (h + 28);
    if (start > length || e.compressedSize > length - start)
    {
        fail (error, "truncated entry: " + e.name);
        return std::nullopt;
    }
    const auto* p = bytes + start;
    std::vector<std::uint8_t> out;
    if (e.method == 0)
    {
        if (e.compressedSize != e.size)
        {
            fail (error, "stored entry with two sizes: " + e.name);
            return std::nullopt;
        }
        out.assign (p, p + e.size);
    }
    else if (e.method == 8)
    {
        auto inflated = inflateZlib (p, (std::size_t) e.compressedSize, true, (std::size_t) std::min<std::uint64_t> (maxSize, e.size));
        if (! inflated || inflated->size() != e.size)
        {
            fail (error, "corrupt deflated data: " + e.name);
            return std::nullopt;
        }
        out = std::move (*inflated);
    }
    else
    {
        fail (error, "unsupported compression method " + std::to_string (e.method) + ": " + e.name);
        return std::nullopt;
    }
    if (gzip::crc32 (out.data(), out.size()) != e.crc)
    {
        fail (error, "CRC mismatch: " + e.name);
        return std::nullopt;
    }
    return out;
}

std::vector<std::uint8_t> write (const std::vector<NewEntry>& entries, std::string* error)
{
    constexpr std::uint32_t dosTime = 0, dosDate = (0u << 9) | (1u << 5) | 1u; // 1980-01-01 00:00
    constexpr std::uint16_t utf8Flag = 0x0800;
    std::vector<std::uint8_t> out, dir;
    if (entries.size() >= 0xFFFF)
    {
        fail (error, "too many entries for a ZIP archive without ZIP64");
        return {};
    }
    std::uint64_t total = 0;
    for (const auto& e : entries)
        total += 30 + 46 + 2 * e.name.size() + e.data.size();
    if (total + 22 >= 0xFFFFFFFFull)
    {
        fail (error, "archive too large for a ZIP archive without ZIP64");
        return {};
    }
    out.reserve ((std::size_t) total + 22);
    for (const auto& e : entries)
    {
        if (e.name.empty() || e.name.size() > 0xFFFF)
        {
            fail (error, "bad entry name");
            return {};
        }
        const auto crc = gzip::crc32 (e.data.data(), e.data.size());
        const auto offset = (std::uint32_t) out.size();
        const auto size = (std::uint32_t) e.data.size();
        put32 (out, localSig);
        put16 (out, 20); // version needed: 2.0
        put16 (out, utf8Flag);
        put16 (out, 0); // stored
        put16 (out, dosTime);
        put16 (out, dosDate);
        put32 (out, crc);
        put32 (out, size);
        put32 (out, size);
        put16 (out, (std::uint32_t) e.name.size());
        put16 (out, 0);
        out.insert (out.end(), e.name.begin(), e.name.end());
        out.insert (out.end(), e.data.begin(), e.data.end());

        put32 (dir, centralSig);
        put16 (dir, 20); // made by: MS-DOS / FAT, 2.0
        put16 (dir, 20);
        put16 (dir, utf8Flag);
        put16 (dir, 0);
        put16 (dir, dosTime);
        put16 (dir, dosDate);
        put32 (dir, crc);
        put32 (dir, size);
        put32 (dir, size);
        put16 (dir, (std::uint32_t) e.name.size());
        put16 (dir, 0); // extra
        put16 (dir, 0); // comment
        put16 (dir, 0); // disk
        put16 (dir, 0); // internal attributes
        put32 (dir, 0); // external attributes
        put32 (dir, offset);
        dir.insert (dir.end(), e.name.begin(), e.name.end());
    }
    const auto dirOffset = (std::uint32_t) out.size();
    out.insert (out.end(), dir.begin(), dir.end());
    put32 (out, endSig);
    put16 (out, 0);
    put16 (out, 0);
    put16 (out, (std::uint32_t) entries.size());
    put16 (out, (std::uint32_t) entries.size());
    put32 (out, (std::uint32_t) dir.size());
    put32 (out, dirOffset);
    put16 (out, 0);
    return out;
}
} // namespace lynx::zip
