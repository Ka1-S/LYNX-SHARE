#pragma once

// Clip properties and arrangement management (FL's "Clip properties" and "Merge arrangement" windows, interpreted;
// REA_rodada84). Plain edits on a Song, each run inside AudioEngine::editSong (one undo step). No JUCE.

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::clipprops
{
// What the Clip properties window shows and edits. Name and colour belong to the clip's source (the pattern, the audio
// clip's channel or the automation), so they change every clip of that source; pitch and stretch are the audio
// channel's (every clip of that file).
struct Props
{
    ClipKind kind = ClipKind::pattern;
    std::string name;
    std::uint32_t colour = 0; // 0 = automatic
    int source = -1;          // pattern / audio channel / automation index
    int track = 0;
    std::int64_t start = 0, length = 0, offset = 0;
    float gain = 1.0f;        // audio
    float pan = 0.0f;         // audio: -1..1
    std::int64_t fadeIn = 0, fadeOut = 0; // audio
    bool muted = false;
    float pitch = 0.0f;       // audio: semitones (-24..24)
    float stretchBeats = 0.0f; // audio: 0 = the file's own speed
    bool keepPitch = false;

    bool operator== (const Props&) const = default;
};

// The clip's properties; false when `clip` does not exist.
bool read (const Song& song, int clip, Props& out);

// Applies `p` to the clip (start/length/offset are clamped: length ≥ 1 tick, offset ≥ 0, start ≥ 0; fades fit in
// the clip). A source that does not exist, or of another kind, is ignored. False when nothing could be applied.
bool apply (Song& song, int clip, const Props& p);

// For several clips at once (FL's "Mute all / Unmute all / Flip state"):
enum class MuteChange
{
    keep,
    muteAll,
    unmuteAll,
    flip
};
void changeMute (Song& song, const std::vector<int>& clips, MuteChange change);
// Audio clips' gain: set to `value`, or multiply (`relative`).
void changeGain (Song& song, const std::vector<int>& clips, float value, bool relative);

// Positions and lengths as the window shows them, "bar:beat:tick" (ticks at LYNX's PPQ); a position counts from 1 ("1:1:000" is the start), a length from 0 ("2:0:000" = two bars). Time signature
// changes from markers are not applied (bars of `beatsPerBar` beats).
std::string formatPosition (std::int64_t ticks, int beatsPerBar);
std::string formatLength (std::int64_t ticks, int beatsPerBar);
// The inverse; also accepts "bar" or "bar:beat". -1 when the text is not a time.
std::int64_t parsePosition (const std::string& text, int beatsPerBar);
std::int64_t parseLength (const std::string& text, int beatsPerBar);

// Sources a clip of this kind can switch to: their names (patterns / audio channels / automations).
std::vector<std::pair<int, std::string>> sourcesFor (const Song& song, ClipKind kind);
} // namespace lynx::clipprops

namespace lynx::arrangeops
{
enum class MergeMode
{
    insert,  // the source's clips go in at the position; what follows moves later by the source's length
    merge,   // laid over what is there
    replace  // what is there over the source's length is removed first
};

enum class MergePosition
{
    start,
    current, // the playback position
    end      // after the last clip
};

// The clips of arrangement `index` (the playlist for the current one).
const std::vector<Clip>& clipsOf (const Song& song, int index);

// End of the last clip of arrangement `index` (0 when empty).
std::int64_t lengthOf (const Song& song, int index);

// The tick where a merge goes. `currentTick` = the playback position; `bar` rounds the end up to a bar.
std::int64_t positionTick (const Song& song, MergePosition position, std::int64_t currentTick);

// Removes what the current arrangement has in [from, to) (clips crossing the edges are cut) — without moving the rest.
void clearRange (std::vector<Clip>& clips, std::int64_t from, std::int64_t to);

// Merges arrangement `source` into the current one at `at`. False when `source` is the current one or missing.
bool merge (Song& song, int source, MergeMode mode, std::int64_t at);

// Moves arrangement `from` to `to` (the current one stays current).
void move (Song& song, int from, int to);

// Renames (an empty name is ignored).
void rename (Song& song, int index, const std::string& name);

// Duplicates arrangement `index` after itself (not selected); returns the new index.
int duplicate (Song& song, int index);
} // namespace lynx::arrangeops

// FL's Playlist track modes and "Consolidate track(s)" (REA round 146a).
namespace lynx::trackmodes
{
// A mixer track nothing uses yet (no channel routed to it, no track linked to it, no effects, nothing sent into it),
// from insert 1 on; a new insert is added when there is none and the mixer has room. -1 when the mixer is full
// (FL: "Can't create new audio or instrument track: no available mixer tracks"). In an online session an insert
// reserved for another participant is never taken (Song::nextFreeInsert's rule).
int freeMixerTrack (Song& song);
// "Track mode › Audio track": links the track to a free mixer track (named and coloured after the track). False when
// no mixer track is free.
bool makeAudioTrack (Song& song, int track);
// "Track mode › Instrument track": the track belongs to `channel` (an instrument, not an audio clip or automation
// channel) and to its mixer track (a free one when the channel plays on the Master or on a track already linked).
bool makeInstrumentTrack (Song& song, int track, int channel);
// "Track mode › Audio track › N. <insert>" (FL 2026: the submenu lists the mixer tracks; issue #15's "route the track
// itself to 14"; REA round 190cp, FUN_012f38f0): the track becomes an audio track linked to mixer track `mixer` (an
// instrument track too: its channel stays where it is), and the track and the insert end with one name and colour:
// - an unassigned track first takes the insert's own name (hasOwnName) when it has one, the insert's colour when its
//   own is the default, and "Insert N" when neither has a name;
// - an audio track's previous insert goes back to its default name ("Insert N") and colour (its effects and routing
//   stay);
// - then a track without a name takes the insert's name and colour, otherwise the insert takes the track's.
// The audio clips already on the track follow it (followAudioTracks). False when canLinkToMixerTrack says no; the
// insert the track already has changes nothing.
bool linkToMixerTrack (Song& song, int track, int mixer);
// Whether `track` can be linked to `mixer`: an existing insert (never the Master), not linked to another Playlist
// track (one track per insert, as makeAudioTrack keeps it) and not reserved for another participant of an online
// session.
bool canLinkToMixerTrack (const Song& song, int track, int mixer);
// Whether insert `mixer` carries a name of its own: a stored name other than the default "Insert <its number>".
bool hasOwnName (const Song& song, int mixer);
// FL's "New mixer track(s)" is enabled: the mixer has room for another insert (in an online session, one not
// reserved for another participant).
bool canAddMixerTrack (const Song& song);
// Appends an insert past the last one (skipping inserts reserved for another participant) and returns it; -1 (and no
// change) when canAddMixerTrack says no.
int addMixerTrack (Song& song);
constexpr int newMixerTrack = -1;
// FL's Track mode › Audio track pick (FUN_012ec590, one undo step "assign to audio track(s)"): the first of `tracks`
// is linked to `mixer` (newMixerTrack: a new insert, FL's "New mixer track(s)"), each next one to the next insert no
// Playlist track is linked to, new inserts past the last one, up to the mixer's limit. Returns how many were linked.
int assignAudioTracks (Song& song, const std::vector<int>& tracks, int mixer);
// The Audio track submenu as FL builds it (FUN_0122eff0): "New mixer track(s)" (enabled while canAddMixerTrack), a
// separator, then one entry per insert from 1 on: "N. <name>" ("N. Insert N" without a name), ticked when it is the
// track's stored link, greyed when it is not and the insert is linked to another track (LYNX: or reserved for another
// participant of an online session). The text of a greyed entry is not changed.
struct AudioTrackMenuItem
{
    int mixer = 0;
    std::string name; // the insert's stored name (empty: show "Insert N")
    bool enabled = false, ticked = false;
};
struct AudioTrackMenu
{
    bool canAddMixerTrack = false;
    std::vector<AudioTrackMenuItem> inserts;
};
AudioTrackMenu audioTrackMenu (const Song& song, int track);
// "Track mode › Unassigned".
void unassign (Song& song, int track);
// --- FL's new audio / instrument tracks (round 190dx; FLEngine 2026 [DECOMP], /mnt/e/lynx-work/flpl190dx/FL-FACTS.md) ---
// The first Playlist track that has no clip and is unassigned (FUN_012e8380: tracks 1..500 in FL's count), -1 when
// all 500 are taken. New audio and instrument tracks go there.
int firstFreeTrack (const Song& song);
// The Playlist's "+" › "Add audio track" and its right click (AddAudioTrackMenuClick → FUN_012e3640): the first free
// track, linked to a mixer track nothing uses (freeMixerTrack) as Track mode › Audio track links it (FUN_012ec590).
// Returns the track, or -1 with nothing changed (no free track, or no free mixer track).
int addAudioTrack (Song& song);
// Whether `channel` can have an instrument track (FL: generators only; not audio clip or automation channels).
bool canBeInstrumentTrack (const Song& song, int channel);
// The Playlist tracks that are instrument tracks of `channel`.
std::vector<int> instrumentTracksOf (const Song& song, int channel);
// The Channel rack menu's "Assign to new instrument track" (LineAssignInstrTrackMenuClick Tag 0 → FUN_013036e0 with
// track −1): the first free track becomes the channel's instrument track (makeInstrumentTrack). Returns the track, or
// −1 with nothing changed.
int assignToNewInstrumentTrack (Song& song, int channel);
// The same item while the channel has an instrument track: "Unassign from instrument track" (Tag 1 → FUN_01303e30):
// each of its instrument tracks goes back to unassigned. Returns how many.
int unassignFromInstrumentTracks (Song& song, int channel);
// A new instrument track playing a new channel of `generator` (the "+" menu's / Track mode › Instrument track's "More
// plugins..." and generator list: FUN_012e34d0 / FUN_00e71980): on `track` (an unassigned or audio track), or on the
// first free track when `track` < 0. Returns the new channel, or −1 with nothing changed.
int addInstrumentTrack (Song& song, ChannelDef generator, int track = -1);
// The pattern the pencil places on an instrument track (REA round 151 [EXEC FL 2026: drawing on an instrument track
// places a pattern named after its channel, made on the first click, empty and one bar long; every later click
// places the same pattern; its piano roll is that channel's]): the pattern named after the track's channel whose
// notes (if any) are all that channel's, else a new one (the channel's colour). -1 when the track is not an
// instrument track.
int instrumentTrackPattern (Song& song, int track);
// The text FL's hint bar adds for a track header: "(Audio track, linked to mixer track N)" / "(Instrument track)";
// empty for an unassigned track.
std::string describe (const Song& song, int track);
// The mixer track an audio track's record button arms (FL 2026, issue #11: the header of an audio track has "Audio input
// source", "Recording / Monitoring" and "Arm/disarm recording" buttons that act on its linked mixer track; unassigned and
// instrument tracks have none). -1 when `track` is not an audio track linked to an existing insert.
int armableInsert (const Song& song, int track);
// FL 2026's Playlist menu › View › Track headers (round 190w; [DFM] EEShowAudioTrackControlsAction Checked=true,
// EEShowAudioTrackMetersAction Checked=true, EEShowInstrumentTrackMetersAction unchecked; [SHOT] FL 2026 flgap190w):
// "Show controls on audio tracks", "Show levels on audio tracks", "Show levels on instrument tracks". User options, not
// saved in the project (FL keeps them with its settings).
struct HeaderOptions
{
    bool audioControls = true;
    bool audioLevels = true;
    bool instrumentLevels = false;
};
// The mixer track whose level a track header shows (FL draws a stereo peak meter beside the header): an audio track's
// linked mixer track while audioLevels is on; an instrument track's channel's mixer track (0 = the Master) while
// instrumentLevels is on. -1 = no meter on this track.
int meteredInsert (const Song& song, int track, const HeaderOptions& options);
// Audio clips placed on an audio track play through its mixer track (issue #15, FL 2026 [DECOMP FLEngine FUN_00e66ba0 /
// FUN_00e66580]): only the clips an edit placed (`touched`: moved, painted, pasted, duplicated, dropped — indices into
// song.playlist) are routed, never the others, so a channel routed by hand stays where the user put it until its clip
// is placed again. A touched audio clip on an audio track linked to mixer track M routes its channel to M — unless
// another clip of that channel sits on an audio track linked to a different mixer track: then FL asks "Make clip
// unique?" (`conflicts`), and only with `makeUnique` the touched clips of that channel get a copy of it routed to M
// (No: they keep their channel and its route). Clips of the channel on unassigned / instrument tracks don't count.
// Returns how many clips were routed.
int followAudioTracks (Song& song, const std::vector<int>& touched, bool makeUnique = false);
// The channels of `touched` clips that need FL's "Make clip unique?" question before followAudioTracks routes them
// (see above), each once, in clip order.
std::vector<int> audioTrackConflicts (const Song& song, const std::vector<int>& touched);

// Playlist › Edit › "Drop audio on track headers" (round 189e; FLEngine 2026 [DECOMP] FUN_00e71980 places one file, FUN_00e71430
// asks, FUN_00e79d60 walks the dropped files; EEDropAudioToPL1..4Action Tags 256..258 / none, saved as DropAudioToPLMode).
// FL acts only on an unassigned track; each further file goes to the next track. `Ask` (FL's default, "Always ask") shows
// "Dropped sample(s)": Audio clip(s) / Audio track(s) / Instrument track(s) / Cancel, once for the whole drop.
enum class HeaderDrop : int { ask = 0, audioClips = 256, audioTracks = 257, instrumentTracks = 258 };
// FL's mode and its two Edit-menu companions, app settings (FL: registry, not the project): "Dropping on track header
// locks to content" (AutoLockWhenDropping, off) and "Select tracks when samples are dropped" (SelectPLTracksOnSampleDrop, off).
struct HeaderDropOptions
{
    HeaderDrop mode = HeaderDrop::ask;
    bool lockToContent = false;
    bool selectTracks = false;
};
HeaderDropOptions& headerDropOptions() noexcept;
// One dropped sample (`path`, shown as `name`, `lengthTicks` long) on header `track` as `how` (not `ask`), FL's results
// checked live in FL 2026 (round 189e shots f15/f21/f24): audio clips = a clip at the song start (an audio-clip channel
// of the same file is reused), the track named and coloured after it; audio track = a new audio-clip channel (FL's
// "<name> #2" when the name is taken), the track linked to a free mixer track and named after the channel, its clip at the
// song start; instrument track = a new Sampler channel of the file, the track becomes its instrument track (a free mixer
// track) with its pattern at the song start. `lock`: the track is locked to content. Returns false when `track` is not
// an unassigned track or no mixer track is free (FL: "Can't create instrument/audio track|No available mixer track was
// found.") — then nothing changes.
bool dropOnHeader (Song& song, int track, const std::string& path, const std::string& name, std::int64_t lengthTicks, HeaderDrop how,
                   bool lock);

// "Consolidate track(s)" moved to core/QuickRender.h (round 168e: FL's new track above, the old one grouped and muted).
} // namespace lynx::trackmodes

// Audio clips and the channels they play (round 190ax).
namespace lynx::audioclips
{
// A Playlist audio clip only sounds on an audio clip channel: its voice key (audioClipKey) is no note for a Sampler,
// which drops it, so a clip that points at a Sampler channel showed its waveform and played nothing (a file dropped on
// an audio clip channel's Channel rack row turned it into a Sampler; so does a project written by hand). The channels
// such clips point at, in every clip list (arrangements, clip grid), are put right: a Sampler with a file and no notes
// becomes the audio clip channel it is used as; one that also plays notes keeps them and its clips move to an audio
// clip copy of it (same file, levels and mixer track). Clips on channels that hold no sample file are left as they
// are. Returns how many channels were changed or added.
int repairChannels (Song& song);
// The same channel loads another file: an audio clip channel stays one (FL: the channel's type does not change with
// its sample), a Granulizer / Wave Traveller keeps its engine, every other channel becomes a Sampler named after it.
// `autoName`: FL's General › "Auto name channels" (on by default; round 190bx, core/FlSettings.h): off = the
// channel keeps the name it has (a channel without one still takes the file's).
void loadSampleInto (ChannelDef& channel, const std::string& path, const std::string& name, bool autoName = true);
} // namespace lynx::audioclips

// FL's "Create automation clip" placement (REA round 177b: FUN_01191a50 places it, FUN_00e50b50 picks the track).
namespace lynx::autoclipplace
{
// FL's Options › General "Create automation clips at the playback position" (CreateAutoClipsAtCursorBox, checked in
// FL's form [DFM]); an app setting, not saved in projects.
inline std::atomic<bool>& atPlaybackFlag() noexcept
{
    static std::atomic<bool> on { true };
    return on;
}
inline bool atPlayback() noexcept { return atPlaybackFlag().load (std::memory_order_relaxed); }
inline void setAtPlayback (bool on) noexcept { atPlaybackFlag().store (on, std::memory_order_relaxed); }

struct Place
{
    int track = 0;
    std::int64_t start = 0, length = 0;
};
// Where a new automation clip goes. The Playlist time selection (Song::loopStart / loopEnd) gives its place and length.
// Otherwise it starts at `playTick` when `atPlayback` (FL's Options › General "Create automation clips at the
// playback position", on by default), else at the song start; it reaches the end of the song (at least one bar). The
// track: the first one, from the top, with no clips at all (checked live in FL 2026: a free gap on a used track is not
// taken; instrument tracks take no automation clips: FL's "Instrument tracks are not for audio or automation clips.").
Place forNewClip (const Song& song, bool atPlayback, std::int64_t playTick);
} // namespace lynx::autoclipplace

// Round 177b: optional automation lanes under Playlist tracks (Song.h AutomationLane). Plain edits for editSong.
namespace lynx::autolanes
{
// Playlist › "Automation lanes" (round 177b): whether lanes are shown under their tracks. Off by default (FL has no
// lanes); an app setting, not saved in projects. The Ableton-style layout (round 175a) turns it on by default.
inline std::atomic<bool>& shownFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool shown() noexcept { return shownFlag().load (std::memory_order_relaxed); }
inline void setShown (bool on) noexcept { shownFlag().store (on, std::memory_order_relaxed); }

// The track a new lane for `target` goes on: the track that already has a lane for it; else the instrument track of
// its channel; else an audio / instrument track linked to its mixer track (a channel's own insert counts); else the
// first track with no clips and no lanes.
int trackFor (const Song& song, const AutomationTarget& target);
// The track whose lanes hold automation `a`, or -1 (an automation clip's automation, a live link...).
int ownerTrack (const Song& song, int automation);
// A new lane on `track` for `target`, a flat line at `value` over the song; returns the automation index (-1: bad track).
int addLane (Song& song, int track, const AutomationTarget& target, const std::string& name, float value);
// Removes lane `lane` of `track` and its automation.
bool removeLane (Song& song, int track, int lane);
// "Move clip into lane": the automation clip's curve (over its span, offset applied) replaces what the lane of the same
// target on `track` has there (a lane is made when there is none); the clip is removed. Returns the lane's automation.
int clipToLane (Song& song, int clip, int track);
// "Lane to automation clip": the lane's automation becomes a clip on the first free track over [0, its end]; the lane
// entry goes. Returns the new clip's index, or -1.
int laneToClip (Song& song, int track, int lane);
// Every lane to a clip (the .flp export: FL has no lanes). Returns how many.
int lanesToClips (Song& song);
} // namespace lynx::autolanes
