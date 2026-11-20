#pragma once

// ARA 2 on audio clips (round 190kx; a LYNX extra: FL Studio 2026 and Ableton Live 12 host no ARA, LYNX hosts it as
// Cubase, Studio One, Logic and REAPER do). What the song keeps of it. No JUCE.
//
// - An audio clip channel (InstrumentKind::audioClip) can be "edited in" an ARA plugin (Melodyne, RipX, VocAlign,
//   SpectraLayers, Auto-Tune ARA…): ChannelDef::ara names the plugin. Its clips then play through that plugin's ARA
//   playback renderer instead of the channel's sampler (core/AraRegions.h maps the clips to ARA playback regions); the
//   channel's volume, pan, mixer track and everything after it stay as they are.
// - The plugin's own edits (its notes, pitch, timing...) live in the plugin's ARA document; LYNX stores that document
//   in the project (Song::araDocuments, one per plugin: the archive the plugin writes, base64) and gives it back on open.
// - Nothing of this exists in a song that does not use it, and nothing runs (no plugin, no document) until a channel
//   asks for an ARA plugin: ARA costs no CPU when it is not used.

#include <cstdint>
#include <string>
#include <vector>

namespace lynx
{
struct AraLink
{
    std::string pluginId;   // the plugin (JUCE's identifier string, as PluginRef::id); empty = not edited in ARA
    std::string pluginName; // shown when the plugin is not installed
    bool active() const noexcept { return ! pluginId.empty(); }
    bool operator== (const AraLink&) const = default;
};

// One ARA document of the song: every channel edited in that plugin shares it (as the hosts that support ARA do: one
// document controller per plugin and project).
struct AraDocumentArchive
{
    std::string pluginId;
    std::string archive; // base64 of the bytes the plugin wrote (ARA storeObjectsToArchive)
    std::string archiveId; // the plugin's document archive id when it wrote it (ARA: handed back with the archive)
    bool operator== (const AraDocumentArchive&) const = default;
};
} // namespace lynx
