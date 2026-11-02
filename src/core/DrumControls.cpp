#include "core/DrumControls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "core/BassDrum.h"
#include "core/DrumSynthLive.h"
#include "core/Drumaxx.h"
#include "core/FruitKick.h"
#include "core/PhysicalModels.h"
#include "core/Plucked.h"
#include "core/Slayer.h"

namespace lynx::drumcontrols
{
namespace
{
Control unit (const char* name, int index) { return { name, index, 0.0f, 1.0f, false }; }
Control flag (const char* name, int index) { return { name, index, 0.0f, 1.0f, true }; }
Control ranged (const char* name, int index, float low, float high) { return { name, index, low, high, true }; }
Control part (Control c)
{
    c.perPart = true;
    return c;
}

// Drumpad's 38 parameters (REA_rodada28 §2.1; tags of Drumpad's form). Drumaxx's pads hold the same set at +5.
std::vector<Control> drumpadVoice()
{
    return { flag ("Mallet retrig", 0), unit ("Mid retrig", 1), unit ("Mallet amp", 2), unit ("Mallet decay", 3), unit ("Mallet noise", 4),
             unit ("Mallet noise RP", 5), unit ("Membrane decay", 6), unit ("Membrane cutoff", 7), unit ("Membrane tension", 8),
             unit ("Membrane phase", 9), unit ("Membrane material", 10), unit ("Membrane size", 11), unit ("Membrane shape", 12),
             unit ("Low freq", 13), unit ("Low res", 14), unit ("SOF freq", 15), unit ("SOF decay", 16), unit ("SOF level", 17),
             unit ("Mid freq", 18), unit ("Mid level", 19), unit ("Mid res", 20), unit ("Mid freq mod", 21), unit ("Mid level mod", 22),
             unit ("Mid attack", 23), unit ("High freq", 24), unit ("High level", 25), unit ("High res", 26), unit ("Lo-fi", 27),
             unit ("Hold", 28), unit ("Pitch", 29), ranged ("Vel mod 1 target", 30, 0, 28), ranged ("Vel mod 2 target", 31, 0, 28),
             ranged ("Vel mod 3 target", 32, 0, 28), ranged ("Vel mod 4 target", 33, 0, 28), unit ("Vel mod 1 value", 34),
             unit ("Vel mod 2 value", 35), unit ("Vel mod 3 value", 36), unit ("Vel mod 4 value", 37) };
}

std::vector<Control> makeDrumaxx()
{
    std::vector<Control> out;
    for (auto c : drumpadVoice())
    {
        c.index += 5;
        out.push_back (part (c));
    }
    static std::vector<std::string> names; // (stable storage for the generated names)
    if (names.empty())
        for (int pad = 1; pad <= 16; ++pad)
            for (const auto* what : { "volume", "pan", "mute", "solo" })
                names.push_back ("Pad " + std::to_string (pad) + " " + what);
    for (int pad = 0; pad < 16; ++pad)
    {
        out.push_back (unit (names[(std::size_t) (4 * pad)].c_str(), 44 * pad));
        out.push_back (unit (names[(std::size_t) (4 * pad + 1)].c_str(), 44 * pad + 1));
        out.push_back (flag (names[(std::size_t) (4 * pad + 2)].c_str(), 44 * pad + 2));
        out.push_back (flag (names[(std::size_t) (4 * pad + 3)].c_str(), 44 * pad + 3));
    }
    // the master (REA_rodada67 §6.2): level, EQ on + 8 bands, limiter on, drive, release
    out.push_back (unit ("Master", 0x2c1));
    out.push_back (flag ("EQ on", 0x2c3));
    static const char* const bands[8] = { "EQ band 1", "EQ band 2", "EQ band 3", "EQ band 4", "EQ band 5", "EQ band 6", "EQ band 7", "EQ band 8" };
    for (int b = 0; b < 8; ++b)
        out.push_back (unit (bands[b], 0x2c4 + b));
    out.push_back (flag ("Limiter on", 0x2cc));
    out.push_back (unit ("Limiter gain", 0x2cd));
    out.push_back (unit ("Limiter release", 0x2ce));
    return out;
}

std::vector<Control> makeSakura()
{
    std::vector<Control> v = {
        unit ("Master", 0), unit ("Tune", 1), unit ("Amp attack", 2), unit ("Amp decay", 3), unit ("Amp sustain", 4), unit ("Amp release", 5),
        unit ("Str1 decay", 6), flag ("Str1 decay inv", 7), unit ("Str1 damp offset", 8), unit ("Str1 damp", 9), unit ("Str1 damp sharp", 10),
        unit ("Str2 decay", 11), flag ("Str2 decay inv", 12), unit ("Str2 damp offset", 13), unit ("Str2 damp", 14), unit ("Str2 damp sharp", 15),
        unit ("Saturation", 16), unit ("String mix", 17), unit ("Stereo sep", 18), unit ("Comb ratio", 19), unit ("Comb feedback", 20),
        flag ("Comb feedback inv", 21), unit ("Comb level", 22), flag ("Comb level inv", 23), unit ("Lo-cut", 24), unit ("Lo-cut res", 25),
        unit ("Hi-cut attack", 26), unit ("Hi-cut decay", 27), unit ("Hi-cut sustain", 28), unit ("Hi-cut", 29), unit ("Hi-cut res", 30),
        unit ("Noise level", 31), unit ("Noise rate", 32), unit ("Hi-cut drive", 33), unit ("EG attack", 34), unit ("EG decay", 35),
        unit ("EG sustain", 36), unit ("EG release", 37), unit ("LFO rate", 38),
        flag ("Delay on", 63), unit ("Delay time", 64), unit ("Delay feedback", 65), unit ("Delay speed", 66), unit ("Delay depth", 67),
        unit ("Delay blur", 68), unit ("Delay wet", 69), flag ("Chorus on", 70), unit ("Chorus rate", 71), unit ("Chorus depth", 72),
        unit ("Chorus wet", 73), flag ("Reverb on", 74), unit ("Reverb mix", 75), unit ("Reverb decay", 76), unit ("Reverb color", 77),
        ranged ("Str2 ratio", 111, 0, 6), ranged ("Delay sync", 112, 0, 9), unit ("Transpose", 113) };
    static const char* const res[3][8] = {
        { "Res 1 freq", "Res 2 freq", "Res 3 freq", "Res 4 freq", "Res 5 freq", "Res 6 freq", "Res 7 freq", "Res 8 freq" },
        { "Res 1 feedback", "Res 2 feedback", "Res 3 feedback", "Res 4 feedback", "Res 5 feedback", "Res 6 feedback", "Res 7 feedback", "Res 8 feedback" },
        { "Res 1 on", "Res 2 on", "Res 3 on", "Res 4 on", "Res 5 on", "Res 6 on", "Res 7 on", "Res 8 on" } };
    static const char* const mod[4][8] = {
        { "Mod 1 source", "Mod 2 source", "Mod 3 source", "Mod 4 source", "Mod 5 source", "Mod 6 source", "Mod 7 source", "Mod 8 source" },
        { "Mod 1 amount", "Mod 2 amount", "Mod 3 amount", "Mod 4 amount", "Mod 5 amount", "Mod 6 amount", "Mod 7 amount", "Mod 8 amount" },
        { "Mod 1 negative", "Mod 2 negative", "Mod 3 negative", "Mod 4 negative", "Mod 5 negative", "Mod 6 negative", "Mod 7 negative", "Mod 8 negative" },
        { "Mod 1 target", "Mod 2 target", "Mod 3 target", "Mod 4 target", "Mod 5 target", "Mod 6 target", "Mod 7 target", "Mod 8 target" } };
    for (int k = 0; k < 8; ++k)
    {
        v.push_back (unit (res[0][k], 39 + k));
        v.push_back (unit (res[1][k], 47 + k));
        v.push_back (flag (res[2][k], 55 + k));
        v.push_back (ranged (mod[0][k], 79 + 4 * k, 0, 8));
        v.push_back (unit (mod[1][k], 80 + 4 * k));
        v.push_back (flag (mod[2][k], 81 + 4 * k));
        v.push_back (ranged (mod[3][k], 82 + 4 * k, 0, 14));
    }
    return v;
}

// DrumSynth Live's 38 patch integers (REA_rodada30 §1.4; the ranges of its form)
std::vector<Control> makeDrumSynth()
{
    std::vector<Control> v = {
        ranged ("Level", 0, 0, 128), ranged ("Pan", 1, -64, 64), ranged ("Tone", 2, -50, 50), ranged ("Osc1 harmonics", 3, -50, 50),
        ranged ("Osc1 freq", 4, 0, 1000), ranged ("Osc1 width", 5, 0, 127), ranged ("Osc1 sweep", 6, 0, 127), ranged ("Osc1 decay", 7, 0, 127),
        ranged ("Osc1 level", 8, 0, 127), ranged ("Osc2 shape", 9, 0, 127), ranged ("Osc2 freq", 10, 0, 1000), ranged ("Osc2 width", 11, 0, 127),
        ranged ("Osc2 attack", 12, 0, 127), ranged ("Osc2 decay", 13, 0, 127), ranged ("Osc2 level", 14, 0, 127), ranged ("Waveform", 15, -1, 17),
        ranged ("Wave tune", 16, -50, 50), ranged ("Wave mix", 17, 0, 127), ranged ("Retrig count", 18, 0, 100), ranged ("Retrig rate", 19, 0, 127),
        ranged ("Retrig decay", 20, 0, 127), ranged ("Release", 21, 0, 127), ranged ("Osc1 invert", 22, 0, 1), ranged ("Click", 23, 0, 100),
        ranged ("Voice mode", 24, 0, 5), ranged ("Vel > osc1 freq", 25, -50, 50), ranged ("Vel > osc1 level", 26, -50, 50),
        ranged ("Vel > osc2 freq", 27, -50, 50), ranged ("Vel > osc2 level", 28, -50, 50), ranged ("Vel > wave tune", 29, -50, 50),
        ranged ("Vel > tone", 30, -50, 50), ranged ("Vel > osc1 harm", 31, -50, 50), ranged ("Osc1 noise", 32, 0, 1), ranged ("Ring mod", 33, 0, 127),
        ranged ("Send level", 35, 0, 128), ranged ("Morph amount", 37, 0, 128) };
    for (auto& c : v)
        c.perPart = true;
    return v;
}

const std::vector<Control> empty;

int dslSlot (int key) { return drumSynthKeyToSlot (key); }

// Plucked!'s knobs when the channel has no state yet (the engine's defaults: decay 128, color 64, normalize, widen)
constexpr int pluckedDefaults[5] = { 128, 64, 1, 0, 1 };

std::int32_t i32 (const std::vector<std::uint8_t>& s, std::size_t at)
{
    std::int32_t v = 0;
    std::memcpy (&v, s.data() + at, 4);
    return v;
}
} // namespace

const std::vector<Control>& controls (InstrumentKind kind)
{
    static const std::vector<Control> kick = { ranged ("Max freq", 0, -2400, 3600), ranged ("Min freq", 1, -1200, 1200), ranged ("Freq decay", 2, 0, 256),
                                               ranged ("Amp decay", 3, 0, 256), ranged ("Click", 4, 0, 64), ranged ("Distortion", 5, 0, 128) };
    static const std::vector<Control> pluck = { ranged ("Decay", 0, 0, 256), ranged ("Color", 1, 0, 128), ranged ("Normalize", 2, 0, 1),
                                                ranged ("Gate", 3, 0, 1), ranged ("Widen", 4, 0, 1) };
    static const std::vector<Control> pad = drumpadVoice();
    static const std::vector<Control> maxx = makeDrumaxx();
    static const std::vector<Control> bass = {
        unit ("Output", 0), unit ("Sampling", 1), unit ("Duration", 2), unit ("Sub bass", 3), unit ("Drive", 4), unit ("Attack", 5), unit ("Base", 6),
        unit ("Peak", 7), unit ("Slide", 8), unit ("Var base", 9), unit ("Var peak", 10), unit ("Var slide", 11), unit ("Var phase", 12),
        unit ("Click cut", 13), unit ("Click freq", 14), unit ("Click level", 15), unit ("Noise A", 16), unit ("Noise D", 17), unit ("Noise S", 18),
        unit ("Noise start", 19), unit ("Noise pitch", 20), unit ("Noise mix", 21), unit ("Noise filter", 22) };
    static const std::vector<Control> dsl = makeDrumSynth();
    static const std::vector<Control> sak = makeSakura();
    // FL Slayer: the front-end's integers (REA_rodada68 §6.1); maxima as slayerParamMax
    static const std::vector<Control> slay = {
        ranged ("Strings", 0, 0, 6), ranged ("Coils", 1, 0, 2), ranged ("Pickup", 2, 0, 65535), ranged ("Tone", 3, 0, 65535), ranged ("Slap", 4, 0, 65535),
        ranged ("Fret", 5, 0, 65535), ranged ("Harmonic", 6, 0, 65535), ranged ("Damp", 7, 0, 65535), ranged ("Vel volume", 8, 0, 65535),
        ranged ("Vel damp", 9, 0, 65535), ranged ("Amp type", 12, 0, 3), ranged ("Drive", 13, 0, 65535), ranged ("Presence", 14, 0, 65535),
        ranged ("Feedback", 15, 0, 65535), ranged ("EQ low", 16, 0, 65535), ranged ("EQ mid", 17, 0, 65535), ranged ("EQ high", 18, 0, 65535),
        ranged ("Cabinet", 19, 0, 3), ranged ("Effect", 20, 0, 15), ranged ("FX par 1", 21, 0, 65535), ranged ("FX par 2", 22, 0, 65535),
        ranged ("Mode", 24, 0, 5), ranged ("Timing", 25, 0, 65535), ranged ("Latch", 26, 0, 1), ranged ("Glissando", 27, 0, 1),
        ranged ("Pitchbend 2", 28, 0, 127) };
    switch (kind)
    {
        case InstrumentKind::fruitkick: return kick;
        case InstrumentKind::plucked: return pluck;
        case InstrumentKind::drumpad: return pad;
        case InstrumentKind::drumaxx: return maxx;
        case InstrumentKind::bassdrum: return bass;
        case InstrumentKind::drumsynth: return dsl;
        case InstrumentKind::sakura: return sak;
        case InstrumentKind::slayer: return slay;
        default: return empty;
    }
}

const Control* find (InstrumentKind kind, const std::string& name)
{
    for (const auto& c : controls (kind))
        if (name == c.name)
            return &c;
    return nullptr;
}

int defaultPart (const ChannelDef& def)
{
    if (def.instrument == InstrumentKind::drumsynth)
    {
        auto key = 60;
        if (drumSynthEditorKeyIndex < (int) def.modelParams.size())
            key = (int) def.modelParams[(std::size_t) drumSynthEditorKeyIndex];
        return drumSynthKeyToSlot (key) >= 0 ? key : 60;
    }
    return 0;
}

int absoluteIndex (InstrumentKind kind, const Control& c, int partIndex)
{
    if (! c.perPart)
        return c.index;
    if (kind == InstrumentKind::drumaxx)
        return c.index + 44 * std::clamp (partIndex, 0, 15);
    if (kind == InstrumentKind::drumsynth)
    {
        const auto slot = dslSlot (partIndex);
        return slot < 0 ? -1 : drumSynthPatchParams * slot + c.index;
    }
    return c.index;
}

std::optional<float> value (const ChannelDef& def, const Control& c, int partIndex)
{
    const auto index = absoluteIndex (def.instrument, c, partIndex);
    return index < 0 ? std::nullopt : rawValue (def, index);
}

bool setValue (ChannelDef& def, const Control& c, float v, int partIndex)
{
    if (! std::isfinite (v))
        return false;
    v = std::clamp (v, c.low, c.high);
    if (c.integer)
        v = std::round (v);
    const auto index = absoluteIndex (def.instrument, c, partIndex);
    return index >= 0 && setRawValue (def, index, v);
}

std::optional<float> rawValue (const ChannelDef& def, int index)
{
    const auto& s = def.pluginState;
    const auto& m = def.modelParams;
    if (index < 0)
        return std::nullopt;
    switch (def.instrument)
    {
        case InstrumentKind::fruitkick:
            if (! fruitKickStateUsable (s) || index >= 6)
                return std::nullopt;
            return (float) fruitKickParam (s, index);
        case InstrumentKind::plucked:
            if (index >= 5)
                return std::nullopt;
            if (s.empty()) // a fresh channel: the engine's own defaults
                return (float) pluckedDefaults[index];
            if (s.size() < 20)
                return std::nullopt;
            return (float) i32 (s, 4 * (std::size_t) index);
        case InstrumentKind::drumpad:
        case InstrumentKind::sakura:
        case InstrumentKind::drumsynth:
            if (index >= (int) m.size())
                return std::nullopt;
            return std::isfinite (m[(std::size_t) index]) ? m[(std::size_t) index] : 0.0f;
        case InstrumentKind::drumaxx:
        {
            const auto v = drumaxxParam (s, index);
            return std::isfinite (v) ? std::optional<float> (v) : std::nullopt;
        }
        case InstrumentKind::bassdrum:
        {
            const auto at = bassDrumParamOffset (s, index);
            if (at < 0)
                return std::nullopt;
            float v = 0.0f;
            std::memcpy (&v, s.data() + at, 4);
            return std::isfinite (v) ? v : 0.0f;
        }
        case InstrumentKind::slayer:
        {
            const auto at = 4 + 4 * (std::size_t) index;
            if (! slayerStateUsable (s) || at + 4 > s.size())
                return std::nullopt;
            return (float) i32 (s, at);
        }
        default: return std::nullopt;
    }
}

bool setRawValue (ChannelDef& def, int index, float v)
{
    auto& s = def.pluginState;
    auto& m = def.modelParams;
    if (index < 0 || ! std::isfinite (v))
        return false;
    const auto put32 = [&s] (std::size_t at, std::int32_t x) { std::memcpy (s.data() + at, &x, 4); };
    switch (def.instrument)
    {
        case InstrumentKind::fruitkick: return fruitKickSetParam (s, index, (int) std::lround (v));
        case InstrumentKind::plucked:
            if (s.empty() && index < 5) // a fresh channel gets the five knobs (the engine's defaults) on the first edit
                for (const auto d : pluckedDefaults)
                    for (int b = 0; b < 4; ++b)
                        s.push_back ((std::uint8_t) ((std::uint32_t) d >> (8 * b)));
            if (s.size() < 20 || index >= 5)
                return false;
            put32 (4 * (std::size_t) index, (std::int32_t) std::lround (v));
            return true;
        case InstrumentKind::drumpad:
        case InstrumentKind::sakura:
        case InstrumentKind::drumsynth:
            if (index >= (int) m.size())
                return false;
            m[(std::size_t) index] = v;
            return true;
        case InstrumentKind::drumaxx:
            if (! std::isfinite (drumaxxParam (s, index)))
                return false;
            drumaxxSetParam (s, index, v);
            return true;
        case InstrumentKind::bassdrum:
        {
            const auto at = bassDrumParamOffset (s, index);
            if (at < 0)
                return false;
            std::memcpy (s.data() + at, &v, 4);
            return true;
        }
        case InstrumentKind::slayer:
        {
            const auto at = 4 + 4 * (std::size_t) index;
            if (! slayerStateUsable (s) || at + 4 > s.size())
                return false;
            put32 (at, (std::int32_t) std::lround (v));
            return true;
        }
        default: return false;
    }
}

const std::vector<EngineParam>& params (InstrumentKind kind)
{
    static const std::map<InstrumentKind, std::vector<EngineParam>> all = [] {
        std::map<InstrumentKind, std::vector<EngineParam>> out;
        static const char* const notes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (const auto k : { InstrumentKind::fruitkick, InstrumentKind::plucked, InstrumentKind::drumpad, InstrumentKind::drumaxx,
                              InstrumentKind::bassdrum, InstrumentKind::drumsynth, InstrumentKind::sakura, InstrumentKind::slayer })
        {
            auto& t = out[k];
            for (const auto& c : controls (k))
            {
                if (! c.perPart)
                    t.push_back ({ c.index, c.name, c.low, c.high, c.integer });
                else if (k == InstrumentKind::drumaxx)
                    for (int pad = 0; pad < 16; ++pad)
                        t.push_back ({ c.index + 44 * pad, "Pad " + std::to_string (pad + 1) + " " + c.name, c.low, c.high, c.integer });
                else // DrumSynth Live: every key's patch ("C5 Level"; FL's octave numbers, key 60 = C5)
                    for (int key = 0; key < drumSynthPatches; ++key)
                        t.push_back ({ drumSynthPatchParams * dslSlot (key) + c.index, std::string (notes[key % 12]) + std::to_string (key / 12) + " " + c.name,
                                       c.low, c.high, c.integer });
            }
            std::sort (t.begin(), t.end(), [] (const EngineParam& a, const EngineParam& b) { return a.index < b.index; });
        }
        return out;
    }();
    static const std::vector<EngineParam> none;
    const auto it = all.find (kind);
    return it != all.end() ? it->second : none;
}
} // namespace lynx::drumcontrols

namespace lynx::modelparams
{
namespace
{
std::optional<float> getDrum (const ChannelDef& c, const EngineParam& p) { return drumcontrols::rawValue (c, p.index); }
bool setDrum (ChannelDef& c, const EngineParam& p, float raw) { return drumcontrols::setRawValue (c, p.index, raw); }
Table drumTable (InstrumentKind kind) { return { &drumcontrols::params (kind), &getDrum, &setDrum }; }
} // namespace

Table fruitKickModelTable() { return drumTable (InstrumentKind::fruitkick); }
Table pluckedModelTable() { return drumTable (InstrumentKind::plucked); }
Table drumpadModelTable() { return drumTable (InstrumentKind::drumpad); }
Table drumaxxModelTable() { return drumTable (InstrumentKind::drumaxx); }
Table bassDrumModelTable() { return drumTable (InstrumentKind::bassdrum); }
Table drumSynthModelTable() { return drumTable (InstrumentKind::drumsynth); }
Table sakuraModelTable() { return drumTable (InstrumentKind::sakura); }
Table slayerModelTable() { return drumTable (InstrumentKind::slayer); }
} // namespace lynx::modelparams
