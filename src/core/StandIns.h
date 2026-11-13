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
// withInsert (round 190ff, track freeze): the channel with its volume and pan, through its insert's effects (that insert
// at unity, its EQ, pan and sends left out — they apply live), instead of the channel's raw output.
ChannelRender channelPatternRender (const Song& song, int channel, bool withInsert = false);
// A channel whose plugin follows song-level automation (of its parameters), or a song with tempo automation, gets a
// timeline stem instead (the pattern stems would not follow).
bool needsTimeline (const Song& song, int channel);
// The whole song with only `channel` heard, before its insert (volume / pan / insert left out); withInsert: through its
// insert's effects, as channelPatternRender.
Song channelTimelineRender (const Song& song, int channel, bool withInsert = false);
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
// The channel with that uid plays `stems` from now on (its instrument is unloaded at the next publish). insert > 0: a
// track freeze (round 190ff), the stems hold that insert's effects too.
void apply (Song& song, std::uint64_t channelUid, std::vector<StandIn> stems, std::string source, int insert = 0);
// The channel plays its instrument again (the files are the caller's to delete).
void clear (Song& song, int channel);
bool any (const Song& song) noexcept;
// The song with nothing frozen: what the project exports (.flp, .als) write and what a stem render hears.
Song thawed (Song song);
// A stem render's song made light: only `channel` matters there (every other channel is muted, the audio goes straight
// to the Master), so the other channels' instruments and every insert's effects are swapped for idle built-in ones
// (same channel / slot numbers) — freezing one channel does not load every plugin of the project a second time.
// keepInsert (round 190ff): the effects of that insert stay (a track freeze renders through them).
void lighten (Song& renderSong, int channel, int keepInsert = -1);

// --- Track freeze (round 190ff; the Clip-launch workflow's Freeze Track, Bounce Track in Place) ----------------------
// Live's Freeze Track renders a track with all its devices; its mixer (volume, pan, sends) stays live. A LYNX track is a
// Playlist track whose chain is a channel and the insert it plays through (core/DetailChain.h), so a track freeze
// renders the channel with its volume and pan through that insert's effects, before the insert's EQ and fader, and
// plays it into the same insert with its effects unloaded.

// The insert a track freeze of `channel` takes along: the channel's insert when it is not the Master, else 0 (then a
// track freeze is the channel freeze).
int trackInsert (const Song& song, int channel) noexcept;
// Why `channel` cannot be track-frozen (whyNot, plus the insert's rules: nothing else may play into its insert, and its
// effects may not listen to or send to other inserts); nullptr = it can.
const char* whyNotTrack (const Song& song, int channel) noexcept;
// The channel is track-frozen and that still holds: it plays into the insert it was frozen with and nothing else does.
// A track freeze that does not hold plays live (instrument and effects), never a doubled or wrong sound.
bool trackFreezeHolds (const Song& song, int channel) noexcept;
// The channel whose track freeze holds the effects of `insert` (they are not loaded, their editors have nothing to
// show); -1 = none.
int frozenByInsert (const Song& song, int insert) noexcept;

// Live's Flatten (Bounce Track in Place on a frozen track, Live 12.2 on): the frozen channel `channel` becomes an audio
// clip channel for good ("<name> (Bounce)", as the reference names a bounced track), playing its whole-song stem in the
// Playlist: one audio clip at every clip that played it, same place, length and track, each followed by a tail clip
// until the next one (Live's "freeze tail" clips), so the arrangement plays the frozen sound bit for bit. Its notes
// leave every pattern (pattern clips left empty go); a track freeze also removes the insert's effects and what drove
// them and the channel's volume / pan. Returns false (nothing changed) when the channel is not frozen (or its track
// freeze does not hold), has no whole-song stem or plays in no clip. Afterwards the audio lives in the Playlist only:
// Pattern mode no longer plays the channel.
bool flatten (Song& song, int channel);

// Live's Bounce to New Track (12.2 on) for the selected clips of one Playlist track: the track's sound (its channel with
// volume and pan through its insert's effects, as a track freeze hears it) over the clips' span is rendered into one
// audio clip on a new track below; the selected clips are muted (Live 12 manual, "Bounce to Audio"), and the new track
// keeps the source's mixer (an unused insert gets the source insert's fader, pan, EQ, sends and routing).
struct BouncePlan
{
    int channel = -1;          // the track's channel (detailchain::forTrack)
    int insert = 0;            // its insert whose effects are in the render (0: none, the channel goes to the Master)
    int track = 0;             // the Playlist track
    std::vector<int> clips;    // indices into song.playlist
    std::int64_t start = 0, end = 0;
};
// nullptr = go (plan filled); else why not, as an English sentence for the UI.
const char* planBounceToNewTrack (const Song& song, const std::vector<int>& selected, BouncePlan& plan);
// The song to render (exportopts::Range::selection over [start, end), the tail cut): only the plan's clips (and the
// automation clips) play, through the track's devices; the rest is lightened as for a freeze.
Song bounceRenderSong (const Song& song, const BouncePlan& plan);
// After the render: the plan's clips muted; a new Playlist track under the plan's track holds one audio clip of a new
// audio clip channel (`samplePath`, "<channel> (Bounce)", its colour) over the span, playing into an unused insert set
// like the source's (none free: the Master at unity). Returns the channel.
int applyBounceToNewTrack (Song& song, const BouncePlan& plan, const std::string& samplePath, std::int64_t lengthTicks);

// Live's Consolidate (Ctrl+J) for the selected clips of one Playlist track: they become one clip over their span.
// Pattern clips: a new pattern holding the notes the clips play there (each clip's offset, length and loops), the clips
// replaced by one clip of it — no render. Audio clips of one channel: their audio as played (gain, fades, pitch, the
// channel's sampler, before its volume / pan / insert) is rendered into a new audio clip channel with the source's
// volume, pan and insert, whose one clip replaces them.
struct ConsolidatePlan
{
    bool audio = false;
    int channel = -1;          // audio: the clips' channel
    int track = 0;
    std::vector<int> clips;
    std::int64_t start = 0, end = 0;
};
const char* planConsolidate (const Song& song, const std::vector<int>& selected, ConsolidatePlan& plan);
// Pattern clips: does it (returns the new pattern, −1 = nothing done).
int consolidatePatternClips (Song& song, const ConsolidatePlan& plan);
// Audio clips: the song to render (Range::selection over the span, tail cut) and what follows the render.
Song consolidateRenderSong (const Song& song, const ConsolidatePlan& plan);
int applyAudioConsolidation (Song& song, const ConsolidatePlan& plan, const std::string& samplePath, std::int64_t lengthTicks);
} // namespace lynx::freeze
