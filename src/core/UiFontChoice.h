#pragma once

// Settings › General › Interface font (round 179a): what a saved choice means at start-up. An empty result is the
// platform's default font: nothing saved, or a font that is no longer installed (the choice stays in the file, so it
// works again when the font comes back). No message: a missing font is not worth an error.

#include <functional>
#include <string>

namespace lynx
{
inline std::string chooseUiFont (std::string saved, const std::function<bool (const std::string&)>& installed)
{
    const auto isSpace = [] (char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (! saved.empty() && isSpace (saved.back()))
        saved.pop_back();
    while (! saved.empty() && isSpace (saved.front()))
        saved.erase (saved.begin());
    return ! saved.empty() && installed (saved) ? saved : std::string();
}
} // namespace lynx
