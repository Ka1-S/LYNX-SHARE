#include "core/PresetCycle.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>

namespace lynx::presetcycle
{
namespace
{
namespace fs = std::filesystem;

fs::path u8path (const std::string& s) { return fs::path ((const char8_t*) s.c_str()); }
std::string utf8 (const fs::path& p)
{
    const auto u = p.u8string();
    return std::string ((const char*) u.data(), u.size());
}

char lowerAscii (char c) noexcept { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; }
bool isDigit (char c) noexcept { return c >= '0' && c <= '9'; }

// "Bass/Acid.fst" -> folder "Bass/" (with its separator, "" for the top folder), name "Acid"
void split (const std::string& relative, std::string& folder, std::string& name)
{
    std::string r = relative;
    std::replace (r.begin(), r.end(), '\\', '/');
    const auto slash = r.find_last_of ('/');
    folder = slash == std::string::npos ? std::string() : r.substr (0, slash + 1);
    name = slash == std::string::npos ? r : r.substr (slash + 1);
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos && dot > 0)
        name.resize (dot);
}
} // namespace

int compareNatural (const std::string& a, const std::string& b)
{
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        if (isDigit (a[i]) && isDigit (b[j]))
        {
            // digit runs by value: skip leading zeros, longer run = bigger number
            auto si = i, sj = j;
            while (si < a.size() && a[si] == '0')
                ++si;
            while (sj < b.size() && b[sj] == '0')
                ++sj;
            auto ei = si, ej = sj;
            while (ei < a.size() && isDigit (a[ei]))
                ++ei;
            while (ej < b.size() && isDigit (b[ej]))
                ++ej;
            if (ei - si != ej - sj)
                return (ei - si) < (ej - sj) ? -1 : 1;
            for (auto x = si, y = sj; x < ei; ++x, ++y)
                if (a[x] != b[y])
                    return a[x] < b[y] ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        const auto ca = (unsigned char) lowerAscii (a[i]), cb = (unsigned char) lowerAscii (b[j]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size() || j < b.size())
        return i < a.size() ? 1 : -1; // the shorter one ("Default" before "Default (pad)")
    return a < b ? -1 : (b < a ? 1 : 0);
}

bool flLess (const std::string& a, const std::string& b)
{
    std::string folderA, nameA, folderB, nameB;
    split (a, folderA, nameA);
    split (b, folderB, nameB);
    if (folderA != folderB)
    {
        // FLEngine 0x662a40: a folder that contains the other one comes first (its own files before its subfolders)
        if (folderB.rfind (folderA, 0) == 0)
            return true;
        if (folderA.rfind (folderB, 0) == 0)
            return false;
        return compareNatural (folderA, folderB) < 0;
    }
    const auto byName = compareNatural (nameA, nameB);
    return byName != 0 ? byName < 0 : a < b;
}

std::vector<Preset> listPresets (const std::vector<std::string>& folders, const std::string& extension)
{
    std::map<std::string, Preset> byRelative; // (lower-case key: the same file in two folders on a Windows disk)
    for (const auto& folder : folders)
    {
        std::error_code ec;
        const auto root = u8path (folder);
        if (folder.empty() || ! fs::is_directory (root, ec))
            continue;
        for (fs::recursive_directory_iterator it (root, fs::directory_options::skip_permission_denied, ec), end; it != end && ! ec; it.increment (ec))
        {
            std::error_code fe;
            if (! it->is_regular_file (fe))
                continue;
            auto ext = utf8 (it->path().extension());
            std::transform (ext.begin(), ext.end(), ext.begin(), lowerAscii);
            if (ext != extension)
                continue;
            Preset p;
            p.path = utf8 (it->path());
            p.relative = utf8 (it->path().lexically_relative (root));
            std::replace (p.relative.begin(), p.relative.end(), '\\', '/');
            std::string unused;
            split (p.relative, unused, p.name);
            auto key = p.relative;
            std::transform (key.begin(), key.end(), key.begin(), lowerAscii);
            byRelative[key] = std::move (p);
        }
    }
    std::vector<Preset> out;
    out.reserve (byRelative.size());
    for (auto& [key, p] : byRelative)
        out.push_back (std::move (p));
    std::sort (out.begin(), out.end(), [] (const Preset& x, const Preset& y) { return flLess (x.relative, y.relative); });
    return out;
}

std::vector<std::string> pluginPresetFolders (const FlPathRoots& roots, bool generator, const std::string& folder)
{
    std::vector<std::string> out;
    if (folder.empty())
        return out;
    const std::string kind = generator ? "Generators" : "Effects";
    auto join = [] (std::string base, const std::string& tail) {
        if (! base.empty() && base.back() != '/' && base.back() != '\\')
            base += '/';
        return base + tail;
    };
    if (! roots.data.empty())
        out.push_back (join (roots.data, "Patches/Plugin presets/" + kind + "/" + folder));
    if (! roots.userData.empty())
        out.push_back (join (roots.userData, "Presets/Plugin presets/" + kind + "/" + folder));
    return out;
}

int step (int current, int delta, int count) noexcept
{
    if (count <= 0)
        return -1;
    if (current < 0 || current >= count)
        current = delta > 0 ? -1 : 0; // FL: index = -(delta > 0), then the step below
    return ((current + delta) % count + count) % count;
}

std::string positionText (int index, int count)
{
    return std::to_string (index + 1) + " / " + std::to_string (count);
}

int indexOfName (const std::vector<Preset>& list, const std::string& name)
{
    auto low = [] (std::string s) {
        std::transform (s.begin(), s.end(), s.begin(), lowerAscii);
        return s;
    };
    const auto wanted = low (name);
    for (std::size_t i = 0; i < list.size(); ++i)
        if (low (list[i].name) == wanted)
            return (int) i;
    return -1;
}
} // namespace lynx::presetcycle
