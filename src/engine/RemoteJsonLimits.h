#pragma once

#include <cstddef>
#include <string_view>

namespace lynx::remote_json
{
// Run before JUCE's recursive JSON parser, including before authentication. The
// byte limit alone does not bound parser stack usage. Syntax remains JUCE's job.
inline bool withinNestingLimit (std::string_view json, std::size_t limit = 64) noexcept
{
    std::size_t depth = 0;
    bool quoted = false, escaped = false;
    for (const auto c : json)
    {
        if (quoted)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > limit) return false;
        }
        else if ((c == '}' || c == ']') && depth > 0) --depth;
    }
    return true;
}
}
