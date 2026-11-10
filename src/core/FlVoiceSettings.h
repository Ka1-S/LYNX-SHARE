#pragma once

// What FL's "Envelope / instrument settings" page edits on a generator that runs Image-Line's plugin voice processor
// (core/FlVoiceProcessor.h): the 364-byte block its state carries — five articulators (pan, volume, Mod X, Mod Y,
// pitch; each an envelope and an LFO) and the channel filter. The fields keep FL's own units, so a block read and
// written back is the same bytes (round 190f, issue #129).

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/FlVoiceProcessor.h"

namespace lynx::flvoice
{
enum Target
{
    pan = 0,
    volume,
    cutoff,    // FL's "Mod X"
    resonance, // FL's "Mod Y"
    pitch,
    targetCount
};

// One articulator: FL's 17-int record (the plugins' copy of a channel's 0xDA record)
struct Articulator
{
    std::int32_t flags = 0;       // bit 0: envelope times follow the tempo; bit 1: the LFO's; bit 5: global LFO; others kept
    bool envelope = false;        // the envelope is on
    std::int32_t delay = 100;     // times: FL's knob values, 100 (none) … 65536 (flp::Channel::envelopeTime)
    std::int32_t attack = 20000;
    std::int32_t hold = 20000;
    std::int32_t decay = 30000;
    std::int32_t sustain = 50;    // 0…128
    std::int32_t release = 20000;
    std::int32_t amount = 0;      // −128…128 (pitch: cents)
    std::int32_t lfoDelay = 100;
    std::int32_t lfoAttack = 20000;
    std::int32_t lfoAmount = 0;   // −128…128 (pitch: cents)
    std::int32_t lfoSpeed = 32950;
    std::int32_t lfoShape = 0;    // 0 sine, 1 triangle, 2 pulse
    std::int32_t attackTension = 0, decayTension = 0, releaseTension = 0; // −128…128

    bool envelopeTempo() const noexcept { return (flags & 1) != 0; }
    bool lfoTempo() const noexcept { return (flags & 2) != 0; }
    bool lfoGlobal() const noexcept { return (flags & 0x20) != 0; }
    void setFlag (std::int32_t bit, bool on) noexcept { flags = on ? (flags | bit) : (flags & ~bit); }

    bool operator== (const Articulator&) const = default;
};

struct Settings
{
    std::array<Articulator, targetCount> articulators {};
    std::int32_t modX = 256, modY = 0; // the filter's cutoff and resonance, 0…256
    std::int32_t filterType = 0;       // 0 fast LP, 1 LP, 2 BP, 3 HP, 4 BS, 5 LP ×2, 6 SVF LP, 7 SVF LP ×2

    bool operator== (const Settings&) const = default;
};

// The settings of a block (one that is not 364 bytes long reads as the plugins' defaults)
inline Settings read (const std::vector<std::uint8_t>& block)
{
    auto bytes = flvp::defaultSnapshot();
    if (block.size() == bytes.size())
        std::memcpy (bytes.data(), block.data(), bytes.size());

    Settings s;
    for (int k = 0; k < targetCount; ++k)
    {
        std::int32_t r[17];
        std::memcpy (r, bytes.data() + 0x44 * k, sizeof (r));
        auto& a = s.articulators[(std::size_t) k];
        a.flags = r[0], a.envelope = r[1] != 0;
        a.delay = r[2], a.attack = r[3], a.hold = r[4], a.decay = r[5], a.sustain = r[6], a.release = r[7], a.amount = r[8];
        a.lfoDelay = r[9], a.lfoAttack = r[10], a.lfoAmount = r[11], a.lfoSpeed = r[12], a.lfoShape = r[13];
        a.attackTension = r[14], a.decayTension = r[15], a.releaseTension = r[16];
    }
    std::memcpy (&s.modX, bytes.data() + 0x15c, 4);
    std::memcpy (&s.modY, bytes.data() + 0x160, 4);
    std::memcpy (&s.filterType, bytes.data() + 0x168, 4);
    return s;
}

// Writes the settings into the block: an untouched field keeps its bytes (FL writes −1 for an envelope that is on, and
// a block may hold other truthy values), and so do the bytes the settings do not cover
inline void write (std::vector<std::uint8_t>& block, const Settings& s)
{
    if (block.size() != (std::size_t) flvp::snapshotSize)
    {
        const auto defaults = flvp::defaultSnapshot();
        block.assign (defaults.begin(), defaults.end());
    }
    for (int k = 0; k < targetCount; ++k)
    {
        const auto& a = s.articulators[(std::size_t) k];
        std::int32_t r[17];
        std::memcpy (r, block.data() + 0x44 * k, sizeof (r));
        if ((r[1] != 0) != a.envelope)
            r[1] = a.envelope ? -1 : 0;
        r[0] = a.flags;
        r[2] = a.delay, r[3] = a.attack, r[4] = a.hold, r[5] = a.decay, r[6] = a.sustain, r[7] = a.release, r[8] = a.amount;
        r[9] = a.lfoDelay, r[10] = a.lfoAttack, r[11] = a.lfoAmount, r[12] = a.lfoSpeed, r[13] = a.lfoShape;
        r[14] = a.attackTension, r[15] = a.decayTension, r[16] = a.releaseTension;
        std::memcpy (block.data() + 0x44 * k, r, sizeof (r));
    }
    std::memcpy (block.data() + 0x15c, &s.modX, 4);
    std::memcpy (block.data() + 0x160, &s.modY, 4);
    std::memcpy (block.data() + 0x168, &s.filterType, 4);
}
} // namespace lynx::flvoice
