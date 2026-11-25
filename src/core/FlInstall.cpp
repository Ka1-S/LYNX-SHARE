#include "core/FlInstall.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>

namespace lynx::flinstall
{
namespace fs = std::filesystem;

namespace
{
fs::path pathOf (const std::string& utf8)
{
   #if defined(__cpp_char8_t)
    return fs::path (std::u8string (utf8.begin(), utf8.end()));
   #else
    return fs::u8path (utf8);
   #endif
}
std::string textOf (const fs::path& p)
{
    const auto s = p.u8string();
    return std::string (s.begin(), s.end());
}
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

enum class Kind { none, folder, file, denied };
Kind kindOf (const fs::path& p)
{
    std::error_code ec;
    const auto st = fs::status (p, ec);
    if (ec == std::errc::permission_denied)
        return Kind::denied;
    if (ec || ! fs::exists (st))
        return Kind::none;
    return fs::is_directory (st) ? Kind::folder : Kind::file;
}

// the entries of a folder (nullopt: it can't be listed)
std::optional<std::vector<fs::path>> list (const fs::path& folder)
{
    std::error_code ec;
    fs::directory_iterator it (folder, ec), end;
    if (ec)
        return std::nullopt;
    std::vector<fs::path> out;
    for (; it != end && out.size() < 20000; it.increment (ec))
    {
        out.push_back (it->path());
        if (ec)
            break;
    }
    std::sort (out.begin(), out.end());
    return out;
}

// `name` in the folder, ignoring case ("" = not there)
fs::path child (const fs::path& folder, const std::string& name)
{
    if (kindOf (folder / pathOf (name)) != Kind::none)
        return folder / pathOf (name);
    if (const auto entries = list (folder))
        for (const auto& e : *entries)
            if (lower (textOf (e.filename())) == lower (name))
                return e;
    return {};
}
bool hasFolder (const fs::path& folder, const std::string& name)
{
    const auto c = child (folder, name);
    return ! c.empty() && kindOf (c) == Kind::folder;
}

// an installation: FL's program or engine next to its Data or Plugins folder; a copy of the two folders alone is enough
bool isInstall (const fs::path& f)
{
    const auto data = hasFolder (f, "Data"), plugins = hasFolder (f, "Plugins");
    if (data && plugins && hasFolder (child (f, "Plugins"), "Fruity"))
        return true;
    for (const auto* program : { "FL64.exe", "FL.exe", "FLEngine_x64.dll" })
        if (const auto c = child (f, program); ! c.empty() && kindOf (c) == Kind::file)
            return data || plugins;
    return false;
}

// FL's user data folder: Presets / Settings / Projects, no program
bool isUserData (const fs::path& f)
{
    return hasFolder (f, "Presets") && (hasFolder (f, "Settings") || hasFolder (f, "Projects")) && ! hasFolder (f, "Plugins");
}

std::vector<unsigned char> read (const fs::path& p, std::uintmax_t limit)
{
    std::error_code ec;
    const auto size = fs::file_size (p, ec);
    if (ec || size > limit)
        return {};
    std::ifstream in (p, std::ios::binary);
    if (! in)
        return {};
    return std::vector<unsigned char> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

Check accept (const fs::path& f)
{
    Check c;
    c.status = Status::ok;
    c.folder = textOf (f);
    for (const auto* part : { "Data", "Plugins" })
    {
        const auto p = child (f, part);
        if (p.empty() || ! list (p))
        {
            c.missing = part;
            break;
        }
    }
    if (const auto exe = child (f, "FL64.exe"); ! exe.empty())
        c.version = versionOf (read (exe, 64u << 20));
    return c;
}
} // namespace

std::string versionOf (const std::vector<unsigned char>& b)
{
    for (std::size_t i = 0; i + 24 <= b.size(); ++i)
        if (b[i] == 0xBD && b[i + 1] == 0x04 && b[i + 2] == 0xEF && b[i + 3] == 0xFE && b[i + 4] == 0 && b[i + 6] == 1) // signature, struct version 1.0
        {
            const auto word = [&b] (std::size_t at) { return (int) (b[at] | b[at + 1] << 8); };
            return std::to_string (word (i + 10)) + "." + std::to_string (word (i + 8)) + "." + std::to_string (word (i + 14)) + "." + std::to_string (word (i + 12));
        }
    return {};
}

Check check (const std::string& folder)
{
    Check c;
    c.status = Status::missing;
    auto text = folder;
    while (text.size() > 1 && (text.back() == '/' || text.back() == '\\'))
        text.pop_back();
    if (text.empty())
        return c;
    auto f = pathOf (text);
    switch (kindOf (f))
    {
        case Kind::none: return c;
        case Kind::denied: c.status = Status::notReadable; return c;
        case Kind::file: f = f.parent_path(); break; // (FL64.exe picked)
        case Kind::folder: break;
    }
    const auto inside = list (f);
    if (! inside || (! inside->empty() && std::all_of (inside->begin(), inside->end(), [] (const fs::path& e) { return kindOf (e) == Kind::denied; })))
    {
        c.status = Status::notReadable; // (no permission to list it, or to look at anything in it)
        return c;
    }
    // itself; macOS' application; one or two levels up ("Data", "Plugins\Fruity" picked)
    auto up = f;
    for (int i = 0; i < 3 && ! up.empty(); ++i)
    {
        if (isInstall (up))
            return accept (up);
        if (const auto app = child (up, "Contents"); ! app.empty())
            if (const auto inside = app / "Resources" / "FL"; isInstall (inside))
                return accept (inside);
        if (up.parent_path() == up)
            break;
        up = up.parent_path();
    }
    if (isUserData (f))
    {
        c.status = Status::userData;
        return c;
    }
    // below: "Image-Line", "Program Files" or the drive itself
    auto below = f;
    for (const auto* next : { "Program Files", "Image-Line" })
        if (const auto n = child (below, next); ! n.empty() && kindOf (n) == Kind::folder)
            below = n;
    if (const auto entries = list (below))
        for (const auto& e : *entries)
            if (kindOf (e) == Kind::folder && lower (textOf (e.filename())).rfind ("fl studio", 0) == 0 && isInstall (e))
                c.candidates.push_back (textOf (e));
    if (c.candidates.size() == 1)
        return accept (pathOf (c.candidates.front()));
    c.status = c.candidates.empty() ? Status::notFl : Status::several;
    return c;
}

std::vector<std::string> onMountedDrives (const std::vector<std::string>& given)
{
    auto roots = given;
    if (roots.empty())
    {
       #if defined(__linux__)
        roots = { "/mnt", "/media", "/run/media" };
       #endif
    }
    std::vector<std::string> out;
    const auto look = [&out] (const fs::path& mount) {
        for (const auto* programs : { "Program Files", "Program Files (x86)" })
        {
            const auto p = child (mount, programs);
            const auto maker = p.empty() ? fs::path() : child (p, "Image-Line");
            if (maker.empty())
                continue;
            if (const auto entries = list (maker))
                for (const auto& e : *entries)
                    if (lower (textOf (e.filename())).rfind ("fl studio", 0) == 0 && kindOf (e) == Kind::folder && isInstall (e)
                        && std::find (out.begin(), out.end(), textOf (e)) == out.end())
                        out.push_back (textOf (e));
        }
    };
    for (const auto& r : roots)
        if (const auto mounts = list (pathOf (r)))
            for (const auto& m : *mounts)
            {
                if (kindOf (m) != Kind::folder)
                    continue;
                look (m);
                if (textOf (pathOf (r).filename()) == "media") // (/media/<user>/<drive>, /run/media/<user>/<drive>)
                    if (const auto inner = list (m))
                        for (const auto& d : *inner)
                            if (kindOf (d) == Kind::folder)
                                look (d);
            }
    return out;
}
} // namespace lynx::flinstall
