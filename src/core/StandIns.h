#pragma once

// Online Sessions, phase 1 (docs/design/ONLINE_SESSIONS.md §4.2, round 168a): stand-ins — renders of the parts that
// need a third-party plugin, made where the plugin is installed and played where it is not.
//
// The renders themselves run through an ExportJob (engine/SessionPack.cpp); this file has the pure parts: which
// channels / inserts need one, the songs that are rendered and how the result is cut, and the local projection of the
// song the audio engine plays where a plugin is missing (never saved, never shown).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::standins
{
// Third-party plugins (hosted VST3 / VST2 / CLAP) a channel / an effect slot / an insert uses, directly or inside a
// Patcher or a parallel container. LYNX's own engines (the FL engines included) are never stand-ins: the same LYNX
// plays them the same on every computer.
bool slotUsesPlugin (const EffectSlot& slot);
bool channelUsesPlugin (const ChannelDef& channel);
bool insertUsesPlugin (const MixerTrack& track);
// The plugins themselves (each PluginRef once, by id), for the manifest and the badges.
void pluginsOf (const ChannelDef& channel, std::vector<const PluginRef*>& out);
void pluginsOf (const MixerTrack& track, std::vector<const PluginRef*>& out);

// The longest tail a pattern stem keeps (4 bars, the design's limit).
std::int64_t tailTicks (const Song& song) noexcept;

// A channel's pattern stems come from ONE render (one plugin instance): each pattern with notes of the channel is
// played three times — once alone (+ the tail: the Song-mode stem) and twice back to back, the second pass being the
// loop stem (the first pass's tail sounds over its start, as when the pattern loops).
struct Segment
{
    int pattern = 0;
    std::uint64_t patternUid = 0;
    std::int64_t length = 0;    // the pattern's length (ticks)
    std::int64_t once = 0;      // where the single pass starts (its tail follows up to `loopFrom - length`)
    std::int64_t loopFrom = 0;  // where the loop pass starts
};
struct ChannelRender
{
    Song song;                      // render it whole (Song mode) with no tail beyond `end`
    std::vector<Segment> segments;  // empty: the channel plays in no pattern
    std::int64_t tail = 0, end = 0; // ticks
};
ChannelRender channelPatternRender (const Song& song, int channel);
// A channel whose plugin follows song-level automation (of its parameters), or a song with tempo automation, gets a
// timeline stem instead (the pattern stems would not follow).
bool needsTimeline (const Song& song, int channel);
// The whole song with only `channel` heard, before its insert (volume / pan / insert left out).
Song channelTimelineRender (const Song& song, int channel);
// The whole song with only insert `track`'s output heard, after its effects, before its fader / pan / EQ.
Song insertTimelineRender (const Song& song, int track);

// The audio clip gain that plays a stem at the level it was rendered (an audio clip is unity at FL's default channel
// volume, the channel's volume curve applies on top — Instruments.cpp audioClipUnity).
float stemGain() noexcept;

// Which channels / inserts play a stand-in in the projected song (index = channel / insert).
struct Projected
{
    std::vector<int> channels, inserts;
};
// The song the audio engine plays: channels and inserts whose third-party plugin is missing here (`missing`) and that
// have stand-ins play them instead — Song mode: the pattern stems at their clips (or the timeline stem), Pattern mode:
// the current pattern's loop stem; an insert's timeline stem replaces its effects and inputs in Song mode. Without any
// stand-in to play, `song` is returned as is.
// Frozen channels (round 190ay, FreezeState) play their stems the same way whether or not the plugin is installed: the
// whole-song stem in Song mode, the current pattern's loop stem in Pattern mode. They are not listed in `out`.
bool hasAny (const Song& song) noexcept; // any channel or insert has stand-ins, or a channel is frozen
Song project (const Song& song, const std::function<bool (const PluginRef&)>& missing, Projected* out = nullptr);
// The stand-in of a channel / insert for pattern `patternUid` (0 = the timeline stem); nullptr if none.
const StandIn* find (const std::vector<StandIn>& list, std::uint64_t patternUid, bool loop) noexcept;
} // namespace lynx::standins

// Freeze (round 190ay): a channel rendered to audio that plays in its place while its instrument is not loaded at all.
// The renders run through sessionpack::renderFreeze (engine/SessionPack.cpp); the playback is standins::project.
namespace lynx::freeze
{
// Why `channel` cannot be frozen, as an English sentence for the UI (translate it); nullptr = it can.
const char* whyNot (const Song& song, int channel) noexcept;
// The channel with that uid plays `stems` from now on (its instrument is unloaded at the next publish).
void apply (Song& song, std::uint64_t channelUid, std::vector<StandIn> stems, std::string source);
// The channel plays its instrument again (the files are the caller's to delete).
void clear (Song& song, int channel);
bool any (const Song& song) noexcept;
// The song with nothing frozen: what the project exports (.flp, .als) write and what a stem render hears.
Song thawed (Song song);
// A stem render's song made light: only `channel` matters there (every other channel is muted, the audio goes straight
// to the Master), so the other channels' instruments and every insert's effects are swapped for idle built-in ones
// (same channel / slot numbers) — freezing one channel does not load every plugin of the project a second time.
void lighten (Song& renderSong, int channel);
} // namespace lynx::freeze
