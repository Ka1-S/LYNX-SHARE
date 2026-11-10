#pragma once

// "Export for other apps" (round 184a, slice S9; design: /mnt/e/lynx-work/simple184a/DESIGN.md §2.3.2): one click writes
// a folder that any other music app can take in, without the user knowing which formats it reads:
//   <name> (for other apps)/
//     Stems/NN <track>.wav         one per Playlist track that sounds, 24-bit, all from the song's start and of the
//                                  same length, so they line up when dropped at zero (no Master effects: the stems sum
//                                  to the mix before the Master, the mixdown has them)
//     MIDI/NN <track>.mid          each track with notes (tempo, time signature and key as meta events)
//     MIDI/<name> (all tracks).mid
//     <name> Project/<name>.als    a Live 12 set with its samples (core/AlsExport.h)
//     <name>.flp                   an FL Studio project (engine/FlStudioExport.h; LYNX-only sounds rendered to audio)
//     <name> - mixdown.wav         the whole song as heard (optional, on by default)
//     README.txt                   tempo, time signature, key, length, the files and how to import them
// This is the plan only: names, order, which tracks give stems or MIDI, the README. The writing is the UI's
// (ui/OtherAppsExport.*), with the existing writers. No JUCE.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/ProjectKey.h"
#include "core/Song.h"

namespace lynx::exportbundle
{
struct Options
{
    bool mixdown = true; // "Include the mixdown"
    bool zip = false;    // "Zip the folder" (the folder is replaced by "<folder>.zip")
};

struct TrackFile
{
    int track = -1;          // the Playlist track (0-based)
    int number = 0;          // NN in the file name: 1, 2, 3… in track order over the stems
    std::string name;        // the track's name ("Track N" when it has none)
    std::string path;        // relative to the bundle folder, '/' separated ("Stems/01 Drums.wav")
    std::vector<int> clips;  // indices into song.playlist: what plays on it (unmuted clips on an unmuted track)
};

struct Plan
{
    std::string name;                 // the project's name, safe as a file name
    std::string folder;               // "<name> (for other apps)"
    std::int64_t endTick = 0;         // every stem and the mixdown cover [0, endTick) plus the longest tail
    std::vector<TrackFile> stems;     // one per track that sounds, in track order
    std::vector<TrackFile> midi;      // one per track with notes (same number as its stem)
    std::string allMidi;              // "MIDI/<name> (all tracks).mid"; empty when the song has no notes
    std::string als;                  // "<name> Project/<name>.als"
    std::string alsFolder;            // "<name> Project" (where the .als writer puts it)
    std::string flp;                  // "<name>.flp"
    std::string mixdown;              // "<name> - mixdown.wav"; empty when not asked for
    std::string readme = "README.txt";
    bool zip = false;

    bool empty() const noexcept { return stems.empty() && allMidi.empty(); }
};

// The copy of `song` a bundle is planned and rendered from: Song mode, Pattern Blocks turned into clips on tracks of
// their own (blocks play on no Playlist track, so they would give no stem).
Song prepared (const Song& song);

// The bundle for `song` (prepared) under `projectName` (empty: "LYNX SHARE").
Plan plan (const Song& song, const std::string& projectName, const Options& options = {});

// The notes a track's MIDI file holds: its pattern clips as they play (FL's MIDI export of only those clips); empty when
// the track has no pattern clip (an audio track never falls back to the current pattern).
std::vector<Note> trackNotes (const Song& song, const TrackFile& track);
// The whole song's notes (FL's File › Export › "MIDI file..."), empty when no pattern clip plays.
std::vector<Note> songNotes (const Song& song);

// The MIDI key signature of the project key: sharps (+) or flats (−) −7…7, and minor. Scales that are neither major
// nor minor take the signature of the major or minor scale their third gives (Dorian → minor, Mixolydian → major).
// nullopt without a key.
struct KeySignature
{
    int sharps = 0;
    bool minor = false;
    bool operator== (const KeySignature&) const = default;
};
std::optional<KeySignature> keySignature (const Song& song);

// "120 BPM", "4/4", "A Minor Natural (Aeolian)" / "none" (English: the README is a file for any app, in English).
std::string tempoText (const Song& song);
std::string keyText (const Song& song);

// README.txt: what is in the folder and how to bring it into other apps. `seconds`: the song's length; `written` and
// `failed`: the relative paths, as the writer got on (failed ones are listed with why).
std::string readme (const Song& song, const Plan& plan, double seconds, const std::vector<std::string>& written,
                    const std::vector<std::string>& failed);
} // namespace lynx::exportbundle
