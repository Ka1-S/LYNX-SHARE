#pragma once

// Issue #189 (round 190ir): the vertical metrics of a TrueType / OpenType font, read and rescaled in memory.
//
// JUCE sizes text by "height" = the font's ascent + descent (hhea, or OS/2 typo with USE_TYPO_METRICS), not by its em.
// A font with tall metrics draws smaller letters at the same height: Open Sans (2189 + 600 units of 2048 = 1.362 em)
// draws them 15 % smaller than DejaVu Sans (1901 + 483 = 1.164 em), the font JUCE picked on most Linux systems up to
// LYNX SHARE 0.1.23 (LYNX brings Open Sans since 0.1.25, round 179a). `withVerticalExtent` gives a font file the same
// glyphs with its ascent + descent set to `emFraction` of its em (hhea, OS/2 typo and win, the ascent : descent ratio
// kept), so it draws as large as a font with that extent. JUCE-free: tests read and write the tables directly.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace lynx::fontmetrics
{
struct Vertical
{
    int unitsPerEm = 0;
    int hheaAscent = 0, hheaDescent = 0;          // descent negative, as in the file
    int typoAscent = 0, typoDescent = 0;          // (OS/2; 0 when the font has no OS/2 table)
    int winAscent = 0, winDescent = 0;            // (OS/2; both positive)
    bool useTypoMetrics = false;                  // OS/2 fsSelection bit 7
    double extent() const noexcept { return unitsPerEm > 0 ? (double) (hheaAscent - hheaDescent) / unitsPerEm : 0.0; }
};

namespace detail
{
inline std::uint32_t u32 (const unsigned char* p) noexcept { return ((std::uint32_t) p[0] << 24) | ((std::uint32_t) p[1] << 16) | ((std::uint32_t) p[2] << 8) | p[3]; }
inline std::uint16_t u16 (const unsigned char* p) noexcept { return (std::uint16_t) ((p[0] << 8) | p[1]); }
inline std::int16_t s16 (const unsigned char* p) noexcept { return (std::int16_t) u16 (p); }
inline void put16 (unsigned char* p, int v) noexcept
{
    p[0] = (unsigned char) (((unsigned) v >> 8) & 0xff);
    p[1] = (unsigned char) ((unsigned) v & 0xff);
}

// offset of table `tag` in a single font file (not a collection), 0 when it is missing or out of the data
inline std::size_t table (const unsigned char* d, std::size_t size, const char* tag, std::size_t minLength) noexcept
{
    if (size < 12)
        return 0;
    const auto count = u16 (d + 4);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto at = 12 + 16 * i;
        if (at + 16 > size)
            return 0;
        if (std::memcmp (d + at, tag, 4) == 0)
        {
            const auto offset = (std::size_t) u32 (d + at + 8), length = (std::size_t) u32 (d + at + 12);
            return length >= minLength && offset + minLength <= size ? offset : 0;
        }
    }
    return 0;
}
} // namespace detail

// The font's vertical metrics; nullopt when it has no head / hhea table (or is not a font).
inline std::optional<Vertical> read (const void* data, std::size_t size)
{
    const auto* d = static_cast<const unsigned char*> (data);
    if (d == nullptr)
        return std::nullopt;
    const auto head = detail::table (d, size, "head", 54), hhea = detail::table (d, size, "hhea", 36);
    if (head == 0 || hhea == 0)
        return std::nullopt;
    Vertical v;
    v.unitsPerEm = detail::u16 (d + head + 18);
    v.hheaAscent = detail::s16 (d + hhea + 4);
    v.hheaDescent = detail::s16 (d + hhea + 6);
    if (const auto os2 = detail::table (d, size, "OS/2", 78); os2 != 0)
    {
        v.useTypoMetrics = (detail::u16 (d + os2 + 62) & 0x80) != 0;
        v.typoAscent = detail::s16 (d + os2 + 68);
        v.typoDescent = detail::s16 (d + os2 + 70);
        v.winAscent = detail::u16 (d + os2 + 74);
        v.winDescent = detail::u16 (d + os2 + 76);
    }
    if (v.unitsPerEm <= 0)
        return std::nullopt;
    return v;
}

// A copy of the font whose ascent + descent is `emFraction` of its em in every table JUCE / FreeType / HarfBuzz read
// (hhea; OS/2 typo and win), each ascent : descent kept. Empty when the data is not a font this can read.
inline std::vector<unsigned char> withVerticalExtent (const void* data, std::size_t size, double emFraction)
{
    const auto v = read (data, size);
    if (! v.has_value() || emFraction <= 0.0 || v->extent() <= 0.0)
        return {};
    std::vector<unsigned char> out (static_cast<const unsigned char*> (data), static_cast<const unsigned char*> (data) + size);
    auto* d = out.data();
    const auto target = emFraction * v->unitsPerEm;
    const auto split = [target] (int ascent, int descentMagnitude, int& a, int& b) {
        const auto total = (double) ascent + descentMagnitude;
        if (total <= 0.0)
            return false;
        a = (int) std::lround (target * ascent / total);
        b = (int) std::lround (target) - a;
        return true;
    };
    int a = 0, b = 0;
    const auto hhea = detail::table (d, size, "hhea", 36);
    if (split (v->hheaAscent, -v->hheaDescent, a, b))
    {
        detail::put16 (d + hhea + 4, a);
        detail::put16 (d + hhea + 6, -b);
    }
    if (const auto os2 = detail::table (d, size, "OS/2", 78); os2 != 0)
    {
        if (split (v->typoAscent, -v->typoDescent, a, b))
        {
            detail::put16 (d + os2 + 68, a);
            detail::put16 (d + os2 + 70, -b);
        }
        if (split (v->winAscent, v->winDescent, a, b))
        {
            detail::put16 (d + os2 + 74, a);
            detail::put16 (d + os2 + 76, b);
        }
    }
    return out;
}

// DejaVu Sans's ascent + descent (1901 + 483 of 2048 units): the size letters had on Linux up to LYNX SHARE 0.1.23.
inline constexpr double dejaVuSansExtent = (1901.0 + 483.0) / 2048.0;
} // namespace lynx::fontmetrics
