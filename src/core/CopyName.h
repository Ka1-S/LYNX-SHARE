#pragma once

// The name of a copy (Clone, Make unique...), as FL 2026 names it (round 190ri, live check of FL 2026 build 5654): the clone
// of "foo" is "foo #2", and a clone of that one is "foo #3", never "foo #2 #2" ("foo #2" cloned while #3 and #4 exist gives
// "foo #5"). FL's patterns without a name are shown as "Pattern <position>", so the clone of "Pattern 1" is "Pattern 2" and
// the ones below it move down one number: renumberedClone.
// No JUCE: tests/Community190riTests.inc.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace lynx::copyname
{
// The number after the last " #" of `name` (2 or more digits allowed), or 0 when the name has no such ending.
inline int copyNumber (const std::string& name, std::string& base)
{
    base = name;
    const auto hash = name.rfind (" #");
    if (hash == std::string::npos || hash == 0)
        return 0;
    const auto digits = name.substr (hash + 2);
    if (digits.empty() || digits.size() > 6 || ! std::all_of (digits.begin(), digits.end(), [] (char c) { return c >= '0' && c <= '9'; }))
        return 0;
    const auto n = std::stoi (digits);
    if (n < 1)
        return 0;
    base = name.substr (0, hash);
    return n;
}

// The copy's name: "<base> #n", n counting on from the number `name` already ends in, the first one `taken` does not hold.
inline std::string next (const std::string& name, const std::function<bool (const std::string&)>& taken)
{
    std::string base;
    for (auto n = std::max (1, copyNumber (name, base)) + 1;; ++n)
        if (auto made = base + " #" + std::to_string (n); ! taken (made))
            return made;
}

// "Pattern <n>": the name a pattern shows while it has none of its own. Returns n, or 0 for any other name.
inline int defaultPatternNumber (const std::string& name)
{
    static const std::string prefix = "Pattern ";
    if (name.size() <= prefix.size() || name.size() > prefix.size() + 6 || name.compare (0, prefix.size(), prefix) != 0)
        return 0;
    const auto digits = name.substr (prefix.size());
    if (digits[0] == '0' || ! std::all_of (digits.begin(), digits.end(), [] (char c) { return c >= '0' && c <= '9'; }))
        return 0;
    return std::stoi (digits);
}
} // namespace lynx::copyname
