#pragma once

// The tracker editor's model (round 190nw, 0.1.31 queue item 2, stage 1 "M1"; design: study190nd/tracker/DESIGN-tracker.md
// §5a / §6). The tracker is a VIEW of a pattern's own notes, like the Piano roll and the Channel rack: no new data. Time
// runs down the rows (a row = ppq / rowsPerBeat ticks), each rack channel (or a track group of channels) is a track, and
// each track has as many note columns as its notes overlap. Every cell shows fields the notes already have:
//
//   Note  the key (FL's octave numbers, the app's note names), "OFF" where a note ends, "---" empty
//   Ins   the rack channel (only in a track group: which of its channels plays the note)
//   Vol   velocity 00..7F           Pan  00 (left) .. 40 (centre) .. 80 (right)
//   Dly   the ticks into the row the note starts (or, on an OFF, ends) at
//   Fx    LYNX's own note fields, one letter each: Gxx slide, Pxx porta, Yxx chance, Rxx release velocity, Txx fine pitch
//         (cents + 80), Mxx mute; Cxx = a note that ends inside the row it starts in (its length in ticks); round 190om:
//         Bxx the note's pitch curve (the pitch it ends at, semitones + 80: an imported pitch slide, vibrato, arpeggio)
//         and, on any row, Vxx the channel's volume multiplier pattern event in that row (00..FF = 0..200 %, 80 = 100 %)
//
// Lossless: a note occupies whole rows from its start to its end; two notes that would share a cell of a column go to two
// columns, and the start, the end (OFF + Dly) and every field are exact. An edit rewrites only the field it touches, so
// what the tracker does not show (Mod X / Y, colour, group, velocity range...) stays as it was.
// JUCE-free (tests/TrackerGrid190nwTests.inc); the UI is ui/TrackerComponent.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::tracker
{
// ---------------------------------------------------------------------------------------------------------------- layout
// (round 190om: bend = the note's pitch curve; volume = the track's channel's volume multiplier events, not a note field)
enum class Fx : std::uint8_t { slide, porta, chance, release, finePitch, mute, cut, bend, volume };
constexpr int fxKinds = 9;
char fxLetter (Fx fx) noexcept;                       // 'G', 'P', 'Y', 'R', 'T', 'M', 'C', 'B', 'V'
std::optional<Fx> fxFromLetter (char letter) noexcept; // upper or lower case; 'C' is not typed (it is the note's length)
const char* fxDescription (Fx fx) noexcept;           // English, TR_NOOP-marked ("glide into this note (slide)")

struct Track
{
    std::string name;
    std::vector<int> channels; // rack channels (one, or a group's)
    bool group = false;
    int noteColumns = 1;
    int fxColumns = 1; // per note column
    // round 190om: the fx slot of note column 0 that shows the channel's Volume multiplier pattern events (Vxx, on the
    // rows they fall in), −1 = the track's channel has none in this pattern (a group track never shows them)
    int volumeSlot = -1;
    std::vector<int> volumeRows; // per row: the value shown (0..255), −1 = no event in the row
};

// A note placed in the grid.
struct Placed
{
    int note = -1;     // index into Pattern::notes
    int track = 0, column = 0;
    std::int64_t startRow = 0, offRow = 0; // the rows of the start and of the end ("OFF")
    int startDelay = 0, offDelay = 0;      // ticks into those rows
    bool shortNote = false;                // ends inside its start row (Cxx)
    bool offShown = false;                 // the OFF has a cell of its own (not the next note's start, not past the end)
    std::int64_t occupiedEnd = 0;          // first row after the rows it occupies
};

struct Layout
{
    int pattern = -1;
    int rowsPerBeat = 4, rowsPerBar = 16;
    std::int64_t rowTicks = ticksPerStep;
    std::int64_t rows = 16;
    std::vector<Track> tracks;
    std::vector<Placed> placed;
    // placed indices per track and column, sorted by start
    std::vector<std::vector<std::vector<int>>> columns;
    int delayDigits = 2; // hex digits of the Dly (and Fx value) column: enough for rowTicks - 1, at least 2

    std::int64_t rowStart (std::int64_t row) const noexcept { return row * rowTicks; }
    std::int64_t rowOf (std::int64_t tick) const noexcept { return tick >= 0 ? tick / rowTicks : -((-tick + rowTicks - 1) / rowTicks); }
    int trackOfChannel (int channel) const noexcept;
    const Placed* placedAt (int track, int column, std::int64_t row) const noexcept; // a note starting there
    const Placed* soundingAt (int track, int column, std::int64_t row) const noexcept; // occupying the row (start..off)
    const Placed* placedOfNote (int note) const noexcept;
};

struct BuildOptions
{
    int rowsPerBeat = 4;
    std::vector<std::vector<int>> groups;   // channel lists shown as one track each (already resolved from uids)
    std::vector<std::string> groupNames;
    std::vector<int> alsoShow;              // channels shown even without notes (the selected one, a Simple track's)
    std::optional<std::vector<int>> onlyChannels; // show only these (a Simple track); nullopt = every channel with notes
    std::vector<std::pair<int, int>> extraColumns; // (channel of the track, extra empty note columns)
    // the column a note was put in by the user (stable columns while editing); -1 = none
    std::function<int (const Note&)> preferredColumn;
};

// The groups of `settings` as channel indices (unknown uids dropped, a channel in one group at most).
void resolveGroups (const Song& song, const TrackerSettings& settings, BuildOptions& options);

Layout build (const Song& song, int pattern, const BuildOptions& options);

// ------------------------------------------------------------------------------------------------------------ the cells
enum class Field : std::uint8_t { note, ins, vol, pan, dly, fxLetter, fxValue };

struct ShownColumns
{
    bool pan = false, dly = true, fx = true;
};

// A cursor position: a track, one of its note columns, a field of that column (fx: slot `fxSlot`), a row.
struct Cursor
{
    int track = 0, column = 0;
    Field field = Field::note;
    int fxSlot = 0;
    std::int64_t row = 0;
    bool operator== (const Cursor&) const = default;
};

// The fields of a track's note column in screen order.
struct FieldSlot
{
    Field field;
    int fxSlot = 0;
};
std::vector<FieldSlot> fieldsOf (const Layout& layout, int track, const ShownColumns& shown);
// cursor moves (left / right through fields, columns and tracks; wraps at neither end)
Cursor moveHorizontal (const Layout& layout, const ShownColumns& shown, Cursor c, int direction);
Cursor clampCursor (const Layout& layout, const ShownColumns& shown, Cursor c);

// Text of a note's key: "C-5", "C#5" (english), as the app's note names (FL's octaves: key 60 = C5).
std::string keyText (int key);
std::string numberText (int value, int digits, bool hex); // "7F" / "127"; "..": empty (value < 0)

struct Cell
{
    enum class Kind : std::uint8_t { empty, start, sustain, off } kind = Kind::empty;
    const Placed* placed = nullptr;
};
Cell cellAt (const Layout& layout, int track, int column, std::int64_t row);

// The value a field shows for a placed note (start cells); -1 = nothing shown (".." / the default).
int fieldValue (const Song& song, const Layout& layout, const Placed& p, Field field, int fxSlot);
// The fx the note shows, in its slot order (the non-default fields, and Cut for a short note).
std::vector<Fx> shownFx (const Layout& layout, const Placed& p, const Note& n);
int fxValueOf (const Note& n, Fx fx, std::int64_t length); // the 2-digit value (Cut: the length in ticks)
// Round 190om: Vxx — the volume multiplier pattern events (FL's channel parameter 0x11, AutomationTargetKind::channelAdjust)
// of `channel` in a pattern; the value shown for an event (0..255, 128 = 100 %) and back
const PatternEvents* volumeLane (const Pattern& pattern, int channel) noexcept;
int volumeValueOf (float eventValue) noexcept;
float volumeEventValue (int shown) noexcept;
// the volume slot of the cursor's cell (column 0, the track's volumeSlot)
bool isVolumeSlot (const Layout& layout, const Cursor& at) noexcept;
bool fxActive (const Note& n, Fx fx) noexcept;
int maxValue (const Layout& layout, Field field, std::optional<Fx> fx = std::nullopt);

// ----------------------------------------------------------------------------------------------------------- the edits
// Each returns true when the pattern changed. `pattern` is the layout's pattern (the caller's copy inside an edit).

// A note typed at the cursor (tracker semantics, LYNX's data): on a note's start cell the key changes (the rest stays);
// elsewhere a new note starts at the row's start and lasts until the next note in the column (or the pattern's end);
// a note sounding in that column across the row is ended there. `velocity` for a new note; `channel` plays it.
// Returns the index of the typed note in the edited pattern (-1 = nothing).
int typeNote (Pattern& pattern, const Layout& layout, const Cursor& at, int key, int channel, float velocity);
// OFF typed: the note sounding in the column ends at the row's start (a note starting on that row is removed first).
bool typeOff (Pattern& pattern, const Layout& layout, const Cursor& at);
// Del on a cell: the note field removes the note (or, on an OFF, lets the note last to the next one / the end); a value
// field goes back to the note's default (Vol 0.78, Pan centre, Dly 0 keeping the end, the fx off).
bool clearCell (Pattern& pattern, const Layout& layout, const Cursor& at);
// A value typed in a field of a note's start cell (or the Dly of an OFF): Vol 0..127, Pan 0..128, Dly 0..rowTicks-1,
// Ins = channel number + 1, Fx per kind.
bool setField (Pattern& pattern, const Layout& layout, const Cursor& at, int value, int channelCount = 1 << 20);
// An fx letter typed in the fx slot: the slot shows `fx` (the kind it showed goes back to its default).
bool setFxLetter (Pattern& pattern, const Layout& layout, const Cursor& at, Fx fx);

// Insert on the cursor's track: every note of its channels from row `row` on moves down a row, a note across the row
// gets a row longer. deleteRow removes row `row` of the track: its notes go, the later ones move up a row, a note across
// it gets shorter (Backspace removes the row above the cursor, as Furnace's).
bool insertRow (Pattern& pattern, const Layout& layout, int track, std::int64_t row);
bool deleteRow (Pattern& pattern, const Layout& layout, int track, std::int64_t row);

// ------------------------------------------------------------------------------------------------------- the selection
// A block: rows [row0, row1], columns from (track0, column0) to (track1, column1) inclusive (in grid order).
struct Selection
{
    int track0 = 0, column0 = 0, track1 = 0, column1 = 0;
    std::int64_t row0 = 0, row1 = 0;
    bool contains (const Layout& layout, int track, int column, std::int64_t row) const noexcept;
    static Selection of (const Cursor& a, const Cursor& b);
};
// The notes starting inside the block (indices into the pattern's notes, sorted).
std::vector<int> selectedNotes (const Layout& layout, const Selection& s);

struct ClipNote
{
    Note note;            // start relative to the block's first row's start
    int track = 0;        // relative to the block's first track
    int column = 0;       // the note column it came from (absolute)
    bool groupTrack = false;
};
struct Clipboard
{
    std::vector<ClipNote> notes;
    std::int64_t rows = 0, rowTicks = ticksPerStep;
    int tracks = 1;
    bool empty() const noexcept { return notes.empty() && rows == 0; }
};
Clipboard copy (const Pattern& pattern, const Layout& layout, const Selection& s);
bool erase (Pattern& pattern, const Layout& layout, const Selection& s);
// Pastes at the cursor's row and track (tracker paste: the notes starting in the pasted rows of the target tracks are
// replaced). Channels: a single-channel track takes the notes as its own; a group keeps a member channel, else its
// first. Returns the pasted notes' indices (sorted) in the edited pattern.
std::vector<int> paste (Pattern& pattern, const Layout& layout, const Cursor& at, const Clipboard& clip);
bool transpose (Pattern& pattern, const Layout& layout, const Selection& s, int semitones);
// The velocities of the notes in each column of the block go in a straight line from the first note's to the last's.
bool interpolateVolume (Pattern& pattern, const Layout& layout, const Selection& s);

// The block as text (one line per row, "|" between tracks), as other trackers' clipboards do; and back (notes, Vol,
// Dly and Fx; a note's length runs to the next note or OFF of its column).
std::string clipboardText (const Song& song, const Layout& layout, const Selection& s, bool hex);
std::optional<Clipboard> parseClipboardText (const std::string& text, std::int64_t rowTicks);

// ----------------------------------------------------------------------------------------------------------- the keys
// A value digit typed into a field: hex 0-9 A-F (dec: 0-9). Two (dec: three) digits complete a value; the first digit
// alone already shows. Returns the new value; `done` when the field is complete (the cursor moves on by the edit step).
struct ValueEntry
{
    int digits = 0, value = 0;
    int add (int digit, bool hex, int maxDigits, bool& done) noexcept;
    void reset() noexcept { digits = value = 0; }
};
int digitOf (int keyChar, bool hex) noexcept; // -1 = not a digit

// The row playing at pattern tick `tick` (-1 = none).
std::int64_t rowAt (const Layout& layout, double tick) noexcept;
} // namespace lynx::tracker
