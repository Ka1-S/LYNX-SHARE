// LYNX's own BassDrum bank (round 190rh). Designed for LYNX on the documented parameters (REA_rodada15 §12.3, REA_rodada43
// §2) and state layout ("SSLF" + size + "1kiK" + name + noise path + 23 floats); no FL preset was read for it. The noises
// it names are LYNX's own (core/NativeData.h).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "core/BassDrum.h"
#include "core/LynxBanks.h"
#include "core/NativeData.h"

namespace lynx::banks
{
namespace
{
enum Param
{
    output, sampling, duration, butt, drive, attack, baseHz, peak, slide, varBase, varPeak, varSlide, varPhase, clickCut, clickFreq,
    clickAmount, noiseA, noiseD, noiseS, noiseStart, noisePitch, noiseMix, noiseFilter, count
};
float hz (float f) { return (std::log2 (f) - 4.0f) / 4.0f; } // 2^(4x + 4)

std::vector<std::uint8_t> stateOf (const std::string& name, const std::string& noise, const std::array<float, count>& p)
{
    std::vector<std::uint8_t> s { 'S', 'S', 'L', 'F' };
    const auto put = [&s] (std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            s.push_back ((std::uint8_t) (v >> (8 * i)));
    };
    put (4 + 256 + 1024 + 4 * count);
    put (0x4b696b31u);
    const auto text = [&s] (const std::string& t, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i)
            s.push_back (i < t.size() && i + 1 < size ? (std::uint8_t) t[i] : (std::uint8_t) 0);
    };
    text (name, 256);
    text (noise, 1024);
    for (const auto v : p)
    {
        std::uint32_t bits;
        std::memcpy (&bits, &v, 4);
        put (bits);
    }
    return s;
}

Preset make (const char* name, const char* category, std::initializer_list<std::pair<Param, float>> values, const char* noise = "",
             std::vector<std::string> tags = {})
{
    std::array<float, count> p {};
    p[output] = 0.5f, p[sampling] = 0.5f, p[duration] = 0.75f, p[butt] = 0.3f, p[drive] = 0.1f, p[attack] = 0.05f, p[baseHz] = hz (48.0f);
    p[peak] = 0.45f, p[slide] = 0.3f, p[varBase] = p[varPeak] = p[varSlide] = 0.5f, p[clickCut] = 0.5f, p[clickFreq] = 0.5f;
    p[noiseA] = 0.0f, p[noiseD] = 0.75f, p[noiseStart] = 0.5f, p[noisePitch] = 0.5f, p[noiseFilter] = 0.9f;
    for (const auto& [i, v] : values)
        p[(std::size_t) i] = v;
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [p, n = std::string (name), file = std::string (noise)] (ChannelDef& c) {
        c.pluginState = stateOf (n, file, p);
        c.modelParams.clear();
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::bassdrum);
    };
    return out;
}
} // namespace

std::vector<Preset> bassDrumBank()
{
    const char *hiss = "LYNX Hiss", *crush = "LYNX Crush", *metal = "LYNX Metal", *clap = "LYNX Clap";
    return {
        make ("Plain Kick", "Kick", {}),
        make ("Club Kick", "Kick", { { peak, 0.55f }, { slide, 0.25f }, { clickAmount, 0.5f }, { drive, 0.2f } }),
        make ("Deep Sub", "Kick", { { baseHz, hz (38.0f) }, { duration, 0.9f }, { peak, 0.3f }, { butt, 0.6f } }, "", { "sub" }),
        make ("Long Boom", "Kick", { { baseHz, hz (42.0f) }, { duration, 1.0f }, { peak, 0.4f }, { slide, 0.2f }, { drive, 0.15f } }, "", { "808" }),
        make ("Tight Kick", "Kick", { { duration, 0.5f }, { baseHz, hz (58.0f) }, { slide, 0.22f } }, "", { "short" }),
        make ("Punch", "Kick", { { duration, 0.6f }, { peak, 0.65f }, { slide, 0.2f }, { butt, 0.5f }, { clickAmount, 0.6f } }),
        make ("Soft Thud", "Kick", { { attack, 0.35f }, { peak, 0.25f }, { slide, 0.35f } }, "", { "soft" }),
        make ("Wood Kick", "Kick", { { duration, 0.5f }, { baseHz, hz (72.0f) }, { clickAmount, 0.7f }, { clickFreq, 0.7f } }),
        make ("Hard Kick", "Hard", { { drive, 0.55f }, { peak, 0.6f }, { slide, 0.35f }, { clickAmount, 0.6f } }, "", { "drive" }),
        make ("Gabber", "Hard", { { drive, 0.85f }, { baseHz, hz (52.0f) }, { peak, 0.7f }, { slide, 0.45f }, { duration, 0.9f }, { output, 0.42f } }, "", { "distorted" }),
        make ("Rave Stomp", "Hard", { { drive, 0.7f }, { peak, 0.55f }, { slide, 0.4f }, { noiseMix, 0.35f }, { output, 0.45f } }, crush, { "distorted" }),
        make ("Dirt Kick", "Hard", { { drive, 0.4f }, { noiseMix, 0.45f }, { noiseD, 0.68f } }, crush),
        make ("Beater", "Noise", { { noiseMix, 0.4f }, { noiseD, 0.68f }, { noiseFilter, 0.8f } }, hiss),
        make ("Air Kick", "Noise", { { noiseMix, 0.5f }, { noiseD, 0.85f }, { noiseFilter, 1.0f }, { duration, 0.65f } }, hiss),
        make ("Tin Kick", "Noise", { { noiseMix, 0.5f }, { noiseD, 0.85f }, { noiseFilter, 1.0f } }, metal, { "metal" }),
        make ("Clap Kick", "Noise", { { noiseMix, 0.6f }, { noiseD, 0.85f }, { noiseFilter, 1.0f } }, clap, { "clap" }),
        make ("Late Clap", "Noise", { { noiseMix, 0.6f }, { noiseD, 0.85f }, { noiseStart, 0.75f }, { noiseFilter, 1.0f } }, clap, { "clap" }),
        make ("Low Tom", "Tom", { { baseHz, hz (82.0f) }, { peak, 0.25f }, { slide, 0.3f }, { duration, 0.75f } }, "", { "tom" }),
        make ("High Tom", "Tom", { { baseHz, hz (140.0f) }, { peak, 0.22f }, { slide, 0.3f }, { duration, 0.65f } }, "", { "tom" }),
        make ("Twin Drop", "Tom", { { baseHz, hz (64.0f) }, { varBase, 0.65f }, { varPeak, 0.6f }, { varPhase, 0.4f }, { duration, 0.8f } }, "", { "two oscillators" }),
    };
}
} // namespace lynx::banks
