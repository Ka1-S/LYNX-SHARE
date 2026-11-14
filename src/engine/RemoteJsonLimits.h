#pragma once

#include <cstddef>
#include <string_view>

namespace lynx::remote_json
{
inline bool canonicalUtf8 (std::string_view text) noexcept
{
    // JUCE's release parser can interpret overlong UTF-8 as ASCII quotes,
    // escapes or brackets. Validate the original bytes before counting syntax.
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto first = static_cast<unsigned char> (text[i]);
        if (first == 0) return false; // JUCE strings terminate here; JSON uses \u0000 instead.
        if (first < 0x80) continue;
        std::size_t extra = 0;
        unsigned char secondMin = 0x80, secondMax = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) extra = 1;
        else if (first >= 0xe0 && first <= 0xef)
        {
            extra = 2;
            if (first == 0xe0) secondMin = 0xa0; // no overlong encoding below U+0800
            if (first == 0xed) secondMax = 0x9f; // no UTF-16 surrogate code points
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            extra = 3;
            if (first == 0xf0) secondMin = 0x90; // no overlong encoding below U+10000
            if (first == 0xf4) secondMax = 0x8f; // at most U+10FFFF
        }
        else return false;
        if (text.size() - i <= extra) return false;
        const auto second = static_cast<unsigned char> (text[i + 1]);
        if (second < secondMin || second > secondMax) return false;
        for (std::size_t j = 2; j <= extra; ++j)
        {
            const auto continuation = static_cast<unsigned char> (text[i + j]);
            if (continuation < 0x80 || continuation > 0xbf) return false;
        }
        i += extra;
    }
    return true;
}

// Run before JUCE's recursive JSON parser, including before authentication. The
// byte limit alone does not bound parser stack usage. Apart from canonical UTF-8
// and raw NUL rejection, syntax remains JUCE's job.
inline bool withinNestingLimit (std::string_view json, std::size_t limit = 64) noexcept
{
    if (! canonicalUtf8 (json)) return false;
    std::size_t depth = 0;
    char quote = 0;
    bool escaped = false;
    for (const auto c : json)
    {
        if (quote != 0)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
        }
        // JUCE also accepts single-quoted strings. A quote inside a string
        // opened by the other delimiter must not hide structural nesting.
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '{' || c == '[')
        {
            if (++depth > limit) return false;
        }
        else if ((c == '}' || c == ']') && depth > 0) --depth;
    }
    return true;
}
}
