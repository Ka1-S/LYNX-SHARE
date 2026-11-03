#include "core/EffectPresets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

#include "core/Effects.h"
#include "core/FruityBasicFx.h"
#include "core/GrossBeat.h"
#include "core/EffectPresetTable.inc"
#include "core/EffectBanks168r.inc" // round 168r: LYNX's banks for the FL effects and the simple built-ins

namespace lynx::presets
{
namespace
{
std::string trim (std::string s)
{
    const auto notSpace = [] (unsigned char c) { return ! std::isspace (c); };
    s.erase (s.begin(), std::find_if (s.begin(), s.end(), notSpace));
    s.erase (std::find_if (s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// lower case without spaces: "Concert  Hall" == "concerthall"
std::string folded (const std::string& s)
{
    std::string out;
    for (const auto c : s)
        if (! std::isspace ((unsigned char) c))
            out += (char) std::tolower ((unsigned char) c);
    return out;
}

// the number a value text shows ("−12.0 dB" → −12, "6.00 kHz" → 6000, "1/4" → none); nullopt without one
std::optional<double> numberIn (const std::string& text)
{
    std::string s;
    for (std::size_t i = 0; i < text.size(); ++i) // U+2212 minus → '-'
    {
        if ((unsigned char) text[i] == 0xE2 && i + 2 < text.size() && (unsigned char) text[i + 1] == 0x88 && (unsigned char) text[i + 2] == 0x92)
        {
            s += '-';
            i += 2;
            continue;
        }
        s += text[i];
    }
    const auto start = s.find_first_of ("+-.0123456789");
    if (start == std::string::npos)
        return std::nullopt;
    char* end = nullptr;
    const auto v = std::strtod (s.c_str() + start, &end);
    if (end == s.c_str() + start || *end == '/') // none, or a note value ("1/4"): compared as text
        return std::nullopt;
    const auto rest = folded (std::string (end));
    if (rest.rfind ("k", 0) == 0) // kHz
        return v * 1000.0;
    if (rest.rfind ("s", 0) == 0 && rest.rfind ("st", 0) != 0) // seconds (and "ms" stays ms): 1.5 s = 1500 ms
        return v * 1000.0;
    return v;
}

constexpr int grid = 2000;

EffectKind kindNamed (const std::string& name)
{
    // the exact name first: LYNX's "Frequency shifter" and FL's "Frequency Shifter" differ only in case (round 168r)
    for (int k = 0; k <= (int) lastEffectKind; ++k)
        if (name == effectInfo ((EffectKind) k).name)
            return (EffectKind) k;
    for (int k = 0; k <= (int) lastEffectKind; ++k)
        if (folded (effectInfo ((EffectKind) k).name) == folded (name))
            return (EffectKind) k;
    return EffectKind::plugin;
}

struct Table
{
    std::map<EffectKind, std::vector<Preset>> presets;
    std::map<EffectKind, std::vector<int>> keep;
};

int paramIndexOf (EffectKind kind, const std::string& name);

Table parseTable()
{
    Table table;
    std::istringstream in (std::string (builtInTable) + effectBanks168r);
    std::string line, folder;
    auto kind = EffectKind::plugin;
    while (std::getline (in, line))
    {
        if (const auto hash = line.find ('#'); hash != std::string::npos && (hash == 0 || line[hash - 1] == ' '))
            line = line.substr (0, hash);
        line = trim (line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']')
        {
            kind = kindNamed (line.substr (1, line.size() - 2));
            folder.clear();
            continue;
        }
        if (line.rfind ("keep", 0) == 0 && line.find ('=') != std::string::npos && line.find ('|') == std::string::npos)
        {
            std::istringstream names (line.substr (line.find ('=') + 1));
            std::string name;
            while (std::getline (names, name, ';'))
                if (const auto p = paramIndexOf (kind, trim (name)); p >= 0)
                    table.keep[kind].push_back (p);
            continue;
        }
        if (line.rfind ("folder ", 0) == 0)
        {
            folder = trim (line.substr (7));
            continue;
        }
        const auto bar = line.find ('|');
        if (bar == std::string::npos || kind == EffectKind::plugin)
            continue;
        Preset p;
        p.name = trim (line.substr (0, bar));
        p.folder = folder;
        std::istringstream values (line.substr (bar + 1));
        std::string entry;
        while (std::getline (values, entry, ';'))
            if (const auto eq = entry.find ('='); eq != std::string::npos)
                p.values.emplace_back (trim (entry.substr (0, eq)), trim (entry.substr (eq + 1)));
        table.presets[kind].push_back (std::move (p));
    }
    return table;
}

// Gross Beat (round 168m): LYNX's factory bank with a starting pair of slots selected — every preset carries the
// whole bank, so a preset also brings the slots back to LYNX's set
void addGrossBeat (Table& table)
{
    const auto bank = grossbeat::factoryContainer();
    struct Pick
    {
        const char* name;
        const char* folder;
        int time, volume;
    };
    static const Pick picks[] {
        { "Default", "", 0, 0 },
        { "Repeat 1/2 beat", "Time", 1, 0 },
        { "Stutter 1/8", "Time", 3, 0 },
        { "Build-up roll", "Time", 6, 0 },
        { "Half speed", "Time", 7, 0 },
        { "Tape stop", "Time", 9, 0 },
        { "Reverse beats", "Time", 11, 0 },
        { "Scratch", "Time", 14, 0 },
        { "Vinyl brake", "Time", 18, 0 },
        { "Spin back", "Time", 19, 0 },
        { "Repeat 3-3-2", "Time", 21, 0 },
        { "Lo-fi wobble", "Time", 22, 0 },
        { "Triplet roll", "Time", 24, 0 },
        { "Gate 1/4 beat", "Volume", 0, 3 },
        { "Off-beat", "Volume", 0, 6 },
        { "Pump", "Volume", 0, 9 },
        { "Trance gate A", "Volume", 0, 11 },
        { "Tremolo", "Volume", 0, 17 },
        { "Build-up gate", "Volume", 0, 19 },
        { "Sidechain hard", "Volume", 0, 23 },
        { "Sidechain soft", "Volume", 0, 24 },
        { "Gate 3-3-2", "Volume", 0, 26 },
        { "Reverse swell", "Volume", 0, 27 },
        { "Stutter and gate", "Both", 3, 2 },
        { "Half speed pump", "Both", 7, 9 },
        { "Roll and build-up gate", "Both", 6, 19 },
        { "Lo-fi tape", "Both", 22, 24 },
        { "Trap stutter", "Both", 24, 23 },
        { "Spin back pump", "Both", 19, 23 },
    };
    auto& list = table.presets[EffectKind::grossBeat];
    for (const auto& pick : picks)
    {
        Preset p;
        p.name = pick.name;
        p.folder = pick.folder;
        char v[32];
        std::snprintf (v, sizeof (v), "#%.9g", (double) grossbeat::paramFromRaw (0, pick.time));
        p.values.emplace_back ("Time slot", v);
        std::snprintf (v, sizeof (v), "#%.9g", (double) grossbeat::paramFromRaw (1, pick.volume));
        p.values.emplace_back ("Volume slot", v);
        p.state = bank;
        list.push_back (std::move (p));
    }
}

const Table& table()
{
    static const auto t = [] {
        auto parsed = parseTable();
        addGrossBeat (parsed);
        return parsed;
    }();
    return t;
}

int paramIndexOf (EffectKind kind, const std::string& name)
{
    if (kind == EffectKind::plugin)
        return -1;
    const auto& params = effectInfo (kind).params;
    const auto want = folded (name);
    for (int i = 0; i < (int) params.size(); ++i)
        if (folded (params[(std::size_t) i].name) == want)
            return i;
    return -1;
}
} // namespace

const std::vector<Preset>& factory (EffectKind kind)
{
    static const std::vector<Preset> none;
    const auto& t = table().presets;
    const auto it = t.find (kind);
    return it != t.end() ? it->second : none;
}

const std::vector<int>& keptParams (EffectKind kind)
{
    static const std::vector<int> none;
    const auto& t = table().keep;
    const auto it = t.find (kind);
    return it != t.end() ? it->second : none;
}

std::vector<EffectKind> kindsWithFactoryPresets()
{
    std::vector<EffectKind> kinds;
    for (const auto& [kind, list] : table().presets)
        if (! list.empty())
            kinds.push_back (kind);
    return kinds;
}

int paramByName (EffectKind kind, const std::string& name)
{
    return paramIndexOf (kind, name);
}

std::optional<float> valueFromText (EffectKind kind, int param, const std::string& text, double bpm)
{
    const auto t = trim (text);
    if (t.empty() || param < 0 || param >= (int) effectInfo (kind).params.size())
        return std::nullopt;
    if (t.front() == '#')
    {
        char* end = nullptr;
        const auto v = std::strtod (t.c_str() + 1, &end);
        if (end == t.c_str() + 1)
            return std::nullopt;
        return (float) std::clamp (v, 0.0, 1.0);
    }

    const auto want = folded (t);
    std::vector<std::string> texts ((std::size_t) grid + 1);
    for (int i = 0; i <= grid; ++i)
        texts[(std::size_t) i] = formatEffectParam (kind, param, (float) i / (float) grid, bpm);

    // an option's text: the middle of the run of values that show it
    int first = -1, last = -1;
    for (int i = 0; i <= grid; ++i)
        if (folded (texts[(std::size_t) i]) == want)
        {
            if (first < 0) first = i;
            last = i;
        }
        else if (first >= 0)
            break;
    if (first >= 0)
        return (float) (first + last) * 0.5f / (float) grid;

    // a number: the value whose shown number is nearest
    const auto target = numberIn (t);
    if (! target)
        return std::nullopt;
    int best = -1;
    double bestError = 0.0;
    for (int i = 0; i <= grid; ++i)
        if (const auto n = numberIn (texts[(std::size_t) i]))
        {
            const auto error = std::abs (*n - *target);
            if (best < 0 || error < bestError)
            {
                best = i;
                bestError = error;
            }
        }
    if (best < 0)
        return std::nullopt;
    // the shown number must be close: within 3 % of the target (or 0.05 absolute near zero)
    if (bestError > std::max (0.05, 0.03 * std::abs (*target)))
        return std::nullopt;
    // the middle of the run of grid points showing that same text (a stepped value's centre)
    int lo = best, hi = best;
    while (lo > 0 && texts[(std::size_t) lo - 1] == texts[(std::size_t) best]) --lo;
    while (hi < grid && texts[(std::size_t) hi + 1] == texts[(std::size_t) best]) ++hi;
    return (float) (lo + hi) * 0.5f / (float) grid;
}

EffectParams apply (EffectKind kind, const Preset& preset, double bpm, std::vector<std::string>* unresolved, const EffectParams* current)
{
    auto params = makeEffectSlot (kind).params;
    if (current != nullptr)
        for (const auto p : keptParams (kind))
            params[(std::size_t) p] = (*current)[(std::size_t) p];
    for (const auto& [name, text] : preset.values)
    {
        const auto p = paramByName (kind, name);
        const auto v = p >= 0 ? valueFromText (kind, p, text, bpm) : std::nullopt;
        if (v)
            params[(std::size_t) p] = *v;
        else if (unresolved != nullptr)
            unresolved->push_back (name + "=" + text);
    }
    return params;
}

namespace
{
const std::string sendSpanKey = "Send to span";
}

bool savesState (EffectKind kind)
{
    return kind == EffectKind::grossBeat;
}

namespace
{
constexpr const char* stateKey = "state (hex)";

std::string toHex (const std::vector<std::uint8_t>& bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve (bytes.size() * 2);
    for (const auto b : bytes)
        out += digits[b >> 4], out += digits[b & 15];
    return out;
}

std::optional<std::vector<std::uint8_t>> fromHex (const std::string& text)
{
    if (text.size() % 2 != 0)
        return std::nullopt;
    const auto nibble = [] (char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> out;
    out.reserve (text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
    {
        const auto hi = nibble (text[i]), lo = nibble (text[i + 1]);
        if (hi < 0 || lo < 0)
            return std::nullopt;
        out.push_back ((std::uint8_t) (hi * 16 + lo));
    }
    return out;
}
} // namespace

std::string toText (EffectKind kind, const std::string& name, const EffectParams& params, const std::vector<std::uint8_t>* state)
{
    std::ostringstream out;
    out << "LYNX preset 1\n";
    out << "engine = " << effectInfo (kind).name << "\n";
    out << "name = " << name << "\n";
    const auto& info = effectInfo (kind).params;
    // round 167a: Fruity Send's "Send to" over 0..127 when it fits (older LYNX versions read the same insert)
    const auto sendSpan = kind == EffectKind::fruitySend && params.size() > 3 ? flbasic::saveSendSpan ({ params[3] }) : 0;
    if (sendSpan > 0)
        out << sendSpanKey << " = " << sendSpan << "\n";
    for (std::size_t i = 0; i < info.size() && i < params.size(); ++i)
    {
        char value[32];
        const auto v = sendSpan > 0 && i == 3 ? flbasic::sendTrackValueInSpan (params[i], sendSpan) : params[i];
        std::snprintf (value, sizeof (value), "%.6g", (double) v);
        out << info[i].name << " = " << value << "\n";
    }
    if (state != nullptr && savesState (kind))
        out << stateKey << " = " << toHex (*state) << "\n";
    return out.str();
}

std::optional<Loaded> fromText (EffectKind kind, const std::string& text)
{
    std::istringstream in (text);
    std::string line;
    if (! std::getline (in, line) || trim (line).rfind ("LYNX preset", 0) != 0)
        return std::nullopt;
    Loaded loaded;
    loaded.params = makeEffectSlot (kind).params;
    bool sameEngine = false;
    std::size_t saved = 0;
    int sendSpan = flbasic::legacySendSpan; // Fruity Send presets saved before round 166d: "Send to" over 0..127
    while (std::getline (in, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = trim (line.substr (0, eq)), value = trim (line.substr (eq + 1));
        if (key == "engine")
            sameEngine = folded (value) == folded (effectInfo (kind).name);
        else if (key == "name")
            loaded.name = value;
        else if (key == sendSpanKey)
            sendSpan = std::atoi (value.c_str());
        else if (key == stateKey && savesState (kind))
        {
            if (auto bytes = fromHex (value))
            {
                loaded.state = std::move (*bytes);
                loaded.hasState = true;
            }
        }
        else if (const auto p = paramByName (kind, key); p >= 0)
        {
            loaded.params[(std::size_t) p] = (float) std::clamp (std::atof (value.c_str()), 0.0, 1.0);
            saved = std::max (saved, (std::size_t) p + 1);
        }
    }
    if (! sameEngine)
        return std::nullopt;
    if (kind == EffectKind::fruitySend)
        loaded.params[3] = flbasic::rescaleSendTrackValue (loaded.params[3], sendSpan);
    // round 165w: a preset written before an engine's appended law loads like a project of that time (the old law)
    EffectSlot slot = makeEffectSlot (kind);
    slot.params = loaded.params;
    upgradeLoadedEffectParams (slot, saved);
    loaded.params = slot.params;
    return loaded;
}

std::string fileNameFor (const std::string& presetName)
{
    std::string out;
    for (const auto c : trim (presetName))
        out += std::string ("\\/:*?\"<>|").find (c) != std::string::npos || (unsigned char) c < 32 ? '_' : c;
    out = trim (out);
    while (! out.empty() && out.back() == '.')
        out.pop_back();
    if (out.size() > 64)
        out.resize (64);
    return out.empty() ? "Preset" : out;
}
} // namespace lynx::presets
