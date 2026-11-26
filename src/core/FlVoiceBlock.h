#pragma once

// Round 190rt (issue #311): the generators whose plugin state carries Image-Line's plugin voice processor block (the
// 364-byte channel-settings snapshot: the PAN / VOL / MOD X / MOD Y / PITCH envelopes and LFOs and the channel filter,
// core/FlVoiceSettings.h) — 3x Osc, Fruity DX10, Fruity Granulizer and BeepMap. In FL every generator's channel window
// has the "Envelope / instrument settings" tab that edits it; LYNX's engines already play it (bit-exact ports), so the
// tab is FL's page (ui/FlVoicePanel) over these bytes. Fruity Slicer keeps its own block (core/FruitySlicer.h) and
// FL-project Samplers theirs (SamplerSettings::flChannel, flsamplervoice).

#include <cstring>
#include <optional>
#include <vector>

#include "core/BeepMap.h"
#include "core/Dx10.h"
#include "core/FlVoiceSettings.h"
#include "core/Granulizer.h"
#include "core/Song.h"
#include "core/ThreeOsc.h"

namespace lynx::flvoiceblock
{
constexpr std::size_t blockSize = (std::size_t) flvp::snapshotSize;

// where the block sits in a state of this kind that already carries one (3x Osc v14, DX10 v2, Granulizer v7 normalised,
// BeepMap v1); none for a state without it (the plugin then plays its defaults)
inline std::optional<std::size_t> offsetIn (InstrumentKind kind, const std::vector<std::uint8_t>& state) noexcept
{
    std::size_t at = 0;
    switch (kind)
    {
        case InstrumentKind::threeosc: at = 0x60; break;
        case InstrumentKind::dx10: at = 96; break;
        case InstrumentKind::granulizer: at = 64; break;
        case InstrumentKind::beepmap:
        {
            std::int32_t version = 0;
            if (state.size() < 4)
                return std::nullopt;
            std::memcpy (&version, state.data(), 4);
            if (version != 1)
                return std::nullopt;
            at = 0x40;
            break;
        }
        default: return std::nullopt;
    }
    if (state.size() < at + blockSize)
        return std::nullopt;
    return at;
}

// the channel has the tab: one of these generators with a state its engine reads (a .flm 3x Osc keeps FLM's own module)
inline bool has (const ChannelDef& c)
{
    switch (c.instrument)
    {
        case InstrumentKind::threeosc: return threeOscStateUsable (c.pluginState) && ! threeOscIsFlm (c);
        case InstrumentKind::dx10: return dx10StateUsable (c.pluginState);
        case InstrumentKind::granulizer: return granulizerStateUsable (c.pluginState);
        case InstrumentKind::beepmap: return beepMapStateUsable (c.pluginState);
        default: return false;
    }
}

// the block's bytes (the plugin's defaults when the state carries none)
inline std::vector<std::uint8_t> bytes (const ChannelDef& c)
{
    if (const auto at = offsetIn (c.instrument, c.pluginState))
        return { c.pluginState.begin() + (std::ptrdiff_t) *at, c.pluginState.begin() + (std::ptrdiff_t) (*at + blockSize) };
    const auto defaults = flvp::defaultSnapshot();
    return { defaults.begin(), defaults.end() };
}

inline std::optional<flvoice::Settings> read (const ChannelDef& c)
{
    if (! has (c))
        return std::nullopt;
    return flvoice::read (bytes (c));
}

// the state with `block` as its channel settings (a state without one gains it: DX10 v2, Granulizer v7, BeepMap v1)
inline bool setBytes (ChannelDef& c, const std::vector<std::uint8_t>& block)
{
    if (! has (c) || block.size() != blockSize)
        return false;
    if (const auto at = offsetIn (c.instrument, c.pluginState))
    {
        std::memcpy (c.pluginState.data() + *at, block.data(), blockSize);
        return true;
    }
    std::vector<std::uint8_t> out;
    switch (c.instrument)
    {
        case InstrumentKind::threeosc: out = threeOscNormalisedState (c.pluginState, block); break;
        case InstrumentKind::dx10: out = dx10StateWithSnapshot (c.pluginState, block, true); break;
        case InstrumentKind::granulizer: out = granulizerNormalisedState (c.pluginState, block); break;
        case InstrumentKind::beepmap:
            // version 0 → 1: the block after the 15 settings (FUN_0072ebc0 reads it there), the bitmap stream as it was
            if (c.pluginState.size() >= 0x40)
            {
                out.assign (c.pluginState.begin(), c.pluginState.begin() + 0x40);
                const std::int32_t one = 1;
                std::memcpy (out.data(), &one, 4);
                out.insert (out.end(), block.begin(), block.end());
                out.insert (out.end(), c.pluginState.begin() + 0x40, c.pluginState.end());
            }
            break;
        default: break;
    }
    if (out.empty() || ! offsetIn (c.instrument, out))
        return false;
    c.pluginState = std::move (out);
    return true;
}

inline bool write (ChannelDef& c, const flvoice::Settings& s)
{
    auto block = bytes (c);
    flvoice::write (block, s);
    return setBytes (c, block);
}
} // namespace lynx::flvoiceblock
