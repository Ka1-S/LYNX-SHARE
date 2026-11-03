#pragma once

// FL Studio's renders that come back into the project as audio clips, and the Playlist exports per track
// (REA round 168e, docs/analise/REA_rodada168e.md): Patterns › "Quick render as audio clip" / "Render as audio clip..." /
// "Render and replace...", Tools › "Consolidate playlist selection", the Playlist track menu's "Consolidate this track",
// File › Export › "All playlist tracks" / "All playlist track groups". The plain logic only: which clips are rendered,
// over which range, and what the project looks like afterwards. The renders themselves are engine/Exporter's.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::quickrender
{
// --- patterns ---------------------------------------------------------------------------------------------------------

// FL's "Pattern is empty, nothing to render." (FUN_012d8d70): a pattern renders when it has notes or event lanes.
bool patternRenderable (const Song& song, int pattern) noexcept;

// The pattern's name, or FL's "Pattern N" (FUN_011880b0: the render file is "<name>.wav" in the render folder).
std::string patternName (const Song& song, int pattern);

// A channel name nothing else uses: `name`, else FL's "<name> #2", "<name> #3"... (FUN_00fe87a0).
std::string uniqueChannelName (const Song& song, const std::string& name);

// The copy of the song rendered for a pattern: that pattern is the current one, played in pattern mode (FL renders
// patterns in pattern mode: the render window's mode box is set to Pattern and locked, flag 4 of FUN_011dcf60).
Song makePatternRenderSong (const Song& song, int pattern);

// After the render (FUN_01188230): a new audio clip channel (FL's AudioTrack.fst template) with `samplePath`, named after
// the pattern (made unique) and coloured like it, playing on the Master. With `replace` ("Render and replace",
// FUN_01187fd0) every Playlist clip of the pattern, in every arrangement, becomes a clip of that channel at the same
// place and length (a trimmed clip keeps its offset). Returns the new channel.
int applyPatternRender (Song& song, int pattern, const std::string& samplePath, bool replace);

// --- consolidation (FUN_012fc180) -------------------------------------------------------------------------------------

enum class Result
{
    ok,
    empty,           // "Track is empty|This track doesn't produce audio (or is completely muted)..."
    nothingSelected, // "Nothing selected|There is no unmuted playlist selection to consolidate."
    tempoEvents,     // "Can't consolidate|One of the selected pattern clips contain tempo events..."
    noFreeTrack      // "Error|Can't create new playlist track !"
};

// Where the render starts: the song start, the first clip ("From track start" / "From selection start"), or the
// Playlist's time selection.
enum class Start
{
    songStart,
    clipStart,
    timeSelection
};

struct Plan
{
    std::vector<int> clips;   // indices into song.playlist: what is rendered (unmuted, on unmuted tracks)
    int topTrack = 0;         // the highest track of those clips (where the new track goes)
    std::int64_t start = 0;   // the render range (ticks)
    std::int64_t end = 0;
    bool timeSelection = false; // the range is the time selection: the clips are cut at its edges
    std::string name;         // the file / channel name ("<source> (consolidated)", "<track> (consolidated)")
    std::uint32_t colour = 0; // the new channel's colour (the first clip's source)
};

// "Consolidate playlist selection" (Tools, Ctrl+Alt+C "From selection start" / Ctrl+Shift+Alt+C "From song start"):
// the selected clips that play. With a time selection the range is the time selection (FL keeps it), else the
// clips' extent; "From song start" starts at 0. Automation clips are left playing (FL does not mute them) and are
// not part of the selection's extent.
Result planSelection (const Song& song, const std::vector<int>& selected, bool fromSongStart, Plan& plan);

// One track ("Consolidate this track", the exports per track): the unmuted clips of `track`, with `withGroup` also
// those of the tracks grouped under it (FL's "track groups", flag 0x20). `start`: songStart / clipStart / timeSelection
// (the latter needs a time selection: none = empty).
Result planTrack (const Song& song, int track, bool withGroup, Start start, Plan& plan);

// The copy of the song that renders a plan: only its clips and the automation clips of the other tracks play, over
// the plan's range (song.loopStart / loopEnd = the range; render it with exportopts::Range::selection).
Song makePlanRenderSong (const Song& song, const Plan& plan);

// After the render, for a consolidation (FL's flags without 4): the plan's clips are muted (with a time selection
// they are cut at its edges first and only the parts inside are muted), a new track is inserted at the plan's top
// track (the old one and those below move down one; the new track is in the same group as the old one was) and the
// old track becomes a member of the new track's group (FL sets its "grouped with above" flag); the new track holds
// one clip of a new audio clip channel (`samplePath`, the plan's name made unique and colour, on the Master) of
// `lengthTicks` at the plan's start. Returns the new channel.
int applyConsolidation (Song& song, const Plan& plan, const std::string& samplePath, std::int64_t lengthTicks);

// --- exports per playlist track (FUN_012fd7a0) ------------------------------------------------------------------------

// The tracks File › Export › "All playlist tracks" goes over (1..the last track with a clip, FL's 1-based tracks are
// LYNX's 0-based ones); with `groups` the members of a group are left out (their header renders them).
std::vector<int> exportTracks (const Song& song, bool groups);

// The file name of a track's export: "<project> - <track name>" (FL: the project's file name without extension, " - ",
// the track's name or "Track N"); characters illegal in file names replaced.
std::string trackExportName (const Song& song, const std::string& projectName, int track);

// --- File › Export › "Project bones..." (FUN_011edb50 / FUN_011ecca0) ------------------------------------------------

// FL writes into a sub-folder of the chosen folder: Channels\<filter group>\<channel>.fst, Effects\<plugin>.fst,
// Mixer\<insert>.fst, Scores\<pattern> - <channel>.fsc, Automation\<pattern> - <parameter>.fsc. LYNX writes what it has
// a file format for: Mixer\<insert>.lynxtrack (the insert's state), Effects\<effect>.lynxpreset (every effect of every
// insert) and Scores\<pattern> - <channel>.mid (each channel's notes in each pattern).
struct Bone
{
    enum class Kind
    {
        mixer,  // `track`: a mixer track with effects or a name
        effect, // `track`, `slot`: an effect of it
        score   // `pattern`, `channel`: the channel's notes in the pattern
    };
    Kind kind = Kind::mixer;
    std::string folder;  // "Mixer", "Effects", "Scores"
    std::string name;    // the file name without extension (unique within its folder; illegal characters replaced)
    int track = -1, slot = -1, pattern = -1, channel = -1;
};
std::vector<Bone> projectBones (const Song& song);

// A name made safe as a file name (illegal characters replaced, trimmed; `fallback` when nothing is left).
std::string safeFileName (const std::string& name, const std::string& fallback);

// The length in ticks of `seconds` of audio placed at `startTick` (the song's tempo map).
std::int64_t ticksForSeconds (const Song& song, std::int64_t startTick, double seconds);
} // namespace lynx::quickrender
