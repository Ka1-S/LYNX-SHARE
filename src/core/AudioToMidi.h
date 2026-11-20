#pragma once

// "Convert to MIDI" (round a2m189b; Ableton Live's Convert Melody / Harmony / Drums to New MIDI Track, done the FL way:
// a new pattern on new channels). The part without JUCE and without the model runtime:
//  - melody and harmony read Spotify's Basic Pitch model (Apache-2.0; a 230 KB ONNX graph run by ui/AudioToMidi with the
//    ONNX Runtime LYNX already ships): its note and onset posteriorgrams are stitched here from overlapping 2 s windows
//    and decoded into notes with Basic Pitch's own note-creation rules (ported from basic_pitch/note_creation.py);
//  - melody keeps one note at a time, the most salient one;
//  - drums are LYNX's own: onsets in three filtered bands (kick, snare, hi-hat), no model;
//  - the result becomes one pattern on new channels, placed on a new Playlist track under the source clip (one undo step).

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::a2m
{
enum class Mode
{
    melody,
    harmony,
    drums
};

// A note's pitch bend (round 190lj): from `seconds` after the note's start it sounds `cents` off its key.
struct Bend
{
    double seconds = 0.0;
    float cents = 0.0f;
    bool operator== (const Bend&) const = default;
};

// A found note, in seconds of the analysed audio.
struct Note
{
    double start = 0.0, end = 0.0;
    int key = 60;
    float velocity = 0.78f; // 0..1
    std::vector<Bend> bends {}; // (round 190lj) where the bend changes; empty = no bend
};

// --- Basic Pitch (ICASSP 2022 model) -----------------------------------------------------------------------------
inline constexpr double modelRate = 22050.0;
inline constexpr int windowSamples = 43844;   // 2 s minus one hop: the graph's fixed input
inline constexpr int hop = 256;               // model frames are 256 samples apart (86 per second)
inline constexpr int overlapFrames = 30;      // windows overlap by 30 frames; half of it is dropped on each side
inline constexpr int overlapSamples = overlapFrames * hop;
inline constexpr int windowStride = windowSamples - overlapSamples;
inline constexpr int keys = 88;               // A0 (MIDI 21) .. C8 (108), one bin per semitone
inline constexpr int lowestKey = 21;
inline constexpr int contourBins = 264;       // the pitch contour output: 3 bins per semitone from A0 (round 190lj)
inline constexpr int contourPerKey = 3;

// The model's outputs over the whole audio, frame-major: value (frame, key bin) at [frame * keys + bin]; the contour
// (only when it was asked for, else empty) at [frame * contourBins + bin].
struct Posteriorgram
{
    int frames = 0;
    std::vector<float> note, onset, contour;
};

inline int numWindows (std::int64_t samples) noexcept
{
    const auto padded = samples + overlapSamples / 2;
    return (int) std::max<std::int64_t> (1, (padded + windowStride - 1) / windowStride);
}

// Runs `model` over 22.05 kHz mono audio and stitches its windows into one posteriorgram, frame k at sample k * hop.
// `model (in, note, onset, framesPerWindow)` gets one window of `windowSamples` (the first starts half an overlap
// before the audio, padded with silence) and fills `framesPerWindow` x `keys` values of each output; false stops.
// `progress` gets 0..1 after each window. False when the model stopped.
bool runModel (const std::vector<float>& mono, int framesPerWindow,
               const std::function<bool (const float* in, float* note, float* onset)>& model,
               const std::function<void (double)>& progress, Posteriorgram& out);

// The same with up to `batch` windows per model call (round 190lj; the graph has a batch axis, the values are the same
// as one at a time): `model (in, count, note, onset, contour)` gets `count` windows
// back to back and fills `count` x framesPerWindow frames of each output; `contour` (framesPerWindow x contourBins per
// window) is null unless `withContour`. The stitched result is the one of runModel above.
bool runModelBatched (const std::vector<float>& mono, int framesPerWindow, int batch, bool withContour,
                      const std::function<bool (const float* in, int count, float* note, float* onset, float* contour)>& model,
                      const std::function<void (double)>& progress, Posteriorgram& out);

// The audio at the model's 22.05 kHz: the windowed sinc of audioedit::resampleChannel (16 zero crossings each side,
// Blackman window, edges continued), with the kernel worked out once per phase when the two rates are whole numbers
// (44.1 / 48 / 88.2 / 96 kHz...: 147 phases or fewer) instead of once per output sample; other rates take
// resampleChannel itself. Empty when `stop` says so.
std::vector<float> toModelRate (const std::vector<float>& mono, double rate, const std::function<bool()>& stop = {});

struct NoteOptions
{
    float onsetThreshold = 0.5f; // Basic Pitch's defaults
    float frameThreshold = 0.3f;
    double minNoteSeconds = 0.1277;
    bool melodiaTrick = true;    // notes without a clear onset, followed from the strongest frame
    bool inferOnsets = true;     // onsets also from sudden rises of the note posteriorgram
    int minKey = 21, maxKey = 108;
    int energyTolerance = 11;    // frames below the threshold a note may span
};

// Basic Pitch's polyphonic note creation (note_creation.py, output_to_notes_polyphonic), on frame k = k * hop / modelRate s
// (shifted by `timeOffset`, the model's latency).
std::vector<Note> notesFromPosteriorgram (const Posteriorgram& pg, const NoteOptions& options, double timeOffset = 0.0);

// One note at a time (Melody): of notes starting together the loudest; a later one ends the last, unless it is a weaker
// note inside a louder one (an overtone). Notes left shorter than `minSeconds` are dropped.
std::vector<Note> monophonic (std::vector<Note> notes, double minSeconds);

// The settings each mode uses (chosen on round a2m189b's measured clips: higher onset and frame thresholds than Basic
// Pitch's defaults; melody keeps notes from 50 ms and the melodia trick, harmony leaves the melodia trick out).
NoteOptions optionsFor (Mode mode) noexcept;

// Melody or harmony notes of a posteriorgram with the mode's settings; melody also moves each note's start back to where
// its key's activation reached half its level (at most 5 frames, never into the previous note).
std::vector<Note> transcribe (const Posteriorgram& pg, Mode mode);

// --- "Convert to MIDI (polyphonic)" with options (round 190lj) ---------------------------------------------------------
// The dialog's settings. Sensitivities are NeuralNote's / Basic Pitch's two knobs turned round: note sensitivity =
// 1 - frame threshold, split sensitivity = 1 - onset threshold (Basic Pitch's defaults 0.7 / 0.5; LYNX's harmony
// settings 0.6 / 0.3, the defaults here).
struct ConvertOptions
{
    bool polyphonic = true;          // false: one note at a time (the Melody mode's decoder)
    float noteSensitivity = 0.6f;    // 0..1
    float splitSensitivity = 0.3f;   // 0..1
    double minNoteMs = 127.7;        // Basic Pitch's 11 frames
    int minKey = lowestKey, maxKey = lowestKey + keys - 1;
    bool pitchBends = false;         // per-note bends from the contour output (Basic Pitch's get_pitch_bends)
    bool quantize = false;           // note starts and ends to the grid (gridTicks)
    std::int64_t gridTicks = 0;      // the project's grid step in ticks (filled by the caller)
    int channel = -1;                // the instrument channel that plays the notes; -1 = a new Synth channel
    bool operator== (const ConvertOptions&) const = default;
};

// The Basic Pitch settings of a dialog's choices (thresholds clamped to 0.05..0.95, keys to the model's 88).
NoteOptions noteOptionsOf (const ConvertOptions& options) noexcept;

// The notes of a posteriorgram with the dialog's settings: Basic Pitch's polyphonic note creation, or the Melody
// decoder (one note at a time, its starts moved back as transcribe does); then the pitch bends when asked for and the
// contour is there.
std::vector<Note> transcribe (const Posteriorgram& pg, const ConvertOptions& options);

// basic_pitch/note_creation.py get_pitch_bends (n_bins_tolerance 25): for each frame of each note the contour bin of
// most weight within ±25 bins (8 1/3 semitones) of its key, weighted by a Gaussian of std 5 bins around it, as cents
// off the key (1/3 semitone = 33.3 cents a bin). `bends` keeps the frames where the value changes (the first frame
// always); a note whose bend stays 0 gets none.
//
// Where a key sits in the contour: Basic Pitch takes bin 3 (key - 21) (midi_pitch_to_contour_bin), but the model
// answers a tone of exactly that key one bin higher (round 190lj: band-limited saw notes at exact pitches, plain and
// vibrato, peak at +1 bin; its bends came out 33 cents sharp, 33.2 cents mean error against 9.2 with no bends at
// all). `measured` (LYNX's default) centres on bin 3 (key - 21) + 1: 3.1 cents; `basicPitch` is the reference's own.
enum class BendCentre
{
    measured,
    basicPitch
};
void addPitchBends (const Posteriorgram& pg, std::vector<Note>& notes, BendCentre centre = BendCentre::measured);

// --- drums (LYNX's own) ----------------------------------------------------------------------------------------------
inline constexpr int kickKey = 36, snareKey = 38, hatKey = 42; // GM / Live's Convert Drums keys (C1, D1, F#1)

struct DrumOptions
{
    float sensitivity = 0.5f; // 0..1: higher finds softer hits
};

// Drum hits of mono audio: kick / snare / closed hi-hat notes (1/16 s long), velocity from the hit's level.
std::vector<Note> drumHits (const float* mono, std::int64_t frames, double sampleRate, const DrumOptions& options = {});

// --- into the song ---------------------------------------------------------------------------------------------------
// Where an audio clip's notes go: the clip's file frames [firstFrame, firstFrame + frames) are analysed; frame f lands at
// clip tick (f - zeroFrame) / framesPerTick - offset (mirrored when it plays backwards).
struct ClipTiming
{
    double sampleRate = 44100.0;  // of the file
    double zeroFrame = 0.0;       // the file frame at the clip's tick 0 before its offset (its played span's start)
    double framesPerTick = 1.0;
    double spanFrames = 0.0;      // the played span's length (for reversed clips)
    bool backwards = false;
};

// The part of the file an audio clip shows, in file frames [first, first + count), clamped to the file.
struct FrameRange
{
    std::int64_t first = 0, count = 0;
};
FrameRange clipFrames (const Clip& clip, const ClipTiming& timing, std::int64_t fileFrames) noexcept;

// The (translated) names of what is added.
struct Names
{
    std::string pattern;
    std::string channel;                 // melody / harmony: the Synth channel
    std::string kick, snare, hat;        // drums: the three channels
};

// A new pattern holding `notes` (seconds), on new channels: melody and harmony one Synth channel, drums Kick, Snare and
// Hat channels (LYNX's built-in drums, one note per hit on the step grid's key, as FL's Channel Rack). `tickOf` gives a
// time's pattern tick; notes outside [0, lengthTicks) are cut or left out (lengthTicks 0: no limit). Returns the new
// pattern's index, or -1 (song untouched) when no note is left.
//
// Round 190lj: `place` (the options dialog) picks the channel instead (an existing channel plays the notes; nothing is
// added to the Channel rack), quantizes the note starts and ends to its grid (a note keeps one grid step at least) and
// puts each note's bends in its pitch curve (core/NoteCurves.h, cents, ticks from the note's start).
struct Placement
{
    int channel = -1;               // -1: new channels as above
    std::int64_t quantizeTicks = 0; // 0: as found
};
int addPattern (Song& song, Mode mode, const std::vector<Note>& notes, const std::function<double (double)>& tickOf,
                std::int64_t lengthTicks, const Names& names, const Placement& place = {});

// The audio clip menu's conversion: the notes (seconds from file frame `analysedFirst`) as a new pattern, played by a new
// pattern clip on a new track right under the audio clip, aligned with it and as long. Run inside AudioEngine::editSong
// (one undo step). Returns the new pattern's index, or -1 (song untouched) when `clipIndex` is no longer an audio clip of
// `sourcePath` or no note falls inside the clip.
int addClipPattern (Song& song, int clipIndex, const std::string& sourcePath, Mode mode, const std::vector<Note>& notes,
                    std::int64_t analysedFirst, const ClipTiming& timing, const Names& names, const Placement& place = {});

// A channel the options dialog offers to play the notes: an instrument (no audio clip channel, no automation clip).
bool canPlayNotes (const ChannelDef& channel) noexcept;
} // namespace lynx::a2m
