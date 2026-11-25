// LYNX's own FL Slayer bank (round 190rh). Designed for LYNX on the plugin's 29 documented parameters (REA_rodada68 §6); no FL
// preset was read for it. The picks and chord shapes are FL's on a channel that has them, else LYNX's (core/NativeData.h).

#include <algorithm>
#include <array>
#include <initializer_list>
#include <utility>

#include "core/LynxBanks.h"
#include "core/NativeData.h"
#include "core/Slayer.h"

namespace lynx::banks
{
namespace
{
enum Param
{
    strings, coils, pickup, tone, slap, fret, harmonic, damp, velVolume, velDamp, p10, p11, ampType, drive, presence, feedback, low, mid,
    high, cabinet, effect, fx1, fx2, volume, mode, timing, latch, glissando, wheel, count
};
constexpr int half = 32768;

Preset make (const char* name, const char* category, std::initializer_list<std::pair<Param, int>> values, std::vector<std::string> tags = {})
{
    std::array<int, count> p {};
    p[strings] = 1, p[coils] = 1, p[pickup] = 20000, p[tone] = 40000, p[slap] = 8000, p[fret] = 10000, p[harmonic] = 30000, p[damp] = 12000;
    p[velVolume] = half, p[velDamp] = 16000, p[p10] = half, p[p11] = half, p[ampType] = 0, p[drive] = 14000, p[presence] = 30000;
    p[low] = p[mid] = p[high] = half, p[cabinet] = 1, p[fx1] = half, p[fx2] = half, p[volume] = 22000, p[timing] = half;
    for (const auto& [i, v] : values)
        p[(std::size_t) i] = v;
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [p] (ChannelDef& c) {
        c.pluginState.clear();
        const auto put = [&c] (int v) {
            for (int k = 0; k < 4; ++k)
                c.pluginState.push_back ((std::uint8_t) ((unsigned) v >> (8 * k)));
        };
        put (1); // version: 29 parameters
        for (int i = 0; i < count; ++i)
            put (std::clamp (p[(std::size_t) i], 0, slayerParamMax (i)));
        c.modelParams = { 0.0f };
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::slayer);
    };
    return out;
}
} // namespace

std::vector<Preset> slayerBank()
{
    return {
        make ("Clean Single", "Clean", {}),
        make ("Bright Twin", "Clean", { { coils, 2 }, { tone, 60000 }, { presence, 48000 }, { drive, 8000 } }),
        make ("Warm Neck", "Clean", { { pickup, 50000 }, { tone, 20000 }, { high, 20000 } }),
        make ("Soft Fingers", "Clean", { { strings, 0 }, { slap, 2000 }, { tone, 24000 }, { velDamp, 30000 } }),
        make ("Muted Funk", "Clean", { { damp, 48000 }, { slap, 26000 }, { tone, 52000 } }, { "short" }),
        make ("Phase Clean", "Clean", { { effect, 1 }, { fx1, 20000 }, { tone, 50000 } }, { "phaser" }),
        make ("Chorus Clean", "Clean", { { effect, 9 }, { coils, 2 }, { tone, 50000 } }, { "chorus" }),
        make ("Breakup", "Drive", { { ampType, 1 }, { drive, 24000 } }),
        make ("Crunch", "Drive", { { ampType, 1 }, { drive, 42000 }, { cabinet, 2 } }),
        make ("Wah Lead", "Drive", { { ampType, 1 }, { drive, 40000 }, { effect, 3 }, { mode, 5 } }, { "wah" }),
        make ("Power Chords", "Heavy", { { ampType, 2 }, { drive, 46000 }, { mode, 2 }, { cabinet, 2 }, { volume, 26000 } }, { "chords" }),
        make ("Palm Metal", "Heavy", { { ampType, 2 }, { drive, 56000 }, { damp, 44000 }, { low, 44000 }, { cabinet, 3 }, { volume, 26000 } }),
        make ("Solo Fuzz", "Heavy", { { ampType, 3 }, { drive, 50000 }, { mode, 5 }, { feedback, 16000 }, { volume, 24000 } }, { "mono" }),
        make ("Strummed", "Chords", { { mode, 3 }, { timing, 22000 } }, { "strum" }),
        make ("Auto Chords", "Chords", { { mode, 1 }, { tone, 46000 } }, { "chords" }),
        make ("Echo Guitar", "Effects", { { effect, 13 }, { tone, 46000 } }, { "delay" }),
        make ("Room Guitar", "Effects", { { effect, 14 }, { coils, 2 } }, { "reverb" }),
        make ("Rotary", "Effects", { { effect, 11 }, { ampType, 1 }, { drive, 18000 } }, { "leslie" }),
    };
}
} // namespace lynx::banks
