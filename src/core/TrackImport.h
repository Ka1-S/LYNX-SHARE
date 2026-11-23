#pragma once

// Import tracks from another project (round 190pe, P11 of research190oe; a LYNX extra). Pro Tools' "Import Session
// Data", Cubase's "Import Tracks from Project" and Studio One's "Import Song Data", across formats: the other project is
// read by the importer of its format (.lynx, .flp, .als, .flm, .band, .blx, DAWproject...) into a Song of its own, and
// the chosen Playlist tracks come into the open project with what they play:
//   - their clips (pattern, audio and automation clips) and, in the Clip-launch layout's grid, their clip slots;
//   - the channels those clips play, with their instruments, plugin states and samples;
//   - the patterns (with their event lanes), clip envelopes and grooves;
//   - with `withInserts`: the mixer inserts the channels / tracks play through, with their effects, levels and sends,
//     and the inserts those send to (a bus, a return); without it every imported channel plays to the Master;
//   - with `withAutomation`: automation clips anywhere in the other project that drive what comes in (they come on
//     tracks of their own), the tracks' automation lanes;
//   - the tracks' versions (core/TrackVersions.h) and take lanes (a comp track brings its lanes, a folder its members).
// Everything comes in as new: new channels at the end of the Channel Rack, new patterns, new free inserts, new tracks
// under the last used one. Nothing the open project had changes. FL Studio has no such command (File › Import holds
// MIDI file only; IL forum "Feature Request - Project Merge", June 2026; FL's own way is Export › Project bones). JUCE-free.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::trackimport
{
// One Playlist track of the other project, for the picker.
struct SourceTrack
{
    int track = -1;
    std::string name;          // as the Playlist shows it ("Track 3" for an unnamed one)
    std::uint32_t colour = 0;
    int depth = 0;             // folder depth (core/NoteEdit.h folderDepth): the picker indents by it
    bool folder = false;       // a group header (choosing it brings its members)
    bool compTrack = false;    // a comp track (brings its take lanes)
    bool takeLane = false;
    int patternClips = 0, audioClips = 0, automationClips = 0, gridClips = 0, lanes = 0, versions = 0;
    std::string insert;        // the insert it plays through ("" = none / the Master), for the picker's second column
};
// The tracks with anything on them (clips, grid clips, lanes, versions, a mode, a name), in Playlist order.
std::vector<SourceTrack> listTracks (const Song& source);

// The Channel Rack channels no Playlist clip plays (an FL project in pattern mode, a kit): the picker lists them too.
struct SourceChannel
{
    int channel = -1;
    std::string name;
};
std::vector<SourceChannel> looseChannels (const Song& source);

struct Options
{
    bool withInserts = true;
    bool withAutomation = true;
    std::int64_t at = 0;            // where the other project's tick 0 lands (the song start, or the time selection's start)
    std::vector<int> channels;      // extra channels of the other project (looseChannels) to bring
};

struct Result
{
    bool ok = false;
    std::vector<int> tracks;        // the new Playlist tracks
    std::vector<int> channels;      // the new channels
    std::vector<int> inserts;       // the inserts filled
    int patterns = 0, automations = 0, clips = 0, gridClips = 0;
    std::vector<std::string> notes; // the import report (English, one line each)
};

// The chosen tracks with what they bring along: a comp track's take lanes, a folder's members (sorted).
std::vector<int> expandSelection (const Song& source, std::vector<int> tracks);

// One edit of `song` (inside AudioEngine::editSongStructural): the chosen tracks of `source` come in.
Result importTracks (Song& song, const Song& source, const std::vector<int>& tracks, const Options& options);

// The importers leave a sample they could not find out of the channel (its path empty) and list it in the report:
// the path goes back on the source's channel named in the list, so the open project's Missing files window can find
// it a new place. Returns how many paths went back.
int restoreMissingPaths (Song& source, const std::vector<std::pair<std::string, std::vector<std::string>>>& missing);
} // namespace lynx::trackimport
