#include "core/LoopStarter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <set>

namespace lynx::loopstarter
{
// ---------------------------------------------------------------------------------------------------------------------
// FL's generator (System/Tools/Chord progression/toolbox/toolbox.py rand_int): state = {seed, (75 seed + 74) mod 65537};
// each draw returns (s0 * 2654435771) & 0x7FFFFFFF and moves the state on with two rotations. Python's integers are
// unbounded, so the first draw sees every bit of a large (time-based) seed: 64-bit arithmetic keeps them.
namespace
{
std::uint64_t rotl (std::uint64_t x, int k) { return (x << k) | (x >> (32 - k)); }
}

FlRand::FlRand (std::uint64_t seed)
    : s0 (seed), s1 ((std::uint64_t) (((unsigned __int128) seed * 75u + 74u) % 65537u))
{
}

std::uint32_t FlRand::nextInt()
{
    const auto result = (std::uint32_t) ((s0 * 2654435771ull) & 0x7FFFFFFFull);
    auto a = s0, b = s1 ^ s0;
    s0 = (rotl (a, 26) ^ b ^ (b << 9)) & 0xFFFFFFFFull;
    s1 = rotl (b, 13) & 0xFFFFFFFFull;
    return result;
}

double FlRand::uniform() { return (double) nextInt() / 2147483648.0; }

double FlRand::triangular (double mode, int curvature)
{
    auto x = uniform();
    for (int i = 0; i < std::max (1, curvature); ++i)
        x = x <= mode ? std::sqrt (x * mode) : 1.0 - std::sqrt ((mode - 1.0) * (x - 1.0));
    return x;
}

// ---------------------------------------------------------------------------------------------------------------------
// A small JSON reader (the two FL files only).
namespace
{
struct JsonMember;

struct Json
{
    enum class Type { null, boolean, number, string, array, object } type = Type::null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Json> items;
    std::vector<JsonMember> members; // a vector of a complete-later type (std::pair<std::string, Json> is ill-formed here: clang + libstdc++ 14)

    const Json* get (const std::string& key) const;
    bool isString() const { return type == Type::string; }
    bool isNumber() const { return type == Type::number; }
    bool isArray() const { return type == Type::array; }
    bool isObject() const { return type == Type::object; }
};

struct JsonMember
{
    std::string first;
    Json second;
};

const Json* Json::get (const std::string& key) const
{
    for (const auto& [k, v] : members)
        if (k == key)
            return &v;
    return nullptr;
}

class JsonReader
{
public:
    explicit JsonReader (const std::string& t) : text (t) {}

    std::optional<Json> parse()
    {
        Json v;
        if (! value (v, 0))
            return std::nullopt;
        space();
        if (pos != text.size())
            return std::nullopt;
        return v;
    }

private:
    void space()
    {
        while (pos < text.size() && std::isspace ((unsigned char) text[pos]))
            ++pos;
    }

    bool literal (const char* word)
    {
        const auto len = std::char_traits<char>::length (word);
        if (text.compare (pos, len, word) != 0)
            return false;
        pos += len;
        return true;
    }

    static void appendUtf8 (std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80)
            out += (char) cp;
        else if (cp < 0x800)
        {
            out += (char) (0xC0 | (cp >> 6));
            out += (char) (0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += (char) (0xE0 | (cp >> 12));
            out += (char) (0x80 | ((cp >> 6) & 0x3F));
            out += (char) (0x80 | (cp & 0x3F));
        }
        else
        {
            out += (char) (0xF0 | (cp >> 18));
            out += (char) (0x80 | ((cp >> 12) & 0x3F));
            out += (char) (0x80 | ((cp >> 6) & 0x3F));
            out += (char) (0x80 | (cp & 0x3F));
        }
    }

    bool hex4 (std::uint32_t& out)
    {
        if (pos + 4 > text.size())
            return false;
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto c = text[pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (std::uint32_t) (c - '0');
            else if (c >= 'a' && c <= 'f') out |= (std::uint32_t) (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (std::uint32_t) (c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string (std::string& out)
    {
        if (pos >= text.size() || text[pos] != '"')
            return false;
        ++pos;
        while (pos < text.size())
        {
            const auto c = text[pos++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos >= text.size())
                return false;
            const auto e = text[pos++];
            switch (e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    std::uint32_t cp = 0;
                    if (! hex4 (cp))
                        return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && text.compare (pos, 2, "\\u") == 0)
                    {
                        pos += 2;
                        std::uint32_t lo = 0;
                        if (! hex4 (lo))
                            return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8 (out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool value (Json& v, int depth)
    {
        if (depth > 64)
            return false;
        space();
        if (pos >= text.size())
            return false;
        const auto c = text[pos];
        if (c == '{')
        {
            ++pos;
            v.type = Json::Type::object;
            space();
            if (pos < text.size() && text[pos] == '}')
            {
                ++pos;
                return true;
            }
            for (;;)
            {
                space();
                std::string key;
                if (! string (key))
                    return false;
                space();
                if (pos >= text.size() || text[pos] != ':')
                    return false;
                ++pos;
                Json member;
                if (! value (member, depth + 1))
                    return false;
                v.members.push_back (JsonMember { std::move (key), std::move (member) });
                space();
                if (pos < text.size() && text[pos] == ',')
                {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == '}')
                {
                    ++pos;
                    return true;
                }
                return false;
            }
        }
        if (c == '[')
        {
            ++pos;
            v.type = Json::Type::array;
            space();
            if (pos < text.size() && text[pos] == ']')
            {
                ++pos;
                return true;
            }
            for (;;)
            {
                Json item;
                if (! value (item, depth + 1))
                    return false;
                v.items.push_back (std::move (item));
                space();
                if (pos < text.size() && text[pos] == ',')
                {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == ']')
                {
                    ++pos;
                    return true;
                }
                return false;
            }
        }
        if (c == '"')
        {
            v.type = Json::Type::string;
            return string (v.s);
        }
        if (literal ("true")) { v.type = Json::Type::boolean; v.b = true; return true; }
        if (literal ("false")) { v.type = Json::Type::boolean; return true; }
        if (literal ("null")) { v.type = Json::Type::null; return true; }
        // number
        const auto start = pos;
        if (pos < text.size() && (text[pos] == '-' || text[pos] == '+'))
            ++pos;
        while (pos < text.size() && (std::isdigit ((unsigned char) text[pos]) || text[pos] == '.' || text[pos] == 'e'
                                     || text[pos] == 'E' || text[pos] == '-' || text[pos] == '+'))
            ++pos;
        if (pos == start)
            return false;
        v.type = Json::Type::number;
        try
        {
            v.n = std::stod (text.substr (start, pos - start));
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    const std::string& text;
    std::size_t pos = 0;
};

double number (const Json* j, double fallback)
{
    return j != nullptr && j->isNumber() ? j->n : fallback;
}

std::string trim (std::string s)
{
    while (! s.empty() && std::isspace ((unsigned char) s.back()))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace ((unsigned char) s[i]))
        ++i;
    return s.substr (i);
}

std::vector<std::string> splitCategories (const std::string& entry)
{
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= entry.size())
    {
        const auto bar = entry.find ('|', from);
        auto part = trim (entry.substr (from, bar == std::string::npos ? std::string::npos : bar - from));
        if (! part.empty())
            out.push_back (part);
        if (bar == std::string::npos)
            break;
        from = bar + 1;
    }
    return out;
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}
} // namespace

double Genre::kitWeight (const std::string& category) const
{
    for (const auto& [c, w] : kit)
        if (c == category)
            return w;
    return -1.0;
}

const Genre* Config::find (const std::string& id) const
{
    for (const auto& g : genres)
        if (g.id == id)
            return &g;
    return nullptr;
}

std::vector<const Genre*> Config::listed() const
{
    std::vector<const Genre*> out;
    for (const auto& g : genres)
        if (g.hasContent)
            out.push_back (&g);
    return out;
}

std::optional<Config> parseConfig (const std::string& text)
{
    const auto root = JsonReader (text).parse();
    if (! root || ! root->isObject())
        return std::nullopt;
    const auto* genres = root->get ("genres");
    if (genres == nullptr || ! genres->isObject())
        return std::nullopt;

    Config config;
    config.fromFl = true;
    if (const auto* d = root->get ("default_genre"); d != nullptr && d->isString())
        config.defaultGenre = d->s;
    if (const auto* b = root->get ("same_sample_block_count"); b != nullptr && b->isObject())
        config.sameSampleBlockCount = (int) number (b->get ("oneshot"), 3.0);
    const auto* tagMap = root->get ("tag_map");

    for (const auto& [id, g] : genres->members)
    {
        if (! g.isObject())
            continue;
        Genre genre;
        genre.id = id;
        genre.label = id;
        // the picker shows tag_map[genre].label (FLEngine 0x00dbf640)
        if (tagMap != nullptr)
            if (const auto* tag = tagMap->get (id); tag != nullptr)
                if (const auto* label = tag->get ("label"); label != nullptr && label->isString())
                    genre.label = label->s;
        genre.weight = number (g.get ("weight"), 1.0);
        if (const auto* kit = g.get ("kit"); kit != nullptr && kit->isObject())
            for (const auto& [c, w] : kit->members)
                genre.kit.emplace_back (c, w.isNumber() ? w.n : 1.0);
        if (const auto* size = g.get ("size"); size != nullptr && size->isArray())
        {
            if (! size->items.empty() && size->items[0].isNumber())
                genre.sizeMin = (int) size->items[0].n;
            if (size->items.size() > 1 && size->items[1].isNumber())
                genre.sizeMax = (int) size->items[1].n;
        }
        genre.defaultTempo = number (g.get ("default_tempo"), 120.0);
        if (const auto* range = g.get ("tempo_range"); range != nullptr && range->isArray() && range->items.size() == 2)
        {
            genre.tempoMin = number (&range->items[0], 0.0);
            genre.tempoMax = number (&range->items[1], 0.0);
        }
        genre.defaultSwing = number (g.get ("default_swing"), 0.0);
        // FL lists a genre only when its "content" has loops or one-shots (FLEngine 0x00dbf640)
        genre.hasContent = false;
        if (const auto* content = g.get ("content"); content != nullptr && content->isArray())
            for (const auto& c : content->items)
                if (c.isString() && (c.s == "loops" || c.s == "oneshots"))
                    genre.hasContent = true;

        // the FL Studio template's kit channels (Sampler / Drum), their categories from oneshot_channels
        const auto* oneshots = g.get ("oneshot_channels");
        if (const auto* templates = g.get ("templates"); templates != nullptr && templates->isArray())
            for (const auto& t : templates->items)
            {
                const auto* host = t.get ("host");
                if (host == nullptr || ! host->isString() || host->s != "FLS")
                    continue;
                if (const auto* channels = t.get ("channels"); channels != nullptr && channels->isArray())
                    for (const auto& c : channels->items)
                    {
                        const auto* tool = c.get ("tool");
                        const auto* label = c.get ("label");
                        if (tool == nullptr || ! tool->isString() || tool->s != "Drum" || label == nullptr || ! label->isString())
                            continue;
                        KitSlot slot;
                        slot.label = label->s;
                        if (oneshots != nullptr)
                            if (const auto* oc = oneshots->get (label->s); oc != nullptr)
                                if (const auto* options = oc->get ("options"); options != nullptr && options->isArray())
                                    for (const auto& o : options->items)
                                        if (o.isString())
                                            slot.options.push_back (o.s);
                        if (! slot.options.empty())
                            genre.kitSlots.push_back (std::move (slot));
                    }
                break;
            }
        config.genres.push_back (std::move (genre));
    }

    if (config.genres.empty())
        return std::nullopt;
    if (config.find (config.defaultGenre) == nullptr)
        config.defaultGenre = config.genres.front().id;
    return config;
}

Config builtinConfig()
{
    // LYNX's own small set (used without an FL installation): typical tempos and a five-piece kit
    struct Row { const char* id; const char* label; double tempo, lo, hi; };
    static const Row rows[] = {
        { "boom bap", "Boom bap", 90, 84, 96 }, { "drill", "Drill", 142, 138, 146 }, { "edm", "EDM", 126, 122, 128 },
        { "hip hop", "Hip hop", 92, 86, 100 }, { "house", "House", 124, 120, 126 }, { "lo-fi", "Lo-fi", 80, 70, 90 },
        { "techno", "Techno", 130, 126, 134 }, { "trap", "Trap", 140, 135, 146 },
    };
    Config config;
    config.defaultGenre = "edm";
    for (const auto& r : rows)
    {
        Genre g;
        g.id = r.id;
        g.label = r.label;
        g.defaultTempo = r.tempo;
        g.tempoMin = r.lo;
        g.tempoMax = r.hi;
        g.kit = { { "kick", 1.0 }, { "snare", 1.0 }, { "clap", 0.8 }, { "closed hat", 1.0 }, { "open hat", 0.7 }, { "percussion", 0.6 } };
        g.sizeMin = 4;
        g.kitSlots = { { "Kit 1", { "kick" } }, { "Kit 2", { "snare", "clap" } }, { "Kit 3", { "closed hat" } },
                       { "Kit 4", { "open hat" } }, { "Kit 5", { "clap", "snare" } }, { "Kit 6", { "percussion", "shaker" } } };
        config.genres.push_back (std::move (g));
    }
    return config;
}

// ---------------------------------------------------------------------------------------------------------------------
const Lane* Preset::laneFor (const std::string& category) const
{
    for (const auto& l : lanes)
        if (! l.kit.empty() && l.kit.front() == category)
            return &l;
    for (const auto& l : lanes)
        if (std::find (l.kit.begin(), l.kit.end(), category) != l.kit.end())
            return &l;
    return nullptr;
}

std::vector<Preset> parsePresets (const std::string& text)
{
    std::vector<Preset> out;
    const auto root = JsonReader (text).parse();
    if (! root || ! root->isObject())
        return out;

    auto strings = [] (const Json* j) {
        std::vector<std::string> v;
        if (j != nullptr && j->isArray())
            for (const auto& i : j->items)
                v.push_back (i.isString() ? i.s : std::string());
        return v;
    };

    for (const auto& [name, p] : root->members)
    {
        if (! p.isObject())
            continue;
        Preset preset;
        preset.name = name;
        if (const auto* g = p.get ("genre"); g != nullptr && g->isString())
            preset.genre = g->s;
        preset.weight = number (p.get ("weight"), 1.0);
        const auto kit = strings (p.get ("kit"));
        const auto rhythm = strings (p.get ("rhythm_strs")), repeat = strings (p.get ("repeat_strs")),
                   shift = strings (p.get ("shift_strs")), oct = strings (p.get ("oct_strs")),
                   note = strings (p.get ("note_strs")), vel = strings (p.get ("vel_strs"));
        const auto* refs = p.get ("refs");

        for (std::size_t i = 0; i < kit.size() && i < rhythm.size(); ++i)
        {
            Lane lane;
            lane.kit = splitCategories (kit[i]);
            auto at = [i] (const std::vector<std::string>& v) { return i < v.size() ? v[i] : std::string(); };
            lane.main = { rhythm[i], at (repeat), at (shift), at (oct), at (note), at (vel) };

            // refs[kit entry]: {rhythm: [bar strings], repeat: [...], ...} — the preset's per-bar variants
            if (refs != nullptr && refs->isObject())
                if (const auto* r = refs->get (kit[i]); r != nullptr && r->isObject())
                {
                    const auto bars = strings (r->get ("rhythm"));
                    const auto rp = strings (r->get ("repeat")), sh = strings (r->get ("shift")), oc = strings (r->get ("oct")),
                               no = strings (r->get ("note")), ve = strings (r->get ("vel"));
                    for (std::size_t b = 0; b < bars.size(); ++b)
                    {
                        auto bt = [b] (const std::vector<std::string>& v) { return b < v.size() ? v[b] : std::string(); };
                        LaneBar bar { bars[b], bt (rp), bt (sh), bt (oc), bt (no), bt (ve) };
                        if (bar.rhythm.empty())
                            bar = lane.main;
                        lane.bars.push_back (std::move (bar));
                    }
                }
            if (! lane.kit.empty())
                preset.lanes.push_back (std::move (lane));
        }
        if (! preset.lanes.empty())
            out.push_back (std::move (preset));
    }
    return out;
}

std::vector<Preset> builtinPresets (const std::string& genreId)
{
    // LYNX's own one-bar grooves ('>' hit, '`' rest), a few per style; velocity lanes give the accents
    struct Row { const char* style; const char* kick; const char* snare; const char* clap; const char* hat; const char* open; const char* perc; const char* hatVel; };
    static const Row rows[] = {
        { "four", ">```>```>```>```", "````````````````", "````>```````>```", ">`>`>`>`>`>`>`>`", "``>```>```>```>`", "``````>```````>`", "9393939393939393" },
        { "four", ">```>```>```>```", "````>```````>```", "````````````````", "``>```>```>```>`", "``>```>```>```>`", "```>``>````>``>`", "7777777777777777" },
        { "four", ">```>```>```>``>", "````````````````", "````>```````>```", ">>>>>>>>>>>>>>>>", "``>```>```>```>`", "``````````>`````", "b5a5b5a5b5a5b5a5" },
        { "boom", ">``````>`>``````", "````>```````>```", "````````````````", ">`>`>`>`>`>`>`>`", "``````````````>`", "```````>````````", "9595959595959595" },
        { "boom", ">``````>``>`>```", "````>```````>``>", "````````````````", ">`>`>`>`>`>`>`>`", "``````>`````````", "``````````````>`", "a6a6a6a6a6a6a6a6" },
        { "trap", ">`````````>`````", "````````>```````", "````````>```````", ">`>`>`>`>`>>>`>`", "``````````````>`", "```>```````>````", "7777777777777777" },
        { "trap", ">``````>``>`````", "````````>```````", "````````>```````", ">>>>>`>`>`>>>>>`", "``````````>`````", "``````````````>`", "a7a7a7a7a7a7a7a7" },
        { "half", ">`````````>``>``", "````````>```````", "````````>``````>", ">`>`>`>`>`>`>`>`", "``````>`````````", "`````>````````>`", "8686868686868686" },
    };
    std::string style = "four";
    if (genreId == "boom bap" || genreId == "hip hop" || genreId == "lo-fi" || genreId == "r&b")
        style = "boom";
    else if (genreId == "trap" || genreId == "phonk" || genreId == "drill")
        style = "trap";
    else if (genreId == "dubstep" || genreId == "d&b")
        style = "half";

    std::vector<Preset> out;
    int n = 0;
    for (const auto& r : rows)
    {
        if (style != r.style && ! (style == "half" && std::string (r.style) == "trap"))
            continue;
        Preset p;
        p.name = "LYNX " + style + " " + std::to_string (++n);
        p.genre = genreId;
        auto lane = [&] (std::initializer_list<const char*> kit, const char* rhythm, const char* vel) {
            Lane l;
            for (const auto* k : kit)
                l.kit.push_back (k);
            l.main.rhythm = rhythm;
            if (vel != nullptr)
                l.main.vel = vel;
            p.lanes.push_back (std::move (l));
        };
        lane ({ "kick", "sub kick" }, r.kick, nullptr);
        lane ({ "snare", "rim" }, r.snare, nullptr);
        lane ({ "clap", "ghost snare" }, r.clap, nullptr);
        lane ({ "closed hat", "extra hat" }, r.hat, r.hatVel);
        lane ({ "open hat", "ride", "crash" }, r.open, nullptr);
        lane ({ "percussion", "shaker", "tom", "hand drums", "cowbell" }, r.perc, nullptr);
        out.push_back (std::move (p));
    }
    return out;
}

std::string presetFileName (const std::string& genreId) { return genreId + " presets.lstj"; }

// ---------------------------------------------------------------------------------------------------------------------
namespace
{
int tokenIndex (char t)
{
    if (t >= '0' && t <= '9')
        return t - '0';
    if (t >= 'a' && t <= 'z')
        return 10 + (t - 'a');
    return -1;
}
} // namespace

float velocityOfToken (char token)
{
    auto state = tokenIndex (token);
    if (state < 0 || state > 13)
        state = 7; // VelocityLoop: values range(72, 125, 4) / 128, default state 7
    return (float) (72 + 4 * state) / 128.0f;
}

double shiftOfToken (char token)
{
    if (token < 'a' || token > 'x')
        return 0.0; // ShiftLoop: tokens chr(97 + i) for 24 states, shift = state / 24
    return (double) (token - 'a') / 24.0;
}

int octaveOfToken (char token)
{
    return token == 'q' ? -1 : token == 's' ? 1 : 0; // OctaveLoop: q r s, offset = state - 1
}

int semitoneOfToken (char token)
{
    const auto s = tokenIndex (token);
    return s >= 0 && s <= 11 ? s : 0; // OffsetLoop: 0..9 a b
}

double repeatProportionOfToken (char token)
{
    static const double proportions[] = { 1.0, 0.75, 2.0 / 3.0, 0.5, 0.375, 1.0 / 3.0, 0.25, 1.0 / 6.0 }; // STEP_PROPORTION
    const auto s = tokenIndex (token);
    return s >= 0 && s < 8 ? proportions[s] : 1.0;
}

std::vector<Note> laneNotes (const Lane& lane, int bars, int channel, int key)
{
    struct Hit { std::int64_t start; float vel; int key; double repeat; };
    std::vector<Hit> hits;
    bars = std::max (1, bars);
    std::int64_t barTicks = 16 * ticksPerStep;

    for (int b = 0; b < bars; ++b)
    {
        const auto& bar = lane.bars.empty() ? lane.main : lane.bars[(std::size_t) b % lane.bars.size()];
        const auto steps = (int) bar.rhythm.size();
        if (steps == 0)
            continue;
        barTicks = (std::int64_t) steps * ticksPerStep;
        auto token = [&] (const std::string& s, const std::string& fallback, int i) -> char {
            const auto& use = s.size() == (std::size_t) steps ? s : fallback;
            return i < (int) use.size() ? use[(std::size_t) i] : '.';
        };
        for (int i = 0; i < steps; ++i)
        {
            if (bar.rhythm[(std::size_t) i] != '>')
                continue;
            Hit h;
            const auto shift = shiftOfToken (token (bar.shift, lane.main.shift, i));
            h.start = (std::int64_t) b * barTicks + (std::int64_t) i * ticksPerStep + (std::int64_t) (ticksPerStep * shift);
            h.vel = velocityOfToken (token (bar.vel, lane.main.vel, i));
            h.key = std::clamp (key + 12 * octaveOfToken (token (bar.oct, lane.main.oct, i)) + semitoneOfToken (token (bar.note, lane.main.note, i)), 0, 127);
            h.repeat = repeatProportionOfToken (token (bar.repeat, lane.main.repeat, i));
            hits.push_back (h);
        }
    }

    std::vector<Note> notes;
    const auto end = (std::int64_t) bars * barTicks;
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const auto& h = hits[i];
        const auto next = i + 1 < hits.size() ? hits[i + 1].start : end;
        auto add = [&] (std::int64_t at, std::int64_t len) {
            Note n;
            n.start = at;
            n.length = std::max<std::int64_t> (1, len);
            n.channel = channel;
            n.key = h.key;
            n.velocity = h.vel;
            notes.push_back (n);
        };
        if (h.repeat >= 1.0)
        {
            add (h.start, std::min<std::int64_t> (ticksPerStep, std::max<std::int64_t> (1, next - h.start)));
            continue;
        }
        // FL's note repeat: the hit repeats every `repeat` of a step until the next hit (at most a beat)
        const auto every = std::max<std::int64_t> (1, (std::int64_t) std::llround (ticksPerStep * h.repeat));
        const auto stop = std::min (next, h.start + 4 * ticksPerStep);
        for (auto t = h.start; t < stop; t += every)
            add (t, std::min (every, stop - t));
    }
    return notes;
}

// ---------------------------------------------------------------------------------------------------------------------
namespace
{
std::vector<std::string> words (const std::string& s)
{
    std::vector<std::string> out;
    std::string w;
    for (const auto c : s)
    {
        if (std::isalnum ((unsigned char) c))
            w += (char) std::tolower ((unsigned char) c);
        else if (! w.empty())
        {
            out.push_back (w);
            w.clear();
        }
    }
    if (! w.empty())
        out.push_back (w);
    // "Kick01" / "Snare_3" -> also the letters without the digits
    std::vector<std::string> extra;
    for (const auto& x : out)
    {
        std::string letters;
        for (const auto c : x)
            if (! std::isdigit ((unsigned char) c))
                letters += c;
        if (! letters.empty() && letters != x)
            extra.push_back (letters);
    }
    out.insert (out.end(), extra.begin(), extra.end());
    return out;
}

bool hasAny (const std::vector<std::string>& w, std::initializer_list<const char*> keys)
{
    for (const auto* k : keys)
        if (std::find (w.begin(), w.end(), k) != w.end())
            return true;
    return false;
}

std::string categoryOfWords (const std::vector<std::string>& w)
{
    const bool hat = hasAny (w, { "hat", "hats", "hihat", "hihats", "hh", "hi", "chat", "ohat", "phat" });
    if (hasAny (w, { "ghost" }) && hasAny (w, { "snare", "snr", "sd" }))
        return "ghost snare";
    // a drum kit's microphones ("tom1_oh", "kick room") name the drum first: "oh" / "ch" alone are weak words
    const bool otherDrum = hasAny (w, { "kick", "snare", "tom", "toms", "clap", "rim", "ride", "crash", "cymbal", "perc" });
    if ((hat && hasAny (w, { "open", "oh", "ohat" })) || hasAny (w, { "ohh", "ophat", "openhat", "ohat" })
        || (! otherDrum && hasAny (w, { "oh" })))
        return "open hat";
    if (hat || hasAny (w, { "chh", "clhat", "closedhat", "pedal" }) || (! otherDrum && hasAny (w, { "ch" })))
        return "closed hat";
    if (hasAny (w, { "ride" }))
        return "ride";
    if (hasAny (w, { "crash", "splash", "china", "cymbal", "cymbals", "cym" }))
        return "crash";
    if (hasAny (w, { "rim", "rims", "rimshot", "sidestick", "stick" }))
        return "rim";
    if (hasAny (w, { "clap", "claps", "clp" }))
        return "clap";
    if (hasAny (w, { "snare", "snares", "snr", "sd" }))
        return "snare";
    if (hasAny (w, { "sub" }) || (hasAny (w, { "808" }) && hasAny (w, { "bass", "boom" })))
        return "sub kick";
    if (hasAny (w, { "kick", "kicks", "kik", "bd", "bassdrum" }))
        return "kick";
    if (hasAny (w, { "shaker", "shakers", "shk", "tamb", "tambourine", "maraca", "maracas" }))
        return "shaker";
    if (hasAny (w, { "tom", "toms" }))
        return "tom";
    if (hasAny (w, { "conga", "congas", "bongo", "bongos", "djembe", "tabla", "timbale", "timbales", "cajon" }))
        return "hand drums";
    if (hasAny (w, { "cowbell" }) || (hasAny (w, { "cow" }) && hasAny (w, { "bell" })))
        return "cowbell";
    if (hasAny (w, { "perc", "percs", "percussion", "clav", "claves", "block", "woodblock", "guiro", "triangle", "cabasa", "agogo", "snap", "snaps" }))
        return "percussion";
    return {};
}
} // namespace

std::string classifySample (const std::string& path)
{
    auto p = path;
    std::replace (p.begin(), p.end(), '\\', '/');
    const auto slash = p.find_last_of ('/');
    auto file = slash == std::string::npos ? p : p.substr (slash + 1);
    const auto dot = file.find_last_of ('.');
    const auto ext = dot == std::string::npos ? std::string() : lower (file.substr (dot + 1));
    static const std::set<std::string> audio { "wav", "wv", "flac", "ogg", "mp3", "aif", "aiff" };
    if (audio.count (ext) == 0)
        return {};
    if (dot != std::string::npos)
        file = file.substr (0, dot);

    const auto folderPart = slash == std::string::npos ? std::string() : p.substr (0, slash);
    const auto folders = words (folderPart);
    const auto name = words (file);
    // loops, construction kits, vocals and effects are not one-shots
    if (hasAny (name, { "loop", "loops", "bpm", "fill", "roll", "break", "breaks" })
        || hasAny (folders, { "loops", "vocals", "instruments", "risers", "flex", "shapes", "sfx", "fx", "foley" }))
        return {};
    // multi-microphone kits (FL's Drums/Kits/<kit>/...): the room / overhead mics and the extra round robins are not
    // one-shots of their own
    if (hasAny (folders, { "kits", "kit" })
        && hasAny (name, { "oh", "room", "rooms", "overhead", "overheads", "amb", "bleed", "rr2", "rr3", "rr4", "rr5", "rr6", "rr7", "rr8" }))
        return {};

    if (auto c = categoryOfWords (name); ! c.empty())
        return c;
    // the folder (FL's packs: Drums/Kicks, Drums (ModeAudio)/Hi Hats...); the last one that says something
    std::vector<std::string> parts;
    {
        std::string w;
        for (const auto c : folderPart)
        {
            if (c == '/')
            {
                parts.push_back (w);
                w.clear();
            }
            else
                w += c;
        }
        parts.push_back (w);
    }
    for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        if (auto c = categoryOfWords (words (*it)); ! c.empty())
            return c;
    return {};
}

std::vector<std::string> fallbackCategories (const std::string& c)
{
    if (c == "ghost snare") return { "snare", "rim", "clap" };
    if (c == "sub kick") return { "kick" };
    if (c == "rim") return { "snare", "percussion" };
    if (c == "clap") return { "snare" };
    if (c == "snare") return { "clap" };
    if (c == "extra hat") return { "closed hat" };
    if (c == "open hat") return { "closed hat" };
    if (c == "shaker") return { "percussion", "closed hat" };
    if (c == "hand drums" || c == "cowbell" || c == "tom") return { "percussion" };
    if (c == "percussion") return { "shaker", "hand drums", "rim" };
    if (c == "ride" || c == "crash" || c == "cymbals") return { "crash", "ride", "open hat" };
    return {};
}

void Library::add (const std::string& path)
{
    if (const auto c = classifySample (path); ! c.empty())
        byCategory[c].push_back (path);
}

std::size_t Library::size() const noexcept
{
    std::size_t n = 0;
    for (const auto& [c, v] : byCategory)
        n += v.size();
    return n;
}

InstrumentKind builtinDrumFor (const std::string& c)
{
    if (c == "kick" || c == "sub kick")
        return InstrumentKind::kick;
    if (c == "snare" || c == "ghost snare" || c == "rim")
        return InstrumentKind::snare;
    if (c == "clap")
        return InstrumentKind::clap;
    return InstrumentKind::hat;
}

// ---------------------------------------------------------------------------------------------------------------------
double pickTempo (const Genre& g, FlRand& rng)
{
    // util_rack.update_pattern_tempo(randomise): int(rand_tri(mode, 2) * width) + low, mode = (default - low) / width
    const auto width = g.tempoMax - g.tempoMin;
    if (width <= 0.0)
        return g.defaultTempo;
    const auto mode = std::clamp ((g.defaultTempo - g.tempoMin) / width, 0.0, 1.0);
    return (double) (int) (rng.triangular (mode, 2) * width) + g.tempoMin;
}

std::string pickSample (const Library& library, const std::string& category, FlRand& rng,
                        const std::vector<std::string>& recent, std::string* pickedCategory)
{
    std::vector<std::string> order { category };
    for (const auto& f : fallbackCategories (category))
        order.push_back (f);
    for (const auto& c : order)
    {
        const auto it = library.byCategory.find (c);
        if (it == library.byCategory.end() || it->second.empty())
            continue;
        std::vector<std::string> pool;
        for (const auto& s : it->second)
            if (std::find (recent.begin(), recent.end(), s) == recent.end())
                pool.push_back (s);
        if (pool.empty())
            pool = it->second;
        if (pickedCategory != nullptr)
            *pickedCategory = c;
        return pool[(std::size_t) (rng.nextInt() % pool.size())];
    }
    return {};
}

std::vector<Note> generateSteps (const Sources& sources, const std::string& genreId, const std::string& category,
                                 FlRand& rng, int* barsOut, std::string* presetOut)
{
    std::vector<std::pair<const Preset*, const Lane*>> candidates;
    std::vector<double> weights;
    auto collect = [&] (const std::vector<Preset>& presets, const std::string& c) {
        for (const auto& p : presets)
            if (const auto* lane = p.laneFor (c))
            {
                candidates.emplace_back (&p, lane);
                weights.push_back (std::max (0.0, p.weight));
            }
    };
    const auto it = sources.presets.find (genreId);
    if (it != sources.presets.end())
    {
        collect (it->second, category);
        for (const auto& f : fallbackCategories (category))
            if (candidates.empty())
                collect (it->second, f);
    }
    if (candidates.empty())
    {
        static std::map<std::string, std::vector<Preset>> own;
        auto& b = own[genreId];
        if (b.empty())
            b = builtinPresets (genreId);
        collect (b, category);
        for (const auto& f : fallbackCategories (category))
            if (candidates.empty())
                collect (b, f);
    }
    if (candidates.empty())
        return {};

    // util.get_random_preset_name: u * sum(weights), the first preset whose running sum passes it
    double total = 0.0;
    for (const auto w : weights)
        total += w;
    const auto target = rng.uniform() * total;
    std::size_t pick = candidates.size() - 1;
    double run = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i)
    {
        run += weights[i];
        if (run > target)
        {
            pick = i;
            break;
        }
    }
    const auto* lane = candidates[pick].second;
    // a 4-bar loop: bar b plays the preset's variant b % their count (or its one bar again)
    const auto bars = loopBars;
    if (barsOut != nullptr)
        *barsOut = bars;
    if (presetOut != nullptr)
        *presetOut = candidates[pick].first->name;
    return laneNotes (*lane, bars, 0, 60);
}

void setBlanks (const Genre& genre, std::vector<ChannelPlan>& channels, FlRand& rng, const std::vector<bool>& locked)
{
    // DrumKit.set_blanks: nothing below the kit's minimum size; the unlocked slots sorted by kit weight (lightest
    // first) get blank with odds (1 - weight) * KIT_BLANK_CHANCE_MULT, at most DRUMS_MAX_BLANK of them, while more than
    // DRUMS_MIN_NON_BLANK stay
    constexpr int maxBlank = 2, minNonBlank = 2;
    constexpr double blankChanceMult = 0.3;
    if ((int) channels.size() < genre.sizeMin)
        return;
    std::vector<std::pair<std::size_t, double>> order;
    for (std::size_t i = 0; i < channels.size(); ++i)
    {
        const auto w = genre.kitWeight (channels[i].category);
        if (w >= 0.0 && ! (i < locked.size() && locked[i]))
            order.emplace_back (i, w);
    }
    std::stable_sort (order.begin(), order.end(), [] (const auto& a, const auto& b) { return a.second < b.second; });
    int blanks = 0, nonBlank = (int) order.size();
    for (const auto& [index, weight] : order)
    {
        if (blanks >= maxBlank || nonBlank <= minNonBlank)
            return;
        if (rng.uniform() < (1.0 - weight) * blankChanceMult)
        {
            channels[index].blank = true;
            channels[index].notes.clear();
            ++blanks;
            --nonBlank;
        }
    }
}

int suggestedTempo (const Genre& genre)
{
    return (int) std::lround (genre.defaultTempo);
}

int suggestedTempo (const Sources& sources, const std::string& genreId)
{
    const auto* genre = sources.config != nullptr ? sources.config->find (genreId) : nullptr;
    static const Config own = builtinConfig();
    if (genre == nullptr)
        genre = own.find (genreId) != nullptr ? own.find (genreId) : own.find (own.defaultGenre);
    return suggestedTempo (*genre);
}

Plan generate (const Sources& sources, const std::string& genreId, std::uint64_t seed)
{
    Plan plan;
    FlRand rng (seed);
    const auto* genre = sources.config != nullptr ? sources.config->find (genreId) : nullptr;
    static const Config own = builtinConfig();
    if (genre == nullptr)
        genre = own.find (genreId) != nullptr ? own.find (genreId) : own.find (own.defaultGenre);
    plan.genre = genre->id;
    plan.bpm = pickTempo (*genre, rng);
    plan.swing = (float) std::clamp (genre->defaultSwing, 0.0, 1.0);

    static const Library none;
    const auto& library = sources.library != nullptr ? *sources.library : none;
    for (const auto& slot : genre->kitSlots)
    {
        ChannelPlan c;
        c.slot = slot.label;
        c.category = slot.options.empty() ? std::string ("kick") : slot.options.front(); // DRUM_SELECTION_MODE 'first'
        // the slot's sample: one of its categories (the channel then plays the category of the sample it got)
        if (! library.empty())
        {
            std::vector<std::string> options = slot.options;
            rng.shuffle (options);
            // a category the library has samples of first, then one that falls back on a related category
            std::stable_partition (options.begin(), options.end(), [&] (const std::string& o) {
                const auto it = library.byCategory.find (o);
                return it != library.byCategory.end() && ! it->second.empty();
            });
            for (const auto& o : options)
                if (auto s = pickSample (library, o, rng); ! s.empty())
                {
                    c.samplePath = s;
                    c.category = o;
                    break;
                }
        }
        c.notes = generateSteps (sources, genre->id, c.category, rng, &c.bars);
        plan.bars = std::max (plan.bars, c.bars);
        plan.channels.push_back (std::move (c));
    }
    setBlanks (*genre, plan.channels, rng);
    return plan;
}

// ---------------------------------------------------------------------------------------------------------------------
std::string nextPatternName (const Song& song)
{
    // FLEngine 0x00dc4400: "Loop Starter #" + the first number no pattern is named with
    for (int n = 1;; ++n)
    {
        const auto name = "Loop Starter #" + std::to_string (n);
        if (std::none_of (song.patterns.begin(), song.patterns.end(), [&] (const Pattern& p) { return p.name == name; }))
            return name;
    }
}

namespace
{
std::string displayName (const std::string& category)
{
    auto s = category;
    if (! s.empty())
        s[0] = (char) std::toupper ((unsigned char) s[0]);
    return s;
}

int stepsInsert (Song& song)
{
    // FL names the Loop Starter steps' mixer track "LS Steps" (FLEngine 0x00dc1bd0)
    for (std::size_t i = 1; i < song.mixer.size(); ++i)
        if (song.mixer[i].name == "LS Steps")
            return (int) i;
    const auto t = song.claimFreeInsert();
    if (t > 0)
        song.mixer[(std::size_t) t].name = "LS Steps";
    return t;
}
} // namespace

int apply (Song& song, const Plan& plan)
{
    Pattern p;
    p.name = nextPatternName (song);
    p.loopStarterGenre = plan.genre;
    p.lengthTicks = (std::int64_t) std::max (1, plan.bars) * 16 * ticksPerStep;
    song.patterns.push_back (std::move (p));
    const auto patternIndex = (int) song.patterns.size() - 1;
    auto& pattern = song.patterns.back();
    const auto insert = stepsInsert (song);

    for (const auto& c : plan.channels)
    {
        ChannelDef def;
        def.name = displayName (c.category);
        if (! c.samplePath.empty())
        {
            def.instrument = InstrumentKind::sampler;
            def.samplePath = c.samplePath;
        }
        else
            def.instrument = builtinDrumFor (c.category);
        def.mixerTrack = insert;
        def.loopStarterPattern = patternIndex;
        def.loopStarterSlot = c.slot;
        def.loopStarterCategory = c.category;
        song.channels.push_back (std::move (def));
        const auto ch = (int) song.channels.size() - 1;
        for (auto n : c.notes)
        {
            n.channel = ch;
            pattern.notes.push_back (n);
        }
    }
    pattern.sortNotes();
    song.bpm = plan.bpm;
    song.swing = plan.swing;
    song.currentPattern = patternIndex;
    return patternIndex;
}

std::vector<ChannelDef> kitChannels (const Plan& plan)
{
    std::vector<ChannelDef> out;
    for (const auto& c : plan.channels)
    {
        ChannelDef def;
        def.name = displayName (c.category);
        if (! c.samplePath.empty())
        {
            def.instrument = InstrumentKind::sampler;
            def.samplePath = c.samplePath;
        }
        else
            def.instrument = builtinDrumFor (c.category);
        out.push_back (std::move (def));
    }
    return out;
}

Pattern kitPattern (const Plan& plan)
{
    Pattern p;
    p.lengthTicks = (std::int64_t) std::max (1, plan.bars) * 16 * ticksPerStep;
    for (int row = 0; row < (int) plan.channels.size(); ++row)
        for (auto n : plan.channels[(std::size_t) row].notes)
        {
            n.channel = row;
            p.notes.push_back (n);
        }
    p.sortNotes();
    return p;
}

std::vector<int> channelsOf (const Song& song, int pattern)
{
    std::vector<int> out;
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return out;
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (song.channels[(std::size_t) i].loopStarterPattern == pattern)
            out.push_back (i);
    return out;
}

void setChannelNotes (Song& song, int pattern, int channel, const std::vector<Note>& notes, int bars)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return;
    auto& p = song.patterns[(std::size_t) pattern];
    p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [channel] (const Note& n) { return n.channel == channel; }), p.notes.end());
    for (auto n : notes)
    {
        n.channel = channel;
        p.notes.push_back (n);
    }
    p.sortNotes();
    const auto ticks = (std::int64_t) std::max (1, bars) * 16 * ticksPerStep;
    if (p.lengthTicks > 0 && p.lengthTicks < ticks)
        p.lengthTicks = ticks;
}

int sendToPlaylist (Song& song, int pattern, std::int64_t at)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return -1;
    const auto length = std::max<std::int64_t> (ticksPerStep, song.patterns[(std::size_t) pattern].effectiveLength (song.meter()));
    at = std::max<std::int64_t> (0, at);
    // FL: "There aren't enough empty playlist tracks available." — the first track free over the clip's span
    constexpr int maxTracks = 500;
    int track = -1;
    for (int t = 0; t < maxTracks && track < 0; ++t)
    {
        const bool used = std::any_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) {
            return c.track == t && c.start < at + length && c.start + c.length > at;
        });
        if (! used)
            track = t;
    }
    if (track < 0)
        return -1;
    Clip clip;
    clip.pattern = pattern;
    clip.track = track;
    clip.start = at;
    clip.length = length;
    song.playlist.push_back (clip);
    // the channels leave Loop Starter (FLEngine 0x00dbe3a0 for every channel of the pattern)
    for (auto& c : song.channels)
        if (c.loopStarterPattern == pattern)
            c.loopStarterPattern = -1;
    song.patterns[(std::size_t) pattern].loopStarterGenre.clear();
    return track;
}
} // namespace lynx::loopstarter
