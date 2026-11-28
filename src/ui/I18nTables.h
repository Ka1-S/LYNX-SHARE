#pragma once

// The embedded translation tables (ui/I18nData.h, written by tools/make_i18n.py) back as their resources/i18n/<id>.txt
// files, byte for byte. Round 190sh: the tables share their English keys, stored once (ui/i18n/_keys.h); a table's own
// data is its .txt file with each line `"key" = "value"` written as \x01"value" (the key after the previous line's) or
// \x02<key number> "value". Every other line is kept as it is.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Inflate.h"
#include "ui/I18nData.h"

namespace lynx::i18n
{
// nothing if the embedded data is damaged (never with a build of tools/make_i18n.py's output)
inline std::optional<std::string> unpackTable (const data::Table& table)
{
    const auto keyText = inflateZlib (data::t_keys, data::keysZlibSize, false, data::keysTextSize + 16);
    const auto packed = inflateZlib (table.zlib, table.zlibSize, false, table.packedSize + 16);
    if (! keyText || keyText->size() != data::keysTextSize || ! packed || packed->size() != table.packedSize)
        return std::nullopt;

    std::vector<std::string_view> keys;
    const std::string_view all ((const char*) keyText->data(), keyText->size());
    for (std::size_t start = 0;;)
    {
        const auto end = all.find ('\n', start);
        keys.push_back (all.substr (start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    std::string text;
    text.reserve (table.textSize);
    const std::string_view body ((const char*) packed->data(), packed->size());
    std::size_t previous = (std::size_t) -1; // (the first \x01 line is key 0)
    for (std::size_t start = 0;;)
    {
        const auto end = body.find ('\n', start);
        auto line = body.substr (start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (! line.empty() && (line[0] == '\x01' || line[0] == '\x02'))
        {
            std::size_t key = previous + 1;
            line.remove_prefix (1);
            if (body[start] == '\x02')
            {
                const auto space = line.find (' ');
                if (space == std::string_view::npos || space == 0)
                    return std::nullopt;
                key = 0;
                for (const char c : line.substr (0, space))
                {
                    if (c < '0' || c > '9')
                        return std::nullopt;
                    key = key * 10 + (std::size_t) (c - '0');
                }
                line.remove_prefix (space + 1);
            }
            if (key >= keys.size())
                return std::nullopt;
            text.append (keys[key]).append (" = ").append (line);
            previous = key;
        }
        else
        {
            text.append (line);
        }
        if (end == std::string_view::npos)
            break;
        text.push_back ('\n');
        start = end + 1;
    }
    if (text.size() != table.textSize)
        return std::nullopt;
    return text;
}
} // namespace lynx::i18n
