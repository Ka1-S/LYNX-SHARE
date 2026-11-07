#pragma once

// Mixer workflow operations (docs/WORKFLOW_FL.md P2: insert multi-selection and reordering, routing and
// sidechain gestures, linking channels, effect slot reordering). Pure functions over the Song model:
// no JUCE, unit-tested in tests/WorkflowP2Tests.inc.

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::mixerops
{
// Swaps inserts a and b (both >= 1): everything that refers to them by number follows (channel routing,
// sends, sidechain inputs, automation and MIDI targets, controller sources).
void swapTracks (Song& song, int a, int b);

// Moves the selected inserts left (delta < 0) or right by one place as a block; the Master never moves.
// `selection` is updated. Returns false when nothing moved.
bool moveTracks (Song& song, std::vector<int>& selection, int delta);

// Inserts a blank insert at `at` (1..size; FL's "Insert one"): the inserts from `at` on move one place right,
// everything that refers to them follows, and default names ("Insert n") follow their new number.
// False when the mixer is full or `at` is out of range.
bool insertTrack (Song& song, int at);

// Deletes insert `t` (FL's "Delete"): the following inserts move one place left and a blank insert takes the
// last place, so the count stays. Channels on it go to the Master, sends to it and sidechains from it are
// removed. False for the Master or out of range.
bool deleteTrack (Song& song, int t);

// Routes `channels` to insert `track` (all of them), or, with `startingHere`, to consecutive inserts from
// `track` on (adding inserts as needed, up to maxMixerTracks). Returns the insert of each channel.
std::vector<int> linkChannels (Song& song, const std::vector<int>& channels, int track, bool startingHere);

// Removes the unused inserts (Song::insertInUse) at the end of the mixer, keeping at least `keep` inserts (round 163,
// Mixer › "Remove unused inserts at the end"). Returns how many were removed.
int removeUnusedTrailingInserts (Song& song, int keep = 0);

// Channels whose output goes to `track`.
std::vector<int> channelsOn (const Song& song, int track);

// Makes sure insert `track` exists, adding default inserts ("Insert n") up to it — like FL, a channel can be routed to
// any insert, not only to those the mixer shows yet (round 162, issue #9). Returns `track` clamped to 0..maxMixerTracks-1.
int ensureInsert (Song& song, int track);
// Routes channel `channel` to insert `track` (0 = Master), adding the inserts up to it when needed.
void routeChannel (Song& song, int channel, int track);

// Sends each source insert to `to` (0 = Master). `only`: the source stops sending anywhere else. Sources
// that would make a cycle are skipped. Returns how many were routed.
int routeTo (Song& song, const std::vector<int>& sources, int to, bool only);

// Sidechain link: a level-0 send from `from` to `to` (so `from` is processed first) and every compressor
// or vocoder on `to` without a side input listens to `from`. Returns how many effects now listen.
int sidechainTo (Song& song, int from, int to);

// The effects a sidechain link feeds (round 170h): LYNX's Compressor, FL's Fruity Limiter / Fruity Compressor
// (FL users sidechain with these) and the vocoder.
bool listensToSidechain (EffectKind kind) noexcept;
// The one insert whose send into `to` is a sidechain link (a level-0 send, as "Sidechain to this track" makes
// it), or -1 when there is none or more than one.
int soleSidechainSender (const Song& song, int to);
// Round 170h: a dynamics effect just added to `to` (slot `slot`, no side input yet) listens to the
// sole sidechain sender — "Sidechain to this track", then add the compressor, works in that order too.
// Returns whether it was linked.
bool linkAddedSidechain (Song& song, int to, int slot);

// --- One-click sidechain (round 177b) -------------------------------------------------------------------
// The effects that read a side input (Effect::setSidechain): the compressors, limiters, gates, de-esser, vocoders,
// Facet EQ, LuxeVerb, Transmitter, Transporter and Patcher. A hosted plugin listens when it has a second input bus
// (asked of the instance, outside core: pluginhost::sideInputChannels).
bool acceptsSidechain (EffectKind kind) noexcept;
enum class SidechainPick
{
    linked,     // slot `slot` of `to` now listens to `from` (a level-0 send was added when there was no route)
    cleared,    // from < 0: the slot listens to nothing (the routing is left as it is, as FL leaves its sends)
    cycle,      // `from` is fed by `to`: a sidechain would make a loop, nothing changed
    invalid     // no such insert / slot
};
// The wrapper's "Sidechain from" picker: slot `slot` of insert `to` listens to insert `from` (`second`: the second side
// input, a vocoder's carrier). Like FL's "Sidechain to this track", a missing route becomes a level-0 send, so `from`
// is processed first and still goes to the Master and its other sends; an existing route keeps its level. On the
// Master (`to` = 0) no route is needed (every insert plays before it).
SidechainPick sidechainFrom (Song& song, int to, int slot, int from, bool second = false);

// Moves effect slot `from` to position `to` of the same insert (the others shift); automations follow. The rows the
// effects showed in stay in use (the moved effect takes the row of the one it lands on).
bool moveEffectSlot (Song& song, int track, int from, int to);

// --- FL's slot places (round 170b, REA FUN_01233b30 / FUN_012ab750) --------------------------------------------------
// The mixer row of each effect of a chain (EffectSlot::row; -1 = right after the previous one); rows always grow along
// the chain and stay below maxEffectSlots (the last ones are pulled up when needed).
std::vector<int> effectRows (const std::vector<EffectSlot>& effects);
// The index of the effect in `row`, or -1 (an empty slot).
int effectAtRow (const std::vector<EffectSlot>& effects, int row);
// FL's move: effect `index` goes to `row`; an effect already there takes its old row (the two swap), an empty row
// just takes it. The chain order follows the rows; automations follow the effects. False when nothing changed.
bool moveEffectToRow (Song& song, int track, int index, int row);
// FL's Move up / Move down and the wheel over a slot: one row up (-1) or down (+1), wrapping around the 10 slots as FL
// does (slot 1 up = slot 10). Returns the effect's new index, or -1.
int moveEffectBy (Song& song, int track, int index, int delta);
// Adds `fx` into the empty `row` (FL: a plugin picked on an empty slot lands in that slot). Returns its index, or -1
// (row taken, insert full).
int insertEffectAtRow (Song& song, int track, EffectSlot fx, int row);
// Removes effect `index`; the others keep their rows (FL leaves the slot empty).
void removeEffectKeepRows (Song& song, int track, int index);

// --- FL's mixer track menu (REA round 143) ---------------------------------------------------------------
// "Reset selected track(s) to default": level, pan, stereo separation, phase, swap and EQ back to the defaults,
// mute and solo off (name, colour, effects and routing stay).
void resetToDefault (Song& song, const std::vector<int>& tracks);
// FL's "Solo selected track(s)" (S) and "Alt solo selected track(s)" (Alt+S) (FUN_0127c2a0 → FUN_0127bb10,
// REA_rodada167l §4): the targets play with their path to Master; the alt solo also keeps the tracks that feed them.
// Again on the same solo clears it; one kind on the other kind switches to it.
void soloSelected (Song& song, const std::vector<int>& targets, bool alt);
// "Reset selected track(s) routing": each insert sends only to the Master again, at full level.
void resetRouting (Song& song, const std::vector<int>& tracks);
// "Unroute tracks routed to the selected track(s)": the sends into them are removed (an insert left with no
// output goes to the Master). Returns how many sends were removed.
int unrouteInto (Song& song, const std::vector<int>& tracks);
// "Select › Tracks this one is routed to" / "Tracks routed to this one" (0 = Master counts as a destination).
std::vector<int> destinationsOf (const Song& song, int track);
std::vector<int> sourcesOf (const Song& song, int track);
// "Insert multiple...": `count` blank inserts at `at`; returns how many were inserted (the mixer may fill up).
int insertTracks (Song& song, int at, int count);
// How many more inserts the mixer can take (FL 2026: "You can add a maximum of N tracks", up to 500 inserts).
int roomForInserts (const Song& song) noexcept;

// --- FL's mixer groups and docking (REA round 146a) -----------------------------------------------------
// The order the strips are shown in: the tracks docked left, then the middle ones, then those docked right (each block
// by number).
std::vector<int> displayOrder (const Song& song);
// The group of `track`: the run of tracks around it, in display order and in its dock block, from the nearest track with
// a separator (at or before it) up to the next separator. Inserts only (the Master is never part of a group).
std::vector<int> groupOf (const Song& song, int track);
// "Group › Create group": a separator on the first track of each run of `tracks` and on the track after it, so the
// selection becomes a group; the tracks without a colour take the colour of `current` (FL's FUN_012aa200).
void createGroup (Song& song, const std::vector<int>& tracks, int current);
// "Group › Auto color group": the tracks of `track`'s group take its colour. Returns how many changed.
int autoColorGroup (Song& song, int track);
// "Group › Separator" and "Layout › Dock to" for the selected tracks (inserts; the Master can be docked).
void setSeparator (Song& song, const std::vector<int>& tracks, bool on);
void dockTracks (Song& song, const std::vector<int>& tracks, MixerDock dock);

// --- FL's mixer track menu › Routing (REA round 165f-mixer: FUN_012ac520 / FUN_012ac950) ------------------------------
enum class RouteKind
{
    route,         // a send at unity level (Master: the route to the Master)
    routeOnly,     // ... and the source stops sending anywhere else (Master included)
    sidechain,     // a level-0 send whose compressors / vocoders listen to the source (sidechainTo)
    sidechainOnly  // ... and the source stops sending anywhere else
};
// "Selected tracks to this track": every selected insert but `to` is routed into `to`. Returns how many were routed
// (sources that would make a cycle are skipped).
int routeSelectedTo (Song& song, const std::vector<int>& selected, int to, RouteKind kind);
// FL's route arrow under `to` (round 170b, REA FUN_012aa110): a plain click switches the route of EVERY selected insert
// to `to` to `on` (a new route starts at unity); Ctrl / Shift / Ctrl+Shift are routeOnly / sidechain / sidechainOnly
// for all of them (routeSelectedTo). Returns how many routes changed.
int switchSelectedRoute (Song& song, const std::vector<int>& selected, int to, bool on);
// "This track to selected tracks": `from` is routed into every selected track but itself (0 = the Master).
int routeToSelected (Song& song, int from, const std::vector<int>& selected, RouteKind kind);
// "Create submix...": the selected inserts are routed only into `to`; `to` is renamed "<name> master" (when `name` is
// not empty), the routed inserts take its colour and the unnamed ones (no name or "Insert n") the name; when they are
// contiguous in one dock they become a group (separators; `to` joins when it is next to them). Returns how many
// were routed.
int createSubmix (Song& song, const std::vector<int>& selected, int to, const std::string& name);
// True when `track`'s name is empty or the default "Insert <track>".
bool hasDefaultName (const Song& song, int track);

// --- FL's track latency (REA round 165f-mixer) ------------------------------------------------------------------------
// "Plugin delay compensation › Reset manual latency on all tracks". Returns how many tracks had one.
int resetManualLatency (Song& song);

// All effects of the insert on or off (the "effects" switch).
void setAllEffectsEnabled (Song& song, int track, bool enabled);
bool anyEffectEnabled (const Song& song, int track) noexcept;

// --- FL's Disk recording (round 167a, REA_rodada167a §4 / §7) -------------------------------------------------------
// "Auto-create audio clip": what "Render armed tracks to wave file(s)" puts in the project afterwards. Per file, as FL
// 2026 does [SHOT s28–s37]: a new audio clip channel named after the file (target "---", the Master), at the end of
// the Channel Rack, and a clip of the whole file from the song's start on the first Playlist track after every used
// track (the tracks keep their names). Files with no frames are skipped. Returns how many clips were placed.
struct RenderedFile
{
    std::string path;
    double frames = 0.0, sampleRate = 0.0;
};
int placeRenderedFiles (Song& song, const std::vector<RenderedFile>& files);
// The first Playlist track after every track that holds a clip (0 on an empty Playlist).
int firstTrackAfterUsed (const Song& song) noexcept;
// FL's "Record <insert> to" default name (REA_rodada167a §7 [SHOT s20–s27]): "<project>_<yyyy-mm-dd hh-mm-ss>_<insert>"
// (as a take's name, safety::recordingName), the insert's own name or "Insert N".
std::string insertDisplayName (const Song& song, int track);
} // namespace lynx::mixerops
