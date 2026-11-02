#include "core/BankStates.h"

#include <algorithm>
#include <cstring>

#include "core/DrumSynthLive.h"
#include "core/PhysicalModels.h"

namespace lynx::bankstates
{
namespace
{
void put32 (std::vector<std::uint8_t>& s, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        s.push_back ((std::uint8_t) (v >> (8 * i)));
}

void putFloat (std::vector<std::uint8_t>& s, float v)
{
    std::uint32_t u = 0;
    std::memcpy (&u, &v, 4);
    put32 (s, u);
}
} // namespace

std::vector<std::uint8_t> fruitKickState (const std::vector<int>& p)
{
    std::vector<std::uint8_t> s;
    put32 (s, 0); // version 0: the six parameters, no envelope snapshot (the channel's own envelope applies)
    for (int i = 0; i < 6; ++i)
        put32 (s, (std::uint32_t) (i < (int) p.size() ? p[(std::size_t) i] : 0));
    return s;
}

std::vector<std::uint8_t> pluckedState (const std::vector<int>& p)
{
    std::vector<std::uint8_t> s;
    for (int i = 0; i < 5; ++i)
        put32 (s, (std::uint32_t) (i < (int) p.size() ? p[(std::size_t) i] : 0));
    return s;
}

std::vector<std::uint8_t> drumaxxState (const std::string& kitName, const std::vector<DrumaxxPad>& pads, float master, bool limiter,
                                        float limiterDrive, const std::vector<float>& eq)
{
    constexpr int padCount = 16, padParams = 44, paramCount = 720;
    std::vector<float> P ((std::size_t) paramCount, 0.0f);
    for (int k = 0; k < padCount; ++k)
    {
        auto* p = P.data() + k * padParams;
        p[1] = 0.5f;           // pan centre
        p[4] = (float) (36 + k); // key
        if (k >= (int) pads.size())
            continue;
        const auto& pad = pads[(std::size_t) k];
        p[0] = pad.volume;
        p[1] = pad.pan;
        p[2] = pad.mute ? 1.0f : 0.0f;
        p[4] = (float) pad.key;
        for (int i = 0; i < 38 && i < (int) pad.voice.size(); ++i)
            p[5 + i] = pad.voice[(std::size_t) i];
    }
    P[0x2c1] = master;
    P[0x2c3] = eq.empty() ? 0.0f : 1.0f;
    for (int b = 0; b < 8; ++b)
        P[(std::size_t) (0x2c4 + b)] = b < (int) eq.size() ? eq[(std::size_t) b] : 0.5f;
    P[0x2cc] = limiter ? 1.0f : 0.0f;
    P[0x2cd] = limiterDrive;
    P[0x2ce] = 0.5f;

    std::vector<std::uint8_t> body;
    body.insert (body.end(), { '4', 'S', 'M', 'D' }); // tag DMS4 (little-endian 0x444d5334)
    std::vector<std::uint8_t> name (256, 0);
    std::memcpy (name.data(), kitName.data(), std::min<std::size_t> (kitName.size(), 255));
    body.insert (body.end(), name.begin(), name.end());
    for (const auto v : P)
        putFloat (body, v);
    for (int k = 0; k < padCount; ++k) // empty patterns: 0x400 steps, length 16, trigger key 100
    {
        std::vector<std::uint8_t> pattern (0x402, 0);
        pattern[0x400] = 16;
        pattern[0x401] = 100;
        body.insert (body.end(), pattern.begin(), pattern.end());
    }
    std::vector<std::uint8_t> names (0x800, 0);
    for (int k = 0; k < padCount && k < (int) pads.size(); ++k)
        std::memcpy (names.data() + 128 * k, pads[(std::size_t) k].name.data(), std::min<std::size_t> (pads[(std::size_t) k].name.size(), 127));
    body.insert (body.end(), names.begin(), names.end());
    body.insert (body.end(), (std::size_t) padCount, 0); // cut groups
    body.insert (body.end(), (std::size_t) padCount, 0); // output buses (varint 0 each)

    std::vector<std::uint8_t> s { 'S', 'S', 'L', 'F' };
    put32 (s, (std::uint32_t) body.size());
    s.insert (s.end(), body.begin(), body.end());
    return s;
}

std::vector<float> drumSynthParams (const std::vector<DrumSynthPatch>& patches)
{
    std::vector<float> m ((std::size_t) drumSynthParamCount, 0.0f);
    for (int slot = 0; slot < drumSynthPatches; ++slot) // silent patches: level 0, centred knobs
    {
        auto* p = m.data() + drumSynthPatchParams * slot;
        p[4] = p[10] = 500.0f;
        p[15] = -1.0f;
        p[5] = p[6] = p[7] = p[8] = p[9] = p[11] = p[12] = p[13] = p[14] = p[17] = p[19] = p[20] = p[21] = 64.0f;
        p[35] = 50.0f;
        p[37] = 64.0f;
    }
    auto editorKey = 60;
    for (const auto& patch : patches)
    {
        const auto slot = drumSynthKeyToSlot (patch.key);
        if (slot < 0)
            continue;
        if (&patch == &patches.front())
            editorKey = patch.key;
        for (int i = 0; i < drumSynthPatchParams && i < (int) patch.values.size(); ++i)
            m[(std::size_t) (drumSynthPatchParams * slot + i)] = (float) patch.values[(std::size_t) i];
    }
    m[(std::size_t) drumSynthEditorKeyIndex] = (float) editorKey;
    m[(std::size_t) drumSynthEditorKeyIndex + 1] = 4.0f; // state version (informational)
    return m;
}

std::optional<ChannelDef> channelFor (const std::string& kind, const std::vector<float>& v)
{
    ChannelDef def;
    auto ints = [&v] {
        std::vector<int> out;
        for (const auto x : v)
            out.push_back ((int) x);
        return out;
    };
    if (kind == "fruitkick" && v.size() >= 6)
    {
        def.instrument = InstrumentKind::fruitkick;
        def.pluginState = fruitKickState (ints());
    }
    else if (kind == "plucked" && v.size() >= 5)
    {
        def.instrument = InstrumentKind::plucked;
        def.pluginState = pluckedState (ints());
    }
    else if (kind == "drumpad" && v.size() >= (std::size_t) drumpadParamCount)
    {
        def.instrument = InstrumentKind::drumpad;
        def.modelParams.assign (v.begin(), v.begin() + drumpadParamCount);
    }
    else if (kind == "sakura" && v.size() >= 114)
    {
        def.instrument = InstrumentKind::sakura;
        def.modelParams.assign ((std::size_t) sakuraParamCount, 0.0f);
        std::copy (v.begin(), v.begin() + 114, def.modelParams.begin());
    }
    else
        return std::nullopt;
    return def;
}
} // namespace lynx::bankstates
