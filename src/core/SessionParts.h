#pragma once

// Online Sessions, phase 1 (docs/design/ONLINE_SESSIONS.md §2 and §9 P1, round 168a): a guest's area of a project, cut
// out of his copy as a part and merged back into the host's project.
//
// Whose a thing is (§2.2): channels, patterns, automations and clips by the tag in their uid (uids::tagOf); the notes
// of channel C in any pattern belong to C's owner (the "notes slice"); inserts and Playlist rows by the participant's
// reserved ranges. A part holds the participant's own entities plus "stubs" — other people's patterns / channels his
// things refer to (a clip of the host's pattern on his row) — which the merge only uses to find their counterpart by
// uid and never writes.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::parts
{
// Adds (or finds, by name) the guest a project is sent to: a new tag, his reserved inserts and rows after whatever the
// project uses and the other guests' ranges, his colour and whether he may keep a full copy. The host's own record
// (tag 1, `hostName`) is created first if missing. nullptr when the session is full (session::maxParticipants).
// `maxRecords`: how many participants the song may know in all. Live sessions (round 190ah) allow more records than
// people online at once (session::maxParticipants), so someone new can join after someone else left for good.
SessionParticipant* invite (Song& host, const std::string& hostName, const std::string& guestName, std::uint32_t colour, bool keepFull,
                            int maxRecords = session::maxParticipants);

// The copy of the host's project the guest gets: the same song, edited as `guestTag` from now on.
Song guestCopy (const Song& host, std::uint32_t guestTag);

// The part of participant `tag` in `song` (normally the guest's own copy, tag = song.session.me).
Song extract (const Song& song, std::uint32_t tag);

// What merge() did.
struct MergeReport
{
    int channels = 0, patterns = 0, notes = 0, clips = 0, automations = 0, inserts = 0, rows = 0;
    int removed = 0;                         // entities of that guest the part no longer has (deleted by him since)
    std::vector<std::pair<int, int>> movedInserts; // his insert -> where it went (the host used it meanwhile)
    int rowShift = 0;                        // his rows moved down by that much (the host used them meanwhile)
    std::string guest;
    bool ok = false;
    std::string error;
};

// Brings participant `tag`'s part into the host's project as his area: his channels, patterns, notes slices,
// automations and clips replace what an earlier merge of his left (by uid; nothing of another participant is changed),
// his reserved inserts and rows are written (moved elsewhere if the host used them meanwhile), his colour goes on what
// has none. One call = one undo step for the caller (AudioEngine::editSong).
MergeReport merge (Song& host, const Song& part, std::uint32_t tag);

// After "Send my part back" when the host did not allow a full copy (the user's decision 1): the guest's copy reduced to
// his own part, still a project that opens and plays (other people's channels, patterns without his notes, clips,
// automations, inserts and rows are gone).
Song keepOnlyPart (const Song& guestCopy);

// The participant a channel belongs to (its uid tag; 0 = no uid yet).
std::uint32_t ownerOfChannel (const ChannelDef& c) noexcept;
} // namespace lynx::parts
