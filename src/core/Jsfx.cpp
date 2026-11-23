#include "core/Jsfx.h"

#include <algorithm>
#include <cstring>

namespace lynx::jsfx
{
namespace
{
char fold (char c) noexcept { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; }

bool sameText (std::string_view a, std::string_view b, bool caseInsensitive) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (caseInsensitive ? fold (a[i]) != fold (b[i]) : a[i] != b[i])
            return false;
    return true;
}

std::string lowerWord (std::string_view s)
{
    std::string out (s);
    for (auto& c : out)
        c = fold (c);
    return out;
}

void putU32 (std::string& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back ((char) ((v >> (8 * i)) & 0xff));
}
void putU64 (std::string& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out.push_back ((char) ((v >> (8 * i)) & 0xff));
}

struct Reader
{
    const unsigned char* p;
    std::size_t left;
    bool u32 (std::uint32_t& v)
    {
        if (left < 4) return false;
        v = (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24);
        p += 4, left -= 4;
        return true;
    }
    bool u64 (std::uint64_t& v)
    {
        if (left < 8) return false;
        v = 0;
        for (int i = 0; i < 8; ++i)
            v |= (std::uint64_t) p[i] << (8 * i);
        p += 8, left -= 8;
        return true;
    }
    bool bytes (std::string& s, std::size_t n)
    {
        if (left < n) return false;
        s.assign ((const char*) p, n);
        p += n, left -= n;
        return true;
    }
};

constexpr char magic[4] = { 'L', 'J', 'S', 'X' };
constexpr std::uint32_t stateVersion = 1;
} // namespace

std::string normalise (std::string_view path)
{
    std::string s (path);
    std::replace (s.begin(), s.end(), '\\', '/');
    // the prefix kept as it is: "//server" (UNC), "C:" (a drive), "/" (the root)
    std::string prefix;
    std::size_t at = 0;
    if (s.size() >= 2 && s[0] == '/' && s[1] == '/')
        prefix = "//", at = 2;
    else if (s.size() >= 2 && s[1] == ':')
    {
        prefix = s.substr (0, 2), at = 2;
        if (at < s.size() && s[at] == '/')
            prefix += '/', ++at;
    }
    else if (! s.empty() && s[0] == '/')
        prefix = "/", at = 1;
    std::vector<std::string> parts;
    while (at <= s.size())
    {
        const auto next = std::min (s.find ('/', at), s.size());
        const auto part = s.substr (at, next - at);
        if (part == "..")
        {
            if (! parts.empty() && parts.back() != "..")
                parts.pop_back();
            else if (prefix.empty())
                parts.push_back (part); // (a relative path that leaves its start: kept, it never lies inside a root)
        }
        else if (! part.empty() && part != ".")
            parts.push_back (part);
        at = next + 1;
    }
    std::string out = prefix;
    for (std::size_t i = 0; i < parts.size(); ++i)
        out += (i > 0 ? "/" : "") + parts[i];
    return out;
}

bool isInside (std::string_view path, std::string_view root, bool caseInsensitive)
{
    const auto p = normalise (path), r = normalise (root);
    if (r.empty() || p.size() < r.size())
        return false;
    if (! sameText (std::string_view (p).substr (0, r.size()), r, caseInsensitive))
        return false;
    return p.size() == r.size() || r.back() == '/' || p[r.size()] == '/';
}

Located locate (std::string_view path, const std::vector<std::string>& roots, bool caseInsensitive)
{
    Located best;
    std::size_t bestLength = 0;
    const auto p = normalise (path);
    for (std::size_t i = 0; i < roots.size(); ++i)
    {
        const auto r = normalise (roots[i]);
        if (r.empty() || ! isInside (p, r, caseInsensitive) || p.size() == r.size())
            continue;
        if (best.root < 0 || r.size() > bestLength)
        {
            best.root = (int) i;
            bestLength = r.size();
            best.relative = p.substr (r.back() == '/' ? r.size() : r.size() + 1);
        }
    }
    return best;
}

std::int32_t uniqueIdOf (std::string_view relative, bool instrument)
{
    std::uint32_t h = 2166136261u;
    const auto add = [&h] (char c) {
        h ^= (unsigned char) c;
        h *= 16777619u;
    };
    for (const char c : std::string_view (instrument ? "generator:" : "effect:"))
        add (c);
    for (const char c : normalise (relative))
        add (fold (c));
    return (std::int32_t) h;
}

std::uint64_t sourceHash (std::string_view bytes)
{
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : bytes)
    {
        h ^= (unsigned char) c;
        h *= 1099511628211ull;
    }
    return h;
}

bool looksLikeJsfx (std::string_view head)
{
    std::size_t at = 0;
    for (int line = 0; line < 64 && at < head.size(); ++line)
    {
        auto end = head.find ('\n', at);
        if (end == std::string_view::npos)
            end = head.size();
        auto text = head.substr (at, end - at);
        while (! text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\xef' || text.front() == '\xbb' || text.front() == '\xbf'))
            text.remove_prefix (1); // (spaces and a UTF-8 byte order mark)
        if (text.size() >= 5 && sameText (text.substr (0, 5), "desc:", true))
            return true;
        // only blank lines, comments and other header lines (key:value) may come before desc:
        if (! text.empty() && text[0] != '/' && text[0] != '\r' && text.find (':') == std::string_view::npos)
            return false;
        if (! text.empty() && text[0] == '@')
            return false;
        at = end + 1;
    }
    return false;
}

bool listAsGenerator (int audioInputs, const std::vector<std::string>& tags, std::string_view relative)
{
    if (audioInputs == 0)
        return true;
    for (const auto& t : tags)
    {
        const auto tag = lowerWord (t);
        if (tag == "instrument" || tag == "synth" || tag == "synthesizer" || tag == "generator" || tag == "midi")
            return true;
    }
    // the folder: REAPER's "midi/...", "synthesis/..."; chokehold's "MIDI/..."
    const auto rel = lowerWord (normalise (relative));
    const auto slash = rel.find ('/');
    if (slash == std::string::npos)
        return false;
    const auto folder = rel.substr (0, slash);
    return folder == "midi" || folder == "synthesis" || folder == "synth" || folder == "instruments" || folder == "generator";
}

bool listAsEffect (int audioInputs, int audioOutputs)
{
    return audioInputs > 0 || audioOutputs == 0;
}

std::string encodeState (const SavedState& state)
{
    std::string out (magic, magic + 4);
    putU32 (out, stateVersion);
    putU32 (out, (std::uint32_t) state.relative.size());
    out += state.relative;
    putU64 (out, state.source);
    putU32 (out, (std::uint32_t) state.sliders.size());
    for (const auto& [index, value] : state.sliders)
    {
        putU32 (out, index);
        std::uint64_t bits;
        std::memcpy (&bits, &value, sizeof bits);
        putU64 (out, bits);
    }
    putU32 (out, (std::uint32_t) state.serialized.size());
    out += state.serialized;
    return out;
}

bool decodeState (const void* data, std::size_t size, SavedState& state)
{
    if (data == nullptr || size < 8 || std::memcmp (data, magic, 4) != 0)
        return false;
    Reader r { (const unsigned char*) data + 4, size - 4 };
    SavedState s;
    std::uint32_t version = 0, length = 0, count = 0;
    if (! r.u32 (version) || version < 1 || ! r.u32 (length) || ! r.bytes (s.relative, length) || ! r.u64 (s.source) || ! r.u32 (count))
        return false;
    if ((std::size_t) count > r.left / 12)
        return false;
    s.sliders.reserve (count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        std::uint32_t index = 0;
        std::uint64_t bits = 0;
        if (! r.u32 (index) || ! r.u64 (bits))
            return false;
        double value;
        std::memcpy (&value, &bits, sizeof value);
        s.sliders.emplace_back (index, value);
    }
    if (! r.u32 (length) || ! r.bytes (s.serialized, length))
        return false;
    // (a later version may add fields after these: they are skipped)
    state = std::move (s);
    return true;
}
} // namespace lynx::jsfx
