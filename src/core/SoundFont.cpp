// LYNX Soundfont: parameters, categories, the background font library and the voice engine (round 158). See
// core/SoundFont.h and docs/analise/REA_rodada158.md.

#include "core/SoundFont.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <thread>

#include "core/Denormals.h"

namespace lynx::sf
{
// --- font -------------------------------------------------------------------------------------------------------------

int Font::findPreset (int bank, int program) const noexcept
{
    if (presets.empty())
        return -1;
    for (std::size_t i = 0; i < presets.size(); ++i)
        if (presets[i].bank == bank && presets[i].program == program)
            return (int) i;
    // a drum kit asked for: any kit; otherwise the same program in the GM bank
    for (std::size_t i = 0; i < presets.size(); ++i)
        if (bank == 128 ? presets[i].bank == 128 : (presets[i].bank == 0 && presets[i].program == program))
            return (int) i;
    return 0;
}

// --- categories ---------------------------------------------------------------------------------------------------

namespace
{
std::string lowerAscii (std::string s)
{
    for (auto& ch : s)
        if (ch >= 'A' && ch <= 'Z')
            ch = (char) (ch - 'A' + 'a');
    return s;
}

bool hasWord (const std::string& lower, const char* w)
{
    return lower.find (w) != std::string::npos;
}
} // namespace

const std::vector<const char*>& categories()
{
    static const std::vector<const char*> list = { "Piano", "Chromatic percussion", "Organ", "Guitar", "Bass", "Strings",
                                                    "Ensemble", "Brass", "Reed", "Pipe", "Synth lead", "Synth pad",
                                                    "Synth effects", "Ethnic", "Percussive", "Sound effects", "Drums" };
    return list;
}

const char* category (int bank, int program, const std::string& name)
{
    const auto& c = categories();
    const auto n = lowerAscii (name);
    // drum words win (an "808" or "kit" preset anywhere is a drum sound)
    if (bank == 128 || hasWord (n, "drum") || hasWord (n, "kit") || hasWord (n, "808") || hasWord (n, "909") || hasWord (n, "kick")
        || hasWord (n, "snare") || hasWord (n, "hihat") || hasWord (n, "hi-hat") || hasWord (n, "perc"))
        return c[16];
    struct Word { const char* w; int cat; };
    static const Word words[] = { { "piano", 0 }, { "rhodes", 0 }, { "e.p", 0 }, { "epiano", 0 }, { "clav", 0 }, { "harpsi", 0 },
                                  { "bell", 1 }, { "vibra", 1 }, { "marimba", 1 }, { "xylo", 1 }, { "celesta", 1 }, { "glock", 1 },
                                  { "organ", 2 }, { "guitar", 3 }, { "gtr", 3 }, { "bass", 4 }, { "violin", 5 }, { "cello", 5 },
                                  { "viola", 5 }, { "harp", 5 }, { "string", 6 }, { "choir", 6 }, { "voice", 6 }, { "vox", 6 },
                                  { "trumpet", 7 }, { "trombone", 7 }, { "horn", 7 }, { "brass", 7 }, { "tuba", 7 }, { "sax", 8 },
                                  { "oboe", 8 }, { "clarinet", 8 }, { "bassoon", 8 }, { "flute", 9 }, { "piccolo", 9 },
                                  { "lead", 10 }, { "pad", 11 }, { "fx", 12 }, { "sitar", 13 }, { "koto", 13 }, { "banjo", 13 } };
    for (const auto& w : words)
        if (hasWord (n, w.w))
            return c[(std::size_t) w.cat];
    return c[(std::size_t) std::clamp (program, 0, 127) / 8];
}

// --- parameters ------------------------------------------------------------------------------------------------------

namespace
{
const ParamInfo infos[paramCount] = {
    { "bank", "Bank", 0, 128, 0, true },
    { "program", "Patch", 0, 127, 0, true },
    { "reverbAmount", "Reverb level", 0, 1, 0.5f, false },
    { "reverbOverride", "Reverb: custom level", 0, 1, 1, true },
    { "reverbBuiltIn", "Reverb: built-in", 0, 1, 1, true },
    { "reverbTrack", "Reverb: send to", -1, 125, -1, true },
    { "chorusAmount", "Chorus level", 0, 1, 0.5f, false },
    { "chorusOverride", "Chorus: custom level", 0, 1, 1, true },
    { "chorusBuiltIn", "Chorus: built-in", 0, 1, 1, true },
    { "chorusTrack", "Chorus: send to", -1, 125, -1, true },
    { "attack", "Attack", 0, 1, 0, false },
    { "decay", "Decay", 0, 1, 0, false },
    { "sustain", "Sustain", 0, 1, 0, false },
    { "release", "Release", 0, 1, 0, false },
    { "lfoDelay", "LFO delay", 0, 1, 0, false },
    { "lfoAmount", "LFO amount", 0, 1, 0, false },
    { "lfoSpeed", "LFO speed", 0, 1, 0, false },
    { "cutoff", "Cutoff", 0, 1, 0, false },
    { "cutoffMod", "Cutoff mod", 0, 1, 0, false },
    { "resonance", "Resonance", 0, 1, 0, false },
    { "bendRange", "Pitch bend range", 0, 48, 2, true },
    { "resampling", "Resampling", 0, 3, 1, true },
    { "programMode", "Program mode", 0, 2, 0, true },
    { "velocityToCutoff", "Velocity to cutoff", 0, 1, 1, true },
    { "emuEnvelope", "EMU volume envelope", 0, 1, 0, true },
    { "ignoreLinks", "Ignore sample links", 0, 1, 0, true },
    { "polyphony", "Polyphony", 1, 256, 64, true },
    { "useLoops", "Use loops", 0, 1, 1, true },
    { "volume", "Volume", -48, 12, 0, false },
    { "velocityCurve", "Velocity curve", 0, 4, 0, true },
    { "transpose", "Transpose", -48, 48, 0, true },
    { "modWheel", "Mod wheel", 0, 1, 0, false },
};

// knob 0…1 → seconds for the envelope / LFO overrides (0 = the preset's own): 1 ms … 20 s, exponential
float knobSeconds (float k) noexcept { return 0.001f * std::pow (20000.0f, std::clamp (k, 0.0f, 1.0f)); }
// knob → absolute cents for the cut-off override: FL's 1500 … 13500 cents (≈ 20 Hz … 19.9 kHz)
int knobCutoffCents (float k) noexcept { return (int) std::lround (1500.0f + 12000.0f * std::clamp (k, 0.0f, 1.0f)); }
// pitch LFO depth: centre = 0, the ends = -/+1000 cents (FL: 0..1000), finer near the centre
float knobLfoCents (float k) noexcept
{
    const auto x = (std::clamp (k, 0.0f, 1.0f) - 0.5f) * 2.0f;
    return (x < 0.0f ? -1000.0f : 1000.0f) * x * x;
}
// FL Soundfont Player's send level curve: (19^(2k) - 1) / 18, 1.0 at the centre, 20 at the top
float levelMultiplier (float k) noexcept { return (std::pow (19.0f, 2.0f * std::clamp (k, 0.0f, 1.0f)) - 1.0f) / 18.0f; }
// knob → LFO rate (0.1 … 20 Hz)
float knobLfoHz (float k) noexcept { return 0.1f * std::pow (200.0f, std::clamp (k, 0.0f, 1.0f)); }
} // namespace

const ParamInfo& paramInfo (int index) noexcept
{
    return infos[std::clamp (index, 0, paramCount - 1)];
}

std::array<float, paramCount> readParams (const std::vector<float>& modelParams) noexcept
{
    std::array<float, paramCount> p {};
    for (int i = 0; i < paramCount; ++i)
    {
        const auto& info = infos[i];
        auto v = i < (int) modelParams.size() ? modelParams[(std::size_t) i] : info.def;
        if (! std::isfinite (v))
            v = info.def;
        v = std::clamp (v, info.min, info.max);
        if (info.integer)
            v = std::round (v);
        p[(std::size_t) i] = v;
    }
    return p;
}

std::vector<float> defaultParams()
{
    std::vector<float> v;
    for (const auto& info : infos)
        v.push_back (info.def);
    return v;
}

float normalise (int index, float value) noexcept
{
    const auto& info = paramInfo (index);
    return info.max > info.min ? std::clamp ((value - info.min) / (info.max - info.min), 0.0f, 1.0f) : 0.0f;
}

float denormalise (int index, float value01) noexcept
{
    const auto& info = paramInfo (index);
    auto v = info.min + std::clamp (std::isfinite (value01) ? value01 : 0.0f, 0.0f, 1.0f) * (info.max - info.min);
    return info.integer ? std::round (v) : v;
}

std::string displayValue (int index, float v)
{
    char buf[64];
    const auto seconds = [&buf] (float s) {
        if (s < 0.9995f) // (1.00 s, not "1000 ms", for a value a hair below 1 s)
            std::snprintf (buf, sizeof (buf), "%.0f ms", s * 1000.0f);
        else
            std::snprintf (buf, sizeof (buf), "%.2f s", s);
        return std::string (buf);
    };
    switch (index)
    {
        case pAttack: case pDecay: case pRelease: case pLfoDelay:
            return v <= 0.0f ? std::string ("Preset") : seconds (knobSeconds (v));
        case pSustain:
            if (v <= 0.0f)
                return "Preset";
            std::snprintf (buf, sizeof (buf), "%.0f %%", v * 100.0f);
            return buf;
        case pLfoAmount:
            if (v <= 0.0f)
                return "Preset";
            std::snprintf (buf, sizeof (buf), "%+.0f cents", knobLfoCents (v));
            return buf;
        case pLfoSpeed:
            if (v <= 0.0f)
                return "Preset";
            std::snprintf (buf, sizeof (buf), "%.2f Hz", knobLfoHz (v));
            return buf;
        case pCutoff:
            if (v <= 0.0f)
                return "Preset";
            std::snprintf (buf, sizeof (buf), "%.0f Hz", 8.176 * std::pow (2.0, knobCutoffCents (v) / 1200.0));
            return buf;
        case pReverbAmount: case pChorusAmount:
            std::snprintf (buf, sizeof (buf), "%.0f %%", levelMultiplier (v) * 100.0f);
            return buf;
        case pCutoffMod: case pResonance:
            if (v <= 0.0f)
                return "Preset";
            std::snprintf (buf, sizeof (buf), "%.0f %%", v * 100.0f);
            return buf;
        case pModWheel:
            std::snprintf (buf, sizeof (buf), "%.0f %%", v * 100.0f);
            return buf;
        case pBendRange: case pTranspose:
            std::snprintf (buf, sizeof (buf), "%+.0f st", v);
            return index == pBendRange ? std::string (buf + (v >= 0 ? 1 : 0)) : std::string (buf);
        case pResampling:
        {
            static const char* names[] = { "Crunchy (linear)", "Standard", "HQ", "Extreme HQ" };
            return names[std::clamp ((int) v, 0, 3)];
        }
        case pProgramMode:
        {
            static const char* names[] = { "Automatic", "Instrument", "Drums" };
            return names[std::clamp ((int) v, 0, 2)];
        }
        case pVelocityCurve:
        {
            static const char* names[] = { "SoundFont", "Linear", "Soft", "Hard", "Fixed" };
            return names[std::clamp ((int) v, 0, 4)];
        }
        case pPolyphony:
            std::snprintf (buf, sizeof (buf), "%.0f voices", v);
            return buf;
        case pVolume:
            std::snprintf (buf, sizeof (buf), "%+.1f dB", v);
            return buf;
        case pReverbTrack: case pChorusTrack:
            if (v < 0.0f)
                return "None";
            if (v < 1.0f)
                return "Master";
            std::snprintf (buf, sizeof (buf), "Insert %.0f", v);
            return buf;
        default:
            break;
    }
    if (paramInfo (index).integer && paramInfo (index).max <= 1.0f)
        return v >= 0.5f ? "On" : "Off";
    std::snprintf (buf, sizeof (buf), "%g", v);
    return buf;
}

ChannelDef makeChannel (const std::string& fontPath, int bank, int program)
{
    ChannelDef c;
    c.name = "LYNX Soundfont";
    c.instrument = InstrumentKind::soundfont;
    c.samplePath = fontPath;
    c.modelParams = defaultParams();
    c.modelParams[pBank] = (float) std::clamp (bank, 0, 128);
    c.modelParams[pProgram] = (float) std::clamp (program, 0, 127);
    return c;
}

// --- FL Studio's Fruity Soundfont Player ------------------------------------------------------------------------------

bool parseFlPlayerState (const std::vector<std::uint8_t>& state, FlPlayerState& out)
{
    std::size_t at = 0;
    const auto have = [&] (std::size_t n) { return at + n <= state.size(); };
    const auto i32 = [&] {
        std::uint32_t v = 0;
        for (int b = 0; b < 4; ++b)
            v |= (std::uint32_t) state[at + (std::size_t) b] << (8 * b);
        at += 4;
        return (int) (std::int32_t) v;
    };
    const auto u8 = [&] { return state[at++]; };
    FlPlayerState s;
    if (! have (4 + 52 + 1))
        return false;
    s.version = i32();
    if (s.version < 2 || s.version > 6)
        return false;
    for (auto& v : s.p)
        v = i32();
    std::size_t len = u8();
    if (len == 0xFF)
    {
        if (! have (4))
            return false;
        len = (std::size_t) (std::uint32_t) i32();
    }
    if (! have (len))
        return false;
    s.path.assign ((const char*) state.data() + at, len);
    at += len;
    while (! s.path.empty() && (unsigned char) s.path.back() < 0x20)
        s.path.pop_back();
    if (! have (11))
        return false;
    s.reverbTarget = i32();
    s.reverbBuiltIn = u8() != 0;
    s.chorusTarget = i32();
    s.chorusBuiltIn = u8() != 0;
    u8(); // (a flag with no use found)
    if (s.version >= 3 && have (18))
    {
        s.ignoreLinks = u8() != 0;
        s.velocityFilter = u8() != 0;
        s.resampling = i32();
        i32(); // (an engine argument FL resets to 1)
        s.presetIndex = i32();
    }
    if (s.version >= 4 && have (1))
        s.emuEnvelope = u8() != 0;
    if (s.version >= 5 && have (2))
    {
        s.reverbLevel = u8() != 0;
        u8();
        s.chorusLevel = true; // (FL's loader puts both bytes into the reverb box)
    }
    if (s.version >= 6 && have (4))
    {
        s.programMode = i32();
        if (s.programMode < 0 || s.programMode > 2)
            s.programMode = 0;
    }
    out = std::move (s);
    return true;
}

std::pair<int, int> flParamRange (int flIndex) noexcept
{
    static const std::pair<int, int> ranges[13] = { { 1, 256 }, { 0, 127 }, { 0, 256 }, { 0, 256 }, { 0, 127 }, { -1, 5940 }, { -1, 5940 },
                                                    { -1, 127 }, { -1, 5940 }, { -1, 5900 }, { -128, 127 }, { -1, 127 }, { -1, 127 } };
    return flIndex >= 0 && flIndex < 13 ? ranges[flIndex] : std::pair<int, int> { 0, 0 };
}

std::optional<std::pair<int, float>> paramFromFl (int flIndex, int raw) noexcept
{
    constexpr float tiny = 1.0e-4f;
    const auto secondsKnob = [] (double s) { return s <= 0.001 ? tiny : (float) std::clamp (std::log (s / 0.001) / std::log (20000.0), 1.0e-4, 1.0); };
    switch (flIndex)
    {
        case 2: return std::pair { (int) pReverbAmount, (float) std::clamp (raw, 0, 256) / 256.0f };
        case 3: return std::pair { (int) pChorusAmount, (float) std::clamp (raw, 0, 256) / 256.0f };
        case 4: return std::pair { (int) pModWheel, (float) std::clamp (raw, 0, 127) / 127.0f };
        case 5: return std::pair { (int) pAttack, raw < 0 ? 0.0f : secondsKnob (raw * 0.001) };
        case 6: return std::pair { (int) pDecay, raw < 0 ? 0.0f : secondsKnob (raw * 0.004) };
        case 7:
            return std::pair { (int) pSustain, raw < 0 ? 0.0f : std::max (tiny, (float) std::pow (10.0, -(1000.0 - 1000.0 * std::pow (std::min (raw, 127) / 127.0, 0.2)) / 200.0)) };
        case 8: return std::pair { (int) pRelease, raw < 0 ? 0.0f : secondsKnob (raw * 0.004) };
        case 9: return std::pair { (int) pLfoDelay, raw < 0 ? 0.0f : secondsKnob (raw * 0.004) };
        case 10:
            return std::pair { (int) pLfoAmount, raw <= -128 ? 0.0f : 0.5f + 0.5f * (float) std::sqrt (std::min (1000, std::abs (raw * 1000 / 127)) / 1000.0) };
        case 11:
        {
            if (raw < 0)
                return std::pair { (int) pLfoSpeed, 0.0f };
            const auto hz = 8.176 * std::pow (2.0, std::clamp (std::round (1200.0 * std::log2 (std::max (1.0e-9, raw * 0.0102739726))), -16000.0, 4500.0) / 1200.0);
            return std::pair { (int) pLfoSpeed, (float) std::clamp (std::log (hz / 0.1) / std::log (200.0), 1.0e-4, 1.0) };
        }
        case 12: return std::pair { (int) pCutoff, raw < 0 ? 0.0f : std::max (tiny, (float) (std::min (raw, 127) * 12000 / 127) / 12000.0f) };
        default: return std::nullopt; // P0 / P1 (patch, bank) are not automatable in FL
    }
}

std::optional<std::pair<int, float>> automationFromFl (int flIndex, float flNormalised) noexcept
{
    const auto [lo, hi] = flParamRange (flIndex);
    if (hi <= lo || ! std::isfinite (flNormalised))
        return std::nullopt;
    const auto raw = lo + (int) (std::clamp (flNormalised, 0.0f, 1.0f) * (float) (hi - lo) + 1.0e-3f); // FL truncates
    const auto mapped = paramFromFl (flIndex, raw);
    if (! mapped)
        return std::nullopt;
    return std::pair { mapped->first, normalise (mapped->first, mapped->second) };
}

std::vector<float> paramsFromFl (const FlPlayerState& fl, const std::vector<PresetInfo>& fontPresets)
{
    auto p = defaultParams();
    const auto& v = fl.p;
    // the preset: v3+ by index into the presets sorted by bank, program; else (or no font) P1 = bank, P0 = program + 1
    int bank = std::clamp (v[1], 0, 127), program = std::clamp (v[0], 1, 256) - 1;
    if (program >= 128)
        program -= 128, bank = 128;
    if (fl.version >= 3 && fl.presetIndex >= 0 && fl.presetIndex < (int) fontPresets.size())
    {
        auto sorted = fontPresets;
        std::stable_sort (sorted.begin(), sorted.end(), [] (const PresetInfo& a, const PresetInfo& b) {
            return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
        });
        bank = sorted[(std::size_t) fl.presetIndex].bank, program = sorted[(std::size_t) fl.presetIndex].program;
    }
    p[pBank] = (float) std::clamp (bank, 0, 128);
    p[pProgram] = (float) std::clamp (program, 0, 127);
    // sends: the level multiplier (0…256, 128 = ×1) when its box is checked; built-in or a mixer track (else: off)
    p[pReverbAmount] = (float) std::clamp (v[2], 0, 256) / 256.0f;
    p[pChorusAmount] = (float) std::clamp (v[3], 0, 256) / 256.0f;
    p[pReverbOverride] = fl.reverbLevel ? 1.0f : 0.0f;
    p[pChorusOverride] = fl.chorusLevel ? 1.0f : 0.0f;
    p[pReverbBuiltIn] = fl.reverbBuiltIn ? 1.0f : 0.0f;
    p[pChorusBuiltIn] = fl.chorusBuiltIn ? 1.0f : 0.0f;
    p[pReverbTrack] = (float) std::clamp (fl.reverbTarget >= 0 ? fl.reverbTarget + 1 : -1, -1, 125);
    p[pChorusTrack] = (float) std::clamp (fl.chorusTarget >= 0 ? fl.chorusTarget + 1 : -1, -1, 125);
    // envelope / LFO / cut-off overrides (FL's minimum = the preset's: 0 here), FL's units → LYNX's knob curves
    for (int k = 4; k <= 12; ++k)
        if (const auto mapped = paramFromFl (k, v[(std::size_t) k]))
            p[(std::size_t) mapped->first] = mapped->second;
    // options
    p[pResampling] = fl.resampling == 1 ? 0.0f : fl.resampling == 6 ? 2.0f : fl.resampling == 8 ? 3.0f : 1.0f;
    p[pProgramMode] = (float) fl.programMode;
    p[pVelocityToCutoff] = fl.velocityFilter ? 1.0f : 0.0f;
    p[pEmuEnvelope] = fl.emuEnvelope ? 1.0f : 0.0f;
    p[pIgnoreLinks] = fl.ignoreLinks ? 1.0f : 0.0f;
    p[pPolyphony] = 256.0f; // (FL: 512 voices, the channel's own limit on top)
    p[pBendRange] = 2.0f;
    return p;
}

// --- font library -------------------------------------------------------------------------------------------------------

std::string FontHandle::error() const
{
    std::lock_guard<std::mutex> g (lock);
    return errorText;
}

bool FontHandle::wait (int timeoutMs) const
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
    while (! finished())
    {
        if (std::chrono::steady_clock::now() > until)
            return false;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return true;
}

void FontHandle::finish (std::shared_ptr<const Font> f, std::string err)
{
    {
        std::lock_guard<std::mutex> g (lock);
        errorText = std::move (err);
    }
    owner = std::move (f);
    ready.store (owner.get(), std::memory_order_release);
    done.store (true, std::memory_order_release);
}

FontLibrary& FontLibrary::instance()
{
    static auto* library = new FontLibrary(); // (never destroyed: a load may still be running at exit)
    return *library;
}

void FontLibrary::setListener (std::function<void (const std::string&)> f)
{
    std::lock_guard<std::mutex> g (lock);
    listener = std::move (f);
}

std::shared_ptr<FontHandle> FontLibrary::get (const std::string& path)
{
    return obtain (path, false);
}

std::shared_ptr<FontHandle> FontLibrary::getNow (const std::string& path)
{
    return obtain (path, true);
}

namespace
{
std::string fileStamp (const std::string& path)
{
    std::error_code ec;
    const std::filesystem::path p ((const char8_t*) path.c_str());
    const auto size = std::filesystem::file_size (p, ec);
    if (ec)
        return "missing";
    const auto time = std::filesystem::last_write_time (p, ec);
    return std::to_string (size) + ":" + std::to_string ((long long) time.time_since_epoch().count());
}
} // namespace

std::shared_ptr<FontHandle> FontLibrary::obtain (const std::string& path, bool now)
{
    const auto key = path + "|" + fileStamp (path);
    std::shared_ptr<FontHandle> handle;
    {
        std::lock_guard<std::mutex> g (lock);
        cache.erase (std::remove_if (cache.begin(), cache.end(), [] (const auto& e) { return e.second.expired(); }), cache.end());
        for (const auto& [k, weak] : cache)
            if (k == key)
                if (auto h = weak.lock())
                {
                    if (now)
                        h->wait();
                    return h;
                }
        handle = std::make_shared<FontHandle> (path);
        cache.emplace_back (key, handle);
    }
    auto load = [this, handle] {
        std::string error;
        auto font = loadFontFile (handle->filePath(), &error);
        handle->finish (std::move (font), font == nullptr ? error : std::string());
        std::function<void (const std::string&)> notify;
        {
            std::lock_guard<std::mutex> g (lock);
            notify = listener;
        }
        if (notify)
            notify (handle->filePath());
    };
    if (now || path.empty())
        load();
    else
        std::thread (load).detach();
    return handle;
}

// --- the voice engine ---------------------------------------------------------------------------------------------------

namespace
{
constexpr int maxVoices = 256;
constexpr int block = 32; // control-rate block (envelopes, LFOs, filter coefficients)
constexpr float minRelease = 0.002f;

inline float centsToRatio (float cents) noexcept { return std::exp2 (cents / 1200.0f); }
inline float timecentsToSeconds (float tc) noexcept { return tc < -11950.0f ? 0.0f : std::exp2 (tc / 1200.0f); }
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

// windowed-sinc tables (8 and 16 taps, 512 phases), built once off the audio thread
struct SincTable
{
    static constexpr int phases = 512;
    int taps = 8;
    std::vector<float> c; // phases+1 rows of taps

    void build (int t)
    {
        taps = t;
        c.assign ((std::size_t) (phases + 1) * (std::size_t) taps, 0.0f);
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / phases;
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const double x = (double) (k - taps / 2 + 1) - frac; // tap k sits at offset (k − taps/2 + 1)
                const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (3.14159265358979 * x) / (3.14159265358979 * x);
                const double w = 0.5 * (1.0 + std::cos (3.14159265358979 * x / (taps / 2.0))); // Hann window
                const double v = std::abs (x) >= taps / 2.0 ? 0.0 : sinc * w;
                c[(std::size_t) p * (std::size_t) taps + (std::size_t) k] = (float) v;
                sum += v;
            }
            for (int k = 0; k < taps; ++k)
                c[(std::size_t) p * (std::size_t) taps + (std::size_t) k] = (float) (c[(std::size_t) p * (std::size_t) taps + (std::size_t) k] / sum);
        }
    }
};

const SincTable& sinc8()
{
    static const SincTable t = [] { SincTable s; s.build (8); return s; }();
    return t;
}
const SincTable& sinc16()
{
    static const SincTable t = [] { SincTable s; s.build (16); return s; }();
    return t;
}

// SF2 volume envelope: attack linear in amplitude (or dB-linear: "EMU"), decay / release linear in dB over 100 dB
struct AmpEnv
{
    enum Stage : std::uint8_t { delay, attack, hold, decay, sustain, release, done };
    Stage stage = done;
    double t = 0;              // seconds into the stage
    float delayS = 0, attackS = 0, holdS = 0, decayS = 0, releaseS = minRelease;
    float sustainDb = 0;       // attenuation at sustain (≥ 0)
    float db = 100;            // current attenuation (decay / sustain / release)
    float attackLevel = 0;     // current amplitude in the attack
    bool emu = false;

    void start (float d, float a, float h, float dc, float sGain, float r, bool emuCurve) noexcept
    {
        delayS = d, attackS = a, holdS = h, decayS = dc, releaseS = std::max (minRelease, r);
        sustainDb = sGain <= 1.0e-5f ? 100.0f : std::clamp (-20.0f * std::log10 (sGain), 0.0f, 100.0f);
        emu = emuCurve;
        stage = delay, t = 0, db = 100, attackLevel = 0;
        advance (0);
    }

    float gain() const noexcept
    {
        switch (stage)
        {
            case delay: return 0.0f;
            case attack: return attackLevel;
            case done: return 0.0f;
            default: return db >= 100.0f ? 0.0f : dbToGain (-db);
        }
    }

    void noteOff() noexcept
    {
        if (stage == done || stage == release)
            return;
        if (stage == delay)
            db = 100.0f;
        else if (stage == attack)
            db = attackLevel <= 1.0e-5f ? 100.0f : -20.0f * std::log10 (attackLevel);
        stage = release, t = 0;
    }

    void fastRelease (float seconds) noexcept
    {
        noteOff();
        if (stage == release)
            releaseS = std::min (releaseS, std::max (0.0005f, seconds));
    }

    // advances by dt seconds
    void advance (float dt) noexcept
    {
        t += dt;
        for (int guard = 0; guard < 8; ++guard)
            switch (stage)
            {
                case delay:
                    if (t < delayS)
                        return;
                    t -= delayS, stage = attack;
                    break;
                case attack:
                    if (attackS > 0.0f && t < attackS)
                    {
                        const auto x = (float) (t / attackS);
                        attackLevel = emu ? dbToGain (-96.0f * (1.0f - x)) : x;
                        return;
                    }
                    t -= attackS, stage = hold, db = 0, attackLevel = 1;
                    break;
                case hold:
                    if (t < holdS)
                        return;
                    t -= holdS, stage = decay;
                    break;
                case decay:
                {
                    db = decayS > 0.0f ? (float) (100.0 * t / decayS) : 100.0f;
                    if (db < sustainDb)
                        return;
                    db = sustainDb, stage = sustainDb >= 100.0f ? done : sustain;
                    return;
                }
                case sustain:
                    return;
                case release:
                {
                    db += (float) (100.0 * dt / releaseS);
                    dt = 0;
                    if (db >= 100.0f)
                        stage = done;
                    return;
                }
                case done:
                    return;
            }
    }
};

// SF2 modulation envelope: linear, 0…1
struct ModEnv
{
    enum Stage : std::uint8_t { delay, attack, hold, decay, sustain, release, done };
    Stage stage = done;
    double t = 0;
    float delayS = 0, attackS = 0, holdS = 0, decayS = 0, releaseS = minRelease, sustainL = 1, level = 0;

    void start (float d, float a, float h, float dc, float s, float r) noexcept
    {
        delayS = d, attackS = a, holdS = h, decayS = dc, sustainL = std::clamp (s, 0.0f, 1.0f), releaseS = std::max (minRelease, r);
        stage = delay, t = 0, level = 0;
        advance (0);
    }

    void noteOff() noexcept
    {
        if (stage != done && stage != release)
            stage = release, t = 0;
    }

    void advance (float dt) noexcept
    {
        t += dt;
        for (int guard = 0; guard < 8; ++guard)
            switch (stage)
            {
                case delay:
                    if (t < delayS)
                        return;
                    t -= delayS, stage = attack;
                    break;
                case attack:
                    if (attackS > 0.0f && t < attackS)
                    {
                        level = (float) (t / attackS);
                        return;
                    }
                    t -= attackS, stage = hold, level = 1;
                    break;
                case hold:
                    if (t < holdS)
                        return;
                    t -= holdS, stage = decay;
                    break;
                case decay:
                    level = decayS > 0.0f ? 1.0f - (float) (t / decayS) : sustainL;
                    if (level > sustainL)
                        return;
                    level = sustainL, stage = sustain;
                    return;
                case sustain:
                    return;
                case release:
                    level -= (float) (dt / releaseS);
                    dt = 0;
                    if (level <= 0.0f)
                        level = 0, stage = done;
                    return;
                case done:
                    return;
            }
    }
};

// triangle LFO after a delay, −1…1
struct Lfo
{
    float delayS = 0, hz = 8.176f;
    double t = 0, phase = 0;
    void start (float d, float f) noexcept { delayS = d, hz = f, t = 0, phase = 0; }
    float value() const noexcept
    {
        if (t < delayS)
            return 0.0f;
        const auto p = (float) phase; // 0…1: 0 → 1 → −1 → 0
        return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
    }
    void advance (float dt) noexcept
    {
        t += dt;
        if (t > delayS)
        {
            phase += (double) hz * dt;
            phase -= std::floor (phase);
        }
    }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    bool on = false;
    void lowpass (float hz, float q, float rate) noexcept
    {
        hz = std::clamp (hz, 10.0f, rate * 0.45f);
        const auto w = 2.0f * 3.14159265f * hz / rate;
        const auto alpha = std::sin (w) / (2.0f * std::max (q, 0.3f));
        const auto cw = std::cos (w);
        const auto a0 = 1.0f + alpha;
        b0 = (1.0f - cw) * 0.5f / a0, b1 = (1.0f - cw) / a0, b2 = b0;
        a1 = -2.0f * cw / a0, a2 = (1.0f - alpha) / a0;
    }
    float process (float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Voice
{
    bool active = false, released = false, held = false /* by the pedal */, dying = false;
    const Region* region = nullptr;
    int key = 60, velocity = 100, presetIndex = -1;
    std::uint32_t noteId = 0; // the LYNX note that started it (round 190ej)
    std::uint64_t age = 0;
    double pos = 0;
    bool looping = false, loopUntilRelease = false;
    AmpEnv amp;
    ModEnv mod;
    Lfo modLfo, vibLfo;
    Biquad filter;
    float baseCents = 0, gainBase = 1, panL = 1, panR = 1, reverb = 0, chorus = 0;
    float lastGain = 0;
    float slideCents = 0, slideStep = 0; int slideLeft = 0;
    float restAtStart = 0; // the channel's fine tune + main pitch in baseCents (they move it live: setChannelPitchNow)
    float filterBaseCents = 13500, filterQ = 0.7071f;
    bool oneShot = false;
};

// Freeverb-style stereo reverb (mono in), buffers sized in prepare()
class Reverb
{
public:
    void prepare (double rate)
    {
        static const int combs[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int alls[4] = { 556, 441, 341, 225 };
        const auto scale = rate / 44100.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 8; ++i)
                comb[ch][i].assign ((std::size_t) std::max (8, (int) ((combs[i] + ch * 23) * scale)), 0.0f);
            for (int i = 0; i < 4; ++i)
                all[ch][i].assign ((std::size_t) std::max (8, (int) ((alls[i] + ch * 23) * scale)), 0.0f);
        }
        reset();
    }
    void reset() noexcept
    {
        for (auto& ch : comb)
            for (auto& c : ch)
                std::fill (c.begin(), c.end(), 0.0f);
        for (auto& ch : all)
            for (auto& a : ch)
                std::fill (a.begin(), a.end(), 0.0f);
        cp = {}, ap = {}, damp = {};
    }
    void process (const float* in, float* outL, float* outR, int n) noexcept
    {
        constexpr float feedback = 0.84f, damping = 0.25f, inputGain = 0.015f;
        for (int i = 0; i < n; ++i)
        {
            const auto x = in[i] * inputGain;
            for (int ch = 0; ch < 2; ++ch)
            {
                float acc = 0;
                for (int k = 0; k < 8; ++k)
                {
                    auto& buf = comb[ch][k];
                    auto& p = cp[ch][k];
                    const auto y = buf[(std::size_t) p];
                    auto& d = damp[ch][k];
                    d = y * (1.0f - damping) + d * damping;
                    buf[(std::size_t) p] = x + d * feedback;
                    if (++p >= (int) buf.size())
                        p = 0;
                    acc += y;
                }
                for (int k = 0; k < 4; ++k)
                {
                    auto& buf = all[ch][k];
                    auto& p = ap[ch][k];
                    const auto b = buf[(std::size_t) p];
                    buf[(std::size_t) p] = acc + b * 0.5f;
                    acc = b - acc;
                    if (++p >= (int) buf.size())
                        p = 0;
                }
                (ch == 0 ? outL : outR)[i] += acc;
            }
        }
    }

private:
    std::vector<float> comb[2][8], all[2][4];
    std::array<std::array<int, 8>, 2> cp {};
    std::array<std::array<int, 4>, 2> ap {};
    std::array<std::array<float, 8>, 2> damp {};
};

// two modulated delay lines (L / R in quadrature), mono in
class Chorus
{
public:
    void prepare (double r)
    {
        rate = (float) r;
        line.assign ((std::size_t) (0.05 * r) + 4, 0.0f);
        reset();
    }
    void reset() noexcept
    {
        std::fill (line.begin(), line.end(), 0.0f);
        w = 0, phase = 0;
    }
    void process (const float* in, float* outL, float* outR, int n) noexcept
    {
        if (line.size() < 8)
            return;
        const auto size = (int) line.size();
        const auto inc = 0.6f / rate;
        for (int i = 0; i < n; ++i)
        {
            line[(std::size_t) w] = in[i];
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto ph = phase + (ch == 0 ? 0.0f : 0.25f);
                const auto s = std::sin (2.0f * 3.14159265f * ph);
                const auto delay = (0.012f + 0.004f * s) * rate;
                auto rp = (float) w - delay;
                while (rp < 0)
                    rp += (float) size;
                const auto i0 = (int) rp;
                const auto f = rp - (float) i0;
                const auto a = line[(std::size_t) (i0 % size)], b = line[(std::size_t) ((i0 + 1) % size)];
                (ch == 0 ? outL : outR)[i] += (a + (b - a) * f) * 0.7f;
            }
            if (++w >= size)
                w = 0;
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }
    }

private:
    std::vector<float> line;
    int w = 0;
    float phase = 0, rate = 44100;
};

// lock-free "latest wins" triple buffer of the settings
struct Settings
{
    std::array<float, paramCount> p {};
};

class SettingsBuffer
{
public:
    void write (const Settings& s) noexcept
    {
        slots[back] = s;
        back = middle.exchange (back | dirty, std::memory_order_acq_rel) & 3;
    }
    // audio thread: true when newer settings arrived (then `current()` holds them)
    bool update() noexcept
    {
        if ((middle.load (std::memory_order_relaxed) & dirty) == 0)
            return false;
        front = middle.exchange (front, std::memory_order_acq_rel) & 3;
        return true;
    }
    const Settings& current() const noexcept { return slots[front]; }

private:
    static constexpr int dirty = 4;
    Settings slots[3];
    int back = 0, front = 1;
    std::atomic<int> middle { 2 };
};

class SoundFontInstrument final : public Instrument
{
public:
    explicit SoundFontInstrument (std::shared_ptr<FontHandle> h) : handle (std::move (h))
    {
        (void) sinc8(), (void) sinc16();
        Settings s;
        const auto d = defaultParams();
        for (int i = 0; i < paramCount; ++i)
            s.p[(std::size_t) i] = d[(std::size_t) i];
        settings.write (s);
        settings.update();
        sendL.resize (4096), sendR.resize (4096), monoRev.resize (4096), monoCho.resize (4096);
        for (auto& a : aux)
            for (auto& side : a)
                side.assign (auxCapacity, 0.0f);
    }

    // "Send to" a mixer insert (FL: the reverb / chorus send bus, dry, added into that insert): two extra outputs
    void beginChunk (int n) noexcept override
    {
        cursor = 0;
        for (auto& a : aux)
            for (auto& side : a)
                std::fill (side.begin(), side.begin() + std::clamp<std::ptrdiff_t> (n, 0, (std::ptrdiff_t) auxCapacity), 0.0f);
    }
    int auxOutputCount() const noexcept override { return 2; }
    int auxOutputTrack (int k) const noexcept override { return k == 0 || k == 1 ? auxTrack[(std::size_t) k] : -1; }
    const float* auxOutputData (int k, int side) const noexcept override
    {
        return k == 0 || k == 1 ? aux[(std::size_t) k][(std::size_t) (side & 1)].data() : nullptr;
    }

    void prepare (double sampleRate) noexcept override
    {
        rate = (float) std::max (1000.0, sampleRate);
        reverb.prepare (rate);
        chorus.prepare (rate);
        for (auto& v : voices)
            v.active = false;
        if ((offlineRendering() || renderInProgress()) && handle != nullptr)
            handle->wait(); // an export waits for the font
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        Settings s;
        s.p = readParams (channel.modelParams);
        settings.write (s);
        polyLimit.store (channel.maxPolyphony > 0 ? channel.maxPolyphony : 0, std::memory_order_relaxed);
    }

    void setParameter (int index, float value) noexcept override
    {
        if (index >= 0 && index < paramCount)
        {
            automated[(std::size_t) index].store (value, std::memory_order_relaxed);
            automatedMask.fetch_or (1ull << index, std::memory_order_relaxed);
        }
    }

    void noteOn (int key, float velocity) noexcept override
    {
        pending.push ({ key, velocity, true, nextPan, nextCents, startingNoteId() });
        nextPan = 0, nextCents = 0;
    }
    // (round 190ej: the note's own voices — FL's SoundFont Player plays on the other note of the key [EXEC: /R render])
    void noteOff (int key) noexcept override { pending.push ({ key, 0.0f, false, 0, 0, endingNoteId() }); }
    void setNextNoteExpression (float pan, float cents, bool) noexcept override { nextPan = pan, nextCents = cents; }
    // The channel's PITCH knob (issue #73: an FL project's SoundFont Player detuned −400 cents played undetuned) and its
    // fine tune + main pitch, before every render. FL's generators follow both on the sounding notes (round 167a): the
    // knob is added to every voice, the rest is in the note's cents and moves by how much it changed since.
    void setChannelPitchNow (float knob, float rest) noexcept override
    {
        knobCents = std::isfinite (knob) ? knob : 0.0f;
        restCents = std::isfinite (rest) ? rest : 0.0f;
    }

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            if (v.active)
            {
                v.held = false;
                release (v);
            }
        sustainPedal = false;
        pending.clear();
    }

    void panic() noexcept override
    {
        for (auto& v : voices)
            v.active = false;
        pending.clear();
        sustainPedal = false;
        reverb.reset();
        chorus.reset();
    }

    bool followsSlides() const noexcept override { return true; } // (round 168l: a Mono channel's notes)
    void slideTo (int key, float cents, float samples) noexcept override
    {
        // the sounding voices glide to the new key (FL's slide notes)
        for (auto& v : voices)
            if (v.active && ! v.released)
            {
                const auto target = (float) (key - v.key) * 100.0f + cents;
                const auto steps = std::max (1, (int) (samples / (float) block));
                v.slideStep = (target - v.slideCents) / (float) steps;
                v.slideLeft = steps;
            }
    }

    void pitchBend (float) noexcept override {} // (it bends by the raw MIDI pitch bend, midiMessage)

    void midiMessage (const std::uint8_t* m, int size) noexcept override
    {
        if (size < 2)
            return;
        const auto status = m[0] & 0xF0;
        if (status == 0xB0 && size >= 3)
            controller (m[1], m[2]);
        else if (status == 0xE0 && size >= 3)
            bend = std::clamp (((m[2] << 7) | m[1]) - 8192, -8192, 8191) / 8192.0f;
        else if (status == 0xC0)
            programOverride = m[1] & 127, bankOverride = bankSelect;
        else if (status == 0xD0)
            pressure = (float) (m[1] & 127) / 127.0f;
    }

    bool idle() const noexcept override
    {
        if (tail > 0)
            return false;
        for (const auto& v : voices)
            if (v.active)
                return false;
        return pending.count == 0;
    }

    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override
    {
        ScopedFlushDenormals noDenormals;
        // An export waits for the font here too (issue #138): the export's engine prepares its instruments before it
        // switches to offline rendering, so the wait in prepare() never ran for a project without hosted plugins, and
        // the notes played before the loader thread finished were dropped below: a short export from the start of the
        // song came out silent or missing its first notes. (Live playback never waits: the flag is only set by renders.)
        // (renderInProgress(): the render-quality flag is off in an export without "HQ for all plugins", round 190ba)
        if (handle != nullptr && ! handle->finished() && (offlineRendering() || renderInProgress()))
            handle->wait();
        const auto* font = handle != nullptr ? handle->font() : nullptr;
        if (font != lastFont)
        {
            for (auto& v : voices)
                v.active = false;
            lastFont = font;
            lastSwitch = font != nullptr ? font->switchDefault : -1; // (an SFZ's sw_default)
            seqCounters.fill (0);
        }
        if (settings.update())
            params = settings.current().p;
        if (const auto mask = automatedMask.exchange (0, std::memory_order_relaxed); mask != 0)
            for (int i = 0; i < paramCount; ++i)
                if ((mask >> i) & 1)
                    autoValue[(std::size_t) i] = automated[(std::size_t) i].load (std::memory_order_relaxed), autoSet |= 1ull << i;
        if (font == nullptr)
        {
            pending.clear();
            return;
        }
        for (int i = 0; i < pending.count; ++i)
        {
            const auto& e = pending.events[(std::size_t) i];
            if (e.on)
            {
                startingId = e.id;
                startNote (*font, e.key, e.velocity, e.pan, e.cents);
                startingId = 0;
            }
            else
            {
                stopNote (e.key, e.id);
                startReleaseRegions (*font, e.key);
            }
        }
        pending.clear();

        for (int done = 0; done < n;)
        {
            const auto len = std::min (block, n - done);
            renderBlock (*font, left + done, right + done, len, gainLeft, gainRight);
            done += len;
        }
    }

    int voiceCount() const noexcept
    {
        int c = 0;
        for (const auto& v : voices)
            c += v.active ? 1 : 0;
        return c;
    }

private:
    struct NoteMsg { int key; float velocity; bool on; float pan, cents; std::uint32_t id; };
    struct Pending
    {
        std::array<NoteMsg, 256> events {};
        int count = 0;
        void push (const NoteMsg& m) noexcept
        {
            if (count < (int) events.size())
                events[(std::size_t) count++] = m;
        }
        void clear() noexcept { count = 0; }
    };

    float param (int i) const noexcept
    {
        return ((autoSet >> i) & 1) != 0 ? denormalise (i, autoValue[(std::size_t) i]) : params[(std::size_t) i];
    }

    void controller (int cc, int value) noexcept
    {
        const auto v = (float) value / 127.0f;
        switch (cc)
        {
            case 0: bankSelect = value; break;
            case 1: modWheel = v; break;
            case 6:
                if (rpn == 0)
                    rpnBend = (float) value;
                break;
            case 38:
                if (rpn == 0 && rpnBend >= 0)
                    rpnBend = std::floor (rpnBend) + (float) value / 100.0f;
                break;
            case 7: ccVolume = v; break;
            case 10: ccPan = v * 2.0f - 1.0f; break;
            case 11: ccExpression = v; break;
            case 64:
                sustainPedal = value >= 64;
                if (! sustainPedal)
                    for (auto& voice : voices)
                        if (voice.active && voice.held)
                        {
                            voice.held = false;
                            release (voice);
                        }
                break;
            case 91: ccReverb = v; break;
            case 93: ccChorus = v; break;
            case 100: rpnLsb = value, rpn = rpnMsb == 0 && rpnLsb == 0 ? 0 : -1; break;
            case 101: rpnMsb = value, rpn = rpnMsb == 0 && rpnLsb == 0 ? 0 : -1; break;
            case 120: // all sound off
                for (auto& voice : voices)
                    voice.active = false;
                break;
            case 121: // reset all controllers
                modWheel = 0, ccExpression = 1, ccPan = 0, bend = 0, pressure = 0, sustainPedal = false, rpnBend = -1;
                break;
            case 123: case 124: case 125: case 126: case 127:
                allNotesOff();
                break;
            default: break;
        }
    }

    int currentPreset (const Font& font) const noexcept
    {
        const auto bank = bankOverride >= 0 ? bankOverride : (int) param (pBank);
        const auto program = programOverride >= 0 ? programOverride : (int) param (pProgram);
        return font.findPreset (bank, program);
    }

    float velocityGain (int vel) const noexcept
    {
        const auto v = std::clamp ((float) vel / 127.0f, 0.0f, 1.0f);
        switch ((int) param (pVelocityCurve))
        {
            case 1: return v;                        // linear amplitude
            case 2: return std::sqrt (v);            // soft
            case 3: return v * v * v;                // hard
            case 4: return 1.0f;                     // fixed
            default: return v * v;                   // SF2 default: 960 cB concave, 40·log10(v) dB
        }
    }

    void release (Voice& v) noexcept
    {
        if (v.released)
            return;
        v.released = true;
        v.amp.noteOff();
        v.mod.noteOff();
        if (v.loopUntilRelease)
            v.looping = false; // plays on to the end of the sample
    }

    void kill (Voice& v, float seconds) noexcept
    {
        v.dying = true;
        v.released = true;
        v.amp.fastRelease (seconds);
        v.mod.noteOff();
    }

    Voice* freeVoice() noexcept
    {
        Voice* best = nullptr;
        for (auto& v : voices)
            if (! v.active)
                return &v;
        // all busy: the quietest dying voice, else the oldest
        for (auto& v : voices)
            if (best == nullptr || (v.dying && ! best->dying) || (v.dying == best->dying && v.age < best->age))
                best = &v;
        return best;
    }

    void startNote (const Font& font, int key, float velocity01, float notePan, float noteCents) noexcept
    {
        const auto presetIndex = currentPreset (font);
        if (presetIndex < 0)
            return;
        const auto& preset = font.presets[(std::size_t) presetIndex];
        const auto transposed = std::clamp (key + (int) param (pTranspose), 0, 127);
        const auto vel = std::clamp ((int) std::lround (velocity01 * 127.0f), 1, 127);
        const auto mode = (int) param (pProgramMode);
        const auto drums = mode == 2 || (mode == 0 && preset.bank == 128);
        noteVelocity[(std::size_t) std::clamp (key, 0, 127)] = (std::uint8_t) vel;
        if (font.switchLo >= 0 && transposed >= font.switchLo && transposed <= font.switchHi)
            lastSwitch = transposed; // an SFZ key switch (sw_last)
        const auto random = nextRandom(); // one per note: SFZ's lorand / hirand

        // exclusive classes of this note's regions stop the voices of the same preset with that class
        for (const auto& r : preset.regions)
            if (r.exclusiveClass != 0 && transposed >= r.loKey && transposed <= r.hiKey && vel >= r.loVel && vel <= r.hiVel)
                for (auto& v : voices)
                    if (v.active && v.presetIndex == presetIndex && v.region != nullptr && v.region->exclusiveClass == r.exclusiveClass
                        && ! v.dying)
                        kill (v, 0.004f);

        // the same key retriggered while held by the pedal: the old voice fades
        for (auto& v : voices)
            if (v.active && v.key == key && v.held)
            {
                v.held = false;
                release (v);
            }

        const auto limit = std::min ((int) param (pPolyphony), polyLimit.load (std::memory_order_relaxed) > 0 ? polyLimit.load (std::memory_order_relaxed) : maxVoices);
        for (std::size_t ri = 0; ri < preset.regions.size(); ++ri)
        {
            const auto& r = preset.regions[ri];
            if (transposed < r.loKey || transposed > r.hiKey || vel < r.loVel || vel > r.hiVel)
                continue;
            if (r.sfz && ! sfzRegionPlays (r, ri, random, false))
                continue;
            // polyphony: the oldest living voice makes room
            int living = 0;
            for (const auto& v : voices)
                living += v.active && ! v.dying ? 1 : 0;
            while (living >= limit)
            {
                Voice* oldest = nullptr;
                for (auto& v : voices)
                    if (v.active && ! v.dying && (oldest == nullptr || v.age < oldest->age))
                        oldest = &v;
                if (oldest == nullptr)
                    break;
                kill (*oldest, 0.005f);
                --living;
            }
            auto* v = freeVoice();
            if (v == nullptr)
                return;
            startVoice (*v, r, presetIndex, key, transposed, vel, notePan, noteCents, drums);
        }
    }

    // --- SFZ (core/Sfz.h, round 167o) -------------------------------------------------------------------------------

    std::uint32_t nextRandom() noexcept
    {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        return rng;
    }

    // Whether an SFZ region that matches the key and velocity plays now: its trigger, key switch, random range and
    // round robin (the counter only moves for the notes that reach it, as sfizz does).
    bool sfzRegionPlays (const Region& r, std::size_t index, std::uint32_t random, bool atRelease) noexcept
    {
        if (r.releaseTrigger != atRelease)
            return false;
        if (r.switchLast >= 0 && r.switchLast != lastSwitch)
            return false;
        const auto x = (float) (random >> 8) * (1.0f / 16777216.0f);
        if (x < r.loRand || (x >= r.hiRand && r.hiRand < 1.0f))
            return false;
        if (r.seqLength > 1)
        {
            auto& counter = seqCounters[index % seqCounters.size()];
            const auto at = (int) (counter++ % (std::uint32_t) r.seqLength);
            if (at != r.seqPosition - 1)
                return false;
        }
        return true;
    }

    // trigger=release regions: they start when the key is let go, with the velocity it was played with
    void startReleaseRegions (const Font& font, int key) noexcept
    {
        const auto presetIndex = currentPreset (font);
        if (presetIndex < 0)
            return;
        const auto& preset = font.presets[(std::size_t) presetIndex];
        const auto transposed = std::clamp (key + (int) param (pTranspose), 0, 127);
        const auto vel = std::max (1, (int) noteVelocity[(std::size_t) std::clamp (key, 0, 127)]);
        const auto random = nextRandom();
        for (std::size_t ri = 0; ri < preset.regions.size(); ++ri)
        {
            const auto& r = preset.regions[ri];
            if (! r.releaseTrigger || transposed < r.loKey || transposed > r.hiKey || vel < r.loVel || vel > r.hiVel)
                continue;
            if (! sfzRegionPlays (r, ri, random, true))
                continue;
            auto* v = freeVoice();
            if (v == nullptr)
                return;
            startVoice (*v, r, presetIndex, key, transposed, vel, 0.0f, 0.0f, false);
            v->oneShot = true;    // (the key is already up: it plays its sample out, a later note-off doesn't stop it)
            v->looping = false;
        }
    }

    void startVoice (Voice& v, const Region& r, int presetIndex, int key, int playedKey, int vel, float notePan, float noteCents, bool drums) noexcept
    {
        if (r.sfz && r.group != 0) // SFZ: this region's group stops the voices whose off_by is that group
            for (auto& other : voices)
                if (other.active && ! other.dying && other.region != nullptr && other.region->sfz && other.region->offBy == r.group)
                {
                    if (other.region->offFast)
                        kill (other, 0.006f);
                    else
                        release (other);
                }
        v = Voice {};
        v.active = true;
        v.region = &r;
        v.key = key, v.velocity = vel, v.presetIndex = presetIndex;
        v.noteId = startingId;
        v.age = ++clock;
        v.oneShot = drums || r.oneShot;
        v.pos = r.start;
        const auto loops = param (pUseLoops) >= 0.5f;
        v.looping = loops && r.loop != LoopMode::none;
        v.loopUntilRelease = loops && r.loop == LoopMode::untilRelease;
        v.baseCents = (float) (playedKey - r.rootKey) * (float) r.keyTrack + (float) r.transpose * 100.0f + (float) r.tune + noteCents;
        v.restAtStart = restCents;

        // envelopes: the region's, the overrides when their knob is above the minimum
        const auto keyAdjust = [playedKey] (float value, float perKey) { return perKey != 0.0f ? timecentsToSeconds (value + perKey * (float) (60 - playedKey)) : value; };
        auto attack = r.ampEnv.attack, decay = keyAdjust (r.ampEnv.decay, r.ampEnv.keynumToDecay), sustainGain = r.ampEnv.sustain, rel = r.ampEnv.release;
        const auto hold = keyAdjust (r.ampEnv.hold, r.ampEnv.keynumToHold);
        if (param (pAttack) > 0.0f) attack = knobSeconds (param (pAttack));
        if (param (pDecay) > 0.0f) decay = knobSeconds (param (pDecay));
        if (param (pSustain) > 0.0f) sustainGain = param (pSustain);
        if (param (pRelease) > 0.0f) rel = knobSeconds (param (pRelease));
        v.amp.start (r.ampEnv.delay, attack, hold, decay, sustainGain, rel, param (pEmuEnvelope) >= 0.5f);
        v.mod.start (r.modEnv.delay, r.modEnv.attack, keyAdjust (r.modEnv.hold, r.modEnv.keynumToHold),
                     keyAdjust (r.modEnv.decay, r.modEnv.keynumToDecay), r.modEnv.sustain, r.modEnv.release);
        v.modLfo.start (r.delayModLfo, 8.176f * centsToRatio ((float) r.freqModLfo));
        if (param (pLfoAmount) > 0.0f || param (pLfoSpeed) > 0.0f || param (pLfoDelay) > 0.0f)
            v.vibLfo.start (param (pLfoDelay) > 0.0f ? knobSeconds (param (pLfoDelay)) : r.delayVibLfo,
                            param (pLfoSpeed) > 0.0f ? knobLfoHz (param (pLfoSpeed)) : 8.176f * centsToRatio ((float) r.freqVibLfo));
        else
            v.vibLfo.start (r.delayVibLfo, 8.176f * centsToRatio ((float) r.freqVibLfo));

        // level: attenuation (EMU's 0.4 scaling of centibels, as SoundFont players do), velocity, the region's pan
        const auto sfzVelocity = r.sfz && (int) param (pVelocityCurve) == 0; // SFZ's amp_veltrack curve, unless overridden
        const auto v01 = (float) vel / 127.0f;
        v.gainBase = dbToGain (-0.04f * r.attenuation) * (sfzVelocity ? 1.0f - r.ampVelTrack + r.ampVelTrack * v01 * v01 : velocityGain (vel));
        const auto pan = std::clamp (r.pan * 2.0f + notePan, -1.0f, 1.0f);
        const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
        v.panL = std::cos (angle) * 1.41421356f, v.panR = std::sin (angle) * 1.41421356f;
        v.reverb = r.reverbSend, v.chorus = r.chorusSend;

        // filter: the velocity → cut-off default modulator (−2400 cents at velocity 0) when on
        v.filterBaseCents = (float) r.filterFc;
        if (param (pCutoff) > 0.0f)
            v.filterBaseCents = (float) knobCutoffCents (param (pCutoff));
        if (param (pVelocityToCutoff) >= 0.5f)
            v.filterBaseCents -= 2400.0f * (1.0f - (float) vel / 127.0f);
        const auto qDb = param (pResonance) > 0.0f ? param (pResonance) * 24.0f : (float) r.filterQ / 10.0f;
        v.filterQ = std::pow (10.0f, qDb / 20.0f) * 0.7071f;
        v.filter.on = v.filterBaseCents < 13450.0f || qDb > 0.0f || r.modEnvToFilterFc != 0 || r.modLfoToFilterFc != 0 || param (pCutoffMod) > 0.0f;
    }

    std::uint32_t startingId = 0; // (the note id startNote gives its voices)

    // `id` ≠ 0: only that note's voices (round 190ej)
    void stopNote (int key, std::uint32_t id = 0) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.key == key && ! v.released && (id == 0 || v.noteId == id))
            {
                if (v.oneShot && ! v.looping)
                    continue; // drums: the note-off is ignored, the sample plays out
                if (sustainPedal)
                    v.held = true;
                else
                    release (v);
            }
    }

    float fetch (const Font& font, const Voice& v, std::int64_t i) const noexcept
    {
        const auto& r = *v.region;
        if (v.looping && i >= (std::int64_t) r.loopEnd)
            i = (std::int64_t) r.loopStart + (i - (std::int64_t) r.loopStart) % (std::int64_t) (r.loopEnd - r.loopStart);
        if (i < (std::int64_t) r.start || i >= (std::int64_t) r.end)
            return 0.0f;
        return (float) font.samples[(std::size_t) i] * (1.0f / 32768.0f);
    }

    float interpolate (const Font& font, const Voice& v, double pos, int quality) const noexcept
    {
        const auto i = (std::int64_t) pos;
        const auto f = (float) (pos - (double) i);
        if (quality <= 0)
        {
            const auto a = fetch (font, v, i), b = fetch (font, v, i + 1);
            return a + (b - a) * f;
        }
        if (quality == 1)
        {
            const auto y0 = fetch (font, v, i - 1), y1 = fetch (font, v, i), y2 = fetch (font, v, i + 1), y3 = fetch (font, v, i + 2);
            // 4-point Hermite
            const auto c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }
        const auto& table = quality == 2 ? sinc8() : sinc16();
        const auto row = std::min (SincTable::phases, (int) (f * SincTable::phases + 0.5f));
        const auto* c = table.c.data() + (std::size_t) row * (std::size_t) table.taps;
        float acc = 0;
        for (int k = 0; k < table.taps; ++k)
            acc += c[k] * fetch (font, v, i + k - table.taps / 2 + 1);
        return acc;
    }

    void renderBlock (const Font& font, float* left, float* right, int n, float gainLeft, float gainRight) noexcept
    {
        const auto dt = (float) n / rate;
        const auto quality = (int) param (pResampling);
        const auto bendCents = bend * (rpnBend >= 0 ? rpnBend : param (pBendRange)) * 100.0f;
        const auto master = dbToGain (param (pVolume)) * ccVolume * ccVolume * ccExpression * ccExpression;
        const auto revMult = levelMultiplier (param (pReverbAmount)), choMult = levelMultiplier (param (pChorusAmount));
        const auto revOverride = param (pReverbOverride) >= 0.5f, choOverride = param (pChorusOverride) >= 0.5f;
        const auto lfoOverride = param (pLfoAmount) > 0.0f;
        const auto lfoDepth = knobLfoCents (param (pLfoAmount));
        const auto cutMod = param (pCutoffMod);
        const auto ccPanL = std::sqrt (std::clamp (1.0f - ccPan, 0.0f, 1.0f)), ccPanR = std::sqrt (std::clamp (1.0f + ccPan, 0.0f, 1.0f));
        std::fill (monoRev.begin(), monoRev.begin() + n, 0.0f);
        std::fill (monoCho.begin(), monoCho.begin() + n, 0.0f);
        bool anySend = false;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            const auto& r = *v.region;
            // control rate: envelopes, LFOs, pitch, filter
            const auto modLevel = v.mod.level;
            const auto modLfo = v.modLfo.value(), vibLfo = v.vibLfo.value();
            if (v.slideLeft > 0)
                v.slideCents += v.slideStep, --v.slideLeft;
            auto cents = v.baseCents + knobCents + (restCents - v.restAtStart) + v.slideCents + bendCents + modLevel * (float) r.modEnvToPitch + modLfo * (float) r.modLfoToPitch;
            cents += vibLfo * ((lfoOverride ? lfoDepth : (float) r.vibLfoToPitch) + (std::max (modWheel, param (pModWheel)) + pressure) * 50.0f);
            const auto step = (double) centsToRatio (cents) * (double) r.sampleRate / (double) rate;
            if (v.filter.on)
            {
                const auto envToFc = cutMod > 0.0f ? cutMod * 4800.0f : (float) r.modEnvToFilterFc;
                const auto fc = v.filterBaseCents + modLevel * envToFc + modLfo * (float) r.modLfoToFilterFc;
                v.filter.lowpass (8.176f * centsToRatio (std::clamp (fc, 1500.0f, 13500.0f)), v.filterQ, rate);
            }
            const auto tremolo = r.modLfoToVolume != 0 ? dbToGain (-0.1f * modLfo * (float) r.modLfoToVolume) : 1.0f;
            v.amp.advance (dt);
            v.mod.advance (dt);
            v.modLfo.advance (dt);
            v.vibLfo.advance (dt);
            const auto target = v.amp.gain() * v.gainBase * tremolo * master;
            const auto g0 = v.lastGain;
            const auto gStep = (target - g0) / (float) n;
            v.lastGain = target;
            // sends: the preset's own (+ CC 91 / 93 as the SF2 default modulators: 20 %), times FL's level multiplier when
            // "custom level" is on (FL Soundfont Player: 0 .. 1 at the centre .. 20)
            const auto revSend = std::min (1.0f, (v.reverb + 0.2f * ccReverb) * (revOverride ? revMult : 1.0f));
            const auto choSend = std::min (1.0f, (v.chorus + 0.2f * ccChorus) * (choOverride ? choMult : 1.0f));
            anySend = anySend || revSend > 0.0f || choSend > 0.0f;

            auto pos = v.pos;
            auto g = g0;
            bool ended = false;
            for (int i = 0; i < n; ++i)
            {
                if (! v.looping && pos >= (double) r.end - 1.0)
                {
                    ended = true;
                    break;
                }
                auto s = interpolate (font, v, pos, quality);
                if (v.filter.on)
                    s = v.filter.process (s);
                g += gStep;
                const auto out = s * g;
                left[i] += out * v.panL * ccPanL * gainLeft;
                right[i] += out * v.panR * ccPanR * gainRight;
                monoRev[(std::size_t) i] += out * revSend;
                monoCho[(std::size_t) i] += out * choSend;
                pos += step;
                if (v.looping && pos >= (double) r.loopEnd)
                    pos -= (double) (r.loopEnd - r.loopStart);
            }
            v.pos = pos;
            if (ended || v.amp.stage == AmpEnv::done)
                v.active = false;
        }

        // "send to" a mixer insert: the dry send bus into that insert (the channel's gains applied)
        for (std::size_t k = 0; k < 2; ++k)
        {
            const auto track = (int) param (k == 0 ? pReverbTrack : pChorusTrack);
            auxTrack[k] = track;
            if (track < 0 || ! anySend)
                continue;
            const auto& mono = k == 0 ? monoRev : monoCho;
            auto& a = aux[k];
            for (int i = 0; i < n && cursor + i < (int) auxCapacity; ++i)
            {
                a[0][(std::size_t) (cursor + i)] += mono[(std::size_t) i] * gainLeft;
                a[1][(std::size_t) (cursor + i)] += mono[(std::size_t) i] * gainRight;
            }
        }
        cursor += n;

        // built-in reverb / chorus (the sends' levels follow the channel's gains)
        if (anySend || tail > 0)
        {
            const auto revOn = param (pReverbBuiltIn) >= 0.5f, choOn = param (pChorusBuiltIn) >= 0.5f;
            std::fill (sendL.begin(), sendL.begin() + n, 0.0f);
            std::fill (sendR.begin(), sendR.begin() + n, 0.0f);
            if (revOn)
                reverb.process (monoRev.data(), sendL.data(), sendR.data(), n);
            if (choOn)
                chorus.process (monoCho.data(), sendL.data(), sendR.data(), n);
            float peak = 0;
            for (int i = 0; i < n; ++i)
            {
                left[i] += sendL[(std::size_t) i] * gainLeft;
                right[i] += sendR[(std::size_t) i] * gainRight;
                peak = std::max (peak, std::abs (sendL[(std::size_t) i]) + std::abs (sendR[(std::size_t) i]));
            }
            // keep rendering while the tail rings (about 4 s after the last send)
            if (anySend)
                tail = (int) (rate * 4.0f);
            else
                tail = peak < 1.0e-6f ? 0 : std::max (0, tail - n);
        }
    }

    std::shared_ptr<FontHandle> handle;
    const Font* lastFont = nullptr;
    float rate = 44100;
    std::array<Voice, maxVoices> voices {};
    std::uint64_t clock = 0;
    // SFZ state: the last key switch, round-robin counters (per region), the velocity each key was played with
    int lastSwitch = -1;
    std::array<std::uint32_t, 4096> seqCounters {};
    std::array<std::uint8_t, 128> noteVelocity {};
    std::uint32_t rng = 0x9E3779B9u;
    Pending pending;
    float nextPan = 0, nextCents = 0;
    float knobCents = 0, restCents = 0; // setChannelPitchNow
    SettingsBuffer settings;
    std::array<float, paramCount> params {};
    std::array<std::atomic<float>, paramCount> automated {};
    std::atomic<std::uint64_t> automatedMask { 0 };
    std::array<float, paramCount> autoValue {};
    std::uint64_t autoSet = 0;
    std::atomic<int> polyLimit { 0 };
    // MIDI state
    float bend = 0, modWheel = 0, pressure = 0, ccVolume = 1, ccExpression = 1, ccPan = 0, ccReverb = 0, ccChorus = 0, rpnBend = -1;
    bool sustainPedal = false;
    int bankSelect = 0, bankOverride = -1, programOverride = -1, rpnMsb = 127, rpnLsb = 127, rpn = -1;
    // effects
    Reverb reverb;
    Chorus chorus;
    std::vector<float> sendL, sendR, monoRev, monoCho;
    int tail = 0;
    // "send to" outputs: [reverb, chorus][side], from the chunk's start
    static constexpr std::size_t auxCapacity = 16384;
    std::array<std::array<std::vector<float>, 2>, 2> aux;
    std::array<int, 2> auxTrack { -1, -1 };
    int cursor = 0;
};
} // namespace

std::unique_ptr<Instrument> makeInstrument (std::shared_ptr<FontHandle> font)
{
    return std::make_unique<SoundFontInstrument> (std::move (font));
}

int activeVoices (const Instrument& instrument) noexcept
{
    if (const auto* s = dynamic_cast<const SoundFontInstrument*> (&instrument))
        return s->voiceCount();
    return -1;
}
} // namespace lynx::sf
