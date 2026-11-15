#pragma once

// Pattern and Channel Rack operations used by the workflow keys and menus (docs/WORKFLOW_FL.md,
// P1 items 4 and 5). Pure functions over the Song model: no JUCE, unit-tested in tests/tests.cpp.

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// LYNX's colour palette for patterns, channels and inserts (0xFFRRGGBB). Original values.
const std::array<std::uint32_t, 12>& lynxPalette();

// A palette colour different from `current`, chosen from `seed` (any integer, e.g. a counter or time).
std::uint32_t randomPaletteColour (std::uint32_t current, std::uint32_t seed);

// --- patterns ----------------------------------------------------------------------------------

// True when the pattern has no notes (its name and colour don't count).
bool isPatternEmpty (const Pattern& pattern) noexcept;

// Index of the next/previous pattern (clamped to the list).
int stepPattern (const Song& song, int delta) noexcept;

// FL's pattern selector (the toolbar's PatSelect, a TVectorDigiWheel with Min 1 / Max 999; REA round 170a §3): a
// left-drag up or down steps through the patterns, the wheel steps one pattern per notch, a click without a drag opens
// the list. Shared by the toolbar's pattern box and the Channel Rack's pattern selector.
namespace patterndrag
{
inline constexpr int threshold = 3;              // px a press moves before it is a drag (and opens no list)
inline constexpr float pixelsPerPattern = 10.0f; // drag distance per pattern
// Patterns stepped by a drag `dragY` px from where it started (screen y grows downwards): up = the next patterns.
inline int steps (float dragY) noexcept { return (int) (-dragY / pixelsPerPattern); }
// The wheel: one pattern per notch (a trackpad's small deltas add up to a notch); up = the next pattern, as FL's
// selector ([SHOT] round 170a: a notch up on "Pattern 769" gave "Pattern 770").
struct Wheel
{
    float pending = 0.0f;
    static constexpr float notch = 0.1f;
    int take (float delta) noexcept
    {
        if (delta >= notch || delta <= -notch) // a mouse wheel's notch (JUCE: 0.23 on Windows): one pattern
        {
            pending = 0.0f;
            return delta > 0.0f ? 1 : -1;
        }
        if ((delta > 0.0f) != (pending > 0.0f) && pending != 0.0f)
            pending = 0.0f; // a turn the other way starts over
        pending += delta;
        if (pending >= notch || pending <= -notch)
        {
            const auto n = pending > 0.0f ? 1 : -1;
            pending = 0.0f;
            return n;
        }
        return 0;
    }
};
} // namespace patterndrag

// The pattern selector's step (drag / wheel): FL's selector goes on past the last pattern and creates the one it lands
// on ([SHOT] round 170a: a drag up from "Pattern 1" made "Pattern 769"). LYNX creates at most one: stepping forward
// from the last pattern appends a new empty one unless the last one is already empty. Returns the index to select.
int stepPatternCreating (Song& song, int delta);

// The first empty pattern after `from` (wrapping around, never `from` itself); -1 if there is none.
int findNextEmptyPattern (const Song& song, int from) noexcept;

// Selects the next empty pattern, appending a new one ("Pattern N") if every pattern has notes.
// Returns its index.
int gotoNextEmptyPattern (Song& song);

// FL's Patterns › "Find first empty" (Shift+F4, round 167j): selects the lowest-numbered empty pattern, appending a
// new one if every pattern has notes. Returns its index.
int gotoFirstEmptyPattern (Song& song);

// Inserts an empty pattern before `index` (Playlist clips keep pointing at the same patterns)
// and selects it. Returns the new index.
int insertPattern (Song& song, int index, const std::string& name);

// Creates one pattern per channel that has notes in pattern `index`, named "<pattern> - <channel>"
// and coloured after the channel, inserted right after it. The original pattern is unchanged.
// Returns how many patterns were created.
int splitPatternByChannel (Song& song, int index);

// Default name for a new pattern: "Pattern N" with the lowest N not in use.
std::string newPatternName (const Song& song);

// Copies pattern `index` (notes, length, colour) right after it, named "<name> #2" (or the next free
// number), and selects it. Playlist clips keep pointing at the same patterns. Returns the new index.
int clonePattern (Song& song, int index);

// Swaps pattern `index` with its neighbour above (delta < 0) or below; clips and the current pattern
// follow. Returns false at the ends.
bool movePattern (Song& song, int index, int delta);

// --- pattern hygiene (round 177d; FL's Playlist picker "Select unused › Patterns" and "Sort patterns by name / color",
// EEPickerSelectUnusedPatternsMenu / EEPickerSortPatternsBy*Menu in FLEngine; LYNX's own: merge identical patterns) ----
// Patterns that no clip plays in any arrangement, no pattern block uses and no Loop Starter channel belongs to (ascending).
std::vector<int> unusedPatterns (const Song& song);
// Deletes the given patterns (clips follow, as Song::removePattern). Returns how many went.
int deletePatterns (Song& song, std::vector<int> indices);
// Puts pattern `order[k]` at index k (a permutation of every pattern); clips, blocks, Loop Starter channels and the
// current pattern follow.
void reorderPatterns (Song& song, const std::vector<int>& order);
enum class PatternSort : std::uint8_t { name, colour };
void sortPatterns (Song& song, PatternSort by);
// Groups of patterns with the same content (notes, events, length, loops, markers, Loop Starter genre; the name and colour may differ),
// each group ascending, only groups of two or more.
std::vector<std::vector<int>> identicalPatterns (const Song& song);
// Every clip of a duplicate plays the group's first pattern instead, and the duplicates are deleted. Returns how many
// patterns were removed.
int mergeIdenticalPatterns (Song& song);

// --- step sequencer (docs/WORKFLOW_FL.md P2: fill, Euclidean fill, rotate, graph lanes) --------------

// Steps the pattern shows in the rack (its length in sixteenths, at least 16).
int patternSteps (const Song& song, const Pattern& pattern) noexcept;

// True when the channel's notes in the pattern cannot be shown as step buttons: a note starts off the step
// grid, lasts longer than a step, sits on another key than the step key (60) or slides/glides. The Channel
// Rack then draws a miniature of the notes instead of the steps (as FL does once a channel has piano-roll
// notes) and a click opens the Piano Roll. LYNX's own rule (FL's exact test is not confirmed).
bool showsNotePreview (const Pattern& pattern, int channel) noexcept;
// The Channel Rack's piano-roll view (FL's "step sequencer / piano roll overview" switch): every channel row shows
// its notes as a miniature, step patterns included; otherwise only the rows with piano-roll notes do.
bool showsNotePreview (const Pattern& pattern, int channel, bool pianoRollView) noexcept;

// Where a row's miniature is drawn (feedback 190bm; FL's Channel rack option "Show complete piano roll preview"):
// `x` from the left edge of the step grid and `width`, in pixels. The step grid is `gridWidth` wide (the pattern's
// steps at their fixed width), the rack shows `visibleWidth` of it from `scrollX`. Complete: a pattern wider than the
// view is drawn whole in the visible width and stays there while the steps scroll under it, so a long melody is seen
// without scrolling; a pattern that fits is step-aligned as before. Not complete: always step-aligned (it scrolls).
struct PreviewSpan
{
    int x = 0, width = 0;
    bool fitted = false; // narrower than the step grid: times are scaled, not step-aligned
};
constexpr PreviewSpan notePreviewSpan (int gridWidth, int visibleWidth, int scrollX, bool complete) noexcept
{
    if (! complete || visibleWidth <= 0 || gridWidth <= visibleWidth)
        return { 0, gridWidth, false };
    return { scrollX < 0 ? 0 : scrollX, visibleWidth, true };
}

// The key range of the channel's notes in the pattern (lowest, highest); {60, 60} without notes.
std::pair<int, int> channelKeyRange (const Pattern& pattern, int channel) noexcept;

// An Euclidean rhythm: `pulses` onsets spread as evenly as possible over `steps`, starting on the first
// step, then rotated right by `rotation` steps. pulses is clamped to 0..steps.
std::vector<bool> euclideanRhythm (int pulses, int steps, int rotation = 0);

// Turns on every `every`-th step of `channel` (from `offset`) over `steps` steps; existing notes stay.
void fillEverySteps (Pattern& pattern, int channel, int every, int steps, int offset = 0);

// Replaces the channel's step notes (notes starting exactly on a step) inside the first `steps` steps by
// `rhythm`, repeated to fill them. Notes that are off the step grid are left alone.
void setStepRhythm (Pattern& pattern, int channel, const std::vector<bool>& rhythm, int steps);

// FL's ghost steps (issue #11): a step turned on at `step` (0-based, `ticksPerStep` long) after the end of a pattern
// with a set length makes the pattern long enough for it, rounded up to whole bars of `barTicks`. An automatic length
// (0) follows the notes by itself and stays 0.
std::int64_t lengthWithStep (std::int64_t lengthTicks, int step, std::int64_t ticksPerStep, std::int64_t barTicks) noexcept;

// Moves every note of `channel` by `delta` steps, wrapping inside the first `steps` steps.
void rotateSteps (Pattern& pattern, int channel, int delta, int steps);

// True when the pattern has notes the step sequencer can't show as steps (off the step grid, another key
// or a held length): the Playlist then opens it in the Piano Roll.
bool hasPianoRollNotes (const Pattern& pattern) noexcept;

// Indices of the channel's notes that start inside step `step` (the cell the rack draws).
std::vector<int> notesInStep (const Pattern& pattern, int channel, int step);

// --- channel organisation ----------------------------------------------------------------------

enum class ChannelSort { name, colour, insert, type };

// The order (newOrder for permuteChannels) that sorts the channels; stable, so equal keys keep their order.
std::vector<int> channelSortOrder (const Song& song, ChannelSort by);

// Channels no pattern plays and no Playlist clip uses (and that no automation reads as a live source).
std::vector<int> unusedChannels (const Song& song);

// --- channel selection -------------------------------------------------------------------------

// Multi-selection of Channel Rack rows: always sorted, unique and inside [0, count).
struct ChannelSelection
{
    std::vector<int> rows;
    int anchor = 0; // start of Shift+click ranges

    bool contains (int row) const noexcept;
    void selectOnly (int row);
    void toggle (int row);         // Ctrl+click / right-click on the selector
    void extendTo (int row);       // Shift+click: anchor..row
    void selectAll (int count);
    void clamp (int count);        // after channels were removed
};

// --- channel operations ------------------------------------------------------------------------

// Reorders the channels: newOrder[i] = old index of the channel that ends up at i (a permutation).
// Notes, audio clips, automation/MIDI targets and controller sources follow their channel.
void permuteChannels (Song& song, const std::vector<int>& newOrder);

// Moves the selected channels up (delta < 0) or down by one row, as a block that stops at the
// edges. `selection` is updated to the new rows. Returns false when nothing moved.
bool moveChannels (Song& song, std::vector<int>& selection, int delta);

// Copies channel `index` (settings, not notes) to a new channel right below it, named "<name> #2"
// (or the next free number). Returns the new index.
int cloneChannel (Song& song, int index);

// FL (audit round 165z-a): a generator dropped from the Plugin database onto an existing channel replaces that
// channel's instrument; the channel keeps its place, notes, mixer insert, volume, pan, mute, colour, group and swing.
// Returns false when `index` is out of range.
bool replaceGenerator (Song& song, int index, ChannelDef generator);

// Inserts `channel` at `index` (shifting the others down). Returns the index.
int insertChannel (Song& song, int index, ChannelDef channel);

// Adds `channel` at the end of the rack, routed to `insert` (0 = Master). An insert that doesn't exist (the default, -1)
// claims a free one instead. Returns the new channel's index.
// With an existing insert (a generator dropped on a mixer strip or effect slot) it does what FL 2026 does (round 167u):
// a name another channel has gets " #2", " #3"..., and the insert takes the channel's name while it still has its
// default one ("Insert N", Master's "Master"); a name the user gave the insert stays.
int addChannelRouted (Song& song, ChannelDef channel, int insert = -1);

// Removes several channels (any order). Returns how many were removed.
int removeChannels (Song& song, std::vector<int> channels);

// An insert nobody uses yet: no channel routes to it, it has no effects and no insert sends into it.
bool isInsertFree (const Song& song, int track) noexcept;

// Routes each channel to its own free insert (adding inserts at the end if needed, up to
// maxMixerTracks), named and coloured after the channel. Returns the insert of each channel
// (0 = could not get one).
std::vector<int> sendChannelsToFreeInserts (Song& song, const std::vector<int>& channels);

// --- Channel settings "Misc" tab (REA round 143b; FL's channel window: Levels, Polyphony, Time, Group, Arpeggiator) --
// Which of the per-channel functions a channel's instrument has: Mono (one voice, the new note takes over) and
// Portamento (glide from the previous note) exist for LYNX's synth and sampler; Max polyphony, swing mix, cut groups
// and the arpeggiator exist for every channel.
bool channelHasMono (const ChannelDef& c) noexcept;
bool channelHasPortamento (const ChannelDef& c) noexcept;
bool channelMono (const ChannelDef& c) noexcept;
void setChannelMono (ChannelDef& c, bool on) noexcept;
bool channelPortamento (const ChannelDef& c) noexcept;
float channelPortamentoBeats (const ChannelDef& c) noexcept;
void setChannelPortamento (ChannelDef& c, bool on, float beats) noexcept;
// FL's "Cut self" (the channel's cut group and "cut by" are its own): LYNX gives it the group 1000 + channel.
bool channelCutsItself (const ChannelDef& c) noexcept;
void setChannelCutsItself (ChannelDef& c, int channel, bool on) noexcept;
// The cut-group number as the Misc tab shows it: "--" for none, "self" for the channel's own group, else the number.
std::string cutGroupText (int group, int channel);

// FL's Channel Rack Cut / Copy / Paste (Ctrl+X / Ctrl+C / Ctrl+V with the rack focused, and the channel menu's Cut /
// Copy / Paste; REA round 170a §1: EditCutAction / CutMenuClick → the selected channels' steps and notes in the current
// pattern). The clipboard runs from the first selected channel to the last channel: slot k is channel first + k, a
// channel that was not selected is a gap that a paste leaves alone. A paste starts at the first selected channel, each
// copied slot replaces that channel's notes (an empty slot clears them) and its loop point (none: removed). A cut
// removes the notes (the loop points stay).
struct ChannelStepsClipboard
{
    struct Slot
    {
        bool copied = false;     // false: that channel was not selected (left alone by a paste)
        std::vector<Note> notes; // channel field = 0
        bool loops = false;      // it had an entry in Pattern::channelLoops
        int loopSetting = 0;
    };
    std::vector<Slot> slots;
    bool empty() const noexcept { return slots.empty(); }
};
// `selected`: channel indices (any order). Empty clipboard when nothing is selected.
ChannelStepsClipboard copyChannelSteps (const Pattern& pattern, const std::vector<int>& selected, int channelCount);
// Removes the notes of the `selected` channels. Returns true when something was removed.
bool clearChannelSteps (Pattern& pattern, const std::vector<int>& selected);
// Pastes from the first selected channel; returns false (nothing changed) when the clipboard is empty or no channel
// is selected.
bool pasteChannelSteps (Pattern& pattern, const ChannelStepsClipboard& clipboard, const std::vector<int>& selected, int channelCount);

// FL's hint bar over a step shows its song position as bar:step:tick ("1:02:00"): bar from 1, step (a 16th) from 1
// with two digits, the tick inside the step (in FL's 96-per-quarter units: 24 per step) with two digits.
std::string stepPositionText (std::int64_t tick, Meter meter);

// FL's Tools › Macros › "Change declicking for selected channels to" (FUN_011f7180, REA round 181b): only audio clip
// channels change. Returns how many did.
int audioClipsAmong (const Song& song, const std::vector<int>& channels);
int setDeclickOfAudioClips (Song& song, const std::vector<int>& channels, int mode);
// FL's Tools › Macros › "Switch all / selected audio clip channels to" (FUN_01213a20, REA round 181b): every audio
// clip channel among `channels` gets the mode (keepPitch false = Resample). Returns how many changed.
int setStretchOfAudioClips (Song& song, const std::vector<int>& channels, bool keepPitch, StretchMethod method);
// FL's Tools › Macros › "Prepare for MIDI export" (FUN_0122d440, REA round 181b): every channel becomes a MIDI Out and
// keeps its name; an FPC plays on MIDI channel 10, the others take 1...16 in turn without 10.
void prepareForMidiExport (Song& song);
} // namespace lynx
