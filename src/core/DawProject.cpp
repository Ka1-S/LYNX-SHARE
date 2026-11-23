#include "core/DawProject.h"
#include "core/TrackVersions.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

#include "core/AlsDevices.h"
#include "core/AlsImport.h"
#include "core/ClipLauncher.h"
#include "core/ApexLimiter.h"
#include "core/DynamicsSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/FlCurve.h"
#include "core/FlWrapper.h"
#include "core/GeneratorPresets.h"
#include "core/MissingFiles.h"
#include "core/NativeEngines.h"
#include "core/PluginPresets.h"
#include "core/Sections.h"
#include "core/TempoMarkers.h"
#include "core/XmlLite.h"
#include "core/ZipLite.h"

// Evidence tags as in the other importers: [XSD] Project.xsd / MetaData.xsd of DAWproject 1.0, [DOC] the reference's
// documentation (the Java classes' comments and README.md of github.com/bitwig/dawproject, MIT), [README] the example
// project in that README (Bitwig Studio 5.0), [ASSUMED] not confirmed by a file from another DAW yet.

namespace lynx
{
// ---------------------------------------------------------------------------------------------------------------------
// Shared helpers (public for the tests)
namespace dawproject
{
namespace
{
void put32le (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int k = 0; k < 4; ++k)
        out.push_back ((std::uint8_t) (v >> (8 * k)));
}
void put64le (std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int k = 0; k < 8; ++k)
        out.push_back ((std::uint8_t) (v >> (8 * k)));
}
std::uint32_t get32le (const std::uint8_t* p) noexcept { return p[0] | (p[1] << 8) | (p[2] << 16) | ((std::uint32_t) p[3] << 24); }
std::uint64_t get64le (const std::uint8_t* p) noexcept { return get32le (p) | ((std::uint64_t) get32le (p + 4) << 32); }

std::string hexBytes (const std::vector<std::uint8_t>& v)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve (v.size() * 2);
    for (const auto b : v)
    {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}
bool unhex (const std::string& s, std::vector<std::uint8_t>& out)
{
    out.clear();
    if (s.size() % 2 != 0)
        return false;
    const auto val = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (std::size_t i = 0; i < s.size(); i += 2)
    {
        const auto a = val (s[i]), b = val (s[i + 1]);
        if (a < 0 || b < 0)
            return false;
        out.push_back ((std::uint8_t) (a * 16 + b));
    }
    return true;
}
std::string floatBits (float f)
{
    std::uint32_t u;
    std::memcpy (&u, &f, 4);
    char buf[16];
    std::snprintf (buf, sizeof buf, "%08x", u);
    return buf;
}
bool bitsFloat (const std::string& s, float& f)
{
    if (s.size() != 8)
        return false;
    char* end = nullptr;
    const auto u = (std::uint32_t) std::strtoul (s.c_str(), &end, 16);
    if (end != s.c_str() + 8)
        return false;
    std::memcpy (&f, &u, 4);
    return true;
}
// a line value: '\' and line breaks escaped
std::string lineEscape (const std::string& s)
{
    std::string out;
    for (const auto c : s)
        out += c == '\\' ? std::string ("\\\\") : c == '\n' ? std::string ("\\n") : c == '\r' ? std::string ("\\r") : std::string (1, c);
    return out;
}
std::string lineUnescape (const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            const auto c = s[++i];
            out += c == 'n' ? '\n' : c == 'r' ? '\r' : c;
        }
        else
            out += s[i];
    }
    return out;
}
} // namespace

std::string vst3ClassIdText (const std::array<std::uint8_t, 16>& classId)
{
    const auto f = alsVst3Fields (classId);
    char buf[40];
    std::snprintf (buf, sizeof buf, "%08X%08X%08X%08X", (unsigned) f[0], (unsigned) f[1], (unsigned) f[2], (unsigned) f[3]);
    return buf;
}

bool vst3ClassIdFromText (const std::string& text, std::array<std::uint8_t, 16>& classId)
{
    std::string hex;
    for (const auto c : text)
        if (std::isxdigit ((unsigned char) c))
            hex += c;
    if (hex.size() != 32)
        return false;
    std::array<std::int32_t, 4> f {};
    for (int k = 0; k < 4; ++k)
        f[(std::size_t) k] = (std::int32_t) (std::uint32_t) std::strtoul (hex.substr ((std::size_t) k * 8, 8).c_str(), nullptr, 16);
    classId = alsVst3ClassId (f);
    return true;
}

std::vector<std::uint8_t> writeVstPreset (const std::array<std::uint8_t, 16>& classId, const std::vector<std::uint8_t>& component,
                                          const std::vector<std::uint8_t>& controller)
{
    // header: 'VST3', i32 version 1, char[32] class id, i64 offset of the chunk list [Steinberg's documented layout]
    std::vector<std::uint8_t> out { 'V', 'S', 'T', '3' };
    put32le (out, 1);
    const auto id = vst3ClassIdText (classId);
    out.insert (out.end(), id.begin(), id.end());
    const auto listAt = (std::uint64_t) (48 + component.size() + controller.size());
    put64le (out, listAt);
    out.insert (out.end(), component.begin(), component.end());
    out.insert (out.end(), controller.begin(), controller.end());
    out.insert (out.end(), { 'L', 'i', 's', 't' });
    put32le (out, 2);
    out.insert (out.end(), { 'C', 'o', 'm', 'p' });
    put64le (out, 48);
    put64le (out, component.size());
    out.insert (out.end(), { 'C', 'o', 'n', 't' });
    put64le (out, 48 + component.size());
    put64le (out, controller.size());
    return out;
}

bool readVstPreset (const std::vector<std::uint8_t>& f, std::array<std::uint8_t, 16>& classId, std::vector<std::uint8_t>& component,
                    std::vector<std::uint8_t>& controller)
{
    component.clear();
    controller.clear();
    if (f.size() < 48 || std::memcmp (f.data(), "VST3", 4) != 0)
        return false;
    if (! vst3ClassIdFromText (std::string ((const char*) f.data() + 8, 32), classId))
        classId = {};
    const auto listAt = get64le (f.data() + 40);
    if (listAt > f.size() || f.size() - listAt < 8 || std::memcmp (f.data() + listAt, "List", 4) != 0)
        return false;
    const auto count = get32le (f.data() + listAt + 4);
    std::size_t at = (std::size_t) listAt + 8;
    for (std::uint32_t k = 0; k < count && at + 20 <= f.size(); ++k, at += 20)
    {
        const auto offset = get64le (f.data() + at + 4), size = get64le (f.data() + at + 12);
        if (offset > f.size() || size > f.size() - offset)
            return false;
        const auto* p = f.data() + offset;
        if (std::memcmp (f.data() + at, "Comp", 4) == 0)
            component.assign (p, p + size);
        else if (std::memcmp (f.data() + at, "Cont", 4) == 0)
            controller.assign (p, p + size);
    }
    return true;
}

std::vector<std::uint8_t> flVst3State (const std::vector<std::uint8_t>& component, const std::vector<std::uint8_t>& controller)
{
    // FL's VST3 record layout (core/FlWrapper.h splitFlVst3State; the .als import writes the same): u32 version 1, then
    // {u32 id, u64 size, data} records — 1 a 64-byte head (first byte 1), 3 the component state, 2 the controller's
    std::vector<std::uint8_t> out;
    put32le (out, 1);
    const auto record = [&out] (std::uint32_t id, const std::vector<std::uint8_t>& bytes) {
        put32le (out, id);
        put64le (out, bytes.size());
        out.insert (out.end(), bytes.begin(), bytes.end());
    };
    std::vector<std::uint8_t> head (64, 0);
    head[0] = 1;
    record (1, head);
    record (3, component);
    record (2, controller);
    return out;
}

std::vector<std::uint8_t> fxFromFlVst2State (const std::vector<std::uint8_t>& state, std::uint32_t pluginId)
{
    FlVst2State st;
    if (state.empty() || ! parseFlVst2State (state, st))
        return {};
    pluginpresets::FxFile fx;
    fx.pluginId = (std::int32_t) pluginId;
    fx.currentProgram = st.currentProgram;
    if (! st.chunk.empty())
    {
        // a chunk FL applies as one program (version −6 without "all programs") → FPCh; else the bank's chunk → FBCh
        fx.kind = st.chunkIsProgram() ? pluginpresets::FxFile::Kind::chunkProgram : pluginpresets::FxFile::Kind::chunkBank;
        fx.chunk = st.chunk;
        fx.bankPrograms = std::max<std::int32_t> (1, (std::int32_t) st.programNames.size());
        fx.programName = st.programNames.empty() ? std::string() : st.programNames.front();
    }
    else if (! st.params.empty())
    {
        const auto programs = st.allPrograms() && ! st.programNames.empty() && st.params.size() % st.programNames.size() == 0 ? st.programNames.size() : 1;
        const auto per = st.params.size() / programs;
        fx.kind = programs > 1 ? pluginpresets::FxFile::Kind::paramBank : pluginpresets::FxFile::Kind::paramProgram;
        for (std::size_t p = 0; p < programs; ++p)
        {
            pluginpresets::FxProgram prog;
            prog.name = p < st.programNames.size() ? st.programNames[p] : std::string();
            prog.params.assign (st.params.begin() + (std::ptrdiff_t) (p * per), st.params.begin() + (std::ptrdiff_t) ((p + 1) * per));
            fx.programs.push_back (std::move (prog));
        }
    }
    else
        return {};
    return pluginpresets::writeFx (fx);
}

bool flVst2StateFromFx (const std::vector<std::uint8_t>& bytes, std::vector<std::uint8_t>& state, std::uint32_t& pluginId)
{
    const auto fx = pluginpresets::parseFx (bytes);
    if (! fx)
        return false;
    FlVst2State st;
    pluginId = (std::uint32_t) fx->pluginId;
    st.currentProgram = fx->currentProgram;
    using Kind = pluginpresets::FxFile::Kind;
    switch (fx->kind)
    {
        case Kind::chunkProgram:
            st.version = -6; // FL applies a −6 chunk without "all programs" as one program
            st.flags = 8;
            st.chunk = fx->chunk;
            if (! fx->programName.empty())
                st.programNames.push_back (fx->programName);
            break;
        case Kind::chunkBank:
            st.flags = 8;
            st.chunk = fx->chunk;
            break;
        case Kind::paramProgram:
        case Kind::paramBank:
            if (fx->kind == Kind::paramBank)
                st.flags = 1;
            for (const auto& p : fx->programs)
            {
                st.params.insert (st.params.end(), p.params.begin(), p.params.end());
                st.programNames.push_back (p.name);
            }
            break;
    }
    state = writeFlVst2State (st);
    return true;
}

std::string effectStateText (const EffectSlot& s)
{
    std::string out = "LYNX effect state 1\n";
    out += "kind=" + std::to_string ((int) s.kind) + "\n";
    out += "name=" + lineEscape (natives::effectMenuName (s.kind)) + "\n"; // (informative)
    out += std::string ("enabled=") + (s.enabled ? "1" : "0") + "\n";
    out += "mix=" + floatBits (s.mix) + "\n";
    out += std::string ("smartDisable=") + (s.smartDisable ? "1" : "0") + "\n";
    std::size_t n = s.params.size();
    while (n > 0 && s.params[n - 1] == 0.0f && ! std::signbit (s.params[n - 1]))
        --n;
    out += "params=";
    for (std::size_t k = 0; k < n; ++k)
        out += (k > 0 ? " " : "") + floatBits (s.params[k]);
    out += "\n";
    if (! s.state.empty())
        out += "state=" + hexBytes (s.state) + "\n";
    if (! s.samplePath.empty())
        out += "sample=" + lineEscape (s.samplePath) + "\n";
    if (! s.presetName.empty())
        out += "preset=" + lineEscape (s.presetName) + "\n";
    return out;
}

std::optional<EffectSlot> effectFromStateText (const std::string& text)
{
    if (text.rfind ("LYNX effect state 1\n", 0) != 0)
        return std::nullopt;
    std::map<std::string, std::string> v;
    std::size_t pos = text.find ('\n') + 1;
    while (pos < text.size())
    {
        auto end = text.find ('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        const auto line = text.substr (pos, end - pos);
        if (const auto eq = line.find ('='); eq != std::string::npos)
            v[line.substr (0, eq)] = line.substr (eq + 1);
        pos = end + 1;
    }
    char* endp = nullptr;
    const auto kind = std::strtol (v["kind"].c_str(), &endp, 10);
    if (endp == v["kind"].c_str() || kind < 0 || kind > (long) lastEffectKind || (EffectKind) kind == EffectKind::plugin)
        return std::nullopt;
    auto slot = makeEffectSlot ((EffectKind) kind);
    slot.enabled = v["enabled"] != "0";
    slot.smartDisable = v["smartDisable"] != "0";
    if (float f; bitsFloat (v["mix"], f))
        slot.mix = f;
    slot.params.fill (0.0f);
    std::size_t k = 0, at = 0;
    const auto& params = v["params"];
    while (at < params.size() && k < slot.params.size())
    {
        auto sp = params.find (' ', at);
        if (sp == std::string::npos)
            sp = params.size();
        float f;
        if (! bitsFloat (params.substr (at, sp - at), f))
            return std::nullopt;
        slot.params[k++] = f;
        at = sp + 1;
    }
    if (v.count ("state") && ! unhex (v["state"], slot.state))
        return std::nullopt;
    slot.samplePath = lineUnescape (v["sample"]);
    slot.presetName = lineUnescape (v["preset"]);
    return slot;
}

double gainOfVolume (double value, const std::string& unit, double max) noexcept
{
    (void) max;
    if (unit == "decibel")
        return value <= -200.0 ? 0.0 : std::pow (10.0, value / 20.0);
    if (unit == "percent")
        return value / 100.0;
    return std::max (0.0, value); // linear (Bitwig: 0 … 2 [README]); normalized read as a linear gain [ASSUMED]
}

double panOfValue (double value, const std::string& unit, double min, double max) noexcept
{
    double p = 0.0;
    if (unit == "percent")
        p = min < 0.0 ? value / 100.0 : value / 50.0 - 1.0;
    else if (unit == "normalized" || (unit.empty() && min >= 0.0))
        p = (max > min && ! (min == 0.0 && max == 1.0)) ? 2.0 * (value - min) / (max - min) - 1.0 : value * 2.0 - 1.0; // 0.5 = centre [README]
    else
        p = value; // linear −1 … 1
    return std::clamp (p, -1.0, 1.0);
}

std::uint32_t colourOf (const std::string& text) noexcept
{
    if (text.size() != 7 || text[0] != '#')
        return 0;
    std::uint32_t v = 0;
    for (std::size_t i = 1; i < 7; ++i)
    {
        const auto c = text[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return 0;
        v = v * 16 + (std::uint32_t) d;
    }
    return 0xFF000000u | v;
}

std::string colourText (std::uint32_t argb)
{
    char buf[16];
    std::snprintf (buf, sizeof buf, "#%06x", (unsigned) (argb & 0xFFFFFFu));
    return buf;
}
} // namespace dawproject

namespace
{
using xml::Element;
using namespace dawproject;

std::int64_t ticksOf (double beats) noexcept { return (std::int64_t) std::llround (beats * ppq); }

double numberOf (const std::string& s, double fallback) noexcept
{
    if (s.empty())
        return fallback;
    char* end = nullptr;
    const auto v = std::strtod (s.c_str(), &end);
    return end != s.c_str() && std::isfinite (v) ? v : fallback;
}
double num (const Element& e, std::string_view a, double fallback = 0.0) { return e ? numberOf (e.attr (a), fallback) : fallback; }
bool flag (const Element& e, std::string_view a, bool fallback)
{
    if (! e || ! e.hasAttr (a))
        return fallback;
    const auto v = e.attr (a);
    return v == "true" || v == "1";
}
bool hasWord (const std::string& list, std::string_view word)
{
    std::size_t at = 0;
    while (at < list.size())
    {
        while (at < list.size() && list[at] == ' ')
            ++at;
        auto end = list.find (' ', at);
        if (end == std::string::npos)
            end = list.size();
        if (std::string_view (list).substr (at, end - at) == word)
            return true;
        at = end;
    }
    return false;
}
std::string fileStem (const std::string& path)
{
    auto name = pathFileName (path);
    if (const auto dot = name.rfind ('.'); dot != std::string::npos && dot > 0)
        name.erase (dot);
    return name;
}
float linValue (double v, double lo, double hi) noexcept { return (float) std::clamp ((v - lo) / (hi - lo), 0.0, 1.0); }

// Writing XML: a small tree, written with two-space indents
struct Node
{
    std::string name, text;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::list<Node> kids;
    Node() = default;
    explicit Node (std::string n) : name (std::move (n)) {}
    Node& set (const std::string& k, const std::string& v)
    {
        attrs.emplace_back (k, v);
        return *this;
    }
    Node& add (const std::string& n)
    {
        kids.emplace_back (n);
        return kids.back();
    }
    void write (std::string& out, int depth) const
    {
        out.append ((std::size_t) depth * 2, ' ');
        out += '<';
        out += name;
        for (const auto& [k, v] : attrs)
            out += ' ' + k + "=\"" + xml::escape (v) + '"';
        if (kids.empty() && text.empty())
        {
            out += "/>\n";
            return;
        }
        out += '>';
        out += xml::escape (text);
        if (! kids.empty())
        {
            out += '\n';
            for (const auto& k : kids)
                k.write (out, depth + 1);
            out.append ((std::size_t) depth * 2, ' ');
        }
        out += "</" + name + ">\n";
    }
};

std::string numText (double v)
{
    if (! std::isfinite (v) || v == 0.0)
        return "0";
   #if defined (__APPLE__)
    // (rollup 0.1.30: floating-point std::to_chars needs macOS 13.3 and LYNX targets 11.0, as AlsExport.cpp's num(): the
    // shortest precision that reads back as the same double)
    for (int precision = 15;; ++precision)
    {
        std::ostringstream text;
        text.imbue (std::locale::classic());
        text << std::setprecision (precision) << v;
        std::istringstream back (text.str());
        back.imbue (std::locale::classic());
        double read = 0.0;
        back >> read;
        if (read == v || precision == 17)
            return text.str();
    }
   #else
    char buf[64];
    const auto r = std::to_chars (buf, buf + sizeof buf, v);
    return std::string (buf, r.ptr);
   #endif
}
std::string boolText (bool b) { return b ? "true" : "false"; }

// ---------------------------------------------------------------------------------------------------------------------
// Tempo map: song seconds ↔ beats under the tempo automation (hold or linear segments [DOC: RealPoint's interpolation is
// the segment starting at the point, hold by default])
struct TempoMap
{
    struct P
    {
        double beat = 0.0, bpm = 120.0;
        bool linear = false;
    };
    std::vector<P> pts; // sorted by beat; before the first point its tempo holds
    double base = 120.0;

    double secondsOfSegment (const P& a, double bpmEnd, double beats) const noexcept
    {
        if (beats <= 0.0)
            return 0.0;
        if (! a.linear || std::abs (bpmEnd - a.bpm) < 1e-9)
            return beats * 60.0 / a.bpm;
        return 60.0 * beats / (bpmEnd - a.bpm) * std::log (bpmEnd / a.bpm);
    }
    // the tempo at `beats` into a linear segment from a to b
    double secondsAt (double beat) const noexcept
    {
        if (pts.empty())
            return beat * 60.0 / base;
        if (beat <= pts.front().beat)
            return (beat - pts.front().beat) * 60.0 / pts.front().bpm + secondsBefore (0);
        double s = secondsBefore (0);
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const auto& a = pts[i];
            const auto end = i + 1 < pts.size() ? pts[i + 1].beat : 1.0e18;
            const auto bpmEnd = i + 1 < pts.size() ? pts[i + 1].bpm : a.bpm;
            if (beat <= end)
            {
                const auto x = beat - a.beat;
                const auto t = a.linear && i + 1 < pts.size() ? a.bpm + (bpmEnd - a.bpm) * x / (end - a.beat) : a.bpm;
                return s + secondsOfSegment (a, t, x);
            }
            s += secondsOfSegment (a, bpmEnd, end - a.beat);
        }
        return s;
    }
    double secondsBefore (std::size_t) const noexcept { return pts.empty() ? 0.0 : pts.front().beat * 60.0 / pts.front().bpm; }
    double beatAt (double seconds) const noexcept
    {
        if (pts.empty())
            return seconds * base / 60.0;
        double s = secondsBefore (0);
        if (seconds <= s)
            return seconds * pts.front().bpm / 60.0;
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const auto& a = pts[i];
            const auto last = i + 1 >= pts.size();
            const auto end = last ? 1.0e18 : pts[i + 1].beat;
            const auto bpmEnd = last ? a.bpm : pts[i + 1].bpm;
            const auto segment = last ? 1.0e300 : secondsOfSegment (a, bpmEnd, end - a.beat);
            if (seconds <= s + segment)
            {
                const auto ds = seconds - s;
                if (! a.linear || last || std::abs (bpmEnd - a.bpm) < 1e-9)
                    return a.beat + ds * a.bpm / 60.0;
                const auto k = (bpmEnd - a.bpm) / (end - a.beat); // bpm per beat
                const auto t = a.bpm * std::exp (ds * k / 60.0);
                return a.beat + (t - a.bpm) / k;
            }
            s += segment;
        }
        return 0.0;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// Import

// A clip's pass over its content (clip-relative start `at`, content [from, to)), loops unrolled
struct Pass
{
    double at = 0.0, from = 0.0, to = 0.0;
};
std::vector<Pass> passesOf (double duration, double playStart, bool loop, double loopStart, double loopEnd)
{
    std::vector<Pass> out;
    if (! (duration > 1e-12))
        return out;
    if (! loop || loopEnd <= loopStart + 1e-9 || playStart >= loopEnd - 1e-12)
    {
        out.push_back ({ 0.0, playStart, playStart + duration });
        return out;
    }
    double at = 0.0, from = playStart;
    while (at < duration - 1e-9 && out.size() < 4096)
    {
        const auto to = std::min (loopEnd, from + (duration - at));
        out.push_back ({ at, from, to });
        at += to - from;
        from = loopStart;
    }
    return out;
}

// Content time c in [from, to) plays at song beat c + off
struct Win
{
    double off = 0.0, from = -1.0e18, to = 1.0e18;
    double fadeIn = 0.0, fadeOut = 0.0; // beats
    bool muted = false;
    std::string name;
    std::uint32_t colour = 0;
    bool bounded() const noexcept { return from > -1.0e17 && to < 1.0e17; }
};

struct Scope
{
    std::string track;
    bool seconds = false; // the timeline's unit
    bool top = true;      // the arrangement's own timeline (seconds there follow the tempo map)
    std::vector<Win> wins { Win {} };
};

enum class Law { fader, pan, range, tempo, signature };
struct Target
{
    AutomationTarget target;
    Law law = Law::range;
    double min = 0.0, max = 1.0;
    std::string unit, name, track;
};

struct TempoPoint
{
    double beat = 0.0, bpm = 120.0;
    bool linear = false;
};

struct Imp
{
    const DawProjectImportOptions& options;
    FlpImportReport* report;
    const zip::Reader& zipr;
    Song song;
    std::map<std::string, Element> byId;
    std::string masterChannelId;
    struct ChannelInfo
    {
        Element el;
        int insert = -1;
        std::string track;
    };
    std::map<std::string, ChannelInfo> channels;
    std::vector<std::string> channelOrder;
    std::map<std::string, int> rowOfTrack, channelOfTrack, insertOfTrack;
    std::map<std::string, std::string> nameOfTrack;
    std::set<std::string> contentTracks;
    std::map<std::string, Target> targets;
    std::map<std::string, std::string> explained; // parameter id → why its automation is not kept
    std::map<std::string, int> audioChannels;
    std::map<std::size_t, int> patternOfNotes;
    std::set<std::string> missingSeen;
    std::map<std::string, int> automationOf; // target key → automation index
    std::map<int, int> automationRowOf;      // automation index → its row (−1: a row of its own)
    std::vector<TempoPoint> tempoPoints;
    bool arrangementTempo = false; // the arrangement has a TempoAutomation
    int tempoRow = -1;             // the row the tempo lane's Points were on
    std::vector<std::tuple<double, int, int>> signaturePoints;
    TempoMap tempo;
    double bpm = 120.0;
    std::string tempoParam, signatureParam;
    std::vector<Clip>* sink = nullptr; // where new clips go (the Playlist, or a clip slot's clip being read)
    std::vector<Clip>& clipsOut() { return sink != nullptr ? *sink : song.playlist; }

    Imp (const DawProjectImportOptions& o, const zip::Reader& z) : options (o), report (o.common.report), zipr (z) {}

    void note (std::map<std::string, int> FlpImportReport::* list, const std::string& what)
    {
        if (report != nullptr)
            ++(report->*list)[what];
    }

    double toBeats (double v, const Scope& s) const { return ! s.seconds ? v : s.top ? tempo.beatAt (v) : v * bpm / 60.0; }
    double spanBeats (double t, double d, const Scope& s) const
    {
        return ! s.seconds ? d : s.top ? tempo.beatAt (t + d) - tempo.beatAt (t) : d * bpm / 60.0;
    }

    void index (const Element& e)
    {
        if (e.hasAttr ("id"))
            byId[e.attr ("id")] = e;
        for (const auto& c : e.children())
            index (c);
    }

    void findContent (const Element& e, const std::string& track)
    {
        for (const auto& c : e.children())
        {
            const auto n = c.name();
            const auto t = c.hasAttr ("track") ? c.attr ("track") : track;
            if (! t.empty() && (n == "Clip" || n == "Note" || n == "Points" || n == "Warps" || n == "Audio"))
                contentTracks.insert (t);
            findContent (c, t);
        }
    }

    std::vector<std::uint8_t> embedded (const std::string& path, const std::string& what)
    {
        std::string err;
        if (auto bytes = zipr.extract (path, &err))
            return std::move (*bytes);
        note (&FlpImportReport::notReproduced, what + " (file missing in the project: " + path + ")");
        return {};
    }

    // A device's State file (always embedded [DOC: Device.state])
    std::vector<std::uint8_t> stateOf (const Element& device)
    {
        const auto st = device.child ("State");
        if (! st || st.attr ("path").empty())
            return {};
        if (flag (st, "external", false))
        {
            note (&FlpImportReport::notReproduced, "Plug-in state stored outside the project");
            return {};
        }
        return embedded (st.attr ("path"), "Plug-in state");
    }

    // ---- structure ----------------------------------------------------------------------------------------------
    int newInsert (const std::string& name, std::uint32_t colour)
    {
        if ((int) song.mixer.size() >= maxMixerTracks)
        {
            note (&FlpImportReport::notReproduced, "Mixer channels past LYNX's insert limit (played through the Master)");
            return 0;
        }
        MixerTrack m;
        m.name = name;
        m.colour = colour;
        song.mixer.push_back (std::move (m));
        return (int) song.mixer.size() - 1;
    }

    void registerChannel (const Element& ch, int insert, const std::string& track)
    {
        const auto id = ch.attr ("id");
        if (! id.empty())
        {
            channels[id] = { ch, insert, track };
            channelOrder.push_back (id);
        }
        auto& m = song.mixer[(std::size_t) insert];
        if (const auto v = ch.child ("Volume"))
        {
            const auto gain = gainOfVolume (num (v, "value", 1.0), v.attr ("unit"), num (v, "max", 2.0));
            if (gain > maxFaderGain * 1.0001)
                note (&FlpImportReport::approximated, "Channel volume above +5.6 dB (LYNX's fader top)");
            m.volume = faderPosition ((float) std::clamp (gain, 0.0, (double) maxFaderGain));
            if (v.hasAttr ("id"))
                targets[v.attr ("id")] = { { AutomationTargetKind::mixerVolume, insert, 0, 0 }, Law::fader, 0.0, num (v, "max", 2.0), v.attr ("unit"), "Volume", track };
        }
        if (const auto p = ch.child ("Pan"))
        {
            m.pan = (float) panOfValue (num (p, "value", 0.5), p.attr ("unit"), num (p, "min", 0.0), num (p, "max", 1.0));
            if (p.hasAttr ("id"))
                targets[p.attr ("id")] = { { AutomationTargetKind::mixerPan, insert, 0, 0 }, Law::pan, num (p, "min", 0.0), num (p, "max", 1.0), p.attr ("unit"), "Pan", track };
        }
        if (const auto mute = ch.child ("Mute"))
        {
            m.muted = flag (mute, "value", false);
            if (mute.hasAttr ("id"))
                explained[mute.attr ("id")] = "Mute automation (LYNX automates no mute switch)";
        }
        m.solo = flag (ch, "solo", false);
        if (ch.attr ("role") == "vca")
            note (&FlpImportReport::notReproduced, "VCA channels (kept as ordinary inserts)");
    }

    std::optional<ForeignPlugin> pluginOf (const Element& d)
    {
        const auto tag = d.name();
        ForeignPlugin p;
        p.name = d.attr ("deviceName").empty() ? d.attr ("name") : d.attr ("deviceName");
        const auto id = d.attr ("deviceID");
        if (tag == "Vst3Plugin")
        {
            p.format = ForeignPlugin::Format::vst3;
            p.hasClassId = vst3ClassIdFromText (id, p.classId);
            const auto bytes = stateOf (d);
            if (! bytes.empty())
            {
                std::array<std::uint8_t, 16> fromFile {};
                std::vector<std::uint8_t> component, controller;
                if (readVstPreset (bytes, fromFile, component, controller))
                {
                    if (! p.hasClassId && fromFile != std::array<std::uint8_t, 16> {})
                    {
                        p.classId = fromFile;
                        p.hasClassId = true;
                    }
                    p.state = flVst3State (component, controller);
                }
                else
                    note (&FlpImportReport::approximated, "VST3 state that is not a .vstpreset (the plug-in starts at its defaults): " + p.name);
            }
            return p;
        }
        if (tag == "Vst2Plugin")
        {
            p.format = ForeignPlugin::Format::vst2;
            // deviceID: the unique id as a number [ASSUMED: Bitwig writes it in decimal]; a 4-character code is read too
            if (! id.empty())
            {
                char* end = nullptr;
                const auto v = std::strtoll (id.c_str(), &end, id.rfind ("0x", 0) == 0 ? 16 : 10);
                if (end != id.c_str() && *end == '\0')
                    p.vst2Id = (std::uint32_t) v;
                else if (id.size() == 4)
                    p.vst2Id = (std::uint32_t) pluginpresets::fourCC (id.c_str());
            }
            if (const auto bytes = stateOf (d); ! bytes.empty())
            {
                std::uint32_t fromFile = 0;
                if (flVst2StateFromFx (bytes, p.state, fromFile))
                {
                    // FL's record (its current layout) has no program-chunk form: the program's chunk goes to the plug-in as
                    // its chunk, which most plug-ins read either way
                    if (bytes.size() >= 12 && std::memcmp (bytes.data() + 8, "FPCh", 4) == 0)
                        note (&FlpImportReport::approximated, "VST2 program chunk (.fxp) loaded as the plug-in's chunk: " + p.name);
                    if (p.vst2Id == 0)
                        p.vst2Id = fromFile;
                }
                else
                    note (&FlpImportReport::approximated, "VST2 state that is not an .fxp / .fxb (the plug-in starts at its defaults): " + p.name);
            }
            return p;
        }
        if (tag == "ClapPlugin")
        {
            // the state: the plug-in's own stream [ASSUMED: DAWproject's ".clap-preset" holds what clap_plugin_state saved]
            p.format = ForeignPlugin::Format::clap;
            p.clapId = id;
            p.state = stateOf (d);
            return p;
        }
        return std::nullopt;
    }

    // The automatable parameters a device lists (by parameterID) → targets
    void deviceParameters (const Element& d, bool instrument, int channel, int insert, int slot, ForeignPlugin* vst3, const std::string& track)
    {
        std::vector<std::int32_t> slots;
        for (const auto& p : d.child ("Parameters").children())
        {
            const auto id = p.attr ("id");
            if (id.empty())
                continue;
            if (! p.hasAttr ("parameterID"))
            {
                explained[id] = "Automation of a device parameter without an id";
                continue;
            }
            auto param = (int) num (p, "parameterID", -1);
            if (vst3 != nullptr)
            {
                // a VST3's ParamID → its slot, FL's VST3 automation slots (record 4) the plug-in host resolves (round 190ek)
                slots.push_back ((std::int32_t) param);
                param = (int) slots.size() - 1;
            }
            if (param < 0)
                continue;
            Target t;
            t.target = instrument ? AutomationTarget { AutomationTargetKind::channelParam, channel, 0, param }
                                  : AutomationTarget { AutomationTargetKind::effectParam, insert, slot, param };
            t.law = Law::range;
            const auto isBool = p.name() == "BoolParameter";
            t.min = isBool ? 0.0 : num (p, "min", 0.0);
            t.max = isBool ? 1.0 : num (p, "max", 1.0);
            t.unit = p.attr ("unit");
            t.name = p.attr ("name").empty() ? "Parameter " + std::to_string (param) : p.attr ("name");
            t.track = track;
            targets[id] = t;
        }
        if (vst3 != nullptr && ! slots.empty())
        {
            if (vst3->state.size() < 4)
                vst3->state = flVst3State ({}, {}); // (no state: the record that holds the slots, nothing to restore)
            auto& st = vst3->state;
            put32le (st, 4);
            put64le (st, 4 + 4 * slots.size());
            put32le (st, (std::uint32_t) slots.size());
            for (const auto s : slots)
                put32le (st, (std::uint32_t) s);
        }
        if (const auto en = d.child ("Enabled"); en && en.hasAttr ("id"))
            explained[en.attr ("id")] = "Device on / off automation";
    }

    // A RealParameter child of a built-in device, in a target unit
    struct Value
    {
        bool has = false;
        double v = 0.0;
        std::string unit;
    };
    static Value value (const Element& d, std::string_view child)
    {
        const auto e = d.child (child);
        if (! e || ! e.hasAttr ("value"))
            return {};
        return { true, num (e, "value", 0.0), e.attr ("unit") };
    }
    static double db (const Value& v, double fallback)
    {
        if (! v.has)
            return fallback;
        if (v.unit == "linear")
            return alsdev::dbOfGain (v.v);
        return v.v; // decibel
    }
    static double ms (const Value& v, double fallback)
    {
        if (! v.has)
            return fallback;
        return v.unit == "seconds" || v.unit.empty() ? v.v * 1000.0 : v.v;
    }
    static double ratio (const Value& v, double fallback)
    {
        if (! v.has)
            return fallback;
        if (v.unit == "percent") // 100 % = ∞ : 1 [ASSUMED]
            return v.v >= 99.999 ? 1.0e9 : 100.0 / (100.0 - v.v);
        return v.v;
    }

    // DAWproject's generic built-in devices → LYNX's own effects [XSD: equalizer, compressor, noiseGate, limiter]
    bool builtinEffect (const Element& d, EffectSlot& slot)
    {
        const auto tag = d.name();
        if (tag == "Compressor")
        {
            using namespace dyn::crest;
            slot = makeEffectSlot (EffectKind::crestCompressor);
            auto& p = slot.params;
            p[style] = styleValue (Style::clean);
            p[voicing] = 0.0f;
            p[threshold] = linValue (db (value (d, "Threshold"), 0.0), -60.0, 0.0);
            const auto r = ratio (value (d, "Ratio"), 4.0);
            p[Param::ratio] = r >= 1.0e6 ? 1.0f : ratioValue (std::clamp (r, 1.0, 100.0));
            p[attack] = attackValue (ms (value (d, "Attack"), 10.0));
            p[release] = releaseValue (ms (value (d, "Release"), 100.0));
            p[output] = linValue (db (value (d, "OutputGain"), 0.0), -36.0, 36.0);
            if (const auto am = d.child ("AutoMakeup"))
                p[autoGain] = flag (am, "value", false) ? 1.0f : 0.0f;
            if (std::abs (db (value (d, "InputGain"), 0.0)) > 0.01)
                note (&FlpImportReport::approximated, "Compressor input gain (not kept)");
            note (&FlpImportReport::approximated, "Compressor (as Crest Compressor, Clean style)");
            return true;
        }
        if (tag == "Limiter")
        {
            // output = min (level + input, threshold) + output gain → Apex: gain = input + output, ceiling = threshold + output
            using namespace apex;
            slot = makeEffectSlot (EffectKind::apexLimiter);
            auto& p = slot.params;
            const auto in = db (value (d, "InputGain"), 0.0), out = db (value (d, "OutputGain"), 0.0), thr = db (value (d, "Threshold"), 0.0);
            p[pGain] = linValue (in + out, 0.0, 30.0);
            p[pCeiling] = linValue (thr + out, -30.0, 0.0);
            p[pRelease] = apex::releaseValue (ms (value (d, "Release"), 100.0));
            p[pStyle] = 0.0f;
            if (in + out < -0.01 || thr + out > 0.01)
                note (&FlpImportReport::approximated, "Limiter gain below 0 dB or ceiling above 0 dB (clamped)");
            if (value (d, "Attack").has)
                note (&FlpImportReport::approximated, "Limiter attack (Apex sets its own)");
            note (&FlpImportReport::approximated, "Limiter (as Apex Limiter, Clean style)");
            return true;
        }
        if (tag == "NoiseGate")
        {
            using namespace dyn::sentry;
            slot = makeEffectSlot (EffectKind::sentryGate);
            auto& p = slot.params;
            p[style] = styleValue (Style::classic);
            p[threshold] = linValue (db (value (d, "Threshold"), -40.0), -60.0, 0.0);
            const auto r = ratio (value (d, "Ratio"), 1.0e9);
            p[Param::ratio] = r >= 1.0e6 ? 1.0f : dyn::crest::ratioValue (std::clamp (r, 1.0, 100.0)); // (the same law as Crest's)
            const auto range = std::abs (db (value (d, "Range"), -100.0));
            p[Param::range] = range >= 99.95 ? 1.0f : linValue (range, 0.0, 100.0);
            p[attack] = alsdev::sentryAttackValue (ms (value (d, "Attack"), 1.0));
            p[release] = alsdev::sentryReleaseValue (ms (value (d, "Release"), 100.0));
            note (&FlpImportReport::approximated, "Noise gate (as Sentry Gate, Classic style)");
            return true;
        }
        if (tag == "Equalizer")
        {
            slot = makeEffectSlot (EffectKind::facetEq);
            auto& p = slot.params;
            int b = 0;
            for (const auto& band : d.children ("Band"))
            {
                if (b >= facet::bands)
                {
                    note (&FlpImportReport::approximated, "Equalizer bands past Facet EQ's eight");
                    break;
                }
                const auto type = band.attr ("type");
                const auto order = (int) num (band, "order", 2);
                alsdev::Eq8Band e;
                if (type == "highPass")
                    e.mode = order >= 6 ? alsdev::Eq8Mode::lowCut48 : alsdev::Eq8Mode::lowCut12;
                else if (type == "lowPass")
                    e.mode = order >= 6 ? alsdev::Eq8Mode::highCut48 : alsdev::Eq8Mode::highCut12;
                else if (type == "lowShelf")
                    e.mode = alsdev::Eq8Mode::lowShelf;
                else if (type == "highShelf")
                    e.mode = alsdev::Eq8Mode::highShelf;
                else if (type == "notch")
                    e.mode = alsdev::Eq8Mode::notch;
                else if (type == "bell")
                    e.mode = alsdev::Eq8Mode::bell;
                else
                {
                    note (&FlpImportReport::approximated, "Equalizer band type " + type + " (left out)");
                    continue;
                }
                if ((type == "highPass" || type == "lowPass") && order != 2 && order != 8)
                    note (&FlpImportReport::approximated, "Equalizer cut slope (12 or 48 dB per octave)");
                e.hz = num (band.child ("Freq"), "value", 1000.0);
                e.gainDb = db (value (band, "Gain"), 0.0);
                e.q = std::max (0.025, num (band.child ("Q"), "value", 0.7071));
                e.on = ! band.child ("Enabled") || flag (band.child ("Enabled"), "value", true);
                alsdev::setFacetBand (p, b, e, 1.0, 0);
                ++b;
            }
            for (int k = b; k < facet::bands; ++k)
                p[(std::size_t) (facet::powerParam0 + k)] = 1.0f; // the bands not used: off
            p[facet::outputParam] = linValue (db (value (d, "OutputGain"), 0.0), -24.0, 24.0);
            if (std::abs (db (value (d, "InputGain"), 0.0)) > 0.01)
                note (&FlpImportReport::approximated, "Equalizer input gain (not kept)");
            note (&FlpImportReport::approximated, "Equalizer (as Facet EQ)");
            return true;
        }
        return false;
    }

    bool lynxDevice (const Element& d, std::string_view kind) const
    {
        return d.attr ("deviceVendor") == "LYNX" && d.attr ("deviceID").rfind (std::string ("lynx.") + std::string (kind) + ".", 0) == 0;
    }

    // The devices of a Channel: the first instrument into `instrument` (when given), audio effects onto the insert
    bool readDevices (const Element& ch, int insert, ChannelDef* instrument, int channelIndex, const std::string& track)
    {
        if (! ch)
            return false;
        bool haveInstrument = false;
        for (const auto& d : ch.child ("Devices").children())
        {
            const auto tag = std::string (d.name());
            const auto role = d.attr ("deviceRole");
            const auto name = d.attr ("deviceName").empty() ? d.attr ("name") : d.attr ("deviceName");
            const auto enabled = ! d.child ("Enabled") || flag (d.child ("Enabled"), "value", true);
            if (role == "instrument")
            {
                if (instrument == nullptr || haveInstrument)
                {
                    note (&FlpImportReport::notReproduced, "Instrument on an audio track or a second instrument: " + name);
                    continue;
                }
                haveInstrument = true;
                if (lynxDevice (d, "instrument"))
                {
                    const auto bytes = stateOf (d);
                    std::optional<ChannelDef> made;
                    if (options.nativeInstrument)
                        made = options.nativeInstrument (d.attr ("deviceID"), bytes);
                    else if (const auto preset = genpresets::fromText (std::string (bytes.begin(), bytes.end())))
                        made = genpresets::newChannel (*preset);
                    if (made)
                    {
                        const auto keepName = instrument->name;
                        const auto keepColour = instrument->colour;
                        const auto keepInsert = instrument->mixerTrack;
                        const auto keepVolume = instrument->volume;
                        *instrument = std::move (*made);
                        instrument->name = keepName;
                        instrument->colour = keepColour;
                        instrument->mixerTrack = keepInsert;
                        instrument->volume = keepVolume; // (the track's level is its insert's)
                        note (&FlpImportReport::converted, "LYNX instrument: " + name);
                    }
                    else
                        note (&FlpImportReport::unsupported, "LYNX instrument whose state could not be read: " + name);
                }
                else if (auto p = pluginOf (d))
                {
                    instrument->instrument = InstrumentKind::plugin;
                    instrument->plugin.name = p->name;
                    instrument->plugin.foreign = std::move (*p);
                    note (&FlpImportReport::converted, "Plugin: " + name);
                    deviceParameters (d, true, channelIndex, insert, 0,
                                      instrument->plugin.foreign.format == ForeignPlugin::Format::vst3 ? &instrument->plugin.foreign : nullptr, track);
                }
                else
                    note (&FlpImportReport::unsupported, "Instrument: " + name + " (LYNX's synth plays the notes)");
                if (! enabled)
                    note (&FlpImportReport::notReproduced, "Instrument switched off (it plays in LYNX)");
                continue;
            }
            if (role == "noteFX")
            {
                note (&FlpImportReport::notReproduced, "Note effect: " + name);
                continue;
            }
            if (role == "analyzer")
            {
                note (&FlpImportReport::droppedEffects, "Analyzer: " + name);
                continue;
            }
            // audio effects
            if (insert < 0)
                continue;
            auto& fx = song.mixer[(std::size_t) insert].effects;
            if ((int) fx.size() >= maxEffectSlots)
            {
                note (&FlpImportReport::droppedEffects, "Effects past the insert's 10 slots: " + name);
                continue;
            }
            EffectSlot slot;
            bool made = false, isVst3 = false;
            if (lynxDevice (d, "effect"))
            {
                const auto bytes = stateOf (d);
                std::optional<EffectSlot> s;
                if (options.nativeEffect)
                    s = options.nativeEffect (d.attr ("deviceID"), bytes);
                else
                    s = effectFromStateText (std::string (bytes.begin(), bytes.end()));
                if (s)
                {
                    slot = std::move (*s);
                    made = true;
                    note (&FlpImportReport::converted, "LYNX effect: " + name);
                }
            }
            else if (auto p = pluginOf (d))
            {
                slot.kind = EffectKind::plugin;
                slot.plugin.name = p->name;
                isVst3 = p->format == ForeignPlugin::Format::vst3;
                slot.plugin.foreign = std::move (*p);
                made = true;
                note (&FlpImportReport::converted, "Plugin: " + name);
            }
            else if (builtinEffect (d, slot))
                made = true;
            if (! made)
            {
                note (&FlpImportReport::droppedEffects, name.empty() ? tag : name);
                continue;
            }
            slot.enabled = slot.enabled && enabled;
            fx.push_back (std::move (slot));
            const auto index = (int) fx.size() - 1;
            deviceParameters (d, false, -1, insert, index, isVst3 ? &fx.back().plugin.foreign : nullptr, track);
            for (const auto& c : d.children())
                if (c.hasAttr ("id") && c.name() != "Parameters" && c.name() != "Enabled" && ! targets.count (c.attr ("id")))
                    explained[c.attr ("id")] = "Automation of a built-in device's " + std::string (c.name());
        }
        return haveInstrument;
    }

    void readTrack (const Element& t, int depth)
    {
        const auto id = t.attr ("id");
        const auto content = t.attr ("contentType");
        const auto ch = t.child ("Channel");
        const auto role = ch ? ch.attr ("role") : std::string();
        const auto subTracks = t.children ("Track");
        const auto folder = hasWord (content, "tracks") || ! subTracks.empty();
        const auto name = t.attr ("name");
        const auto colour = colourOf (t.attr ("color"));
        nameOfTrack[id] = name;
        if (role == "master")
        {
            if (! ch.attr ("id").empty())
                masterChannelId = ch.attr ("id");
            registerChannel (ch, 0, id);
            insertOfTrack[id] = 0;
            if (song.mixer[0].colour == 0)
                song.mixer[0].colour = colour;
            readDevices (ch, 0, nullptr, -1, id);
            if (contentTracks.count (id))
                note (&FlpImportReport::notReproduced, "Clips on the master track");
            for (const auto& sub : subTracks)
                readTrack (sub, depth + 1);
            return;
        }
        int insert = -1;
        if (ch)
        {
            insert = newInsert (name, colour);
            registerChannel (ch, insert, id);
            insertOfTrack[id] = insert;
        }
        const auto notes = hasWord (content, "notes");
        const auto hasContent = contentTracks.count (id) > 0;
        const auto row = folder || hasContent || ! ch || (role != "effect" && role != "vca");
        if (! row)
        {
            readDevices (ch, insert, nullptr, -1, id);
            for (const auto& sub : subTracks)
                readTrack (sub, depth + 1);
            return;
        }
        PlaylistTrack r;
        r.name = name;
        r.colour = colour;
        r.grouped = depth > 0;
        r.nest = std::max (0, depth - 1);
        if (notes)
        {
            ChannelDef c;
            c.name = name;
            c.colour = colour;
            c.volume = 1.0f; // the track's level is the insert's (the .als import's convention)
            c.mixerTrack = std::max (0, insert);
            const auto index = (int) song.channels.size();
            if (! readDevices (ch, insert, &c, index, id))
                note (&FlpImportReport::unsupported, "Notes track without an instrument (LYNX's synth plays it)");
            song.channels.push_back (std::move (c));
            channelOfTrack[id] = index;
            r.mode = PlaylistTrackMode::instrument;
            r.channel = index;
            r.mixerTrack = insert > 0 ? insert : -1;
        }
        else
        {
            readDevices (ch, insert, nullptr, -1, id);
            if (insert > 0)
            {
                r.mode = PlaylistTrackMode::audio;
                r.mixerTrack = insert;
            }
        }
        // round 190op (P4): a track that is not loaded (DAWproject's Track "loaded" = false: deactivated, its plugins not
        // loaded) comes in deactivated, its settings kept
        if (t.attr ("loaded") == "false")
        {
            if (insert > 0)
                song.mixer[(std::size_t) insert].deactivated = true;
            if (notes && r.channel >= 0 && r.channel < (int) song.channels.size())
                song.channels[(std::size_t) r.channel].deactivated = true;
        }
        song.playlistTracks.push_back (r);
        rowOfTrack[id] = (int) song.playlistTracks.size() - 1;
        for (const auto& sub : subTracks)
            readTrack (sub, depth + 1);
    }

    void routeChannels()
    {
        for (const auto& id : channelOrder)
        {
            const auto& info = channels[id];
            const auto insert = info.insert;
            if (insert <= 0)
                continue;
            auto& m = song.mixer[(std::size_t) insert];
            const auto dest = info.el.attr ("destination");
            if (! dest.empty() && dest != masterChannelId)
            {
                const auto it = channels.find (dest);
                const auto j = it != channels.end() ? it->second.insert : -1;
                if (j > 0 && j != insert && song.canRoute (insert, j))
                {
                    m.toMaster = false;
                    m.sends.insert (m.sends.begin(), MixerSend { j, 1.0f }); // the bus first (arrange::groupBus reads it there)
                }
                else
                    note (&FlpImportReport::approximated, "Channel output to an unknown or looping destination (plays to the Master)");
            }
            for (const auto& send : info.el.path ("Sends").children ("Send"))
            {
                const auto to = channels.find (send.attr ("destination"));
                const auto j = to != channels.end() ? to->second.insert : -1;
                const auto vol = send.child ("Volume");
                if (j < 0 || j == insert)
                {
                    note (&FlpImportReport::notReproduced, "Send to an unknown channel");
                    continue;
                }
                if (send.child ("Enable") && ! flag (send.child ("Enable"), "value", true))
                {
                    note (&FlpImportReport::notReproduced, "Switched-off sends (left out)");
                    continue;
                }
                if (const auto pan = send.child ("Pan"); pan && std::abs (panOfValue (num (pan, "value", 0.5), pan.attr ("unit"), num (pan, "min", 0.0), num (pan, "max", 1.0))) > 1e-4)
                    note (&FlpImportReport::approximated, "Send pan (LYNX's sends have none)");
                const auto level = gainOfVolume (num (vol, "value", 1.0), vol.attr ("unit"), num (vol, "max", 2.0));
                if (j == 0)
                {
                    note (&FlpImportReport::approximated, "Send to the master channel (left out)");
                    continue;
                }
                if (song.routes (insert, j) || ! song.canRoute (insert, j))
                {
                    note (&FlpImportReport::approximated, "Send that would duplicate a route or loop (left out)");
                    continue;
                }
                m.sends.push_back ({ j, (float) std::clamp (level, 0.0, (double) maxFaderGain), send.attr ("type") == "pre" });
                if (vol && vol.hasAttr ("id"))
                    targets[vol.attr ("id")] = { { AutomationTargetKind::mixerSend, insert, j, 0 }, Law::fader, 0.0, num (vol, "max", 2.0), vol.attr ("unit"), "Send", info.track };
            }
        }
    }

    // ---- arrangement --------------------------------------------------------------------------------------------
    int rowOf (const std::string& track) const
    {
        const auto it = rowOfTrack.find (track);
        return it != rowOfTrack.end() ? it->second : -1;
    }

    void readTimeline (const Element& e, const Scope& parent)
    {
        if (! e)
            return;
        Scope s = parent;
        if (e.hasAttr ("track"))
            s.track = e.attr ("track");
        if (e.hasAttr ("timeUnit"))
            s.seconds = e.attr ("timeUnit") == "seconds";
        const auto n = e.name();
        if (n == "Lanes")
            for (const auto& c : e.children())
                readTimeline (c, s);
        else if (n == "Clips")
            for (const auto& c : e.children ("Clip"))
                readClip (c, s);
        else if (n == "Notes")
            readNotes (e, s);
        else if (n == "Warps")
            readWarps (e, s);
        else if (n == "Audio")
            readAudio (e, s, nullptr);
        else if (n == "Points")
            readPoints (e, s);
        else if (n == "markers" || n == "Markers")
            readMarkers (e, s);
        else if (n == "ClipSlot")
            note (&FlpImportReport::notReproduced, "Clip slots on the arrangement");
        else if (n == "Video")
            note (&FlpImportReport::notReproduced, "Video");
    }

    void readClip (const Element& clip, const Scope& s)
    {
        const auto t = num (clip, "time", 0.0);
        const auto contentSeconds = clip.hasAttr ("contentTimeUnit") ? clip.attr ("contentTimeUnit") == "seconds" : s.seconds;
        const auto cb = [&] (double v) { return contentSeconds ? v * bpm / 60.0 : v; };
        const auto playStart = num (clip, "playStart", 0.0);
        double d = num (clip, "duration", -1.0);
        if (d < 0.0)
            d = clip.hasAttr ("playStop") ? num (clip, "playStop", 0.0) - playStart : 0.0; // [DOC: inferred from playStop − playStart]
        const auto tb = toBeats (t, s);
        const auto db = spanBeats (t, d, s);
        const auto loop = clip.hasAttr ("loopStart") && clip.hasAttr ("loopEnd");
        const auto passes = passesOf (db, cb (playStart), loop, cb (num (clip, "loopStart", 0.0)), cb (num (clip, "loopEnd", 0.0)));
        const auto fadeSeconds = clip.attr ("fadeTimeUnit") == "seconds";
        const auto fb = [&] (double v) { return fadeSeconds ? v * bpm / 60.0 : v; };
        const auto fadeIn = fb (num (clip, "fadeInTime", 0.0)), fadeOut = fb (num (clip, "fadeOutTime", 0.0));
        Scope inner;
        inner.track = s.track;
        inner.seconds = contentSeconds;
        inner.top = false;
        inner.wins.clear();
        for (const auto& w : s.wins)
            for (std::size_t k = 0; k < passes.size(); ++k)
            {
                const auto& p = passes[k];
                const auto dshift = tb + p.at - p.from; // content c → parent time c + dshift
                Win x;
                x.from = std::max (p.from, w.from - dshift);
                x.to = std::min (p.to, w.to - dshift);
                if (x.to <= x.from + 1e-12)
                    continue;
                x.off = dshift + w.off;
                x.fadeIn = k == 0 && std::abs (x.from - p.from) < 1e-9 ? fadeIn : std::abs (x.from - (w.from - dshift)) < 1e-9 ? w.fadeIn : 0.0;
                x.fadeOut = k + 1 == passes.size() && std::abs (x.to - p.to) < 1e-9 ? fadeOut : std::abs (x.to - (w.to - dshift)) < 1e-9 ? w.fadeOut : 0.0;
                x.muted = w.muted || ! flag (clip, "enable", true);
                x.name = clip.attr ("name").empty() ? w.name : clip.attr ("name");
                x.colour = clip.hasAttr ("color") ? colourOf (clip.attr ("color")) : w.colour;
                inner.wins.push_back (std::move (x));
            }
        if (inner.wins.empty())
            return;
        if (clip.hasAttr ("reference"))
        {
            const auto it = byId.find (clip.attr ("reference"));
            if (it != byId.end())
                readTimeline (it->second, inner);
            else
                note (&FlpImportReport::notReproduced, "Clip that refers to missing content");
            return;
        }
        for (const auto& c : clip.children())
        {
            readTimeline (c, inner);
            break; // one content timeline per clip [XSD: choice minOccurs 0]
        }
    }

    void readNotes (const Element& notes, const Scope& s)
    {
        const auto row = rowOf (s.track);
        const auto ch = channelOfTrack.find (s.track);
        if (row < 0 || ch == channelOfTrack.end())
        {
            note (&FlpImportReport::notReproduced, "Notes on a track without an instrument");
            return;
        }
        // the pattern: one per Notes element (clips that refer to the same element share it)
        int pattern = -1;
        double shift = 0.0, lo = 1.0e18, hi = -1.0e18;
        const auto list = notes.children ("Note");
        for (const auto& n : list)
        {
            const auto t = toBeats (num (n, "time", 0.0), s);
            lo = std::min (lo, t);
            hi = std::max (hi, t + spanBeats (num (n, "time", 0.0), num (n, "duration", 0.0), s));
        }
        if (list.empty())
            lo = hi = 0.0;
        for (const auto& w : s.wins)
            if (w.bounded())
                lo = std::min (lo, w.from);
        shift = lo < 0.0 ? -lo : 0.0;
        if (const auto it = patternOfNotes.find (notes.index()); it != patternOfNotes.end())
        {
            pattern = it->second;
            shift = 0.0; // (a shared pattern keeps the shift of its first use: clips of the same content start ≥ 0 in practice)
        }
        else
        {
            Pattern p;
            p.name = s.wins.front().name.empty() ? nameOfTrack[s.track] : s.wins.front().name;
            p.colour = s.wins.front().colour;
            bool midiChannels = false, expressions = false;
            for (const auto& n : list)
            {
                Note x;
                const auto t = toBeats (num (n, "time", 0.0), s);
                x.start = ticksOf (t + shift);
                x.length = std::max<std::int64_t> (1, ticksOf (spanBeats (num (n, "time", 0.0), num (n, "duration", 0.0), s)));
                x.channel = ch->second;
                x.key = std::clamp ((int) num (n, "key", 60), 0, 127);
                x.velocity = (float) std::clamp (num (n, "vel", 100.0 / 127.0), 0.0, 1.0);
                x.release = (float) std::clamp (num (n, "rel", 0.5), 0.0, 1.0);
                midiChannels = midiChannels || (int) num (n, "channel", 0) != 0;
                expressions = expressions || ! n.children().empty();
                p.notes.push_back (x);
            }
            if (midiChannels)
                note (&FlpImportReport::approximated, "MIDI channels of notes (all play on the track's instrument)");
            if (expressions)
                note (&FlpImportReport::notReproduced, "Per-note expressions");
            p.sortNotes();
            song.patterns.push_back (std::move (p));
            pattern = (int) song.patterns.size() - 1;
            patternOfNotes[notes.index()] = pattern;
        }
        for (const auto& w : s.wins)
        {
            const auto from = w.bounded() ? w.from : std::max (w.from, lo);
            const auto to = w.bounded() ? w.to : std::min (w.to, std::max (hi, lo + 1.0 / ppq));
            Clip c;
            c.kind = ClipKind::pattern;
            c.pattern = pattern;
            c.track = row;
            c.start = ticksOf (from + w.off);
            c.length = ticksOf (to - from);
            c.offset = std::max<std::int64_t> (0, ticksOf (from + shift));
            c.muted = w.muted;
            if (c.start < 0)
            {
                c.offset -= c.start;
                c.length += c.start;
                c.start = 0;
            }
            if (c.length > 0)
                clipsOut().push_back (c);
        }
    }

    // The file a FileReference names on this computer; "" when it is not here (listed in the Missing files report). Each
    // file is looked for (and an embedded one saved) once: a project's clips share their files (FL's "Thief": 1351 clips,
    // 32 files; looking per clip took the app's import 26 s, round 190jf e2e)
    std::map<std::string, std::string> resolved;
    std::set<std::pair<std::string, std::string>> missingUsers;
    std::map<std::string, double> secondsOf;
    std::string resolveFile (const Element& file, const std::string& user)
    {
        const auto path = file.attr ("path");
        if (path.empty())
            return {};
        const auto key = (flag (file, "external", false) ? "x|" : "e|") + path;
        if (const auto it = resolved.find (key); it != resolved.end())
        {
            if (it->second.empty() && report != nullptr && missingUsers.insert ({ path, user }).second && missingSeen.count (path))
                addMissingFile (report->missingFiles, path, user);
            return it->second;
        }
        auto found = locateFile (file, path, user);
        resolved[key] = found;
        return found;
    }
    std::string locateFile (const Element& file, const std::string& path, const std::string& user)
    {
        if (! flag (file, "external", false))
        {
            auto bytes = embedded (path, "Embedded audio");
            if (bytes.empty())
                return {};
            if (! options.common.saveEmbedded)
            {
                note (&FlpImportReport::notReproduced, "Embedded audio (no place to save it)");
                return {};
            }
            return options.common.saveEmbedded (bytes, pathFileName (path));
        }
        // an external file: the user's relocation, as saved (absolute) or relative to the project, then the file's name next
        // to the project (counted as relocated)
        std::vector<std::pair<std::string, bool>> candidates; // (path, relocated)
        if (const auto it = options.common.relocations.find (path); it != options.common.relocations.end())
            candidates.push_back ({ it->second, false });
        candidates.push_back ({ path, false });
        if (! options.common.projectFolder.empty())
        {
            auto folder = options.common.projectFolder;
            if (folder.back() != '/' && folder.back() != '\\')
                folder += '/';
            const auto absolute = path.front() == '/' || path.front() == '\\' || (path.size() > 1 && path[1] == ':');
            if (! absolute)
                candidates.push_back ({ folder + path, false });
            candidates.push_back ({ folder + pathFileName (path), true });
        }
        if (options.common.fileExists)
            for (const auto& [c, relocated] : candidates)
                if (options.common.fileExists (c))
                {
                    if (report != nullptr && relocated && c != path)
                    {
                        ++report->relocatedSamples;
                        report->relocatedFiles.push_back ({ path, c });
                    }
                    return c;
                }
        if (report != nullptr)
        {
            if (missingSeen.insert (path).second)
                ++report->missingSamples;
            missingUsers.insert ({ path, user });
            addMissingFile (report->missingFiles, path, user);
        }
        return {};
    }

    void readWarps (const Element& warps, const Scope& s)
    {
        const auto contentSeconds = warps.attr ("contentTimeUnit") != "beats";
        if (! contentSeconds)
            note (&FlpImportReport::approximated, "Warps whose content is in beats (read at the project tempo)");
        std::vector<std::pair<double, double>> marks; // (file seconds, content beats of the clip)
        for (const auto& w : warps.children ("Warp"))
        {
            const auto ct = num (w, "contentTime", 0.0);
            marks.push_back ({ contentSeconds ? ct : ct * 60.0 / bpm, toBeats (num (w, "time", 0.0), s) });
        }
        const auto audio = warps.child ("Audio");
        if (! audio)
        {
            note (&FlpImportReport::notReproduced, "Warped content that is not an audio file");
            return;
        }
        readAudio (audio, s, &marks);
    }

    // An audio file played through the windows. `marks` (warped): file seconds ↔ content beats.
    void readAudio (const Element& audio, const Scope& s, std::vector<std::pair<double, double>>* marks)
    {
        const auto row = rowOf (s.track);
        if (row < 0)
        {
            note (&FlpImportReport::notReproduced, "Audio on a track without a Playlist row");
            return;
        }
        const auto name = s.wins.front().name;
        // a file that is not here keeps its clips (as the .flp import keeps a channel whose sample is missing): the channel
        // has no sample until the Missing files window points it at one (the import then runs again with the relocation)
        const auto saved = audio.child ("File").attr ("path");
        const auto path = resolveFile (audio.child ("File"), "Audio clip: " + (name.empty() ? nameOfTrack[s.track] : name));
        if (saved.empty())
        {
            note (&FlpImportReport::notReproduced, "Audio without a file reference");
            return;
        }
        auto seconds = num (audio, "duration", 0.0);
        if (! (seconds > 0.0) && ! path.empty() && options.common.sampleSeconds)
        {
            const auto it = secondsOf.find (path);
            seconds = it != secondsOf.end() ? it->second : (secondsOf[path] = options.common.sampleSeconds (path));
        }
        const auto algorithm = audio.attr ("algorithm");
        std::vector<WarpPoint> warp;
        double b0 = 0.0;
        if (marks != nullptr)
        {
            std::sort (marks->begin(), marks->end());
            std::vector<std::pair<double, double>> clean;
            for (const auto& m : *marks)
                if (clean.empty() || (m.first > clean.back().first + 1e-9 && m.second > clean.back().second + 1e-9))
                    clean.push_back (m);
            if (clean.size() >= 2)
            {
                const auto slope0 = (clean[1].second - clean[0].second) / (clean[1].first - clean[0].first);
                b0 = clean[0].second - clean[0].first * slope0;
                if (clean[0].first > 1e-9)
                    warp.push_back ({ 0.0, 0.0 });
                for (const auto& [sec, beat] : clean)
                    warp.push_back ({ sec, beat - b0 });
                if (warp.front().seconds <= 1e-9)
                    warp.front() = { 0.0, 0.0 };
                const auto n = clean.size();
                const auto slopeN = (clean[n - 1].second - clean[n - 2].second) / (clean[n - 1].first - clean[n - 2].first);
                if (seconds > warp.back().seconds + 1e-6)
                    warp.push_back ({ seconds, warp.back().beats + (seconds - warp.back().seconds) * slopeN });
            }
            else if (clean.size() == 1)
            {
                b0 = clean[0].second - clean[0].first * bpm / 60.0;
                note (&FlpImportReport::approximated, "Audio with a single warp marker (plays at its own speed)");
            }
        }
        const auto insert = std::max (0, insertOfTrack.count (s.track) ? insertOfTrack[s.track] : 0);
        // the channel: one per file, insert and warp map
        std::string key = saved + "|" + std::to_string (insert) + "|" + algorithm;
        for (const auto& w : warp)
            key += "|" + std::to_string (std::llround (w.seconds * 1.0e6)) + ":" + std::to_string (std::llround (w.beats * 1.0e6));
        int channel = -1;
        if (const auto it = audioChannels.find (key); it != audioChannels.end())
            channel = it->second;
        else
        {
            ChannelDef c;
            c.name = name.empty() ? fileStem (saved) : name;
            c.instrument = InstrumentKind::audioClip;
            c.samplePath = path;
            c.volume = audioClipUnityVolume;
            c.mixerTrack = insert;
            c.sampler.declickMode = audioClipDeclickMode;
            if (warp.size() >= 2)
            {
                c.sampler.warp = warp;
                c.sampler.stretchBeats = (float) warp.back().beats;
                // [ASSUMED] Bitwig's algorithm names: "repitch" / "raw" resample, the rest keep the pitch
                const auto resample = algorithm.find ("repitch") != std::string::npos || algorithm == "raw" || algorithm == "resample";
                c.sampler.keepPitch = ! resample;
                if (algorithm.find ("beat") != std::string::npos || algorithm.find ("slice") != std::string::npos)
                    c.sampler.stretchMethod = StretchMethod::beats;
            }
            song.channels.push_back (std::move (c));
            channel = (int) song.channels.size() - 1;
            audioChannels[key] = channel;
        }
        const auto contentEnd = warp.size() >= 2 ? b0 + warp.back().beats : b0 + seconds * bpm / 60.0;
        for (const auto& w : s.wins)
        {
            auto from = w.bounded() ? w.from : std::max (w.from, b0);
            auto to = w.bounded() ? w.to : std::min (w.to, contentEnd);
            if (to <= from + 1e-9)
                continue;
            Clip c;
            c.kind = ClipKind::audio;
            c.channel = channel;
            c.track = row;
            c.start = ticksOf (from + w.off);
            c.length = ticksOf (to - from);
            auto off = (from - b0) * ppq;
            if (off < 0.0)
            {
                const auto cut = (std::int64_t) std::llround (-off);
                c.start += cut;
                c.length -= cut;
                off = 0.0;
            }
            c.offset = (std::int64_t) std::llround (off);
            c.offsetFraction = (float) (off - (double) c.offset);
            c.fadeIn = std::clamp (ticksOf (w.fadeIn), (std::int64_t) 0, std::max<std::int64_t> (0, c.length));
            c.fadeOut = std::clamp (ticksOf (w.fadeOut), (std::int64_t) 0, std::max<std::int64_t> (0, c.length));
            c.muted = w.muted;
            if (c.start < 0)
            {
                c.offset -= c.start;
                c.length += c.start;
                c.start = 0;
            }
            if (c.length > 0)
                clipsOut().push_back (c);
        }
    }

    float mapValue (const Target& t, double v, const std::string& unit) const
    {
        const auto u = unit.empty() ? t.unit : unit;
        switch (t.law)
        {
            case Law::fader: return faderPosition ((float) std::clamp (gainOfVolume (v, u, t.max), 0.0, (double) maxFaderGain));
            case Law::pan: return (float) ((panOfValue (v, u, t.min, t.max) + 1.0) * 0.5);
            case Law::tempo: return std::clamp (valueFromTempo (v), 0.0f, 1.0f);
            case Law::range:
            case Law::signature: break;
        }
        return t.max > t.min ? (float) std::clamp ((v - t.min) / (t.max - t.min), 0.0, 1.0) : (float) std::clamp (v, 0.0, 1.0);
    }

    void readPoints (const Element& points, const Scope& s)
    {
        const auto target = points.child ("Target");
        const auto param = target.attr ("parameter");
        if (param.empty())
        {
            if (target.hasAttr ("expression"))
                note (&FlpImportReport::notReproduced, "MIDI / expression automation (" + target.attr ("expression") + ")");
            else
                note (&FlpImportReport::notReproduced, "Automation without a target");
            return;
        }
        // (song beat, value, linear) through the windows
        struct Raw
        {
            double beat;
            double value;
            bool linear;
            int num, den;
        };
        std::vector<Raw> raw;
        for (const auto& p : points.children())
        {
            const auto kind = p.name();
            if (kind == "Target" || ! p.hasAttr ("time"))
                continue;
            const auto t = toBeats (num (p, "time", 0.0), s);
            double v = 0.0;
            if (kind == "BoolPoint")
                v = flag (p, "value", false) ? 1.0 : 0.0;
            else
                v = num (p, "value", 0.0);
            for (const auto& w : s.wins)
                if (! w.bounded() || (t >= w.from - 1e-9 && t <= w.to + 1e-9))
                {
                    raw.push_back ({ t + w.off, v, p.attr ("interpolation") == "linear", (int) num (p, "numerator", 4), (int) num (p, "denominator", 4) });
                    break;
                }
        }
        if (param == tempoParam)
        {
            // (LYNX writes the tempo in the arrangement's TempoAutomation and again in the lane of the Playlist row its clip
            // was on: that row gets the tempo markers' clip back; the points themselves are read once)
            if (rowOf (s.track) >= 0 && tempoRow < 0)
                tempoRow = rowOf (s.track);
            if (! arrangementTempo)
                for (const auto& r : raw)
                    tempoPoints.push_back ({ r.beat, r.value, r.linear });
            return;
        }
        if (param == signatureParam)
        {
            for (const auto& r : raw)
                signaturePoints.push_back ({ r.beat, r.num, r.den });
            return;
        }
        const auto it = targets.find (param);
        if (it == targets.end())
        {
            const auto why = explained.find (param);
            note (&FlpImportReport::notReproduced, why != explained.end() ? why->second : std::string ("Automation of a parameter LYNX has no counterpart for"));
            return;
        }
        if (raw.empty())
            return;
        const auto& t = it->second;
        const auto unit = points.attr ("unit");
        const auto row = rowOf (s.track) >= 0 ? rowOf (s.track) : rowOf (t.track);
        const auto key = std::to_string ((int) t.target.kind) + ":" + std::to_string (t.target.index) + ":" + std::to_string (t.target.slot) + ":"
                         + std::to_string (t.target.param) + "@" + std::to_string (row);
        int index = -1;
        if (const auto f = automationOf.find (key); f != automationOf.end())
            index = f->second;
        else
        {
            Automation a;
            const auto owner = nameOfTrack.count (t.track) ? nameOfTrack[t.track] : std::string();
            a.name = owner.empty() ? t.name : owner + " " + t.name;
            a.target = t.target;
            song.automations.push_back (std::move (a));
            index = (int) song.automations.size() - 1;
            automationOf[key] = index;
            automationRowOf[index] = row;
        }
        auto& a = song.automations[(std::size_t) index];
        for (const auto& r : raw)
        {
            AutomationPoint p;
            p.tick = std::max<std::int64_t> (0, ticksOf (r.beat));
            p.value = mapValue (t, r.value, unit);
            p.mode = r.linear ? (std::uint8_t) flcurve::singleCurve : (std::uint8_t) flcurve::hold;
            a.points.push_back (p);
        }
    }

    void readMarkers (const Element& markers, const Scope& s)
    {
        bool coloured = false;
        for (const auto& m : markers.children ("Marker"))
        {
            Marker x;
            x.tick = std::max<std::int64_t> (0, ticksOf (toBeats (num (m, "time", 0.0), s) + s.wins.front().off));
            x.name = m.attr ("name");
            coloured = coloured || m.hasAttr ("color");
            song.addMarker (x);
        }
        if (coloured)
            note (&FlpImportReport::notReproduced, "Marker colours");
    }

    // Scenes (the clip launcher) → LYNX's clip grid, as the .als import's Session view [XSD: Scene → a timeline whose
    // ClipSlots (track, hasStop) hold one Clip each]: one grid scene per Scene, a slot's clip on its track's row. A clip
    // with a loop plays its loop region with the slot's Loop on (the Start marker where playStart falls inside it).
    void readScenes (const Element& scenesElement)
    {
        const auto scenes = scenesElement.children ("Scene");
        bool any = false;
        std::function<void (const Element&, const std::string&, int)> walk = [&] (const Element& e, const std::string& track, int scene) {
            for (const auto& c : e.children())
            {
                const auto t = c.hasAttr ("track") ? c.attr ("track") : track;
                if (c.name() != "ClipSlot")
                {
                    walk (c, t, scene);
                    continue;
                }
                const auto row = rowOf (t);
                const auto clip = c.child ("Clip");
                if (row < 0)
                {
                    if (clip)
                        note (&FlpImportReport::notReproduced, "Clip slots of a track without a Playlist row");
                    continue;
                }
                if (! clip)
                {
                    if (! flag (c, "hasStop", true))
                    {
                        launcher::ensureGrid (song);
                        launcher::setStopButton (song, row, scene, false);
                    }
                    continue;
                }
                std::vector<Clip> made;
                sink = &made;
                Scope s;
                s.track = t;
                readClip (clip, s);
                sink = nullptr;
                if (made.empty())
                    continue;
                auto first = made.front();
                const auto loop = clip.hasAttr ("loopStart") && clip.hasAttr ("loopEnd");
                std::int64_t startMarker = 0;
                if (loop)
                {
                    // the loop region as the clip, in the content's ticks (the first pass's offset is where playStart fell)
                    const auto seconds = clip.hasAttr ("contentTimeUnit") ? clip.attr ("contentTimeUnit") == "seconds" : false;
                    const auto cb = [&] (double v) { return seconds ? v * bpm / 60.0 : v; };
                    const auto ls = ticksOf (cb (num (clip, "loopStart", 0.0))), le = ticksOf (cb (num (clip, "loopEnd", 0.0)));
                    if (le > ls)
                    {
                        startMarker = std::clamp<std::int64_t> (first.offset - ls, 0, le - ls - 1);
                        first.offset = std::max<std::int64_t> (0, first.offset - startMarker);
                        first.length = le - ls;
                    }
                }
                first.start = 0;
                const auto id = launcher::setClip (song, row, scene, first);
                if (id < 0)
                {
                    note (&FlpImportReport::notReproduced, "Clip slots past LYNX's grid limits");
                    continue;
                }
                auto& slot = song.clipGrid->slots[(std::size_t) id];
                slot.name = clip.attr ("name");
                slot.colour = colourOf (clip.attr ("color"));
                slot.loop = loop;
                slot.startMarker = startMarker;
                any = true;
            }
        };
        for (std::size_t k = 0; k < scenes.size(); ++k)
        {
            if ((int) k >= launcher::maxScenes)
            {
                note (&FlpImportReport::notReproduced, "Scenes past LYNX's grid limit");
                break;
            }
            walk (scenes[k], {}, (int) k);
        }
        if (! song.clipGrid)
            return;
        if (! any)
        {
            song.clipGrid.reset(); // only stop buttons: no grid, as the .als import does
            return;
        }
        auto& grid = launcher::ensureGrid (song);
        if (grid.scenes.size() < scenes.size())
            grid.scenes.resize (std::min<std::size_t> (scenes.size(), (std::size_t) launcher::maxScenes));
        for (std::size_t k = 0; k < scenes.size() && k < grid.scenes.size(); ++k)
        {
            grid.scenes[k].name = scenes[k].attr ("name");
            grid.scenes[k].colour = colourOf (scenes[k].attr ("color"));
        }
    }

    void finishAutomation()
    {
        for (std::size_t i = 0; i < song.automations.size(); ++i)
        {
            auto& a = song.automations[i];
            std::stable_sort (a.points.begin(), a.points.end(), [] (const AutomationPoint& x, const AutomationPoint& y) { return x.tick < y.tick; });
            const auto row = automationRowOf.count ((int) i) ? automationRowOf[(int) i] : -1;
            if (row >= 0)
            {
                song.playlistTracks[(std::size_t) row].lanes.push_back ({ (int) i, 0 });
                continue;
            }
            // no Playlist row of its own (the master, an effect return): a clip on a row of its own, as the .als import does
            PlaylistTrack r;
            r.name = a.name;
            song.playlistTracks.push_back (r);
            const auto first = a.points.empty() ? 0 : a.points.front().tick;
            for (auto& p : a.points)
                p.tick -= first;
            Clip c;
            c.kind = ClipKind::automation;
            c.automation = (int) i;
            c.track = (int) song.playlistTracks.size() - 1;
            c.start = first;
            c.length = std::max<std::int64_t> (ppq, a.points.empty() ? 0 : a.points.back().tick);
            song.playlist.push_back (c);
        }
    }

    void finishTempo()
    {
        std::stable_sort (tempoPoints.begin(), tempoPoints.end(), [] (const TempoPoint& a, const TempoPoint& b) { return a.beat < b.beat; });
        if (tempoPoints.empty())
            return;
        if (tempoPoints.size() == 1 && tempoPoints.front().beat <= 1e-9)
        {
            song.bpm = std::clamp (tempoPoints.front().bpm, minTempo, maxTempo);
            return;
        }
        bool clamped = false;
        for (std::size_t i = 0; i < tempoPoints.size(); ++i)
        {
            const auto& p = tempoPoints[i];
            clamped = clamped || p.bpm < minTempo || p.bpm > maxTempo;
            const auto glide = i > 0 && tempoPoints[i - 1].linear;
            tempomarkers::add (song, std::max<std::int64_t> (0, ticksOf (p.beat)), p.bpm, glide);
        }
        // the markers' clip: on the row the tempo came from, else on a row of its own at the end (never inside a folder,
        // where tempomarkers::ensure's first free row could be)
        if (auto* clip = tempomarkers::clipOf (song, tempomarkers::automationIndex (song)))
        {
            if (tempoRow < 0)
            {
                PlaylistTrack r;
                r.name = "Tempo";
                song.playlistTracks.push_back (r);
                tempoRow = (int) song.playlistTracks.size() - 1;
            }
            clip->track = tempoRow;
        }
        if (clamped)
            note (&FlpImportReport::approximated, "Tempo outside LYNX's 10 … 522 BPM (clamped)");
    }

    void finishSignatures()
    {
        for (const auto& [beat, n, d] : signaturePoints)
        {
            if (beat <= 1e-9 && meterUnit (d) == d && n >= 1 && n <= 32)
            {
                song.beatsPerBar = n;
                song.beatUnit = d;
                continue;
            }
            Marker m;
            m.tick = std::max<std::int64_t> (0, ticksOf (beat));
            m.numerator = std::clamp (n, 1, 32);
            m.denominator = meterUnit (d);
            m.action = Marker::timeSignature;
            song.addMarker (m);
        }
    }

    void readMetadata (const std::vector<std::uint8_t>& bytes)
    {
        xml::Document doc;
        if (bytes.empty() || ! doc.parse (std::string (bytes.begin(), bytes.end())) || doc.root().name() != "MetaData")
            return;
        const auto root = doc.root();
        auto& info = song.info;
        info.title = root.child ("Title").text();
        info.author = root.child ("Artist").text();
        info.genre = root.child ("Genre").text();
        info.url = root.child ("Website").text();
        info.comments = root.child ("Comment").text();
        for (const auto* other : { "Album", "OriginalArtist", "Composer", "Songwriter", "Producer", "Arranger", "Year", "Copyright" })
            if (! root.child (other).text().empty())
                note (&FlpImportReport::notReproduced, std::string ("Project info: ") + other);
    }
};

bool parseProject (const std::vector<std::uint8_t>& bytes, zip::Reader& z, xml::Document& doc, std::vector<std::uint8_t>& metadata, std::string* error)
{
    std::string err;
    if (! z.open (bytes.data(), bytes.size(), &err))
    {
        if (error != nullptr)
            *error = "not a DAWproject file: " + err;
        return false;
    }
    auto project = z.extract ("project.xml", &err, 512u << 20);
    if (! project)
    {
        if (error != nullptr)
            *error = "not a DAWproject file: " + err;
        return false;
    }
    if (! doc.parse (std::string (project->begin(), project->end()), &err))
    {
        if (error != nullptr)
            *error = "project.xml is not valid XML: " + err;
        return false;
    }
    if (doc.root().name() != "Project")
    {
        if (error != nullptr)
            *error = "project.xml is not a DAWproject project";
        return false;
    }
    if (auto m = z.extract ("metadata.xml", nullptr, 16u << 20))
        metadata = std::move (*m);
    return true;
}

void collectPlugins (const Element& e, std::vector<FlpPluginUse>& out)
{
    for (const auto& c : e.children())
    {
        const auto n = c.name();
        if (n == "Vst2Plugin" || n == "Vst3Plugin" || n == "ClapPlugin")
        {
            ForeignPlugin p;
            p.name = c.attr ("deviceName");
            const auto id = c.attr ("deviceID");
            if (n == "Vst3Plugin")
            {
                p.format = ForeignPlugin::Format::vst3;
                p.hasClassId = vst3ClassIdFromText (id, p.classId);
            }
            else if (n == "Vst2Plugin")
            {
                p.format = ForeignPlugin::Format::vst2;
                p.vst2Id = (std::uint32_t) std::strtoll (id.c_str(), nullptr, 10);
            }
            else
            {
                p.format = ForeignPlugin::Format::clap;
                p.clapId = id;
            }
            out.push_back ({ std::move (p), c.attr ("deviceRole") == "instrument" });
            continue;
        }
        collectPlugins (c, out);
    }
}
} // namespace

bool isDawProjectData (const std::uint8_t* data, std::size_t size)
{
    if (! zip::isZip (data, size))
        return false;
    zip::Reader z;
    if (! z.open (data, size) || z.find ("project.xml") < 0)
        return false;
    const auto project = z.extract ("project.xml", nullptr, 512u << 20);
    if (! project)
        return false;
    const std::string head (project->begin(), project->begin() + (std::ptrdiff_t) std::min<std::size_t> (project->size(), 4096));
    return head.find ("<Project") != std::string::npos;
}

std::vector<FlpPluginUse> dawProjectForeignPlugins (const std::vector<std::uint8_t>& bytes)
{
    std::vector<FlpPluginUse> out;
    zip::Reader z;
    xml::Document doc;
    std::vector<std::uint8_t> metadata;
    if (parseProject (bytes, z, doc, metadata, nullptr))
        collectPlugins (doc.root(), out);
    return out;
}

std::optional<Song> importDawProject (const std::vector<std::uint8_t>& bytes, const DawProjectImportOptions& options, std::string* error)
{
    zip::Reader z;
    xml::Document doc;
    std::vector<std::uint8_t> metadata;
    if (! parseProject (bytes, z, doc, metadata, error))
        return std::nullopt;
    const auto root = doc.root();
    if (const auto v = root.attr ("version"); ! v.empty() && v.rfind ("1.", 0) != 0)
        if (options.common.report != nullptr)
            ++options.common.report->approximated["DAWproject version " + v + " (read as 1.0)"];

    Imp imp (options, z);
    auto& song = imp.song;
    song.channels.clear();
    song.patterns.clear();
    song.playlist.clear();
    song.playlistTracks.clear();
    song.mixer = Song::defaultMixer (0);
    song.mode = PlayMode::song;
    song.arrangements = { Arrangement { "Arrangement", {}, 0, 0, {} } };
    imp.index (root);

    // transport
    const auto transport = root.child ("Transport");
    if (const auto t = transport.child ("Tempo"))
    {
        song.bpm = std::clamp (num (t, "value", 120.0), minTempo, maxTempo);
        imp.tempoParam = t.attr ("id");
    }
    imp.bpm = song.bpm;
    imp.tempo.base = song.bpm;
    if (const auto ts = transport.child ("TimeSignature"))
    {
        imp.signatureParam = ts.attr ("id");
        const auto n = (int) num (ts, "numerator", 4), d = (int) num (ts, "denominator", 4);
        if (meterUnit (d) == d && n >= 1 && n <= 32)
        {
            song.beatsPerBar = n;
            song.beatUnit = d;
        }
        else
            imp.signaturePoints.push_back ({ 0.0, n, d });
    }
    const auto arrangement = root.child ("Arrangement");
    // the tempo automation first: seconds on the arrangement follow it
    if (const auto ta = arrangement.child ("TempoAutomation"))
    {
        imp.arrangementTempo = true;
        const auto seconds = ta.attr ("timeUnit") == "seconds";
        std::vector<std::pair<double, std::pair<double, bool>>> raw;
        for (const auto& p : ta.children ("RealPoint"))
            raw.push_back ({ num (p, "time", 0.0), { num (p, "value", song.bpm), p.attr ("interpolation") == "linear" } });
        std::stable_sort (raw.begin(), raw.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        for (const auto& [t, v] : raw)
        {
            double beat = t;
            if (seconds)
                beat = imp.tempo.beatAt (t); // (the map so far: the points before this one)
            if (seconds && ! imp.tempo.pts.empty() && imp.tempo.pts.back().linear)
            {
                // a ramp into this point: solve the segment's length in beats from its length in seconds
                const auto& a = imp.tempo.pts.back();
                const auto s0 = imp.tempo.secondsAt (a.beat);
                beat = std::abs (v.first - a.bpm) < 1e-9 ? a.beat + (t - s0) * a.bpm / 60.0
                                                          : a.beat + (t - s0) * (v.first - a.bpm) / (60.0 * std::log (v.first / a.bpm));
            }
            imp.tempo.pts.push_back ({ beat, std::max (1.0, v.first), v.second });
            imp.tempoPoints.push_back ({ beat, v.first, v.second });
        }
    }
    if (const auto tsa = arrangement.child ("TimeSignatureAutomation"))
        for (const auto& p : tsa.children ("TimeSignaturePoint"))
        {
            Scope s;
            s.seconds = tsa.attr ("timeUnit") == "seconds";
            imp.signaturePoints.push_back ({ imp.toBeats (num (p, "time", 0.0), s), (int) num (p, "numerator", 4), (int) num (p, "denominator", 4) });
        }

    // structure
    imp.findContent (arrangement, {});
    for (const auto& c : root.child ("Structure").children())
    {
        if (c.name() == "Track")
            imp.readTrack (c, 0);
        else if (c.name() == "Channel")
        {
            // a bare channel (an effect return, a bus) [XSD: Structure holds Tracks and Channels]
            if (c.attr ("role") == "master")
            {
                imp.masterChannelId = c.attr ("id");
                imp.registerChannel (c, 0, {});
                imp.readDevices (c, 0, nullptr, -1, {});
            }
            else
            {
                const auto insert = imp.newInsert (c.attr ("name"), colourOf (c.attr ("color")));
                imp.registerChannel (c, insert, {});
                imp.readDevices (c, insert, nullptr, -1, {});
            }
        }
    }
    // the master's id when its track came after the channels that name it
    imp.routeChannels();

    // arrangement
    if (const auto lanes = arrangement.child ("Lanes"))
    {
        Scope s;
        imp.readTimeline (lanes, s);
    }
    if (const auto markers = arrangement.child ("Markers"))
    {
        Scope s;
        s.seconds = markers.attr ("timeUnit") == "seconds";
        imp.readMarkers (markers, s);
    }
    imp.finishSignatures();
    imp.finishAutomation();
    imp.finishTempo();

    imp.readScenes (root.child ("Scenes"));
    imp.readMetadata (metadata);

    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    if (song.patterns.empty())
    {
        Pattern first;
        first.name = "Pattern 1";
        song.patterns.push_back (std::move (first));
    }
    song.currentPattern = 0;
    return std::move (imp.song);
}

// ---------------------------------------------------------------------------------------------------------------------
// Export

namespace
{
struct TNode
{
    // lane: a row with only automation clips or a named empty row (a Track without a Channel, its Points in its lane)
    enum class Kind { notes, audio, folder, bus, lane } kind = Kind::audio;
    int row = -1, channel = -1, insert = -1;
    bool owns = false;
    std::string name, role = "regular";
    std::uint32_t colour = 0;
    std::string trackId, channelId;
    std::list<TNode> kids;
};

struct Exp
{
    const Song& song;
    const DawProjectExportOptions& opt;
    DawProjectExportResult& res;
    int ids = 0;
    std::vector<zip::NewEntry> files;
    std::set<std::string> usedNames, noted;
    std::map<std::string, std::string> embeddedPath; // sample path → its entry
    std::map<int, std::string> volId, panId;
    std::map<std::pair<int, int>, std::string> sendId;
    std::map<std::tuple<int, int, int>, std::string> fxParamId;
    std::map<std::pair<int, int>, std::string> chParamId;
    std::map<std::string, std::string> trackOfParam;
    std::map<int, std::string> trackOfInsert;
    std::set<std::tuple<int, int, int>> automatedFx;
    std::set<std::pair<int, int>> automatedCh;
    std::string tempoId, signatureId, masterTrackId, masterChannelId;
    double bpm = 120.0;

    Exp (const Song& s, const DawProjectExportOptions& o, DawProjectExportResult& r) : song (s), opt (o), res (r), bpm (s.bpm) {}

    std::string newId() { return "id" + std::to_string (ids++); }
    void note (const std::string& what)
    {
        if (noted.insert (what).second)
            res.notes.push_back (what);
    }
    std::string uniqueName (const std::string& folder, std::string name)
    {
        for (auto& c : name)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        if (name.empty())
            name = "file";
        auto candidate = folder + name;
        for (int k = 2; usedNames.count (candidate); ++k)
        {
            const auto dot = name.rfind ('.');
            candidate = folder + (dot == std::string::npos ? name + "-" + std::to_string (k) : name.substr (0, dot) + "-" + std::to_string (k) + name.substr (dot));
        }
        usedNames.insert (candidate);
        return candidate;
    }
    std::string addFile (const std::string& folder, const std::string& name, std::vector<std::uint8_t> bytes)
    {
        const auto path = uniqueName (folder, name);
        files.push_back ({ path, std::move (bytes) });
        return path;
    }

    // ---- devices ------------------------------------------------------------------------------------------------
    Node& realParam (Node& parent, const std::string& tag, const std::string& id, const std::string& name, const std::string& unit, double min, double max,
                     double value)
    {
        auto& p = parent.add (tag);
        p.set ("id", id).set ("name", name).set ("unit", unit).set ("min", numText (min)).set ("max", numText (max)).set ("value", numText (value));
        return p;
    }

    void stateFile (Node& device, const std::string& name, std::vector<std::uint8_t> bytes)
    {
        if (bytes.empty())
            return;
        device.add ("State").set ("path", addFile ("plugins/", name, std::move (bytes)));
    }

    // A hosted plug-in. `params` (LYNX automation index → the Parameters entry's id), filled for the automated ones
    bool pluginDevice (Node& devices, const PluginRef& ref, bool instrument, bool enabled, const std::vector<int>& automated,
                       std::map<int, std::string>& paramIds)
    {
        ForeignPlugin f = ref.foreign;
        if (f.format == ForeignPlugin::Format::none && opt.pluginRecord)
            if (auto record = opt.pluginRecord (ref, instrument))
            {
                f = std::move (*record);
                if (! ref.name.empty())
                    f.name = ref.name;
            }
        const auto name = ref.name.empty() ? f.name : ref.name;
        std::string tag, deviceId, ext;
        std::vector<std::uint8_t> state;
        std::vector<std::int32_t> vst3Slots;
        switch (f.format)
        {
            case ForeignPlugin::Format::vst3:
            {
                tag = "Vst3Plugin";
                deviceId = vst3ClassIdText (f.classId);
                ext = ".vstpreset";
                std::vector<std::uint8_t> component, controller;
                if (! f.state.empty() && ! splitFlVst3State (f.state, component, controller))
                    note ("Plug-in state not carried over: " + name);
                if (! component.empty() || ! controller.empty())
                    state = writeVstPreset (f.classId, component, controller);
                vst3Slots = flVst3ParamSlots (f.state);
                break;
            }
            case ForeignPlugin::Format::vst2:
                tag = "Vst2Plugin";
                deviceId = std::to_string (f.vst2Id);
                ext = ".fxb";
                state = fxFromFlVst2State (f.state, f.vst2Id);
                if (! state.empty() && state.size() >= 12 && (std::memcmp (state.data() + 8, "FxCk", 4) == 0 || std::memcmp (state.data() + 8, "FPCh", 4) == 0))
                    ext = ".fxp";
                if (! f.state.empty() && state.empty())
                    note ("Plug-in state not carried over: " + name);
                break;
            case ForeignPlugin::Format::clap:
                tag = "ClapPlugin";
                deviceId = f.clapId;
                ext = ".clap-preset";
                state = f.state; // the plug-in's own stream [ASSUMED]
                break;
            default:
                note ("Plug-in that is not a VST2, VST3 or CLAP (left out): " + name);
                return false;
        }
        auto& d = devices.add (tag);
        const auto id = newId();
        d.set ("id", id).set ("name", name).set ("deviceName", name).set ("deviceRole", instrument ? "instrument" : "audioFX").set ("deviceID", deviceId);
        d.set ("loaded", "true");
        if (! automated.empty())
        {
            auto& params = d.add ("Parameters");
            for (const auto k : automated)
            {
                int paramId = k;
                if (f.format == ForeignPlugin::Format::vst3)
                {
                    if (k < 0 || k >= (int) vst3Slots.size() || vst3Slots[(std::size_t) k] < 0)
                    {
                        note ("VST3 parameter automation without the parameter's id (left out): " + name);
                        continue;
                    }
                    paramId = vst3Slots[(std::size_t) k];
                }
                const auto pid = newId();
                auto& p = params.add ("RealParameter");
                p.set ("id", pid).set ("parameterID", std::to_string (paramId)).set ("unit", "normalized").set ("min", "0").set ("max", "1");
                paramIds[k] = pid;
            }
        }
        d.add ("Enabled").set ("id", newId()).set ("name", "On/Off").set ("value", boolText (enabled));
        stateFile (d, name + ext, std::move (state));
        return true;
    }

    void nativeInstrument (Node& devices, const ChannelDef& c, int channelIndex)
    {
        const auto kindId = genpresets::kindId (c.instrument);
        auto& d = devices.add ("Device");
        d.set ("id", newId()).set ("name", c.name).set ("deviceName", "LYNX " + genpresets::engineName (c.instrument)).set ("deviceRole", "instrument");
        d.set ("deviceVendor", "LYNX").set ("deviceID", "lynx.instrument." + kindId).set ("loaded", "true");
        std::vector<int> automated;
        for (const auto& [ch, p] : automatedCh)
            if (ch == channelIndex)
                automated.push_back (p);
        if (! automated.empty())
        {
            auto& params = d.add ("Parameters");
            for (const auto k : automated)
            {
                const auto pid = newId();
                params.add ("RealParameter").set ("id", pid).set ("parameterID", std::to_string (k)).set ("unit", "normalized").set ("min", "0").set ("max", "1");
                chParamId[{ channelIndex, k }] = pid;
            }
        }
        d.add ("Enabled").set ("id", newId()).set ("name", "On/Off").set ("value", "true");
        std::vector<std::uint8_t> state;
        if (opt.nativeInstrumentState)
            state = opt.nativeInstrumentState (c);
        else
        {
            const auto text = genpresets::toText (c.name, c);
            state.assign (text.begin(), text.end());
            if (c.instrument == InstrumentKind::sampler || c.instrument == InstrumentKind::patcher || c.instrument == InstrumentKind::layer)
                note ("LYNX instrument settings beyond its sound (sampler page, Patcher graph, Layer children) not in the core state: " + c.name);
        }
        stateFile (d, (c.name.empty() ? kindId : c.name) + ".lynxpreset", std::move (state));
    }

    void instrumentDevice (Node& devices, const ChannelDef& c, int channelIndex, const std::string& trackId)
    {
        if (c.instrument == InstrumentKind::plugin)
        {
            std::vector<int> automated;
            for (const auto& [ch, p] : automatedCh)
                if (ch == channelIndex)
                    automated.push_back (p);
            std::map<int, std::string> ids;
            if (pluginDevice (devices, c.plugin, true, true, automated, ids))
                for (const auto& [k, id] : ids)
                {
                    chParamId[{ channelIndex, k }] = id;
                    trackOfParam[id] = trackId;
                }
            return;
        }
        nativeInstrument (devices, c, channelIndex);
        for (const auto& [key, id] : chParamId)
            if (key.first == channelIndex)
                trackOfParam[id] = trackId;
        if (c.arp.direction != 0)
            note ("Channel arpeggiators (not written)");
    }

    void effectDevices (Node& devices, int insert, const std::string& trackId)
    {
        const auto& m = song.mixer[(std::size_t) insert];
        for (std::size_t s = 0; s < m.effects.size(); ++s)
        {
            const auto& slot = m.effects[s];
            std::vector<int> automated;
            for (const auto& [i, k, p] : automatedFx)
                if (i == insert && k == (int) s)
                    automated.push_back (p);
            std::map<int, std::string> ids;
            if (slot.kind == EffectKind::plugin)
            {
                if (! pluginDevice (devices, slot.plugin, false, slot.enabled, automated, ids))
                    continue;
                if (slot.mix != 1.0f)
                    note ("Dry / wet of hosted plug-in slots (not written)");
            }
            else
            {
                auto& d = devices.add ("Device");
                const auto name = natives::effectMenuName (slot.kind);
                d.set ("id", newId()).set ("name", name).set ("deviceName", "LYNX " + name).set ("deviceRole", "audioFX");
                d.set ("deviceVendor", "LYNX").set ("deviceID", "lynx.effect." + std::to_string ((int) slot.kind)).set ("loaded", "true");
                if (! automated.empty())
                {
                    auto& params = d.add ("Parameters");
                    for (const auto k : automated)
                    {
                        const auto pid = newId();
                        params.add ("RealParameter").set ("id", pid).set ("parameterID", std::to_string (k)).set ("unit", "normalized").set ("min", "0").set ("max", "1").set (
                            "value", numText (k >= 0 && k < maxEffectParams ? slot.params[(std::size_t) k] : 0.0f));
                        ids[k] = pid;
                    }
                }
                d.add ("Enabled").set ("id", newId()).set ("name", "On/Off").set ("value", boolText (slot.enabled));
                std::vector<std::uint8_t> state;
                if (opt.nativeEffectState)
                    state = opt.nativeEffectState (slot);
                else
                {
                    const auto text = effectStateText (slot);
                    state.assign (text.begin(), text.end());
                    if (! slot.branches.empty() || ! slot.patcher.nodes.empty())
                        note ("LYNX effects' Patcher graphs and parallel branches not in the core state: " + name);
                    if (slot.sidechain >= 0 || slot.sidechain2 >= 0)
                        note ("Side-chain inputs of LYNX effects (not written)");
                }
                stateFile (d, name + ".lynxfx", std::move (state));
            }
            for (const auto& [k, id] : ids)
            {
                fxParamId[{ insert, (int) s, k }] = id;
                trackOfParam[id] = trackId;
            }
        }
    }

    // ---- channels -----------------------------------------------------------------------------------------------
    // The Channel of a track. `insert` ≥ 0 and `owns`: the insert's own settings; else a plain strip into the insert.
    void channel (Node& track, TNode& t, const std::map<int, std::string>& channelIdOfInsert)
    {
        auto& ch = track.add ("Channel");
        ch.set ("id", t.channelId).set ("role", t.role).set ("audioChannels", "2");
        const auto* c = t.kind == TNode::Kind::notes && t.channel >= 0 ? &song.channels[(std::size_t) t.channel] : nullptr;
        const MixerTrack* m = t.owns && t.insert >= 0 && t.insert < (int) song.mixer.size() ? &song.mixer[(std::size_t) t.insert] : nullptr;
        // destination
        std::string dest;
        if (m != nullptr && t.insert > 0)
        {
            if (! m->toMaster && ! m->sends.empty())
            {
                if (const auto it = channelIdOfInsert.find (m->sends.front().target); it != channelIdOfInsert.end())
                    dest = it->second;
            }
            else if (m->toMaster)
                dest = masterChannelId;
            else
                note ("Insert with no output (written without a destination): " + m->name);
            if (m->masterLevel != 1.0f && m->toMaster)
                note ("Insert-to-Master route levels (not written)");
        }
        else if (m == nullptr)
        {
            if (t.insert > 0)
            {
                if (const auto it = channelIdOfInsert.find (t.insert); it != channelIdOfInsert.end())
                    dest = it->second;
            }
            else
                dest = masterChannelId;
        }
        if (! dest.empty())
            ch.set ("destination", dest);
        ch.set ("solo", boolText (m != nullptr && m->solo));
        // devices
        auto& devices = ch.add ("Devices");
        if (c != nullptr)
            instrumentDevice (devices, *c, t.channel, t.trackId);
        if (m != nullptr)
            effectDevices (devices, t.insert, t.trackId);
        if (devices.kids.empty())
            ch.kids.pop_back();
        // levels: an owned insert's (with a channel's own level folded in), else the channel's
        double gain = 1.0, pan = 0.0;
        bool muted = false;
        if (m != nullptr)
        {
            gain = faderGain (m->volume);
            pan = m->pan;
            muted = m->muted;
        }
        if (c != nullptr)
        {
            gain *= channelGain (c->volume);
            if (c->pan != 0.0f)
            {
                if (m != nullptr && m->pan != 0.0f)
                    note ("Channel pan on a panned insert (the insert's pan written)");
                else
                    pan = c->pan;
            }
            muted = muted || c->muted;
        }
        const auto muteId = newId();
        ch.add ("Mute").set ("id", muteId).set ("name", "Mute").set ("value", boolText (muted));
        const auto pid = newId();
        realParam (ch, "Pan", pid, "Pan", "normalized", 0.0, 1.0, (pan + 1.0) * 0.5);
        if (m != nullptr)
        {
            panId[t.insert] = pid;
            trackOfParam[pid] = t.trackId;
            Node sends ("Sends");
            for (std::size_t k = 0; k < m->sends.size(); ++k)
            {
                const auto& s = m->sends[k];
                if (k == 0 && ! m->toMaster)
                    continue; // the destination
                const auto it = channelIdOfInsert.find (s.target);
                if (it == channelIdOfInsert.end())
                    continue;
                auto& send = sends.add ("Send");
                send.set ("id", newId()).set ("destination", it->second).set ("type", s.pre ? "pre" : "post");
                const auto vid = newId();
                realParam (send, "Volume", vid, "Send", "linear", 0.0, 2.0, s.level);
                sendId[{ t.insert, s.target }] = vid;
                trackOfParam[vid] = t.trackId;
            }
            if (! sends.kids.empty())
                ch.kids.push_back (std::move (sends));
            if (! m->lostSends.empty())
                note ("Sends to deleted inserts (not written)");
        }
        const auto vid = newId();
        realParam (ch, "Volume", vid, "Volume", "linear", 0.0, 2.0, gain);
        if (m != nullptr)
        {
            volId[t.insert] = vid;
            trackOfParam[vid] = t.trackId;
        }
    }
};

int depthOf (const Song& song, int row)
{
    if (row < 0 || row >= (int) song.playlistTracks.size())
        return 0;
    const auto& t = song.playlistTracks[(std::size_t) row];
    return t.grouped ? 1 + std::max (0, t.nest) : 0;
}
} // namespace

DawProjectExportResult exportDawProject (const Song& input, const DawProjectExportOptions& opt)
{
    DawProjectExportResult res;
    // track versions (round 190pe, a LYNX extra): DAWproject has none; each track goes over as the version it plays
    std::optional<Song> stripped;
    if (trackversions::any (input))
    {
        stripped = input;
        const auto n = trackversions::keepActiveOnly (*stripped);
        res.notes.push_back ("Track versions: " + std::to_string (n) + " version(s) that are not active left out (each track is written as the version it plays)");
    }
    const auto& song = stripped ? *stripped : input;
    Exp x (song, opt, res);
    const auto& clips = song.playlist;

    // ---- what is automated (devices list their automated parameters)
    for (const auto& a : song.automations)
    {
        if (a.target.kind == AutomationTargetKind::effectParam)
            x.automatedFx.insert ({ a.target.index, a.target.slot, a.target.param });
        else if (a.target.kind == AutomationTargetKind::channelParam)
            x.automatedCh.insert ({ a.target.index, a.target.param });
    }

    // ---- the rows: content, depth, channels with notes
    int rows = (int) song.playlistTracks.size();
    for (const auto& c : clips)
        rows = std::max (rows, c.track + 1);
    std::vector<bool> hasAudio ((std::size_t) rows, false), hasLanes ((std::size_t) rows, false), hasAutomation ((std::size_t) rows, false);
    std::vector<bool> channelNotes (song.channels.size(), false);
    for (const auto& c : clips)
    {
        if (c.kind == ClipKind::audio && c.track >= 0)
            hasAudio[(std::size_t) c.track] = true;
        if (c.kind == ClipKind::automation && c.track >= 0)
            hasAutomation[(std::size_t) c.track] = true;
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
            for (const auto& n : song.patterns[(std::size_t) c.pattern].notes)
                if (n.channel >= 0 && n.channel < (int) song.channels.size())
                    channelNotes[(std::size_t) n.channel] = true;
    }
    for (int r = 0; r < (int) song.playlistTracks.size(); ++r)
        hasLanes[(std::size_t) r] = ! song.playlistTracks[(std::size_t) r].lanes.empty();
    std::vector<int> instrumentRow (song.channels.size(), -1);
    for (int r = 0; r < (int) song.playlistTracks.size(); ++r)
    {
        const auto& t = song.playlistTracks[(std::size_t) r];
        if (t.mode == PlaylistTrackMode::instrument && t.channel >= 0 && t.channel < (int) song.channels.size() && instrumentRow[(std::size_t) t.channel] < 0)
            instrumentRow[(std::size_t) t.channel] = r;
    }
    // a channel without an instrument row whose pattern clips all sit on one row (FL's usual layout: a row per channel)
    // takes that row, unless the row is an instrument row or another channel took it
    std::vector<int> channelOfRow ((std::size_t) rows, -1);
    for (int r = 0; r < (int) song.playlistTracks.size(); ++r)
        if (const auto& t = song.playlistTracks[(std::size_t) r]; t.mode == PlaylistTrackMode::instrument && t.channel >= 0
                                                                  && t.channel < (int) song.channels.size() && instrumentRow[(std::size_t) t.channel] == r)
            channelOfRow[(std::size_t) r] = t.channel;
    for (std::size_t ch = 0; ch < song.channels.size(); ++ch)
    {
        if (! channelNotes[ch] || instrumentRow[ch] >= 0)
            continue;
        int only = -1;
        bool one = true;
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.track >= 0 && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
            {
                const auto& notes = song.patterns[(std::size_t) c.pattern].notes;
                if (std::none_of (notes.begin(), notes.end(), [ch] (const Note& n) { return n.channel == (int) ch; }))
                    continue;
                one = one && (only < 0 || only == c.track);
                only = c.track;
            }
        if (one && only >= 0 && channelOfRow[(std::size_t) only] < 0)
        {
            channelOfRow[(std::size_t) only] = (int) ch;
            instrumentRow[ch] = only;
        }
    }

    // ---- the track tree (rows nest by folder depth)
    std::list<TNode> roots;
    {
        std::vector<std::pair<int, std::list<TNode>*>> stack { { -1, &roots } };
        int previous = -1;
        for (int r = 0; r < rows; ++r)
        {
            auto depth = std::min (depthOf (song, r), previous + 1);
            previous = depth;
            while (stack.size() > 1 && stack.back().first >= depth)
                stack.pop_back();
            TNode n;
            n.row = r;
            const auto* t = r < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) r] : nullptr;
            n.name = t != nullptr && ! t->name.empty() ? t->name : "Track " + std::to_string (r + 1);
            n.colour = t != nullptr ? t->colour : 0;
            if (channelOfRow[(std::size_t) r] >= 0)
            {
                n.kind = TNode::Kind::notes;
                n.channel = channelOfRow[(std::size_t) r];
                if (t == nullptr || t->name.empty())
                    n.name = song.channels[(std::size_t) n.channel].name;
                if (n.colour == 0)
                    n.colour = song.channels[(std::size_t) n.channel].colour;
            }
            else if (! hasAudio[(std::size_t) r])
                n.kind = TNode::Kind::lane;
            auto& list = *stack.back().second;
            list.push_back (std::move (n));
            stack.push_back ({ depth, &list.back().kids });
        }
    }
    // prune: a row stays when it holds something, or a row below it in its folder does
    std::function<bool (std::list<TNode>&)> prune = [&] (std::list<TNode>& list) {
        bool any = false;
        for (auto it = list.begin(); it != list.end();)
        {
            const auto kids = prune (it->kids);
            const auto r = it->row;
            const auto named = r < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) r].name.empty();
            const auto own = it->kind == TNode::Kind::notes || hasAudio[(std::size_t) r] || hasLanes[(std::size_t) r] || hasAutomation[(std::size_t) r] || named;
            if (! own && ! kids)
            {
                it = list.erase (it);
                continue;
            }
            if (! it->kids.empty())
                it->kind = it->kind == TNode::Kind::notes ? TNode::Kind::notes : TNode::Kind::folder;
            any = true;
            ++it;
        }
        return any;
    };
    prune (roots);
    // channels with notes and no instrument row: tracks of their own
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        if (channelNotes[c] && instrumentRow[c] < 0 && song.channels[c].instrument != InstrumentKind::audioClip)
        {
            TNode n;
            n.kind = TNode::Kind::notes;
            n.channel = (int) c;
            n.name = song.channels[c].name;
            n.colour = song.channels[c].colour;
            roots.push_back (std::move (n));
        }

    // ---- inserts: each node's strip
    std::map<int, int> users;
    std::function<void (std::list<TNode>&)> assign = [&] (std::list<TNode>& list) {
        for (auto& n : list)
        {
            const auto* t = n.row >= 0 && n.row < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) n.row] : nullptr;
            if (n.kind == TNode::Kind::notes)
                n.insert = song.channels[(std::size_t) n.channel].mixerTrack;
            else if (t != nullptr && t->mode == PlaylistTrackMode::audio && t->mixerTrack > 0)
                n.insert = t->mixerTrack;
            else if (n.kind == TNode::Kind::audio)
            {
                n.insert = 0;
                for (const auto& c : clips)
                    if (c.track == n.row && c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
                    {
                        n.insert = song.channels[(std::size_t) c.channel].mixerTrack;
                        break;
                    }
            }
            if (n.insert >= (int) song.mixer.size())
                n.insert = song.mixer.empty() ? -1 : 0;
            if (n.insert > 0)
                ++users[n.insert];
            assign (n.kids);
        }
    };
    assign (roots);
    std::function<void (std::list<TNode>&)> owners = [&] (std::list<TNode>& list) {
        for (auto& n : list)
        {
            n.owns = n.insert > 0 && users[n.insert] == 1;
            if (n.kind == TNode::Kind::folder && n.owns)
                n.role = "submix";
            owners (n.kids);
        }
    };
    owners (roots);
    // inserts every included insert plays into, and shared inserts: bus tracks of their own
    std::set<int> owned;
    std::function<void (const std::list<TNode>&)> collectOwned = [&] (const std::list<TNode>& list) {
        for (const auto& n : list)
        {
            if (n.owns)
                owned.insert (n.insert);
            collectOwned (n.kids);
        }
    };
    collectOwned (roots);
    std::set<int> needed;
    for (const auto& [i, count] : users)
        if (count > 1)
            needed.insert (i);
    for (bool changed = true; changed;)
    {
        changed = false;
        std::set<int> all = owned;
        all.insert (needed.begin(), needed.end());
        for (const auto i : all)
            for (const auto& s : song.mixer[(std::size_t) i].sends)
                if (s.target > 0 && s.target < (int) song.mixer.size() && ! owned.count (s.target) && needed.insert (s.target).second)
                    changed = true;
    }
    std::set<int> busTargets;
    for (std::size_t i = 1; i < song.mixer.size(); ++i)
        if (! song.mixer[i].toMaster && ! song.mixer[i].sends.empty())
            busTargets.insert (song.mixer[i].sends.front().target);
    for (const auto i : needed)
    {
        TNode n;
        n.kind = TNode::Kind::bus;
        n.insert = i;
        n.owns = true;
        n.role = busTargets.count (i) ? "submix" : "effect";
        n.name = song.mixer[(std::size_t) i].name.empty() ? "Insert " + std::to_string (i) : song.mixer[(std::size_t) i].name;
        n.colour = song.mixer[(std::size_t) i].colour;
        roots.push_back (std::move (n));
    }

    // ---- ids
    x.masterTrackId = x.newId();
    x.masterChannelId = x.newId();
    std::map<int, std::string> channelIdOfInsert { { 0, x.masterChannelId } };
    std::function<void (std::list<TNode>&)> giveIds = [&] (std::list<TNode>& list) {
        for (auto& n : list)
        {
            n.trackId = x.newId();
            const auto hasChannel = (n.kind != TNode::Kind::folder && n.kind != TNode::Kind::lane) || n.insert > 0;
            if (hasChannel)
                n.channelId = x.newId();
            if (n.owns)
            {
                channelIdOfInsert[n.insert] = n.channelId;
                x.trackOfInsert[n.insert] = n.trackId;
            }
            giveIds (n.kids);
        }
    };
    giveIds (roots);

    // ---- project.xml
    Node project ("Project");
    project.set ("version", "1.0");
    project.add ("Application").set ("name", opt.application).set ("version", opt.version);
    auto& transport = project.add ("Transport");
    x.tempoId = x.newId();
    x.realParam (transport, "Tempo", x.tempoId, "Tempo", "bpm", minTempo, maxTempo, song.bpm);
    x.signatureId = x.newId();
    transport.add ("TimeSignature").set ("id", x.signatureId).set ("numerator", std::to_string (song.beatsPerBar)).set ("denominator", std::to_string (song.beatUnit));
    auto& structure = project.add ("Structure");
    std::map<int, std::string> trackIdOfRow, trackIdOfChannel;
    std::function<void (Node&, std::list<TNode>&)> writeTracks = [&] (Node& parent, std::list<TNode>& list) {
        for (auto& n : list)
        {
            auto& t = parent.add ("Track");
            std::string content;
            if (n.kind == TNode::Kind::folder)
                content = "tracks";
            if (n.kind == TNode::Kind::notes)
                content = n.kids.empty() ? "notes" : "tracks notes";
            if (n.row >= 0 && hasAudio[(std::size_t) n.row])
                content += content.empty() ? "audio" : " audio";
            if (n.kind == TNode::Kind::lane)
                content = "automation";
            if (content.empty())
                content = "audio";
            // round 190op (P4): a deactivated unit is a track that is not loaded (DAWproject's "loaded"; plugins unloaded,
            // their states still written)
            const auto off = (n.insert > 0 && n.insert < (int) song.mixer.size() && n.owns && song.mixer[(std::size_t) n.insert].deactivated)
                             || (n.kind == TNode::Kind::notes && n.channel >= 0 && n.channel < (int) song.channels.size()
                                 && song.channels[(std::size_t) n.channel].deactivated);
            t.set ("id", n.trackId).set ("name", n.name).set ("contentType", content).set ("loaded", off ? "false" : "true");
            if (n.colour != 0)
                t.set ("color", colourText (n.colour));
            if (! n.channelId.empty())
                x.channel (t, n, channelIdOfInsert);
            if (n.row >= 0)
            {
                trackIdOfRow[n.row] = n.trackId;
                if (song.playlistTracks.size() > (std::size_t) n.row && song.playlistTracks[(std::size_t) n.row].muted)
                    x.note ("Muted Playlist tracks (their clips are written switched off)");
            }
            if (n.kind == TNode::Kind::notes)
                trackIdOfChannel[n.channel] = n.trackId;
            writeTracks (t, n.kids);
        }
    };
    writeTracks (structure, roots);
    // the master
    {
        auto& t = structure.add ("Track");
        t.set ("id", x.masterTrackId).set ("name", song.mixer.empty() || song.mixer[0].name.empty() ? std::string ("Master") : song.mixer[0].name);
        t.set ("contentType", "audio notes").set ("loaded", "true");
        TNode m;
        m.kind = TNode::Kind::bus;
        m.insert = song.mixer.empty() ? -1 : 0;
        m.owns = ! song.mixer.empty();
        m.role = "master";
        m.trackId = x.masterTrackId;
        m.channelId = x.masterChannelId;
        x.trackOfInsert[0] = x.masterTrackId;
        x.channel (t, m, channelIdOfInsert);
    }

    // ---- arrangement
    auto& arrangement = project.add ("Arrangement");
    arrangement.set ("id", x.newId());
    auto& lanes = arrangement.add ("Lanes");
    lanes.set ("timeUnit", "beats").set ("id", x.newId());
    std::map<std::string, Node*> laneOfTrack;
    const auto laneFor = [&] (const std::string& trackId) -> Node& {
        if (const auto it = laneOfTrack.find (trackId); it != laneOfTrack.end())
            return *it->second;
        auto& l = lanes.add ("Lanes");
        l.set ("track", trackId).set ("id", x.newId());
        laneOfTrack[trackId] = &l;
        return l;
    };
    std::map<std::string, Node*> clipsOfTrack;
    const auto clipsFor = [&] (const std::string& trackId) -> Node& {
        if (const auto it = clipsOfTrack.find (trackId); it != clipsOfTrack.end())
            return *it->second;
        auto& c = laneFor (trackId).add ("Clips");
        c.set ("id", x.newId());
        clipsOfTrack[trackId] = &c;
        return c;
    };
    std::vector<const Clip*> ordered;
    for (const auto& c : clips)
        ordered.push_back (&c);
    std::stable_sort (ordered.begin(), ordered.end(), [] (const Clip* a, const Clip* b) { return a->start < b->start; });
    const auto rowMuted = [&] (int r) { return r >= 0 && r < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) r].muted; };
    // notes: each channel's notes, pattern clip by pattern clip
    std::map<std::pair<int, int>, std::string> notesId;
    bool mutedNotes = false, noteExtras = false;
    for (const auto& [channel, trackId] : trackIdOfChannel)
        for (const auto* c : ordered)
        {
            if (c->kind != ClipKind::pattern || c->pattern < 0 || c->pattern >= (int) song.patterns.size())
                continue;
            const auto& p = song.patterns[(std::size_t) c->pattern];
            if (std::none_of (p.notes.begin(), p.notes.end(), [ch = channel] (const Note& n) { return n.channel == ch && ! n.muted; }))
                continue;
            auto& clip = clipsFor (trackId).add ("Clip");
            clip.set ("time", numText ((double) c->start / ppq)).set ("duration", numText ((double) c->length / ppq));
            clip.set ("playStart", numText ((double) c->offset / ppq));
            if (! p.name.empty())
                clip.set ("name", p.name);
            if (p.colour != 0)
                clip.set ("color", colourText (p.colour));
            if (c->muted || rowMuted (c->track))
                clip.set ("enable", "false");
            const auto key = std::make_pair (c->pattern, channel);
            if (const auto it = notesId.find (key); it != notesId.end())
            {
                clip.set ("reference", it->second);
                continue;
            }
            const auto nid = x.newId();
            notesId[key] = nid;
            auto& notes = clip.add ("Notes");
            notes.set ("id", nid);
            for (const auto& n : p.notes)
            {
                if (n.channel != channel)
                    continue;
                if (n.muted)
                {
                    mutedNotes = true;
                    continue;
                }
                noteExtras = noteExtras || n.pan != 0.0f || n.finePitch != 0.0f || n.slide || n.porta || n.hasChance() || n.modX != 0.5f || n.modY != 0.5f;
                notes.add ("Note")
                    .set ("time", numText ((double) n.start / ppq))
                    .set ("duration", numText ((double) n.length / ppq))
                    .set ("channel", "0")
                    .set ("key", std::to_string (n.key))
                    .set ("vel", numText (n.velocity))
                    .set ("rel", numText (n.release));
            }
            if (c->offset + c->length > p.effectiveLength (song.meter()) && stretchedClipsLoop())
                x.note ("Pattern clips longer than their pattern (written without the loop)");
        }
    if (mutedNotes)
        x.note ("Muted notes (left out)");
    {
        std::set<int> placed;
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern)
                placed.insert (c.pattern);
        for (std::size_t p = 0; p < song.patterns.size(); ++p)
            if (! song.patterns[p].notes.empty() && ! placed.count ((int) p))
            {
                x.note ("Patterns not placed on the Playlist (not written)");
                break;
            }
    }
    if (noteExtras)
        x.note ("Note pan, fine pitch, slide, porta, Mod X / Y and chance (not written)");
    // audio clips
    bool clipExtras = false;
    for (const auto* c : ordered)
    {
        if (c->kind != ClipKind::audio || c->channel < 0 || c->channel >= (int) song.channels.size())
            continue;
        const auto it = trackIdOfRow.find (c->track);
        if (it == trackIdOfRow.end())
            continue;
        const auto& ch = song.channels[(std::size_t) c->channel];
        // a channel whose sample is not on this computer (an .flp opened without its samples keeps no path): the clip is
        // written with a reference by the channel's name, so it comes back as a missing file the user can locate
        const auto missing = ch.samplePath.empty();
        if (missing)
            x.note ("Audio clips whose file is missing here (referenced by their channel's name)");
        const auto samplePath = missing ? (ch.name.empty() ? std::string ("Audio") : ch.name) + ".wav" : ch.samplePath;
        double seconds = 0.0;
        int rate = 44100, channels = 2;
        if (missing || ! opt.sampleFormat || ! opt.sampleFormat (ch.samplePath, seconds, rate, channels))
        {
            seconds = 0.0;
            x.note ("Audio files whose length was not known (written as 0 s)");
        }
        // the file: embedded, else referenced where it is
        Node audio ("Audio");
        audio.set ("id", x.newId()).set ("channels", std::to_string (channels)).set ("sampleRate", std::to_string (rate)).set ("duration", numText (seconds));
        const auto& s = ch.sampler;
        const auto warped = s.warp.size() >= 2 || (s.stretchBeats > 0.0f && seconds > 0.0) || (c->stretch != 1.0 && seconds > 0.0);
        audio.set ("algorithm", warped && s.keepPitch ? "stretch" : "repitch");
        auto& file = audio.add ("File");
        if (const auto e = x.embeddedPath.find (samplePath); e != x.embeddedPath.end())
            file.set ("path", e->second);
        else
        {
            auto bytes = opt.readFile && ! missing ? opt.readFile (ch.samplePath) : std::vector<std::uint8_t> {};
            if (! bytes.empty())
            {
                const auto path = x.addFile ("audio/", pathFileName (ch.samplePath), std::move (bytes));
                x.embeddedPath[ch.samplePath] = path;
                file.set ("path", path);
            }
            else
                file.set ("path", samplePath).set ("external", "true");
        }
        auto& clip = clipsFor (it->second).add ("Clip");
        clip.set ("time", numText ((double) c->start / ppq)).set ("duration", numText ((double) c->length / ppq));
        const auto offsetBeats = ((double) c->offset + (double) c->offsetFraction) / ppq;
        if (! ch.name.empty())
            clip.set ("name", ch.name);
        if (c->muted || rowMuted (c->track))
            clip.set ("enable", "false");
        clipExtras = clipExtras || c->gain != 1.0f || c->pan != 0.0f || c->pitch != 0.0f || c->reversed || c->envelope >= 0 || s.pitch != 0.0f || s.reverse;
        if (warped)
        {
            clip.set ("contentTimeUnit", "beats").set ("playStart", numText (offsetBeats));
            clip.set ("fadeTimeUnit", "beats").set ("fadeInTime", numText ((double) c->fadeIn / ppq)).set ("fadeOutTime", numText ((double) c->fadeOut / ppq));
            auto& w = clip.add ("Warps");
            w.set ("id", x.newId()).set ("timeUnit", "beats").set ("contentTimeUnit", "seconds");
            w.kids.push_back (std::move (audio));
            std::vector<WarpPoint> marks = s.warp;
            if (marks.size() < 2)
            {
                const auto beats = s.stretchBeats > 0.0f ? (double) s.stretchBeats : seconds * song.bpm / 60.0 * c->stretch;
                marks = { { 0.0, 0.0 }, { seconds, beats } };
                if (s.stretchBeats <= 0.0f)
                    x.note ("Per-clip stretch written as warp markers");
            }
            else if (c->stretch != 1.0)
                x.note ("Per-clip stretch on warped audio (not written)");
            for (const auto& m : marks)
                w.add ("Warp").set ("time", numText (m.beats)).set ("contentTime", numText (m.seconds));
        }
        else
        {
            // plays the file at its own speed: the content in seconds [DOC: contentTimeUnit]
            clip.set ("contentTimeUnit", "seconds").set ("playStart", numText (offsetBeats * 60.0 / song.bpm));
            clip.set ("fadeTimeUnit", "beats").set ("fadeInTime", numText ((double) c->fadeIn / ppq)).set ("fadeOutTime", numText ((double) c->fadeOut / ppq));
            clip.kids.push_back (std::move (audio));
            if (s.stretchBeats > 0.0f || c->stretch != 1.0)
                x.note ("Stretched audio whose file length was not known (written unstretched)");
        }
    }
    if (clipExtras)
        x.note ("Audio clip gain, pan, pitch, reverse and clip envelopes (not written)");

    // automation: each target's points in song time, from automation clips and lanes
    struct Pt
    {
        double beat;
        float value;
        bool hold;
    };
    // (by target and the Playlist row the clip or lane is on: an exported row keeps its automation in its own lane)
    std::map<std::tuple<int, int, int, int, int>, std::vector<Pt>> byTarget;
    std::map<std::tuple<int, int, int, int, int>, int> sources;
    bool curved = false;
    // (a clip that cuts its automation's start or end: the value there; the tempo holds after its clip anyway)
    const auto take = [&] (const Automation& a, int row, std::int64_t origin, std::int64_t from, std::int64_t to) {
        if (! trackIdOfRow.count (row))
            row = -1;
        const auto key = std::make_tuple ((int) a.target.kind, a.target.index, a.target.slot, a.target.param, row);
        ++sources[key];
        auto& list = byTarget[key];
        if (a.points.empty())
            return;
        const auto add = [&] (std::int64_t tick, float v, std::uint8_t mode) {
            list.push_back ({ (double) tick / ppq, v, mode == flcurve::hold });
            curved = curved || (mode != flcurve::hold && mode != flcurve::singleCurve);
        };
        const auto edges = a.target.kind != AutomationTargetKind::tempo;
        if (edges && a.points.front().tick + origin < from && from > std::numeric_limits<std::int64_t>::min())
            add (from, a.valueAt ((double) (from - origin)), a.points.front().mode);
        for (const auto& p : a.points)
        {
            const auto t = p.tick + origin;
            if (t < from || t > to)
                continue;
            curved = curved || p.tension != 0.0f;
            add (t, p.value, p.mode);
        }
        if (edges && a.points.back().tick + origin > to && to < std::numeric_limits<std::int64_t>::max())
            add (to, a.valueAt ((double) (to - origin)), flcurve::hold);
    };
    for (const auto* c : ordered)
        if (c->kind == ClipKind::automation && c->automation >= 0 && c->automation < (int) song.automations.size())
        {
            const auto& a = song.automations[(std::size_t) c->automation];
            if (a.isLive() || ! a.links.empty())
            {
                x.note ("Live controller links and multi-target automation clips (not written)");
                continue;
            }
            if (a.lfo.on)
                x.note ("Automation clip LFOs (not written)");
            take (a, c->track, c->start - c->offset, c->start, c->end());
        }
    for (int r = 0; r < (int) song.playlistTracks.size(); ++r)
        for (const auto& lane : song.playlistTracks[(std::size_t) r].lanes)
            if (lane.automation >= 0 && lane.automation < (int) song.automations.size())
                take (song.automations[(std::size_t) lane.automation], r, 0, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());
    if (curved)
        x.note ("Curved automation segments (written as straight lines)");
    std::vector<Pt> tempoPts;
    for (auto& [key, pts] : byTarget)
    {
        std::stable_sort (pts.begin(), pts.end(), [] (const Pt& a, const Pt& b) { return a.beat < b.beat; });
        if (sources[key] > 1)
            x.note ("Several automations of one target (merged)");
        const auto [kind, index, slot, param, row] = key;
        std::string paramId, unit = "normalized";
        std::function<double (float)> conv = [] (float v) { return (double) v; };
        switch ((AutomationTargetKind) kind)
        {
            case AutomationTargetKind::tempo:
                tempoPts.insert (tempoPts.end(), pts.begin(), pts.end());
                if (row < 0)
                    continue;
                paramId = x.tempoId; // (and in its row's lane: the import puts the tempo markers' clip back on that row)
                unit = "bpm";
                conv = [] (float v) { return tempoFromValue (v); };
                break;
            case AutomationTargetKind::mixerVolume:
                paramId = x.volId.count (index) ? x.volId[index] : std::string();
                unit = "linear";
                conv = [] (float v) { return (double) faderGain (v); };
                break;
            case AutomationTargetKind::mixerPan: paramId = x.panId.count (index) ? x.panId[index] : std::string(); break;
            case AutomationTargetKind::mixerSend:
                paramId = x.sendId.count ({ index, slot }) ? x.sendId[{ index, slot }] : std::string();
                unit = "linear";
                conv = [] (float v) { return (double) faderGain (v); };
                break;
            case AutomationTargetKind::effectParam: paramId = x.fxParamId.count ({ index, slot, param }) ? x.fxParamId[{ index, slot, param }] : std::string(); break;
            case AutomationTargetKind::channelParam: paramId = x.chParamId.count ({ index, param }) ? x.chParamId[{ index, param }] : std::string(); break;
            default: break;
        }
        if (paramId.empty())
        {
            x.note ("Automation of a target DAWproject has no parameter for (channel volume / pan, effect mix, VCA, a level adjustment, a route to the Master)");
            continue;
        }
        auto& points = laneFor (row >= 0 ? trackIdOfRow[row] : x.trackOfParam[paramId]).add ("Points");
        points.set ("id", x.newId()).set ("unit", unit);
        points.add ("Target").set ("parameter", paramId);
        for (const auto& p : pts)
            points.add ("RealPoint").set ("time", numText (p.beat)).set ("value", numText (conv (p.value))).set ("interpolation", p.hold ? "hold" : "linear");
    }
    // markers, tempo and signature changes
    std::vector<const Marker*> plain, signatures;
    // (round 190ol: DAWproject has no sections: a marker at each section's start, named after it)
    const auto withSections = sections::markersWithSections (song.markers(), song.sections());
    for (const auto& m : withSections)
        (m.isSignature() ? signatures : plain).push_back (&m);
    if (! song.sections().empty())
        x.note ("Song sections written as markers at their starts");
    if (! plain.empty())
    {
        auto& markers = arrangement.add ("Markers");
        markers.set ("id", x.newId());
        for (const auto* m : plain)
        {
            markers.add ("Marker").set ("time", numText ((double) m->tick / ppq)).set ("name", m->name);
            if (m->action != Marker::none)
                x.note ("Marker actions (loops, skips, punch…) written as plain markers");
        }
    }
    if (! tempoPts.empty())
    {
        std::stable_sort (tempoPts.begin(), tempoPts.end(), [] (const Pt& a, const Pt& b) { return a.beat < b.beat; });
        auto& ta = arrangement.add ("TempoAutomation");
        ta.set ("id", x.newId()).set ("unit", "bpm");
        ta.add ("Target").set ("parameter", x.tempoId);
        for (const auto& p : tempoPts)
            ta.add ("RealPoint").set ("time", numText (p.beat)).set ("value", numText (tempoFromValue (p.value))).set ("interpolation", p.hold ? "hold" : "linear");
    }
    if (! signatures.empty())
    {
        auto& tsa = arrangement.add ("TimeSignatureAutomation");
        tsa.set ("id", x.newId());
        tsa.add ("Target").set ("parameter", x.signatureId);
        for (const auto* m : signatures)
            tsa.add ("TimeSignaturePoint")
                .set ("time", numText ((double) m->tick / ppq))
                .set ("numerator", std::to_string (m->numerator))
                .set ("denominator", std::to_string (m->denominator));
    }
    project.add ("Scenes");

    // what is not written
    if (song.arrangements.size() > 1)
        x.note ("Arrangements other than the current one (not written)");
    if (song.clipGrid)
        x.note ("The clip grid (scenes are not written yet)");
    if (song.hasLoop())
        x.note ("The song's loop range (not written)");
    if (! song.vcas.empty())
        x.note ("VCA faders (not written)");
    if (song.patternBlocksOn && ! song.blocks.empty())
        x.note ("Pattern Blocks (not written)");
    for (std::size_t i = 1; i < song.mixer.size(); ++i)
        if (! song.mixer[i].effects.empty() && ! owned.count ((int) i) && ! needed.count ((int) i))
        {
            x.note ("Inserts no track plays through (left out)");
            break;
        }
    {
        // (round 190jf, parent: a channel that plays nothing on the Playlist and has no instrument track is not written)
        std::set<int> playing;
        for (const auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
                for (const auto& n : song.patterns[(std::size_t) c.pattern].notes)
                    playing.insert (n.channel);
        for (const auto& t : song.playlistTracks)
            if (t.mode == PlaylistTrackMode::instrument)
                playing.insert (t.channel);
        for (int ch = 0; ch < (int) song.channels.size(); ++ch)
            if (song.channels[(std::size_t) ch].instrument != InstrumentKind::audioClip && ! playing.count (ch))
            {
                x.note ("Channels that play nothing on the Playlist (left out)");
                break;
            }
    }

    std::string xmlText = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    project.write (xmlText, 0);
    res.projectXml = std::move (xmlText);

    Node meta ("MetaData");
    const auto text = [&meta] (const char* tag, const std::string& v) {
        if (! v.empty())
            meta.add (tag).text = v;
    };
    text ("Title", song.info.title);
    text ("Artist", song.info.author);
    text ("Genre", song.info.genre);
    text ("Website", song.info.url);
    text ("Comment", song.info.comments);
    std::string metaText = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    meta.write (metaText, 0);
    res.metadataXml = std::move (metaText);

    std::vector<zip::NewEntry> entries;
    entries.push_back ({ "project.xml", std::vector<std::uint8_t> (res.projectXml.begin(), res.projectXml.end()) });
    entries.push_back ({ "metadata.xml", std::vector<std::uint8_t> (res.metadataXml.begin(), res.metadataXml.end()) });
    for (auto& f : x.files)
        entries.push_back (std::move (f));
    std::string err;
    res.bytes = zip::write (entries, &err);
    if (res.bytes.empty())
        x.note ("The project could not be written: " + err);
    return res;
}
} // namespace lynx
