#pragma once

// The clip grid as a place to work (round 190bv): recording into slots and moving clips about. Pure song operations
// and the recorder's arithmetic; the launch engine is core/ClipLauncher.h, the engine side src/engine/AudioEngine.
//
// Recording into a slot follows the manual of the session-style program the Ableton-style layout studies (chapter
// "Recording New Clips") and what its program file tells (docs/analise/REA_rodada190bv.md has every statement):
//   - a recording starts on the next point of the global launch quantisation and ends on one ("correctly cut clips");
//   - ended by the clip's launch button it goes straight on into loop playback; ended by a stop it just ends;
//   - a fixed length ends it by itself and the clip plays on (a slot fired with a record length);
//   - notes played into a clip that is already playing are added to it as it loops (overdub).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Recorder.h"
#include "core/ScoreLog.h"
#include "core/Song.h"

namespace lynx::session
{
// --- recording: when ---------------------------------------------------------------------------------------------------
// The next point of a grid of `quant` ticks at or after `now` (quant 0: now itself, on a whole tick).
double nextPoint (double now, double quant) noexcept;
// Where a recording asked for at `now` starts.
inline double recordStart (double now, double quant) noexcept { return nextPoint (now, quant); }
// Where a recording that started at `from` ends when asked to at `now`: the next grid point, at least one grid step
// (or one tick without a grid) after its start. `fixedLength` > 0: exactly that long, whenever it is asked.
double recordEnd (double from, double now, double quant, double fixedLength = 0.0) noexcept;

// --- recording: what ---------------------------------------------------------------------------------------------------
// The notes of a take. `events`: what was played, their `tick` on the launcher's clock. A new clip keeps the notes that
// start in [from − early, until): those up to `early` ticks ahead of the start (a downbeat played a little soon) go on
// its first tick, a note still held at the end is cut there. Starts are quantised to `grid` (<= 1: as played).
// Positions are from the clip's start.
std::vector<Note> takeNotes (const std::vector<RecordedEvent>& events, double from, double until, std::int64_t grid = 0,
                             std::int64_t early = ppq / 8);
// Overdub: the same events as notes of a clip that loops every `length` ticks from `anchor` on: each note lands where
// the loop was when it was played; one held across the loop's end is cut at the end.
std::vector<Note> overdubNotes (const std::vector<RecordedEvent>& events, double anchor, std::int64_t length, std::int64_t grid = 0);
// Adds overdubbed notes to a pattern (sorted; a note that would double one already there, same channel, key and start,
// replaces it).
void mergeNotes (Pattern& pattern, const std::vector<Note>& notes);

// The name of a MIDI clip recorded into a slot (round 190cy; Live 12.4.16, live check 190cw: "rec MIDI 1", "rec MIDI 2"
// on the track "rec MIDI"): the track's name and the first number from 1 that no pattern or slot of the song uses yet.
// `trackName`: the track's name as the grid shows it.
std::string recordedClipName (const Song& song, const std::string& trackName);
// The length counter of a slot that records (round 190cy, Live 12.4.16): `ticks` recorded so far as
// "bars.beats.sixteenths", each counted from 0 ("0.1.0" one beat in).
std::string lengthCounter (double ticks, int beatsPerBar);
// A recorded MIDI clip in (track, scene): a new pattern of `length` ticks with `notes`, named `name` (empty: as a new
// pattern is), and a clip of it in the slot, in the track's colour. Returns the pattern's index (−1: the slot cannot
// take it).
int placeRecordedPattern (Song& song, int track, int scene, std::vector<Note> notes, std::int64_t length, const std::string& name = {});
// A recorded audio clip in (track, scene): a new audio channel for `path` on mixer insert `insert` (≤ 0: a free one),
// named `name`, and a clip of it: `length` ticks from `offset` (+ `offsetFraction`) ticks into the file.
// Returns the channel's index (−1: the slot cannot take it).
int placeRecordedAudio (Song& song, int track, int scene, const std::string& path, const std::string& name, int insert,
                        std::int64_t length, std::int64_t offset, float offsetFraction = 0.0f);
// A new, empty MIDI clip in an empty slot: one bar long. Returns the pattern's index (−1: the slot is taken).
int insertEmptyClip (Song& song, int track, int scene);

// --- clips between slots, and to and from the Playlist ---------------------------------------------------------------
// What a slot holds: the clip and its settings (the clipboard's content).
struct SlotClip
{
    Clip clip;
    ClipGridSlot settings;
};
std::optional<SlotClip> copyOf (const Song& song, int track, int scene);
// Puts a copied clip into (track, scene), replacing what is there; it keeps its name, colour, launch settings, follow
// action and groove.
bool paste (Song& song, int track, int scene, const SlotClip& content);
// Cut: the copy, and the slot emptied.
std::optional<SlotClip> cut (Song& song, int track, int scene);
// Drag: the clip goes to the other slot (what was there is replaced); `copy` leaves the original where it was.
bool moveClip (Song& song, int fromTrack, int fromScene, int toTrack, int toScene, bool copy = false);
// Duplicate: a copy in the slot below (a new scene is added under the last one). Returns the copy's scene, −1: nothing.
int duplicate (Song& song, int track, int scene);

// A slot's clip on the Playlist: a copy on `playlistTrack` at `tick`, one pass long. False: nothing in the slot.
bool toPlaylist (Song& song, int track, int scene, int playlistTrack, std::int64_t tick);
// A copy of a Playlist-style clip (pattern or audio, as copied there) into a slot. False: not a clip a slot can hold.
bool putClip (Song& song, Clip clip, int track, int scene);
// A Playlist clip (pattern or audio; index into the current playlist) into a slot: a copy that starts where the clip
// starts in its source and is as long as the clip. `move` takes it off the Playlist. False: not a clip a slot can hold.
bool fromPlaylist (Song& song, std::size_t playlistIndex, int track, int scene, bool move = false);
// A pattern, or an audio channel already in the project (a file dropped from the Browser, loaded by the caller), into a slot.
bool putPattern (Song& song, int track, int scene, int pattern);
bool putAudio (Song& song, int track, int scene, int channel, std::int64_t length);

// --- scenes (round 190ef; docs/analise/REA_rodada190ef.md) -------------------------------------------------------------
// The reference's Duplicate on a selected scene: a new row under `scene` with a copy of every slot of the row (clips with
// their settings, removed stop buttons) and the scene's name, colour, tempo and bar length. Returns the new row, −1 when
// the grid is full or there is no such scene.
int duplicateScene (Song& song, int scene);
// The reference's "Capture and Insert Scene" (Live 12.4.16, live check 190ei): a new row under `below`.
//   - Clips play (`playing[t]` = the row whose clip track t plays, −1: none): the new row is a plain scene (no name,
//     tempo and signature off) holding copies of the playing clips, and playback moves to it at once, seamlessly: the
//     slots that play take the new row and their copies take the old one (the player follows its slot, so nothing
//     restarts). `moved`, when given, gets the playing slots (ClipGrid::slots index) that now sit in the new row.
//   - Nothing plays: the new row is empty and takes the name, tempo and time signature of the scene `below`.
// Returns the new row, −1 when the grid is full.
int captureScene (Song& song, int below, const std::vector<int>& playing, std::vector<int>* moved = nullptr);

// A scene's own tempo and time signature (the reference's Scene Tempo / Scene Time Signature fields). A value <= 0
// switches the field off (the reference's "Return to Default"); others are clamped to LYNX's tempo range, 1..32 beats
// and the denominators 1, 2, 4, 8, 16. False: no such scene.
constexpr int sceneBeatsMin = 1, sceneBeatsMax = 32;
bool setSceneTempo (Song& song, int scene, double bpm);
bool setSceneBeats (Song& song, int scene, int beats);
bool setSceneSignature (Song& song, int scene, int beats, int unit);
int sceneBeatUnit (int unit) noexcept;                    // the nearest of 1, 2, 4, 8, 16
int sceneBeatUnitStep (int unit, int steps) noexcept;     // `steps` denominators up (+) or down (−) from `unit`
int sceneBarBeats (int beats, int unit) noexcept;         // n/d as LYNX's n/4 meter: the bar's length in quarter notes
std::string sceneTempoText (double bpm);                  // "120.00"
std::string sceneSignatureText (int beats, int unit = 4); // "3 / 4" (the reference's spacing)
// The reference's drag rates on the fields (live check 190ei: 5 px +14 BPM, 10 px +29, 20 px +58, 40 px +112, whole
// BPM; the numerator 4 → 50 in 10 px): the value after a drag of `pixels` (up = positive) from `start`.
constexpr double sceneTempoPerPixel = 2.8, sceneBeatsPerPixel = 4.6, sceneUnitPixelsPerStep = 10.0;
double sceneTempoDragged (double start, double pixels) noexcept;
int sceneBeatsDragged (int start, double pixels) noexcept;
int sceneUnitDragged (int start, double pixels) noexcept;

// Capture MIDI into the clip grid (the Clip-launch layout): the slot that takes the captured clip on `track`: `scene`
// when it is empty, else the first empty one below (a row past the last scene when every one below is taken).
int captureSlot (const Song& song, int track, int scene);
// The captured phrase as the notes of a new clip and its `length` (whole bars, at least one): played while the transport
// ran, the notes keep their place in the bar of the first one; played while stopped, the first starts the clip, the
// timing kept at `bpm`. `nowMs`: notes still held end there.
std::vector<Note> phraseNotes (const std::vector<scorelog::Entry>& phrase, double nowMs, double bpm, int beatsPerBar, std::int64_t& length);

// --- Capture MIDI in the Clip-launch layout (round 190em; Live 12.4.16, live check 190ei) ---------------------------
// Played with the transport stopped, the reference sets the song tempo from the phrase (a s d f a s d f played evenly:
// 148.00). LYNX's own estimate: the onsets (chords merged within 40 ms), the median gap between them refined by a least-
// squares fit to its grid, as a tempo folded into [80, 160) BPM, to 0.01 BPM. Fewer than three onsets: `fallback`.
double phraseTempo (const std::vector<scorelog::Entry>& phrase, double fallback);
// The phrase on the unwrapped timeline of a clip that plays: a note played at song tick T lands at T + `shift` (shift =
// the launcher's clock minus the song position, minus the clip's anchor). Notes still held end at `nowMs`.
std::vector<Note> clipTimeNotes (const std::vector<scorelog::Entry>& phrase, double nowMs, double bpm, double shift);
// Capture into the clip playing in (track, scene) (the reference writes into it, no new clip): its loop content is
// unrolled up to the bars just played, the notes are added there, and the clip's start and loop move to those bars (whole
// bars of `song.beatsPerBar`, from the bar of the first note to the end of the last). A pattern another clip also uses
// is copied first. False: no pattern clip there, or no notes.
bool captureIntoClip (Song& song, int track, int scene, const std::vector<Note>& notes);
// The notes' channel becomes the track's instrument channel (`channel` ≥ 0).
void setNotesChannel (std::vector<Note>& notes, int channel) noexcept;
} // namespace lynx::session
