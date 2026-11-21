#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "core/AutoPointEdit.h"
#include "core/RackOps.h"
#include "core/Song.h"

// Issue #232, stage 2 (round 190nz, docs/design/PROJECT_TABS.md): what one LYNX SHARE window copies, as text another
// window (another process, or a later session) can paste. LYNX's own versioned JSON on the system clipboard, after a
// first line that names it; a copy larger than `inlineLimit` (plugin states, many patterns) goes to a file and the
// clipboard holds its path (the "side channel"). The project parts are written by the project's own JSON code
// (ProjectFile), so whatever a .lynx keeps, a copy keeps. juce_core only (tests/CrossClipboardTests.cpp).
namespace lynx::crossclipboard
{
// What was copied:
// notes       Piano roll notes (Ctrl+C / Copy ghost notes)
// clips       Playlist / clip grid clips, with the patterns, audio clip channels and automation clips they play
// steps       Channel Rack Ctrl+C: the selected channels' steps and notes in the current pattern
// channels    Channel Rack › Copy channels: the channels, their instruments, plugin states and samples (paths)
// mixerTracks Mixer track menu › File › Copy mixer track state: effects with their states, level, pan, EQ, name, colour
// points      selected automation points (Playlist)
enum class Kind { notes, clips, steps, channels, mixerTracks, points };
const char* kindName (Kind kind) noexcept;
std::optional<Kind> kindFromName (const juce::String& name) noexcept;

struct Payload
{
    Kind kind = Kind::notes;
    juce::String stamp;  // unique for each copy (the window that made it knows its own)
    int pid = 0;         // the process that copied it
    juce::String from;   // the project it was copied from (its name; shown when it is pasted)

    std::vector<Note> notes;             // notes
    Song fragment;                       // clips, channels (core/CrossPaste.h)
    ChannelStepsClipboard steps;         // steps
    std::vector<MixerTrack> mixerTracks; // mixerTracks (states, ProjectFile::mixerTrackStateToJson)
    autopoints::Clipboard points;        // points
};

constexpr int formatVersion = 1;
// The first line of the clipboard text.
constexpr const char* magic = "LYNX SHARE clipboard";

// The payload as one JSON document, and back (nullopt: not a LYNX clipboard document, a newer format, or damaged).
juce::String toJson (const Payload& payload);
std::optional<Payload> fromJson (const juce::String& json);

// What sits on the system clipboard: `magic` + " " + version, a line with the kind, stamp and process
// ("<kind> <stamp> <pid>"), then the document itself, or "file " + the path of the file holding it.
constexpr std::size_t inlineLimit = 64 * 1024;
struct Envelope
{
    Kind kind = Kind::notes;
    juce::String stamp;
    int pid = 0;
    juce::String document; // inline
    juce::File file;       // or this file
};
// The clipboard text for `payload`; when its document is larger than `limit` it is written to `sideFolder` (created;
// written to a temporary file then moved, so a reader never sees half of it) and the text points at it. Empty when
// that file cannot be written.
juce::String clipboardText (const Payload& payload, const juce::File& sideFolder, std::size_t limit = inlineLimit);
// The envelope of a clipboard text (nullopt: not LYNX's). Cheap: the document is neither parsed nor read.
std::optional<Envelope> envelopeOf (const juce::String& text);
// The payload of an envelope (the file read when it is in one; nullopt when it is gone or damaged).
std::optional<Payload> open (const Envelope& envelope);
// Removes the side files but the `keepNewest` newest (the system clipboard only ever points at the newest; the folder is
// shared by every window, so a copy made at the same moment in another window stays too).
void prune (const juce::File& sideFolder, int keepNewest = 4);
} // namespace lynx::crossclipboard
