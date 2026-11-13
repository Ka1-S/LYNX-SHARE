#pragma once

// Mixer workflow operations (docs/WORKFLOW_FL.md P2: insert multi-selection and reordering, routing and
// sidechain gestures, linking channels, effect slot reordering). Pure functions over the Song model:
// no JUCE, unit-tested in tests/WorkflowP2Tests.inc.

#include <set>
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

// FL's "Delete" on the selected mixer tracks (round 186c; REA FUN_012a5b60 / FUN_012a5280, checked in FL 2026):
// every insert of `tracks` (the Master is skipped) goes. A track that sent into a deleted insert now sends to that
// insert's own destinations (FL 21's fix 19603), so the signal still reaches where it went. Playlist audio /
// instrument tracks linked to a deleted insert follow `linked`: `unlink` keeps the Playlist track (and its name) as
// an unassigned track, `deleteToo` resets it (FL's "Delete tracks and channels": default name, unassigned) and
// deletes the channel of an instrument track. Returns how many inserts were deleted.
enum class LinkedPlaylistTracks
{
    unlink,
    deleteToo
};
// `keepLostLinks` (LYNX's option, pain point 1; off = FL): every send into a deleted insert also stays as a
// MixerTrack::lostSends entry, drawn greyed, until reconnectLostSend or removeLostSends.
int deleteTracks (Song& song, std::vector<int> tracks, LinkedPlaylistTracks linked = LinkedPlaylistTracks::unlink, bool keepLostLinks = false);
// Turns lost send `index` of `track` into a send to insert `to` at its level. False when that route isn't possible
// (out of range, itself, or a cycle).
bool reconnectLostSend (Song& song, int track, int index, int to);
// Removes lost send `index` of `track` (all of them for −1).
void removeLostSends (Song& song, int track, int index = -1);

// The Playlist audio / instrument tracks linked to one of `tracks` (FL asks "Delete tracks and channels" or
// "Unlink tracks and channels" when there is one; FUN_012a5b00): an audio track by its mixer track, an instrument
// track by its channel's mixer track.
std::vector<int> playlistTracksLinkedTo (const Song& song, const std::vector<int>& tracks);

// Routes `channels` to insert `track` (all of them), or, with `startingHere`, to consecutive inserts from
// `track` on (adding inserts as needed, up to maxMixerTracks). Returns the insert of each channel.
std::vector<int> linkChannels (Song& song, const std::vector<int>& channels, int track, bool startingHere);

// Removes the unused inserts (Song::insertInUse) at the end of the mixer, keeping at least `keep` inserts (round 163,
// Mixer › "Remove unused inserts at the end"). Returns how many were removed.
int removeUnusedTrailingInserts (Song& song, int keep = 0);

// Mixer menu › "Show only used inserts" (round 163; the rule made exact in round 190am): which strips take no room.
// An insert is hidden only when ALL of these hold:
//  - it is not the Master;
//  - it is not in `keep` (the selected inserts, those brought back with "+", and those whose meter moved: an insert
//    that receives audio always shows);
//  - nothing uses it (Song::insertInUse): no channel plays into it, no insert sends to it and it sends nowhere but
//    the Master, no effect slot, no audio input, no Playlist track link, no effect's sidechain or extra output points
//    at it, its name, fader, pan, separation, mute, solo, colour, EQ and output are the defaults, nothing automates it.
// Returns one flag per insert plus one for the "+" button (never hidden). Nothing in the song changes.
std::vector<bool> hiddenUnusedInserts (const Song& song, const std::set<int>& keep);

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

// FL's inputs list of an insert (REA_rodada22 §1, round 188d [EXEC]): [the insert itself, then every insert routed
// to it — at any level, level 0 = sidechain only — in ascending order]. A plugin's "sidechain index" k in the
// wrapper's Processing › Connections picks entry k (0 = the insert's own audio).
std::vector<int> insertInputs (const Song& song, int track);
// The source insert of sidechain index k on `track`, or −1 (k ≤ 0, or past the list)
int sidechainIndexSource (const Song& song, int track, int k);
// The index of `source` in `track`'s inputs list, or 0 when it is not there
int sidechainIndexOf (const Song& song, int track, int source);

// The effects a sidechain link feeds (round 170h): LYNX's Compressor, FL's Fruity Limiter / Fruity Compressor
// (FL users sidechain with these) and the vocoder.
bool listensToSidechain (EffectKind kind) noexcept;
// Round 177d (the user's decision on an FL wishlist item): LYNX's own dynamics whose live meter 0 is their gain
// reduction, 0..1 over 0..30 dB (Effect::controlOutput; published for every slot in ProcessTimes::effectMeters): the
// Compressor, Apex Limiter, Crest Compressor, Sentry Gate, Quell Denoiser, Ridge Strip, Glow Leveler and Hush De-esser.
// The mixer strip shows a gain-reduction meter only when it holds one of them.
bool reportsGainReduction (EffectKind kind) noexcept;
constexpr float gainReductionRangeDb = 30.0f;
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
// Removes every effect of `kind` from every mixer track, each slot left empty (FL's Tools › Macros › "Remove all Edison
// instances", round 190cd). Returns how many were removed.
int removeEffectsOfKind (Song& song, EffectKind kind);

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
// "Gradient color..." (FL's SetGradientColorMenuClick, REA_rodada177a §5): track k of the n selected tracks (in mixer
// order) takes from + (to - from) * floor (k * 256 / (n - 1)) / 256 on each of R, G and B, so the first gets `from` and the
// last gets `to`. Fewer than two tracks do nothing. Returns how many tracks changed.
std::uint32_t gradientColourStep (std::uint32_t from, std::uint32_t to, int k, int n) noexcept;
int gradientColour (Song& song, std::vector<int> tracks, std::uint32_t from, std::uint32_t to);
// "Assign to new Audio track(s)" (FL's AssignAudiotrackMenuClick, Tag 0; REA_rodada177a §6): each selected insert
// takes the first untouched Playlist track (no name, colour, mode or clip; FL scans tracks 1–500) as its audio track,
// named and coloured as the insert. FL skips an insert that a Playlist track already points at (any mode), or whose
// only channel already has an instrument track. Returns how many tracks were assigned.
int assignNewAudioTracks (Song& song, std::vector<int> inserts);
// "Unassign from Audio track(s)" (the same handler, Tag 1): the first audio track linked to each selected insert goes
// back to unassigned. Its name, colour and clips stay. Returns how many tracks changed.
int unassignAudioTracks (Song& song, std::vector<int> inserts);
// "Group › Separator" and "Layout › Dock to" for the selected tracks (inserts; the Master can be docked).
void setSeparator (Song& song, const std::vector<int>& tracks, bool on);
void dockTracks (Song& song, const std::vector<int>& tracks, MixerDock dock);

// --- Folded mixer groups (round 177a, LYNX's addition to FL's groups; REA_rodada177a §2) ---------------------------------
// A group can fold when it is set off by a separator (on its first strip or after its last) and holds 2+ inserts.
bool canFold (const Song& song, int track);
// The insert a group sums into: a member every other member sends only to (no Master route, no other send), the way
// "Create submix" makes it. -1 = not a summing group.
int groupBus (const Song& song, const std::vector<int>& group);
// How a strip is shown: normally, hidden (a member of a folded group) or as the slim tab of a folded group that has no
// bus (its first strip). A folded summing group shows only its bus strip.
enum class FoldShow : std::uint8_t { strip, hidden, tab };
std::vector<FoldShow> foldView (const Song& song); // one per mixer track (all `strip` when no group is folded)
// Folds / unfolds `track`'s group (the flag sits on its first strip). Returns the new state.
bool toggleFold (Song& song, int track);
bool isFolded (const Song& song, int track);
// Folds (or unfolds) every group that can fold. Returns how many changed.
int foldAll (Song& song, bool fold);

// --- Mixer groups that follow the Playlist (round 186c, pain point 4; LYNX's addition, FL has no link) -------------------
// The inserts of each Playlist track group (a header and its grouped tracks), in Playlist order: an audio track's mixer
// track, an instrument track's channel's mixer track (each insert once, in the first group that names it).
struct PlaylistGroupInserts
{
    std::vector<int> inserts;
    bool collapsed = false; // the header is collapsed
};
std::vector<PlaylistGroupInserts> playlistGroupInserts (const Song& song);
// The inserts linked to any Playlist track, in Playlist order (each once).
std::vector<int> playlistOrderInserts (const Song& song);
// "Mixer groups follow the Playlist": the separators and folds of the inserts linked to Playlist tracks mirror the
// Playlist's groups (a group of 2+ inserts that sit next to each other in one dock becomes a mixer group, folded when its
// header is collapsed); a closing separator may land on the insert after a group. Separators of the other inserts stay.
// Returns true when something changed.
bool followPlaylistGroups (Song& song);
bool followsPlaylistGroups (const Song& song); // nothing for followPlaylistGroups to change
// "Arrange inserts like the Playlist": the inserts linked to Playlist tracks move to the front (insert 1 on) in Playlist
// order, the others keep their order after them (everything that refers to an insert follows it), then
// followPlaylistGroups. Returns how many inserts changed place.
int arrangeLikePlaylist (Song& song);

// --- VCA faders (round 177a slice 3, LYNX's addition; REA_rodada177a §3) ---------------------------------------------
// The VCAs that act on `track`: those it is a member of and their parents, each once (indices into Song::vcas).
std::vector<int> vcasOf (const Song& song, int track);
// The VCAs' linear gain on `track` (1 when it is in none), from their fader positions.
float vcaGain (const Song& song, int track);
// True when a VCA acting on `track` is muted.
bool vcaMuted (const Song& song, int track);
// "New VCA from selected": a VCA (at unity) whose members are the selected inserts. Returns its index (-1: none).
int createVca (Song& song, const std::vector<int>& tracks, const std::string& name);
// Adds / removes inserts to / from VCA `vca`. Returns how many changed.
int assignToVca (Song& song, const std::vector<int>& tracks, int vca);
int removeFromVca (Song& song, const std::vector<int>& tracks, int vca);
// Deletes VCA `vca`: its members keep their faders; its children lose their parent; its automation loses its target.
bool deleteVca (Song& song, int vca);
// "Merge VCA into members" (undoable through the song edit): each member's fader (and its volume automation, when it
// has one) takes the VCA's gain, then the VCA goes back to unity. The sound does not change (within the faders' range).
bool mergeVcaIntoMembers (Song& song, int vca);
// Solo on a VCA: its members (and the inserts its child VCAs reach) are soloed with FL's solo rules (soloSelected).
std::vector<int> vcaReach (const Song& song, int vca);

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
