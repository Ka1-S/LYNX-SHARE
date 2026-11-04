#include "core/BankStates.h"

#include <algorithm>
#include <cmath>
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
        p[4] = 0.0f;           // key: an unused pad is on key 0, as in FL's Default Kit (REA_rodada169e §1)
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
    const auto put = [&m] (const DrumSynthPatch& patch, int key) {
        const auto slot = drumSynthKeyToSlot (key);
        if (slot < 0)
            return;
        for (int i = 0; i < drumSynthPatchParams && i < (int) patch.values.size(); ++i)
            m[(std::size_t) (drumSynthPatchParams * slot + i)] = (float) patch.values[(std::size_t) i];
    };
    // FL's own kits cover every key: its fresh DrumSynth Live (the Plugin database's state) repeats its 24-patch kit
    // over the 120 keys (key k plays patch k mod 24, REA_rodada169e §3). LYNX's kits are one-octave kits, repeated the
    // same way every 12 keys, so the step sequencer's C5 (60) plays the kit's C like any other C.
    for (int key = 0; key < drumSynthPatches; ++key)
        for (const auto& patch : patches)
            if (((patch.key % 12) + 12) % 12 == key % 12)
            {
                put (patch, key);
                break;
            }
    for (const auto& patch : patches) // (a patch on its own key wins over a repeat)
    {
        if (drumSynthKeyToSlot (patch.key) < 0)
            continue;
        if (&patch == &patches.front())
            editorKey = patch.key;
        put (patch, patch.key);
    }
    m[(std::size_t) drumSynthEditorKeyIndex] = (float) editorKey;
    m[(std::size_t) drumSynthEditorKeyIndex + 1] = 4.0f; // state version (informational)
    return m;
}

std::optional<ChannelDef> channelFor (const std::string& kind, const std::vector<float>& v, const std::string& name)
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
    else if (kind == "drumaxx" && ! v.empty() && v.size() % 41 == 0)
    {
        // per pad: key, volume, pan, the 38 voice values (Drumpad's)
        std::vector<DrumaxxPad> pads;
        for (std::size_t at = 0; at + 41 <= v.size() && pads.size() < 16; at += 41)
        {
            DrumaxxPad pad;
            pad.key = (int) v[at], pad.volume = v[at + 1], pad.pan = v[at + 2];
            pad.voice.assign (v.begin() + (long) at + 3, v.begin() + (long) at + 41);
            pads.push_back (pad);
        }
        def.instrument = InstrumentKind::drumaxx;
        def.pluginState = drumaxxState (name.empty() ? std::string ("LYNX") : name, pads);
    }
    else if (kind == "drumsynth" && ! v.empty() && v.size() % 39 == 0)
    {
        // per patch: key, the 38 patch integers
        std::vector<DrumSynthPatch> patches;
        for (std::size_t at = 0; at + 39 <= v.size(); at += 39)
        {
            DrumSynthPatch patch;
            patch.key = (int) v[at];
            for (std::size_t i = 0; i < 38; ++i)
                patch.values.push_back ((int) std::lround (v[at + 1 + i]));
            patches.push_back (patch);
        }
        def.instrument = InstrumentKind::drumsynth;
        def.modelParams = drumSynthParams (patches);
    }
    else
        return std::nullopt;
    return def;
}
} // namespace lynx::bankstates
