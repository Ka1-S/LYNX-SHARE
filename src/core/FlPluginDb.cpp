#include "core/FlPluginDb.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <map>
#include <sstream>

#include "core/FlWrapper.h"

namespace lynx
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// backslashes, lower case, no trailing separator: two spellings of one Windows path compare equal
std::string pathKey (std::string s)
{
    std::replace (s.begin(), s.end(), '/', '\\');
    while (! s.empty() && s.back() == '\\')
        s.pop_back();
    return lower (s);
}

std::string fileName (const std::string& path)
{
    const auto slash = path.find_last_of ("\\/");
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

std::string stem (const std::string& path)
{
    auto name = fileName (path);
    const auto dot = name.find_last_of ('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr (0, dot);
}

std::string parentOf (const std::string& path)
{
    const auto slash = path.find_last_of ("\\/");
    return slash == std::string::npos ? std::string() : path.substr (0, slash);
}

bool endsWith (const std::string& s, const std::string& tail)
{
    return s.size() >= tail.size() && s.compare (s.size() - tail.size(), tail.size(), tail) == 0;
}

std::string trim (const std::string& s)
{
    auto b = s.find_first_not_of (" \t\r\n");
    auto e = s.find_last_not_of (" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr (b, e - b + 1);
}

int hexDigit (char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char) std::tolower ((unsigned char) c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// "{01234567-89AB-...}" or 32 hex digits → 16 bytes in the order written
bool parseGuid (const std::string& text, std::array<std::uint8_t, 16>& out)
{
    std::string hex;
    for (auto c : text)
        if (hexDigit (c) >= 0)
            hex += c;
        else if (c != '{' && c != '}' && c != '-' && c != ' ')
            return false;
    if (hex.size() != 32)
        return false;
    for (std::size_t i = 0; i < 16; ++i)
        out[i] = (std::uint8_t) (hexDigit (hex[2 * i]) * 16 + hexDigit (hex[2 * i + 1]));
    return std::any_of (out.begin(), out.end(), [] (auto b) { return b != 0; });
}

// the same GUID in COM memory order (first three fields little-endian)
std::array<std::uint8_t, 16> comOrder (const std::array<std::uint8_t, 16>& g)
{
    auto c = g;
    std::swap (c[0], c[3]);
    std::swap (c[1], c[2]);
    std::swap (c[4], c[5]);
    std::swap (c[6], c[7]);
    return c;
}

bool isPluginBinary (const std::string& value)
{
    return pluginFormatFromPath (value) != ForeignPlugin::Format::none && value.find_first_of ("\\/") != std::string::npos;
}
} // namespace

ForeignPlugin::Format pluginFormatFromPath (const std::string& path)
{
    const auto p = lower (trim (path));
    if (endsWith (p, ".vst3"))
        return ForeignPlugin::Format::vst3;
    if (endsWith (p, ".clap"))
        return ForeignPlugin::Format::clap;
    if (endsWith (p, ".dll"))
        return ForeignPlugin::Format::vst2;
    return ForeignPlugin::Format::none;
}

std::string pluginNameKey (const std::string& name)
{
    std::string key;
    for (auto c : lower (name))
        if (std::isalnum ((unsigned char) c))
            key += c;
    // architecture / format suffixes FL and vendors append to file names
    for (bool again = true; again;)
    {
        again = false;
        for (const char* suffix : { "x64", "x8664", "64bit", "64", "vst3", "vst", "clap" })
            if (key.size() > std::string (suffix).size() + 2 && endsWith (key, suffix))
            {
                key.resize (key.size() - std::string (suffix).size());
                again = true;
            }
    }
    return key;
}

std::vector<FlPluginDbEntry> parseFlPluginNfo (const std::string& text, const std::string& sourcePath)
{
    std::map<std::string, std::string> values;
    std::istringstream in (text);
    for (std::string line; std::getline (in, line);)
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        values[lower (trim (line.substr (0, eq)))] = trim (line.substr (eq + 1));
    }

    const auto folder = pathKey (sourcePath);
    const auto folderRole = folder.find ("\\generators\\") != std::string::npos ? 2 : folder.find ("\\effects\\") != std::string::npos ? 1 : 0;

    // indexed keys: ps_file_<field>_<N>
    std::map<int, std::map<std::string, std::string>> files;
    for (const auto& [key, value] : values)
    {
        if (key.rfind ("ps_file_", 0) != 0)
            continue;
        const auto under = key.find_last_of ('_');
        const auto index = key.substr (under + 1);
        if (index.empty() || ! std::all_of (index.begin(), index.end(), [] (char c) { return std::isdigit ((unsigned char) c); }))
            continue;
        files[std::stoi (index)][key.substr (8, under - 8)] = value;
    }

    std::vector<FlPluginDbEntry> out;
    for (auto& [index, fields] : files)
    {
        FlPluginDbEntry e;
        // the binary: any *name* field holding a plugin path (FL's field names are "not confirmed" beyond these)
        for (const auto* field : { "filename", "name", "path" })
            if (auto it = fields.find (field); it != fields.end() && isPluginBinary (it->second))
            {
                e.path = it->second;
                break;
            }
        if (e.path.empty())
            for (const auto& [field, value] : fields)
                if (field.find ("name") != std::string::npos && isPluginBinary (value))
                {
                    e.path = value;
                    break;
                }
        if (e.path.empty())
            continue;

        e.format = pluginFormatFromPath (e.path);
        e.source = sourcePath;
        e.vendor = fields["vendorname"];
        e.name = fields.count ("pluginname") && ! fields["pluginname"].empty() ? fields["pluginname"] : values["ps_name"];
        if (e.name.empty())
            e.name = stem (e.path);
        const auto& type = fields["type"];
        e.role = type == "1" ? 1 : type == "2" ? 2 : folderRole;
        if (auto it = fields.find ("guid"); it != fields.end())
            e.hasClassId = parseGuid (it->second, e.classId);
        if (auto it = fields.find ("magic"); it != fields.end() && ! it->second.empty())
        {
            const auto& m = it->second;
            if (m.size() == 4)
                e.vst2Id = ((std::uint32_t) (unsigned char) m[0] << 24) | ((std::uint32_t) (unsigned char) m[1] << 16)
                           | ((std::uint32_t) (unsigned char) m[2] << 8) | (std::uint32_t) (unsigned char) m[3];
            else if (std::all_of (m.begin(), m.end(), [] (char c) { return std::isdigit ((unsigned char) c); }) && m.size() <= 10)
                e.vst2Id = (std::uint32_t) std::stoul (m);
        }
        out.push_back (std::move (e));
    }
    return out;
}

bool parseFlPluginFst (std::span<const std::uint8_t> bytes, FlPluginDbEntry& out)
{
    try
    {
        flp::EventReader reader (bytes);
        flp::Event event;
        while (reader.next (event))
        {
            if (event.id != 0xD5 || event.data.size() < 16)
                continue;
            ForeignPlugin plugin;
            if (! parseFlWrapper ({ event.data.begin(), event.data.end() }, plugin) || plugin.format == ForeignPlugin::Format::none
                || plugin.format == ForeignPlugin::Format::other)
                continue;
            out.name = plugin.name;
            out.path = plugin.path;
            out.format = plugin.format;
            out.classId = plugin.classId;
            out.hasClassId = plugin.hasClassId;
            out.clapId = plugin.clapId;
            out.vst2Id = plugin.vst2Id;
            return ! out.name.empty() || ! out.path.empty();
        }
    }
    catch (const flp::Error&)
    {
    }
    return false;
}

void FlPluginDb::add (const FlPluginDbEntry& entry)
{
    if (entry.path.empty() && entry.name.empty())
        return;
    const auto key = pathKey (entry.path);
    for (auto& e : list)
        if (! key.empty() && pathKey (e.path) == key && (e.name.empty() || entry.name.empty() || pluginNameKey (e.name) == pluginNameKey (entry.name)))
        {
            if (e.vendor.empty()) e.vendor = entry.vendor;
            if (e.role == 0) e.role = entry.role;
            if (! e.hasClassId && entry.hasClassId) { e.classId = entry.classId; e.hasClassId = true; }
            if (e.clapId.empty()) e.clapId = entry.clapId;
            if (e.vst2Id == 0) e.vst2Id = entry.vst2Id;
            if (e.name.empty()) e.name = entry.name;
            return;
        }
    list.push_back (entry);
}

const FlPluginDbEntry* FlPluginDb::match (const ForeignPlugin& plugin, bool instrument) const
{
    // 1) identity
    for (const auto& e : list)
    {
        if (plugin.format == ForeignPlugin::Format::vst3 && plugin.hasClassId && e.hasClassId && e.format == ForeignPlugin::Format::vst3
            && (e.classId == plugin.classId || comOrder (e.classId) == plugin.classId))
            return &e;
        if (plugin.format == ForeignPlugin::Format::clap && ! plugin.clapId.empty() && e.clapId == plugin.clapId)
            return &e;
        if (plugin.format == ForeignPlugin::Format::vst2 && plugin.vst2Id != 0 && e.vst2Id == plugin.vst2Id && e.format == ForeignPlugin::Format::vst2)
            return &e;
    }
    // 2) the file the project saved
    if (const auto saved = pathKey (plugin.path); ! saved.empty())
        for (const auto& e : list)
            if (pathKey (e.path) == saved)
                return &e;
    // 3) name, same role, preferring the same format
    const auto key = pluginNameKey (plugin.name.empty() ? stem (plugin.path) : plugin.name);
    if (key.empty())
        return nullptr;
    const auto role = instrument ? 2 : 1;
    const FlPluginDbEntry* byName = nullptr;
    for (const auto& e : list)
    {
        if (e.role != 0 && e.role != role)
            continue;
        if (pluginNameKey (e.name) != key && pluginNameKey (stem (e.path)) != key)
            continue;
        if (e.format == plugin.format)
            return &e;
        if (byName == nullptr)
            byName = &e;
    }
    return byName;
}

std::vector<std::string> FlPluginDb::pluginFolders() const
{
    std::vector<std::string> folders;
    for (const auto& e : list)
    {
        if (e.path.empty())
            continue;
        auto folder = parentOf (e.path);
        // a VST3 bundle: Foo.vst3\Contents\x86_64-win\Foo.vst3 → the folder holding the bundle
        const auto l = lower (e.path);
        if (const auto bundle = l.find (".vst3\\"); bundle != std::string::npos)
            folder = parentOf (e.path.substr (0, bundle + 5));
        else if (const auto bundle2 = l.find (".vst3/"); bundle2 != std::string::npos)
            folder = parentOf (e.path.substr (0, bundle2 + 5));
        if (folder.empty())
            continue;
        if (std::none_of (folders.begin(), folders.end(), [&] (const auto& f) { return pathKey (f) == pathKey (folder); }))
            folders.push_back (folder);
    }
    return folders;
}

bool pluginFileMatches (const std::string& file, const ForeignPlugin& plugin, const FlPluginDbEntry* entry)
{
    const auto f = pathKey (file);
    if (f.empty())
        return false;
    if (f == pathKey (plugin.path) || (entry != nullptr && f == pathKey (entry->path)))
        return true;
    const auto key = pluginNameKey (stem (file));
    if (key.empty())
        return false;
    // a Waves sub-plugin lives in whichever WaveShell version is installed (a project saved with the 15.5 shell opens
    // with the 16.0 one): every shell file is worth describing (round 113)
    auto shell = [] (const std::string& k) { return k.rfind ("waveshell", 0) == 0; };
    if (shell (key) && (shell (pluginNameKey (plugin.name)) || shell (pluginNameKey (stem (plugin.path)))))
        return true;
    return key == pluginNameKey (plugin.name) || key == pluginNameKey (stem (plugin.path))
           || (entry != nullptr && (key == pluginNameKey (entry->name) || key == pluginNameKey (stem (entry->path))));
}

std::vector<FlpPluginUse> flpForeignPlugins (const flp::Project& project)
{
    std::vector<FlpPluginUse> uses;
    for (const auto& c : project.channels)
    {
        ForeignPlugin p;
        if (c.pluginName == "Fruity Wrapper" && parseFlWrapper (c.pluginState, p)
            && (p.format == ForeignPlugin::Format::vst2 || p.format == ForeignPlugin::Format::vst3 || p.format == ForeignPlugin::Format::clap))
            uses.push_back ({ std::move (p), true });
    }
    for (const auto& insert : project.mixer)
        for (const auto& slot : insert.slots)
        {
            ForeignPlugin p;
            if (slot.plugin == "Fruity Wrapper" && parseFlWrapper (slot.state, p)
                && (p.format == ForeignPlugin::Format::vst2 || p.format == ForeignPlugin::Format::vst3 || p.format == ForeignPlugin::Format::clap))
                uses.push_back ({ std::move (p), false });
        }
    return uses;
}

std::string locateFlSample (const std::string& flPath, const FlPathRoots& roots, const std::string& projectFolder,
                            const std::function<bool (const std::string&)>& fileExists)
{
    if (flPath.empty() || ! fileExists)
        return {};

    const auto resolved = resolveFlPath (flPath, roots);
    if (! resolved.empty() && fileExists (resolved))
        return resolved;

    auto path = resolved.empty() ? flPath : resolved;
    std::replace (path.begin(), path.end(), '/', '\\');
    const auto l = lower (path);
    std::vector<std::string> candidates;

    // another machine's FL user data folder ("...\Image-Line\FL Studio\...") or FL install ("...\Image-Line\FL Studio 2024\...")
    if (const auto il = l.find ("\\image-line\\fl studio"); il != std::string::npos)
    {
        const auto afterMarker = il + std::string ("\\image-line\\").size();
        const auto end = l.find ('\\', afterMarker);
        if (end != std::string::npos)
        {
            const auto component = l.substr (afterMarker, end - afterMarker);
            const auto rest = path.substr (end);
            if (component == "fl studio" && ! roots.userData.empty())
                candidates.push_back (roots.userData + rest);
            else if (! roots.factoryData.empty())
                candidates.push_back (roots.factoryData + rest);
        }
    }
    // another machine's FL Shared data folder ("...\Image-Line\Downloads\...", "...\Image-Line\My kicks\..."): this PC's
    if (const auto il = l.find ("\\image-line\\"); il != std::string::npos && ! roots.sharedData.empty())
    {
        auto shared = roots.sharedData;
        while (! shared.empty() && (shared.back() == '\\' || shared.back() == '/'))
            shared.pop_back();
        candidates.push_back (shared + path.substr (il + std::string ("\\image-line").size()));
    }
    // another user's profile: X:\Users\<name>\rest → this user's profile
    if (! roots.home.empty() && l.size() > 9 && l.compare (1, 8, ":\\users\\") == 0)
        if (const auto end = l.find ('\\', 9); end != std::string::npos)
            candidates.push_back (roots.home + path.substr (end));

    // next to the project: the file itself, then with its last folders (Samples\kick.wav, Audio\Vocals\take 1.wav)
    if (! projectFolder.empty())
    {
        auto base = projectFolder;
        while (! base.empty() && (base.back() == '\\' || base.back() == '/'))
            base.pop_back();
        std::string tail;
        auto rest = path;
        for (int depth = 0; depth < 4; ++depth)
        {
            const auto slash = rest.find_last_of ('\\');
            const auto part = slash == std::string::npos ? rest : rest.substr (slash + 1);
            if (part.empty() || part.find (':') != std::string::npos)
                break;
            tail = tail.empty() ? part : part + "\\" + tail;
            candidates.push_back (base + "\\" + tail);
            if (slash == std::string::npos)
                break;
            rest = rest.substr (0, slash);
        }
    }

    for (const auto& c : candidates)
        if (fileExists (c))
            return c;
    return {};
}
// --- round 190sb: the database kept between starts ---------------------------------------------------------------

namespace
{
constexpr std::string_view cacheHeader = "LYNX FL plugin database cache 1";

// a field of a cache line: \ t n escaped, so a name with a tab or a line break stays one field
void putField (std::string& out, const std::string& s)
{
    out += '\t';
    for (const auto c : s)
    {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\t')
            out += "\\t";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
}

std::vector<std::string> splitFields (std::string_view line)
{
    std::vector<std::string> fields (1);
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const auto c = line[i];
        if (c == '\t')
            fields.emplace_back();
        else if (c == '\\' && i + 1 < line.size())
        {
            const auto n = line[++i];
            fields.back() += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        }
        else
            fields.back() += c;
    }
    return fields;
}

bool toInt64 (const std::string& s, std::int64_t& out)
{
    if (s.empty())
        return false;
    std::size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size())
        return false;
    std::int64_t v = 0;
    for (; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9' || v > (INT64_MAX - 9) / 10)
            return false;
        v = v * 10 + (s[i] - '0');
    }
    out = s[0] == '-' ? -v : v;
    return true;
}
} // namespace

const std::vector<FlPluginDbEntry>* FlPluginDbCache::find (const FlPluginDbFileStamp& stamp) const
{
    const auto it = files.find (stamp.path);
    if (it == files.end() || it->second.size != stamp.size || it->second.modified != stamp.modified)
        return nullptr;
    return &it->second.entries;
}

void FlPluginDbCache::put (const FlPluginDbFileStamp& stamp, std::vector<FlPluginDbEntry> entries)
{
    files[stamp.path] = File { stamp.size, stamp.modified, std::move (entries) };
}

std::string FlPluginDbCache::serialise() const
{
    static const char* hex = "0123456789abcdef";
    std::string out (cacheHeader);
    out += '\n';
    for (const auto& [path, file] : files)
    {
        out += 'F';
        putField (out, path);
        putField (out, std::to_string (file.size));
        putField (out, std::to_string (file.modified));
        putField (out, std::to_string (file.entries.size()));
        out += '\n';
        for (const auto& e : file.entries)
        {
            std::string id;
            for (const auto b : e.classId)
                id += hex[b >> 4], id += hex[b & 15];
            out += 'E';
            putField (out, e.name);
            putField (out, e.vendor);
            putField (out, e.path);
            putField (out, e.source);
            putField (out, std::to_string ((int) e.format));
            putField (out, std::to_string (e.role));
            putField (out, id);
            putField (out, e.hasClassId ? "1" : "0");
            putField (out, e.clapId);
            putField (out, std::to_string (e.vst2Id));
            out += '\n';
        }
    }
    out += "end\n"; // (a file cut short is not a cache)
    return out;
}

FlPluginDbCache FlPluginDbCache::parse (std::string_view text)
{
    FlPluginDbCache cache, none;
    std::size_t pos = 0;
    const auto nextLine = [&] (std::string_view& line) {
        if (pos >= text.size())
            return false;
        const auto end = text.find ('\n', pos);
        line = text.substr (pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        pos = end == std::string_view::npos ? text.size() : end + 1;
        return true;
    };
    const auto hexValue = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string_view line;
    if (! nextLine (line) || line != cacheHeader)
        return none;
    while (nextLine (line))
    {
        if (line == "end")
            return pos == text.size() ? cache : none;
        const auto f = splitFields (line);
        std::int64_t size = 0, modified = 0, count = 0;
        if (f.size() != 5 || f[0] != "F" || ! toInt64 (f[2], size) || ! toInt64 (f[3], modified) || ! toInt64 (f[4], count) || count < 0 || count > 4096)
            return none;
        File file { size, modified, {} };
        for (std::int64_t i = 0; i < count; ++i)
        {
            if (! nextLine (line))
                return none;
            const auto e = splitFields (line);
            std::int64_t format = 0, role = 0, vst2 = 0;
            if (e.size() != 11 || e[0] != "E" || ! toInt64 (e[5], format) || format < 0 || format > (int) ForeignPlugin::Format::other
                || ! toInt64 (e[6], role) || ! toInt64 (e[10], vst2) || vst2 < 0 || vst2 > (std::int64_t) UINT32_MAX || e[7].size() != 32
                || (e[8] != "0" && e[8] != "1"))
                return none;
            FlPluginDbEntry entry;
            entry.name = e[1];
            entry.vendor = e[2];
            entry.path = e[3];
            entry.source = e[4];
            entry.format = (ForeignPlugin::Format) format;
            entry.role = (int) role;
            for (std::size_t b = 0; b < 16; ++b)
            {
                const auto hi = hexValue (e[7][2 * b]), lo = hexValue (e[7][2 * b + 1]);
                if (hi < 0 || lo < 0)
                    return none;
                entry.classId[b] = (std::uint8_t) (hi * 16 + lo);
            }
            entry.hasClassId = e[8] == "1";
            entry.clapId = e[9];
            entry.vst2Id = (std::uint32_t) vst2;
            file.entries.push_back (std::move (entry));
        }
        cache.files[f[1]] = std::move (file);
    }
    return none; // (no "end" line)
}
} // namespace lynx
