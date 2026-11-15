#pragma once

#include "core/RackOps.h"
#include <optional>
#include <stdexcept>

namespace lynx::remote_instruments
{
// Stage complete model edits before touching the engine or its undo stack.
struct Result { Song song; int channelIndex; };
inline Result create (const Song& source, ChannelDef generator, std::optional<int> at = {}, std::optional<int> mixer = {})
{
    const auto count = (int) source.channels.size();
    const auto index = at.value_or (count);
    if (count >= 100000 || index < 0 || index > count)
        throw std::runtime_error ("channelIndex must be an insertion position in the current Channel Rack");
    if (mixer && (*mixer < 0 || *mixer >= (int) source.mixer.size()))
        throw std::runtime_error ("mixerIndex must identify an existing mixer insert");
    auto next = source;
    generator.mixerTrack = mixer ? *mixer : next.claimFreeInsert();
    insertChannel (next, index, std::move (generator));
    return { std::move (next), index };
}
inline Result replace (const Song& source, int index, ChannelDef generator)
{
    if (index < 0 || index >= (int) source.channels.size())
        throw std::runtime_error ("channelIndex must identify an existing channel");
    const auto& previous = source.channels[(std::size_t) index];
    // These are channel preferences/identity, not the generator's own state.
    generator.uid = previous.uid;
    generator.maxPolyphony = previous.maxPolyphony;
    generator.arp = previous.arp;
    generator.cutGroup = previous.cutGroup;
    generator.cutBy = previous.cutBy;
    generator.misc = previous.misc;
    generator.keyLabels = previous.keyLabels;
    generator.ghostHidden = previous.ghostHidden;
    generator.soloLock = previous.soloLock;
    generator.keyLow = previous.keyLow;
    generator.keyHigh = previous.keyHigh;
    generator.receiveFrom = previous.receiveFrom;
    generator.receiveMidiChannel = previous.receiveMidiChannel;
    generator.icon = previous.icon;
    generator.loopStarterPattern = previous.loopStarterPattern;
    generator.loopStarterSlot = previous.loopStarterSlot;
    generator.loopStarterCategory = previous.loopStarterCategory;
    generator.loopStarterLocked = previous.loopStarterLocked;
    auto next = source;
    replaceGenerator (next, index, std::move (generator));
    return { std::move (next), index };
}
}
