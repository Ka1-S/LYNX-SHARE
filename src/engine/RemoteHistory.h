#pragma once

#include "core/Song.h"
#include <algorithm>

namespace lynx::remote_history
{
namespace detail
{
inline void mirrorGraph (const PatcherDoc&, PatcherDoc&);

inline void mirrorChannel (const ChannelDef& captured, ChannelDef& published)
{
    if (captured.instance.id != published.instance.id || captured.instrument != published.instrument) return;
    if (captured.instrument == InstrumentKind::plugin && captured.plugin.id == published.plugin.id)
        published.plugin.state = captured.plugin.state;
    if (captured.instrument == InstrumentKind::patcher) mirrorGraph (captured.patcher, published.patcher);
}

inline void mirrorEffect (const EffectSlot& captured, EffectSlot& published)
{
    if (captured.instance.id != published.instance.id || captured.kind != published.kind) return;
    if (captured.kind == EffectKind::plugin && captured.plugin.id == published.plugin.id)
        published.plugin.state = captured.plugin.state;
    if (captured.kind == EffectKind::patcher) mirrorGraph (captured.patcher, published.patcher);
}

inline void mirrorGraph (const PatcherDoc& captured, PatcherDoc& published)
{
    for (std::size_t i = 0; i < std::min (captured.nodes.size(), published.nodes.size()); ++i)
        if (captured.nodes[i].generator == published.nodes[i].generator)
        {
            if (captured.nodes[i].generator) mirrorChannel (captured.nodes[i].channel, published.nodes[i].channel);
            else mirrorEffect (captured.nodes[i].effect, published.nodes[i].effect);
        }
}
}

inline void mirrorCapturedHostedStates (const Song& captured, Song& published)
{
    // The published shape can contain stand-in projections and extra stem channels. Capture updates only saved
    // hosted-state bytes; do not replace that shape with raw project definitions or disturb its reuse identities.
    for (std::size_t i = 0; i < std::min (captured.channels.size(), published.channels.size()); ++i)
        detail::mirrorChannel (captured.channels[i], published.channels[i]);
    for (std::size_t t = 0; t < std::min (captured.mixer.size(), published.mixer.size()); ++t)
        for (std::size_t i = 0; i < std::min (captured.mixer[t].effects.size(), published.mixer[t].effects.size()); ++i)
            detail::mirrorEffect (captured.mixer[t].effects[i], published.mixer[t].effects[i]);
}

// Called only after a remote-owned restore and its live-state capture both succeeded. A vendor may normalize
// its saved bytes while restoring them. The inverse step expects that actual state. An adjacent owned step may
// adopt it only if its existing fence described exactly the saved target we restored; a different fence records
// an intervening producer device change and must continue to refuse undo/redo.
template <typename Entry, typename Hash>
void acceptRestoredState (Entry& inverse, Entry* adjacent, const Hash& savedTargetHash, const Hash& restoredHash)
{
    inverse.deviceHash = restoredHash;
    if (adjacent != nullptr && adjacent->remote && adjacent->deviceHash == savedTargetHash)
        adjacent->deviceHash = restoredHash;
}
}
