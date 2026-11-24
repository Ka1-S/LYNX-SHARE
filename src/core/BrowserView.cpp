#include "core/BrowserView.h"


#include <algorithm>
#include <cctype>
#include <sstream>

namespace lynx::browserview
{
namespace
{
std::string extensionOf (const std::string& fileName)
{
    const auto dot = fileName.find_last_of ('.');
    if (dot == std::string::npos || dot + 1 >= fileName.size())
        return {};
    auto ext = fileName.substr (dot + 1);
    std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return ext;
}

bool among (const std::string& ext, std::initializer_list<const char*> list)
{
    return std::any_of (list.begin(), list.end(), [&ext] (const char* e) { return ext == e; });
}

// (".speech": FL's speech presets, spoken into a sample by core/Speech.h; round 168b)
bool isAudioExt (const std::string& ext) { return among (ext, { "wav", "flac", "ogg", "mp3", "aif", "aiff", "wv", "speech", "ncw" }); }
// (and other samplers' instruments, opened as DirectWave channels: core/SamplerImport.h, round 190ii)
bool isFontExt (const std::string& ext) { return among (ext, { "sf2", "sf3", "dwp", "dwb", "gig" }); }
bool isPresetExt (const std::string& ext) { return among (ext, { "fst" }); }
bool isProjectExt (const std::string& ext) { return among (ext, { "lynx", "flp", "mid", "midi" }); }
} // namespace

const char* contentFilterId (ContentFilter filter)
{
    switch (filter)
    {
        case ContentFilter::audio: return "audio";
        case ContentFilter::samples: return "samples";
        case ContentFilter::presets: return "presets";
        case ContentFilter::projects: return "projects";
        default: return "all";
    }
}

const char* contentFilterLabel (ContentFilter filter)
{
    switch (filter)
    {
        case ContentFilter::audio: return "Audio files only";
        case ContentFilter::samples: return "Samples and SoundFonts";
        case ContentFilter::presets: return "Presets";
        case ContentFilter::projects: return "Projects and MIDI";
        default: return "All files";
    }
}

ContentFilter contentFilterFromId (const std::string& id)
{
    for (int i = 0; i < contentFilterCount; ++i)
        if (id == contentFilterId ((ContentFilter) i))
            return (ContentFilter) i;
    return ContentFilter::all;
}

bool listed (const std::string& fileName, ContentFilter filter)
{
    const auto ext = extensionOf (fileName);
    const auto audio = isAudioExt (ext), font = isFontExt (ext), preset = isPresetExt (ext), project = isProjectExt (ext);
    switch (filter)
    {
        case ContentFilter::audio: return audio;
        case ContentFilter::samples: return audio || font;
        case ContentFilter::presets: return preset || font;
        case ContentFilter::projects: return project;
        default: return audio || font || preset || project;
    }
}

const std::vector<TypeGroup>& typeGroups()
{
    static const std::vector<TypeGroup> groups {
        { "Audio", { "wav", "flac", "ogg", "mp3", "aif", "aiff", "wv", "ncw", "speech" } },
        { "MIDI", { "mid", "midi" } },
        { "Projects", { "lynx", "flp" } },
        { "Presets", { "fst" } },
        { "SoundFonts and instruments", { "sf2", "sf3", "dwp", "dwb", "gig" } },
    };
    return groups;
}

bool typeChosen (const std::string& fileName, const std::set<std::string>& types)
{
    return types.empty() || types.count (extensionOf (fileName)) > 0;
}

namespace
{
std::string lowerAscii (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trimmed (const std::string& s)
{
    const auto a = s.find_first_not_of (" \t");
    const auto b = s.find_last_not_of (" \t");
    return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
}

// '*' = any run of characters (both lower-case already)
bool globMatches (const std::string& text, const std::string& pattern)
{
    std::size_t t = 0, p = 0, star = std::string::npos, mark = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            mark = t;
        }
        else if (p < pattern.size() && pattern[p] == text[t])
        {
            ++p;
            ++t;
        }
        else if (star != std::string::npos)
        {
            p = star + 1;
            t = ++mark;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}
} // namespace

std::vector<std::string> parseFolderNames (const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in (text);
    for (std::string part; std::getline (in, part, ',');)
    {
        auto name = trimmed (part);
        name.erase (std::remove_if (name.begin(), name.end(), [] (char c) { return c == '\n' || c == '\r' || c == '\t' || c == '/' || c == '\\'; }), name.end());
        if (name.empty() || name.find_first_not_of ('*') == std::string::npos) // ("*" alone would hide everything)
            continue;
        if (std::none_of (out.begin(), out.end(), [&name] (const std::string& o) { return lowerAscii (o) == lowerAscii (name); }))
            out.push_back (name);
    }
    return out;
}

std::string joinFolderNames (const std::vector<std::string>& names)
{
    std::string out;
    for (const auto& n : names)
        out += (out.empty() ? "" : ", ") + n;
    return out;
}

bool folderNameHidden (const std::string& name, const std::vector<std::string>& names)
{
    if (names.empty() || name.empty())
        return false;
    const auto lowered = lowerAscii (name);
    return std::any_of (names.begin(), names.end(), [&lowered] (const std::string& n) { return globMatches (lowered, lowerAscii (n)); });
}

bool pathHidden (const std::string& path, const std::string& root, const std::vector<std::string>& names)
{
    if (names.empty())
        return false;
    auto rest = path;
    if (! root.empty() && path.size() > root.size() && lowerAscii (path.substr (0, root.size())) == lowerAscii (root))
        rest = path.substr (root.size());
    std::vector<std::string> parts;
    std::string part;
    for (const auto c : rest)
    {
        if (c == '/' || c == '\\')
        {
            if (! part.empty())
                parts.push_back (part);
            part.clear();
        }
        else
            part += c;
    }
    // (the last name is the file's own; a folder's path ends in its own name, which is checked too when it ends in a slash)
    for (const auto& p : parts)
        if (folderNameHidden (p, names))
            return true;
    return false;
}

bool sampleBrowserShowsByDefault (RootKind kind)
{
    return kind == RootKind::flSamples || kind == RootKind::music || kind == RootKind::downloads || kind == RootKind::user;
}

void View::setShown (const std::string& id, bool shown)
{
    if (shown)
        hidden.erase (id);
    else
        hidden.insert (id);
}

std::string View::toText() const
{
    std::ostringstream out;
    out << "filter=" << contentFilterId (filter) << "\n";
    out << "open=" << (open ? 1 : 0) << "\n";
    out << "bounds=" << x << "," << y << "," << width << "," << height << "\n";
    for (const auto& id : hidden)
        out << "hidden=" << id << "\n";
    for (const auto& path : excluded)
        out << "excluded=" << path << "\n";
    for (const auto& t : types)
        out << "type=" << t << "\n";
    for (const auto& n : hiddenFolderNames)
        out << "hideFolder=" << n << "\n";
    return out.str();
}

View View::fromText (const std::string& text)
{
    View v;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        auto trim = [] (std::string s) {
            const auto a = s.find_first_not_of (" \t");
            const auto b = s.find_last_not_of (" \t");
            return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
        };
        const auto key = trim (line.substr (0, eq));
        const auto value = trim (line.substr (eq + 1));
        if (key == "filter")
            v.filter = contentFilterFromId (value);
        else if (key == "open")
            v.open = value == "1";
        else if (key == "hidden" && ! value.empty())
            v.hidden.insert (value);
        else if (key == "excluded" && ! value.empty())
            v.excluded.insert (value);
        else if (key == "type" && ! value.empty())
        {
            auto t = value;
            std::transform (t.begin(), t.end(), t.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            v.types.insert (t);
        }
        else if (key == "hideFolder" && ! value.empty())
        {
            for (const auto& n : parseFolderNames (value))
                if (std::find (v.hiddenFolderNames.begin(), v.hiddenFolderNames.end(), n) == v.hiddenFolderNames.end())
                    v.hiddenFolderNames.push_back (n);
        }
        else if (key == "bounds")
        {
            int n[4] {};
            char sep = 0;
            std::istringstream b (value);
            if ((b >> n[0] >> sep >> n[1] >> sep >> n[2] >> sep >> n[3]) && n[2] >= 0 && n[3] >= 0)
            {
                v.x = n[0];
                v.y = n[1];
                v.width = n[2];
                v.height = n[3];
            }
        }
    }
    return v;
}
MergedListing mergeListings (const std::vector<Listing>& listings)
{
    auto lower = [] (std::string s) {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);
        return s;
    };
    MergedListing out;
    auto add = [&] (std::vector<MergedEntry>& entries, const std::vector<std::string>& names, int from, bool merge) {
        for (const auto& name : names)
        {
            const auto key = lower (name);
            const auto it = std::find_if (entries.begin(), entries.end(), [&] (const MergedEntry& e) { return lower (e.name) == key; });
            if (it == entries.end())
                entries.push_back ({ name, from, {} });
            else if (merge && it->from != from && std::find (it->also.begin(), it->also.end(), from) == it->also.end())
                it->also.push_back (from);
        }
    };
    for (int i = 0; i < (int) listings.size(); ++i)
    {
        add (out.folders, listings[(std::size_t) i].folders, i, true);
        add (out.files, listings[(std::size_t) i].files, i, false);
    }
    return out;
}
bool migrateMergedRoot (View& view, const std::string& rootId, const std::vector<std::string>& formerIds)
{
    bool changed = false;
    for (const auto& id : formerIds)
        if (id != rootId && view.hidden.erase (id) > 0)
        {
            view.excluded.insert (id);
            changed = true;
        }
    return changed;
}
} // namespace lynx::browserview
