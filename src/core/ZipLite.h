#pragma once

// A small ZIP reader and writer (round 190jf, B22: the container of DAWproject files, core/DawProject.h). JUCE-free.
// Format: PKWARE's APPNOTE.TXT (the public ZIP specification), written from the specification only.
//  - reading: the central directory (found from the end-of-central-directory record, ZIP64 records and extra fields
//    included), entries stored (method 0) or deflated (method 8, through core/Inflate.h), each one's CRC-32 checked
//    (core/Gzip.h); encrypted, multi-disk and other methods are refused with an error, never a crash;
//  - writing: every entry stored (method 0), names flagged UTF-8, a fixed date (1980-01-01 00:00) so the same input
//    always gives the same bytes. Archives that would need ZIP64 (more than 65535 entries or 4 GB) are refused.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lynx::zip
{
struct EntryInfo
{
    std::string name;             // as stored ('/' separated)
    std::uint16_t method = 0;     // 0 stored, 8 deflated
    std::uint16_t flags = 0;
    std::uint32_t crc = 0;
    std::uint64_t compressedSize = 0, size = 0, localOffset = 0;
    bool isDirectory() const noexcept { return ! name.empty() && name.back() == '/'; }
};

// True when the bytes start like a ZIP archive (a local file header, or an empty archive's end record).
bool isZip (const std::uint8_t* data, std::size_t size) noexcept;

// Reads an archive held in memory (the bytes must outlive the reader).
class Reader
{
public:
    // Reads the central directory. False (and `error`) for anything that is not a readable archive.
    bool open (const std::uint8_t* data, std::size_t size, std::string* error = nullptr);
    const std::vector<EntryInfo>& entries() const noexcept { return list; }
    // The entry named `name` (exact match first, then ignoring case and '\\' vs '/', a leading "./" or '/'); −1 = none.
    int find (std::string_view name) const;
    // The uncompressed bytes of entry `index` (nullopt and `error`: a bad index, an unsupported method, corrupt data,
    // a CRC mismatch, or more than `maxSize` bytes).
    std::optional<std::vector<std::uint8_t>> extract (int index, std::string* error = nullptr, std::size_t maxSize = 2048u << 20) const;
    std::optional<std::vector<std::uint8_t>> extract (std::string_view name, std::string* error = nullptr, std::size_t maxSize = 2048u << 20) const;

private:
    const std::uint8_t* bytes = nullptr;
    std::size_t length = 0;
    std::vector<EntryInfo> list;
};

struct NewEntry
{
    std::string name;
    std::vector<std::uint8_t> data;
};

// The archive of `entries` (stored, in order). Empty with `error` when it would need ZIP64 or a name is empty or too long.
std::vector<std::uint8_t> write (const std::vector<NewEntry>& entries, std::string* error = nullptr);
} // namespace lynx::zip
