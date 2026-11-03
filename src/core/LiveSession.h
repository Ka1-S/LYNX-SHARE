#pragma once

// Online Sessions, phase 2 (docs/design/ONLINE_SESSIONS.md §1.2–§1.5 and §2, round 169b): live sessions.
//
// Host-authoritative state replication of the song's entities. No network here: the engine (engine/LiveSession.h)
// moves the messages below between computers; the tests move them inside one process.
//
// * An entity is the unit that is compared, owned and conflict-checked (§1.2.4): the globals, a channel, a pattern
//   header, the notes of one channel in one pattern (the "notes slice"), an automation, an arrangement header, a clip,
//   a mixer insert (by index), a Playlist row (by index), the participants, the links and the order of the lists.
// * A Patch carries the entities that changed, as a small Song (the "carrier": the changed entities plus id-only
//   stubs of whatever they refer to), so the engine sends it with the .lynx JSON code. Index references are matched
//   by uid on the receiving side: two computers never need the same indices.
// * Every peer (the host too) runs a Replica: confirmed state C (host revision r) + its own unconfirmed edits = the
//   working song W (the engine's). The host also runs the Authority, which orders proposals, checks permissions
//   (§2.2) and conflicts (§1.3) and commits them. Undo per user (§1.4) is selectiveRevert.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::live
{
constexpr int protocolVersion = 1;

enum class Kind : std::uint8_t
{
    globals = 1, // tempo, time signature, swing, master pitch, project switches, info, channel groups
    session,     // the participants (names, colours, reserved ranges)
    links,       // MIDI links and initial values
    order,       // the order of the channels, patterns, automations and arrangements
    arrangement, // an arrangement's name, loop and markers (a = uid)
    channel,     // a = uid
    pattern,     // a pattern's header: everything but its notes (a = uid)
    notes,       // the notes of channel b in pattern a
    automation,  // a = uid
    clip,        // a = uid (the arrangement it is in is part of it)
    insert,      // a = mixer index (0 = Master)
    row          // a = Playlist row
};

struct Key
{
    Kind kind = Kind::globals;
    std::uint64_t a = 0, b = 0;
    auto operator<=> (const Key&) const = default;
};
std::string toString (const Key& k);              // "c:0000010000000003" … (the wire form)
std::optional<Key> keyFromString (const std::string& s);
std::string describe (const Key& k, const Song& song); // for the hint bar: "Pattern 3", "Insert 12", "Kick"

// What changed between two states: `changed` keys take their value from `carrier`, `removed` keys are deleted.
struct Patch
{
    std::vector<Key> changed, removed;
    Song carrier;
    bool empty() const noexcept { return changed.empty() && removed.empty(); }
    std::vector<Key> keys() const;
};

// The patch that turns `from` into `to` (only = restrict to these keys).
Patch diff (const Song& from, const Song& to, const std::set<Key>* only = nullptr);
// Applies the patch (the keys `filter` accepts) to `target`, keeping its local view (current pattern / arrangement,
// mode, loop, row heights, who "me" is, the uid counter). Deleted channels / patterns / automations go through
// Song::removeChannel & co., so their ripples are the same as a local edit's.
void apply (Song& target, const Patch& patch, const std::function<bool (const Key&)>& filter = {});
// Whether the entity is the same in both songs (absent in both = same).
bool same (const Song& a, const Song& b, const Key& key);
// Every key present in the song.
std::set<Key> keysOf (const Song& song);

// The song's local view, copied from `from` onto `to` (what apply keeps): what each person has on screen.
void copyLocalView (const Song& from, Song& to);

// --- ownership (§2.2) -------------------------------------------------------------------------------------------------

struct Permissions
{
    bool editEverything = false; // the host's "Edit everything" for this participant
    bool listener = false;       // no edits at all
};
// Whether participant `tag` may change (or create / delete) `key`, judged on the state `song`, with `proposed` the
// song the change comes from (a new entity is judged by its uid there). The host may change everything.
bool mayEdit (std::uint32_t tag, const Permissions& perms, const Key& key, const Song& song, const Song& proposed);
// The participant who owns the entity (for the area colours and the hint "Owned by …"; 0 = nobody / unknown).
std::uint32_t ownerOf (const Key& key, const Song& song);

// --- messages ---------------------------------------------------------------------------------------------------------

struct Proposal
{
    std::uint32_t seq = 0;      // the proposer's own counter
    std::uint64_t baseRev = 0;  // the revision the proposer had confirmed when he made it
    Patch patch;
};

struct Commit
{
    std::uint64_t rev = 0;       // the host revision this commit makes
    std::uint32_t origin = 0;    // whose proposal it is (participant tag)
    std::uint32_t seq = 0;       // the proposal's seq (0: none)
    Patch patch;                 // what changed (may be empty: everything was rejected)
    std::vector<Key> rejected;   // the proposal's keys that were not accepted (only meaningful for the origin)
    std::string reason;          // why, for the hint bar ("owned" / "conflict")
};

// The serialiser's round trip (the engine: the .lynx JSON; tests: identity or a rounding fake). A patch is applied
// everywhere only in the form every peer receives, so all copies stay bit-identical.
using Codec = std::function<Patch (const Patch&)>;

// --- the host's authority -------------------------------------------------------------------------------------------

class Authority
{
public:
    Authority (Song initial, Codec codec);

    const Song& confirmed() const noexcept { return state; }
    std::uint64_t revision() const noexcept { return rev; }

    void setPermissions (std::uint32_t tag, Permissions p) { perms[tag] = p; }
    Permissions permissionsOf (std::uint32_t tag) const;

    // Orders one proposal: checks permissions and conflicts per entity, applies what passes, runs the normalisations
    // every edit runs (pattern lengths move their clips) and returns the commit to broadcast (rev + 1 when anything
    // changed). Ripples (an entity changed as a consequence of an allowed edit, the same way here) are accepted.
    Commit propose (std::uint32_t origin, const Proposal& proposal);

    // The host changes the participants (join, leave, colours): a commit like any other.
    Commit setParticipants (const std::vector<SessionParticipant>& participants);

private:
    Song state;
    std::uint64_t rev = 0;
    Codec codec;
    std::map<std::uint32_t, Permissions> perms;
    struct Change
    {
        std::uint64_t rev = 0;
        std::uint32_t origin = 0;
    };
    std::map<Key, Change> lastChange;
    Commit commitPatch (std::uint32_t origin, std::uint32_t seq, Patch patch);
};

// --- every peer's replica -------------------------------------------------------------------------------------------

class Replica
{
public:
    // `snapshot` at host revision `rev`; `me` = this participant's tag.
    Replica (Song snapshot, std::uint64_t rev, std::uint32_t me, Codec codec);

    std::uint32_t me() const noexcept { return tag; }
    std::uint64_t revision() const noexcept { return rev; }
    const Song& confirmed() const noexcept { return state; }
    int pending() const noexcept { return (int) inflight.size(); }
    // commits received but not yet put into the working song (while this peer records, §1.3)
    int waiting() const noexcept { return (int) (rev - appliedRev); }

    // The local edits not sent yet, as a proposal (none when nothing changed since the last one).
    std::optional<Proposal> poll (const Song& working);

    struct Update
    {
        std::optional<Song> working;   // the new working song, when it changed
        std::vector<Key> rejected;     // own edits the host did not accept (they were undone here)
        std::string reason;
        bool gap = false;              // a revision is missing: ask for a fresh snapshot
    };
    // A commit from the host. With `hold` (recording / playing a take), the working song is left as it is until
    // release() (the confirmed state still moves on).
    Update onCommit (const Commit& commit, const Song& working, bool hold = false);
    // Puts the commits held back so far into the working song.
    Update release (const Song& working);

private:
    Song state;          // C
    Song applied;        // the C the working song was last rebased on
    Song lastSent;       // the working song as of the last proposal (rebased like W)
    std::uint64_t rev = 0, appliedRev = 0;
    std::uint32_t tag = 0, seq = 0;
    Codec codec;
    std::map<std::uint32_t, Patch> inflight; // sent, not committed yet (unrounded values, to recognise them)
    std::vector<std::pair<std::uint32_t, Commit>> acks; // own commits applied to C but not yet to W (hold)
    Update rebase (const Song& working);
};

// --- undo per user (§1.4) -------------------------------------------------------------------------------------------

// Undo one local step: each entity the step changed (between `before` and `after`) goes back to its `before` state
// when it still is as the step left it; entities someone else changed since are kept and listed in `skipped`. Redo
// is the same call with before and after swapped.
Song selectiveRevert (const Song& current, const Song& before, const Song& after, std::vector<Key>* skipped = nullptr);
} // namespace lynx::live
