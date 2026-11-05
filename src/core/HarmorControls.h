#pragma once

// The Harmor controls LYNX's channel window edits (round 172a): raw FL integers inside the saved state, read and written
// through harmorParamOffset (core/Harmor.h). Part parameters exist for both parts (A = 0, B = 1; the window's A / B
// tabs show one part at a time, as FL's), global ones once. Indices, laws and ranges from REA_rodada34 (§1.8–§1.19) and
// the ranges FL's 325 factory presets use; the filter block is 51 + 12·f … 61 + 12·f (core/Harmor.cpp buildFilter).
// Shared by the page layout (core/PluginLayoutSynths.cpp) and the panel (ui/ChannelSettings.cpp), so a control's name
// is the same in both.

#include <string>
#include <vector>

namespace lynx::harmorui
{
struct Control
{
    int part;          // 0 = A, 1 = B, −1 = global
    int index;         // part parameter 0…385 / global parameter 0…52
    std::string name;  // the knob's name (also its tooltip)
    int low, high;     // FL's range
    bool invert;       // the stored value runs against the knob (a period stored where FL's knob turns up the speed)
};

inline const std::vector<Control>& controls()
{
    static const std::vector<Control> list = [] {
        struct P
        {
            int index;
            const char* name;
            int low, high;
            bool invert;
        };
        static const P part[] = {
            { 0, "Phase start", 0, 1000, false }, { 1, "Phase randomness", -1000, 1000, false }, { 4, "Timbre mix", 0, 1000, false },
            { 5, "Sub level 1", 0, 1448, false }, { 6, "Sub level 2", 0, 1448, false }, { 7, "Sub level 3", 0, 1448, false },
            { 8, "Low harmonics protection", 0, 1024, false }, { 9, "Clip threshold", 0, 1024, false },
            { 10, "FX send", -1000, 1000, false }, { 11, "Volume", 0, 2000, false }, { 12, "Volume envelope amount", 0, 2000, false },
            { 13, "Auto gain", 0, 1000, false },
            { 17, "Tremolo depth", 0, 1000, false }, { 19, "Tremolo stereo gap", 0, 1000, false },
            { 20, "Blur mix", 0, 1000, false }, { 21, "Time blur attack", 0, 2000, false }, { 22, "Time blur decay", 0, 2000, false },
            { 23, "Harmonic blur", 0, 10000, false },
            { 26, "Prism amount", -1000, 1000, false },
            { 27, "Harmonizer amount", 0, 1000, false }, { 28, "Harmonizer width", 0, 1000, false }, { 29, "Harmonizer strength", 0, 1024, false },
            { 34, "Unison order", 1, 9, false }, { 35, "Unison type", 0, 4, false }, { 36, "Unison alternate phase", 0, 1, false },
            { 37, "Unison panning", 0, 256, false }, { 38, "Unison pitch", 0, 256, false }, { 39, "Unison phase", -1000, 1000, false },
            { 45, "Vibrato depth", 0, 1000, false }, { 46, "Vibrato speed", 0, 1000, true },
            { 51, "Filter 1 adaptive envelope", 0, 1, false }, { 52, "Filter 1 frequency", 0, 8192, false }, { 53, "Filter 1 scale", 0, 1, false },
            { 54, "Filter 1 width", 0, 1000, false }, { 55, "Filter 1 key tracking", -2000, 2000, false },
            { 56, "Filter 1 envelope amount", -1000, 1000, false }, { 57, "Resonance 1 amount", 0, 1000, false },
            { 58, "Resonance 1 scale", 0, 1, false }, { 59, "Resonance 1 width", 0, 1000, false }, { 60, "Resonance 1 offset", -2400, 2400, false },
            { 61, "Resonance 1 adaptive width", 0, 1, false }, { 62, "Self-oscillation", 0, 1024, false },
            { 63, "Filter 2 adaptive envelope", 0, 1, false }, { 64, "Filter 2 frequency", 0, 8192, false }, { 65, "Filter 2 scale", 0, 1, false },
            { 66, "Filter 2 width", 0, 1000, false }, { 67, "Filter 2 key tracking", -2000, 2000, false },
            { 68, "Filter 2 envelope amount", -1000, 1000, false }, { 69, "Resonance 2 amount", 0, 1000, false },
            { 70, "Resonance 2 scale", 0, 1, false }, { 71, "Resonance 2 width", 0, 1000, false }, { 72, "Resonance 2 offset", -2400, 2400, false },
            { 73, "Resonance 2 adaptive width", 0, 1, false }, { 74, "Filter 1 / 2 mix", -1000, 1000, false },
            { 75, "Pluck time", 0, 1000, false },
            { 76, "Phaser mix", 0, 1000, false }, { 77, "Phaser scale", 0, 2, false }, { 78, "Phaser width", 0, 10000, false },
            { 79, "Phaser offset", 0, 999, false }, { 80, "Phaser speed", -8000, 8000, false }, { 81, "Phaser key tracking", -2000, 2000, false },
        };
        static const P global[] = {
            { 0, "Mod X", 0, 512, false }, { 1, "Mod Y", 0, 512, false }, { 2, "Mod Z", 0, 512, false },
            { 4, "Legato", 0, 1, false }, { 5, "Portamento", 0, 1, false }, { 6, "Portamento velocity", 0, 1, false },
            { 10, "Part A / B mix", 0, 1000, false }, { 11, "Volume pre-FX", 0, 2000, false }, { 12, "Volume post-FX", 0, 2000, false },
            { 14, "Global pitch", -2400, 2400, false }, { 15, "Global LFO amount", 0, 1000, false },
            // the effects rack's delay and reverb (HarmorRack: 39 on, 41 feedback, 44 volume; 22 on, 31 wet)
            { 39, "Delay on", 0, 1, false }, { 41, "Delay feedback", 0, 1000, false }, { 44, "Delay volume", 0, 1000, false },
            { 22, "Reverb on", 0, 1, false }, { 31, "Reverb wet", 0, 1000, false },
        };
        std::vector<Control> v;
        for (const auto& g : global)
            v.push_back ({ -1, g.index, g.name, g.low, g.high, g.invert });
        for (int p = 0; p < 2; ++p)
            for (const auto& c : part)
                v.push_back ({ p, c.index, std::string (p == 0 ? "A: " : "B: ") + c.name, c.low, c.high, c.invert });
        return v;
    }();
    return list;
}

// the name of a control (a stable pointer for LayoutItem::param), nullptr when there is none
inline const char* name (int part, int index)
{
    for (const auto& c : controls())
        if (c.part == part && c.index == index)
            return c.name.c_str();
    return nullptr;
}
} // namespace lynx::harmorui
