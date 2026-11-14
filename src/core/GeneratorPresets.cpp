#include "core/GeneratorPresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

#include "core/EffectPresets.h"
#include "core/LynxBanks.h"
#include "core/NativeEngines.h"
#include "core/PluginPresets.h"

namespace lynx::genpresets
{
namespace
{
// Every generator kind with its id; the ids are the ones engine/ProjectFile.cpp writes in .lynx projects
constexpr std::pair<InstrumentKind, const char*> ids[] = {
    { InstrumentKind::kick, "kick" },
    { InstrumentKind::snare, "snare" },
    { InstrumentKind::hat, "hat" },
    { InstrumentKind::clap, "clap" },
    { InstrumentKind::synth, "synth" },
    { InstrumentKind::sampler, "sampler" },
    { InstrumentKind::audioClip, "audioClip" },
    { InstrumentKind::plugin, "plugin" },
    { InstrumentKind::controller, "controller" },
    { InstrumentKind::sakura, "sakura" },
    { InstrumentKind::drumpad, "drumpad" },
    { InstrumentKind::boobass, "boobass" },
    { InstrumentKind::drumsynth, "drumsynth" },
    { InstrumentKind::harmless, "harmless" },
    { InstrumentKind::sytrus, "sytrus" },
    { InstrumentKind::harmor, "harmor" },
    { InstrumentKind::gms, "gms" },
    { InstrumentKind::morphine, "morphine" },
    { InstrumentKind::ogun, "ogun" },
    { InstrumentKind::minisynth, "minisynth" },
    { InstrumentKind::dx10, "dx10" },
    { InstrumentKind::kepler, "kepler" },
    { InstrumentKind::poizone, "poizone" },
    { InstrumentKind::fruitkick, "fruitkick" },
    { InstrumentKind::bassdrum, "bassdrum" },
    { InstrumentKind::toxic, "toxic" },
    { InstrumentKind::threeosc, "threeosc" },
    { InstrumentKind::autogun, "autogun" },
    { InstrumentKind::transistorbass, "transistorbass" },
    { InstrumentKind::simsynth, "simsynth" },
    { InstrumentKind::wasp, "wasp" },
    { InstrumentKind::plucked, "plucked" },
    { InstrumentKind::beepmap, "beepmap" },
    { InstrumentKind::sawer, "sawer" },
    { InstrumentKind::flkeys, "flkeys" },
    { InstrumentKind::granulizer, "granulizer" },
    { InstrumentKind::drumaxx, "drumaxx" },
    { InstrumentKind::slayer, "slayer" },
    { InstrumentKind::fruitydance, "fruitydance" },
    { InstrumentKind::directwave, "directwave" },
    { InstrumentKind::midiOut, "midiOut" },
    { InstrumentKind::patcher, "patcher" },
    { InstrumentKind::soundfont, "soundfont" },
    { InstrumentKind::layer, "layer" },
    { InstrumentKind::dashboard, "dashboard" },
    { InstrumentKind::voltage, "voltage" },
    { InstrumentKind::vfx, "vfx" },
    { InstrumentKind::videoplayer, "videoplayer" },
    { InstrumentKind::keplerExo, "keplerexo" },
    { InstrumentKind::wavetraveller, "wavetraveller" },
    { InstrumentKind::fruityslicer, "fruityslicer" },
    { InstrumentKind::slicex, "slicex" },
    { InstrumentKind::fpc, "fpc" },
    { InstrumentKind::pocketSuperSaw, "pocketsupersaw" },
};

constexpr const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode (const std::vector<std::uint8_t>& data)
{
    std::string out;
    out.reserve ((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3)
    {
        const auto size = data.size();
        const std::uint32_t v = ((std::uint32_t) data[i] << 16) | (i + 1 < size ? (std::uint32_t) data[i + 1] << 8 : 0u)
                                | (i + 2 < size ? (std::uint32_t) data[i + 2] : 0u);
        out += b64[(v >> 18) & 63];
        out += b64[(v >> 12) & 63];
        out += i + 1 < size ? b64[(v >> 6) & 63] : '=';
        out += i + 2 < size ? b64[v & 63] : '=';
    }
    return out;
}

bool base64Decode (const std::string& text, std::vector<std::uint8_t>& out)
{
    out.clear();
    std::uint32_t acc = 0;
    int bits = 0;
    for (const auto c : text)
    {
        if (c == '=' || c == ' ' || c == '\r' || c == '\n' || c == '\t')
            continue;
        const auto* at = c != 0 ? std::strchr (b64, c) : nullptr;
        if (at == nullptr)
            return false;
        acc = (acc << 6) | (std::uint32_t) (at - b64);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back ((std::uint8_t) (acc >> bits));
        }
    }
    return true;
}

// A value on one line: newlines and backslashes escaped (a name or path never ends a line early)
std::string escape (const std::string& s)
{
    std::string out;
    for (const auto c : s)
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    return out;
}

std::string unescape (const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '\\' || i + 1 >= s.size())
        {
            out += s[i];
            continue;
        }
        const auto n = s[++i];
        out += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    }
    return out;
}

// Floats as their exact bits (8 hex digits): a load gives back the very same value, whatever the locale
std::string floatToken (float v)
{
    std::uint32_t bits = 0;
    std::memcpy (&bits, &v, 4);
    char text[12];
    std::snprintf (text, sizeof (text), "%08x", (unsigned) bits);
    return text;
}

bool parseFloatToken (const std::string& t, float& v)
{
    if (t.size() != 8)
        return false;
    std::uint32_t bits = 0;
    for (const auto c : t)
    {
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return false;
        bits = (bits << 4) | (std::uint32_t) d;
    }
    std::memcpy (&v, &bits, 4);
    return true;
}

bool parseInt (const std::string& t, long long& v)
{
    if (t.empty())
        return false;
    std::size_t i = t[0] == '-' ? 1 : 0;
    if (i >= t.size())
        return false;
    long long out = 0;
    for (; i < t.size(); ++i)
    {
        if (t[i] < '0' || t[i] > '9' || out > 1000000000000000LL)
            return false;
        out = out * 10 + (t[i] - '0');
    }
    v = t[0] == '-' ? -out : out;
    return true;
}

std::vector<std::string> tokens (const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream in (s);
    for (std::string t; in >> t;)
        out.push_back (t);
    return out;
}

// Writes or reads a fixed sequence of values (the synth's settings): one function lists the fields for both
struct TokenWriter
{
    std::string out;
    void put (const std::string& t)
    {
        if (! out.empty())
            out += ' ';
        out += t;
    }
    void f (float& v) { put (floatToken (v)); }
    void i (int& v) { put (std::to_string (v)); }
    void b (bool& v) { put (v ? "1" : "0"); }
    void u8 (Waveform& w) { put (std::to_string ((int) w)); }
};

struct TokenReader
{
    std::vector<std::string> list;
    std::size_t at = 0;
    bool ok = true;
    const std::string* next()
    {
        if (at >= list.size())
        {
            ok = false;
            return nullptr;
        }
        return &list[at++];
    }
    void f (float& v)
    {
        if (const auto* t = next(); t == nullptr || ! parseFloatToken (*t, v))
            ok = false;
    }
    void i (int& v)
    {
        long long x = 0;
        if (const auto* t = next(); t == nullptr || ! parseInt (*t, x) || x < -2147483647LL || x > 2147483647LL)
            ok = false;
        else
            v = (int) x;
    }
    void b (bool& v)
    {
        int x = 0;
        i (x);
        v = x != 0;
    }
    void u8 (Waveform& w)
    {
        int x = 0;
        i (x);
        if (x < 0 || x > (int) Waveform::spectral)
            ok = false;
        else
            w = (Waveform) x;
    }
};

template <typename IO>
void envelopeFields (IO& io, ModEnvelope& e)
{
    io.f (e.amount), io.f (e.attack), io.f (e.hold), io.f (e.decay), io.f (e.sustain), io.f (e.release), io.i (e.shape);
}

// The synth's settings apart from its four 256-value tables (those have their own lines, only when used)
template <typename IO>
void synthFields (IO& io, SynthSettings& s)
{
    for (auto& o : s.osc)
        io.u8 (o.wave), io.i (o.octave), io.f (o.detune), io.f (o.level), io.i (o.semitones), io.f (o.pan);
    io.f (s.attack), io.f (s.decay), io.f (s.sustain), io.f (s.release), io.f (s.cutoff), io.f (s.resonance);
    io.i (s.filterType), io.b (s.ladder), io.i (s.chorus), io.f (s.stereoTremolo), io.f (s.vibratoCents), io.f (s.keyTrack);
    io.f (s.sweep), io.f (s.sweepTime);
    for (auto& g : s.bandGain)
        io.f (g);
    envelopeFields (io, s.filterEnv);
    envelopeFields (io, s.pitchEnv);
    for (auto& l : s.lfo)
        io.f (l.amount), io.f (l.period), io.b (l.sync), io.i (l.shape), io.f (l.delay), io.f (l.attack), io.b (l.global), io.b (l.secondsTiming);
    io.b (s.mono), io.b (s.porta), io.f (s.portaBeats), io.b (s.am), io.b (s.ring), io.f (s.fm), io.f (s.fmDecay), io.f (s.fmSustain);
}

using Table = std::array<float, SynthSettings::spectrumSize>;
constexpr std::pair<const char*, Table SynthSettings::*> synthTables[] = {
    { "synthSpectrum", &SynthSettings::spectrum },
    { "synthSpectrumPhase", &SynthSettings::spectrumPhase },
    { "synthSpectrumDecay", &SynthSettings::spectrumDecay },
    { "synthSpectrumTarget", &SynthSettings::spectrumTarget },
};

std::string floatList (const float* v, std::size_t n)
{
    std::string out;
    out.reserve (n * 9);
    for (std::size_t k = 0; k < n; ++k)
    {
        if (k > 0)
            out += ' ';
        out += floatToken (v[k]);
    }
    return out;
}

bool readFloats (const std::string& value, std::vector<float>& out)
{
    out.clear();
    for (const auto& t : tokens (value))
    {
        float f = 0.0f;
        if (! parseFloatToken (t, f))
            return false;
        out.push_back (f);
    }
    return true;
}

std::string trim (const std::string& s)
{
    const auto a = s.find_first_not_of (" \t\r\n");
    if (a == std::string::npos)
        return {};
    return s.substr (a, s.find_last_not_of (" \t\r\n") - a + 1);
}

void copySound (const ChannelDef& from, ChannelDef& to)
{
    to.samplePath = from.samplePath;
    to.zones = from.zones;
    to.modelParams = from.modelParams;
    to.pluginState = from.pluginState;
    to.controllerState = from.controllerState;
    to.synth = from.synth;
    to.keyLabels = from.keyLabels;
}
} // namespace

std::string kindId (InstrumentKind kind)
{
    for (const auto& [k, id] : ids)
        if (k == kind)
            return id;
    return {};
}

std::optional<InstrumentKind> kindFromId (const std::string& id)
{
    for (const auto& [k, name] : ids)
        if (id == name)
            return k;
    return std::nullopt;
}

std::string engineName (InstrumentKind kind)
{
    if (const auto* e = natives::instrumentEngine (kind))
        return e->name;
    switch (kind)
    {
        case InstrumentKind::synth: return "Synth";
        case InstrumentKind::drumsynth: return "DrumSynth Live";
        case InstrumentKind::midiOut: return "MIDI Out";
        case InstrumentKind::autogun: return "Autogun";
        case InstrumentKind::directwave: return "DirectWave";
        case InstrumentKind::soundfont: return "LYNX Soundfont";
        default: break;
    }
    return kindId (kind);
}

std::string folderName (InstrumentKind kind)
{
    return pluginpresets::folderNameFor (engineName (kind));
}

bool canSave (InstrumentKind kind)
{
    return kind == InstrumentKind::synth || ! natives::presetFolderOf (kind).empty() || banks::hasBank (kind);
}

std::string toText (const std::string& name, const ChannelDef& c)
{
    std::string out = std::string (header) + "\n";
    auto line = [&out] (const char* key, const std::string& value) {
        out += key;
        out += ": ";
        out += value;
        out += '\n';
    };
    line ("name", escape (name));
    line ("generator", kindId (c.instrument));
    line ("engine", escape (engineName (c.instrument))); // (informative)
    if (! c.samplePath.empty())
        line ("sample", escape (c.samplePath));
    if (! c.pluginState.empty())
        line ("pluginState", base64Encode (c.pluginState));
    if (! c.controllerState.empty())
        line ("controllerState", base64Encode (c.controllerState));
    if (! c.modelParams.empty())
        line ("modelParams", floatList (c.modelParams.data(), c.modelParams.size()));
    for (const auto& z : c.zones) // root, keys, velocities, tune, gain, loop; the path last (the rest of the line)
        line ("zone", std::to_string (z.rootKey) + " " + std::to_string (z.lowKey) + " " + std::to_string (z.highKey) + " "
                          + std::to_string (z.lowVelocity) + " " + std::to_string (z.highVelocity) + " " + floatToken (z.tune) + " "
                          + floatToken (z.gain) + " " + std::to_string (z.loopStart) + " " + std::to_string (z.loopEnd) + " "
                          + std::to_string (z.loopMode) + " " + escape (z.path));
    for (const auto& [key, label] : c.keyLabels)
        line ("keyLabel", std::to_string (key) + " " + escape (label));
    if (! (c.synth == SynthSettings {}))
    {
        auto s = c.synth;
        TokenWriter w;
        synthFields (w, s);
        line ("synth", w.out);
        for (const auto& [key, table] : synthTables)
            if (std::any_of ((s.*table).begin(), (s.*table).end(), [] (float v) { return v != 0.0f || std::signbit (v); }))
                line (key, floatList ((s.*table).data(), (s.*table).size()));
    }
    return out;
}

bool looksLikePreset (const std::string& start)
{
    const std::string h = header;
    return start.size() >= h.size() && start.compare (0, h.size(), h) == 0
           && (start.size() == h.size() || start[h.size()] == '\n' || start[h.size()] == '\r');
}

std::optional<Preset> fromText (const std::string& text)
{
    if (! looksLikePreset (text))
        return std::nullopt;
    Preset p;
    bool haveKind = false;
    std::istringstream in (text);
    std::string row;
    std::getline (in, row); // (the header)
    while (std::getline (in, row))
    {
        if (! row.empty() && row.back() == '\r')
            row.pop_back();
        const auto colon = row.find (": ");
        if (colon == std::string::npos)
            continue;
        const auto key = row.substr (0, colon);
        const auto value = row.substr (colon + 2);
        auto& c = p.sound;
        if (key == "name")
            p.name = unescape (value);
        else if (key == "generator")
        {
            const auto kind = kindFromId (trim (value));
            if (! kind)
                return std::nullopt; // (a generator this build does not know: nothing to load it into)
            p.kind = *kind;
            haveKind = true;
        }
        else if (key == "sample")
            c.samplePath = unescape (value);
        else if (key == "pluginState")
        {
            if (! base64Decode (value, c.pluginState))
                return std::nullopt;
        }
        else if (key == "controllerState")
        {
            if (! base64Decode (value, c.controllerState))
                return std::nullopt;
        }
        else if (key == "modelParams")
        {
            if (! readFloats (value, c.modelParams))
                return std::nullopt;
        }
        else if (key == "zone")
        {
            // 10 numbers, then the path (which may hold spaces)
            std::size_t at = 0;
            std::vector<std::string> numbers;
            while (numbers.size() < 10)
            {
                const auto end = value.find (' ', at);
                if (end == std::string::npos)
                    return std::nullopt;
                numbers.push_back (value.substr (at, end - at));
                at = end + 1;
            }
            SampleZone z;
            long long v[8] {};
            const int intAt[] = { 0, 1, 2, 3, 4, 7, 8, 9 };
            for (int k = 0; k < 8; ++k)
                if (! parseInt (numbers[(std::size_t) intAt[k]], v[k]))
                    return std::nullopt;
            if (! parseFloatToken (numbers[5], z.tune) || ! parseFloatToken (numbers[6], z.gain))
                return std::nullopt;
            z.rootKey = (int) v[0], z.lowKey = (int) v[1], z.highKey = (int) v[2], z.lowVelocity = (int) v[3], z.highVelocity = (int) v[4];
            z.loopStart = v[5], z.loopEnd = v[6], z.loopMode = (int) v[7];
            z.path = unescape (value.substr (at));
            c.zones.push_back (std::move (z));
        }
        else if (key == "keyLabel")
        {
            const auto space = value.find (' ');
            long long k = 0;
            if (space == std::string::npos || ! parseInt (value.substr (0, space), k))
                return std::nullopt;
            c.keyLabels[(int) k] = unescape (value.substr (space + 1));
        }
        else if (key == "synth")
        {
            TokenReader r;
            r.list = tokens (value);
            synthFields (r, c.synth);
            if (! r.ok || r.at != r.list.size())
                return std::nullopt;
        }
        else
        {
            for (const auto& [name, table] : synthTables)
                if (key == name)
                {
                    std::vector<float> v;
                    if (! readFloats (value, v) || v.size() != (std::size_t) SynthSettings::spectrumSize)
                        return std::nullopt;
                    std::copy (v.begin(), v.end(), (c.synth.*table).begin());
                }
            // (other keys: written by a newer LYNX SHARE; skipped)
        }
    }
    if (! haveKind)
        return std::nullopt;
    p.sound.instrument = p.kind;
    p.sound.name = p.name;
    return p;
}

ChannelDef soundOf (const ChannelDef& channel)
{
    ChannelDef out;
    out.instrument = channel.instrument;
    copySound (channel, out);
    return out;
}

bool sameSound (const ChannelDef& a, const ChannelDef& b)
{
    return a.instrument == b.instrument && soundOf (a) == soundOf (b);
}

std::optional<ChannelDef> withPreset (const ChannelDef& current, const Preset& preset)
{
    if (current.instrument != preset.kind)
        return std::nullopt;
    auto out = current;
    copySound (preset.sound, out);
    if (! preset.name.empty())
        out.name = preset.name;
    return out;
}

ChannelDef newChannel (const Preset& preset)
{
    ChannelDef out;
    out.instrument = preset.kind;
    copySound (preset.sound, out);
    out.name = preset.name.empty() ? engineName (preset.kind) : preset.name;
    return out;
}

std::string fileNameFor (const std::string& name)
{
    return presets::fileNameFor (name);
}
} // namespace lynx::genpresets
