#pragma once

// Note editing operations used by the Piano Roll (selection, copy/paste,
// quantize...). They work with indices into Pattern::notes. No JUCE, testable.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/Song.h"
#include "core/FlSettings.h"

namespace lynx::noteedit
{
// Sorts items with a `start` field (notes, clips) and updates `tracked` (indices) to the new positions.
template <typename T>
void sortByStartTracking (std::vector<T>& items, std::vector<int>& tracked)
{
    std::vector<int> order (items.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;

    std::stable_sort (order.begin(), order.end(),
                      [&] (int a, int b) { return items[(std::size_t) a].start < items[(std::size_t) b].start; });

    std::vector<int> newIndex (order.size());
    std::vector<T> sorted;
    sorted.reserve (order.size());

    for (std::size_t j = 0; j < order.size(); ++j)
    {
        newIndex[(std::size_t) order[j]] = (int) j;
        sorted.push_back (items[(std::size_t) order[j]]);
    }

    items = std::move (sorted);

    for (auto& i : tracked)
        if (i >= 0 && i < (int) newIndex.size())
            i = newIndex[(std::size_t) i];
}

// Sorts the notes by start and updates `tracked` (indices) to the new positions.
void sortTracking (Pattern& pattern, std::vector<int>& tracked);

// Removes the notes at the given indices (in any order, duplicates ignored).
void remove (Pattern& pattern, std::vector<int> indices);

// Copies of the notes at the given indices, in start order.
std::vector<Note> copy (const Pattern& pattern, const std::vector<int>& indices);

// Appends `notes` shifted by `offset` ticks; channel >= 0 forces the channel.
// Returns the indices of the new notes (the vector is not re-sorted).
std::vector<int> paste (Pattern& pattern, const std::vector<Note>& notes, std::int64_t offset, int channel = -1);

// Start of the first note and end of the last (0, 0 if empty).
void span (const std::vector<Note>& notes, std::int64_t& start, std::int64_t& end);

// Duplicates the notes right after themselves (like FL's Ctrl+B): the offset is the
// span length rounded up to a multiple of `grid`. Returns the new indices.
std::vector<int> duplicate (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// The Piano roll's Ctrl+B / Duplicate (round 170h, FL 2026 driven with the snap on Step): the copy goes whole bars
// later, the selection's span rounded up to bars (notes over steps 1-8 -> bar 2, over 1-24 -> bar 3), so a riff
// repeats in time even when its last note ends before the bar line.
std::vector<int> duplicateBars (Pattern& pattern, const std::vector<int>& indices, int beatsPerBar);

// Rounds the notes' start to the nearest multiple of `grid` (length kept).
void quantize (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Rounds start and end to the nearest multiple of `grid` (the length stays at least one grid unit).
void quantizeFull (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Moves the notes by (deltaTicks, deltaKeys), clamping so none leaves [0, ...) and [0, 127].
// Returns the offset actually applied.
void move (Pattern& pattern, const std::vector<int>& indices, std::int64_t& deltaTicks, int& deltaKeys);

// --- quick note operations (each is one undo step in the Piano Roll) -----------------

// Splits note `index` at `tick` (absolute) when the tick is strictly inside it. The original keeps the
// head; the tail is appended. Returns the tail's index, or -1 when nothing was split.
int split (Pattern& pattern, int index, std::int64_t tick);

// Chop to grid: cuts every note at each multiple of `grid` that falls inside it. Returns the indices of
// all the resulting pieces (originals included; the vector is not re-sorted).
std::vector<int> chop (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Join: merges notes of the same channel and key that touch (end == next start) or overlap into one
// note (the first one's properties are kept). Returns the number of notes removed.
int join (Pattern& pattern, const std::vector<int>& indices);

// Legato: each note (per channel) lasts until the next later start among the given notes; notes that
// start together (a chord) end together. The last ones keep their length.
void legato (Pattern& pattern, const std::vector<int>& indices);

// Shrink to grid: every length becomes one grid unit.
void shrinkToGrid (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Rotate within the bar: moves the notes `steps` grid units (negative = left), wrapping inside the
// whole bars the notes start in (`bar` = ticks per bar).
void rotate (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid, std::int64_t bar, int steps);

// --- grid --------------------------------------------------------------------------

// The multiple of `step` at or before `tick` (step <= 1: whole ticks).
std::int64_t snapDown (double tick, std::int64_t step);

// The multiple of `step` nearest to `tick`, a tie going to the later one (step <= 1: whole ticks). FL puts the song
// position on the grid line nearest to a click in the Playlist's ruler (round 169d, [SHOT] r169d f32–f34: Bar snap,
// clicks at bar 5.33 / 5.5 / 5.83 → 5:01, 6:01, 6:01), not on the line before it.
std::int64_t snapRound (double tick, std::int64_t step);

// The candidate closest to `tick` when it lies within `maxDistance` ticks; otherwise snapDown (tick, fallbackStep).
std::int64_t snapNearest (double tick, const std::vector<std::int64_t>& candidates, double maxDistance, std::int64_t fallbackStep);

// The finest of the usual grid steps (1/4 step, 1/2 step, step, beat, bar, then whole multiples of bars)
// whose width on screen is at least `minPixels`, i.e. the lines the editor draws ("Screen" grid).
std::int64_t screenStep (double pixelsPerTick, std::int64_t bar, double minPixels = 16.0);

// The step of FL's "Line" and "Cell" snaps at a zoom (round 190ax; FL 2026 FLEngine FUN_00e59e90, which both modes
// share: they differ in how a position is rounded, not in the step; docs/analise/REA_rodada190ax.md). With `min` =
// minPixels truncated to whole pixels (FL: 10), the first of these that holds:
//   a step is at least `min` wide                        -> one step
//   two steps are (not in x/16 with an odd numerator)    -> two steps (half a beat in x/4)
//   a beat is                                            -> one beat
//   two beats are, and the numerator is even             -> two beats
//   otherwise                                            -> one bar, doubled while narrower than `min` (up to 32 bars)
// So it is never finer than a step, however far in the zoom goes. (FL's one exception, 1 tick at its maximum zoom of
// 8 px per tick, is out of reach here: at 96 PPQ that is 3072 px per bar and LYNX's editors stop at 800.)
// numerator / denominator: the time signature (a beat is 16 / denominator steps, a bar `numerator` beats).
std::int64_t flLineStep (double pixelsPerTick, int numerator, int denominator = 4, double minPixels = 10.0);

// "bar:step:tick" (bar and step from 1, tick from 0; 16 steps per 4/4 bar) <-> ticks.
std::string formatTime (std::int64_t ticks, int beatsPerBar, bool isLength = false); // lengths count bars and steps from 0
bool parseTime (const std::string& text, int beatsPerBar, std::int64_t& ticks, bool isLength = false);

// --- Playlist clips ------------------------------------------------------------------

// Moves the clip's left edge to `newStart` without moving its content (the offset follows). The start
// cannot go before the content's beginning (offset 0) nor past end - minLength.
void trimStart (Clip& clip, std::int64_t newStart, std::int64_t minLength);

// Slip: moves the content under the clip by `delta` ticks (positive = later); the clip stays put.
// The offset never goes below 0.
void slip (Clip& clip, std::int64_t delta);

// The Playlist's paint brush (round 169d, issue #32): a copy it would paint over a clip of the same source (pattern,
// audio channel, automation) on the same track. FL's brush does not stack one there ([SHOT] r169d f37–f39: a stroke
// from bar 1 across clips of the same pattern at bars 2–6 adds only bar 1's; deleting bar 3's leaves nothing under it).
// Stacked copies played the pattern twice ("later on playback they sound doubled (louder)").
bool paintWouldStack (const std::vector<Clip>& playlist, const Clip& c);

// Inserts `length` ticks of silence at `at`: clips starting at or after it (and markers) move right.
void insertTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t at, std::int64_t length);

// Removes [from, to) from the timeline: what lies inside is deleted, clips crossing an edge are cut,
// and everything after `to` (clips and markers) moves left.
void deleteTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t from, std::int64_t to);

// Playlist › Edit › "Slice & insert space" (Ctrl+Alt+Ins; EESliceInsSpaceAction, Tag 0 → FUN_00eb35f0 / FUN_00e3b2b0,
// round 189e): the clips crossing `at` are cut there first, then `length` ticks are inserted as insertTime does.
void sliceInsertTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t at, std::int64_t length);

// Playlist › Edit › "Trim selection" (EETrimSpaceAction → FUN_00eb35f0, its "trim space" branch, round 189e): the space
// after `to` (to the end of the song) and then the space before `from` are deleted as deleteTime does, so only the time
// selection is left, at the song start.
void trimTime (std::vector<Clip>& clips, std::vector<Marker>& markers, std::int64_t from, std::int64_t to);

// Indices of the clips that overlap [from, to).
std::vector<int> clipsInTime (const std::vector<Clip>& clips, std::int64_t from, std::int64_t to);

// Repeats the time selection [from, to) right after itself: copies of `sources` (or, if empty, of the
// clips that start inside it) shifted by its length. Returns the new indices.
std::vector<int> repeatTime (std::vector<Clip>& clips, std::int64_t from, std::int64_t to, std::vector<int> sources);

// --- ripple editing and time-selection moves (docs/WORKFLOW_FL.md P2) -------------------------

// Ripple delete: removes the clips and closes each gap: on the same track, the clips after a removed
// clip move left by its length.
void rippleDelete (std::vector<Clip>& clips, std::vector<int> indices);

// Ripple resize: clip `index` gets `newLength` (at least 1) and every later clip on its track moves by the
// change, so nothing after it overlaps or opens a gap.
void rippleResize (std::vector<Clip>& clips, int index, std::int64_t newLength);

// Moves (or copies) what lies inside the time selection [from, to) by `delta` ticks: clips crossing its
// edges are cut there first, so only the part inside moves. Returns the indices of the moved clips (or
// of the copies). The start never goes below 0.
std::vector<int> moveTimeRange (std::vector<Clip>& clips, std::int64_t from, std::int64_t to, std::int64_t delta, bool copy);

// --- Playlist tracks and clips (docs/WORKFLOW_FL.md P2: clip and track menus) ------------------

// Cuts clip `index` at every multiple of `grid` (song time) inside it. Returns the indices of all pieces.
std::vector<int> chopClip (std::vector<Clip>& clips, int index, std::int64_t grid);

// Inserts an empty track at `track` (the clips there and below move down); every arrangement follows.
void insertTrack (Song& song, int track);
// Removes track `track` and its clips (the tracks below move up), in every arrangement.
void removeTrack (Song& song, int track);
// Swaps two tracks (their clips and headers), in every arrangement.
void swapTracks (Song& song, int a, int b);
// Solo: `track` plays alone (every other track muted); on the only unmuted track it unmutes all.
// `memory` (round 190bx, core/FlSettings.h; null = none kept): the solo remembers every track's mute switch; with
// `restore` (FL's General › "Restore previous state after solo") the un-solo puts them back instead of unmuting
// everything.
void soloTrack (Song& song, int track, int trackCount, flsettings::SoloMemory* memory = nullptr, bool restore = false);
// FL's channel solo (FUN_00ffaaa0, REA_rodada167l §5): `channel` plays and every other channel is muted, except the
// ones with "Lock active/mute state"; on a channel already playing alone (among the unlocked ones) every channel the
// solo touched plays again (`memory` / `restore`: as soloTrack's).
void soloChannel (Song& song, int channel, flsettings::SoloMemory* memory = nullptr, bool restore = false);

// --- Playlist track groups (FL's "Group with above track", REA round 104) -----------------------------
// A track with `grouped` set belongs to the group of the nearest ungrouped track above it: the header.
bool isGroupHeader (const Song& song, int track);            // the next track is grouped with it
int groupHeaderOf (const Song& song, int track);             // the header of the track's group (itself for a header), -1 if none
std::vector<int> groupMembers (const Song& song, int header); // the grouped tracks under a header
bool isFoldedAway (const Song& song, int track);             // a member of a collapsed group (drawn as a thin row)
void groupWithAbove (Song& song, int track);                 // no-op on track 0
void ungroupTrack (Song& song, int track);                   // a member leaves its group (headers stay)
void setGroupCollapsed (Song& song, int track, bool collapsed); // the group of `track`
void colourGroup (Song& song, int track);                    // "Auto color group": members take the header's colour

// --- Piano roll note template and Alt+wheel (round 166e, FL 2026 measured in docs/analise/REA_rodada166e.md) --------
// FL: clicking a note (or Alt+wheel-editing the nearest one) makes its properties the ones new notes get. Velocity and
// pan were measured; release, fine pitch and mod X / Y travel with them (the length already did: lastLength).
struct NoteTemplate
{
    float velocity = Note {}.velocity, pan = Note {}.pan, release = Note {}.release, finePitch = Note {}.finePitch;
    float modX = Note {}.modX, modY = Note {}.modY;

    void takeFrom (const Note& n) noexcept
    {
        velocity = n.velocity;
        pan = n.pan;
        release = n.release;
        finePitch = n.finePitch;
        modX = n.modX;
        modY = n.modY;
    }
    void applyTo (Note& n) const noexcept
    {
        n.velocity = velocity;
        n.pan = pan;
        n.release = release;
        n.finePitch = finePitch;
        n.modX = modX;
        n.modY = modY;
    }
};

// FL's Alt+wheel with nothing selected: the note of `candidates` nearest to the mouse at (tick, key), measured in
// pixels to the note's rectangle (`pixelsPerTick` across, `rowHeight` per key); −1 when there are none.
inline int nearestNote (const std::vector<Note>& notes, const std::vector<int>& candidates, double tick, int key, double pixelsPerTick,
                        double rowHeight)
{
    int best = -1;
    double bestDistance = 0.0;
    for (auto i : candidates)
    {
        if (i < 0 || i >= (int) notes.size())
            continue;
        const auto& n = notes[(std::size_t) i];
        const auto dx = std::max ({ 0.0, (double) n.start - tick, tick - (double) (n.start + n.length) }) * pixelsPerTick;
        const auto dy = (double) std::abs (n.key - key) * rowHeight;
        const auto d = dx * dx + dy * dy;
        if (best < 0 || d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

// FL's editor arrow keys on a selection (ShortcutsModule EEShiftU/D, EEShiftU12/D12, EEShiftL/R, round 167j):
// Shift+Up/Down = a semitone, Ctrl+Up/Down = an octave, Shift+Left/Right = one grid step. The plain arrows act like
// Shift (a LYNX extra). Ctrl+Shift+Left/Right (rotate), Ctrl+Left/Right and Alt are not arrow moves.
struct ArrowMove
{
    bool handled = false;
    int keys = 0;      // semitones
    int gridSteps = 0; // multiples of the grid
};

enum class Arrow { up, down, left, right };

inline ArrowMove arrowMove (Arrow arrow, bool ctrl, bool shift, bool alt) noexcept
{
    if (alt)
        return {};
    if (arrow == Arrow::up || arrow == Arrow::down)
    {
        if (ctrl && shift)
            return {};
        const auto sign = arrow == Arrow::up ? 1 : -1;
        return { true, sign * (ctrl ? 12 : 1), 0 };
    }
    if (ctrl)
        return {};
    return { true, 0, arrow == Arrow::right ? 1 : -1 };
}

// --- Piano roll Select / Edit items of FL's editor menu (round 168g, docs/analise/REA_rodada168g.md) -------------------
// Select one at random (Shift+R) / more at random (Shift+M) (EESelRndAction → FUN_00eb2510): adds random notes of
// `channelNotes` that are not selected yet to `selection`: one, or (more) half as many as the channel has (rounded
// down, at least the ones left). Returns the new selection (sorted).
std::vector<int> selectRandomMore (const Pattern& pattern, const std::vector<int>& channelNotes, std::vector<int> selection, bool more,
                                   std::uint32_t seed);
// Select odd (Shift+O, EESelSwingAction → FUN_00eb2b70: "Select odd (swung) notes"): adds the notes whose start rounds to
// an odd multiple of `step` (FL: one step = PPQ / 4) to `selection`. Returns the new selection (sorted).
std::vector<int> selectOddSteps (const Pattern& pattern, const std::vector<int>& channelNotes, std::vector<int> selection, std::int64_t step);

// Insert space / Slice & insert space / Delete space / Trim (EEInsSpaceAction → FUN_00eb35f0 / FUN_00e3b2b0) on the
// time selection [from, to) of the notes `channelNotes`. Insert: every note starting at or after `from` moves right by
// to - from (`slice`: the notes crossing `from` are cut there first, so their tails move). Delete: the notes crossing
// `from` and `to` are cut there, the ones starting inside are removed and the later ones move left. Returns the
// indices of the notes that moved (FL selects them); pattern markers follow (moved / removed like the notes).
std::vector<int> insertSpace (Pattern& pattern, const std::vector<int>& channelNotes, std::int64_t from, std::int64_t to, bool slice);
std::vector<int> deleteSpace (Pattern& pattern, const std::vector<int>& channelNotes, std::int64_t from, std::int64_t to);
// Trim (FUN_00eb35f0, the "trim space" branch): deletes the space after `to` (to the pattern's end `end`) and before
// `from`, so only the time selection is left, at the start of the pattern.
void trimToSelection (Pattern& pattern, int channel, std::int64_t from, std::int64_t to, std::int64_t end);

// The Piano roll's pattern time markers (FL's Piano roll › Time markers, the same EEMarker* items as the Playlist's on
// the pattern's own list, editor +0xd84; .flp: inside the pattern's events). Add one (Alt+T, FUN_0122b0d0 Tag 0: the
// name dialog, then FUN_00e543b0 type 0): the new marker's index.
int addPatternMarker (Pattern& pattern, const Marker& marker);
// Ctrl+T "Add auto": "Auto", "Auto #2"... at `tick`; a marker already there is kept (its index is returned)
int addPatternAutoMarker (Pattern& pattern, std::int64_t tick);
// Shift+Alt+T (FUN_0122b0d0 Tag 2): a num/den change at `from` (floored to a beat) named "num/den" (FL's "%d/%d"); with a
// time selection ending at `to`, the signature in force before comes back there (unless a change is already there)
void addPatternSignatureChange (Pattern& pattern, std::int64_t from, std::int64_t to, int numerator, int denominator, int beatsPerBar);
// the pattern's signature at `tick`: its last signature marker at or before it, else beatsPerBar / 4
std::pair<int, int> patternSignatureAt (const Pattern& pattern, std::int64_t tick, int beatsPerBar);
// FL's Paint tool in the Piano roll (round 189o; FLEngine 2026 [DECOMP] FUN_00e5cd20: mode 0x11 "mono paint" when
// Shift != General › "Monophonic painting in piano roll", PRPaintMode, off by default): with the option on, a plain
// stroke paints monophonically and Shift paints chords / layers as before.
inline bool monophonicPaint (bool shiftDown, bool monophonicOption) noexcept { return shiftDown != monophonicOption; }

} // namespace lynx::noteedit
