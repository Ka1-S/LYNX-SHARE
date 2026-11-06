#include "core/ModelParamsSynths.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Dx10.h"
#include "core/Gms.h"
#include "core/Harmless.h"
#include "core/MiniSynth.h"
#include "core/Morphine.h"
#include "core/Ogun.h"
#include "core/SimSynth.h"

// Control sets read from the editor forms of FL Studio 2026's own plugin DLLs (each control's parameter number is its
// Tag, its range Min…Max): docs/analise/REA_rodada168o.md §2. Names follow the plugins' own hints.

namespace lynx
{
namespace
{
struct Builder
{
    std::vector<EngineParam> t;
    void add (int index, const char* name, float low, float high, bool stepped = true)
    {
        t.push_back ({ index, name, low, high, stepped });
    }
    void knob (int index, const char* name, float low, float high) { add (index, name, low, high, true); }
    void sw (int index, const char* name) { add (index, name, 0.0f, 1.0f, true); }
    void unit (int index, const char* name) { add (index, name, 0.0f, 1.0f, false); }
    std::vector<EngineParam> sorted()
    {
        std::sort (t.begin(), t.end(), [] (const EngineParam& a, const EngineParam& b) { return a.index < b.index; });
        return std::move (t);
    }
};
} // namespace

const std::vector<EngineParam>& harmlessEngineParams()
{
    // raw FL integers: every Harmless control stores whole numbers
    static const std::vector<EngineParam> table = [] {
        Builder b;
        b.knob (0, "Phase randomness", -1000, 1000);
        b.knob (1, "Low harmonics protection", 0, 1024);
        b.knob (2, "Sub harmonic 1", 0, 2048);
        b.knob (3, "Sub harmonic 3", 0, 2048);
        b.knob (4, "Sub harmonic 4", 0, 2048);
        static const char* const eq[6] = { "Timbre EQ band 1", "Timbre EQ band 2", "Timbre EQ band 3", "Timbre EQ band 4", "Timbre EQ band 5", "Timbre EQ band 6" };
        for (int i = 0; i < 6; ++i)
            b.knob (5 + i, eq[i], -1024, 1024);
        static const char* const mask[12] = { "Harmonic mask 1", "Harmonic mask 2", "Harmonic mask 3", "Harmonic mask 4", "Harmonic mask 5", "Harmonic mask 6",
                                              "Harmonic mask 7", "Harmonic mask 8", "Harmonic mask 9", "Harmonic mask 10", "Harmonic mask 11", "Harmonic mask 12" };
        for (int i = 0; i < 12; ++i)
            b.knob (11 + i, mask[i], 0, 1000);
        b.knob (23, "Harmonic mask mix", 0, 1000);
        b.knob (24, "Mask mix LFO amount", -1000, 1000);
        b.knob (25, "Stereo tremolo depth", 0, 1000);
        b.knob (26, "Volume attack", 0, 1000);
        b.knob (27, "Volume decay", 0, 1000);
        b.knob (28, "Volume release", 0, 1000);
        b.sw (29, "Velocity to attack");
        b.sw (30, "Release velocity to release");
        b.knob (31, "Pluck decay", 0, 1000);
        b.sw (32, "Velocity to pluck decay");
        b.sw (33, "Alternate pluck mode");
        b.sw (34, "Pluck uses harmonic mask");
        b.sw (35, "Pluck only on release");
        b.knob (36, "Master volume", 0, 2000);
        b.sw (37, "Velocity to volume");
        b.knob (38, "Master pitch", -2400, 2400);
        b.knob (39, "Detuning", -2400, 2400);
        b.knob (40, "Grittiness", -1000, 1000);
        b.knob (41, "Vibrato depth", 0, 1000);
        b.knob (42, "Pitch LFO amount", -1000, 1000);
        b.sw (43, "Portamento");
        b.sw (44, "Monophonic");
        b.knob (45, "Portamento time", -1000, 1000);
        b.sw (46, "Velocity to portamento time");
        b.sw (47, "Adaptive filter envelope");
        b.knob (48, "Filter env attack", 0, 1000);
        b.knob (49, "Filter env decay", 0, 1000);
        b.knob (50, "Filter env attack slope", -1000, 1000);
        b.knob (51, "Filter env decay slope", -1000, 1000);
        b.knob (52, "Filter env amount", -1000, 1000);
        b.sw (53, "Velocity to filter env amount");
        b.knob (54, "Filter frequency", 0, 8192);
        b.sw (55, "Filter scale");
        b.knob (56, "Filter width", 0, 1000);
        b.knob (57, "Key to filter frequency", -2000, 2000);
        b.knob (58, "Filter LFO amount", -1000, 1000);
        b.knob (59, "Resonance amount", 0, 1000);
        b.sw (60, "Resonance scale");
        b.knob (61, "Resonance offset", -2400, 2400);
        b.knob (62, "Resonance self-oscillation", 0, 2048);
        b.sw (63, "Adaptive resonance width");
        b.sw (64, "Track filter frequency motion");
        b.knob (65, "Unison order", 1, 9);
        b.knob (66, "Unison panning", 0, 128);
        b.knob (67, "Unison pitch thickness", 0, 256);
        b.knob (68, "Unison phase", 0, 128);
        b.knob (69, "Unison variation", 0, 128);
        b.knob (70, "Unison type", 0, 2);
        b.knob (71, "Phaser mix", 0, 1000);
        b.sw (72, "Phaser scale");
        b.knob (73, "Phaser width", 0, 10000);
        b.knob (74, "Phaser width LFO amount", -1000, 1000);
        b.knob (75, "Phaser offset", 0, 999);
        b.knob (76, "Phaser offset LFO amount", -1000, 1000);
        b.knob (77, "Phaser motion speed", -8000, 8000);
        b.knob (78, "Key to phaser offset", -2000, 2000);
        b.knob (79, "Harmonizer mix", 0, 1000);
        b.knob (80, "Harmonizer width", 0, 1000);
        b.knob (81, "Harmonizer width LFO amount", -1000, 1000);
        b.sw (82, "Velocity to harmonizer width");
        b.knob (83, "Harmonizer strength", 0, 1024);
        b.knob (84, "Harmonizer type", 0, 4);
        b.knob (85, "Harmonizer position", 0, 3);
        b.knob (86, "LFO shape", 0, 13);
        b.sw (87, "Global LFO");
        b.knob (88, "LFO attack", 0, 1000);
        b.knob (89, "LFO speed", -8000, 8000);
        b.knob (90, "Chorus type", 0, 9);
        b.knob (91, "Chorus mix", 0, 1000);
        b.knob (92, "Delay type", 0, 3);
        b.knob (93, "Delay input level", 0, 1000);
        b.knob (94, "Delay feedback", 0, 900);
        b.knob (95, "Delay time", 0, 384);
        b.knob (96, "Reverb type", 0, 6);
        b.knob (97, "Reverb wet level", 0, 1000);
        b.knob (98, "Compression level", 0, 1000);
        b.sw (99, "Velocity to unison pitch thickness");
        // the combos (state integers after the 100 parameters)
        b.knob (harmlessComboIndex + 0, "Filter type", 0, 16);
        b.knob (harmlessComboIndex + 1, "Resonance type", 0, 8);
        b.knob (harmlessComboIndex + 2, "Phaser type", 0, 9);
        b.knob (harmlessComboIndex + 3, "Compression type", 0, 4);
        b.knob (harmlessComboIndex + 4, "Timbre color", 0, 11);
        b.sw (harmlessComboIndex + 5, "Threaded");
        b.sw (harmlessComboIndex + 6, "High quality");
        return b.sorted();
    }();
    return table;
}

const std::vector<EngineParam>& dx10EngineParams()
{
    static const std::vector<EngineParam> table = [] {
        Builder b;
        static const char* const names[22] = { "Attack", "Decay", "Release", "Mod 1 coarse", "Mod 1 fine", "Mod 1 init", "Mod 1 time",
                                               "Mod 1 sustain", "Mod 1 release", "Mod 1 velocity", "Vibrato", "Waveform", "Mod thru",
                                               "LFO rate", "Mod 2 coarse", "Mod 2 fine", "Mod 2 init", "Mod 2 time", "Mod 2 sustain",
                                               "Mod 2 release", "Mod 2 velocity", "Coarse" };
        for (int i = 0; i < 21; ++i)
            b.knob (i, names[i], 0, 65536);
        b.knob (21, names[21], -3, 3);
        return b.sorted();
    }();
    return table;
}

const std::vector<EngineParam>& simSynthEngineParams()
{
    // FL's host parameter numbers (the editor's Tags): knobs 0…65535, the coarse pitch knobs in 48 semitone steps
    static const std::vector<EngineParam> table = [] {
        Builder b;
        static const char* const knobs[26] = { "Osc 1 level", "Osc 1 LFO", "Osc 1 envelope", "Osc 2 level", "Osc 2 LFO", "Osc 2 envelope",
                                               "Osc 3 level", "Osc 3 LFO", "Osc 3 envelope", "LFO delay", "LFO rate", "Cutoff",
                                               "Emphasis", "Filter envelope", "Filter LFO", "High pass", "Band pass", "Attack",
                                               "Decay", "Sustain", "Release", "Velocity to volume", "Filter attack", "Filter decay",
                                               "Filter sustain", "Filter release" };
        for (int i = 0; i < 26; ++i)
            b.knob (i, knobs[i], 0, 65535);
        for (int o = 0; o < 3; ++o)
        {
            static const char* const pw[3] = { "Osc 1 shape", "Osc 2 shape", "Osc 3 shape" };
            static const char* const coarse[3] = { "Osc 1 coarse", "Osc 2 coarse", "Osc 3 coarse" };
            static const char* const fine[3] = { "Osc 1 fine", "Osc 2 fine", "Osc 3 fine" };
            b.knob (26 + 3 * o, pw[o], 0, 65535);
            b.knob (27 + 3 * o, coarse[o], 0, 48);
            b.knob (28 + 3 * o, fine[o], 0, 65535);
        }
        b.knob (35, "Key tracking", 0, 65535);
        b.sw (36, "Chorus");
        b.sw (37, "LFO retrigger");
        b.sw (38, "Filter tracks amp envelope");
        b.sw (39, "LFO on");
        b.sw (40, "Filter on");
        b.sw (41, "Osc 1 on");
        b.sw (42, "Osc 2 on");
        b.sw (43, "Osc 3 on");
        b.sw (44, "Ring mod 1 x 2");
        static const char* const layers[9] = { "Osc 1 +1 octave", "Osc 1 +2 octaves", "Osc 1 warm", "Osc 2 +1 octave", "Osc 2 +2 octaves",
                                               "Osc 2 warm", "Osc 3 +1 octave", "Osc 3 +2 octaves", "Osc 3 warm" };
        for (int i = 0; i < 9; ++i)
            b.sw (45 + i, layers[i]);
        b.knob (54, "Osc 1 wave", 0, 4);
        b.knob (55, "Osc 2 wave", 0, 4);
        b.knob (56, "Osc 3 wave", 0, 4);
        b.knob (57, "LFO shape", 0, 3);
        return b.sorted();
    }();
    return table;
}

namespace
{
void ogunCommon (Builder& b)
{
    b.knob (0, "Master volume", 0, 256);
    b.knob (1, "Coarse pitch", -24, 24);
    b.knob (2, "Fine pitch", -50, 50);
    b.knob (3, "Modulation X", 0, 512);
    b.knob (4, "Modulation Y", 0, 512);
    b.knob (5, "Pre-decay", 0, 1000);
    b.knob (6, "Decay time", 0, 10000);
    b.knob (7, "Release time", 0, 10000);
    b.knob (8, "Fullness", -1000, 1000);
    b.knob (9, "Morph", 0, 1000);
    b.knob (10, "Seed 1", 0, 9999);
    b.knob (11, "Seed 2", 0, 9999);
    b.knob (12, "Unison order", 1, 9);
    b.knob (13, "Unison panning", 0, 128);
    b.knob (14, "Unison volume", 16, 128);
    b.knob (15, "Unison pitch", 0, 256);
    b.knob (16, "Unison phase", 0, 128);
    b.knob (17, "Filter frequency", 0, 256);
    b.knob (18, "Filter resonance", 0, 256);
    b.knob (19, "Chorus order", 0, 9);
    b.knob (20, "Chorus depth", 0, 1024);
    b.knob (21, "Chorus speed", 0, 1024);
    b.knob (22, "Chorus delay", 0, 128);
    b.knob (23, "Chorus spread", 0, 128);
    b.knob (24, "Chorus cross", -128, 128);
    b.knob (25, "Chorus mix", -128, 128);
    b.sw (ogunDelayOnIndex, "Delay on");
    b.sw (ogunDelayTempoIndex, "Delay tempo sync");
    b.knob (26, "Delay feedback", 0, 127);
    b.knob (27, "Delay time", 0, 384);
    b.knob (28, "Delay stereo offset", -512, 512);
    b.knob (29, "Delay volume", -128, 128);
    b.knob (30, "Delay feedback mode", 0, 3);
    b.sw (ogunReverbOnIndex, "Reverb on");
    b.knob (31, "Reverb low cut", 19, 3000);
    b.knob (32, "Reverb high cut", 5, 221);
    b.knob (33, "Reverb predelay", 0, 384);
    b.knob (34, "Reverb room size", 1, 100);
    b.knob (35, "Reverb diffusion", 0, 100);
    b.knob (36, "Reverb decay", 1, 200);
    b.knob (37, "Reverb high damping", 5, 221);
    b.knob (38, "Reverb color", 0, 4);
    b.knob (39, "Reverb wet", -128, 128);
    b.knob (40, "EQ band 1 level", -1800, 1800);
    b.knob (41, "EQ band 2 level", -1800, 1800);
    b.knob (42, "EQ band 3 level", -1800, 1800);
    b.knob (43, "EQ band 1 freq", 0, 65536);
    b.knob (44, "EQ band 2 freq", 0, 65536);
    b.knob (45, "EQ band 3 freq", 0, 65536);
    b.knob (46, "EQ band 1 width", 0, 65536);
    b.knob (47, "EQ band 2 width", 0, 65536);
    b.knob (48, "EQ band 3 width", 0, 65536);
    b.knob (52, "Richness", 6, 16);
    b.knob (54, "Quality", 0, 4);
}
} // namespace

const std::vector<EngineParam>& ogunEngineParams()
{
    static const std::vector<EngineParam> table = [] {
        Builder b;
        ogunCommon (b);
        return b.sorted();
    }();
    return table;
}

const std::vector<EngineParam>& autogunEngineParams()
{
    static const std::vector<EngineParam> table = [] {
        Builder b;
        ogunCommon (b);
        b.knob (63, "Magic dust", 0, 1000);
        return b.sorted();
    }();
    return table;
}

const std::vector<EngineParam>& miniSynthEngineParams()
{
    // the 43 floats 0…1 (REA_rodada41 §1.2); the mode / choice parameters are read as int(n·p) by the engine
    static const std::vector<EngineParam> table = [] {
        Builder b;
        static const char* const names[43] = { "Voices mode", "Transpose", "Slide time", "Mod wheel destination", "Mod wheel amount",
                                               "Osc kind", "Osc mod", "Noise", "Cutoff", "Resonance", "Filter key tracking",
                                               "Filter velocity", "Filter attack", "Filter decay", "Filter sustain", "Filter release",
                                               "Filter env amount", "LFO shape", "LFO destination", "LFO rate", "LFO amount",
                                               "Attack", "Decay", "Sustain", "Release", "Distortion on", "Overdrive", "Decimator",
                                               "Delay on", "Delay time", "Delay feedback", "Delay filter", "Delay mix", "Chorus on",
                                               "Chorus rate", "Chorus depth", "Chorus mix", "Phaser on", "Phaser speed", "Phaser width",
                                               "Phaser feedback", "Phaser mix", "Phaser frequency" };
        for (int i = 0; i < 43; ++i)
            b.unit (i, names[i]);
        return b.sorted();
    }();
    return table;
}

const std::vector<EngineParam>& gmsEngineParams()
{
    // GMS' own 0…1 floats P[i] (REA_rodada12 §1.2 / rodada39 §1.2): the choices are read as int(n·p) by the engine
    static const std::vector<EngineParam> table = [] {
        Builder b;
        const std::pair<int, const char*> named[] = {
            { 0, "Master level" }, { 1, "Panning" }, { 2, "Pitch" }, { 4, "Attack" }, { 5, "Decay" }, { 6, "Sustain" },
            { 7, "Release" }, { 8, "Amp level" }, { 10, "Slide time" }, { 11, "Slide" }, { 12, "Cutoff" }, { 13, "Resonance" },
            { 14, "Key tracking" }, { 15, "Filter type" }, { 16, "Envelope 1 amount" }, { 17, "Envelope 2 amount" },
            { 18, "Envelope 1 invert" }, { 19, "Envelope 2 invert" }, { 20, "Envelope 1 attack" }, { 21, "Envelope 2 attack" },
            { 22, "Envelope 1 decay" }, { 23, "Envelope 2 decay" }, { 24, "Envelope 1 destination" }, { 25, "Envelope 2 destination" },
            { 26, "LFO 1 retrigger" }, { 27, "LFO 2 retrigger" }, { 28, "LFO 1 amount" }, { 29, "LFO 2 amount" }, { 30, "LFO 1 invert" },
            { 31, "LFO 2 invert" }, { 32, "LFO 1 shape" }, { 33, "LFO 2 shape" }, { 34, "LFO 1 rate" }, { 35, "LFO 2 rate" },
            { 36, "LFO 1 sync" }, { 37, "LFO 2 sync" }, { 38, "LFO 1 destination" }, { 39, "LFO 2 destination" }, { 40, "Mono" },
            { 41, "Unison voices" }, { 42, "Unison detune" }, { 43, "Unison stereo" }, { 44, "Octave" }, { 45, "Noise mix" },
            { 46, "Unison phase" }, { 47, "Unison phase retrigger" }, { 48, "Osc 2 mix" }, { 49, "Osc 3 mix" }, { 50, "Modulation" },
            { 51, "Modulation kind" }, { 52, "Osc 2 phase offset" }, { 53, "Osc 2 phase retrigger" }, { 54, "Osc 2 invert" },
            { 55, "Osc 1 pitch" }, { 56, "Osc 1 detune" }, { 57, "Osc 2 pitch" }, { 58, "Osc 2 detune" }, { 59, "Osc 3 pitch" },
            { 60, "Osc 3 detune" }, { 205, "Pitch bend range" }, { 206, "Output EQ" }, { 207, "EQ band 1" }, { 208, "EQ band 2" },
            { 209, "EQ band 3" }, { 210, "EQ band 4" }, { 211, "EQ band 5" } };
        for (const auto& [i, n] : named)
            b.unit (i, n);
        static const char* const fx[10] = { "Distortion", "Crusher", "Flanger", "Phaser", "Low pass", "High pass", "Delay", "Reverb", "Panner", "Trancer" };
        for (int k = 0; k < 10; ++k)
        {
            const std::string f = fx[k];
            const std::pair<int, std::string> fields[] = { { 117 + k, f + " X" }, { 127 + k, f + " Y" }, { 137 + k, f + " LFO rate" },
                                                           { 147 + k, f + " on" }, { 157 + k, f + " LFO shape" }, { 167 + k, f + " LFO sync" },
                                                           { 177 + k, f + " LFO amount" } };
            for (const auto& [i, n] : fields)
                b.t.push_back ({ i, n, 0.0f, 1.0f, false });
        }
        return b.sorted();
    }();
    return table;
}

const std::vector<EngineParam>& morphineEngineParams()
{
    // Morphine's global block A[i] (REA_rodada39 §2.2), 0…1 floats
    static const std::vector<EngineParam> table = [] {
        Builder b;
        const std::pair<int, const char*> named[] = {
            { 0, "Master level" }, { 1, "Glide" }, { 4, "Velocity curve" }, { 5, "Transpose" }, { 6, "Fine" }, { 7, "Overdrive" },
            { 8, "Bend range" }, { 9, "Attack" }, { 10, "Decay" }, { 11, "Sustain" }, { 12, "Release" }, { 13, "Chorus depth" },
            { 14, "Chorus mix" }, { 15, "Chorus rate" }, { 16, "Chorus on" }, { 17, "Delay time" }, { 18, "Delay stereo" },
            { 19, "Delay modulation" }, { 20, "Delay feedback" }, { 21, "Delay mix" }, { 22, "Delay on" }, { 23, "Reverb decay" },
            { 24, "Reverb damping" }, { 25, "Reverb mix" }, { 26, "Reverb on" }, { 27, "EQ 62 Hz" }, { 28, "EQ 125 Hz" },
            { 29, "EQ 250 Hz" }, { 30, "EQ 500 Hz" }, { 31, "EQ 1 kHz" }, { 32, "EQ 2 kHz" }, { 33, "EQ 4 kHz" }, { 34, "EQ 8 kHz" },
            { 35, "EQ on" }, { 36, "Noise mix" }, { 37, "Noise pan" }, { 38, "Noise mute" }, { 39, "Noise velocity" } };
        for (const auto& [i, n] : named)
            b.unit (i, n);
        for (int i : { 16, 22, 26, 35, 38 })
            for (auto& p : b.t)
                if (p.index == i)
                    p.stepped = true;
        return b.sorted();
    }();
    return table;
}
} // namespace lynx

namespace lynx::modelparams
{
namespace
{
// Harmless: modelParams[i]. A channel saved before its parameters were kept (an empty list) plays FL's untouched patch:
// that is what it reads, and an edit writes the whole list first.
std::optional<float> harmlessGet (const ChannelDef& c, const EngineParam& p)
{
    if (p.index < 0 || p.index >= harmlessCustomFlagIndex)
        return std::nullopt;
    if (c.modelParams.empty())
    {
        static const auto defaults = harmlessDefaultModelParams();
        return defaults[(std::size_t) p.index];
    }
    if (c.modelParams.size() < (std::size_t) harmlessParamCount)
        return std::nullopt;
    const auto v = c.modelParams[(std::size_t) p.index];
    return std::isfinite (v) ? v : p.low;
}
bool harmlessSet (ChannelDef& c, const EngineParam& p, float raw)
{
    if (p.index < 0 || p.index >= harmlessCustomFlagIndex)
        return false;
    if (c.modelParams.empty())
        c.modelParams = harmlessDefaultModelParams();
    if (c.modelParams.size() < (std::size_t) harmlessParamCount)
        return false;
    c.modelParams[(std::size_t) p.index] = std::round (raw);
    return true;
}

std::optional<float> dx10Get (const ChannelDef& c, const EngineParam& p)
{
    const auto v = dx10Param (c.pluginState, p.index);
    return v ? std::optional<float> ((float) *v) : std::nullopt;
}
bool dx10Set (ChannelDef& c, const EngineParam& p, float raw) { return dx10SetParam (c.pluginState, p.index, (int) std::lround (raw)); }

// SimSynth: an empty state plays the plugin's defaults (simsynth::defaultState)
std::optional<float> simGet (const ChannelDef& c, const EngineParam& p)
{
    static const auto defaults = simsynth::defaultState();
    const auto v = simsynth::hostParam (c.pluginState.empty() ? defaults : c.pluginState, p.index);
    return v ? std::optional<float> ((float) *v) : std::nullopt;
}
bool simSet (ChannelDef& c, const EngineParam& p, float raw)
{
    if (c.pluginState.empty())
        c.pluginState = simsynth::defaultState();
    return simsynth::setHostParam (c.pluginState, p.index, (int) std::lround (raw));
}

std::optional<float> ogunGet (const ChannelDef& c, const EngineParam& p)
{
    const auto v = ogunParam (c.pluginState, p.index);
    return v ? std::optional<float> ((float) *v) : std::nullopt;
}
bool ogunSet (ChannelDef& c, const EngineParam& p, float raw) { return ogunSetParam (c.pluginState, p.index, (int) std::lround (raw)); }

using Offset = int (*) (const std::vector<std::uint8_t>&, int);
template <Offset offset>
std::optional<float> floatGet (const ChannelDef& c, const EngineParam& p)
{
    const auto at = offset (c.pluginState, p.index);
    if (at < 0 || (std::size_t) at + 4 > c.pluginState.size())
        return std::nullopt;
    float v = 0.0f;
    std::memcpy (&v, c.pluginState.data() + at, 4);
    return std::isfinite (v) ? v : p.low;
}
template <Offset offset>
bool floatSet (ChannelDef& c, const EngineParam& p, float raw)
{
    const auto at = offset (c.pluginState, p.index);
    if (at < 0 || (std::size_t) at + 4 > c.pluginState.size())
        return false;
    std::memcpy (c.pluginState.data() + at, &raw, 4);
    return true;
}
} // namespace

Table harmlessModelTable() { return { &harmlessEngineParams(), &harmlessGet, &harmlessSet }; }
Table dx10ModelTable() { return { &dx10EngineParams(), &dx10Get, &dx10Set }; }
Table simSynthModelTable() { return { &simSynthEngineParams(), &simGet, &simSet }; }
Table ogunModelTable() { return { &ogunEngineParams(), &ogunGet, &ogunSet }; }
Table autogunModelTable() { return { &autogunEngineParams(), &ogunGet, &ogunSet }; }
Table miniSynthModelTable() { return { &miniSynthEngineParams(), &floatGet<miniSynthParamOffset>, &floatSet<miniSynthParamOffset> }; }
Table gmsModelTable() { return { &gmsEngineParams(), &floatGet<gmsParamOffset>, &floatSet<gmsParamOffset> }; }
Table morphineModelTable() { return { &morphineEngineParams(), &floatGet<morphineParamOffset>, &floatSet<morphineParamOffset> }; }
} // namespace lynx::modelparams
