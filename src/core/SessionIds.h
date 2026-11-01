#pragma once

// Online Sessions, phase 1 (docs/design/ONLINE_SESSIONS.md §1.2, round 168a): stable ids for the song's entities and
// one place that knows every index reference between them.
//
// Channels, patterns, automations, arrangements and clips get a 64-bit uid = participant tag << 40 | counter, so two
// people can create things at the same time without collisions, and whose a thing is can be read from its uid.
// normalize() runs after every edit (AudioEngine::publishSong) and after loading or importing: it gives a fresh uid to
// whatever has none or repeats an earlier one (a duplicated channel is a copy with the same uid: the later one is
// renamed), so none of the editing code has to know about uids.

#include <cstdint>
#include <functional>
#include <string>

#include "core/Song.h"

namespace lynx::uids
{
constexpr int tagShift = 40;
constexpr std::uint64_t counterMask = (std::uint64_t (1) << tagShift) - 1;

constexpr std::uint32_t tagOf (std::uint64_t uid) noexcept { return (std::uint32_t) (uid >> tagShift); }
constexpr std::uint64_t make (std::uint32_t tag, std::uint64_t counter) noexcept
{
    return (std::uint64_t) tag << tagShift | (counter & counterMask);
}

// A new uid for the participant editing this copy (song.session.me).
std::uint64_t next (Song& song);

// Fresh uids where they are 0 or repeated (per kind, all arrangements' clips together), a projectId when there is
// none, and nextUid past every counter of this participant's uids. Returns whether anything changed.
bool normalize (Song& song);

// A random 128-bit id as 32 hex digits.
std::string newProjectId();

// uid as 16 hex digits (the .lynx and package form) and back (0 when invalid).
std::string toHex (std::uint64_t uid);
std::uint64_t fromHex (const std::string& hex) noexcept;
} // namespace lynx::uids

namespace lynx::refs
{
// Every index reference of the song to one kind of entity, so a part can be moved into another song with its
// references renumbered (the "forEachIndexRef" of the design). `fn` may change the index; -1 / 0 "none" values are
// passed too (the caller keeps them). tests/Session168aTests.inc fills every field and checks each one is visited.
using IndexFn = std::function<void (int&)>;

void forEachChannelRef (Song& song, const IndexFn& fn);    // notes, loop points, audio clips, rows, layers, targets, live sources
void forEachPatternRef (Song& song, const IndexFn& fn);    // pattern clips, the current pattern
void forEachAutomationRef (Song& song, const IndexFn& fn); // automation clips
void forEachInsertRef (Song& song, const IndexFn& fn);     // routing, sends, side-chains, rows, targets, live sources, Patcher outputs
void forEachRowRef (Song& song, const IndexFn& fn);        // clips' Playlist rows

// The target's index refers to a channel / an insert (and, for mixerSend, its slot to an insert too).
bool targetsChannel (const AutomationTarget& t) noexcept;
bool targetsInsert (const AutomationTarget& t) noexcept;
} // namespace lynx::refs
