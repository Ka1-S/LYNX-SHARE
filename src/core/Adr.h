#pragma once

// ADR, automated dialogue replacement (round 174d, issue #83, docs/analise/REA_rodada174d.md §5): LYNX's own (FL has
// none). The cue list (in / out timecode, character, the line, notes), its CSV import / export, the takes of each cue
// (named "<cue>_<character>_T<n>", rated, circled), the comp (which take plays each part of the cue), and the cue
// signals the actor sees and hears before the line: the streamer (a bar crossing the picture), the punch (a flash at
// the in point), the wipe, and the three beeps one second apart (the fourth, silent, is the in point). No JUCE here
// (tests/Video174dTests.inc); ui/AdrWindow runs the record cycle.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/VideoTime.h"

namespace lynx
{
struct AdrTake
{
    int number = 1;           // T1, T2 …
    std::string name;         // "<cue>_<character>_T<n>" (renamable)
    std::uint64_t clip = 0;   // the uid of its audio clip on the Playlist
    int rating = 0;           // 0…5 stars
    bool circled = false;     // the director's pick
    std::string note;
    bool aligned = false;     // made by Align from another take
    bool operator== (const AdrTake&) const = default;
};

// One part of the comp: from `from` to `to` (song seconds) the cue plays take `take` (its number)
struct AdrCompPart
{
    double from = 0.0, to = 0.0;
    int take = 1;
    bool operator== (const AdrCompPart&) const = default;
};

struct AdrCue
{
    std::string id;        // "101", "SC12-04" … (unique in the list)
    double in = 0.0;       // song seconds
    double out = 0.0;
    std::string character; // who says it
    std::string line;      // the dialogue
    std::string notes;
    enum class Status : std::uint8_t { todo, recorded, approved, omit } status = Status::todo;
    std::vector<AdrTake> takes;
    std::vector<AdrCompPart> comp; // empty: the selected take plays the whole cue
    std::vector<std::uint64_t> compClips; // the Playlist clips the last "Apply the comp" made (replaced by the next)
    int selected = 0;      // the take that plays (its number; 0 = none)
    bool operator== (const AdrCue&) const = default;
};

struct AdrSettings
{
    double preRoll = 3.0, postRoll = 1.0; // seconds before the in / after the out point
    bool beeps = true;                     // the three beeps before the in point
    double beepFrequency = 1000.0;         // Hz
    bool streamer = true;                  // the streamer on the video window
    double streamerLength = 2.0;           // seconds the streamer takes to cross the picture
    std::uint32_t streamerColour = 0xFFFFFFFF;
    bool punch = true;                     // a flash at the in point
    bool cycle = false;                    // record again and again until stopped (one take per pass)
    int guideInsert = -1;                  // the mixer insert of the original dialogue (the guide), −1 none
    int headphoneOutput = -1;              // the device output pair the guide and the beeps go to (0-based), −1 none
    int recordInsert = -1;                 // the insert the actor is recorded through, −1 = the armed ones
    int takeTrack = -1;                    // the Playlist track the takes go on (−1 = the first free audio track)
    bool operator== (const AdrSettings&) const = default;
};

struct AdrSession
{
    std::vector<AdrCue> cues;
    AdrSettings settings;
    int current = 0; // the selected cue
    bool operator== (const AdrSession&) const = default;
};

namespace adr
{
const char* statusName (AdrCue::Status s) noexcept; // "To do", "Recorded", "Approved", "Omit" (English)
std::optional<AdrCue::Status> statusFromName (const std::string& s) noexcept;

// The next free cue id after the list's last ("101" → "102", "SC1-09" → "SC1-10", none → "1")
std::string nextId (const std::vector<AdrCue>& cues);
// "<cue>_<character>_T<n>" with characters a file name can take (spaces → "-")
std::string takeName (const AdrCue& cue, int number);
int nextTakeNumber (const AdrCue& cue) noexcept;
// Sorts the cues by in point (stable)
void sortByTime (std::vector<AdrCue>& cues);
// The cue under song time t (in ≤ t < out), else −1
int cueAt (const std::vector<AdrCue>& cues, double t) noexcept;

// --- CSV ------------------------------------------------------------------------------------------------------------
// Columns: Cue, In, Out, Character, Line, Notes, Status (header row; timecode at `rate` on the clock that starts at
// `startTimecode`). Reading accepts ',' ';' or tab separators, quoted fields ("" inside quotes), a BOM, the columns in
// any order (by header name, English; also "ID", "Start", "End", "Role", "Dialogue", "Text", "Comment"), and times as
// timecode or seconds. Rows without a valid In are skipped; Out ≤ In becomes In + 2 s.
std::string toCsv (const std::vector<AdrCue>& cues, const vtime::Rate& rate, double startTimecode);
struct CsvResult
{
    std::vector<AdrCue> cues;
    int skipped = 0;
    std::string error; // empty: read (no header with an In column: an error)
};
CsvResult fromCsv (const std::string& text, const vtime::Rate& rate, double startTimecode);

// --- the cue signals -------------------------------------------------------------------------------------------------
// The beeps before `in`: at in − 3, in − 2, in − 1 s, each `length` s long (the in point itself is the silent fourth)
std::vector<double> beepTimes (double in);
constexpr double beepLength = 0.04; // 40 ms (one frame at 25 fps)
// Renders the beep tone (sine with 2 ms fades), mono
std::vector<float> renderBeep (double sampleRate, double frequency, double length = beepLength);

// What the video window draws at song time t for a cue: the streamer's position (0 = the picture's left edge, 1 = the
// right edge, reached at `in`; nullopt when not running) and whether the punch flash shows (the frame at `in` and the
// next one)
struct Signals
{
    std::optional<double> streamer;
    bool punch = false;
    bool inLine = false; // in ≤ t < out (the line is being said: a mark in the corner)
};
Signals signalsAt (const AdrCue& cue, const AdrSettings& settings, double t, double frameSeconds) noexcept;

// The comp of a cue as a list of (take, from, to) covering [in, out): the comp parts, gaps filled by the selected take
struct CompSpan
{
    int take = 0;
    double from = 0.0, to = 0.0;
};
std::vector<CompSpan> compSpans (const AdrCue& cue);
// Sets take `take` for [from, to) in the cue's comp (parts it covers are cut, neighbours of the same take merge)
void setCompRange (AdrCue& cue, double from, double to, int take);
} // namespace adr
} // namespace lynx
