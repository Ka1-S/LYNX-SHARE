#include "core/BlxImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

#include "core/BandImport.h"
#include "core/Effects.h"
#include "core/KeyFollow.h"
#include "core/MissingFiles.h"
#include "core/PlaylistOps.h"
#include "core/ProjectKey.h"
#include "core/SimpleTracks.h"
#include "core/SynthPresets.h"

namespace lynx
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------------
// A small JSON reader: the revision tree only (numbers as double, strings UTF-8 with \u escapes, depth-limited)
struct JsonField;
struct Json
{
    enum class Type : std::uint8_t { null, boolean, number, string, array, object } type = Type::null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Json> items;
    // (an object's members: a struct of its own, not std::pair — clang with libstdc++ 14 / 15 instantiates
    // std::pair<std::string, Json> while Json is still incomplete and fails; CI, rollup step 5)
    std::vector<JsonField> fields;

    const Json* get (const char* key) const noexcept; // (below JsonField)
    double num (const char* key, double fallback) const noexcept
    {
        const auto* v = get (key);
        if (v == nullptr)
            return fallback;
        if (v->type == Type::number && std::isfinite (v->n))
            return v->n;
        if (v->type == Type::string) // ("120" as a string: some exports write numbers so)
        {
            char* end = nullptr;
            const auto d = std::strtod (v->s.c_str(), &end);
            if (end != v->s.c_str() && std::isfinite (d))
                return d;
        }
        return fallback;
    }
    bool flag (const char* key, bool fallback = false) const noexcept
    {
        const auto* v = get (key);
        return v == nullptr ? fallback : v->type == Type::boolean ? v->b : v->type == Type::number ? v->n != 0.0 : fallback;
    }
    std::string str (const char* key) const
    {
        const auto* v = get (key);
        return v != nullptr && v->type == Type::string ? v->s : std::string();
    }
    const std::vector<Json>& list (const char* key) const noexcept; // (below JsonField)
};

struct JsonField
{
    std::string key;
    Json value;
};

const Json* Json::get (const char* key) const noexcept
{
    if (type != Type::object)
        return nullptr;
    for (const auto& [k, v] : fields)
        if (k == key)
            return &v;
    return nullptr;
}

const std::vector<Json>& Json::list (const char* key) const noexcept
{
    static const std::vector<Json> none;
    const auto* v = get (key);
    return v != nullptr && v->type == Type::array ? v->items : none;
}

class JsonReader
{
public:
    JsonReader (const std::uint8_t* d, std::size_t n) : start (d), p (d), end (d + n) {}

    bool read (Json& out, std::string& error)
    {
        if (end - p >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
            p += 3;
        if (! value (out, 0))
        {
            error = "not valid JSON (byte " + std::to_string (failAt) + ")";
            return false;
        }
        space();
        return true; // (trailing bytes are ignored)
    }

private:
    const std::uint8_t* const start;
    const std::uint8_t* p;
    const std::uint8_t* end;
    std::size_t failAt = 0;

    bool fail()
    {
        failAt = (std::size_t) (p - start);
        return false;
    }
    void space()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }
    bool literal (const char* word)
    {
        const auto n = std::strlen (word);
        if ((std::size_t) (end - p) < n || std::memcmp (p, word, n) != 0)
            return fail();
        p += n;
        return true;
    }
    static void utf8 (std::string& s, std::uint32_t c)
    {
        if (c < 0x80)
            s += (char) c;
        else if (c < 0x800)
        {
            s += (char) (0xC0 | (c >> 6));
            s += (char) (0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            s += (char) (0xE0 | (c >> 12));
            s += (char) (0x80 | ((c >> 6) & 0x3F));
            s += (char) (0x80 | (c & 0x3F));
        }
        else
        {
            s += (char) (0xF0 | (c >> 18));
            s += (char) (0x80 | ((c >> 12) & 0x3F));
            s += (char) (0x80 | ((c >> 6) & 0x3F));
            s += (char) (0x80 | (c & 0x3F));
        }
    }
    bool hex4 (std::uint32_t& c)
    {
        if (end - p < 4)
            return fail();
        c = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto h = *p++;
            c <<= 4;
            if (h >= '0' && h <= '9') c |= (std::uint32_t) (h - '0');
            else if (h >= 'a' && h <= 'f') c |= (std::uint32_t) (h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') c |= (std::uint32_t) (h - 'A' + 10);
            else return fail();
        }
        return true;
    }
    bool string (std::string& s)
    {
        ++p; // '"'
        while (p < end && *p != '"')
        {
            if (*p != '\\')
            {
                s += (char) *p++;
                continue;
            }
            if (++p >= end)
                return fail();
            const auto e = *p++;
            switch (e)
            {
                case '"': s += '"'; break;
                case '\\': s += '\\'; break;
                case '/': s += '/'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                case 'u':
                {
                    std::uint32_t c = 0;
                    if (! hex4 (c))
                        return false;
                    if (c >= 0xD800 && c < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
                    {
                        p += 2;
                        std::uint32_t low = 0;
                        if (! hex4 (low))
                            return false;
                        c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    }
                    utf8 (s, c);
                    break;
                }
                default: return fail();
            }
        }
        if (p >= end)
            return fail();
        ++p;
        return true;
    }
    bool value (Json& out, int depth)
    {
        space();
        if (p >= end || depth > 64)
            return fail();
        switch (*p)
        {
            case '{':
            {
                out.type = Json::Type::object;
                ++p;
                space();
                if (p < end && *p == '}')
                {
                    ++p;
                    return true;
                }
                for (;;)
                {
                    space();
                    if (p >= end || *p != '"')
                        return fail();
                    std::string key;
                    if (! string (key))
                        return false;
                    space();
                    if (p >= end || *p != ':')
                        return fail();
                    ++p;
                    Json v;
                    if (! value (v, depth + 1))
                        return false;
                    out.fields.push_back ({ std::move (key), std::move (v) });
                    space();
                    if (p < end && *p == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (p < end && *p == '}')
                    {
                        ++p;
                        return true;
                    }
                    return fail();
                }
            }
            case '[':
            {
                out.type = Json::Type::array;
                ++p;
                space();
                if (p < end && *p == ']')
                {
                    ++p;
                    return true;
                }
                for (;;)
                {
                    Json v;
                    if (! value (v, depth + 1))
                        return false;
                    out.items.push_back (std::move (v));
                    space();
                    if (p < end && *p == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (p < end && *p == ']')
                    {
                        ++p;
                        return true;
                    }
                    return fail();
                }
            }
            case '"': out.type = Json::Type::string; return string (out.s);
            case 't': out.type = Json::Type::boolean; out.b = true; return literal ("true");
            case 'f': out.type = Json::Type::boolean; out.b = false; return literal ("false");
            case 'n': out.type = Json::Type::null; return literal ("null");
            default:
            {
                const auto* from = p;
                while (p < end && (std::isdigit (*p) || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E'))
                    ++p;
                if (p == from)
                    return fail();
                const std::string text ((const char*) from, (std::size_t) (p - from));
                char* stop = nullptr;
                out.type = Json::Type::number;
                out.n = std::strtod (text.c_str(), &stop);
                return stop != text.c_str() ? true : fail();
            }
        }
    }
};

// The revision: the top object when it has "tracks", else the first object below it (breadth first, 4 levels) that
// has a "tracks" array ({ "revision": {...} }, { "data": {...} }, an array of revisions...)
const Json* findRevision (const Json& root)
{
    std::vector<const Json*> level { &root };
    for (int depth = 0; depth < 5 && ! level.empty(); ++depth)
    {
        std::vector<const Json*> next;
        for (const auto* j : level)
        {
            if (const auto* t = j->get ("tracks"); t != nullptr && t->type == Json::Type::array)
                return j;
            if (j->type == Json::Type::object)
                for (const auto& [k, v] : j->fields)
                    next.push_back (&v);
            else if (j->type == Json::Type::array)
                for (const auto& v : j->items)
                    next.push_back (&v);
        }
        level = std::move (next);
    }
    return nullptr;
}

std::vector<blx::AutoPoint> points (const Json* list)
{
    std::vector<blx::AutoPoint> out;
    if (list == nullptr || list->type != Json::Type::array)
        return out;
    for (const auto& p : list->items)
        if (p.type == Json::Type::object && p.get ("position") != nullptr)
            out.push_back ({ std::max (0.0, p.num ("position", 0.0)), p.num ("value", 0.0) });
    std::stable_sort (out.begin(), out.end(), [] (const blx::AutoPoint& a, const blx::AutoPoint& b) { return a.beat < b.beat; });
    return out;
}

std::string lowercase (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

std::string baseName (const std::string& path)
{
    auto s = path;
    if (const auto q = s.find_first_of ("?#"); q != std::string::npos) // (a URL's query)
        s.resize (q);
    const auto slash = s.find_last_of ("/\\");
    return slash == std::string::npos ? s : s.substr (slash + 1);
}

std::string stemOf (const std::string& path)
{
    auto s = baseName (path);
    if (const auto dot = s.find_last_of ('.'); dot != std::string::npos && dot > 0)
        s.resize (dot);
    return s;
}

std::string extOf (const std::string& path)
{
    const auto b = baseName (path);
    const auto dot = b.find_last_of ('.');
    return dot == std::string::npos ? std::string() : lowercase (b.substr (dot + 1));
}

bool audioExt (const std::string& e)
{
    for (const auto* x : { "wav", "wave", "aif", "aiff", "flac", "mp3", "ogg", "m4a", "aac", "caf", "opus", "wma" })
        if (e == x)
            return true;
    return false;
}

bool midiExt (const std::string& e) { return e == "mid" || e == "midi"; }

std::string joinPath (const std::string& folder, const std::string& rel)
{
    if (folder.empty())
        return rel;
    const auto last = folder.back();
    return last == '/' || last == '\\' ? folder + rel : folder + "/" + rel;
}

void note (FlpImportReport* report, std::map<std::string, int> FlpImportReport::*list, const std::string& what, int n = 1)
{
    if (report != nullptr && n > 0)
        (report->*list)[what] += n;
}

// BandLab's linear fader (1 = unity, 2 ≈ +6 dB) → LYNX's fader position (capped at its +5.6 dB)
float faderOf (double linear) noexcept
{
    return faderPosition ((float) std::clamp (linear, 0.0, (double) maxFaderGain));
}

float pan01 (double pan) noexcept { return (float) std::clamp ((pan + 1.0) * 0.5, 0.0, 1.0); }

// the laws of LYNX's basic effects (core/Effects.cpp), inverted
float logValue (double v, double lo, double hi) noexcept
{
    v = std::clamp (v, lo, hi);
    return (float) (std::log (v / lo) / std::log (hi / lo));
}
float eqGainValue (double db) noexcept { return (float) std::clamp (db / 36.0 + 0.5, 0.0, 1.0); }      // -18..+18 dB
float compThresholdValue (double db) noexcept { return (float) std::clamp ((db + 60.0) / 60.0, 0.0, 1.0); } // -60..0 dB
float compRatioValue (double r) noexcept { return (float) std::clamp ((r - 1.0) / 19.0, 0.0, 1.0); }    // 1..20

struct Mapped
{
    std::vector<EffectSlot> slots;
    bool partial = false;
};

// One BandLab effect → LYNX slots. Empty: no equivalent.
Mapped mapEffect (const blx::Effect& fx)
{
    Mapped m;
    const auto slug = lowercase (fx.slug);
    const auto param = [&fx] (const char* key, double fallback) {
        const auto it = fx.params.find (key);
        return it == fx.params.end() ? fallback : it->second;
    };
    const auto has = [&slug] (std::initializer_list<const char*> words) {
        return std::any_of (words.begin(), words.end(), [&slug] (const char* w) { return slug.find (w) != std::string::npos; });
    };
    auto slot = [&m, &fx] (EffectKind kind) -> EffectSlot& {
        m.slots.push_back (makeEffectSlot (kind));
        m.slots.back().enabled = ! fx.bypass;
        return m.slots.back();
    };

    if (slug == "compressor")
    {
        auto& s = slot (EffectKind::compressor);
        s.params[0] = compThresholdValue (param ("threshold", -24.0));
        s.params[1] = compRatioValue (param ("ratio", 4.0));
        s.params[2] = logValue (param ("attack", 0.01) * 1000.0, 0.1, 100.0);  // seconds → ms
        s.params[3] = logValue (param ("release", 0.1) * 1000.0, 10.0, 1000.0);
        m.partial = fx.params.count ("knee") != 0; // (LYNX's compressor has its own knee)
        return m;
    }
    if (slug == "threebandeq")
    {
        auto& s = slot (EffectKind::eq);
        s.params[0] = eqGainValue (param ("lowGain", 0.0));
        s.params[1] = eqGainValue (param ("midGain", 0.0));
        s.params[2] = logValue (param ("midFreq", 1000.0), 200.0, 5000.0);
        s.params[3] = eqGainValue (param ("highGain", 0.0));
        return m;
    }
    if (slug == "bossge7" || has ({ "graphiceq", "graphic" }))
    {
        // seven peaking bands at 100 Hz … 6.4 kHz (BandLab's key names), plus the level trim
        auto& s = slot (EffectKind::parametricEq);
        constexpr double centres[] = { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };
        for (int b = 0; b < peq::bands; ++b)
        {
            const auto key = "gainAt" + std::to_string ((int) centres[b]) + "Hz";
            s.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (peq::BandType::peak);
            s.params[(std::size_t) peq::param (b, 1)] = peq::hzValue (centres[b]);
            s.params[(std::size_t) peq::param (b, 2)] = peq::gainValue (param (key.c_str(), 0.0));
            s.params[(std::size_t) peq::param (b, 3)] = peq::qValue (1.4); // (about one octave per band)
        }
        s.params[peq::outputParam] = peq::gainValue (param ("level", 0.0));
        m.partial = true; // (the pedal's band shapes are approximated)
        return m;
    }
    if (has ({ "reverb", "hall", "plate", "room" }))
    {
        auto& s = slot (EffectKind::reverb);
        if (const auto it = fx.params.find ("dryWetMix"); it != fx.params.end())
            s.params[3] = (float) std::clamp (it->second, 0.0, 1.0);
        if (const auto it = fx.params.find ("roomSize"); it != fx.params.end())
            s.params[0] = (float) std::clamp (it->second / 5.0, 0.0, 1.0);
        m.partial = true;
        return m;
    }
    if (has ({ "delay", "echo" }))
    {
        slot (EffectKind::delay);
        m.partial = true;
        return m;
    }
    if (has ({ "chorus" }))
    {
        slot (EffectKind::chorus);
        m.partial = true;
        return m;
    }
    if (has ({ "distortion", "overdrive", "fuzz", "drive", "amp", "tube", "crusher" }))
    {
        slot (EffectKind::distortion);
        m.partial = true;
        return m;
    }
    if (has ({ "limiter", "maximizer" }))
    {
        slot (EffectKind::limiter);
        m.partial = true;
        return m;
    }
    return m;
}
} // namespace

namespace blx
{
std::optional<Project> parse (const std::vector<std::uint8_t>& bytes, std::string* error)
{
    auto failed = [error] (const std::string& why) -> std::optional<Project> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] == 'K')
        return failed ("a zipped BandLab project (unpack it first)");
    Json root;
    std::string why;
    if (! JsonReader (bytes.data(), bytes.size()).read (root, why))
        return failed ("not a BandLab project: " + why);
    const auto* rev = findRevision (root);
    if (rev == nullptr)
        return failed ("not a BandLab project: no tracks in the file");

    Project p;
    if (const auto* song = rev->get ("song"); song != nullptr)
        p.title = song->str ("name");
    if (p.title.empty())
        p.title = rev->str ("title");
    p.key = rev->str ("key");
    p.masterVolume = rev->num ("volume", 1.0);
    if (const auto* metro = rev->get ("metronome"); metro != nullptr)
    {
        p.bpm = metro->num ("bpm", 120.0);
        if (const auto* sig = metro->get ("signature"); sig != nullptr)
        {
            p.numerator = (int) std::lround (sig->num ("notesCount", 4.0));
            p.denominator = (int) std::lround (sig->num ("noteValue", 4.0));
        }
    }
    if (const auto* m = rev->get ("mastering"); m != nullptr && m->type == Json::Type::object && ! m->flag ("bypass"))
        p.mastering = m->str ("preset");
    p.auxChannels = (int) rev->list ("auxChannels").size();
    if (const auto* lyrics = rev->get ("lyrics"); lyrics != nullptr && lyrics->type == Json::Type::string && ! lyrics->s.empty())
        p.hasLyrics = true;

    for (const auto& s : rev->list ("samples"))
    {
        Sample sample;
        sample.id = s.str ("id");
        sample.name = s.str ("name");
        sample.file = s.str ("file");
        sample.midi = s.flag ("isMidi") || midiExt (extOf (sample.file));
        sample.duration = std::max (0.0, s.num ("duration", 0.0));
        if (! sample.id.empty())
            p.samples.push_back (std::move (sample));
    }

    int order = 0;
    for (const auto& t : rev->list ("tracks"))
    {
        if (t.type != Json::Type::object)
            continue;
        Track track;
        track.id = t.str ("id");
        track.name = t.str ("name");
        track.type = t.str ("type");
        track.soundbank = t.str ("soundbank");
        track.preset = t.str ("preset");
        track.colorName = t.str ("colorName");
        track.order = (int) std::lround (t.num ("order", (double) order));
        ++order;
        track.volume = std::max (0.0, t.num ("volume", 1.0));
        track.pan = std::clamp (t.num ("pan", 0.0), -1.0, 1.0);
        track.muted = t.flag ("isMuted");
        track.solo = t.flag ("isSolo");
        track.frozen = t.flag ("isFrozen");
        track.hasPatterns = ! t.list ("patterns").empty();
        if (const auto* kit = t.get ("samplerKit"); kit != nullptr && kit->type != Json::Type::null)
            track.hasSamplerKit = true;
        if (const auto* a = t.get ("automation"); a != nullptr)
        {
            track.volumeAutomation = points (a->get ("volume"));
            track.panAutomation = points (a->get ("pan"));
        }
        for (const auto& send : t.list ("auxSends"))
            track.reverbSend = std::max (track.reverbSend, std::clamp (send.num ("sendLevel", send.num ("SendLevel", 0.0)), 0.0, 1.0));
        for (const auto& e : t.list ("effects"))
        {
            Effect fx;
            fx.slug = e.str ("slug");
            fx.bypass = e.flag ("bypass");
            if (const auto* params = e.get ("params"); params != nullptr)
                for (const auto& [k, v] : params->fields)
                {
                    if (v.type == Json::Type::number)
                        fx.params[k] = v.n;
                    else if (v.type == Json::Type::boolean)
                        fx.params[k] = v.b ? 1.0 : 0.0;
                    else if (v.type == Json::Type::string)
                        fx.text[k] = v.s;
                }
            if (const auto* a = e.get ("automation"); a != nullptr && a->type == Json::Type::object)
                for (const auto& [k, v] : a->fields)
                    fx.automated = fx.automated || (v.type == Json::Type::array && ! v.items.empty());
            track.hasEffectAutomation = track.hasEffectAutomation || fx.automated;
            if (! fx.slug.empty())
                track.effects.push_back (std::move (fx));
        }
        for (const auto& r : t.list ("regions"))
        {
            Region region;
            region.id = r.str ("id");
            region.name = r.str ("name");
            region.sampleId = r.str ("sampleId");
            region.start = std::max (0.0, r.num ("startPosition", 0.0));
            region.end = r.num ("endPosition", region.start);
            region.offset = std::max (0.0, r.num ("sampleOffset", 0.0));
            region.loopLength = std::max (0.0, r.num ("loopLength", 0.0));
            region.playbackRate = r.num ("playbackRate", 1.0);
            if (! (region.playbackRate > 0.01 && region.playbackRate < 100.0))
                region.playbackRate = 1.0;
            region.pitchShift = std::clamp (r.num ("pitchShift", 0.0), -48.0, 48.0);
            region.gain = std::clamp (r.num ("gain", 1.0), 0.0, 4.0);
            region.fadeIn = std::max (0.0, r.num ("fadeIn", 0.0));
            region.fadeOut = std::max (0.0, r.num ("fadeOut", 0.0));
            if (region.end > region.start)
                track.regions.push_back (std::move (region));
        }
        std::stable_sort (track.regions.begin(), track.regions.end(), [] (const Region& a, const Region& b) { return a.start < b.start; });
        p.tracks.push_back (std::move (track));
    }
    std::stable_sort (p.tracks.begin(), p.tracks.end(), [] (const Track& a, const Track& b) { return a.order < b.order; });
    if (! (p.bpm >= minTempo && p.bpm <= maxTempo))
        p.bpm = 120.0;
    return p;
}

std::optional<std::vector<MidiNote>> readMidi (const std::vector<std::uint8_t>& b)
{
    const auto n = b.size();
    auto be32 = [&b] (std::size_t at) { return ((std::uint32_t) b[at] << 24) | ((std::uint32_t) b[at + 1] << 16) | ((std::uint32_t) b[at + 2] << 8) | b[at + 3]; };
    // (RIFF-wrapped MIDI, .rmi: the "MThd" further in)
    std::size_t at = 0;
    if (n >= 20 && std::memcmp (b.data(), "RIFF", 4) == 0)
        for (std::size_t i = 12; i + 4 <= n && i < 64; ++i)
            if (std::memcmp (b.data() + i, "MThd", 4) == 0)
            {
                at = i;
                break;
            }
    if (n < at + 14 || std::memcmp (b.data() + at, "MThd", 4) != 0)
        return std::nullopt;
    const auto headerLength = be32 (at + 4);
    const auto tracks = (int) ((b[at + 10] << 8) | b[at + 11]);
    const auto division = (int) ((b[at + 12] << 8) | b[at + 13]);
    if (division <= 0 || (division & 0x8000) != 0) // (SMPTE time: not a beat-based file)
        return std::nullopt;
    at += 8 + headerLength;

    std::vector<MidiNote> notes;
    for (int t = 0; t < tracks && at + 8 <= n; ++t)
    {
        if (std::memcmp (b.data() + at, "MTrk", 4) != 0)
            break;
        const auto length = (std::size_t) be32 (at + 4);
        auto p = at + 8;
        const auto stop = std::min (n, p + length);
        at = p + length;
        std::uint64_t tick = 0;
        std::uint8_t running = 0;
        // per channel and key: the open note's start tick and velocity
        std::map<int, std::pair<std::uint64_t, int>> open;
        auto varLen = [&b, &p, stop] {
            std::uint32_t v = 0;
            for (int i = 0; i < 4 && p < stop; ++i)
            {
                const auto c = b[p++];
                v = (v << 7) | (c & 0x7F);
                if ((c & 0x80) == 0)
                    break;
            }
            return v;
        };
        auto close = [&] (int channel, int key, std::uint64_t at) {
            const auto it = open.find (channel * 128 + key);
            if (it == open.end())
                return;
            MidiNote m;
            m.beat = (double) it->second.first / division;
            m.length = std::max (1.0 / 64.0, (double) (at - it->second.first) / division);
            m.key = key;
            m.velocity = it->second.second;
            notes.push_back (m);
            open.erase (it);
        };
        while (p < stop)
        {
            tick += varLen();
            if (p >= stop)
                break;
            auto status = b[p];
            if (status & 0x80)
                ++p;
            else
                status = running;
            if (status == 0xFF)
            {
                if (p >= stop)
                    break;
                ++p; // type
                p += varLen();
                continue;
            }
            if (status == 0xF0 || status == 0xF7)
            {
                p += varLen();
                continue;
            }
            if ((status & 0x80) == 0)
                break; // (no running status to use: a damaged track)
            running = status;
            const auto kind = status & 0xF0;
            const auto channel = status & 0x0F;
            const auto data = kind == 0xC0 || kind == 0xD0 ? 1 : 2;
            if (p + (std::size_t) data > stop)
                break;
            const int d1 = b[p], d2 = data > 1 ? b[p + 1] : 0;
            p += (std::size_t) data;
            if (kind == 0x90 && d2 > 0)
            {
                close (channel, d1, tick); // (a key struck again ends the one before)
                open[channel * 128 + d1] = { tick, d2 };
            }
            else if (kind == 0x80 || (kind == 0x90 && d2 == 0))
                close (channel, d1, tick);
        }
        for (auto it = open.begin(); it != open.end();)
        {
            const auto key = it->first;
            ++it;
            close (key / 128, key % 128, tick); // (notes left open end with their track)
        }
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const MidiNote& a, const MidiNote& b) { return a.beat < b.beat; });
    return notes;
}

std::pair<int, bool> parseKey (const std::string& text) noexcept
{
    std::string k;
    for (const auto c : text)
        if (! std::isspace ((unsigned char) c) && c != '_' && c != '-')
            k += c;
    if (k.empty())
        return { -1, false };
    static const int roots[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = (char) std::toupper ((unsigned char) k[0]);
    if (letter < 'A' || letter > 'G')
        return { -1, false };
    int root = roots[letter - 'A'];
    std::size_t i = 1;
    if (i < k.size() && (k[i] == '#' || k.compare (i, 3, "\xE2\x99\xAF") == 0))
    {
        root += 1;
        i += k[i] == '#' ? 1 : 3;
    }
    else if (i < k.size() && (k[i] == 'b' || k.compare (i, 3, "\xE2\x99\xAD") == 0))
    {
        root += 11;
        i += k[i] == 'b' ? 1 : 3;
    }
    const auto rest = lowercase (k.substr (i));
    const auto minor = rest == "m" || rest.rfind ("min", 0) == 0 || rest == "minor";
    return { root % 12, minor };
}

std::uint32_t colourOf (const std::string& name) noexcept
{
    const auto n = lowercase (name);
    if (n == "red") return 0xFFE5484D;
    if (n == "blue") return 0xFF3B82F6;
    if (n == "violet") return 0xFF8B5CF6;
    if (n == "pink") return 0xFFEC4899;
    if (n == "green") return 0xFF22C55E;
    if (n == "yellow") return 0xFFEAB308;
    return 0;
}
} // namespace blx

std::string blxMissingPath (const BlxImportOptions& options, const std::string& sampleId, const std::string& regionName, bool midi)
{
    const auto name = ! sampleId.empty() ? sampleId : ! regionName.empty() ? regionName : std::string ("region");
    return joinPath (options.folder, name + (midi ? ".mid" : ".wav"));
}

std::string blxSampleFile (const blx::Sample* sample, const std::string& sampleId, const std::string& regionName, bool midi,
                           const BlxImportOptions& options)
{
    // the file the user located for it in the Missing files window (the path the report gave)
    if (const auto r = options.common.relocations.find (blxMissingPath (options, sampleId, regionName, midi));
        r != options.common.relocations.end() && ! r->second.empty())
        return r->second;
    const auto fits = [midi] (const std::string& file) {
        const auto e = extOf (file);
        return midi ? midiExt (e) : audioExt (e);
    };
    const auto found = [&options] (const std::string& path) -> std::string {
        if (const auto r = options.common.relocations.find (path); r != options.common.relocations.end() && ! r->second.empty())
            return r->second;
        return options.common.fileExists && options.common.fileExists (path) ? path : std::string();
    };
    std::vector<std::string> wanted; // file stems, in the order they are tried (lowercase)
    if (! sampleId.empty())
        wanted.push_back (lowercase (sampleId));
    if (sample != nullptr && ! sample->file.empty())
        wanted.push_back (lowercase (stemOf (sample->file)));
    if (sample != nullptr && ! sample->name.empty())
        wanted.push_back (lowercase (sample->name));
    if (! regionName.empty())
        wanted.push_back (lowercase (regionName));

    for (const auto& w : wanted)
        for (const auto& rel : options.files)
            if (fits (rel) && lowercase (stemOf (rel)) == w)
                if (auto path = found (joinPath (options.folder, rel)); ! path.empty())
                    return path;
    // the sample id inside a longer name ("<id>_regions.wav", "Audio/<id>.wav")
    if (! sampleId.empty())
        for (const auto& rel : options.files)
            if (fits (rel) && lowercase (rel).find (lowercase (sampleId)) != std::string::npos)
                if (auto path = found (joinPath (options.folder, rel)); ! path.empty())
                    return path;
    // without a listing: the direct names next to the .blx and in its Audio / MIDI / Samples folders
    // (two arrays of their own, not a conditional between two braced initializer lists: g++ 13 ends their backing
    // arrays with the conditional and the loop read freed stack — CI's core build crashed here, rollup step 5)
    static constexpr const char* midiExts[] { ".mid", ".midi" };
    static constexpr const char* audioExts[] { ".wav", ".flac", ".mp3", ".m4a", ".ogg", ".aif" };
    const auto exts = midi ? std::span<const char* const> (midiExts) : std::span<const char* const> (audioExts);
    if (options.files.empty())
        for (const auto& w : wanted)
            for (const auto* sub : { "", "Audio/", "MIDI/", "Samples/" })
                for (const auto* ext : exts)
                    if (auto path = found (joinPath (options.folder, std::string (sub) + w + ext)); ! path.empty())
                        return path;
    if (sample != nullptr && ! sample->file.empty() && sample->file.find ("://") == std::string::npos)
        if (auto path = found (sample->file); ! path.empty() && fits (path)) // (an absolute path written by the exporter)
            return path;
    return {};
}

Song importBlx (const blx::Project& project, const BlxImportOptions& options)
{
    auto* report = options.common.report;
    Song song;
    song.channels.clear();
    song.patterns.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (0);
    song.mode = PlayMode::song;
    song.info.title = project.title;
    song.bpm = std::clamp (project.bpm, minTempo, maxTempo);
    song.beatsPerBar = std::clamp (project.numerator, 1, 32);
    const auto unit = project.denominator;
    song.beatUnit = unit == 2 || unit == 4 || unit == 8 || unit == 16 ? unit : 4;
    if (const auto [root, minor] = blx::parseKey (project.key); root >= 0)
        projectkey::set (song, keyfollow::projectKeyFor ({ root, minor }));
    if (! song.mixer.empty())
    {
        song.mixer[0].volume = faderOf (project.masterVolume);
        if (project.masterVolume > maxFaderGain)
            note (report, &FlpImportReport::partialEffects, "BandLab: master volume above +5.6 dB (capped)");
    }
    if (! project.mastering.empty())
        note (report, &FlpImportReport::droppedEffects, "BandLab: Mastering preset \"" + project.mastering + "\" (use the Master's own effects)");
    if (project.hasLyrics)
        note (report, &FlpImportReport::notReproduced, "BandLab: lyrics / notes");

    // seconds and quarter notes → LYNX ticks (BandLab's tempo is in quarter notes, as LYNX's)
    const auto ticksPerSecond = song.bpm / 60.0 * ppq;
    const auto toTicks = [ticksPerSecond] (double seconds) { return (std::int64_t) std::llround (std::max (0.0, seconds) * ticksPerSecond); };
    std::map<std::string, const blx::Sample*> samples;
    for (const auto& s : project.samples)
        samples[s.id] = &s;

    std::map<std::string, std::optional<std::vector<blx::MidiNote>>> midiCache;
    std::map<std::string, int> missingSeen;
    auto missing = [&] (const std::string& what, const std::string& user) {
        if (report == nullptr)
            return;
        if (missingSeen[what]++ == 0)
            ++report->missingSamples;
        addMissingFile (report->missingFiles, what, user);
    };

    int reverbSends = 0;
    for (const auto& track : project.tracks)
    {
        // what the track holds: a region whose sample is MIDI (or a .mid found for it) makes it a MIDI track
        const auto type = lowercase (track.type);
        auto midiRegions = 0, audioRegions = 0;
        for (const auto& r : track.regions)
        {
            const auto it = samples.find (r.sampleId);
            (it != samples.end() && it->second->midi ? midiRegions : audioRegions) += 1;
        }
        const auto drums = type.find ("drum") != std::string::npos || bandDrumKitName (track.soundbank) || bandDrumKitName (track.preset);
        const auto sampler = type.find ("kit") != std::string::npos || type.find ("sampler") != std::string::npos || track.hasSamplerKit;
        const auto audio = ! drums && ! sampler && (type == "voice" || type == "audio" || type.find ("guitar") != std::string::npos
                                                    || type.find ("bass") != std::string::npos || (midiRegions == 0 && audioRegions > 0)
                                                    || (type.empty() && track.regions.empty()));
        const auto label = track.name.empty() ? std::string ("Track") : track.name;
        const auto sound = ! track.soundbank.empty() ? track.soundbank : ! track.preset.empty() ? track.preset : track.name;

        simpletracks::Request q;
        q.name = track.name;
        if (audio)
            q.kind = simpletracks::Kind::audio;
        else if (drums)
        {
            q.kind = simpletracks::Kind::drums;
            for (const auto kind : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::clap, InstrumentKind::hat })
            {
                ChannelDef c;
                c.instrument = kind;
                c.name = kind == InstrumentKind::kick ? "Kick" : kind == InstrumentKind::snare ? "Snare" : kind == InstrumentKind::clap ? "Clap" : "Hat";
                q.kit.push_back (std::move (c));
            }
            note (report, &FlpImportReport::approximated, "BandLab drum track \"" + label + "\" (LYNX's built-in kick, snare, clap and hat)");
        }
        else
        {
            q.kind = simpletracks::Kind::instrument;
            ChannelDef synth;
            synth.instrument = InstrumentKind::synth;
            const auto preset = bandPresetFor (sound);
            for (const auto& [n, s] : synthPresets())
                if (n == preset)
                    synth.synth = s;
            q.channel = std::move (synth);
            note (report, &FlpImportReport::approximated, std::string (sampler ? "BandLab sampler" : "BandLab instrument") + " \"" + label
                                                              + "\" (LYNX's built-in synth, " + preset + " preset)");
        }
        const auto made = simpletracks::addTrack (song, q);
        if (! made.ok())
        {
            note (report, &FlpImportReport::notReproduced, "BandLab: tracks past LYNX's track or insert count");
            continue;
        }
        if (drums) // (a Drums track starts with an empty one-bar pattern clip: the regions take its place)
            song.playlist.erase (std::remove_if (song.playlist.begin(), song.playlist.end(),
                                                 [&made] (const Clip& c) { return c.track == made.track && c.kind == ClipKind::pattern; }),
                                 song.playlist.end());
        auto reuse = drums ? made.pattern : -1;
        if (const auto c = blx::colourOf (track.colorName); c != 0)
            simpletracks::recolour (song, made.track, c);
        const auto colour = song.playlistTracks[(std::size_t) made.track].colour != 0 ? song.playlistTracks[(std::size_t) made.track].colour
                                                                                       : simpletracks::defaultColour (q.kind);

        // volume, pan, mute, solo
        if (made.insert > 0 && made.insert < (int) song.mixer.size())
        {
            auto& m = song.mixer[(std::size_t) made.insert];
            m.volume = faderOf (track.volume);
            m.pan = (float) track.pan;
            if (track.volume > maxFaderGain * 1.0001)
                note (report, &FlpImportReport::partialEffects, "BandLab: track volume above +5.6 dB (capped)");
        }
        if (track.muted)
            song.playlistTracks[(std::size_t) made.track].muted = true;
        if (track.solo)
            note (report, &FlpImportReport::notReproduced, "BandLab: track solo (all tracks play)");
        if (track.frozen)
            note (report, &FlpImportReport::converted, "BandLab: frozen track (imported unfrozen)");

        // effects onto the track's insert, in order
        if (made.insert > 0 && made.insert < (int) song.mixer.size())
            for (const auto& fx : track.effects)
            {
                const auto mapped = mapEffect (fx);
                if (mapped.slots.empty())
                {
                    note (report, &FlpImportReport::droppedEffects, "BandLab effect \"" + fx.slug + "\"");
                    continue;
                }
                auto& effects = song.mixer[(std::size_t) made.insert].effects;
                for (const auto& s : mapped.slots)
                    if ((int) effects.size() < maxEffectSlots)
                        effects.push_back (s);
                note (report, mapped.partial ? &FlpImportReport::partialEffects : &FlpImportReport::converted, "BandLab effect \"" + fx.slug + "\"");
                if (fx.automated)
                    note (report, &FlpImportReport::notReproduced, "BandLab: effect automation");
            }
        if (track.reverbSend > 0.0 && made.insert > 0)
        {
            if (simpletracks::ensureSharedReverb (song) >= 0 && simpletracks::setReverbSend (song, made.insert, (float) track.reverbSend))
                ++reverbSends;
        }

        // volume and pan automation: lanes under the track (BandLab's positions are quarter notes, values linear / -1..1)
        auto lane = [&] (const std::vector<blx::AutoPoint>& points, AutomationTargetKind kind, const char* name) {
            if (points.empty() || made.insert <= 0)
                return;
            const auto first = kind == AutomationTargetKind::mixerVolume ? faderOf (points.front().value) : pan01 (points.front().value);
            const auto a = autolanes::addLane (song, made.track, { kind, made.insert, 0, 0 }, label + " - " + name, first);
            if (a < 0 || a >= (int) song.automations.size())
                return;
            auto& out = song.automations[(std::size_t) a].points;
            out.clear();
            for (const auto& p : points)
            {
                AutomationPoint ap;
                ap.tick = (std::int64_t) std::llround (p.beat * ppq);
                ap.value = kind == AutomationTargetKind::mixerVolume ? faderOf (p.value) : pan01 (p.value);
                if (! out.empty() && out.back().tick == ap.tick)
                    out.back() = ap; // (one value per tick: the later wins)
                else
                    out.push_back (ap);
            }
            note (report, &FlpImportReport::converted, std::string ("BandLab: ") + name + " automation (as an automation lane)");
        };
        lane (track.volumeAutomation, AutomationTargetKind::mixerVolume, "Volume");
        lane (track.panAutomation, AutomationTargetKind::mixerPan, "Pan");

        // regions
        std::map<std::string, int> audioChannels; // path + rate → channel
        for (const auto& region : track.regions)
        {
            const auto sit = samples.find (region.sampleId);
            const auto* sample = sit == samples.end() ? nullptr : sit->second;
            const auto midi = sample != nullptr ? sample->midi : ! audio;
            const auto shownName = ! region.name.empty() ? region.name : sample != nullptr && ! sample->name.empty() ? sample->name : label;
            const auto start = toTicks (region.start);
            const auto end = std::max (start + 1, toTicks (region.end));
            const auto pass = region.loopLength > 0.0 ? std::max<std::int64_t> (1, toTicks (region.loopLength)) : end - start;

            if (! midi)
            {
                if (q.kind != simpletracks::Kind::audio)
                {
                    note (report, &FlpImportReport::notReproduced, "BandLab: an audio region on a MIDI track");
                    continue;
                }
                const auto path = blxSampleFile (sample, region.sampleId, region.name, false, options);
                if (path.empty())
                {
                    missing (blxMissingPath (options, region.sampleId, region.name, false), "Audio clip: " + label + " / " + shownName);
                    continue;
                }
                const auto rate = region.playbackRate;
                const auto key = path + "|" + std::to_string (rate);
                auto channel = audioChannels.find (key);
                if (channel == audioChannels.end())
                {
                    ChannelDef c;
                    c.name = stemOf (path);
                    if (c.name == region.sampleId && ! shownName.empty())
                        c.name = shownName;
                    c.instrument = InstrumentKind::audioClip;
                    c.samplePath = path;
                    c.mixerTrack = made.insert;
                    c.colour = colour;
                    if (std::abs (rate - 1.0) > 1.0e-4)
                    {
                        // BandLab's playback rate: the file plays `rate` times faster; LYNX keeps its pitch and gives it
                        // the length in beats it then lasts at the song's tempo
                        auto seconds = sample != nullptr ? sample->duration : 0.0;
                        if (seconds <= 0.0 && options.common.sampleSeconds)
                            seconds = options.common.sampleSeconds (path);
                        if (seconds > 0.0)
                        {
                            c.sampler.stretchBeats = (float) (seconds / rate * song.bpm / 60.0);
                            c.sampler.keepPitch = true;
                            note (report, &FlpImportReport::converted, "BandLab: region playback rate (time stretch)");
                        }
                        else
                            note (report, &FlpImportReport::notReproduced, "BandLab: region playback rate (the sample's length is unknown)");
                    }
                    song.channels.push_back (std::move (c));
                    channel = audioChannels.emplace (key, (int) song.channels.size() - 1).first;
                }
                Clip clip;
                clip.kind = ClipKind::audio;
                clip.channel = channel->second;
                clip.track = made.track;
                const auto offset = region.offset / region.playbackRate * ticksPerSecond;
                clip.offset = (std::int64_t) std::floor (offset);
                clip.offsetFraction = (float) (offset - (double) clip.offset);
                clip.gain = (float) region.gain;
                clip.pitch = (float) std::clamp (region.pitchShift, -12.0, 12.0);
                if (std::abs (region.pitchShift) > 12.0)
                    note (report, &FlpImportReport::partialEffects, "BandLab: region pitch beyond ±12 semitones (capped)");
                clip.fadeIn = toTicks (region.fadeIn);
                clip.fadeOut = toTicks (region.fadeOut);
                // a looped region: one clip per pass, the last one cut where the region ends (core/RegionLoop.h's runs)
                for (auto at = start; at < end; at += pass)
                {
                    auto piece = clip;
                    piece.start = at;
                    piece.length = std::min (pass, end - at);
                    piece.fadeIn = at == start ? clip.fadeIn : 0;
                    piece.fadeOut = at + pass >= end ? std::min (clip.fadeOut, piece.length) : 0;
                    piece.fadeIn = std::min (piece.fadeIn, piece.length);
                    song.playlist.push_back (piece);
                }
                note (report, &FlpImportReport::converted, region.loopLength > 0.0 ? "BandLab: looped audio region" : "BandLab: audio region");
                continue;
            }

            // a MIDI region: the notes of its MIDI sample
            if (q.kind == simpletracks::Kind::audio)
            {
                note (report, &FlpImportReport::notReproduced, "BandLab: a MIDI region on an audio track");
                continue;
            }
            const auto path = blxSampleFile (sample, region.sampleId, region.name, true, options);
            if (path.empty())
            {
                missing (blxMissingPath (options, region.sampleId, region.name, true), "MIDI region: " + label + " / " + shownName);
                continue;
            }
            auto& notes = midiCache[path];
            if (! notes.has_value() && options.common.readFile)
                notes = blx::readMidi (options.common.readFile (path));
            if (! notes.has_value())
            {
                note (report, &FlpImportReport::notReproduced, "BandLab: unreadable MIDI file \"" + baseName (path) + "\"");
                continue;
            }
            Pattern p;
            p.name = shownName;
            p.colour = colour;
            p.lengthTicks = pass;
            const auto offsetBeats = region.offset * song.bpm / 60.0;
            for (const auto& n : *notes)
            {
                const auto beat = n.beat - offsetBeats;
                const auto at = (std::int64_t) std::llround (beat * ppq);
                if (at < 0 || at >= p.lengthTicks)
                    continue; // (outside the region's pass: BandLab doesn't play it)
                Note out;
                out.start = at;
                out.length = std::clamp<std::int64_t> ((std::int64_t) std::llround (n.length * ppq), 1, std::max<std::int64_t> (1, p.lengthTicks - at));
                out.key = std::clamp (n.key, 0, 127);
                out.velocity = std::clamp ((float) n.velocity / 127.0f, 0.0f, 1.0f);
                if (drums)
                {
                    const auto kind = bandDrumFor (n.key);
                    const auto slot = kind == InstrumentKind::kick ? 0 : kind == InstrumentKind::snare ? 1 : kind == InstrumentKind::clap ? 2 : 3;
                    out.channel = slot < (int) made.channels.size() ? made.channels[(std::size_t) slot] : made.channels.front();
                    out.key = 60;
                }
                else
                    out.channel = made.channels.empty() ? 0 : made.channels.front();
                p.notes.push_back (out);
            }
            p.sortNotes();
            int patternIndex;
            if (reuse >= 0 && reuse < (int) song.patterns.size())
            {
                song.patterns[(std::size_t) reuse] = std::move (p);
                patternIndex = reuse;
                reuse = -1;
            }
            else
            {
                song.patterns.push_back (std::move (p));
                patternIndex = (int) song.patterns.size() - 1;
            }
            for (auto at = start; at < end; at += pass)
            {
                Clip clip;
                clip.kind = ClipKind::pattern;
                clip.pattern = patternIndex;
                clip.track = made.track;
                clip.start = at;
                clip.length = std::min (pass, end - at);
                song.playlist.push_back (clip);
            }
            note (report, &FlpImportReport::converted, region.loopLength > 0.0 ? "BandLab: looped MIDI region" : "BandLab: MIDI region");
            if (std::abs (region.playbackRate - 1.0) > 1.0e-4 || std::abs (region.pitchShift) > 1.0e-4)
                note (report, &FlpImportReport::notReproduced, "BandLab: MIDI region speed / pitch");
        }
        if (track.hasPatterns)
            note (report, &FlpImportReport::notReproduced, "BandLab: Drum Machine patterns (their regions' MIDI is imported)");
        if (track.hasSamplerKit)
            note (report, &FlpImportReport::notReproduced, "BandLab: sampler kit sounds (not stored in the project)");
    }
    if (reverbSends > 0)
        note (report, &FlpImportReport::converted, "BandLab: shared reverb send (the Simple layout's Reverb)", reverbSends);
    if (project.auxChannels > 1)
        note (report, &FlpImportReport::notReproduced, "BandLab: aux channels after the first", project.auxChannels - 1);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    if (song.patterns.empty())
        song.patterns.push_back (Pattern {}); // (a song keeps at least one pattern)
    return song;
}

std::optional<Song> importBlxBytes (const std::vector<std::uint8_t>& bytes, const BlxImportOptions& options, std::string* error)
{
    auto project = blx::parse (bytes, error);
    if (! project)
        return std::nullopt;
    return importBlx (*project, options);
}
} // namespace lynx
