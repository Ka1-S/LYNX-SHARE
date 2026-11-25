#include "core/NativeData.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>

namespace lynx::nativedata
{
namespace
{
struct OwnSet
{
    InstrumentKind kind;
    const char* name;
    int revision; // the newest
};
constexpr OwnSet ownSets[] = {
    { InstrumentKind::transistorbass, "transistorbass", 1 },
    { InstrumentKind::minisynth, "minisynth", 1 },
    { InstrumentKind::sawer, "sawer", 1 },
    { InstrumentKind::bassdrum, "bassdrum", 1 },
    { InstrumentKind::gms, "gms", 1 },
    { InstrumentKind::morphine, "morphine", 1 },
};

const OwnSet* setOf (InstrumentKind kind) noexcept
{
    for (const auto& s : ownSets)
        if (s.kind == kind)
            return &s;
    return nullptr;
}

std::mutex& rootLock()
{
    static std::mutex m;
    return m;
}
std::string& rootFolder()
{
    static std::string folder;
    return folder;
}

std::filesystem::path pathOf (const std::string& utf8)
{
   #if defined(__cpp_char8_t)
    return std::filesystem::path (std::u8string (utf8.begin(), utf8.end()));
   #else
    return std::filesystem::u8path (utf8);
   #endif
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

bool exists (const std::string& path)
{
    std::error_code ec;
    return ! path.empty() && std::filesystem::exists (pathOf (path), ec);
}

// `tail` ("\Plugins\Fruity\…", either separator) under `root`, its names matched ignoring case (FL writes them for a
// case-insensitive file system); "" when it is not there
std::string under (const std::string& root, const std::string& tail)
{
    namespace fs = std::filesystem;
    const auto separator = root.find ('\\') != std::string::npos ? '\\' : '/';
    auto at = root;
    while (! at.empty() && (at.back() == '/' || at.back() == '\\'))
        at.pop_back();
    std::string part;
    const auto step = [&] {
        if (part.empty())
            return true;
        if (part == "." || part == "..") // (never out of the installation)
            return false;
        auto next = at + separator + part;
        if (! exists (next))
        {
            std::error_code ec;
            bool found = false;
            for (fs::directory_iterator it (pathOf (at), ec), end; ! ec && it != end; it.increment (ec))
                if (const auto name = it->path().filename().string(); lower (name) == lower (part))
                {
                    next = at + separator + name;
                    found = true;
                    break;
                }
            if (! found)
                return false;
        }
        at = next;
        part.clear();
        return true;
    };
    for (const auto c : tail)
    {
        if (c != '/' && c != '\\')
            part.push_back (c);
        else if (! step())
            return {};
    }
    return step() ? at : std::string();
}
} // namespace

bool isOwn (const std::string& path) noexcept { return path.rfind (scheme, 0) == 0; }

std::string ownPath (InstrumentKind kind)
{
    const auto* s = setOf (kind);
    return s != nullptr ? std::string (scheme) + s->name + "/" + std::to_string (s->revision) : std::string();
}

bool hasOwnSet (InstrumentKind kind) { return setOf (kind) != nullptr; }

int ownRevision (const std::string& path, InstrumentKind kind) noexcept
{
    const auto* s = setOf (kind);
    if (s == nullptr || ! isOwn (path))
        return 0;
    const std::string head = std::string (scheme) + s->name + "/";
    if (path.rfind (head, 0) != 0 || path.size() == head.size())
        return 0;
    int n = 0;
    for (auto i = head.size(); i < path.size(); ++i)
    {
        if (path[i] < '0' || path[i] > '9' || n > 100000)
            return 0;
        n = n * 10 + (path[i] - '0');
    }
    return std::min (n, s->revision);
}

void setFactoryData (const std::string& folder)
{
    const std::lock_guard<std::mutex> g (rootLock());
    rootFolder() = folder;
}

std::string factoryData()
{
    const std::lock_guard<std::mutex> g (rootLock());
    return rootFolder();
}

bool isFlFile (const std::string& path)
{
    auto plain = lower (path);
    std::replace (plain.begin(), plain.end(), '\\', '/');
    return plain.find ("/plugins/fruity/") != std::string::npos;
}

std::string locate (const std::string& path)
{
    if (path.empty() || isOwn (path) || exists (path))
        return path;
    auto plain = lower (path); // '/' only, lower case: the place of "/plugins/fruity/" in either spelling
    std::replace (plain.begin(), plain.end(), '\\', '/');
    if (path.find ('\\') != std::string::npos) // FL's spelling on a system with '/' separators
    {
        auto local = path;
        std::replace (local.begin(), local.end(), '\\', '/');
        if (exists (local))
            return local;
    }
    const std::string marker = "/plugins/fruity/";
    const auto at = plain.rfind (marker);
    const auto root = factoryData();
    if (at == std::string::npos || root.empty())
        return path;
    const auto there = under (root, path.substr (at));
    return ! there.empty() ? there : path;
}
} // namespace lynx::nativedata
