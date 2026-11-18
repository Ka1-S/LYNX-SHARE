#pragma once

// Issue #200 (round 190ll): folders dropped from the system's file manager onto the Browser join its folder list, as
// FL's Browser does (manual, Browser: "The simplest way to add folders is drag and drop folders from your Operating
// System browser to the FL Studio Browser"; "You can also drop any folder from an external File Browser onto the FL
// Studio Browser and it will be added to the list"). The list is the one Add folder... writes (one path per line).

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace lynx::userfolders
{
// a path as the list compares it: no trailing separator (a drive root keeps its own), case folded where the file
// system ignores case
inline std::string key (std::string path, bool ignoreCase)
{
    const auto first = path.find_first_not_of (" \t\r\n"), last = path.find_last_not_of (" \t\r\n");
    path = first == std::string::npos ? std::string() : path.substr (first, last - first + 1);
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\') && ! (path.size() == 3 && path[1] == ':'))
        path.pop_back();
    if (ignoreCase)
        std::transform (path.begin(), path.end(), path.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return path;
}

// Adds the folders of `dropped` (directories only: the caller filters) that the list does not have yet, in the order
// they were dropped; returns them. Blank lines of the list are dropped.
inline std::vector<std::string> add (std::vector<std::string>& list, const std::vector<std::string>& dropped, bool ignoreCase)
{
    list.erase (std::remove_if (list.begin(), list.end(), [] (const std::string& l) {
                    return l.find_first_not_of (" \t\r\n") == std::string::npos;
                }),
                list.end());
    std::vector<std::string> added;
    for (const auto& path : dropped)
    {
        if (path.empty())
            continue;
        const auto k = key (path, ignoreCase);
        if (std::any_of (list.begin(), list.end(), [&] (const std::string& l) { return key (l, ignoreCase) == k; }))
            continue;
        const auto stored = key (path, false);
        list.push_back (stored);
        added.push_back (stored);
    }
    return added;
}
} // namespace lynx::userfolders
