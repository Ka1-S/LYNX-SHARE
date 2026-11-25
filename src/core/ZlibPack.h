#pragma once

// A small zlib writer (RFC 1950 / 1951, one block of the fixed Huffman codes, greedy matches through a hash of three bytes).
// The states of Sytrus and Ogun are zlib streams; they are mostly repeated defaults, so this is enough to keep them small
// (round 190rh: LYNX's own preset banks write them).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lynx
{
inline std::vector<std::uint8_t> zlibPack (const std::vector<std::uint8_t>& data)
{
    std::vector<std::uint8_t> out { 0x78, 0x01 };
    std::uint32_t acc = 0;
    int bits = 0;
    const auto put = [&] (std::uint32_t v, int n) { // least significant bit first
        acc |= v << bits;
        bits += n;
        while (bits >= 8)
        {
            out.push_back ((std::uint8_t) acc);
            acc >>= 8;
            bits -= 8;
        }
    };
    const auto code = [&] (std::uint32_t v, int n) { // a Huffman code: most significant bit first
        std::uint32_t r = 0;
        for (int i = 0; i < n; ++i)
            r |= ((v >> i) & 1u) << (n - 1 - i);
        put (r, n);
    };
    const auto symbol = [&] (int s) {
        if (s < 144)
            code ((std::uint32_t) (0x30 + s), 8);
        else if (s < 256)
            code ((std::uint32_t) (0x190 + s - 144), 9);
        else if (s < 280)
            code ((std::uint32_t) (s - 256), 7);
        else
            code ((std::uint32_t) (0xC0 + s - 280), 8);
    };
    static constexpr int lengthBase[29] { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static constexpr int lengthBits[29] { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static constexpr int distBase[30] { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static constexpr int distBits[30] { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    put (1, 1), put (1, 2); // the last block, fixed codes
    std::vector<std::ptrdiff_t> last (1u << 15, -1);
    const auto n = (std::ptrdiff_t) data.size();
    const auto hash = [&data] (std::ptrdiff_t i) {
        const auto k = (std::size_t) i;
        return (std::size_t) (((std::uint32_t) data[k] * 506832829u ^ (std::uint32_t) data[k + 1] * 2654435761u ^ (std::uint32_t) data[k + 2] * 40503u) >> 4) & 0x7fffu;
    };
    for (std::ptrdiff_t i = 0; i < n;)
    {
        int length = 0;
        std::ptrdiff_t from = -1;
        if (i + 3 <= n)
        {
            from = last[hash (i)];
            if (from >= 0 && i - from <= 32768)
                while (length < 258 && i + length < n && data[(std::size_t) (from + length)] == data[(std::size_t) (i + length)])
                    ++length;
        }
        if (length < 3)
        {
            if (i + 3 <= n)
                last[hash (i)] = i;
            symbol (data[(std::size_t) i++]);
            continue;
        }
        int l = 28;
        while (lengthBase[l] > length)
            --l;
        symbol (257 + l);
        put ((std::uint32_t) (length - lengthBase[l]), lengthBits[l]);
        const auto distance = (int) (i - from);
        int d = 29;
        while (distBase[d] > distance)
            --d;
        code ((std::uint32_t) d, 5);
        put ((std::uint32_t) (distance - distBase[d]), distBits[d]);
        for (const auto end = i + length; i < end; ++i)
            if (i + 3 <= n)
                last[hash (i)] = i;
    }
    symbol (256);
    put (0, 7); // (flush to a byte)
    std::uint32_t a = 1, b = 0;
    for (const auto v : data)
    {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    const auto sum = (b << 16) | a;
    for (int i = 3; i >= 0; --i)
        out.push_back ((std::uint8_t) (sum >> (8 * i)));
    return out;
}
} // namespace lynx
