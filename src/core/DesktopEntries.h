#pragma once

// Issue #278 (round 190qr): Linux desktop entries (.desktop files) that start a given AppImage.
//
// An AppImage manager (Gear Lever, AppImageLauncher, appimaged) integrates an AppImage by writing its own entry to
// ~/.local/share/applications, pointing at the AppImage (which it may move and rename first). LYNX's own switch "Show
// LYNX SHARE in the applications menu" writes com.lynx_share.LYNX.desktop for the same file. With both, the menu lists
// LYNX twice. This tells whether an entry's text starts `appImage`, so LYNX can leave the menu to the manager.
//
// No JUCE (unit-tested in tests/Issues274_190qrTests.inc).

#include <string>
#include <string_view>

namespace lynx::desktopentries
{
// The value of key `key` (e.g. "Exec") in the [Desktop Entry] group of a .desktop file's text, or "" (localised keys
// such as Name[de] are other keys).
inline std::string valueOf (std::string_view text, std::string_view key)
{
    bool inMain = false;
    std::size_t at = 0;
    while (at <= text.size())
    {
        auto end = text.find ('\n', at);
        if (end == std::string_view::npos)
            end = text.size();
        auto line = text.substr (at, end - at);
        at = end + 1;
        while (! line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix (1);
        while (! line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix (1);
        if (line.empty() || line.front() == '#')
            continue;
        if (line.front() == '[')
        {
            inMain = line == "[Desktop Entry]";
            continue;
        }
        if (! inMain || line.size() <= key.size() || line.substr (0, key.size()) != key)
            continue;
        auto rest = line.substr (key.size());
        while (! rest.empty() && rest.front() == ' ')
            rest.remove_prefix (1);
        if (rest.empty() || rest.front() != '=')
            continue;
        rest.remove_prefix (1);
        while (! rest.empty() && rest.front() == ' ')
            rest.remove_prefix (1);
        return std::string (rest);
    }
    return {};
}

// The program an Exec= value runs: its first argument, after the two levels of the desktop entry spec — the string
// escapes of the value (\s \n \t \r \\) and then the Exec quoting ("..." with \" \` \$ \\ inside).
inline std::string programOf (std::string_view exec)
{
    std::string value;
    for (std::size_t i = 0; i < exec.size(); ++i)
    {
        if (exec[i] == '\\' && i + 1 < exec.size())
        {
            const auto c = exec[++i];
            if (c == 's' || c == 'n' || c == 't' || c == 'r' || c == '\\')
                value += c == 's' ? ' ' : c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : '\\';
            else
                value.append ({ '\\', c }); // (not a string escape: left for the quoting level)
            continue;
        }
        value += exec[i];
    }
    std::string out;
    std::size_t i = 0;
    while (i < value.size() && value[i] == ' ')
        ++i;
    if (i < value.size() && value[i] == '"')
    {
        for (++i; i < value.size() && value[i] != '"'; ++i)
        {
            if (value[i] == '\\' && i + 1 < value.size())
                ++i;
            out += value[i];
        }
        return out;
    }
    for (; i < value.size() && value[i] != ' '; ++i)
        out += value[i];
    return out;
}

// Whether the entry `text` (a .desktop file) starts `appImage` (an absolute path): its Exec= runs that file, or its
// TryExec= names it. Entries marked with `ownMarker` (a line of LYNX's own entry) never count.
inline bool startsAppImage (std::string_view text, std::string_view appImage, std::string_view ownMarker = "X-LYNX-AppImage=true")
{
    if (appImage.empty() || (! ownMarker.empty() && text.find (ownMarker) != std::string_view::npos))
        return false;
    if (valueOf (text, "Type") != "Application" && ! valueOf (text, "Type").empty())
        return false;
    return programOf (valueOf (text, "Exec")) == appImage || valueOf (text, "TryExec") == appImage;
}
} // namespace lynx::desktopentries
