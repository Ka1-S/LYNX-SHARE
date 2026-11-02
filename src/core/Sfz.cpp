#include "core/Sfz.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>

#include "core/AudioEdit.h"
#include "core/FlpImport.h"

namespace lynx::sfz
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

std::string lower (std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char) (c - 'A' + 'a');
    return s;
}

std::string trim (const std::string& s)
{
    const auto a = s.find_first_not_of (" \t\r\n");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of (" \t\r\n");
    return s.substr (a, b - a + 1);
}

bool isNameChar (char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'; }

std::optional<std::string> readText (const std::string& path)
{
    std::ifstream in (u8path (path), std::ios::binary);
    if (! in)
        return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::mutex decoderLock;
Decoder& installedDecoder()
{
    static Decoder d;
    return d;
}

// comments out (// to the end of the line, /* … */), then #include and #define expanded, line by line
std::string preprocess (const std::string& text, const std::function<std::optional<std::string> (const std::string&)>& readInclude,
                        std::vector<std::pair<std::string, std::string>>& defines, int depth)
{
    std::string noComments;
    noComments.reserve (text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n')
                ++i;
            noComments += '\n';
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < text.size() && ! (text[i] == '*' && text[i + 1] == '/'))
            {
                if (text[i] == '\n')
                    noComments += '\n';
                ++i;
            }
            ++i;
            noComments += ' ';
            continue;
        }
        noComments += text[i];
    }

    std::string out;
    std::istringstream lines (noComments);
    std::string line;
    while (std::getline (lines, line))
    {
        // $NAME → its #define'd value (longest names first, so $A doesn't eat $AB)
        if (line.find ('$') != std::string::npos)
            for (const auto& [name, value] : defines)
                for (std::size_t at = line.find (name); at != std::string::npos; at = line.find (name, at + value.size()))
                    line.replace (at, name.size(), value);
        const auto t = trim (line);
        if (t.rfind ("#define", 0) == 0)
        {
            std::istringstream words (t.substr (7));
            std::string name, value;
            words >> name;
            std::getline (words, value);
            value = trim (value);
            if (! name.empty() && name[0] == '$')
            {
                defines.erase (std::remove_if (defines.begin(), defines.end(), [&] (const auto& d) { return d.first == name; }), defines.end());
                defines.push_back ({ name, value });
                std::sort (defines.begin(), defines.end(), [] (const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
            }
            continue;
        }
        if (t.rfind ("#include", 0) == 0)
        {
            const auto q1 = t.find ('"'), q2 = t.rfind ('"');
            if (q1 != std::string::npos && q2 > q1 && readInclude && depth < 16)
                if (auto included = readInclude (t.substr (q1 + 1, q2 - q1 - 1)))
                    out += preprocess (*included, readInclude, defines, depth + 1) + "\n";
            continue;
        }
        out += line;
        out += '\n';
    }
    return out;
}

float toFloat (const std::string& s, float fallback)
{
    try
    {
        std::size_t used = 0;
        const auto v = std::stof (s, &used);
        return used > 0 ? v : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

int toInt (const std::string& s, int fallback)
{
    try
    {
        std::size_t used = 0;
        const auto v = std::stol (s, &used);
        return used > 0 ? (int) v : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

int absCents (float hz) { return (int) std::lround (1200.0 * std::log2 (std::max (1.0, (double) hz) / 8.176)); }
} // namespace

std::string ParsedRegion::get (const std::string& name, const std::string& fallback) const
{
    const auto it = opcodes.find (name);
    return it != opcodes.end() ? it->second : fallback;
}

int noteNumber (const std::string& textIn)
{
    const auto text = lower (trim (textIn));
    if (text.empty())
        return -1;
    if ((text[0] >= '0' && text[0] <= '9') || text[0] == '-')
        return toInt (text, -1);
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    if (text[0] < 'a' || text[0] > 'g')
        return -1;
    int n = base[text[0] - 'a'];
    std::size_t i = 1;
    if (i < text.size() && text[i] == '#')
        ++n, ++i;
    else if (i < text.size() && text[i] == 'b' && i + 1 < text.size() && (text[i + 1] == '-' || (text[i + 1] >= '0' && text[i + 1] <= '9')))
        --n, ++i;
    if (i >= text.size())
        return -1;
    const auto octave = toInt (text.substr (i), -100);
    if (octave == -100)
        return -1;
    return n + (octave + 1) * 12; // c4 = 60
}

Parsed parse (const std::string& text, const std::function<std::optional<std::string> (const std::string&)>& readInclude)
{
    std::vector<std::pair<std::string, std::string>> defines;
    const auto source = preprocess (text, readInclude, defines, 0);

    Parsed out;
    enum class Header { none, control, global, master, group, region, other };
    auto header = Header::none;
    std::map<std::string, std::string> global, master, group, region;
    bool inRegion = false;
    auto closeRegion = [&] {
        if (! inRegion)
            return;
        ParsedRegion r;
        r.opcodes = global;
        for (const auto& [k, v] : master)
            r.opcodes[k] = v;
        for (const auto& [k, v] : group)
            r.opcodes[k] = v;
        for (const auto& [k, v] : region)
            r.opcodes[k] = v;
        // the <control> values in force for this region
        for (const auto* c : { "default_path", "note_offset", "octave_offset" })
            if (const auto it = out.control.find (c); it != out.control.end())
                r.opcodes["__" + std::string (c)] = it->second;
        out.regions.push_back (std::move (r));
        region.clear();
        inRegion = false;
    };
    auto set = [&] (const std::string& key, const std::string& value) {
        switch (header)
        {
            case Header::control: out.control[key] = value; break;
            case Header::global: global[key] = value; break;
            case Header::master: master[key] = value; break;
            case Header::group: group[key] = value; break;
            case Header::region: region[key] = value; break;
            case Header::none: global[key] = value; break; // (opcodes before any header: as global)
            case Header::other: break;
        }
    };

    std::istringstream lines (source);
    std::string line;
    while (std::getline (lines, line))
    {
        std::size_t i = 0;
        while (i < line.size())
        {
            const auto c = line[i];
            if (c == ' ' || c == '\t' || c == '\r')
            {
                ++i;
                continue;
            }
            if (c == '<')
            {
                const auto close = line.find ('>', i);
                if (close == std::string::npos)
                    break;
                const auto name = lower (line.substr (i + 1, close - i - 1));
                closeRegion();
                if (name == "region")
                    header = Header::region, inRegion = true;
                else if (name == "group")
                    header = Header::group, group.clear();
                else if (name == "master")
                    header = Header::master, master.clear(), group.clear();
                else if (name == "global")
                    header = Header::global, global.clear(), master.clear(), group.clear();
                else if (name == "control")
                    header = Header::control;
                else
                    header = Header::other, ++out.unknownHeaders;
                i = close + 1;
                continue;
            }
            // opcode: name=value; the value runs to the next " name=" or "<" (so sample paths may have spaces)
            const auto eq = line.find ('=', i);
            if (eq == std::string::npos)
                break;
            const auto key = lower (trim (line.substr (i, eq - i)));
            std::size_t end = eq + 1, valueEnd = line.size();
            while (end < line.size())
            {
                if (line[end] == '<')
                {
                    valueEnd = end;
                    break;
                }
                if (line[end] == ' ' || line[end] == '\t')
                {
                    auto j = end;
                    while (j < line.size() && (line[j] == ' ' || line[j] == '\t'))
                        ++j;
                    auto k = j;
                    while (k < line.size() && isNameChar (line[k]))
                        ++k;
                    if (k > j && k < line.size() && line[k] == '=')
                    {
                        valueEnd = end;
                        break;
                    }
                    end = j;
                    continue;
                }
                ++end;
            }
            if (! key.empty())
                set (key, trim (line.substr (eq + 1, valueEnd - eq - 1)));
            i = valueEnd;
        }
    }
    closeRegion();
    return out;
}

void setDecoder (Decoder decoder)
{
    std::lock_guard<std::mutex> g (decoderLock);
    installedDecoder() = std::move (decoder);
}

bool decodeWavFile (const std::string& path, Audio& out)
{
    std::ifstream in (u8path (path), std::ios::binary);
    if (! in)
        return false;
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    audioedit::Document doc;
    if (! audioedit::decodeWav (bytes.data(), bytes.size(), doc) || doc.audio.empty())
        return false;
    out.channels = std::move (doc.audio.channels);
    out.sampleRate = doc.audio.sampleRate;
    out.loopStart = doc.hasLoop() ? doc.loopStart : -1;
    out.loopEnd = doc.hasLoop() ? doc.loopEnd : -1;
    return true;
}

bool isSfzPath (const std::string& path)
{
    return path.size() > 4 && lower (path.substr (path.size() - 4)) == ".sfz";
}

std::shared_ptr<const sf::Font> loadText (const std::string& text, const std::string& folder, const std::string& name, std::string* error, Report* report)
{
    Report local;
    auto& rep = report != nullptr ? *report : local;
    const auto parsed = parse (text, [&folder] (const std::string& file) {
        auto p = file;
        std::replace (p.begin(), p.end(), '\\', '/');
        auto found = readText (utf8 (u8path (folder) / u8path (p)));
        if (! found)
            found = readText (existingPathIgnoringCase (utf8 (u8path (folder) / u8path (p))));
        return found;
    });

    Decoder decode;
    {
        std::lock_guard<std::mutex> g (decoderLock);
        decode = installedDecoder();
    }

    auto font = std::make_shared<sf::Font>();
    font->name = name;
    sf::Preset preset;
    preset.name = name;

    struct Loaded
    {
        std::vector<std::uint32_t> starts; // per channel, into font->samples
        std::int64_t frames = 0;
        double rate = 44100.0;
        std::int64_t loopStart = -1, loopEnd = -1;
        bool ok = false;
    };
    std::map<std::string, Loaded> cache;
    auto load = [&] (const std::string& path) -> const Loaded& {
        if (const auto it = cache.find (path); it != cache.end())
            return it->second;
        Loaded l;
        Audio audio;
        // WAV by LYNX's own reader (it keeps the smpl loop), the other formats by the app's decoder
        bool ok = decodeWavFile (path, audio);
        if (! ok && decode)
            ok = decode (path, audio);
        if (ok && ! audio.channels.empty() && ! audio.channels[0].empty())
        {
            l.frames = (std::int64_t) audio.channels[0].size();
            l.rate = audio.sampleRate > 0 ? audio.sampleRate : 44100.0;
            l.loopStart = audio.loopStart, l.loopEnd = audio.loopEnd;
            const auto channels = std::min<std::size_t> (2, audio.channels.size());
            for (std::size_t ch = 0; ch < channels; ++ch)
            {
                l.starts.push_back ((std::uint32_t) font->samples.size());
                for (const auto x : audio.channels[ch])
                    font->samples.push_back ((std::int16_t) std::lround (std::clamp (x, -1.0f, 1.0f) * 32767.0f));
                font->samples.insert (font->samples.end(), 8, 0); // (interpolation reads past the end)
            }
            l.ok = true;
            ++rep.samplesLoaded;
        }
        else
        {
            ++rep.samplesMissing;
            rep.missing.push_back (path);
        }
        return cache.emplace (path, std::move (l)).first->second;
    };

    static const char* const known[] = { "sample", "lokey", "hikey", "key", "lovel", "hivel", "pitch_keycenter", "pitch_keytrack", "transpose", "tune",
                                         "pitch", "volume", "gain", "amplitude", "pan", "offset", "end", "loop_mode", "loopmode", "loop_start", "loop_end",
                                         "loopstart", "loopend", "ampeg_delay", "ampeg_attack", "ampeg_hold", "ampeg_decay", "ampeg_sustain", "ampeg_release",
                                         "amp_veltrack", "fil_type", "filtype", "cutoff", "resonance", "fileg_delay", "fileg_attack", "fileg_hold", "fileg_decay",
                                         "fileg_sustain", "fileg_release", "fileg_depth", "pitcheg_delay", "pitcheg_attack", "pitcheg_hold", "pitcheg_decay",
                                         "pitcheg_sustain", "pitcheg_release", "pitcheg_depth", "amplfo_delay", "amplfo_freq", "amplfo_depth", "fillfo_delay",
                                         "fillfo_freq", "fillfo_depth", "pitchlfo_delay", "pitchlfo_freq", "pitchlfo_depth", "group", "off_by", "off_mode",
                                         "seq_length", "seq_position", "lorand", "hirand", "trigger", "sw_lokey", "sw_hikey", "sw_last", "sw_default", "delay" };
    std::map<std::string, bool> counted;

    for (const auto& pr : parsed.regions)
    {
        for (const auto& [k, v] : pr.opcodes)
            if (k.rfind ("__", 0) != 0 && std::find_if (std::begin (known), std::end (known), [&k] (const char* n) { return k == n; }) == std::end (known)
                && ! counted[k])
                counted[k] = true, ++rep.unknownOpcodes;

        const auto noteOffset = toInt (pr.get ("__note_offset", "0"), 0) + 12 * toInt (pr.get ("__octave_offset", "0"), 0);
        auto note = [&] (const char* opcode, int fallback) {
            const auto v = pr.get (opcode);
            if (v.empty())
                return fallback;
            const auto n = noteNumber (v);
            return n < 0 ? fallback : n + noteOffset;
        };
        auto num = [&] (const char* opcode, float fallback) { return toFloat (pr.get (opcode), fallback); };

        // key switches: the range and default are the instrument's
        if (const auto lo = note ("sw_lokey", -1); lo >= 0 && font->switchLo < 0)
            font->switchLo = lo, font->switchHi = std::max (lo, note ("sw_hikey", lo));
        if (const auto d = note ("sw_default", -1); d >= 0 && font->switchDefault < 0)
            font->switchDefault = d;

        auto samplePath = pr.get ("sample");
        if (samplePath.empty() || samplePath[0] == '*') // (generated waves: not played)
            continue;
        samplePath = pr.get ("__default_path") + samplePath;
        std::replace (samplePath.begin(), samplePath.end(), '\\', '/');
        auto full = utf8 (u8path (folder) / u8path (samplePath));
        if (std::error_code ec; ! fs::exists (u8path (full), ec))
            full = existingPathIgnoringCase (full);
        const auto& sample = load (full);
        if (! sample.ok)
            continue;

        sf::Region r;
        r.sfz = true;
        auto lokey = note ("lokey", 0), hikey = note ("hikey", 127), center = note ("pitch_keycenter", 60);
        if (const auto k = note ("key", -1); k >= 0)
            lokey = hikey = k, center = note ("pitch_keycenter", k);
        r.loKey = (std::uint8_t) std::clamp (lokey, 0, 127), r.hiKey = (std::uint8_t) std::clamp (hikey, 0, 127);
        r.loVel = (std::uint8_t) std::clamp ((int) num ("lovel", 1), 0, 127), r.hiVel = (std::uint8_t) std::clamp ((int) num ("hivel", 127), 0, 127);
        if (r.loKey > r.hiKey || r.loVel > r.hiVel)
            continue;
        r.rootKey = std::clamp (center, 0, 127);
        r.keyTrack = (int) num ("pitch_keytrack", 100);
        r.transpose = (int) num ("transpose", 0);
        r.tune = (int) std::lround (num ("tune", num ("pitch", 0)));
        auto db = num ("volume", 0) + num ("gain", 0);
        if (const auto a = num ("amplitude", 100); a > 0 && a != 100)
            db += 20.0f * std::log10 (a / 100.0f);
        r.attenuation = -db / 0.04f; // (the engine's EMU scaling: −0.04 · attenuation dB)
        r.sampleRate = (std::uint32_t) std::lround (sample.rate);

        // sample points, in frames of the file
        const auto frames = sample.frames;
        const auto offset = std::clamp<std::int64_t> ((std::int64_t) num ("offset", 0), 0, frames - 1);
        auto endFrame = frames;
        if (const auto e = pr.get ("end"); ! e.empty())
            endFrame = std::clamp<std::int64_t> ((std::int64_t) toFloat (e, (float) frames) + 1, 0, frames); // (inclusive in SFZ)
        if (endFrame <= offset)
            continue;
        auto mode = lower (pr.get ("loop_mode", pr.get ("loopmode")));
        auto loopStart = (std::int64_t) num ("loop_start", num ("loopstart", (float) sample.loopStart));
        auto loopEnd = pr.get ("loop_end").empty() && pr.get ("loopend").empty() ? sample.loopEnd
                                                                                  : (std::int64_t) num ("loop_end", num ("loopend", 0)) + 1;
        if (mode.empty())
            mode = sample.loopStart >= 0 && sample.loopEnd > sample.loopStart ? "loop_continuous" : "no_loop";
        if ((mode == "loop_continuous" || mode == "loop_sustain") && (loopStart < 0 || loopEnd <= loopStart))
            loopStart = offset, loopEnd = endFrame;
        loopStart = std::clamp<std::int64_t> (loopStart, 0, endFrame);
        loopEnd = std::clamp<std::int64_t> (loopEnd, loopStart, endFrame);
        r.loop = mode == "loop_continuous" && loopEnd > loopStart ? sf::LoopMode::continuous
                 : mode == "loop_sustain" && loopEnd > loopStart  ? sf::LoopMode::untilRelease
                                                                   : sf::LoopMode::none;
        r.oneShot = mode == "one_shot";

        // amplitude envelope (seconds; sustain %), the region's start delay on top
        r.ampEnv.delay = num ("ampeg_delay", 0) + num ("delay", 0);
        r.ampEnv.attack = num ("ampeg_attack", 0);
        r.ampEnv.hold = num ("ampeg_hold", 0);
        r.ampEnv.decay = num ("ampeg_decay", 0);
        r.ampEnv.sustain = std::clamp (num ("ampeg_sustain", 100) / 100.0f, 0.0f, 1.0f);
        r.ampEnv.release = std::max (0.001f, num ("ampeg_release", 0.001f));
        r.ampVelTrack = num ("amp_veltrack", 100) / 100.0f;

        // filter: the low-pass types (others: no filter)
        const auto filType = lower (pr.get ("fil_type", pr.get ("filtype", "lpf_2p")));
        if (! pr.get ("cutoff").empty() && filType.rfind ("lpf", 0) == 0)
        {
            r.filterFc = std::clamp (absCents (num ("cutoff", 20000)), 1500, 13500);
            r.filterQ = (int) std::lround (std::clamp (num ("resonance", 0), 0.0f, 40.0f) * 10.0f);
        }
        // one modulation envelope: the filter's, else the pitch's
        const auto envOf = [&] (const std::string& p) {
            sf::Envelope e;
            e.delay = num ((p + "_delay").c_str(), 0), e.attack = num ((p + "_attack").c_str(), 0), e.hold = num ((p + "_hold").c_str(), 0);
            e.decay = num ((p + "_decay").c_str(), 0), e.sustain = std::clamp (num ((p + "_sustain").c_str(), 0) / 100.0f, 0.0f, 1.0f);
            e.release = num ((p + "_release").c_str(), 0);
            return e;
        };
        if (! pr.get ("fileg_depth").empty())
            r.modEnv = envOf ("fileg"), r.modEnvToFilterFc = (int) num ("fileg_depth", 0);
        else if (! pr.get ("pitcheg_depth").empty())
            r.modEnv = envOf ("pitcheg"), r.modEnvToPitch = (int) num ("pitcheg_depth", 0);
        // LFOs: amplitude and filter share the modulation LFO, pitch has the vibrato LFO
        if (! pr.get ("amplfo_freq").empty() || ! pr.get ("fillfo_freq").empty())
        {
            const auto amp = ! pr.get ("amplfo_freq").empty();
            r.freqModLfo = absCents (num (amp ? "amplfo_freq" : "fillfo_freq", 5));
            r.delayModLfo = num (amp ? "amplfo_delay" : "fillfo_delay", 0);
            r.modLfoToVolume = (int) std::lround (-10.0f * num ("amplfo_depth", 0));
            r.modLfoToFilterFc = (int) num ("fillfo_depth", 0);
        }
        if (! pr.get ("pitchlfo_freq").empty())
            r.freqVibLfo = absCents (num ("pitchlfo_freq", 5)), r.delayVibLfo = num ("pitchlfo_delay", 0), r.vibLfoToPitch = (int) num ("pitchlfo_depth", 0);

        // groups, round robin, random, triggers, key switches
        r.group = (int) num ("group", 0);
        r.offBy = (int) num ("off_by", 0);
        r.offFast = lower (pr.get ("off_mode", "fast")) != "normal";
        r.seqLength = std::max (1, (int) num ("seq_length", 1));
        r.seqPosition = std::clamp ((int) num ("seq_position", 1), 1, r.seqLength);
        r.loRand = num ("lorand", 0), r.hiRand = num ("hirand", 1);
        r.releaseTrigger = lower (pr.get ("trigger", "attack")) == "release";
        r.switchLast = note ("sw_last", -1);

        // one region per channel of the sample (a stereo file: left and right, panned like an SF2 stereo pair, 3 dB
        // down so the pair plays at the file's level)
        const auto pan = std::clamp (num ("pan", 0), -100.0f, 100.0f) / 200.0f;
        const auto channels = sample.starts.size();
        for (std::size_t ch = 0; ch < channels; ++ch)
        {
            auto c = r;
            const auto base = (std::int64_t) sample.starts[ch];
            c.start = (std::uint32_t) (base + offset);
            c.end = (std::uint32_t) (base + endFrame);
            c.loopStart = (std::uint32_t) (base + loopStart);
            c.loopEnd = (std::uint32_t) (base + loopEnd);
            if (channels == 2)
            {
                c.pan = std::clamp ((ch == 0 ? -0.5f : 0.5f) + pan, -0.5f, 0.5f);
                c.attenuation += 3.0103f / 0.04f;
            }
            else
                c.pan = pan;
            preset.regions.push_back (c);
            ++rep.regions;
        }
    }

    if (preset.regions.empty())
    {
        if (error != nullptr)
            *error = rep.samplesMissing > 0 ? "its samples were not found" : "no playable region";
        return nullptr;
    }
    font->presets.push_back (std::move (preset));
    return font;
}

sf::FontInfo scanFile (const std::string& path)
{
    sf::FontInfo info;
    const auto text = readText (path);
    if (! text)
        return info;
    const auto p = u8path (path);
    const auto folder = utf8 (p.parent_path());
    const auto parsed = parse (*text, [&folder] (const std::string& file) {
        auto q = file;
        std::replace (q.begin(), q.end(), '\\', '/');
        return readText (utf8 (u8path (folder) / u8path (q)));
    });
    for (const auto& r : parsed.regions)
        if (! r.get ("sample").empty())
            info.valid = true;
    if (info.valid)
    {
        info.name = utf8 (p.stem());
        info.presets.push_back ({ info.name, 0, 0 });
    }
    return info;
}

std::shared_ptr<const sf::Font> loadFile (const std::string& path, std::string* error, Report* report)
{
    const auto text = readText (path);
    if (! text)
    {
        if (error != nullptr)
            *error = "file not found";
        return nullptr;
    }
    const auto p = u8path (path);
    auto font = loadText (*text, utf8 (p.parent_path()), utf8 (p.stem()), error, report);
    if (font != nullptr)
        const_cast<sf::Font&> (*font).path = path;
    return font;
}
} // namespace lynx::sfz
